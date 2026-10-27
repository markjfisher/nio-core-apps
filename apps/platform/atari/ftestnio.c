/* FTESTNIO: a screenful of proof that this Atari talks to FujiNet-NIO.
 *
 * Every line below is the answer to a real call through the public
 * fujinet-nio-lib API, so each one went out over SIO and came back from the
 * FujiNet: the native query, the device itself, the clock, Wi-Fi, the mounted
 * disks, a sector read and a directory listing. A failing step prints its
 * FujiNet status code (FN_ERR_* in fujinet-nio.h) instead of stopping the run.
 *
 * Two pages, each ending on a key press (cgetc), then a small menu: P sends raw SIO commands to a
 * few device IDs (what an SIO speed driver does about each, see sio.status on the FujiNet), B reads
 * a run of sectors through the native endpoint and shows the time taken. It is built as ftestnio.xex
 * and staged on the boot disk as AUTORUN.XEX, which PicoBoot runs.
 */

#include <atari.h>
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
static char shown_name[27];
static uint8_t sector[512];
/* The list-mounts payload (420 bytes at most, as in FDRIVE) shares the sector buffer: it is printed
 * before the first sector is read. */
#define MOUNTS_MAX 420u
#define mounts ((char *) sector)

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
  report_fnsvc("mounts", fnsvc_disk_list_mounts(0, mounts, MOUNTS_MAX, &count, &more));
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

/* Calls the OS SIO entry with the DCB the caller filled in. */
static void sio_call(void)
{
  __asm__("jsr $E459");
}

typedef struct {
  uint8_t device;
  uint8_t unit;
  uint8_t command;
  uint8_t read_bytes;
  const char *what;
} probe_t;

/* Raw SIO commands, one per device ID. The FujiNet ignores the ones it does not serve, so those end
 * in a timeout; what matters is which frames reach it (sio.status: first frames for other devices)
 * and at what speed. D1: and 0x7f are the controls. */
static const probe_t probes[] = {
  { 0x31, 1, 'S', 4, "D1: status" },
  { 0x7f, 1, 'Q', 4, "0x7f query" },
  { 0x71, 1, 'S', 4, "0x71 u1 (NOS N1:)" },
  { 0x72, 1, 'S', 4, "0x72 u1 (N2:)" },
  { 0x70, 1, 'S', 4, "0x70 u1 (config)" }
};

static void page_probe(void)
{
  uint8_t i;

  clrscr();
  puts("FTESTNIO - SIO probe (about 3 s each)");
  puts("");
  for (i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
    memset(sector, 0, 8);
    OS.dcb.ddevic = probes[i].device;
    OS.dcb.dunit = probes[i].unit;
    OS.dcb.dcomnd = probes[i].command;
    OS.dcb.dstats = 0x40;
    OS.dcb.dbuf = sector;
    OS.dcb.dtimlo = 3;
    OS.dcb.dunuse = 0;
    OS.dcb.dbyt = probes[i].read_bytes;
    OS.dcb.daux = 0;
    sio_call();
    printf("%-18s -> $%02x\n", probes[i].what, (unsigned) OS.dcb.dstats);
  }
  puts("");
  puts("now: sio.status on the FujiNet");
}

/* Toggles the library's high-speed SIO for native calls (the FujiNet needs sio.hsio set). */
static void page_hsio(void)
{
  uint8_t status;

  clrscr();
  puts("FTESTNIO - native high speed");
  puts("");
  if (fn_atari_hsio_index() != 0xFFu) {
    fn_atari_hsio_disable();
    puts("high speed off");
  } else {
    status = fn_atari_hsio_enable();
    if (status == FN_OK)
      printf("high speed on, index %u\n", (unsigned) fn_atari_hsio_index());
    else
      printf("not enabled: ERR $%02x\n  (sio.hsio on the FujiNet?)\n", (unsigned) status);
  }
}

/* The driver's error counts add up across runs; turning it off and on again zeroes them. */
static void hsio_zero_counts(void)
{
  if (fn_atari_hsio_index() != 0xFFu) {
    fn_atari_hsio_disable();
    (void) fn_atari_hsio_enable();
  }
}

#define BENCH_READS 100u

/* Reads BENCH_READS sectors of the boot disk through the native endpoint and times it with the OS
 * clock (a jiffy is 1/50 s on PAL, 1/60 on NTSC). */
static void page_bench(void)
{
  uint16_t i;
  uint16_t length = 0;
  uint16_t start;
  uint16_t end;
  uint16_t ticks;
  uint16_t failed = 0;
  uint32_t bytes = 0;
  uint8_t rate = (get_tv() == AT_PAL) ? 50u : 60u;

  hsio_zero_counts();
  clrscr();
  puts("FTESTNIO - native read benchmark");
  puts("");
  printf("reading %u sectors...\n", BENCH_READS);
  start = ((uint16_t) OS.rtclok[1] << 8) | OS.rtclok[2];
  for (i = 0; i < BENCH_READS; ++i) {
    if (fn_disk_read_sector(BOOT_SLOT, i, sector, sizeof(sector), &length) == FN_OK)
      bytes += length;
    else
      ++failed;
  }
  end = ((uint16_t) OS.rtclok[1] << 8) | OS.rtclok[2];
  ticks = end - start;
  printf("%lu bytes, %u failed\n", (unsigned long) bytes, (unsigned) failed);
  printf("%u jiffies (%u/s)\n", (unsigned) ticks, (unsigned) rate);
  if (ticks) {
    printf("%lu ms, %lu bytes/s\n", (unsigned long) ticks * 1000ul / rate,
           (unsigned long) bytes * rate / ticks);
    printf("%lu ms per read\n", (unsigned long) ticks * 1000ul / rate / BENCH_READS);
  }
  /* What the high-speed driver ran into (it retries an error after the ACK at standard speed). */
  if (fn_atari_hsio_index() != 0xFFu)
    printf("hs errors to%u nak%u frm%u ovr%u chk%u\n", (unsigned) fn_atari_hsio_error_count(0x8A),
           (unsigned) fn_atari_hsio_error_count(0x8B), (unsigned) fn_atari_hsio_error_count(0x8C),
           (unsigned) fn_atari_hsio_error_count(0x8E), (unsigned) fn_atari_hsio_error_count(0x8F));
  puts("");
  puts("now: sio.status on the FujiNet");
}

#define TIMING_CALLS 100u

typedef struct {
  const char *what;
  uint8_t kind;
  uint16_t capacity; /* kind 3: the read's maxBytes */
} timing_t;

/* kind 0: raw OS SIO to a device, 4 byte reply (one command frame, no native protocol);
 * kind 0x80/0x81: the same single frames through the library's SIO entry, so they run at the
 * driver's speed once H is on (a 'Q' and a 3 byte 'P' to 0x7f: one transaction each, no payload);
 * kind 1: fn_fuji_get_info (the native protocol's three transactions, tiny payload);
 * kind 2/3: fn_disk_read_sector with the capacity shown (three transactions plus a sector). */
static const timing_t timings[] = {
  { "OS D1: status", 0x31, 0 },
  { "OS 0x7f query", 0x7f, 0 },
  { "lib 0x7f query", 0x80, 0 },
  { "lib 0x7f poll", 0x81, 0 },
  { "native info", 1, 0 },
  { "native read 128", 3, 128 },
  { "native read 256", 3, 256 },
  { "native read 512", 3, 512 }
};

static uint8_t timing_call(const timing_t *t, uint16_t i)
{
  fn_fuji_info_t info;
  uint16_t length;

  if (t->kind == 0x31 || t->kind == 0x7f) {
    OS.dcb.ddevic = t->kind;
    OS.dcb.dunit = 1;
    OS.dcb.dcomnd = (t->kind == 0x31) ? 'S' : 'Q';
    OS.dcb.dstats = 0x40;
    OS.dcb.dbuf = sector;
    OS.dcb.dtimlo = 3;
    OS.dcb.dunuse = 0;
    OS.dcb.dbyt = 4;
    OS.dcb.daux = 0;
    sio_call();
    return OS.dcb.dstats == 1 ? FN_OK : FN_ERR_IO;
  }
  if (t->kind == 0x80 || t->kind == 0x81) {
    OS.dcb.ddevic = 0x7f;
    OS.dcb.dunit = 1;
    OS.dcb.dcomnd = (t->kind == 0x80) ? 'Q' : 'P';
    OS.dcb.dstats = 0x40;
    OS.dcb.dbuf = sector;
    OS.dcb.dtimlo = 3;
    OS.dcb.dunuse = 0;
    OS.dcb.dbyt = (t->kind == 0x80) ? 4 : 3;
    OS.dcb.daux = 0;
    fn_atari_sio_call();
    return OS.dcb.dstats == 1 ? FN_OK : FN_ERR_IO;
  }
  if (t->kind == 1)
    return fn_fuji_get_info(&info);
  return fn_disk_read_sector(BOOT_SLOT, i & 0x0Fu, sector, t->capacity, &length);
}

/* Times TIMING_CALLS of each call type, so the fixed cost of a transaction can be separated from
 * the cost of the data in it. Run it with high speed off, then on, to compare. */
static void page_timing(void)
{
  uint8_t k;
  uint16_t i;
  uint16_t start;
  uint16_t ticks;
  uint16_t failed;
  uint32_t tenths;
  uint8_t rate = (get_tv() == AT_PAL) ? 50u : 60u;

  hsio_zero_counts();
  clrscr();
  printf("FTESTNIO - timing, hs %s\n\n", fn_atari_hsio_index() == 0xFFu ? "off" : "on");
  for (k = 0; k < sizeof(timings) / sizeof(timings[0]); ++k) {
    failed = 0;
    start = ((uint16_t) OS.rtclok[1] << 8) | OS.rtclok[2];
    for (i = 0; i < TIMING_CALLS; ++i)
      if (timing_call(&timings[k], i) != FN_OK)
        ++failed;
    ticks = (((uint16_t) OS.rtclok[1] << 8) | OS.rtclok[2]) - start;
    tenths = (uint32_t) ticks * 10000ul / rate / TIMING_CALLS;
    printf("%-16s %3lu.%lu ms  fail %u\n", timings[k].what, tenths / 10, tenths % 10, (unsigned) failed);
  }
  if (fn_atari_hsio_index() != 0xFFu)
    printf("hs errors to%u nak%u frm%u ovr%u chk%u\n", (unsigned) fn_atari_hsio_error_count(0x8A),
           (unsigned) fn_atari_hsio_error_count(0x8B), (unsigned) fn_atari_hsio_error_count(0x8C),
           (unsigned) fn_atari_hsio_error_count(0x8E), (unsigned) fn_atari_hsio_error_count(0x8F));
  puts("");
  puts("now: sio.status on the FujiNet");
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
  for (;;) {
    char key;

    puts("");
    printf("P probe  B bench  T timing  H high speed (%s)\n", fn_atari_hsio_index() == 0xFFu ? "off" : "on");
    puts("other: finish");
    key = cgetc();
    if (key == 'p' || key == 'P')
      page_probe();
    else if (key == 'b' || key == 'B')
      page_bench();
    else if (key == 't' || key == 'T')
      page_timing();
    else if (key == 'h' || key == 'H')
      page_hsio();
    else
      break;
  }

  return steps_failed ? 2 : 0;
}
