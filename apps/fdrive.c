#include "fnsvc.h"
#ifdef __ATARI__
#include <conio.h>
#endif

#include <stdio.h>

static char mounts_text[420];

int main(void)
{
  uint16_t start = 0;
  uint16_t count;
  unsigned shown = 0;
  uint8_t more;

  do {
    if (!fnsvc_disk_list_mounts(start, mounts_text, sizeof(mounts_text),
                                &count, &more)) {
      puts("Unable to list mounted images");
      return 2;
    }
    if (mounts_text[0])
      fputs(mounts_text, stdout);
    shown += count;
    start = (uint16_t) (start + count);
  } while (more && count);

  if (!shown) {
    puts("No FujiNet mounted images found");
    return 1;
  }

#ifdef __ATARI__
  cgetc();
#endif

  return 0;
}
