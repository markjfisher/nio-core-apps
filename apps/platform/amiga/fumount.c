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
#ifdef __KICK13__
  puts("Usage: FUMOUNT 0|1|DN0:|DN1:|HN0:|HN1:|DO0:|DO1:|HO0:|HO1:");
#else
  puts("Usage: FUMOUNT 0|...|7");
#endif
}

#ifndef __KICK13__
enum {
  HANDLER_ERROR = -1,
  HANDLER_INACTIVE = 0,
  HANDLER_ACTIVE = 1
};
#endif

#ifndef __KICK13__
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

  return -1;
}
#else
/* WB1.3 endpoint names identify both their static handler and real unit. */
static int parse_endpoint(const char *s, char dos_name[5])
{
  int index;
  char first, second;

  if (!s || !dos_name || !s[0] || !s[1] || s[2] < '0' || s[2] > '1' ||
      !(s[3] == '\0' || (s[3] == ':' && s[4] == '\0')))
    return -1;
  first = s[0] & (char)~0x20;
  second = s[1] & (char)~0x20;
  index = s[2] - '0';
  dos_name[0] = first;
  dos_name[1] = second;
  dos_name[2] = s[2];
  dos_name[3] = ':';
  dos_name[4] = '\0';
  if (first == 'D' && second == 'N')
    return index;
  if (first == 'H' && second == 'N')
    return 2 + index;
  if (first == 'D' && second == 'O')
    return 4 + index;
  if (first == 'H' && second == 'O')
    return 6 + index;
  return -1;
}
static int load_logical_endpoint(int index, char dos_name[5])
{
  static const char *const paths[] = { "T:FNLA", "T:FNLB" };
  BPTR file;
  LONG length;

  file = Open((CONST_STRPTR)paths[index], MODE_OLDFILE);
  if (file == 0)
    return -1;
  length = Read(file, (APTR)dos_name, 4);
  Close(file);
  if (length != 4 || dos_name[3] != ':')
    return -1;
  dos_name[4] = '\0';
  return parse_endpoint(dos_name, dos_name);
}
static void clear_logical_endpoint(int index)
{
  static const char *const paths[] = { "T:FNLA", "T:FNLB" };
  (void)DeleteFile((CONST_STRPTR)paths[index]);
}
static void clear_matching_logical_endpoint(const char *label)
{
  int index;
  char recorded[5];
  for (index = 0; index < 2; ++index) {
    if (load_logical_endpoint(index, recorded) >= 0 &&
        recorded[0] == label[0] && recorded[1] == label[1] &&
        recorded[2] == label[2])
      clear_logical_endpoint(index);
  }
}
#endif

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
  int logical_unit = -1;
  char dos_name[5];
  struct MsgPort *port;
  struct IOExtTD *request;
  LONG result;
#ifndef __KICK13__
  struct MsgPort *handler_port;
  LONG err;
  LONG flush_result;
  int handler_state;
  BOOL inhibited = FALSE;
#endif

  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 10;
  }

#ifdef __KICK13__
  if (argv[1][0] >= '0' && argv[1][0] <= '1' && argv[1][1] == '\0') {
    logical_unit = argv[1][0] - '0';
    unit = load_logical_endpoint(logical_unit, dos_name);
  } else {
    unit = parse_endpoint(argv[1], dos_name);
  }
#else
  unit = parse_unit(argv[1]);
#endif
  if (unit < 0) {
    usage();
    return 10;
  }

#ifndef __KICK13__
    sprintf(dos_name, "DN%d:", unit);
#endif

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
      if (err == ERROR_ACTION_NOT_KNOWN) {
        /*
         * Some filesystem handlers (WB3.1's FFS 40.1, at least) never
         * implemented ACTION_DIE, so it can't tell us whether anything
         * is still using the volume. ACTION_DISK_INFO (what Info() sends)
         * predates ACTION_DIE and reports that regardless: id_InUse covers
         * open files and locks, so it stands in for the busy check
         * ACTION_DIE would otherwise have made. Sent straight to the
         * handler, not via a fresh Lock() — a lock taken just to ask
         * would itself count as "in use" and always report busy.
         */
        struct InfoData info;
        BOOL busy;

        printf("DN%d: handler has no ACTION_DIE (IoErr=%ld); checking activity\n",
               unit, (long)err);
        memset(&info, 0, sizeof(info));
        busy = !DoPkt(handler_port, ACTION_DISK_INFO, MKBADDR(&info), 0, 0, 0, 0) ||
               info.id_InUse != 0;
        if (busy) {
          fprintf(stderr, "Cannot retire DN%d: handler (busy)\n", unit);
          printf("Cannot retire DN%d: handler (busy)\n", unit);
          rc = 10;
          goto cleanup;
        }
        if (!Inhibit((CONST_STRPTR)dos_name, DOSTRUE)) {
          fprintf(stderr, "Cannot inhibit %s\n", dos_name);
          printf("Cannot inhibit %s\n", dos_name);
          rc = 10;
          goto cleanup;
        }
        inhibited = TRUE;
      } else {
        fprintf(stderr, "DN%d: ACTION_DIE refused (IoErr=%ld)\n",
                unit, (long)err);
        printf("DN%d: ACTION_DIE refused (IoErr=%ld)\n",
               unit, (long)err);
        rc = wait_handler_retired(unit);
        if (rc != 0)
          goto cleanup;
      }
    } else {
      rc = wait_handler_retired(unit);
      if (rc != 0)
        goto cleanup;
    }
  }
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

#ifndef __KICK13__
  if (inhibited && result != 0 && !Inhibit((CONST_STRPTR)dos_name, DOSFALSE)) {
    /*
     * Eject failed: the media is still there, so restore normal access.
     * On success, leave the handler inhibited — un-inhibiting it here
     * makes the filesystem revalidate the now-missing disk and raise a
     * "Please replace volume" requester instead of just going away with
     * the DosList entry below.
     */
    fprintf(stderr, "Cannot uninhibit %s\n", dos_name);
  }
#endif

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

#ifdef __KICK13__
  if (logical_unit >= 0)
    clear_logical_endpoint(logical_unit);
  else
    clear_matching_logical_endpoint(dos_name);
#endif

  printf("Ejected %s\n", dos_name);
  return 0;
}
