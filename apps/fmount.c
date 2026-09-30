#include "fnctl.h"
#include "fnsvc.h"
#include "fujinet-nio.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* No sector size by default: NIO identifies known formats itself, and asks
 * (GeometryRequired) for anything it cannot, which the user answers with
 * SS=<bytes>. A fixed default would be a guess for such media. */
#define FMOUNT_SECTOR_SIZE_HINT 0

/* Why NIO refused the mount, for the FN_DISK_ERR_* codes a user can act on. */
static const char *mount_failure(uint8_t error)
{
  switch (error) {
  case FN_DISK_ERR_GEOMETRY_REQUIRED:
    return "NIO cannot tell this image's sector size; add SS=<bytes>";
  case FN_DISK_ERR_INVALID_GEOMETRY: return "sector size does not fit the image";
  case FN_DISK_ERR_FILE_NOT_FOUND:   return "image not found";
  case FN_DISK_ERR_NO_SUCH_FILESYSTEM: return "no such filesystem";
  case FN_DISK_ERR_BAD_IMAGE:        return "image is damaged or mislabelled";
  case FN_DISK_ERR_UNSUPPORTED_TYPE: return "unsupported image type";
  case FN_DISK_ERR_OPEN_FAILED:      return "image could not be opened";
  default:                           return 0;
  }
}
static int fn_stricmp(const char *a, const char *b)
{
  unsigned char ca;
  unsigned char cb;

  do {
    ca = (unsigned char)tolower((unsigned char)*a++);
    cb = (unsigned char)tolower((unsigned char)*b++);
    if (ca != cb)
      return (int)ca - (int)cb;
  } while (ca != 0);
  return 0;
}

static void usage(void)
{
  puts("Usage: FMOUNT slot [drive:] [RO|RW] [SS=bytes]");
  puts("SS= sector size, for images NIO cannot identify");
}

/* A block size in bytes: 128, 256, ... 4096. */
static int parse_sector_size(const char *s, uint16_t *out)
{
  uint16_t v = 0;

  if (!*s)
    return 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > 409)
      return 0;
    v = (uint16_t) (v * 10 + (uint16_t) (*s - '0'));
  }
  if (v < 128 || v > 4096 || (v & (v - 1)) != 0)
    return 0;
  *out = v;
  return 1;
}

static fnsvc_mount_t mount;
#ifdef __ATARI__
static char input_slot[4];
static char input_drive[4];
static char input_mode[4];
static char input_ss[6];
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

static int prompt_args(uint8_t *slot, int *unit, uint8_t *readonly,
                       uint16_t *sector_size)
{
  printf("Slot: ");
  fflush(stdout);
  if (!fgets(input_slot, sizeof(input_slot), stdin))
    input_slot[0] = 0;
  trim_line(input_slot);
  if (!input_slot[0])
    return 0;

  if (!fnsvc_parse_u8(input_slot, slot))
    return 0;
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
    if (fn_stricmp(input_mode, "RO") == 0)
      *readonly = 1;
    else if (fn_stricmp(input_mode, "RW") == 0)
      *readonly = 0;
    else
      return 0;
  }

  printf("Sector size (blank=auto): ");
  fflush(stdout);
  if (!fgets(input_ss, sizeof(input_ss), stdin))
    input_ss[0] = 0;
  trim_line(input_ss);
  if (input_ss[0] && !parse_sector_size(input_ss, sector_size))
    return 0;

  return 1;
}
#endif

int main(int argc, char **argv)
{
  uint8_t slot;
  int unit;
  uint8_t readonly = 0;
  uint16_t sector_size = FMOUNT_SECTOR_SIZE_HINT;
  int argi;

#ifdef __ATARI__
  if (argc == 1) {
    if (!prompt_args(&slot, &unit, &readonly, &sector_size)) {
      usage();
      return 1;
    }
  } else
#endif
  if (argc < 2 || argc > 5 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  } else {
    if (!fnsvc_parse_u8(argv[1], &slot)) {
      puts("Bad slot");
      return 1;
    }
    unit = slot;
    for (argi = 2; argi < argc; argi++) {
      if (isalpha((unsigned char) argv[argi][0]) && argv[argi][1] == ':') {
        unit = drive_to_unit(argv[argi]);
        if (unit < 0) {
          printf("%c: is not a FujiNet drive\n", toupper((unsigned char) argv[argi][0]));
          return 1;
        }
      } else if (fn_stricmp(argv[argi], "RO") == 0) {
        readonly = 1;
      } else if (fn_stricmp(argv[argi], "RW") == 0) {
        readonly = 0;
      } else if (toupper((unsigned char) argv[argi][0]) == 'S' &&
                 toupper((unsigned char) argv[argi][1]) == 'S' &&
                 argv[argi][2] == '=') {
        if (!parse_sector_size(argv[argi] + 3, &sector_size)) {
          puts("Bad sector size (128, 256, 512 ... 4096)");
          return 1;
        }
      } else {
        usage();
        return 1;
      }
    }
  }

  if (unit < 0 || unit >= FNCTL_MAX_UNITS) {
    puts("Bad drive");
    return 1;
  }

  if (!fnsvc_get_mount(slot, &mount) || !mount.enabled || !mount.uri[0]) {
    printf("Slot %u has no image selected\n", (unsigned) slot);
    return 2;
  }

  if (!fnsvc_disk_mount((uint8_t) unit, mount.uri, readonly,
                        sector_size)) {
    {
      const uint8_t error = fnsvc_disk_last_error();
      const char *why = mount_failure(error);
      if (why)
        printf("Disk mount failed: %s (error %u)\n", why, (unsigned) error);
      else
        puts("Disk mount failed");
    }
    return 2;
  }

  if (!fnctl_set_unit_slot((uint8_t) unit, (uint8_t) unit)) {
    puts("Unable to update FujiNet drive mapping");
    return 2;
  }

  {
    int drive = fnctl_find_drive_for_unit((uint8_t) unit);
    printf("Mounted slot %u on %c:\n", (unsigned) slot, drive ? ('A' + drive - 1) : '?');
  }
  return 0;
}
