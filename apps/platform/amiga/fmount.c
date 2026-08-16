#include "fujinet_disk_iface.h"

#include <devices/trackdisk.h>
#include <exec/io.h>
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <proto/dos.h>
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

static int is_ro(const char *s)
{
  return (s[0]=='R'||s[0]=='r') && (s[1]=='O'||s[1]=='o') && s[2]=='\0';
}
static int is_rw(const char *s)
{
  return (s[0]=='R'||s[0]=='r') && (s[1]=='W'||s[1]=='w') && s[2]=='\0';
}

static BOOL has_active_dos_handler(int unit)
{
  char name[4];
  struct DosList *list;
  struct DosList *entry;
  BOOL active = FALSE;

  sprintf(name, "DN%d", unit);
  list = LockDosList(LDF_READ | LDF_DEVICES);
  if (list != NULL) {
    entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
    active = entry != NULL && entry->dol_Task != NULL;
    UnLockDosList(LDF_READ | LDF_DEVICES);
  }
  return active;
}

int main(int argc, char **argv)
{
  uint8_t slot;
  int unit;
  uint8_t readonly = 0;
  int argi;
  long slot_val;

  if (argc < 2 || argc > 4 || (argc > 1 && argv[1][0] == '?')) {
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
    char dos_name[5];
    BOOL previously_inhibited = FALSE;
    LONG result;
    LONG uninhibit_result;

    sprintf(dos_name, "DN%d:", unit);
    if (has_active_dos_handler(unit)) {
      if (!Inhibit((CONST_STRPTR)dos_name, DOSTRUE)) {
        fprintf(stderr, "Cannot inhibit %s (IoErr=%ld)\n", dos_name,
                (long)IoErr());
        return 30;
      }
      previously_inhibited = TRUE;
    }

    port = CreatePort(NULL, 0);
    if (port == NULL) {
      if (previously_inhibited)
        Inhibit((CONST_STRPTR)dos_name, DOSFALSE);
      puts("Cannot create message port");
      return 20;
    }
    request = (struct IOExtTD *)CreateExtIO(port, sizeof(*request));
    if (request == NULL) {
      DeletePort(port);
      if (previously_inhibited)
        Inhibit((CONST_STRPTR)dos_name, DOSFALSE);
      puts("Cannot create I/O request");
      return 20;
    }
    if (OpenDevice((CONST_STRPTR)FUJINET_DISK_DEVICE_NAME, (ULONG)unit,
                   (struct IORequest *)request, 0) != 0) {
      DeleteExtIO((struct IORequest *)request);
      DeletePort(port);
      if (previously_inhibited)
        Inhibit((CONST_STRPTR)dos_name, DOSFALSE);
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

    if (previously_inhibited) {
      uninhibit_result = Inhibit((CONST_STRPTR)dos_name, DOSFALSE);
      if (result == 0 && !uninhibit_result) {
        fprintf(stderr, "Mounted slot %u on DN%d, but cannot uninhibit %s (IoErr=%ld)\n",
                (unsigned)slot, unit, dos_name, (long)IoErr());
        return 31;
      }
      if (result != 0 && !uninhibit_result) {
        fprintf(stderr, "Replacement failed (%ld); cannot uninhibit %s (IoErr=%ld)\n",
                (long)result, dos_name, (long)IoErr());
      }
    }

    if (result != 0) {
      fprintf(stderr, "Mount failed (%ld)\n", result);
      return 10;
    }
    printf("Mounted slot %u on DN%d:\n", (unsigned)slot, unit);
  }
  return 0;
}
