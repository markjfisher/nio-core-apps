/* FTESTNIO: a screenful of proof that this Atari talks to FujiNet-NIO.
 *
 * Every line below is the answer to a real call through the public
 * fujinet-nio-lib API, so each one went out over SIO and came back from the
 * FujiNet: the native query, the device itself, the clock, Wi-Fi, the mounted
 * disks, a sector read and a directory listing. A failing step prints its
 * FujiNet status code (FN_ERR_* in fujinet-nio.h) instead of stopping the run.
 *
 * Two pages, each ending on a key press (cgetc). It is built as ftestnio.xex and
 * staged on the boot disk as AUTORUN.XEX, which PicoBoot runs.
 */

#include <conio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fnsvc.h"
#include "fujinet-nio.h"

#define COLUMNS      39u   /* the Atari text screen is 40 wide; stay one short so lines never wrap */
#define BOOT_SLOT    1u    /* the boot disk is the first DiskDevice slot */
#define DIR_URI      "flash:/boot/atari"
#define DIR_MAX_SHOWN 5u

static uint8_t steps_ok;
static uint8_t steps_failed;

static fn_fuji_info_t fuji_info;
static fn_wifi_status_t wifi;
static fn_disk_info_t disk;
static char time_text[FN_MAX_TIME_STRING + 1];
static char timezone[FN_MAX_TIMEZONE_LEN + 1];
static char mounts[420];   /* the list-mounts payload limit, as in FDRIVE */
static char shown_name[27];
static uint8_t sector[512];

/* One result line: the step's name, then OK or the FujiNet status code. */
static void report(const char *name, uint8_t status)
{
  printf("%-9s", name);
  if (status == FN_OK) {
    puts("OK");
    ++steps_ok;
  } else {
    printf("ERR $%02x\n", (unsigned) status);
    ++steps_failed;
  }
}

/* fnsvc_* calls return nonzero for success; report them the same way. */
static void report_fnsvc(const char *name, int success)
{
  report(name, success ? FN_OK : (uint8_t) (fnsvc_last_status() ? fnsvc_last_status() : FN_ERR_IO));
}

/* cc65 maps the literal '\n' to the ATASCII end of line ($9B), but the FujiNet's text uses a
 * real line feed. Compare against the byte values, or the line feed prints as a graphic. */
#define LF 0x0a
#define CR 0x0d

/* Print at most `max_lines` lines of `text`, each cut to the screen width. */
static void print_lines(const char *text, uint8_t max_lines)
{
  uint8_t shown = 0;

  while (*text && shown < max_lines) {
    unsigned used = 0;

    fputs("  ", stdout);
    while (*text && *text != LF && *text != CR) {
      if (used < COLUMNS - 2u) {
        putchar(*text);
        ++used;
      }
      ++text;
    }
    putchar('\n');
    while (*text == LF || *text == CR)
      ++text;
    ++shown;
  }
}

static void wait_for_key(const char *prompt)
{
  puts("");
  puts(prompt);
  cgetc();
}

typedef struct {
  uint8_t shown;
  uint8_t total;
} dir_ctx_t;

static void dir_entry(uint8_t is_dir, const char *name, uint32_t size,
                      uint32_t mtime, void *ctx)
{
  dir_ctx_t *dir = (dir_ctx_t *) ctx;

  (void) mtime;
  ++dir->total;
  if (dir->shown >= DIR_MAX_SHOWN)
    return;
  ++dir->shown;
  strncpy(shown_name, name, sizeof(shown_name) - 1u);
  shown_name[sizeof(shown_name) - 1u] = '\0';
  if (is_dir)
    printf("  %s  <dir>\n", shown_name);
  else
    printf("  %s  %lu\n", shown_name, (unsigned long) size);
}

static void page_device(uint8_t init_status)
{
  uint8_t status;
  uint8_t flags;
  uint16_t payload;

  clrscr();
  puts("FTESTNIO - FujiNet-NIO self test");
  puts("");

  report("init", init_status);

  status = fn_atari_query_capabilities();
  report("query", status);
  if (status == FN_OK) {
    /* Keep each in its own variable: printing a uint8_t call result directly can show the
     * previous call's high byte (cc65 leaves X unchanged), e.g. flags $200. */
    payload = fn_atari_max_payload();
    flags = fn_atari_capability_flags();
    printf("  payload %u bytes, flags $%02x\n", (unsigned) payload, (unsigned) flags);
  }

  status = fn_fuji_get_info(&fuji_info);
  report("device", status);
  if (status == FN_OK) {
    fuji_info.profile[COLUMNS - 2u] = '\0';
    printf("  firmware %s\n", fuji_info.firmware);
    printf("  %s\n", fuji_info.profile);
  }

  memset(time_text, 0, sizeof(time_text));
  status = fn_clock_get_format((uint8_t *) time_text, FN_TIME_FORMAT_TZ_ISO);
  report("clock", status);
  if (status == FN_OK)
    printf("  %s\n", time_text);
  memset(timezone, 0, sizeof(timezone));
  if (fn_clock_get_timezone(timezone) == FN_OK && timezone[0]) {
    timezone[COLUMNS - 5u] = '\0';
    printf("  tz %s\n", timezone);
  }

  status = fn_wifi_get_status(&wifi);
  report("wifi", status);
  if (status == FN_OK)
    printf("  link %u  ip %s\n", (unsigned) wifi.link_state, wifi.ip[0] ? wifi.ip : "-");
}

static void page_storage(void)
{
  uint16_t count = 0;
  uint8_t more = 0;
  uint16_t length = 0;
  uint8_t status;
  uint8_t i;
  uint8_t sio_status;
  dir_ctx_t dir;

  clrscr();
  puts("FTESTNIO - disks and files");
  puts("");

  mounts[0] = '\0';
  report_fnsvc("mounts", fnsvc_disk_list_mounts(0, mounts, sizeof(mounts), &count, &more));
  print_lines(mounts, 2);

  status = fn_disk_info(BOOT_SLOT, &disk);
  report("slot 1", status);
  if (status == FN_OK)
    printf("  %s%s type %u, %u x %lu\n", (disk.flags & FN_DISK_FLAG_MOUNTED) ? "mounted" : "empty",
           (disk.flags & FN_DISK_FLAG_READONLY) ? " ro" : "", (unsigned) disk.type,
           (unsigned) disk.sector_size, (unsigned long) disk.sector_count);

  memset(sector, 0, sizeof(sector));
  status = fn_disk_read_sector(BOOT_SLOT, 0, sector, sizeof(sector), &length);
  report("read", status);
  if (status == FN_OK) {
    printf("  sector 0, %u bytes:", (unsigned) length);
    for (i = 0; i < 6 && i < length; ++i)
      printf(" %02x", (unsigned) sector[i]);
    putchar('\n');
  }

  dir.shown = 0;
  dir.total = 0;
  report_fnsvc("files", fnsvc_list_directory(DIR_URI, dir_entry, &dir));
  if (dir.total > dir.shown)
    printf("  ... %u more\n", (unsigned) (dir.total - dir.shown));

  puts("");
  printf("%u ok, %u failed", (unsigned) steps_ok, (unsigned) steps_failed);
  sio_status = fn_atari_last_sio_status();
  printf("   last SIO $%02x\n", (unsigned) sio_status);
}

int main(void)
{
  uint8_t status = fn_init();

  if (status != FN_OK) {
    clrscr();
    puts("FTESTNIO - FujiNet-NIO self test");
    puts("");
    printf("init      ERR $%02x\n", (unsigned) status);
    puts("  cannot start the FujiNet library");
    wait_for_key("Press a key");
    return 1;
  }

  page_device(status);
  wait_for_key("Press a key for disks and files");

  page_storage();
  wait_for_key("Press a key to finish");

  return steps_failed ? 2 : 0;
}
