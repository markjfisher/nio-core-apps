/* U1MBINFO: does the U1MB's HSIO driver register a device, and when?
 *
 * No FujiNet library calls and no other traffic: the program shows the U1MB's HSIO tables, then
 * sends one raw SIO command at a time through the OS (so through the U1MB's handler) and shows the
 * tables again after each, so the command that first creates an entry for a device is visible.
 * Run `sio.status` on the FujiNet afterwards for what reached it.
 *
 * The tables are the U1MB's own (undocumented): the PBI ROM selected by writing its ID bit to $D1FF
 * shows "S3", "IC" or "XU" at $D800; $D174+n is disk n+1's entry (0 = never polled, else the divisor
 * plus 1) and $D184..$D187 / $D188..$D18B are up to four non-disk device IDs and their entries.
 */

#include <atari.h>
#include <conio.h>
#include <stdint.h>
#include <stdio.h>

#define TABLE_ADDRESS 0xD174u
#define DISKS         15u
#define DEVICES       4u

typedef struct {
  uint8_t device;
  uint8_t command;
  uint8_t stats;      /* DSTATS: $40 reads from the device, $80 writes to it, 0 neither */
  uint8_t bytes;
  const char *what;
} step_t;

/* With a U1MB BIOS that polls every device ($3F first, then the other detection commands), $7F is
 * the one the FujiNet answers: a Q (our query), a status, a repeat of the Q (now at whatever speed was
 * agreed), and $70 for comparison (nothing answers it, so the whole detection sequence times out).
 * sio.status on the FujiNet shows "3F asked: ... 0x7f n" and the first frames for other devices. */
static const step_t steps[] = {
  { 0x7f, 'Q', 0x40, 4, "7F Q query" },
  { 0x7f, 'Q', 0x40, 4, "7F Q again" },
  { 0x7f, 'S', 0x40, 4, "7F S status" },
  { 0x70, 'S', 0x40, 4, "70 S status" }
};

static uint8_t buffer[8];

static void pair(uint8_t a, uint8_t b)
{
  printf(" %02x:%02x", (unsigned) a, (unsigned) b);
}

static void dump_tables(void)
{
  static uint8_t table[DISKS + 1 + DEVICES * 2];
  uint8_t sigs[16];
  uint8_t i;
  uint8_t sel = 0;

  for (i = 0; i < 8 && !sel; ++i) {
    *(volatile uint8_t *) 0xD1FF = 0x80 >> i;
    sigs[i * 2] = *(volatile uint8_t *) 0xD800;
    sigs[i * 2 + 1] = *(volatile uint8_t *) 0xD801;
    if ((sigs[i * 2] == 'X' && sigs[i * 2 + 1] == 'U') || (sigs[i * 2] == 'I' && sigs[i * 2 + 1] == 'C') ||
        (sigs[i * 2] == 'S' && sigs[i * 2 + 1] == '3')) {
      for (sel = 0; sel < sizeof(table); ++sel)
        table[sel] = *(volatile uint8_t *) (TABLE_ADDRESS + sel);
      sel = 0x80 >> i;
    }
  }
  *(volatile uint8_t *) 0xD1FF = 0;

  if (!sel) {
    printf("no ROM; $D800/1 per select:");
    for (i = 0; i < 8; ++i)
      pair(sigs[i * 2], sigs[i * 2 + 1]);
    puts("");
    return;
  }
  printf("PBI $%02x disk:", (unsigned) sel);
  for (i = 0; i < DISKS; ++i)
    if (table[i])
      pair(i + 1, table[i]);
  printf("\n dev:");
  for (i = 0; i < DEVICES; ++i)
    pair(table[16 + i], table[20 + i]);
  puts("");
}

static void send(const step_t *s)
{
  buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0;
  OS.dcb.ddevic = s->device;
  OS.dcb.dunit = 1;
  OS.dcb.dcomnd = s->command;
  OS.dcb.dstats = s->stats;
  OS.dcb.dbuf = buffer;
  OS.dcb.dtimlo = 3;
  OS.dcb.dunuse = 0;
  OS.dcb.dbyt = s->bytes;
  OS.dcb.daux = 0;
  __asm__("jsr $E459");
}

int main(void)
{
  uint8_t i;

  clrscr();
  puts("U1MBINFO (no FujiNet calls)");
  puts("start:");
  dump_tables();
  for (i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
    printf("key: send %s\n", steps[i].what);
    cgetc();
    send(&steps[i]);
    printf("-> $%02x reply", (unsigned) OS.dcb.dstats);
    pair(buffer[0], buffer[1]);
    pair(buffer[2], buffer[3]);
    puts("");
    dump_tables();
  }
  puts("done (key to exit); sio.status on the FujiNet");
  cgetc();
  return 0;
}
