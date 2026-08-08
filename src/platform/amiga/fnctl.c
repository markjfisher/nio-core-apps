#include "fnctl.h"

#include "fn_raw.h"
#include "fujinet-nio.h"

#include <string.h>

#define FNCTL_APP_NS "nio.apps"
#define FNCTL_STATE_NS "fujinet-nio"
#define FNCTL_KEY_URI "current-host"
#define FNCTL_KEY_PATH "current-display-path"

static uint16_t last_error;
static fnctl_state_t state;
static uint8_t unit_slots[FNCTL_MAX_UNITS];
static uint8_t state_initialized;
static uint8_t appstore_buf[320];
static fn_appstore_io_t appstore_io = { appstore_buf, sizeof(appstore_buf) };
static char key_buf[16];

static const char *unit_key(uint8_t unit)
{
  key_buf[0] = 'u'; key_buf[1] = 'n'; key_buf[2] = 'i';
  key_buf[3] = 't'; key_buf[4] = '.';
  key_buf[5] = (char)('0' + (unit % 10)); key_buf[6] = 0;
  return key_buf;
}

static int read_key(const char *key, char *dst, uint16_t cap)
{
  fn_appstore_read_t rr;
  uint8_t result;
  uint16_t n;

  if (cap == 0)
    return 0;
  dst[0] = 0;
  result = fn_appstore_read(&appstore_io, FNCTL_STATE_NS, key, 0,
                            appstore_buf, (uint16_t)(cap - 1), &rr);
  if (result != FN_OK || (rr.flags & FN_APPSTORE_READ_EXISTS) == 0)
    return 0;
  n = rr.bytes_read;
  if (n >= cap)
    n = (uint16_t)(cap - 1);
  memcpy(dst, appstore_buf, n);
  dst[n] = 0;
  return 1;
}

static int write_key(const char *key, const char *value)
{
  fn_appstore_write_t wr;
  uint16_t len = (uint16_t)strlen(value);
  uint8_t result = fn_appstore_write(&appstore_io, FNCTL_STATE_NS, key, 0,
                                     (const uint8_t *)value, len, &wr);
  return result == FN_OK && wr.bytes_written == len;
}

static void ensure_state(void)
{
  uint8_t i;

  if (state_initialized)
    return;
  memset(&state, 0, sizeof(state));
  memcpy(state.signature, FNCTL_SIGNATURE, 4);
  state.version = FNCTL_VERSION;
  state.max_units = FNCTL_MAX_UNITS;
  (void)read_key(FNCTL_KEY_URI, state.current_uri,
                 sizeof(state.current_uri));
  (void)read_key(FNCTL_KEY_PATH, state.display_path,
                 sizeof(state.display_path));
  state.current_uri_len = (uint16_t)strlen(state.current_uri);
  state.display_path_len = (uint16_t)strlen(state.display_path);
  for (i = 0; i < FNCTL_MAX_UNITS; ++i)
    unit_slots[i] = i;
  state_initialized = 1;
}

int fnctl_find_drive(void) { return 1; }

int fnctl_find_drive_for_unit(uint8_t unit)
{
  return unit < FNCTL_MAX_UNITS ? (int)unit + 1 : 0;
}

int fnctl_get_state(fnctl_state_t *out)
{
  if (!out) return 0;
  ensure_state();
  *out = state;
  return 1;
}

int fnctl_set_state(const char *uri, const char *display_path)
{
  size_t uri_len;
  size_t path_len;

  if (!uri || !display_path) return 0;
  uri_len = strlen(uri); path_len = strlen(display_path);
  if (uri_len > FNCTL_MAX_URI || path_len > FNCTL_MAX_PATH) return 0;
  ensure_state();
  memcpy(state.current_uri, uri, uri_len + 1);
  memcpy(state.display_path, display_path, path_len + 1);
  state.current_uri_len = (uint16_t)uri_len;
  state.display_path_len = (uint16_t)path_len;
  return write_key(FNCTL_KEY_URI, uri) && write_key(FNCTL_KEY_PATH, display_path);
}

int fnctl_get_unit_slot(uint8_t unit, uint8_t *slot)
{
  fn_appstore_read_t rr;
  uint8_t result;

  if (!slot || unit >= FNCTL_MAX_UNITS) return 0;
  ensure_state();
  result = fn_appstore_read(&appstore_io, FNCTL_APP_NS, unit_key(unit), 0,
                            appstore_buf, 1, &rr);
  if (result != FN_OK || (rr.flags & FN_APPSTORE_READ_EXISTS) == 0 ||
      rr.bytes_read == 0) {
    *slot = unit_slots[unit];
    return 1;
  }
  *slot = appstore_buf[0];
  return 1;
}

int fnctl_set_unit_slot(uint8_t unit, uint8_t slot)
{
  fn_appstore_write_t wr;
  uint8_t slot_buf[1];
  uint8_t result;

  if (unit >= FNCTL_MAX_UNITS || slot >= FNCTL_MAX_UNITS) return 0;
  ensure_state();
  unit_slots[unit] = slot;
  slot_buf[0] = slot;
  result = fn_appstore_write(&appstore_io, FNCTL_APP_NS, unit_key(unit), 0,
                             slot_buf, 1, &wr);
  return result == FN_OK && wr.bytes_written == 1;
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
  if (result != FN_OK) {
    last_error = result;
    return 0;
  }
  last_error = 0;
  if (nio_status) *nio_status = raw.status;
  if (response_len) *response_len = raw.payload_length;
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

uint16_t fnctl_last_dos_error(void) { return last_error; }
