/* Story 2.5 increment 1 -- install a bounded resident N: CIO table.
 *
 * This intentionally contains no network operation.  Its handler vectors
 * provide the exact CIO ABI and return the Atari OS FNCNOT status until the
 * later OPEN/GET/PUT/STATUS increments attach public fn_* calls.
 */

#include <stdint.h>

#include "fujinet-nio.h"

#define FNRESIDENT_RESULT_ADDR 0x0B00u
#define FNRESIDENT_HATABS_ADDR 0x031Au
#define FNRESIDENT_MEMTOP_ADDR 0x02E5u
#define FNRESIDENT_MEMLO_ADDR  0x02E7u
#define FNRESIDENT_SDLSTL_ADDR 0x0230u
#define FNRESIDENT_SAVMSC_ADDR 0x0058u
#define FNRESIDENT_STACK_BYTES 0x0800u
#define FNRESIDENT_HATABS_BYTES 35u
#define FNRESIDENT_HATABS_STEP 3u

/* Names and values are frozen from cc65/asminc/atari.inc: SUCCES=$01 and
 * FNCNOT=$92.  DOS-specific errors are deliberately not mapped in increment
 * 1. */
#define FNRESIDENT_CIO_SUCCESS 0x01u
#define FNRESIDENT_CIO_FNCNOT  0x92u

enum fnresident_outcome {
    FNRESIDENT_INSTALLED = 0,
    FNRESIDENT_ALREADY_OWNED = 1,
    FNRESIDENT_INIT_FAILED = 2,
    FNRESIDENT_MEMORY_UNSAFE = 3,
    FNRESIDENT_FOREIGN_HANDLER = 4,
    FNRESIDENT_NO_HATABS_SLOT = 5
};

struct fnresident_result {
    uint8_t magic[4];
    uint8_t version;
    uint8_t outcome;
    uint8_t init_status;
    uint8_t cio_stub_status;
    uint16_t old_memlo;
    uint16_t new_memlo;
    uint16_t resident_start;
    uint16_t resident_end;
    uint16_t memtop;
    uint16_t stack_floor;
    uint16_t sdlstl;
    uint16_t savmsc;
    uint8_t hatabs_offset;
    uint8_t reserved;
    uint16_t handler_table;
    uint16_t vectors[6];
};

extern const uint8_t __BSS_LOAD__;
extern const uint8_t __BSS_SIZE__;
extern const uint8_t fnresident_handler_table[];

static volatile struct fnresident_result* const result =
    (volatile struct fnresident_result*)FNRESIDENT_RESULT_ADDR;

static uint16_t read16(uint16_t address)
{
    volatile uint8_t* const p = (volatile uint8_t*)address;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write16(uint16_t address, uint16_t value)
{
    volatile uint8_t* const p = (volatile uint8_t*)address;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t symbol_address(const uint8_t* symbol)
{
    return (uint16_t)symbol;
}

static void begin_result(void)
{
    uint8_t index;

    result->magic[0] = 'N';
    result->magic[1] = 'R';
    result->magic[2] = 'I';
    result->magic[3] = '!';
    result->version = 1;
    result->outcome = FNRESIDENT_MEMORY_UNSAFE;
    result->init_status = FN_ERR_UNKNOWN;
    result->cio_stub_status = FNRESIDENT_CIO_FNCNOT;
    result->hatabs_offset = 0xff;
    result->handler_table = symbol_address(fnresident_handler_table);
    for (index = 0; index != 6; ++index) {
        result->vectors[index] = read16((uint16_t)(result->handler_table + index * 2));
    }
}

int main(void)
{
    volatile uint8_t* const hatabs = (volatile uint8_t*)FNRESIDENT_HATABS_ADDR;
    uint16_t old_memlo;
    uint16_t memtop;
    uint16_t stack_floor;
    uint16_t resident_start;
    uint16_t resident_end;
    uint16_t handler_table;
    uint8_t slot;
    uint8_t index;
    uint8_t status;

    begin_result();
    old_memlo = read16(FNRESIDENT_MEMLO_ADDR);
    memtop = read16(FNRESIDENT_MEMTOP_ADDR);
    resident_start = symbol_address((const uint8_t*)0x1d00u);
    resident_end = (uint16_t)(symbol_address(&__BSS_LOAD__) +
                              symbol_address(&__BSS_SIZE__));
    stack_floor = (uint16_t)(memtop - FNRESIDENT_STACK_BYTES);
    handler_table = symbol_address(fnresident_handler_table);

    result->old_memlo = old_memlo;
    result->new_memlo = old_memlo;
    result->resident_start = resident_start;
    result->resident_end = resident_end;
    result->memtop = memtop;
    result->stack_floor = stack_floor;
    result->sdlstl = read16(FNRESIDENT_SDLSTL_ADDR);
    result->savmsc = read16(FNRESIDENT_SAVMSC_ADDR);

    /* The full linked image (including fn_init state) must fit below the
     * cc65 stack floor and before the OS-owned display allocation. */
    if (resident_start < old_memlo || resident_end <= resident_start ||
        resident_end > stack_floor || resident_end > memtop ||
        resident_end > result->sdlstl || resident_end > result->savmsc) {
        result->outcome = FNRESIDENT_MEMORY_UNSAFE;
        return 0;
    }

    slot = 0xff;
    for (index = 0; index < FNRESIDENT_HATABS_BYTES; index += FNRESIDENT_HATABS_STEP) {
        if (hatabs[index] == 'N') {
            if (read16((uint16_t)(FNRESIDENT_HATABS_ADDR + index + 1)) == handler_table) {
                result->hatabs_offset = index;
                result->outcome = FNRESIDENT_ALREADY_OWNED;
            } else {
                result->outcome = FNRESIDENT_FOREIGN_HANDLER;
            }
            return 0;
        }
        if (hatabs[index] == 0 && slot == 0xff) {
            slot = index;
        }
    }
    if (slot == 0xff) {
        result->outcome = FNRESIDENT_NO_HATABS_SLOT;
        return 0;
    }

    status = fn_init();
    result->init_status = status;
    if (status != FN_OK) {
        result->outcome = FNRESIDENT_INIT_FAILED;
        return 0;
    }

    /* Publish only after the resident range and library initialization are
     * valid.  HATABS is three bytes per entry and the following zero remains
     * the terminator for a newly allocated entry. */
    write16(FNRESIDENT_MEMLO_ADDR, resident_end);
    hatabs[slot + 1] = (uint8_t)handler_table;
    hatabs[slot + 2] = (uint8_t)(handler_table >> 8);
    hatabs[slot] = 'N';
    result->new_memlo = resident_end;
    result->hatabs_offset = slot;
    result->outcome = FNRESIDENT_INSTALLED;
    return 0;
}
