#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define FHOST_BUILD_ID "host-device-v1"

#define NIO_DEVICEID_HOST 0xF0
#define NIO_HOST_VERSION 1
#define NIO_HOST_GET_CURRENT 0x01
#define NIO_HOST_SET_CURRENT 0x02
#define NIO_HOST_LIST_HISTORY 0x03
#define NIO_HOST_SELECT_HISTORY 0x04
#define NIO_HOST_DELETE_HISTORY 0x05

static uint8_t req_buf[FNCTL_MAX_DATA];
static uint8_t resp_buf[FNCTL_MAX_DATA];
static char text_buf[FNCTL_MAX_DATA];
static char path_buf[FNCTL_MAX_DATA];
#ifdef __ATARI__
static char input_uri[FNSVC_MAX_URI + 1];
#endif

static void usage(void)
{
  puts("FHOST " FHOST_BUILD_ID);
  puts("Usage: FHOST [uri|list|index [D]]");
}

static uint16_t put_u16le(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t) (value & 0xFF);
  p[1] = (uint8_t) ((value >> 8) & 0xFF);
  return 2;
}

static uint16_t get_u16le(const uint8_t *p)
{
  return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static int host_set_current(const char *value)
{
  uint16_t len = (uint16_t) strlen(value);
  uint8_t status = 0xFF;
  uint16_t response_len = 0;
  uint16_t off = 0;

  req_buf[off++] = NIO_HOST_VERSION;
  off += put_u16le(&req_buf[off], len);
  if (off + len > sizeof(req_buf))
    return 0;
  memcpy(&req_buf[off], value, len);
  off += len;

  return fnctl_nio_call(NIO_DEVICEID_HOST, NIO_HOST_SET_CURRENT,
                        req_buf, off, resp_buf, sizeof(resp_buf),
                        &status, &response_len) &&
         status == 0 && response_len >= 1 && resp_buf[0] == NIO_HOST_VERSION;
}

static int host_get_current(char *host, uint16_t host_cap, char *path, uint16_t path_cap)
{
  uint8_t status = 0xFF;
  uint16_t response_len = 0;
  uint16_t host_len;
  uint16_t path_len;

  if (host_cap == 0 || path_cap == 0)
    return 0;
  host[0] = 0;
  path[0] = 0;
  req_buf[0] = NIO_HOST_VERSION;

  if (!fnctl_nio_call(NIO_DEVICEID_HOST, NIO_HOST_GET_CURRENT,
                      req_buf, 1, resp_buf, sizeof(resp_buf),
                      &status, &response_len) ||
      status != 0 || response_len < 5 || resp_buf[0] != NIO_HOST_VERSION) {
    return 0;
  }

  host_len = get_u16le(&resp_buf[1]);
  path_len = get_u16le(&resp_buf[3]);
  if ((uint16_t) (5 + host_len + path_len) > response_len)
    return 0;

  if (host_len >= host_cap)
    host_len = (uint16_t) (host_cap - 1);
  memcpy(host, &resp_buf[5], host_len);
  host[host_len] = 0;

  if (path_len >= path_cap)
    path_len = (uint16_t) (path_cap - 1);
  memcpy(path, &resp_buf[5 + get_u16le(&resp_buf[1])], path_len);
  path[path_len] = 0;
  return 1;
}

static int host_list_history(char *dst, uint16_t cap)
{
  uint8_t status = 0xFF;
  uint16_t response_len = 0;
  uint16_t bytes;
  uint16_t off = 0;

  if (cap == 0)
    return 0;
  dst[0] = 0;
  req_buf[off++] = NIO_HOST_VERSION;
  off += put_u16le(&req_buf[off], 0);
  off += put_u16le(&req_buf[off], (uint16_t) (cap - 1));

  if (!fnctl_nio_call(NIO_DEVICEID_HOST, NIO_HOST_LIST_HISTORY,
                      req_buf, off, resp_buf, sizeof(resp_buf),
                      &status, &response_len) ||
      status != 0 || response_len < 6 || resp_buf[0] != NIO_HOST_VERSION)
    return 0;

  bytes = get_u16le(&resp_buf[4]);
  if (bytes >= cap)
    bytes = (uint16_t) (cap - 1);
  if ((uint16_t) (6 + bytes) > response_len)
    return 0;
  memcpy(dst, &resp_buf[6], bytes);
  dst[bytes] = 0;
  return 1;
}

static int host_index_command(uint8_t command, int index)
{
  uint8_t status = 0xFF;
  uint16_t response_len = 0;

  req_buf[0] = NIO_HOST_VERSION;
  req_buf[1] = (uint8_t) index;
  return fnctl_nio_call(NIO_DEVICEID_HOST, command,
                        req_buf, 2, resp_buf, sizeof(resp_buf),
                        &status, &response_len) &&
         status == 0 && response_len >= 1 && resp_buf[0] == NIO_HOST_VERSION;
}

static int equals_ci(const char *a, const char *b)
{
  while (*a && *b) {
    if (tolower((unsigned char) *a) != tolower((unsigned char) *b))
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int parse_index(const char *s)
{
  int value = 0;
  if (!s || !*s)
    return -1;
  while (*s) {
    if (!isdigit((unsigned char) *s))
      return -1;
    value = value * 10 + (*s - '0');
    s++;
  }
  return value >= 0 && value < 32 ? value : -1;
}

static int show_current(void)
{
  if (!host_get_current(text_buf, sizeof(text_buf), path_buf, sizeof(path_buf)) ||
      text_buf[0] == 0) {
    puts("HOST: (none)");
    puts("PATH: (none)");
    return 0;
  }

  printf("HOST: %s\n", text_buf);
  if (path_buf[0] != 0)
    printf("PATH: %s\n", path_buf);
  else
    puts("PATH: /");
  return 1;
}

#ifdef __ATARI__
static const char *prompt_uri(void)
{
  char *nl;

  printf("URI (blank=keep): ");
  fflush(stdout);
  if (!fgets(input_uri, sizeof(input_uri), stdin))
    input_uri[0] = 0;

  nl = strchr(input_uri, '\n');
  if (nl)
    *nl = 0;
  nl = strchr(input_uri, '\r');
  if (nl)
    *nl = 0;

  return input_uri;
}
#endif

int main(int argc, char **argv)
{
  const char *target_uri;
  int index;

  if (argc > 3 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  }

  if (argc == 2 && equals_ci(argv[1], "list")) {
    if (!host_list_history(text_buf, sizeof(text_buf)) ||
        text_buf[0] == 0) {
      puts("No host history");
      return 0;
    }
    fputs(text_buf, stdout);
    return 0;
  }

  index = argc > 1 ? parse_index(argv[1]) : -1;
  if (index >= 0) {
    if (argc == 3) {
      if (!equals_ci(argv[2], "D")) {
        usage();
        return 1;
      }
      if (!host_index_command(NIO_HOST_DELETE_HISTORY, index)) {
        printf("Unable to delete host history entry (error %u, %s)\n",
               fnctl_last_dos_error(), FHOST_BUILD_ID);
        return 2;
      }
      return 0;
    }

    if (!host_index_command(NIO_HOST_SELECT_HISTORY, index)) {
      printf("Unable to select host history entry (error %u, %s)\n",
             fnctl_last_dos_error(), FHOST_BUILD_ID);
      return 2;
    }
    if (!show_current())
      return 2;
    return 0;
  }

  if (argc == 3) {
    usage();
    return 1;
  }

  if (argc == 1) {
    show_current();
#ifdef __ATARI__
    target_uri = prompt_uri();
    if (!target_uri || !*target_uri)
      return 0;
#else
    return 0;
#endif
  } else {
    target_uri = argv[1];
  }

  if (!host_set_current(target_uri)) {
    printf("Unable to store current host (error %u, %s)\n",
           fnctl_last_dos_error(), FHOST_BUILD_ID);
    return 2;
  }

  if (!show_current()) {
    puts("Stored host could not be verified (" FHOST_BUILD_ID ")");
    return 2;
  }
  return 0;
}
