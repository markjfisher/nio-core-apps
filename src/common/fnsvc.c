#include "fnsvc.h"

#include "fnctl.h"

#include <stddef.h>
#include <string.h>

enum {
  NIO_DEVICEID_FUJI = 0x70,
  NIO_DEVICEID_DISK = 0xFC,
  NIO_DEVICEID_FILE = 0xFE
};

enum {
  NIO_FUJI_GET_MOUNT = 0xFB,
  NIO_FUJI_SET_MOUNT = 0xFC
};

enum {
  NIO_DISK_VERSION = 1,
  NIO_DISK_MOUNT = 0x01,
  NIO_DISK_UNMOUNT = 0x02,
  NIO_DISK_RESTORE_BOOT = 0x0A
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
  if (!fnctl_nio_call(device, command, request, request_len,
                      response, response_capacity, status, response_len)) {
    last_raw_error = (uint8_t) fnctl_last_dos_error();
    return 0;
  }
  last_raw_error = 0;
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
    if (uri_len + FNSVC_LIST_REQUEST_OVERHEAD > sizeof(req_buf))
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

#ifdef CONFIG_NIO_BBC_LITE
int fnsvc_list_directory_page(const char *uri, uint16_t start,
                              uint16_t max_payload, uint8_t max_entries,
                              fnsvc_list_cb cb,
                              void *ctx, uint16_t *next_start,
                              uint8_t *more)
{
  uint16_t uri_len;
  uint8_t status;
  uint16_t resp_len;
  uint16_t off;
  uint16_t count;
  uint16_t entries_len;
  uint16_t pos;
  uint16_t idx;
  uint8_t delivered;
  uint8_t flags;

  if (!uri || !cb || max_entries == 0)
    return fail(FNSVC_ERR_INVALID_ARG);

  uri_len = (uint16_t) strlen(uri);
  last_error = FNSVC_ERR_NONE;
  last_status = 0;
  last_raw_error = 0;
  last_response_len = 0;

  if (max_payload > (uint16_t) (sizeof(resp_buf) - 10))
    max_payload = (uint16_t) (sizeof(resp_buf) - 10);
  /* v1 + uriLen + startIndex + maxPayloadBytes + flags = 6 bytes. */
  if (uri_len + FNSVC_LIST_REQUEST_OVERHEAD > sizeof(req_buf))
    return fail(FNSVC_ERR_REQUEST_TOO_LARGE);

  off = 0;
  req_buf[off++] = NIO_FILE_VERSION;
  put_u16le(&req_buf[off], uri_len);
  off += 2;
  memcpy(&req_buf[off], uri, uri_len);
  off += uri_len;
  put_u16le(&req_buf[off], start);
  off += 2;
  put_u16le(&req_buf[off], max_payload);
  off += 2;
  req_buf[off++] = NIO_FILE_LIST_FLAG_SORT_BY_NAME | NIO_FILE_LIST_FLAG_COMPACT;

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
  delivered = 0;
  for (idx = 0; idx < count; idx++) {
    uint8_t eflags;
    uint8_t name_len;

    if ((uint16_t) (pos + 2) > resp_len)
      return fail(FNSVC_ERR_ENTRY_BOUNDS);
    eflags = resp_buf[pos++];
    name_len = resp_buf[pos++];
    if ((uint16_t) (pos + name_len) > resp_len || name_len >= sizeof(list_name))
      return fail(FNSVC_ERR_ENTRY_BOUNDS);
    memcpy(list_name, &resp_buf[pos], name_len);
    list_name[name_len] = 0;
    pos += name_len;

    if (delivered < max_entries) {
      cb((uint8_t) (eflags & 0x01), list_name, 0, 0, ctx);
      delivered++;
    }
  }

  if (next_start)
    *next_start = (uint16_t) (start + delivered);
  if (more)
    *more = (uint8_t) (delivered < count || (flags & NIO_FILE_LIST_RESP_MORE) != 0);
  return 1;
}
#endif

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

int fnsvc_get_mount(uint8_t slot, fnsvc_mount_t *mount)
{
  uint8_t req[1];
  uint8_t status;
  uint16_t resp_len;
  uint16_t off;
  uint8_t len;

  if (!mount || slot >= FNCTL_MAX_UNITS)
    return 0;
  last_error = FNSVC_ERR_NONE;
  last_status = 0;
  last_raw_error = 0;
  last_response_len = 0;
  zero_bytes(mount, sizeof(*mount));
  req[0] = slot;

  if (!service_call(NIO_DEVICEID_FUJI, NIO_FUJI_GET_MOUNT,
                    req, sizeof(req), resp_buf, sizeof(resp_buf), &status, &resp_len))
    return fail(FNSVC_ERR_TRANSPORT);

  last_status = status;
  last_response_len = resp_len;

  if (status != FNSVC_STATUS_OK)
    return fail(FNSVC_ERR_STATUS);
  if (resp_len < 4)
    return fail(FNSVC_ERR_SHORT_RESPONSE);
  if (resp_buf[0] != slot)
    return fail(FNSVC_ERR_BAD_VERSION);

  mount->enabled = resp_buf[1] & 0x01;
  off = 3;
  len = resp_buf[2];
  if (off + len + 1 > resp_len)
    return fail(FNSVC_ERR_SHORT_RESPONSE);
#if FNSVC_MOUNT_URI_MAX < FNSVC_MAX_URI
  if (len >= sizeof(mount->uri)) {
    memcpy(mount->uri, &resp_buf[off], sizeof(mount->uri) - 1);
    mount->uri[sizeof(mount->uri) - 1] = 0;
  } else {
    memcpy(mount->uri, &resp_buf[off], len);
    mount->uri[len] = 0;
  }
#else
  memcpy(mount->uri, &resp_buf[off], len);
  mount->uri[len] = 0;
#endif
  off += len;
  len = resp_buf[off++];
  if (off + len > resp_len || len >= sizeof(mount->mode))
    return fail(FNSVC_ERR_SHORT_RESPONSE);
  memcpy(mount->mode, &resp_buf[off], len);
  mount->mode[len] = 0;
  return 1;
}

int fnsvc_set_mount(uint8_t slot, const char *uri, const char *mode, uint8_t enabled)
{
  uint8_t status;
  uint16_t resp_len;
  size_t uri_size = strlen(uri ? uri : "");
  size_t mode_size = strlen(mode ? mode : "");
  uint8_t uri_len;
  uint8_t mode_len;
  uint16_t off = 0;

  if (slot >= FNCTL_MAX_UNITS || uri_size > FNSVC_MAX_URI ||
      mode_size > 255 || 4 + uri_size + mode_size > sizeof(req_buf))
    return 0;

  uri_len = (uint8_t) uri_size;
  mode_len = (uint8_t) mode_size;

  req_buf[off++] = slot;
  req_buf[off++] = enabled ? 0x01 : 0x00;
  req_buf[off++] = uri_len;
  if (uri_len) {
    memcpy(&req_buf[off], uri, uri_len);
    off += uri_len;
  }
  req_buf[off++] = mode_len;
  if (mode_len) {
    memcpy(&req_buf[off], mode, mode_len);
    off += mode_len;
  }

  return service_call(NIO_DEVICEID_FUJI, NIO_FUJI_SET_MOUNT,
                      req_buf, off, resp_buf, sizeof(resp_buf), &status, &resp_len) &&
         status == FNSVC_STATUS_OK;
}

int fnsvc_disk_mount(uint8_t slot, const char *uri, uint8_t readonly)
{
  uint8_t status;
  uint16_t resp_len;
  uint16_t uri_len = (uint16_t) strlen(uri);
  uint16_t off = 0;

  if (slot >= FNCTL_MAX_UNITS || uri_len == 0 || 8 + uri_len > sizeof(req_buf))
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
