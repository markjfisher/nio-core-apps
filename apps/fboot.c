#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>

static void usage(void)
{
  puts("Usage: FBOOT [drive:]");
}

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

int main(int argc, char **argv)
{
  int unit = 0;
  int drive;

  if (argc > 2 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  }

  if (argc == 2) {
    if (!isalpha((unsigned char) argv[1][0]) || argv[1][1] != ':') {
      usage();
      return 1;
    }
    unit = drive_to_unit(argv[1]);
    if (unit < 0) {
      printf("%c: is not a FujiNet drive\n", toupper((unsigned char) argv[1][0]));
      return 1;
    }
  }

  if (!fnsvc_disk_restore_boot((uint8_t) unit)) {
    puts("Boot disk restore failed");
    printf("status=%s(%u) err=%u raw=%u\n",
           fnctl_status_name(fnsvc_last_status()),
           (unsigned) fnsvc_last_status(),
           (unsigned) fnsvc_last_error(),
           (unsigned) fnsvc_last_raw_error());
    return 2;
  }

  if (!fnctl_set_unit_slot((uint8_t) unit, (uint8_t) unit)) {
    puts("Unable to update FujiNet drive mapping");
    return 2;
  }

  drive = fnctl_find_drive_for_unit((uint8_t) unit);
  printf("Restored boot disk on %c:\n", drive ? ('A' + drive - 1) : '?');
  return 0;
}
