#include "fujinet_disk_iface.h"

#include <devices/trackdisk.h>
#include <exec/io.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

static void usage(void)
{
  puts("Usage: FUMOUNT DN0:|...|DN7:");
}

/* Parse DNx: / dnx: or bare digit 0-7; returns unit 0-7 or -1 on error. */
static int parse_unit(const char *s)
{
  int n;
  if (!s || !*s) return -1;
  if (s[0] >= '0' && s[0] <= '7' && s[1] == '\0') {
    n = s[0] - '0';
    return n <= FUJINET_DISK_MAX_UNIT ? n : -1;
  }
  if ((s[0] == 'D' || s[0] == 'd') &&
      (s[1] == 'N' || s[1] == 'n') &&
      s[2] >= '0' && s[2] <= '7' &&
      (s[3] == ':' || s[3] == '\0')) {
    n = s[2] - '0';
    return n <= FUJINET_DISK_MAX_UNIT ? n : -1;
  }
  return -1;
}

int main(int argc, char **argv)
{
  int unit;
  struct MsgPort *port;
  struct IOExtTD *request;
  LONG result;

  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 10;
  }

  unit = parse_unit(argv[1]);
  if (unit < 0) {
    usage();
    return 10;
  }

  port = CreatePort(NULL, 0);
  if (port == NULL) {
    puts("Cannot create message port");
    return 20;
  }
  request = (struct IOExtTD *)CreateExtIO(port, sizeof(*request));
  if (request == NULL) {
    DeletePort(port);
    puts("Cannot create I/O request");
    return 20;
  }
  if (OpenDevice((CONST_STRPTR)FUJINET_DISK_DEVICE_NAME, (ULONG)unit,
                 (struct IORequest *)request, 0) != 0) {
    DeleteExtIO((struct IORequest *)request);
    DeletePort(port);
    fprintf(stderr, "Cannot open %s unit %d\n", FUJINET_DISK_DEVICE_NAME, unit);
    return 20;
  }

  request->iotd_Req.io_Command = TD_EJECT;
  request->iotd_Req.io_Length = 0;
  result = DoIO((struct IORequest *)request);

  CloseDevice((struct IORequest *)request);
  DeleteExtIO((struct IORequest *)request);
  DeletePort(port);

  if (result != 0) {
    fprintf(stderr, "Eject failed (%ld)\n", result);
    return 10;
  }
  printf("Ejected DN%d:\n", unit);
  return 0;
}
