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
 * T  the real thing: open a session to a URL you type (tcp://host:port, tls://host:port, http://... or
 *    https://...; a bare host:port is tcp), and take what comes, either only when PROC says so or by trying
 *    every time round the loop, counting the SIO calls each way costs. HTTP sessions wait for the status
 *    (fn_info) and then the body. It needs `sio.tune proc 1` on the FujiNet and something to talk to: for
 *    TCP `nc -l 7000` on a PC, for HTTP httpbin (scripts/start_test_services.sh http), whose /delay/5 answers
 *    after five seconds and /drip?duration=5&numbytes=20 spreads bytes over time.
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

#define LOG_SIZE       120   /* three screen lines of received text */
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

static void page_url(void)
{
  static char line[72];
  static char url[80];
  static uint8_t data[128];
  static char tail[LOG_SIZE + 1];   /* the latest bytes received, as text */
  uint8_t is_http;
  uint8_t have_info;
  uint16_t http_status = 0;
  uint32_t content_length = 0;
  uint8_t info_flags = 0;
  uint16_t infos = 0;       /* fn_info calls issued */
  uint8_t last_info = 0;
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
  uint32_t sent = 0;        /* bytes written, which is also the next write offset */
  uint16_t sends = 0;
  uint8_t send_status = 0;
  uint8_t tail_len = 0;
  uint8_t last_status = 0;  /* what the last fn_read returned, and its flags */
  uint8_t last_flags = 0;
  uint8_t last_draw = 0;

  clrscr();
  cputs("PROCTEST - network session\r\n\r\nurl, e.g. http://192.168.1.5:8080/delay/5\r\nor tcp:// tls:// host:port: ");
  read_line(line, sizeof(line));
  if (line[0] == '\0') return;
  if (strstr(line, "://")) {
    strcpy(url, line);
  } else {
    strcpy(url, "tcp://");
    strcat(url, line);
  }
  is_http = strncmp(url, "http", 4) == 0;

  status = fn_init();
  if (status == FN_OK) status = fn_atari_query_capabilities();
  cprintf("\r\ninit/capabilities: $%02x  flags $%02x  PROC advertised: %s\r\n", (unsigned) status,
          (unsigned) fn_atari_capability_flags(),
          (fn_atari_capability_flags() & FN_ATARI_CAP_PROC) ? "yes" : "NO (sio.tune proc 1)");
  status = fn_open(&handle, is_http ? FN_METHOD_GET : 0, url, 0);
  cprintf("open: $%02x\r\n", (unsigned) status);
  if (status != FN_OK) {
    cputs("press a key\r\n");
    cgetc();
    return;
  }
  have_info = !is_http;     /* streams answer at once; HTTP waits for the status */
  use_proc = (fn_atari_capability_flags() & FN_ATARI_CAP_PROC) != 0;
  if (use_proc) (void) fn_atari_proc_enable();
  clrscr();
  cprintf("%s\r\nPROC advertised: %s   M mode, S send, X exit\r\n", url, use_proc ? "yes" : "NO");
  tail[0] = '\0';

  for (;;) {
    uint8_t ask = 0;

    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if ((key == 's' || key == 'S') && !is_http) {   /* an echo service sends it back */
        static const char hello[] = "hello from the Atari\n";
        uint16_t written = 0;
        send_status = fn_write(handle, sent, (const uint8_t *) hello, sizeof(hello) - 1, &written);
        if (send_status == FN_OK) sent += written;
        ++sends;
        dirty = 1;
        last_change = OS.rtclok[2];
      }
      if (key == 'm' || key == 'M') {
        use_proc = !use_proc;
        if (use_proc) (void) fn_atari_proc_enable();
        else fn_atari_proc_disable();
        told = reads = with_data = not_ready = infos = 0;
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
    if (ask && !have_info) {   /* HTTP: the status first */
      ++infos;
      last_info = fn_info(handle, &http_status, &content_length, &info_flags);
      if (last_info == FN_OK) {
        have_info = 1;
      } else if (last_info != FN_ERR_NOT_READY) {
        have_info = 1;   /* a failure: stop asking and let the read report it */
      } else {
        ask = 0;
      }
      dirty = 1;
      last_change = OS.rtclok[2];
    }
    while (ask) {   /* read until the session says there is no more */
      got = 0;
      flags = 0;
      status = fn_read(handle, offset, data, sizeof(data), &got, &flags);
      ++reads;
      last_status = status;
      last_flags = flags;
      if (status == FN_OK && got > 0) {
        ++with_data;
        offset += got;
        total += got;
        /* append to the rolling tail, dropping the oldest text when it is full */
        {
          uint8_t k;
          for (k = 0; k < got; ++k) {
            const char c = (char) data[k];
            if (tail_len == LOG_SIZE) {
              memmove(tail, tail + 1, LOG_SIZE - 1);
              --tail_len;
            }
            tail[tail_len++] = (c >= ' ' && c < 0x7F) ? c : (c == '\n' ? '|' : '.');
          }
          tail[tail_len] = '\0';
        }
        dirty = 1;
        last_change = OS.rtclok[2];
      } else {
        if (status == FN_ERR_NOT_READY) ++not_ready;
        ask = 0;
      }
    }
    /* Redraw when something changed and has been quiet a moment, and at least twice a second while polling
     * (the counts move with no data at all). */
    if ((dirty && (uint8_t) (OS.rtclok[2] - last_change) >= 3) || (uint8_t) (OS.rtclok[2] - last_draw) >= 25) {
      uint8_t i;
      last_draw = OS.rtclok[2];
      gotoxy(0, 5);
      cprintf("mode: %s    \r\n\r\n", use_proc ? "PROC: read when told  " : "POLLING: read every pass");
      cprintf("told by PROC:  %5u\r\n", (unsigned) told);
      if (is_http) {
        cprintf("fn_info calls: %5u  last $%02x  HTTP %u len %lu\r\n", (unsigned) infos, (unsigned) last_info,
                (unsigned) http_status, (unsigned long) content_length);
      }
      cprintf("fn_read calls: %5u  (data %u, NOT_READY %u)   \r\n", (unsigned) reads, (unsigned) with_data,
              (unsigned) not_ready);
      cprintf("last fn_read: $%02x flags $%02x%s     \r\n", (unsigned) last_status, (unsigned) last_flags,
              (last_flags & FN_READ_EOF) ? " EOF (peer closed)" : "");
      cprintf("bytes: %lu   sent %lu (%u, last $%02x)   \r\n", (unsigned long) total, (unsigned long) sent,
              (unsigned) sends, (unsigned) send_status);
      for (i = 0; i < LOG_SIZE; ++i) cputc(i < tail_len ? tail[i] : ' ');
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
  cputs("P poll the flag  I interrupt  T session  R reset  X exit\r\n");
  fn_atari_proc_disable();
  clear_flag();   /* any old flag */

  for (;;) {
    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if (key == 't' || key == 'T') {
        page_url();
        clrscr();
        cputs("PROCTEST - PROC line from the FujiNet\r\n\r\n");
        cputs("On the FujiNet console: sio.proc pulse 5\r\n\r\n");
        cputs("P poll the flag  I interrupt  T session  R reset  X exit\r\n");
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
