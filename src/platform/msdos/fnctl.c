#include "fnctl.h"

#include "fn_msdos.h"
#include "fn_raw.h"

#include <ctype.h>
#include <i86.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int cached_drive;
static uint16_t last_dos_error;
static fnctl_query_t query_buf;
static fnctl_unit_map_t unit_map_buf;
static uint8_t state_request_buf[FNCTL_MAX_DATA];

static int valid_sig(const char *sig)
{
  return memcmp(sig, FNCTL_SIGNATURE, 4) == 0;
}

static int ioctl_call(int drive, uint8_t subfunction, void *buffer, uint16_t size)
{
  union REGS regs;
  struct SREGS sregs;

  regs.h.ah = 0x44;
  regs.h.al = subfunction;
  regs.h.bl = (unsigned char) drive;
  regs.w.cx = size;
  regs.x.dx = FP_OFF(buffer);
  sregs.ds = FP_SEG(buffer);

  int86x(0x21, &regs, &regs, &sregs);
  if (regs.x.cflag & INTR_CF) {
    last_dos_error = regs.x.ax;
    return 0;
  }
  last_dos_error = 0;
  return 1;
}

static int ioctl_recv(int drive, void *buffer, uint16_t size)
{
  return ioctl_call(drive, 0x04, buffer, size);
}

static int ioctl_send(int drive, void *buffer, uint16_t size)
{
  return ioctl_call(drive, 0x05, buffer, size);
}

int fnctl_find_drive(void)
{
  int drive;

  if (cached_drive)
    return cached_drive;

  for (drive = 3; drive <= 26; drive++) {
    memset(&query_buf, 0, sizeof(query_buf));
    query_buf.command = FNCTL_QUERY;
    if (ioctl_recv(drive, &query_buf, sizeof(query_buf)) &&
        valid_sig(query_buf.signature)) {
      cached_drive = drive;
      return drive;
    }
  }

  return 0;
}

int fnctl_find_drive_for_unit(uint8_t unit)
{
  int drive;

  for (drive = 3; drive <= 26; drive++) {
    memset(&query_buf, 0, sizeof(query_buf));
    query_buf.command = FNCTL_QUERY;
    if (ioctl_recv(drive, &query_buf, sizeof(query_buf)) &&
        valid_sig(query_buf.signature) &&
        query_buf.unit == unit)
      return drive;
  }

  return 0;
}

int fnctl_get_state(fnctl_state_t *state)
{
  int drive = fnctl_find_drive();
  if (!drive)
    return 0;

  memset(state, 0, sizeof(*state));
  state->command = FNCTL_GET_STATE;
  if (!ioctl_recv(drive, state, sizeof(*state)) || !valid_sig(state->signature))
    return 0;
  return 1;
}

int fnctl_set_state(const char *uri, const char *display_path)
{
  size_t uri_len;
  size_t path_len;
  uint16_t request_len;
  uint8_t status = 0xFF;
  uint16_t response_len = 0;

  uri_len = strlen(uri);
  path_len = strlen(display_path);
  if (uri_len > FNCTL_MAX_URI || path_len > FNCTL_MAX_PATH)
    return 0;

  request_len = (uint16_t) (4 + uri_len + path_len);
  if (request_len > sizeof(state_request_buf))
    return 0;

  state_request_buf[0] = (uint8_t) (uri_len & 0xFF);
  state_request_buf[1] = (uint8_t) ((uri_len >> 8) & 0xFF);
  state_request_buf[2] = (uint8_t) (path_len & 0xFF);
  state_request_buf[3] = (uint8_t) ((path_len >> 8) & 0xFF);
  memcpy(&state_request_buf[4], uri, uri_len);
  memcpy(&state_request_buf[4 + uri_len], display_path, path_len);

  return fnctl_nio_call(0x00, FNCTL_SET_STATE,
                        state_request_buf, request_len,
                        NULL, 0, &status, &response_len) &&
         status == 0;
}

int fnctl_get_unit_slot(uint8_t unit, uint8_t *slot)
{
  int drive = fnctl_find_drive();

  if (!drive)
    return 0;

  memset(&unit_map_buf, 0, sizeof(unit_map_buf));
  unit_map_buf.command = FNCTL_GET_UNIT_MAP;
  unit_map_buf.unit = unit;
  if (!ioctl_recv(drive, &unit_map_buf, sizeof(unit_map_buf)) ||
      !valid_sig(unit_map_buf.signature))
    return 0;

  *slot = unit_map_buf.slot;
  return 1;
}

int fnctl_set_unit_slot(uint8_t unit, uint8_t slot)
{
  int drive = fnctl_find_drive();

  if (!drive)
    return 0;

  memset(&unit_map_buf, 0, sizeof(unit_map_buf));
  unit_map_buf.command = FNCTL_SET_UNIT_MAP;
  unit_map_buf.unit = unit;
  unit_map_buf.slot = slot;
  return ioctl_send(drive, &unit_map_buf, sizeof(unit_map_buf));
}

int fnctl_nio_call(uint8_t device, uint8_t command,
                   const void *request, uint16_t request_len,
                   void *response, uint16_t response_capacity,
                   uint8_t *nio_status, uint16_t *response_len)
{
  fn_raw_response_t raw;
  uint8_t result;

  if (request_len > FNCTL_MAX_DATA || response_capacity > FNCTL_MAX_DATA)
    return 0;

  result = fn_raw_call(device, command, request, request_len,
                       response, response_capacity, &raw);
  if (result != 0) {
    last_dos_error = fn_msdos_ioctl_last_error();
    return 0;
  }
  last_dos_error = 0;
  if (nio_status)
    *nio_status = raw.status;
  if (response_len)
    *response_len = raw.payload_length;
  return 1;
}

const char *fnctl_status_name(uint8_t status)
{
  switch (status) {
  case 0: return "OK";
  case 1: return "DEVICE_NOT_FOUND";
  case 2: return "INVALID_REQUEST";
  case 3: return "DEVICE_BUSY";
  case 4: return "NOT_READY";
  case 5: return "IO_ERROR";
  case 6: return "TIMEOUT";
  case 7: return "INTERNAL_ERROR";
  case 8: return "UNSUPPORTED";
  default: return "UNKNOWN";
  }
}

uint16_t fnctl_last_dos_error(void)
{
  return last_dos_error;
}
