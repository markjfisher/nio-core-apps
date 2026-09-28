#include "fujinet_disk_iface.h"

#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

#define WB13_WRITEBACK_SETTLE_TICKS 250

static void usage(void)
{
  puts("Usage: FUMOUNT DN0:|...|DN7:|HD0:|...|HD3:|FF0:|...|FF7:");
}

#ifndef __KICK13__
enum {
  HANDLER_ERROR = -1,
  HANDLER_INACTIVE = 0,
  HANDLER_ACTIVE = 1
};
#endif

/* Bare 0-7, or DNx: / dnx: with a required colon and nothing after. */
static int parse_unit(const char *s)
{
  if (!s || !*s)
    return -1;

  if (s[0] >= '0' && s[0] <= '7' && s[1] == '\0')
    return s[0] - '0';

  if ((s[0] == 'D' || s[0] == 'd') &&
      (s[1] == 'N' || s[1] == 'n') &&
      s[2] >= '0' && s[2] <= '7' &&
      s[3] == ':' && s[4] == '\0')
    return s[2] - '0';

#ifdef __KICK13__
  if ((s[0] == 'H' || s[0] == 'h') &&
      (s[1] == 'D' || s[1] == 'd') &&
      s[2] >= '0' && s[2] <= '3' &&
      s[3] == ':' && s[4] == '\0')
    return 4 + s[2] - '0';
  if ((s[0] == 'F' || s[0] == 'f') &&
      (s[1] == 'F' || s[1] == 'f') &&
      s[2] >= '0' && s[2] <= '7' &&
      s[3] == ':' && s[4] == '\0')
    return s[2] - '0';
#endif

  return -1;
}

#ifndef __KICK13__
static int get_handler_state(int unit)
{
  char name[4];
  struct DosList *list;
  struct DosList *entry;
  int state;

  sprintf(name, "DN%d", unit);

  list = LockDosList(LDF_READ | LDF_DEVICES);
  if (!list)
    return HANDLER_ERROR;

  entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
  state = (entry != NULL && entry->dol_Task != NULL)
              ? HANDLER_ACTIVE
              : HANDLER_INACTIVE;
  UnLockDosList(LDF_READ | LDF_DEVICES);
  return state;
}

/*
 * Retirement is dol_Task becoming null (or the DOS node vanishing). DoPkt
 * ACTION_DIE returning -1 with IoErr=0 is not treated as success.
 */
static int wait_handler_retired(int unit)
{
  int tries;
  char name[4];
  struct DosList *list;
  struct DosList *entry;
  BOOL retired;

  sprintf(name, "DN%d", unit);

  for (tries = 0; tries < 20; ++tries) {
    list = LockDosList(LDF_READ | LDF_DEVICES);
    if (!list)
      Delay(1);
    else {
      entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
      retired = entry == NULL || entry->dol_Task == NULL;
      UnLockDosList(LDF_READ | LDF_DEVICES);
      if (retired)
        return 0;
      Delay(1);
    }
  }

  list = LockDosList(LDF_READ | LDF_DEVICES);
  if (!list) {
    fprintf(stderr, "Unable to determine DN%d: handler state\n", unit);
    printf("Unable to determine DN%d: handler state\n", unit);
    return 10;
  }
  entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
  retired = entry == NULL || entry->dol_Task == NULL;
  UnLockDosList(LDF_READ | LDF_DEVICES);
  if (retired)
    return 0;

  fprintf(stderr, "Cannot retire DN%d: handler (busy)\n", unit);
  printf("Cannot retire DN%d: handler (busy)\n", unit);
  return 10;
}
#endif

int main(int argc, char **argv)
{
  int unit;
  int rc = 0;
  char dos_name[5];
  struct MsgPort *port;
  struct IOExtTD *request;
  LONG result;
#ifndef __KICK13__
  struct MsgPort *handler_port;
  LONG err;
  LONG flush_result;
  int handler_state;
#endif

  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 10;
  }

  unit = parse_unit(argv[1]);
  if (unit < 0) {
    usage();
    return 10;
  }

#ifdef __KICK13__
  if ((argv[1][0] == 'H' || argv[1][0] == 'h') &&
      (argv[1][1] == 'D' || argv[1][1] == 'd'))
    sprintf(dos_name, "HD%d:", unit - 4);
  else if ((argv[1][0] == 'F' || argv[1][0] == 'f') &&
           (argv[1][1] == 'F' || argv[1][1] == 'f'))
    sprintf(dos_name, "FF%d:", unit);
  else
#endif
    sprintf(dos_name, "DN%d:", unit);

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
    fprintf(stderr, "Cannot open %s unit %d\n",
            FUJINET_DISK_DEVICE_NAME, unit);
    return 20;
  }

#ifndef __KICK13__
  /*
   * Live handler: FLUSH (fail-safe: no DIE, no eject), then ACTION_DIE
   * until dol_Task is null. DIE failure does not eject. Inactive handler:
   * skip FLUSH/DIE and eject. LockDosList failure is not treated as
   * inactive — do not eject.
   */
  handler_state = get_handler_state(unit);
  if (handler_state == HANDLER_ERROR) {
    fprintf(stderr, "Unable to determine DN%d: handler state\n", unit);
    printf("Unable to determine DN%d: handler state\n", unit);
    rc = 10;
    goto cleanup;
  }

  if (handler_state == HANDLER_ACTIVE) {
    handler_port = DeviceProc((CONST_STRPTR)dos_name);
    if (handler_port == NULL) {
      err = IoErr();
      fprintf(stderr, "Cannot find handler for %s, IoErr=%ld\n",
              dos_name, (long)err);
      rc = 10;
      goto cleanup;
    }

    flush_result = DoPkt(handler_port, ACTION_FLUSH, 0, 0, 0, 0, 0);
    if (!flush_result) {
      err = IoErr();
      fprintf(stderr, "Cannot flush %s, IoErr=%ld\n",
              dos_name, (long)err);
      rc = 10;
      goto cleanup;
    }

    result = DoPkt(handler_port, ACTION_DIE, 0, 0, 0, 0, 0);
    if (!result) {
      err = IoErr();
      fprintf(stderr, "DN%d: ACTION_DIE refused (IoErr=%ld)\n",
              unit, (long)err);
      printf("DN%d: ACTION_DIE refused (IoErr=%ld)\n",
             unit, (long)err);
    }
    rc = wait_handler_retired(unit);
    if (rc != 0)
      goto cleanup;
  }
#else
  /*
   * WB1.3 has one permanent MountList entry per DN unit.  Its file-system
   * handler remains registered across media changes; TD_EJECT is the normal
   * removable-media transition and a later FMOUNT supplies the next disk.
   * Do not use the WB2+ DosList-removal/ACTION_DIE lifecycle here.
   */
  (void)dos_name;
#endif

#ifdef __KICK13__
  /*
   * The 1.3 FFS writes volume metadata asynchronously after a CLI copy
   * returns.  Its handler does not safely accept ACTION_FLUSH/INHIBIT packet
   * control, so allow its normal write-back interval to finish before making
   * removable media absent.  The target's 250 DOS ticks are the five-second
   * write-back interval established by the WB1.3 acceptance test.
   */
  Delay(WB13_WRITEBACK_SETTLE_TICKS);
#endif

  request->iotd_Req.io_Command = TD_EJECT;
  request->iotd_Req.io_Length = 0;
  result = DoIO((struct IORequest *)request);

  if (result != 0) {
    fprintf(stderr, "Eject failed (%ld)\n", result);
    rc = 10;
  } else {
#ifndef __KICK13__
    /* Remove the DosList entry so the device can be unloaded.
     * Do NOT FreeDosEntry — MountList entries are system-managed. */
    char name[4];
    struct DosList *list;
    struct DosList *entry;

    sprintf(name, "DN%d", unit);

    list = LockDosList(LDF_WRITE | LDF_DEVICES);
    if (!list) {
      fprintf(stderr, "Cannot lock DosList for DN%d:\n", unit);
      rc = 10;
    } else {
      entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
      if (!entry) {
        fprintf(stderr, "Cannot find DosList entry for DN%d:\n", unit);
        rc = 10;
      } else {
        RemDosEntry(entry);
      }
      UnLockDosList(LDF_WRITE | LDF_DEVICES);
    }
#endif
  }

#ifndef __KICK13__
cleanup:
#endif
  CloseDevice((struct IORequest *)request);
  DeleteExtIO((struct IORequest *)request);
  DeletePort(port);

  if (rc != 0)
    return rc;

  printf("Ejected %s\n", dos_name);
  return 0;
}
