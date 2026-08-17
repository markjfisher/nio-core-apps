#include "fujinet-nio.h"

#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define MAPPINGS_SIZE 17U
#define MAPPING_VALID 0x01U
#define MAPPING_READONLY 0x02U

static uint8_t mappings[MAPPINGS_SIZE];
static uint8_t service_buffer[768];

int main(void)
{
  fn_appstore_io_t io = { service_buffer, sizeof(service_buffer) };
  fn_appstore_read_t read_result;
  unsigned unit;
  unsigned restored = 0;

  if (fn_init() != FN_OK ||
      fn_appstore_read(&io, "config-nio", "mappings", 0, mappings,
                       sizeof(mappings), &read_result) != FN_OK) {
    puts("FMOUNTRESTORE mappings=read-failed");
    return 20;
  }
  if ((read_result.flags & FN_APPSTORE_READ_EXISTS) == 0) {
    fn_shutdown();
    puts("FMOUNTRESTORE mappings=none");
    return 0;
  }
  if (read_result.bytes_read != sizeof(mappings) || mappings[0] != 1) {
    fn_shutdown();
    puts("FMOUNTRESTORE mappings=invalid");
    return 20;
  }

  for (unit = 0; unit < 8; ++unit) {
    const uint8_t flags = mappings[1 + unit * 2];

    if ((flags & MAPPING_VALID) == 0) continue;
    ++restored;
  }
  fn_shutdown();

  for (unit = 0; unit < 8; ++unit) {
    const uint8_t flags = mappings[1 + unit * 2];
    const uint8_t slot = mappings[2 + unit * 2];
    char command[32];
    LONG result;

    if ((flags & MAPPING_VALID) == 0) continue;
    sprintf(command, "SYS:C/fmount %u DN%u: %s", (unsigned)slot, unit,
            (flags & MAPPING_READONLY) ? "RO" : "RW");
    result = SystemTags((CONST_STRPTR)command, SYS_Asynch, FALSE, TAG_DONE);
    if (result != 0) {
      printf("FMOUNTRESTORE unit=%u slot=%u rc=%ld\n", unit,
             (unsigned)slot, (long)result);
      return 10;
    }
    printf("FMOUNTRESTORE unit=%u slot=%u mode=%s\n", unit,
           (unsigned)slot, (flags & MAPPING_READONLY) ? "RO" : "RW");
  }
  printf("FMOUNTRESTORE restored=%u\n", restored);
  return 0;
}
