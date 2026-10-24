/* Relocatable resident N: payload for Story 2.5.
 *
 * This object deliberately has no main() or crt0.  The transient Atari-side
 * o65 loader owns DATA/BSS/ZP/stack setup, calls fnresident_module_entry(),
 * and publishes its CIO table only after that call succeeds.
 */
#include <stdint.h>

#include "fujinet-nio.h"

/* Keep the public API closure and the eventual per-IOCB ownership in the
 * resident payload, rather than accidentally growing the transient loader
 * when CIO operations are enabled in later increments. */
static fn_handle_t handles[2];
static uint8_t caches[2][128];
static uint16_t transferred;
static uint16_t http_status;
static uint32_t content_length;
static uint8_t flags;

uint8_t fnresident_module_entry(void)
{
    return fn_init();
}

/* Not called by the stub-only increment.  Keeping this ordinary C routine in
 * the directly linked object proves and retains the exact public operations
 * that later CIO OPEN/GET/PUT/STATUS/CLOSE entry points will use. */
uint8_t fnresident_module_api_closure(void)
{
    static const char uri[] = "fixture://resident-module";
    uint8_t status;

    status = fn_open(&handles[0], FN_METHOD_GET, uri, 0);
    if (status == FN_OK) {
        status = fn_write(handles[0], 0, (const uint8_t *)"NIO", 3,
                          &transferred);
    }
    if (status == FN_OK) {
        status = fn_read(handles[0], 0, caches[0], sizeof(caches[0]),
                         &transferred, &flags);
    }
    if (status == FN_OK) {
        status = fn_info(handles[0], &http_status, &content_length, &flags);
    }
    if (status == FN_OK) {
        status = fn_close(handles[0]);
    }
    return status;
}
