/* Story 2.4 public-API Atari acceptance guest.
 *
 * This application deliberately owns only its result block.  All FujiBus and
 * SIO work is inside the public fujinet-nio-lib calls below.
 */

#include <stdint.h>

#include "fujinet-nio.h"

#define FNACCEPT_RESULT_ADDR 0x0A00u
#define FNACCEPT_RESULT_VERSION 1u

enum {
    FNACCEPT_SIGNATURE = 0,
    FNACCEPT_VERSION = 4,
    FNACCEPT_STAGE = 5,
    FNACCEPT_INIT_STATUS = 6,
    FNACCEPT_CLOCK_STATUS = 7,
    FNACCEPT_CLOCK_VALUE = 8,
    FNACCEPT_HEARTBEAT = 16,
    FNACCEPT_HEARTBEAT_HIGH = 17
};

static volatile uint8_t *const result = (volatile uint8_t *)FNACCEPT_RESULT_ADDR;

static void set_signature(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    result[FNACCEPT_SIGNATURE + 0] = a;
    result[FNACCEPT_SIGNATURE + 1] = b;
    result[FNACCEPT_SIGNATURE + 2] = c;
    result[FNACCEPT_SIGNATURE + 3] = d;
}

int main(void)
{
    FN_TIME_T clock_value;
    uint8_t status;
    uint8_t index;

    set_signature('A', 'C', '?', '?');
    result[FNACCEPT_VERSION] = FNACCEPT_RESULT_VERSION;
    result[FNACCEPT_STAGE] = 1;
    result[FNACCEPT_INIT_STATUS] = 0xff;
    result[FNACCEPT_CLOCK_STATUS] = 0xff;
    result[FNACCEPT_HEARTBEAT] = 0;
    result[FNACCEPT_HEARTBEAT_HIGH] = 0;
    for (index = 0; index != 8; ++index) {
        result[FNACCEPT_CLOCK_VALUE + index] = 0;
    }

    status = fn_init();
    result[FNACCEPT_INIT_STATUS] = status;
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 2;
    status = fn_clock_get(&clock_value);
    result[FNACCEPT_CLOCK_STATUS] = status;
    for (index = 0; index != 8; ++index) {
        result[FNACCEPT_CLOCK_VALUE + index] = clock_value.b[index];
    }
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 3;
    set_signature('A', 'C', 'P', '!');
    for (;;) {
        if (++result[FNACCEPT_HEARTBEAT] == 0) {
            ++result[FNACCEPT_HEARTBEAT_HIGH];
        }
    }
}
