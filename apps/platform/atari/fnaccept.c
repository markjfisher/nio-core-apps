/* Story 2.4 public-API Atari acceptance guest.
 *
 * This application deliberately owns only its result block.  All FujiBus and
 * SIO work is inside the public fujinet-nio-lib calls below.
 */

#include <stdint.h>

#include "fujinet-nio.h"
#include "fn_protocol.h"
#include "fn_raw.h"

/* fn_disk_*() correctly retains its bounded 512-byte codec buffers in static
 * storage on cc65.  Keep the observable block above that enlarged BSS range. */
#define FNACCEPT_RESULT_ADDR 0x0B00u
#define FNACCEPT_RESULT_VERSION 7u
#define FNACCEPT_MEMLO_ADDR 0x02E7u
#define FNACCEPT_MEMTOP_ADDR 0x02E5u
#define FNACCEPT_RAMTOP_ADDR 0x006Au
#define FNACCEPT_SAVMSC_ADDR 0x0058u
#define FNACCEPT_SDMCTL_ADDR 0x022Fu
#define FNACCEPT_SDLSTL_ADDR 0x0230u
#define FNACCEPT_DMACTL_ADDR 0xD400u
#define FNACCEPT_MEMORY_SNAPSHOT_OFFSET 0x60u

#define FNACCEPT_FILE_URI "persist:///fnaccept-v1.bin"
#define FNACCEPT_FILE_URI_LEN (sizeof(FNACCEPT_FILE_URI) - 1u)
#define FNACCEPT_FILE_MARKER "ATARI-FILE-24"
#define FNACCEPT_FILE_MARKER_LEN (sizeof(FNACCEPT_FILE_MARKER) - 1u)
#define FNACCEPT_FILE_RECOVERY_URI "persist:///fnaccept-recovered-v1.bin"
#define FNACCEPT_FILE_RECOVERY_URI_LEN (sizeof(FNACCEPT_FILE_RECOVERY_URI) - 1u)
#define FNACCEPT_FILE_RECOVERY_MARKER "ATARI-FILE-RECOVERY-24"
#define FNACCEPT_FILE_RECOVERY_MARKER_LEN (sizeof(FNACCEPT_FILE_RECOVERY_MARKER) - 1u)
#define FNACCEPT_FILE_REQUEST_CAPACITY 96u
#define FNACCEPT_FILE_REPLY_CAPACITY 64u

#define FNACCEPT_FILE_CMD_READ 0x03u
#define FNACCEPT_FILE_CMD_WRITE 0x04u

#define FNACCEPT_NETWORK_URI "fixture://nio-accept"
#define FNACCEPT_NETWORK_WRITE_MARKER "NET-WRITE-24"
#define FNACCEPT_NETWORK_WRITE_MARKER_LEN (sizeof(FNACCEPT_NETWORK_WRITE_MARKER) - 1u)
#define FNACCEPT_NETWORK_REPLY_TEXT "NET-REPLY-24"
#define FNACCEPT_NETWORK_REPLY_LEN (sizeof(FNACCEPT_NETWORK_REPLY_TEXT) - 1u)
#define FNACCEPT_NETWORK_REPLY_CAPACITY 32u

#define FNACCEPT_DISK_SLOT 2u
#define FNACCEPT_DISK_LBA 5UL
#define FNACCEPT_DISK_SECTOR_BYTES 128u
#define FNACCEPT_DISK_EXPECTED_SECTOR_COUNT 16UL
#define FNACCEPT_DISK_WRITE_ADDR 0x0B80u
#define FNACCEPT_DISK_READ_ADDR 0x0C00u

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
    FNACCEPT_FILE_VERIFY = 30,
    FNACCEPT_NETWORK_OPEN_STATUS = 31,
    FNACCEPT_NETWORK_HANDLE = 32,
    FNACCEPT_NETWORK_WRITE_STATUS = 34,
    FNACCEPT_NETWORK_WRITE_COUNT = 35,
    FNACCEPT_NETWORK_READ_STATUS = 37,
    FNACCEPT_NETWORK_READ_COUNT = 38,
    FNACCEPT_NETWORK_READ_FLAGS = 40,
    FNACCEPT_NETWORK_VERIFY = 41,
    FNACCEPT_NETWORK_CLOSE_STATUS = 42,
    FNACCEPT_NETWORK_REPLY = 43,
    FNACCEPT_DISK_INFO_STATUS = 55,
    FNACCEPT_DISK_FLAGS = 56,
    FNACCEPT_DISK_SLOT_VALUE = 57,
    FNACCEPT_DISK_TYPE = 58,
    FNACCEPT_DISK_SECTOR_SIZE = 59,
    FNACCEPT_DISK_SECTOR_COUNT = 61,
    FNACCEPT_DISK_LAST_ERROR = 65,
    FNACCEPT_DISK_WRITE_STATUS = 66,
    FNACCEPT_DISK_FLUSH_STATUS = 67,
    FNACCEPT_DISK_READ_STATUS = 68,
    FNACCEPT_DISK_WRITE_LENGTH = 69,
    FNACCEPT_DISK_READ_LENGTH = 71,
    FNACCEPT_DISK_VERIFY = 73,
    FNACCEPT_FILE_REJECTED_W_STATUS = 74,
    FNACCEPT_FILE_RECOVERY_ATTEMPTED = 75,
    FNACCEPT_FILE_RECOVERY_WRITE_STATUS = 76,
    FNACCEPT_FILE_RECOVERY_WRITE_DEVICE_STATUS = 77,
    FNACCEPT_FILE_RECOVERY_WRITE_RESPONSE_LENGTH = 78,
    FNACCEPT_FILE_RECOVERY_WRITE_COUNT = 80,
    FNACCEPT_FILE_RECOVERY_VERIFY = 82,
    /* These make a transport failure observable independently of the normal
     * File reply fields.  In the malformed-P-length scenario the first Write
     * has already reached FileDevice, but its terminal R is rejected. */
    FNACCEPT_FILE_FAILURE_STATUS = 83,
    FNACCEPT_FILE_RECOVERY_KIND = 84,
    FNACCEPT_CAPABILITY_STATUS = 85,
    FNACCEPT_CAPABILITY_MAX_PAYLOAD = 86,
    FNACCEPT_CAPABILITY_FLAGS = 88,
    FNACCEPT_PROC_RESULT = 89
};

enum {
    FNACCEPT_PROC_NOT_ADVERTISED = 1,
    FNACCEPT_PROC_ADVERTISED_UNEXERCISED = 2
};

static volatile uint8_t *const result = (volatile uint8_t *)FNACCEPT_RESULT_ADDR;
static uint8_t file_request[FNACCEPT_FILE_REQUEST_CAPACITY];
static uint8_t file_reply[FNACCEPT_FILE_REPLY_CAPACITY];
static uint8_t network_reply[FNACCEPT_NETWORK_REPLY_CAPACITY];
/* Keep the two public Disk-sector buffers out of the cc65 BSS.  fn_disk_*()
 * brings in the library's full packet buffers, so these fixed static Atari RAM
 * windows preserve enough conventional memory for the public transport. */
static uint8_t *const disk_write = (uint8_t *)FNACCEPT_DISK_WRITE_ADDR;
static uint8_t *const disk_read = (uint8_t *)FNACCEPT_DISK_READ_ADDR;

static uint16_t read_os_word(uint16_t address)
{
    volatile uint8_t *value = (volatile uint8_t *)address;
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

/* The final layout stays below the live Graphics 0 and cc65-stack boundary;
 * this is an observation only, before any application service call. */
static void capture_memory_snapshot(void)
{
    uint8_t offset = FNACCEPT_MEMORY_SNAPSHOT_OFFSET;
    uint16_t value;

    result[offset++] = 'H';
    result[offset++] = 'M';
    result[offset++] = 'S';
    result[offset++] = '1';
    result[offset++] = 1u;
    value = read_os_word(FNACCEPT_MEMLO_ADDR);
    result[offset++] = (uint8_t)value;
    result[offset++] = (uint8_t)(value >> 8);
    value = read_os_word(FNACCEPT_MEMTOP_ADDR);
    result[offset++] = (uint8_t)value;
    result[offset++] = (uint8_t)(value >> 8);
    result[offset++] = *(volatile uint8_t *)FNACCEPT_RAMTOP_ADDR;
    value = read_os_word(FNACCEPT_SDLSTL_ADDR);
    result[offset++] = (uint8_t)value;
    result[offset++] = (uint8_t)(value >> 8);
    value = read_os_word(FNACCEPT_SAVMSC_ADDR);
    result[offset++] = (uint8_t)value;
    result[offset++] = (uint8_t)(value >> 8);
    result[offset++] = *(volatile uint8_t *)FNACCEPT_SDMCTL_ADDR;
    result[offset++] = *(volatile uint8_t *)FNACCEPT_DMACTL_ADDR;
    result[offset] = 0u; /* no headless transition occurred */
}

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
static uint8_t build_file_request(uint8_t command, uint8_t recovery)
{
    const char *uri = recovery ? FNACCEPT_FILE_RECOVERY_URI : FNACCEPT_FILE_URI;
    const char *marker = recovery ? FNACCEPT_FILE_RECOVERY_MARKER : FNACCEPT_FILE_MARKER;
    uint8_t uri_length = recovery ? FNACCEPT_FILE_RECOVERY_URI_LEN : FNACCEPT_FILE_URI_LEN;
    uint8_t marker_length = recovery ? FNACCEPT_FILE_RECOVERY_MARKER_LEN : FNACCEPT_FILE_MARKER_LEN;
    uint8_t index;
    uint8_t at = 0;

    file_request[at++] = 1u;
    put_u16le(at, uri_length);
    at += 2u;
    for (index = 0; index < uri_length; ++index) {
        file_request[at++] = uri[index];
    }
    /* Both operations start at offset zero. */
    file_request[at++] = 0;
    file_request[at++] = 0;
    file_request[at++] = 0;
    file_request[at++] = 0;
    put_u16le(at, marker_length);
    at += 2u;
    if (command == FNACCEPT_FILE_CMD_WRITE) {
        for (index = 0; index < marker_length; ++index) {
            file_request[at++] = marker[index];
        }
    }
    return at;
}

static uint8_t valid_write_reply(const fn_raw_response_t *response, uint8_t recovery)
{
    uint16_t marker_length = recovery ? FNACCEPT_FILE_RECOVERY_MARKER_LEN : FNACCEPT_FILE_MARKER_LEN;

    return response->status == 0 && response->payload_length == 10u &&
           file_reply[0] == 1u && file_reply[1] == 0 &&
           file_reply[2] == 0 && file_reply[3] == 0 &&
           file_reply[4] == 0 && file_reply[5] == 0 &&
           file_reply[6] == 0 && file_reply[7] == 0 &&
           get_u16le(file_reply, 8) == marker_length;
}

static uint8_t valid_read_reply(const fn_raw_response_t *response, uint8_t recovery)
{
    const char *marker = recovery ? FNACCEPT_FILE_RECOVERY_MARKER : FNACCEPT_FILE_MARKER;
    uint8_t marker_length = recovery ? FNACCEPT_FILE_RECOVERY_MARKER_LEN : FNACCEPT_FILE_MARKER_LEN;
    uint8_t index;

    if (response->status != 0 ||
        response->payload_length != (uint16_t)(10u + marker_length) ||
        file_reply[0] != 1u || (file_reply[1] & (uint8_t)~0x03u) != 0 ||
        file_reply[2] != 0 || file_reply[3] != 0 ||
        file_reply[4] != 0 || file_reply[5] != 0 ||
        file_reply[6] != 0 || file_reply[7] != 0 ||
        get_u16le(file_reply, 8) != marker_length) {
        return 0;
    }
    for (index = 0; index < marker_length; ++index) {
        if (file_reply[10u + index] != marker[index]) {
            return 0;
        }
    }
    return 1;
}

static uint8_t valid_network_reply(uint16_t count, uint8_t flags)
{
    uint8_t index;

    if (count != FNACCEPT_NETWORK_REPLY_LEN || flags != FN_READ_EOF) {
        return 0;
    }
    for (index = 0; index < FNACCEPT_NETWORK_REPLY_LEN; ++index) {
        if (network_reply[index] != FNACCEPT_NETWORK_REPLY_TEXT[index]) {
            return 0;
        }
    }
    return 1;
}

static uint8_t disk_pattern(uint8_t index)
{
    return (uint8_t)((uint8_t)(index * 37u) ^ 0xa5u);
}

static void put_result_u16(uint8_t offset, uint16_t value)
{
    result[offset] = (uint8_t)value;
    result[offset + 1u] = (uint8_t)(value >> 8);
}

static void put_result_u32(uint8_t offset, uint32_t value)
{
    result[offset] = (uint8_t)value;
    result[offset + 1u] = (uint8_t)(value >> 8);
    result[offset + 2u] = (uint8_t)(value >> 16);
    result[offset + 3u] = (uint8_t)(value >> 24);
}

static uint8_t valid_disk_read(uint16_t count)
{
    uint8_t index;

    if (count != FNACCEPT_DISK_SECTOR_BYTES) {
        return 0;
    }
    for (index = 0; index < FNACCEPT_DISK_SECTOR_BYTES; ++index) {
        if (disk_read[index] != disk_pattern(index)) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    FN_TIME_T clock_value;
    fn_raw_response_t file_response;
    fn_disk_info_t disk_info;
    fn_handle_t network_handle;
    uint8_t status;
    uint8_t index;
    uint8_t request_length;
    uint8_t read_flags;
    uint8_t file_recovery;
    uint16_t transferred;

    capture_memory_snapshot();
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
         index <= FNACCEPT_FILE_RECOVERY_KIND; ++index) {
        result[index] = 0;
    }
    result[FNACCEPT_NETWORK_OPEN_STATUS] = 0xff;
    result[FNACCEPT_NETWORK_WRITE_STATUS] = 0xff;
    result[FNACCEPT_NETWORK_READ_STATUS] = 0xff;
    result[FNACCEPT_NETWORK_CLOSE_STATUS] = 0xff;
    result[FNACCEPT_DISK_INFO_STATUS] = 0xff;
    result[FNACCEPT_DISK_WRITE_STATUS] = 0xff;
    result[FNACCEPT_DISK_FLUSH_STATUS] = 0xff;
    result[FNACCEPT_DISK_READ_STATUS] = 0xff;
    result[FNACCEPT_FILE_RECOVERY_WRITE_STATUS] = 0xff;
    result[FNACCEPT_CAPABILITY_STATUS] = 0xff;
    result[FNACCEPT_CAPABILITY_MAX_PAYLOAD] = 0;
    result[FNACCEPT_CAPABILITY_MAX_PAYLOAD + 1u] = 0;
    result[FNACCEPT_CAPABILITY_FLAGS] = 0;
    result[FNACCEPT_PROC_RESULT] = 0xff;
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
    status = fn_atari_query_capabilities();
    result[FNACCEPT_CAPABILITY_STATUS] = status;
    put_result_u16(FNACCEPT_CAPABILITY_MAX_PAYLOAD, fn_atari_max_payload());
    result[FNACCEPT_CAPABILITY_FLAGS] = fn_atari_capability_flags();
    /* Q flag bit 1 says the FujiNet pulses PROC when a network session becomes
     * readable (off unless `sio.tune proc 1`).  Either answer is an accepted
     * capability outcome, not a failed mandatory test: installing a VPRCED
     * handler or synthesising a notification here would prove nothing about the
     * production endpoint (PROCTEST and its TCP page exercise it). */
    result[FNACCEPT_PROC_RESULT] =
        (result[FNACCEPT_CAPABILITY_FLAGS] & FN_ATARI_CAP_PROC) != 0u
            ? FNACCEPT_PROC_ADVERTISED_UNEXERCISED
            : FNACCEPT_PROC_NOT_ADVERTISED;
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 3;
    status = fn_clock_get(&clock_value);
    result[FNACCEPT_CLOCK_STATUS] = status;
    for (index = 0; index != 8; ++index) {
        result[FNACCEPT_CLOCK_VALUE + index] = clock_value.b[index];
    }
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 4;
    file_recovery = 0;
    request_length = build_file_request(FNACCEPT_FILE_CMD_WRITE, file_recovery);
    status = fn_raw_call(FN_DEVICE_FILE, FNACCEPT_FILE_CMD_WRITE,
                         file_request, request_length,
                         file_reply, sizeof(file_reply), &file_response);
    result[FNACCEPT_FILE_WRITE_STATUS] = status;
    result[FNACCEPT_FILE_WRITE_DEVICE_STATUS] = file_response.status;
    result[FNACCEPT_FILE_WRITE_RESPONSE_LENGTH] = (uint8_t)file_response.payload_length;
    result[FNACCEPT_FILE_WRITE_RESPONSE_LENGTH + 1u] = (uint8_t)(file_response.payload_length >> 8);
    result[FNACCEPT_FILE_WRITE_COUNT] = get_u16le(file_reply, 8);
    result[FNACCEPT_FILE_WRITE_COUNT + 1u] = file_reply[9];
    if (status != FN_OK) {
        /* Do not replay this side-effecting request.  Whether W itself was
         * rejected (no outstanding transaction) or terminal R was rejected
         * after a delivered READY reply (an outstanding response remains),
         * issue one distinct File write.  The public transport owns the
         * required P/R drain before that new W in the latter case. */
        result[FNACCEPT_FILE_REJECTED_W_STATUS] = status;
        result[FNACCEPT_FILE_FAILURE_STATUS] = status;
        result[FNACCEPT_FILE_RECOVERY_ATTEMPTED] = 1;
        result[FNACCEPT_FILE_RECOVERY_KIND] = 1;
        file_recovery = 1;
        result[FNACCEPT_STAGE] = 5;
        request_length = build_file_request(FNACCEPT_FILE_CMD_WRITE, file_recovery);
        status = fn_raw_call(FN_DEVICE_FILE, FNACCEPT_FILE_CMD_WRITE,
                             file_request, request_length,
                             file_reply, sizeof(file_reply), &file_response);
        result[FNACCEPT_FILE_RECOVERY_WRITE_STATUS] = status;
        result[FNACCEPT_FILE_RECOVERY_WRITE_DEVICE_STATUS] = file_response.status;
        result[FNACCEPT_FILE_RECOVERY_WRITE_RESPONSE_LENGTH] = (uint8_t)file_response.payload_length;
        result[FNACCEPT_FILE_RECOVERY_WRITE_RESPONSE_LENGTH + 1u] = (uint8_t)(file_response.payload_length >> 8);
        result[FNACCEPT_FILE_RECOVERY_WRITE_COUNT] = (uint8_t)get_u16le(file_reply, 8);
        result[FNACCEPT_FILE_RECOVERY_WRITE_COUNT + 1u] = file_reply[9];
        if (status != FN_OK || !valid_write_reply(&file_response, file_recovery)) {
            set_signature('A', 'C', 'F', '!');
            return 1;
        }
        result[FNACCEPT_FILE_RECOVERY_VERIFY] = 1;
    } else if (!valid_write_reply(&file_response, file_recovery)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 6;
    request_length = build_file_request(FNACCEPT_FILE_CMD_READ, file_recovery);
    status = fn_raw_call(FN_DEVICE_FILE, FNACCEPT_FILE_CMD_READ,
                         file_request, request_length,
                         file_reply, sizeof(file_reply), &file_response);
    result[FNACCEPT_FILE_READ_STATUS] = status;
    result[FNACCEPT_FILE_READ_DEVICE_STATUS] = file_response.status;
    result[FNACCEPT_FILE_READ_RESPONSE_LENGTH] = (uint8_t)file_response.payload_length;
    result[FNACCEPT_FILE_READ_RESPONSE_LENGTH + 1u] = (uint8_t)(file_response.payload_length >> 8);
    result[FNACCEPT_FILE_READ_COUNT] = (uint8_t)get_u16le(file_reply, 8);
    result[FNACCEPT_FILE_READ_COUNT + 1u] = file_reply[9];
    if (status != FN_OK || !valid_read_reply(&file_response, file_recovery)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_FILE_VERIFY] = 1;
    result[FNACCEPT_STAGE] = 7;
    network_handle = FN_INVALID_HANDLE;
    status = fn_open(&network_handle, FN_METHOD_POST, FNACCEPT_NETWORK_URI, 0);
    result[FNACCEPT_NETWORK_OPEN_STATUS] = status;
    result[FNACCEPT_NETWORK_HANDLE] = (uint8_t)network_handle;
    result[FNACCEPT_NETWORK_HANDLE + 1u] = (uint8_t)(network_handle >> 8);
    if (status != FN_OK || network_handle == FN_INVALID_HANDLE) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 8;
    transferred = 0;
    status = fn_write(network_handle, 0,
                      (const uint8_t *)FNACCEPT_NETWORK_WRITE_MARKER,
                      FNACCEPT_NETWORK_WRITE_MARKER_LEN, &transferred);
    result[FNACCEPT_NETWORK_WRITE_STATUS] = status;
    result[FNACCEPT_NETWORK_WRITE_COUNT] = (uint8_t)transferred;
    result[FNACCEPT_NETWORK_WRITE_COUNT + 1u] = (uint8_t)(transferred >> 8);
    if (status != FN_OK || transferred != FNACCEPT_NETWORK_WRITE_MARKER_LEN) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 9;
    transferred = 0;
    read_flags = 0;
    status = fn_read(network_handle, 0, network_reply, sizeof(network_reply),
                     &transferred, &read_flags);
    result[FNACCEPT_NETWORK_READ_STATUS] = status;
    result[FNACCEPT_NETWORK_READ_COUNT] = (uint8_t)transferred;
    result[FNACCEPT_NETWORK_READ_COUNT + 1u] = (uint8_t)(transferred >> 8);
    result[FNACCEPT_NETWORK_READ_FLAGS] = read_flags;
    for (index = 0; index < FNACCEPT_NETWORK_REPLY_LEN; ++index) {
        result[FNACCEPT_NETWORK_REPLY + index] = network_reply[index];
    }
    if (status != FN_OK || !valid_network_reply(transferred, read_flags)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_NETWORK_VERIFY] = 1;
    result[FNACCEPT_STAGE] = 10;
    status = fn_close(network_handle);
    result[FNACCEPT_NETWORK_CLOSE_STATUS] = status;
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 11;
    status = fn_disk_info(FNACCEPT_DISK_SLOT, &disk_info);
    result[FNACCEPT_DISK_INFO_STATUS] = status;
    result[FNACCEPT_DISK_FLAGS] = disk_info.flags;
    result[FNACCEPT_DISK_SLOT_VALUE] = disk_info.slot;
    result[FNACCEPT_DISK_TYPE] = disk_info.type;
    put_result_u16(FNACCEPT_DISK_SECTOR_SIZE, disk_info.sector_size);
    put_result_u32(FNACCEPT_DISK_SECTOR_COUNT, disk_info.sector_count);
    result[FNACCEPT_DISK_LAST_ERROR] = disk_info.last_error;
    if (status != FN_OK || disk_info.slot != FNACCEPT_DISK_SLOT ||
        (disk_info.flags & 0x13u) != 0x11u ||
        disk_info.sector_size != FNACCEPT_DISK_SECTOR_BYTES ||
        disk_info.sector_count != FNACCEPT_DISK_EXPECTED_SECTOR_COUNT) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 12;
    for (index = 0; index < FNACCEPT_DISK_SECTOR_BYTES; ++index) {
        disk_write[index] = disk_pattern(index);
    }
    status = fn_disk_write_sector(FNACCEPT_DISK_SLOT, FNACCEPT_DISK_LBA,
                                  disk_write, FNACCEPT_DISK_SECTOR_BYTES);
    result[FNACCEPT_DISK_WRITE_STATUS] = status;
    put_result_u16(FNACCEPT_DISK_WRITE_LENGTH,
                   status == FN_OK ? FNACCEPT_DISK_SECTOR_BYTES : 0);
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 13;
    status = fn_disk_flush(FNACCEPT_DISK_SLOT);
    result[FNACCEPT_DISK_FLUSH_STATUS] = status;
    if (status != FN_OK) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_STAGE] = 14;
    transferred = 0;
    status = fn_disk_read_sector(FNACCEPT_DISK_SLOT, FNACCEPT_DISK_LBA,
                                 disk_read, FNACCEPT_DISK_SECTOR_BYTES, &transferred);
    result[FNACCEPT_DISK_READ_STATUS] = status;
    put_result_u16(FNACCEPT_DISK_READ_LENGTH, transferred);
    if (status != FN_OK || !valid_disk_read(transferred)) {
        set_signature('A', 'C', 'F', '!');
        return 1;
    }

    result[FNACCEPT_DISK_VERIFY] = 1;
    result[FNACCEPT_STAGE] = 15;
    set_signature('A', 'D', 'P', '!');
    for (;;) {
        if (++result[FNACCEPT_HEARTBEAT] == 0) {
            ++result[FNACCEPT_HEARTBEAT_HIGH];
        }
    }
}
