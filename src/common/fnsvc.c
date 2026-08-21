#include "fnsvc.h"

#include "fnctl.h"
#include "fujinet-nio.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum {
  NIO_DEVICEID_DISK = 0xFC,
  NIO_DEVICEID_FILE = 0xFE
};

enum {
  NIO_DISK_VERSION = 1,
  NIO_DISK_MOUNT = 0x01,
  NIO_DISK_UNMOUNT = 0x02,
  NIO_DISK_RESTORE_BOOT = 0x0A,
  NIO_DISK_LIST_MOUNTS = 0x0D
};

enum {
  NIO_FILE_VERSION = 1,
  NIO_FILE_LIST_DIRECTORY = 0x02,
  NIO_FILE_RESOLVE_PATH = 0x05
};

#ifndef FNSVC_IO_BUF_SIZE
#define FNSVC_IO_BUF_SIZE FNCTL_MAX_DATA
#endif

#ifndef FNSVC_LIST_NAME_MAX
#define FNSVC_LIST_NAME_MAX 220
#endif

static uint8_t req_buf[FNSVC_IO_BUF_SIZE];
static uint8_t resp_buf[FNSVC_IO_BUF_SIZE];
static fn_appstore_io_t appstore_io = { req_buf, sizeof(req_buf) };
static char slot_key_buf[9];
static uint8_t last_error;
static uint8_t last_status;
static uint8_t last_raw_error;
static uint16_t last_response_len;
static char list_name[FNSVC_LIST_NAME_MAX + 1];

enum {
  NIO_FILE_LIST_FLAG_COMPACT = 0x01,
  NIO_FILE_LIST_FLAG_SORT_BY_NAME = 0x02,
  NIO_FILE_LIST_RESP_MORE = 0x01,
  NIO_FILE_LIST_RESP_COMPACT = 0x02
};

#define FNSVC_LIST_REQUEST_OVERHEAD 6
#define FNSVC_RESOLVE_PATH_REQUEST_OVERHEAD 5

#ifndef FNSVC_LIST_MAX_PAYLOAD
#define FNSVC_LIST_MAX_PAYLOAD 420
#endif

static void zero_bytes(void *ptr, uint16_t len)
{
  uint8_t *p = (uint8_t *) ptr;
  while (len--)
    *p++ = 0;
}

static int fail(uint8_t error)
{
  last_error = error;
  return 0;
}

static int service_call(uint8_t device, uint8_t command,
                        const void *request, uint16_t request_len,
                        void *response, uint16_t response_capacity,
                        uint8_t *status, uint16_t *response_len)
{
  int ok;
  printf("DBG service_call dev=%02X cmd=%02X req=%u\n",
         (unsigned) device, (unsigned) command, (unsigned) request_len);
  ok = fnctl_nio_call(device, command, request, request_len,
                      response, response_capacity, status, response_len);
  if (!ok) {
    last_raw_error = (uint8_t) fnctl_last_dos_error();
    printf("DBG fnctl_nio_call FAILED raw_err=%u\n", (unsigned) last_raw_error);
    return 0;
  }
  last_raw_error = 0;
  printf("DBG fnctl_nio_call OK status=%u resp_len=%u\n",
         (unsigned) *status, (unsigned) *response_len);
  return 1;
}

static void put_u16le(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t) (value & 0xFF);
  p[1] = (uint8_t) ((value >> 8) & 0xFF);
}

static uint16_t get_u16le(const uint8_t *p)
{
  return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static uint32_t get_u32le(const uint8_t *p)
{
  return (uint32_t) p[0]
      | ((uint32_t) p[1] << 8)
      | ((uint32_t) p[2] << 16)
      | ((uint32_t) p[3] << 24);
}

int fnsvc_list_directory(const char *uri, fnsvc_list_cb cb, void *ctx)
{
  uint16_t uri_len = (uint16_t) strlen(uri);
  uint16_t start = 0;
  uint8_t status;
  uint16_t resp_len;

  printf("DBG fnsvc_list_directory uri='%s' uri_len=%u\n", uri ? uri : "(null)", (unsigned) uri_len);

  if (!cb)
    return fail(FNSVC_ERR_INVALID_ARG);

  last_error = FNSVC_ERR_NONE;
  last_status = 0;
  last_raw_error = 0;
  last_response_len = 0;

  for (;;) {
    uint16_t off = 0;
    uint16_t count;
    uint16_t entries_len;
    uint16_t pos;
    uint16_t idx;
    uint8_t flags;

    /* v1 + uriLen + startIndex + maxPayloadBytes + flags = 6 bytes. */
    if ((size_t) uri_len + FNSVC_LIST_REQUEST_OVERHEAD > sizeof(req_buf))
      return fail(FNSVC_ERR_REQUEST_TOO_LARGE);

    req_buf[off++] = NIO_FILE_VERSION;
    put_u16le(&req_buf[off], uri_len);
    off += 2;
    memcpy(&req_buf[off], uri, uri_len);
    off += uri_len;
    put_u16le(&req_buf[off], start);
    off += 2;
    put_u16le(&req_buf[off], FNSVC_LIST_MAX_PAYLOAD);
    off += 2;
    req_buf[off++] = NIO_FILE_LIST_FLAG_SORT_BY_NAME;

    if (!service_call(NIO_DEVICEID_FILE, NIO_FILE_LIST_DIRECTORY,
                      req_buf, off, resp_buf, sizeof(resp_buf), &status, &resp_len))
      return fail(FNSVC_ERR_TRANSPORT);

    last_status = status;
    last_response_len = resp_len;

    if (status != FNSVC_STATUS_OK)
      return fail(FNSVC_ERR_STATUS);
    if (resp_len < 10)
      return fail(FNSVC_ERR_SHORT_RESPONSE);
    if (resp_buf[0] != NIO_FILE_VERSION)
      return fail(FNSVC_ERR_BAD_VERSION);

    flags = resp_buf[1];
    count = get_u16le(&resp_buf[6]);
    entries_len = get_u16le(&resp_buf[8]);
    if ((uint16_t) (10 + entries_len) > resp_len)
      return fail(FNSVC_ERR_ENTRIES_BOUNDS);

    pos = 10;
    for (idx = 0; idx < count; idx++) {
      uint8_t eflags;
      uint8_t name_len;
      uint32_t size = 0;
      uint32_t mtime = 0;

      if ((uint16_t) (pos + 2) > resp_len)
        return fail(FNSVC_ERR_ENTRY_BOUNDS);
      eflags = resp_buf[pos++];
      name_len = resp_buf[pos++];
      if ((uint16_t) (pos + name_len) > resp_len || name_len >= sizeof(list_name))
        return fail(FNSVC_ERR_ENTRY_BOUNDS);
      memcpy(list_name, &resp_buf[pos], name_len);
      list_name[name_len] = 0;
      pos += name_len;

      if ((flags & NIO_FILE_LIST_RESP_COMPACT) == 0) {
        if ((uint16_t) (pos + 16) > resp_len)
          return fail(FNSVC_ERR_ENTRY_BOUNDS);
        size = get_u32le(&resp_buf[pos]);
        mtime = get_u32le(&resp_buf[pos + 8]);
        pos += 16;
      }

      cb((uint8_t) (eflags & 0x01), list_name, size, mtime, ctx);
    }

    start = (uint16_t) (start + count);
    if ((flags & NIO_FILE_LIST_RESP_MORE) == 0)
      break;
    if (count == 0)
      return fail(FNSVC_ERR_SHORT_RESPONSE);
  }

  return 1;
}

int fnsvc_resolve_path(const char *base_uri, const char *arg,
                       char *resolved_uri, uint16_t resolved_cap,
                       char *display_path, uint16_t display_cap,
                       uint8_t *flags_out)
{
  uint16_t base_len;
  uint16_t arg_len;
  uint8_t status;
  uint16_t resp_len;
  uint16_t off;
  uint16_t len;
  uint8_t flags;

  if (!base_uri || !resolved_uri || resolved_cap == 0)
    return fail(FNSVC_ERR_INVALID_ARG);
  if (!arg)
    arg = "";

  base_len = (uint16_t) strlen(base_uri);
  arg_len = (uint16_t) strlen(arg);
  /* v1 + baseLen + argLen = 5 bytes, excluding the two strings. */
  if ((uint16_t) (base_len + arg_len + FNSVC_RESOLVE_PATH_REQUEST_OVERHEAD) > sizeof(req_buf))
    return fail(FNSVC_ERR_REQUEST_TOO_LARGE);

  last_error = FNSVC_ERR_NONE;
  last_status = 0;
  last_raw_error = 0;
  last_response_len = 0;

  off = 0;
  req_buf[off++] = NIO_FILE_VERSION;
  put_u16le(&req_buf[off], base_len);
  off += 2;
  memcpy(&req_buf[off], base_uri, base_len);
  off += base_len;
  put_u16le(&req_buf[off], arg_len);
  off += 2;
  if (arg_len) {
    memcpy(&req_buf[off], arg, arg_len);
    off += arg_len;
  }

  if (!service_call(NIO_DEVICEID_FILE, NIO_FILE_RESOLVE_PATH,
                    req_buf, off, resp_buf, sizeof(resp_buf), &status, &resp_len))
    return fail(FNSVC_ERR_TRANSPORT);

  last_status = status;
  last_response_len = resp_len;
  if (status != FNSVC_STATUS_OK)
    return fail(FNSVC_ERR_STATUS);
  if (resp_len < 8 || resp_buf[0] != NIO_FILE_VERSION)
    return fail(FNSVC_ERR_BAD_VERSION);

  flags = resp_buf[1];
  off = 4;
  len = get_u16le(&resp_buf[off]);
  off += 2;
  if ((uint16_t) (off + len + 2) > resp_len)
    return fail(FNSVC_ERR_SHORT_RESPONSE);
  if (len >= resolved_cap)
    len = (uint16_t) (resolved_cap - 1);
  memcpy(resolved_uri, &resp_buf[off], len);
  resolved_uri[len] = 0;
  off += get_u16le(&resp_buf[4]);

  len = get_u16le(&resp_buf[off]);
  off += 2;
  if ((uint16_t) (off + len) > resp_len)
    return fail(FNSVC_ERR_SHORT_RESPONSE);
  if (display_path && display_cap > 0) {
    uint16_t copy_len = len;
    if (copy_len >= display_cap)
      copy_len = (uint16_t) (display_cap - 1);
    memcpy(display_path, &resp_buf[off], copy_len);
    display_path[copy_len] = 0;
  }
  if (flags_out)
    *flags_out = flags;
  return 1;
}

uint8_t fnsvc_last_error(void)
{
  return last_error;
}

uint8_t fnsvc_last_status(void)
{
  return last_status;
}

uint8_t fnsvc_last_raw_error(void)
{
  return last_raw_error;
}

uint16_t fnsvc_last_response_len(void)
{
  return last_response_len;
}

int fnsvc_parse_u8(const char *text, uint8_t *value)
{
  uint16_t parsed = 0;

  if (!text || !text[0] || !value)
    return 0;
  while (*text) {
    uint8_t digit;
    if (*text < '0' || *text > '9')
      return 0;
    digit = (uint8_t) (*text++ - '0');
    if (parsed > 25 || (parsed == 25 && digit > 5))
      return 0;
    parsed = (uint16_t) (parsed * 10 + digit);
  }
  *value = (uint8_t) parsed;
  return 1;
}

int fnsvc_get_mount(uint8_t slot, fnsvc_mount_t *mount)
{
  fn_appstore_read_t rr;
  uint8_t result;
  uint16_t uri_len;

  if (!mount)
    return 0;
  zero_bytes(mount, sizeof(*mount));

  slot_key_buf[0] = 's';
  slot_key_buf[1] = 'l';
  slot_key_buf[2] = 'o';
  slot_key_buf[3] = 't';
  slot_key_buf[4] = '-';
  slot_key_buf[5] = (char) ('0' + slot / 100);
  slot_key_buf[6] = (char) ('0' + (slot / 10) % 10);
  slot_key_buf[7] = (char) ('0' + slot % 10);
  slot_key_buf[8] = 0;

  result = fn_appstore_read(&appstore_io, "config-nio", slot_key_buf, 0,
                            resp_buf, sizeof(resp_buf), &rr);
  if (result != FN_OK)
    return fail(FNSVC_ERR_TRANSPORT);
  if ((rr.flags & FN_APPSTORE_READ_EXISTS) == 0)
    return 1;
  if (rr.bytes_read < 3 || resp_buf[0] != 1)
    return fail(FNSVC_ERR_BAD_VERSION);

  uri_len = (uint16_t) (rr.bytes_read - 2);
  if (uri_len >= sizeof(mount->uri))
    uri_len = (uint16_t) (sizeof(mount->uri) - 1);
  mount->enabled = 1;
  strcpy(mount->mode, (resp_buf[1] & 0x01) ? "r" : "rw");
  memcpy(mount->uri, resp_buf + 2, uri_len);
  mount->uri[uri_len] = 0;
  return mount->uri[0] != 0;
}

int fnsvc_set_mount(uint8_t slot, const char *uri, const char *mode, uint8_t enabled)
{
  fn_appstore_delete_t dr;
  fn_appstore_write_t wr;
  uint16_t uri_len;
  uint16_t record_len;

  slot_key_buf[0] = 's';
  slot_key_buf[1] = 'l';
  slot_key_buf[2] = 'o';
  slot_key_buf[3] = 't';
  slot_key_buf[4] = '-';
  slot_key_buf[5] = (char) ('0' + slot / 100);
  slot_key_buf[6] = (char) ('0' + (slot / 10) % 10);
  slot_key_buf[7] = (char) ('0' + slot % 10);
  slot_key_buf[8] = 0;

  if (fn_appstore_delete(&appstore_io, "config-nio", slot_key_buf, &dr) != FN_OK)
    return fail(FNSVC_ERR_TRANSPORT);
  if (!enabled)
    return 1;
  if (!uri || !uri[0])
    return fail(FNSVC_ERR_INVALID_ARG);

  uri_len = (uint16_t) strlen(uri);
  if (uri_len > FNSVC_MAX_URI || (size_t) uri_len + 2 > sizeof(resp_buf))
    return fail(FNSVC_ERR_REQUEST_TOO_LARGE);
  resp_buf[0] = 1;
  resp_buf[1] = (uint8_t) (mode && strcmp(mode, "r") == 0 ? 0x01 : 0x00);
  memcpy(resp_buf + 2, uri, uri_len);
  record_len = (uint16_t) (uri_len + 2);
  if (fn_appstore_write(&appstore_io, "config-nio", slot_key_buf, 0,
                        resp_buf, record_len, &wr) != FN_OK ||
      wr.bytes_written != record_len)
    return fail(FNSVC_ERR_TRANSPORT);
  return 1;
}

int fnsvc_disk_mount(uint8_t slot, const char *uri, uint8_t readonly)
{
  uint8_t status;
  uint16_t resp_len;
  uint16_t uri_len = (uint16_t) strlen(uri);
  uint16_t off = 0;

  if (slot >= FNCTL_MAX_UNITS || uri_len == 0 ||
      (size_t) uri_len + 8 > sizeof(req_buf))
    return 0;

  req_buf[off++] = NIO_DISK_VERSION;
  req_buf[off++] = (uint8_t) (slot + 1);
  req_buf[off++] = readonly ? 0x01 : 0x00;
  req_buf[off++] = 0x00;
  put_u16le(&req_buf[off], 512);
  off += 2;
  put_u16le(&req_buf[off], uri_len);
  off += 2;
  memcpy(&req_buf[off], uri, uri_len);
  off += uri_len;

  return service_call(NIO_DEVICEID_DISK, NIO_DISK_MOUNT,
                      req_buf, off, resp_buf, 32, &status, &resp_len) &&
         status == FNSVC_STATUS_OK;
}

int fnsvc_disk_list_mounts(uint16_t start, char *text, uint16_t text_cap,
                           uint16_t *entry_count, uint8_t *more)
{
  uint8_t status;
  uint16_t resp_len;
  uint16_t entries_len;

  if (!text || text_cap < 2 || !entry_count || !more)
    return 0;

  req_buf[0] = NIO_DISK_VERSION;
  req_buf[1] = 0x01;
  put_u16le(&req_buf[2], 0);
  put_u16le(&req_buf[4], 0);
  put_u16le(&req_buf[6], start);
  put_u16le(&req_buf[8], (uint16_t) (text_cap - 1));

  if (!service_call(NIO_DEVICEID_DISK, NIO_DISK_LIST_MOUNTS,
                    req_buf, 10, resp_buf, sizeof(resp_buf),
                    &status, &resp_len))
    return fail(FNSVC_ERR_TRANSPORT);
  last_status = status;
  last_response_len = resp_len;
  if (status != FNSVC_STATUS_OK)
    return fail(FNSVC_ERR_STATUS);
  if (resp_len < 10 || resp_buf[0] != NIO_DISK_VERSION)
    return fail(FNSVC_ERR_BAD_VERSION);
  if ((resp_buf[1] & 0x02) == 0)
    return fail(FNSVC_ERR_BAD_VERSION);

  *entry_count = get_u16le(&resp_buf[6]);
  entries_len = get_u16le(&resp_buf[8]);
  if ((uint16_t) (10 + entries_len) > resp_len || entries_len >= text_cap)
    return fail(FNSVC_ERR_ENTRIES_BOUNDS);
  memcpy(text, resp_buf + 10, entries_len);
  text[entries_len] = 0;
  *more = (uint8_t) (resp_buf[1] & 0x01);
  return 1;
}

int fnsvc_disk_unmount(uint8_t slot)
{
  uint8_t status;
  uint16_t resp_len;

  if (slot >= FNCTL_MAX_UNITS)
    return 0;
  req_buf[0] = NIO_DISK_VERSION;
  req_buf[1] = (uint8_t) (slot + 1);

  return service_call(NIO_DEVICEID_DISK, NIO_DISK_UNMOUNT,
                      req_buf, 2, resp_buf, 16, &status, &resp_len) &&
         status == FNSVC_STATUS_OK;
}

int fnsvc_disk_restore_boot(uint8_t slot)
{
  uint8_t status;
  uint16_t resp_len;

  if (slot >= FNCTL_MAX_UNITS)
    return 0;
  last_error = FNSVC_ERR_NONE;
  last_status = 0;
  last_raw_error = 0;
  last_response_len = 0;

  req_buf[0] = NIO_DISK_VERSION;
  req_buf[1] = (uint8_t) (slot + 1);

  if (!service_call(NIO_DEVICEID_DISK, NIO_DISK_RESTORE_BOOT,
                    req_buf, 2, resp_buf, 16, &status, &resp_len))
    return fail(FNSVC_ERR_TRANSPORT);

  last_status = status;
  last_response_len = resp_len;

  if (status != FNSVC_STATUS_OK)
    return fail(FNSVC_ERR_STATUS);
  if (resp_len < 12)
    return fail(FNSVC_ERR_SHORT_RESPONSE);
  if (resp_buf[0] != NIO_DISK_VERSION)
    return fail(FNSVC_ERR_BAD_VERSION);
  return 1;
}
