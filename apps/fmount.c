#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <strings.h>
#define stricmp strcasecmp
#endif

static void usage(void)
{
  puts("Usage: FMOUNT slot [drive:] [RO|RW]");
}

static fnsvc_mount_t mount;
#ifdef __ATARI__
static char input_slot[4];
static char input_drive[4];
static char input_mode[4];
#endif

static int drive_to_unit(const char *s)
{
  int drive;
  int unit;

  if (!s || !isalpha((unsigned char) s[0]))
    return -1;
  drive = toupper((unsigned char) s[0]) - 'A' + 1;
  for (unit = 0; unit < FNCTL_MAX_UNITS; unit++) {
    int found = fnctl_find_drive_for_unit((uint8_t) unit);
    if (found == drive)
      return unit;
  }
  return -1;
}

#ifdef __ATARI__
static void trim_line(char *s)
{
  char *p;

  p = strchr(s, '\n');
  if (p)
    *p = 0;
  p = strchr(s, '\r');
  if (p)
    *p = 0;
}

static int prompt_args(uint8_t *slot, int *unit, uint8_t *readonly)
{
  printf("Slot: ");
  fflush(stdout);
  if (!fgets(input_slot, sizeof(input_slot), stdin))
    input_slot[0] = 0;
  trim_line(input_slot);
  if (!input_slot[0])
    return 0;

  *slot = (uint8_t) atoi(input_slot);
  *unit = *slot;

  printf("Drive (blank=slot): ");
  fflush(stdout);
  if (!fgets(input_drive, sizeof(input_drive), stdin))
    input_drive[0] = 0;
  trim_line(input_drive);
  if (input_drive[0]) {
    *unit = drive_to_unit(input_drive);
    if (*unit < 0) {
      printf("%c: is not a FujiNet drive\n", toupper((unsigned char) input_drive[0]));
      return 0;
    }
  }

  printf("Mode RO/RW (blank=RW): ");
  fflush(stdout);
  if (!fgets(input_mode, sizeof(input_mode), stdin))
    input_mode[0] = 0;
  trim_line(input_mode);
  if (input_mode[0]) {
    if (stricmp(input_mode, "RO") == 0)
      *readonly = 1;
    else if (stricmp(input_mode, "RW") == 0)
      *readonly = 0;
    else
      return 0;
  }

  return 1;
}
#endif

int main(int argc, char **argv)
{
  uint8_t slot;
  int unit;
  uint8_t readonly = 0;
  int argi;

#ifdef __ATARI__
  if (argc == 1) {
    if (!prompt_args(&slot, &unit, &readonly)) {
      usage();
      return 1;
    }
  } else
#endif
  if (argc < 2 || argc > 4 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  } else {
    slot = (uint8_t) atoi(argv[1]);
    unit = slot;
    for (argi = 2; argi < argc; argi++) {
      if (isalpha((unsigned char) argv[argi][0]) && argv[argi][1] == ':') {
        unit = drive_to_unit(argv[argi]);
        if (unit < 0) {
          printf("%c: is not a FujiNet drive\n", toupper((unsigned char) argv[argi][0]));
          return 1;
        }
      } else if (stricmp(argv[argi], "RO") == 0) {
        readonly = 1;
      } else if (stricmp(argv[argi], "RW") == 0) {
        readonly = 0;
      } else {
        usage();
        return 1;
      }
    }
  }

  if (slot >= FNCTL_MAX_UNITS) {
    puts("Bad slot");
    return 1;
  }

  if (!fnsvc_get_mount(slot, &mount) || !mount.enabled || !mount.uri[0]) {
    printf("Slot %u has no image selected\n", (unsigned) slot);
    return 2;
  }

  if (!fnsvc_disk_mount(slot, mount.uri, readonly)) {
    puts("Disk mount failed");
    return 2;
  }

  if (!fnctl_set_unit_slot((uint8_t) unit, slot)) {
    puts("Unable to update FujiNet drive mapping");
    return 2;
  }

  {
    int drive = fnctl_find_drive_for_unit((uint8_t) unit);
    printf("Mounted slot %u on %c:\n", (unsigned) slot, drive ? ('A' + drive - 1) : '?');
  }
  return 0;
}
