#include "fujinet_disk_iface.h"

#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <clib/alib_protos.h>
#include <proto/dos.h>
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

  if (!s || !*s)
    return -1;

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

static BOOL has_active_handler(int unit)
{
  char name[4];
  struct DosList *list;
  struct DosList *entry;
  BOOL active = FALSE;

  sprintf(name, "DN%d", unit);

  list = LockDosList(LDF_READ | LDF_DEVICES);
  if (list) {
    entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
    active = entry != NULL && entry->dol_Task != NULL;
    UnLockDosList(LDF_READ | LDF_DEVICES);
  }

  return active;
}

int main(int argc, char **argv)
{
  int unit;
  int rc = 0;
  char dos_name[5];
  struct MsgPort *port;
  struct MsgPort *handler_port;
  struct IOExtTD *request;
  LONG result;
  LONG err;
  LONG flush_result;
  BOOL inhibited = FALSE;

  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 10;
  }

  unit = parse_unit(argv[1]);
  if (unit < 0) {
    usage();
    return 10;
  }

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

  /*
   * If AmigaDOS has a live filesystem handler for this unit, settle any
   * pending filesystem writes and quiesce the handler before making the
   * media unavailable.  The handler is re-enabled after TD_EJECT so it
   * can process the resulting no-media state normally.
   */
  if (has_active_handler(unit)) {
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

    if (!Inhibit((CONST_STRPTR)dos_name, DOSTRUE)) {
      err = IoErr();
      fprintf(stderr, "Cannot inhibit %s, IoErr=%ld\n",
              dos_name, (long)err);
      rc = 10;
      goto cleanup;
    }

    inhibited = TRUE;
  }

  request->iotd_Req.io_Command = TD_EJECT;
  request->iotd_Req.io_Length = 0;
  result = DoIO((struct IORequest *)request);

  /*
   * Once inhibited, always attempt to restore the handler regardless of
   * whether TD_EJECT itself succeeded.
   */
  if (inhibited) {
    if (!Inhibit((CONST_STRPTR)dos_name, DOSFALSE)) {
      err = IoErr();
      fprintf(stderr, "Cannot uninhibit %s, IoErr=%ld\n",
              dos_name, (long)err);
      rc = 10;
    }
    inhibited = FALSE;
  }

  if (result != 0) {
    fprintf(stderr, "Eject failed (%ld)\n", result);
    rc = 10;
  }

cleanup:
  /*
   * Defensive cleanup if a later edit ever introduces an error path after
   * Inhibit(TRUE) but before the normal uninhibit above.
   */
  if (inhibited) {
    if (!Inhibit((CONST_STRPTR)dos_name, DOSFALSE)) {
      err = IoErr();
      fprintf(stderr, "Cannot uninhibit %s, IoErr=%ld\n",
              dos_name, (long)err);
      rc = 10;
    }
  }

  CloseDevice((struct IORequest *)request);
  DeleteExtIO((struct IORequest *)request);
  DeletePort(port);

  if (rc != 0)
    return rc;

  printf("Ejected DN%d:\n", unit);
  return 0;
}