#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
  puts("Usage: FIN [slot] image-uri-or-path");
}

static fnsvc_mount_t mount;
#ifdef __ATARI__
static char input_slot[4];
static char input_path[FNSVC_MAX_URI + 1];
#endif

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

static void prompt_args(uint8_t *slot, const char **path)
{
  printf("Slot (blank=0): ");
  fflush(stdout);
  if (!fgets(input_slot, sizeof(input_slot), stdin))
    input_slot[0] = 0;
  trim_line(input_slot);

  if (input_slot[0])
    *slot = (uint8_t) atoi(input_slot);

  printf("Image URI/path: ");
  fflush(stdout);
  if (!fgets(input_path, sizeof(input_path), stdin))
    input_path[0] = 0;
  trim_line(input_path);

  *path = input_path;
}
#endif

int main(int argc, char **argv)
{
  uint8_t slot = 0;
  const char *path;

#ifdef __ATARI__
  if (argc == 1) {
    prompt_args(&slot, &path);
  } else
#endif
  if (argc < 2 || argc > 3 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  } else if (argc == 3) {
    slot = (uint8_t) atoi(argv[1]);
    path = argv[2];
  } else {
    path = argv[1];
  }

  if (!path || !*path) {
    usage();
    return 1;
  }

  if (slot >= FNCTL_MAX_UNITS) {
    puts("Bad slot");
    return 1;
  }

  if (!fnsvc_set_mount(slot, path, "rw", 1)) {
    puts("Unable to persist mount slot");
    return 2;
  }

  if (fnsvc_get_mount(slot, &mount) && mount.enabled && mount.uri[0])
    printf("Slot %u: %s\n", (unsigned) slot, mount.uri);
  else
    printf("Slot %u: %s\n", (unsigned) slot, path);
  return 0;
}
