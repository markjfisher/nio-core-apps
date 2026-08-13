#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void usage(void)
{
  puts("Usage: FUMOUNT drive:");
}

int main(int argc, char **argv)
{
  int unit;

  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 1;
  }

  if (!isalpha((unsigned char) argv[1][0]) || argv[1][1] != ':') {
    usage();
    return 1;
  }

  {
    int drive = toupper((unsigned char) argv[1][0]) - 'A' + 1;
    int found_unit = -1;
    int u;

    for (u = 0; u < FNCTL_MAX_UNITS; u++) {
      if (fnctl_find_drive_for_unit((uint8_t) u) == drive) {
        found_unit = u;
        break;
      }
    }
    if (found_unit < 0) {
      fprintf(stderr, "%c: is not a FujiNet drive\n",
              toupper((unsigned char) argv[1][0]));
      return 1;
    }
    unit = found_unit;
  }

  if (!fnsvc_disk_unmount((uint8_t) unit)) {
    puts("Unmount failed");
    return 2;
  }

  if (!fnctl_set_unit_slot((uint8_t) unit, 0)) {
    puts("Unable to update FujiNet drive mapping");
    return 2;
  }

  printf("Ejected %c:\n", toupper((unsigned char) argv[1][0]));
  return 0;
}
