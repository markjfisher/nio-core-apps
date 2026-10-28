/* PROCTEST: does the FujiNet's PROC (proceed) line reach the Atari?
 *
 * Run it, then use the FujiNet console to drive the line: `sio.proc pulse 5` (five pulses of 10 ms).
 * It checks the two layers separately, so a failure says where:
 *
 *   P  poll the PIA's CA1 flag (PACTL bit 7) from the main loop, no interrupt involved. It counts when
 *      the wire, the pin and the PIA are right.
 *   I  the library's interrupt (fn_atari_proc_enable): the PIA raises the OS PROCEED interrupt and the
 *      handler at VPRCED counts it. It counts when the interrupt is delivered as well.
 *
 * T  the real thing: open a TCP session to a host you type, and read whatever it sends, either only when
 *    PROC says so or by trying every time round the loop, counting the SIO reads each way costs. It needs
 *    `sio.tune proc 1` on the FujiNet and something to talk to, for example `nc -l 7000` on a PC and type
 *    lines into it.
 *
 * P and I make no FujiNet library call other than the PROC ones, so nothing else is on the SIO bus.
 */

#include <atari.h>
#include <conio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fujinet-nio.h"

/* The PIA registers, volatile: a read of PORTA that is not used is what clears the CA1 flag, and cc65
 * removes an unused read of anything not volatile (the PIA struct in <atari.h> is not). */
#define PORTA (*(volatile uint8_t *) 0xD300)
#define PACTL (*(volatile uint8_t *) 0xD302)

static volatile uint8_t sink;

static void clear_flag(void)
{
  sink = PORTA;
}

#define MODE_POLL      0
#define MODE_INTERRUPT 1
#define QUIET_JIFFIES  3   /* the screen is redrawn this long after the last edge, not while edges are coming */

/* Redrawing takes longer than the gap between pulses, and the PIA flag is a single latch, so counting
 * has to be a tight loop that never draws; the screen follows when the edges have stopped. */
static void show(uint8_t mode, uint16_t polled, uint8_t pending)
{
  gotoxy(0, 8);
  cprintf("mode: %s       \r\n\r\n", mode == MODE_POLL ? "P polling the PIA flag" : "I interrupt (VPRCED)");
  cprintf("polled edges:  %5u\r\n", (unsigned) polled);
  cprintf("interrupts:    %5u   since last redraw: %s\r\n", (unsigned) fn_atari_proc_count(),
          pending ? "yes" : "no ");
}

/* ---- T: a TCP session, told by PROC or polling ---- */

/* Reads a line (host:port) with a visible cursor; Return ends it, Delete/Backspace edit it. */
static void read_line(char *buf, uint8_t max)
{
  uint8_t len = 0;
  for (;;) {
    const char c = cgetc();
    if (c == '\n' || c == '\r' || c == (char) 0x9B) break;
    if ((c == 0x7E || c == 0x08) && len > 0) {
      --len;
      cputs("\x1e \x1e");   /* ATASCII cursor left, blank, left */
    } else if (c >= ' ' && c < 0x7F && len < max - 1) {
      buf[len++] = c;
      cputc(c);
    }
  }
  buf[len] = '\0';
}

static void page_tcp(void)
{
  static char line[40];
  static uint8_t data[64];
  char *colon;
  fn_handle_t handle;
  uint16_t got;
  uint8_t flags;
  uint8_t status;
  uint8_t use_proc;
  uint8_t dirty = 1;
  uint8_t last_change = 0;
  uint16_t told = 0;
  uint16_t reads = 0;       /* fn_read calls issued: each is SIO transactions */
  uint16_t with_data = 0;
  uint16_t not_ready = 0;
  uint32_t offset = 0;
  uint32_t total = 0;
  uint8_t shown = 0;        /* the last bytes received, as text */
  char last[17];

  clrscr();
  cputs("PROCTEST - TCP session\r\n\r\nhost:port (e.g. 192.168.1.5:7000): ");
  read_line(line, sizeof(line));
  colon = strrchr(line, ':');
  if (!colon || colon == line) {
    cputs("\r\nneed host:port\r\n");
    cgetc();
    return;
  }
  *colon = '\0';

  status = fn_init();
  if (status == FN_OK) status = fn_atari_query_capabilities();
  cprintf("\r\ninit/capabilities: $%02x  flags $%02x  PROC advertised: %s\r\n", (unsigned) status,
          (unsigned) fn_atari_capability_flags(),
          (fn_atari_capability_flags() & FN_ATARI_CAP_PROC) ? "yes" : "NO (sio.tune proc 1)");
  status = fn_tcp_open(&handle, line, (uint16_t) atoi(colon + 1));
  cprintf("tcp open: $%02x\r\n", (unsigned) status);
  if (status != FN_OK) {
    cputs("press a key\r\n");
    cgetc();
    return;
  }
  use_proc = (fn_atari_capability_flags() & FN_ATARI_CAP_PROC) != 0;
  if (use_proc) (void) fn_atari_proc_enable();
  cputs("\r\nM toggles PROC / polling, X exits\r\n");
  last[0] = '\0';

  for (;;) {
    uint8_t ask = 0;

    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if (key == 'm' || key == 'M') {
        use_proc = !use_proc;
        if (use_proc) (void) fn_atari_proc_enable();
        else fn_atari_proc_disable();
        told = reads = with_data = not_ready = 0;
        dirty = 1;
      }
    }
    if (use_proc) {
      if (fn_atari_proc_pending()) {
        ++told;
        ask = 1;
        dirty = 1;
        last_change = OS.rtclok[2];
      }
    } else {
      ask = 1;
    }
    while (ask) {   /* read until the session says there is no more */
      got = 0;
      flags = 0;
      status = fn_read(handle, offset, data, sizeof(data), &got, &flags);
      ++reads;
      if (status == FN_OK && got > 0) {
        ++with_data;
        offset += got;
        total += got;
        shown = got > 16 ? 16 : (uint8_t) got;
        memcpy(last, data, shown);
        last[shown] = '\0';
        dirty = 1;
        last_change = OS.rtclok[2];
      } else {
        if (status == FN_ERR_NOT_READY) ++not_ready;
        ask = 0;
      }
    }
    if (dirty && (uint8_t) (OS.rtclok[2] - last_change) >= 3) {
      uint8_t i;
      gotoxy(0, 8);
      cprintf("mode: %s    \r\n\r\n", use_proc ? "PROC: read when told  " : "POLLING: read every pass");
      cprintf("told by PROC:  %5u\r\n", (unsigned) told);
      cprintf("fn_read calls: %5u  (data %u, NOT_READY %u)   \r\n", (unsigned) reads, (unsigned) with_data,
              (unsigned) not_ready);
      cprintf("bytes: %lu\r\nlast: ", (unsigned long) total);
      for (i = 0; i < shown; ++i) cputc(last[i] >= ' ' && last[i] < 0x7F ? last[i] : '.');
      cputs("                \r\n");
      dirty = 0;
    }
  }
  fn_atari_proc_disable();
  (void) fn_close(handle);
}

int main(void)
{
  uint8_t mode = MODE_POLL;
  uint16_t polled = 0;
  uint16_t seen = 0;        /* the interrupt count last noticed */
  uint8_t dirty = 1;
  uint8_t last_edge = 0;    /* the clock when the last change was noticed */

  clrscr();
  cputs("PROCTEST - PROC line from the FujiNet\r\n\r\n");
  cputs("On the FujiNet console: sio.proc pulse 5\r\n\r\n");
  cputs("P poll the flag  I interrupt  T tcp  R reset  X exit\r\n");
  fn_atari_proc_disable();
  clear_flag();   /* any old flag */

  for (;;) {
    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if (key == 't' || key == 'T') {
        page_tcp();
        clrscr();
        cputs("PROCTEST - PROC line from the FujiNet\r\n\r\n");
        cputs("On the FujiNet console: sio.proc pulse 5\r\n\r\n");
        cputs("P poll the flag  I interrupt  T tcp  R reset  X exit\r\n");
        fn_atari_proc_disable();
        mode = MODE_POLL;
        clear_flag();
      } else if (key == 'p' || key == 'P') {
        fn_atari_proc_disable();
        mode = MODE_POLL;
        clear_flag();
      } else if (key == 'i' || key == 'I') {
        (void) fn_atari_proc_enable();
        mode = MODE_INTERRUPT;
      } else if (key == 'r' || key == 'R') {
        polled = 0;
        if (mode == MODE_INTERRUPT) {   /* a new count */
          fn_atari_proc_disable();
          (void) fn_atari_proc_enable();
        }
      }
      seen = fn_atari_proc_count();
      last_edge = OS.rtclok[2] - QUIET_JIFFIES;
      dirty = 1;
    }
    if (mode == MODE_POLL) {
      if (PACTL & 0x80u) {
        ++polled;
        clear_flag();   /* reading PORTA clears the flag */
        last_edge = OS.rtclok[2];
        dirty = 1;
      }
    } else if (fn_atari_proc_count() != seen) {
      seen = fn_atari_proc_count();
      last_edge = OS.rtclok[2];
      dirty = 1;
    }
    if (dirty && (uint8_t) (OS.rtclok[2] - last_edge) >= QUIET_JIFFIES) {
      show(mode, polled, mode == MODE_INTERRUPT ? fn_atari_proc_pending() : 0);
      dirty = 0;
    }
  }
  fn_atari_proc_disable();
  return 0;
}
