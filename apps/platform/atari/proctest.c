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
 * No FujiNet library call other than the PROC ones is made, so nothing else is on the SIO bus.
 */

#include <atari.h>
#include <conio.h>
#include <stdint.h>

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
  cputs("P poll the flag  I interrupt  R reset  X exit\r\n");
  fn_atari_proc_disable();
  clear_flag();   /* any old flag */

  for (;;) {
    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if (key == 'p' || key == 'P') {
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
