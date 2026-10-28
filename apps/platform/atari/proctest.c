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

#define MODE_POLL      0
#define MODE_INTERRUPT 1

static void show(uint8_t mode, uint16_t polled, uint8_t flag)
{
  gotoxy(0, 8);
  cprintf("mode: %s       \r\n", mode == MODE_POLL ? "P polling the PIA flag" : "I interrupt (VPRCED)");
  cprintf("PACTL $%02x  CA1 flag %s \r\n\r\n", (unsigned) PIA.pactl, flag ? "SET  " : "clear");
  cprintf("polled edges:  %5u\r\n", (unsigned) polled);
  cprintf("interrupts:    %5u   pending: %s\r\n", (unsigned) fn_atari_proc_count(),
          mode == MODE_INTERRUPT && fn_atari_proc_pending() ? "yes" : "no ");
}

int main(void)
{
  uint8_t mode = MODE_POLL;
  uint16_t polled = 0;
  uint8_t flag = 0;
  uint8_t last_shown = 0;
  uint16_t spin = 0;

  clrscr();
  cputs("PROCTEST - PROC line from the FujiNet\r\n\r\n");
  cputs("On the FujiNet console: sio.proc pulse 5\r\n\r\n");
  cputs("P poll the flag  I interrupt  R reset  X exit\r\n");
  fn_atari_proc_disable();
  (void) PIA.porta;   /* clear any old flag */

  for (;;) {
    if (kbhit()) {
      const char key = cgetc();
      if (key == 'x' || key == 'X') break;
      if (key == 'p' || key == 'P') {
        fn_atari_proc_disable();
        mode = MODE_POLL;
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
      last_shown = 0;
    }
    if (mode == MODE_POLL && (PIA.pactl & 0x80u)) {
      ++polled;
      flag = 1;
      (void) PIA.porta;   /* reading PORTA clears the flag */
      last_shown = 0;
    } else if (mode == MODE_POLL) {
      flag = 0;
    }
    if (!last_shown || ++spin == 0) {
      show(mode, polled, flag);
      last_shown = 1;
    }
  }
  fn_atari_proc_disable();
  return 0;
}
