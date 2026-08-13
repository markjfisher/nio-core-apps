#include "fujinet_disk_iface.h"

#include <devices/trackdisk.h>
#include <exec/io.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

static void usage(void)
{
  puts("Usage: FMOUNT slot [DN0:|...|DN7:] [RO|RW]");
}

/* Parse DNx: / dnx: or bare digit 0-7; returns unit 0-7 or -1 on error. */
static int drive_to_unit(const char *s)
{
  int n;
  if (!s || !*s) return -1;
  /* Accept bare digit: "0".."7" */
  if (s[0] >= '0' && s[0] <= '7' && s[1] == '\0') {
    n = s[0] - '0';
    return n <= FUJINET_DISK_MAX_UNIT ? n : -1;
  }
  /* Accept DNx: or DNx (with or without trailing colon) */
  if ((s[0] == 'D' || s[0] == 'd') &&
      (s[1] == 'N' || s[1] == 'n') &&
      s[2] >= '0' && s[2] <= '7' &&
      (s[3] == ':' || s[3] == '\0')) {
    n = s[2] - '0';
    return n <= FUJINET_DISK_MAX_UNIT ? n : -1;
  }
  return -1;
}

static int is_ro(const char *s)
{
  return (s[0]=='R'||s[0]=='r') && (s[1]=='O'||s[1]=='o') && s[2]=='\0';
}
static int is_rw(const char *s)
{
  return (s[0]=='R'||s[0]=='r') && (s[1]=='W'||s[1]=='w') && s[2]=='\0';
}

int main(int argc, char **argv)
{
  uint8_t slot;
  int unit;
  uint8_t readonly = 0;
  int argi;
  long slot_val;

  {
    int _i;
    FILE *_dbg = fopen("DH0:fmount-debug.txt", "w");
    if (_dbg) {
      fprintf(_dbg, "argc=%d\n", argc);
      for (_i = 0; _i < argc; _i++)
        fprintf(_dbg, "argv[%d]=[%s] len=%d\n", _i, argv[_i], (int)strlen(argv[_i]));
      fclose(_dbg);
    }
  }
  if (argc < 2 || argc > 4 || (argc > 1 && argv[1][0] == '?')) {
    { FILE *_d = fopen("DH0:fmount-debug.txt", "a"); if (_d) { fprintf(_d, "USAGE@top argc=%d\n", argc); fclose(_d); } }
    usage();
    return 10;
  }

  slot_val = 0;
  {
    const char *p = argv[1];
    if (*p < '0' || *p > '9') { puts("Bad slot"); return 10; }
    while (*p >= '0' && *p <= '9') slot_val = slot_val * 10 + (*p++ - '0');
    if (*p != '\0' || slot_val > 255) { puts("Bad slot"); return 10; }
  }
  slot = (uint8_t)slot_val;
  unit = (int)slot;

  for (argi = 2; argi < argc; argi++) {
    if (drive_to_unit(argv[argi]) >= 0) {
      unit = drive_to_unit(argv[argi]);
    } else if (is_ro(argv[argi])) {
      readonly = 1;
    } else if (is_rw(argv[argi])) {
      readonly = 0;
    } else {
      { FILE *_d = fopen("DH0:fmount-debug.txt", "a"); if (_d) { fprintf(_d, "USAGE@loop argi=%d arg=[%s] dtu=%d\n", argi, argv[argi], drive_to_unit(argv[argi])); fclose(_d); } }
      usage();
      return 10;
    }
  }

  if (unit < 0 || unit > FUJINET_DISK_MAX_UNIT) {
    puts("Bad drive");
    return 10;
  }

  {
    struct MsgPort *port;
    struct IOExtTD *request;
    struct fujinet_disk_catalog_mount catalog;
    LONG result;

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

    catalog.catalog_slot = slot;
    catalog.writable = (UBYTE)(readonly ? 0 : 1);
    request->iotd_Req.io_Command = FUJINET_DISK_CMD_MOUNT_CATALOG;
    request->iotd_Req.io_Data = &catalog;
    request->iotd_Req.io_Length = (ULONG)sizeof(catalog);
    result = DoIO((struct IORequest *)request);

    CloseDevice((struct IORequest *)request);
    DeleteExtIO((struct IORequest *)request);
    DeletePort(port);

    if (result != 0) {
      fprintf(stderr, "Mount failed (%ld)\n", result);
      return 10;
    }
    printf("Mounted slot %u on DN%d:\n", (unsigned) slot, unit);
  }
  return 0;
}
