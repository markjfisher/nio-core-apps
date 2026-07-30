#include "fnctl.h"
#include "fnsvc.h"

#include <ctype.h>
#include <stdio.h>
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

static int prompt_args(uint8_t *slot, const char **path)
{
  printf("Slot (blank=0): ");
  fflush(stdout);
  if (!fgets(input_slot, sizeof(input_slot), stdin))
    input_slot[0] = 0;
  trim_line(input_slot);

  if (input_slot[0] && !fnsvc_parse_u8(input_slot, slot))
    return 0;

  printf("Image URI/path: ");
  fflush(stdout);
  if (!fgets(input_path, sizeof(input_path), stdin))
    input_path[0] = 0;
  trim_line(input_path);

  *path = input_path;
  return 1;
}
#endif

int main(int argc, char **argv)
{
  uint8_t slot = 0;
  const char *path;

#ifdef __ATARI__
  if (argc == 1) {
    if (!prompt_args(&slot, &path)) {
      puts("Bad slot");
      return 1;
    }
  } else
#endif
  if (argc < 2 || argc > 3 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  } else if (argc == 3) {
    if (!fnsvc_parse_u8(argv[1], &slot)) {
      puts("Bad slot");
      return 1;
    }
    path = argv[2];
  } else {
    path = argv[1];
  }

  if (!path || !*path) {
    usage();
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
