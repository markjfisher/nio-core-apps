/* Story 2.4 public-API Atari acceptance guest.
 *
 * This application deliberately owns only its result block.  All FujiBus and
 * SIO work is inside the public fujinet-nio-lib calls below.
 */

#include <stdint.h>

#include "fujinet-nio.h"
#include "fn_protocol.h"
#include "fn_raw.h"

#define FNACCEPT_RESULT_ADDR 0x0A00u
#define FNACCEPT_RESULT_VERSION 2u

#define FNACCEPT_FILE_URI "persist:///fnaccept-v1.bin"
#define FNACCEPT_FILE_URI_LEN (sizeof(FNACCEPT_FILE_URI) - 1u)
#define FNACCEPT_FILE_MARKER "ATARI-FILE-24"
#define FNACCEPT_FILE_MARKER_LEN (sizeof(FNACCEPT_FILE_MARKER) - 1u)
#define FNACCEPT_FILE_REQUEST_CAPACITY 64u
#define FNACCEPT_FILE_REPLY_CAPACITY 64u

#define FNACCEPT_FILE_CMD_READ 0x03u
#define FNACCEPT_FILE_CMD_WRITE 0x04u

enum {
    FNACCEPT_SIGNATURE = 0,
    FNACCEPT_VERSION = 4,
    FNACCEPT_STAGE = 5,
    FNACCEPT_INIT_STATUS = 6,
    FNACCEPT_CLOCK_STATUS = 7,
    FNACCEPT_CLOCK_VALUE = 8,
    FNACCEPT_HEARTBEAT = 16,
    FNACCEPT_HEARTBEAT_HIGH = 17,
    FNACCEPT_FILE_WRITE_STATUS = 18,
    FNACCEPT_FILE_WRITE_DEVICE_STATUS = 19,
    FNACCEPT_FILE_WRITE_RESPONSE_LENGTH = 20,
    FNACCEPT_FILE_WRITE_COUNT = 22,
    FNACCEPT_FILE_READ_STATUS = 24,
    FNACCEPT_FILE_READ_DEVICE_STATUS = 25,
    FNACCEPT_FILE_READ_RESPONSE_LENGTH = 26,
    FNACCEPT_FILE_READ_COUNT = 28,
    FNACCEPT_FILE_VERIFY = 30
};

static volatile uint8_t *const result = (volatile uint8_t *)FNACCEPT_RESULT_ADDR;
static uint8_t file_request[FNACCEPT_FILE_REQUEST_CAPACITY];
static uint8_t file_reply[FNACCEPT_FILE_REPLY_CAPACITY];

static void set_signature(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    result[FNACCEPT_SIGNATURE + 0] = a;
    result[FNACCEPT_SIGNATURE + 1] = b;
    result[FNACCEPT_SIGNATURE + 2] = c;
    result[FNACCEPT_SIGNATURE + 3] = d;
}

static void put_u16le(uint8_t offset, uint16_t value)
{
    file_request[offset] = (uint8_t)value;
    file_request[offset + 1u] = (uint8_t)(value >> 8);
}

static uint16_t get_u16le(const uint8_t *value, uint8_t offset)
{
    return (uint16_t)value[offset] | ((uint16_t)value[offset + 1u] << 8);
}

/* FileDevice v1 application payload only: this guest never builds a FujiBus
 * packet or a native-SIO frame. */
static uint8_t build_file_request(uint8_t command)
{
    uint8_t index;
    uint8_t at = 0;

    file_request[at++] = 1u;
    put_u16le(at, FNACCEPT_FILE_URI_LEN);
    at += 2u;
    for (index = 0; index < FNACCEPT_FILE_URI_LEN; ++index) {
        file_request[at++] = FNACCEPT_FILE_URI[index];
    }
    /* Both operations start at offset zero. */
    file_request[at++] = 0;
    file_request[at++] = 0;
    file_request[at++] = 0;
    file_request[at++] = 0;
    put_u16le(at, FNACCEPT_FILE_MARKER_LEN);
    at += 2u;
    if (command == FNACCEPT_FILE_CMD_WRITE) {
        for (index = 0; index < FNACCEPT_FILE_MARKER_LEN; ++index) {
            file_request[at++] = FNACCEPT_FILE_MARKER[index];
        }
    }
    return at;
}

static uint8_t valid_write_reply(const fn_raw_response_t *response)
{
    return response->status == 0 && response->payload_length == 10u &&
           file_reply[0] == 1u && file_reply[1] == 0 &&
           file_reply[2] == 0 && file_reply[3] == 0 &&
           file_reply[4] == 0 && file_reply[5] == 0 &&
           file_reply[6] == 0 && file_reply[7] == 0 &&
           get_u16le(file_reply, 8) == FNACCEPT_FILE_MARKER_LEN;
}

static uint8_t valid_read_reply(const fn_raw_response_t *response)
{
    uint8_t index;

    if (response->status != 0 ||
        response->payload_length != (uint16_t)(10u + FNACCEPT_FILE_MARKER_LEN) ||
        file_reply[0] != 1u || (file_reply[1] & (uint8_t)~0x03u) != 0 ||
        file_reply[2] != 0 || file_reply[3] != 0 ||
        file_reply[4] != 0 || file_reply[5] != 0 ||
        file_reply[6] != 0 || file_reply[7] != 0 ||
        get_u16le(file_reply, 8) != FNACCEPT_FILE_MARKER_LEN) {
        return 0;
    }
    for (index = 0; index < FNACCEPT_FILE_MARKER_LEN; ++index) {
        if (file_reply[10u + index] != FNACCEPT_FILE_MARKER[index]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    FN_TIME_T clock_value;
    fn_raw_response_t file_response;
    uint8_t status;
    uint8_t index;
    uint8_t request_length;

    set_signature('A', 'C', '?', '?');
    result[FNACCEPT_VERSION] = FNACCEPT_RESULT_VERSION;
    result[FNACCEPT_STAGE] = 1;
    result[FNACCEPT_INIT_STATUS] = 0xff;
    result[FNACCEPT_CLOCK_STATUS] = 0xff;
    result[FNACCEPT_HEARTBEAT] = 0;
    result[FNACCEPT_HEARTBEAT_HIGH] = 0;
    result[FNACCEPT_FILE_WRITE_STATUS] = 0xff;
    result[FNACCEPT_FILE_WRITE_DEVICE_STATUS] = 0xff;
    result[FNACCEPT_FILE_READ_STATUS] = 0xff;
    result[FNACCEPT_FILE_READ_DEVICE_STATUS] = 0xff;
    result[FNACCEPT_FILE_VERIFY] = 0;
    for (index = FNACCEPT_FILE_WRITE_RESPONSE_LENGTH;
         index < FNACCEPT_FILE_VERIFY; ++index) {
        result[index] = 0;
    }
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
    request_length = build_file_request(FNACCEPT_FILE_CMD_WRITE);
    status = fn_raw_call(FN_DEVICE_FILE, FNACCEPT_FILE_CMD_WRITE,
                         file_request, request_length,
                         file_reply, sizeof(file_reply), &file_response);
    result[FNACCEPT_FILE_WRITE_STATUS] = status;
    result[FNACCEPT_FILE_WRITE_DEVICE_STATUS] = file_response.status;
    result[FNACCEPT_FILE_WRITE_RESPONSE_LENGTH] = (uint8_t)file_response.payload_length;
    result[FNACCEPT_FILE_WRITE_RESPONSE_LENGTH + 1u] = (uint8_t)(file_response.payload_length >> 8);
    result[FNACCEPT_FILE_WRITE_COUNT] = get_u16le(file_reply, 8);
    result[FNACCEPT_FILE_WRITE_COUNT + 1u] = file_reply[9];
    if (status != FN_OK || !valid_write_reply(&file_response)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 4;
    request_length = build_file_request(FNACCEPT_FILE_CMD_READ);
    status = fn_raw_call(FN_DEVICE_FILE, FNACCEPT_FILE_CMD_READ,
                         file_request, request_length,
                         file_reply, sizeof(file_reply), &file_response);
    result[FNACCEPT_FILE_READ_STATUS] = status;
    result[FNACCEPT_FILE_READ_DEVICE_STATUS] = file_response.status;
    result[FNACCEPT_FILE_READ_RESPONSE_LENGTH] = (uint8_t)file_response.payload_length;
    result[FNACCEPT_FILE_READ_RESPONSE_LENGTH + 1u] = (uint8_t)(file_response.payload_length >> 8);
    result[FNACCEPT_FILE_READ_COUNT] = (uint8_t)get_u16le(file_reply, 8);
    result[FNACCEPT_FILE_READ_COUNT + 1u] = file_reply[9];
    if (status != FN_OK || !valid_read_reply(&file_response)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_FILE_VERIFY] = 1;
    result[FNACCEPT_STAGE] = 5;
    set_signature('A', 'F', 'P', '!');
    for (;;) {
        if (++result[FNACCEPT_HEARTBEAT] == 0) {
            ++result[FNACCEPT_HEARTBEAT_HIGH];
        }
    }
}
