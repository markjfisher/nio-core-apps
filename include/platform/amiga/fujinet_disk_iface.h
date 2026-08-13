#ifndef FUJINET_DISK_IFACE_H
#define FUJINET_DISK_IFACE_H

/*
 * Shared interface to fujinet-disk.device for Amiga platform tools.
 * This header mirrors the relevant subset of the driver's fujinet_disk_device.h
 * so that nio-core-apps can call the resident device without a source dependency
 * on the driver repository.
 *
 * Keep in sync with repos/fujinet-nio-driver/amiga/include/fujinet_disk_device.h.
 */

#include <exec/io.h>

/* Private commands beyond the complete trackdisk.device range.
 * CMD_NONSTD itself is TD_MOTOR and must never be repurposed. */
#define FUJINET_DISK_CMD_MOUNT          (CMD_NONSTD + 0x100)
#define FUJINET_DISK_CMD_TRACE          (CMD_NONSTD + 0x101)
#define FUJINET_DISK_CMD_MOUNT_WRITABLE (CMD_NONSTD + 0x102)
#define FUJINET_DISK_CMD_MOUNT_CATALOG  (CMD_NONSTD + 0x103)

struct fujinet_disk_catalog_mount {
    UBYTE catalog_slot;
    UBYTE writable;
};

#define FUJINET_DISK_DEVICE_NAME "fujinet-disk.device"
#define FUJINET_DISK_MAX_UNIT    7

#endif /* FUJINET_DISK_IFACE_H */
