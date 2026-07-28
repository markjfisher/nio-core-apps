#include "fnctl.h"
#include "fnsvc.h"
#ifdef __ATARI__
#include "fujinet-nio.h"
#include <conio.h>
#endif

#include <stdio.h>

static fnsvc_mount_t mount_buf;

int main(void)
{
  uint8_t unit;
  uint8_t slot;
  int drive;
  unsigned found = 0;
  unsigned shown = 0;
  unsigned printed_diag = 0;

  for (unit = 0; unit < FNCTL_MAX_UNITS; unit++) {
    drive = fnctl_find_drive_for_unit(unit);

    if (!drive)
      continue;
    found++;
    if (!fnctl_get_unit_slot(unit, &slot))
      continue;

    {
      int ok = fnsvc_get_mount(slot, &mount_buf);

      if (ok && mount_buf.enabled && mount_buf.uri[0]) {
        printf("%c: slot %u [%s] %s", 'A' + drive - 1, (unsigned) slot,
               mount_buf.mode, mount_buf.uri);
      } else {
#ifdef __ATARI__
        if (!printed_diag) {
          printf("%c: slot %u -- no image selected --", 'A' + drive - 1,
                 (unsigned) slot);
          printf(" ok=%u err=%u raw=%u st=%u len=%u sio=%u",
                 (unsigned) ok,
                 (unsigned) fnsvc_last_error(),
                 (unsigned) fnsvc_last_raw_error(),
                 (unsigned) fnsvc_last_status(),
                 (unsigned) fnsvc_last_response_len(),
                 (unsigned) fn_atari_last_sio_status());
          printed_diag = 1;
          puts("");
        }
#endif
        continue;
      }
    }
    puts("");
    shown++;
  }

  if (!shown) {
    if (found)
      puts("No FujiNet mounted images found");
    else
      puts("No FujiNet drives found");
    return 1;
  }

#ifdef __ATARI__
  cgetc();
#endif

  return 0;
}
