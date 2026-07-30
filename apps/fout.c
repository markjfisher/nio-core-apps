#include "fnctl.h"
#include "fnsvc.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
  puts("Usage: FOUT slot");
}

#ifdef __ATARI__
static char input_slot[4];

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

static int prompt_args(uint8_t *slot)
{
  printf("Slot: ");
  fflush(stdout);
  if (!fgets(input_slot, sizeof(input_slot), stdin))
    input_slot[0] = 0;
  trim_line(input_slot);
  if (!input_slot[0])
    return 0;

  return fnsvc_parse_u8(input_slot, slot);
}
#endif

int main(int argc, char **argv)
{
  uint8_t slot;

#ifdef __ATARI__
  if (argc == 1) {
    if (!prompt_args(&slot)) {
      usage();
      return 1;
    }
  } else
#endif
  if (argc != 2 || argv[1][0] == '?') {
    usage();
    return 1;
  } else {
    if (!fnsvc_parse_u8(argv[1], &slot)) {
      puts("Bad slot");
      return 1;
    }
  }

  if (!fnsvc_set_mount(slot, "", "r", 0)) {
    puts("Unable to clear mount slot");
    return 2;
  }

  printf("Slot %u cleared\n", (unsigned) slot);
  return 0;
}
