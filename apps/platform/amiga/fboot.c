/*
 * Activate FujiNet's configured default disk as Amiga DN0:.
 *
 * The firmware owns the policy: a configured boot/default image occupies
 * DiskDevice unit zero only when runtime recovery did not restore a user
 * image there.  AmigaDOS still needs a DN0: handler, which is what this
 * command supplies after asking NIO to restore the configured image.
 */
#include "fujinet_disk_iface.h"

#include <clib/alib_protos.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <libraries/expansion.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include <stdio.h>
#include <string.h>

#include <fujinet-amiga-disk/support.h>

struct ExpansionBase *ExpansionBase;

static void usage(void)
{
    puts("Usage: FBOOT");
}

static int open_unit_zero(struct MsgPort **port_out, struct IOExtTD **io_out)
{
    struct MsgPort *port = CreatePort(NULL, 0);
    struct IOExtTD *io = port ? (struct IOExtTD *)CreateExtIO(port, sizeof(*io)) : NULL;

    if (!io || OpenDevice((CONST_STRPTR)FUJINET_DISK_DEVICE_NAME, 0,
                          (struct IORequest *)io, 0) != 0) {
        if (io) DeleteExtIO((struct IORequest *)io);
        if (port) DeletePort(port);
        return 0;
    }
    *port_out = port;
    *io_out = io;
    return 1;
}

static void close_unit(struct MsgPort *port, struct IOExtTD *io)
{
    if (io) {
        CloseDevice((struct IORequest *)io);
        DeleteExtIO((struct IORequest *)io);
    }
    if (port) DeletePort(port);
}

static int default_disk_is_present(struct IOExtTD *io)
{
    io->iotd_Req.io_Command = TD_CHANGESTATE;
    io->iotd_Req.io_Length = 0;
    return DoIO((struct IORequest *)io) == 0 && io->iotd_Req.io_Actual == 0;
}

static int restore_default_disk(struct IOExtTD *io)
{
    io->iotd_Req.io_Command = FUJINET_DISK_CMD_RESTORE_BOOT;
    io->iotd_Req.io_Data = NULL;
    io->iotd_Req.io_Length = 0;
    return DoIO((struct IORequest *)io) == 0;
}

/* The shipped default disk contract is an 880 KiB FFS ADF.  Validate it
 * before attaching a DOS handler, so a bad firmware URI cannot leave DN0:
 * using an incorrect geometry or filesystem. */
static int validate_default_disk(struct IOExtTD *io)
{
    struct DriveGeometry geometry;
    UBYTE boot_block[512];
    uint32_t dostype;

    memset(&geometry, 0, sizeof(geometry));
    io->iotd_Req.io_Command = TD_GETGEOMETRY;
    io->iotd_Req.io_Data = &geometry;
    io->iotd_Req.io_Length = sizeof(geometry);
    if (DoIO((struct IORequest *)io) != 0 || geometry.dg_SectorSize != 512 ||
        geometry.dg_TotalSectors != 1760 || geometry.dg_TrackSectors != 11)
        return 0;

    io->iotd_Req.io_Command = CMD_READ;
    io->iotd_Req.io_Data = boot_block;
    io->iotd_Req.io_Length = sizeof(boot_block);
    io->iotd_Req.io_Offset = 0;
    if (DoIO((struct IORequest *)io) != 0 || io->iotd_Req.io_Actual != sizeof(boot_block))
        return 0;
    return fujinet_disk_classify_filesystem(boot_block, sizeof(boot_block), &dostype) == FN_OK &&
           dostype == FUJINET_AMIGA_DOS_FFS;
}

#ifndef __KICK13__
static int node_state(void)
{
    struct DosList *list;
    struct DosList *node;
    int state = 0;

    list = LockDosList(LDF_READ | LDF_DEVICES);
    if (!list) return -1;
    node = FindDosEntry(list, (CONST_STRPTR)"DN0", LDF_DEVICES);
    if (node) state = node->dol_Task ? 2 : 1;
    UnLockDosList(LDF_READ | LDF_DEVICES);
    return state; /* 0 absent, 1 inactive, 2 active */
}

static int create_dn0(void)
{
    static const char name[] = "DN0";
    static const char device[] = FUJINET_DISK_DEVICE_NAME;
    fujinet_disk_media_profile_t profile;
    fujinet_disk_dos_envec_t envec;
    ULONG packet[24];
    struct DeviceNode *node;

    memset(&profile, 0, sizeof(profile));
    profile.kind = FUJINET_DISK_MEDIA_PROFILE_DD_ADF;
    profile.block_size = 512;
    profile.surfaces = 2;
    profile.blocks_per_track = 11;
    profile.low_cylinder = 0;
    profile.high_cylinder = 79;
    profile.reserved_blocks = 2;
    if (fujinet_disk_build_dos_envec(&profile, FUJINET_AMIGA_DOS_FFS, &envec) != FN_OK)
        return 0;
    packet[0] = (ULONG)name;
    packet[1] = (ULONG)device;
    packet[2] = 0;
    packet[3] = 0;
    fujinet_disk_serialize_dos_envec(&envec, packet + 4);
    node = MakeDosNode(packet);
    if (!node) return 0;
    node->dn_StackSize = 32768;
    node->dn_Priority = 5;
    node->dn_GlobalVec = (BPTR)-1;
    node->dn_Handler = 0;
    return AddDosNode(0, 0, node) ? 1 : 0;
}

static int start_inactive_dn0(void)
{
    return Execute((CONST_STRPTR)"C:Mount DN0:", 0, 0) ? 1 : 0;
}
#endif

int main(int argc, char **argv)
{
    struct MsgPort *port;
    struct IOExtTD *io;
#ifndef __KICK13__
    int state;
#endif

    if (argc != 1 || (argc > 1 && argv[1][0] == '?')) {
        usage();
        return 10;
    }

#ifndef __KICK13__
    state = node_state();
    if (state < 0) {
        puts("Cannot inspect the AmigaDOS device list");
        return 20;
    }
    if (state == 2) {
        puts("DN0: is already active; default disk not restored");
        return 10;
    }
#endif

    if (!open_unit_zero(&port, &io)) {
        puts("Cannot open fujinet-disk.device unit 0");
        return 20;
    }
    if (!restore_default_disk(io)) {
        close_unit(port, io);
        puts("Default disk restore failed");
        return 20;
    }
    if (!default_disk_is_present(io)) {
        close_unit(port, io);
        puts("No configured default disk");
        return 0;
    }
    if (!validate_default_disk(io)) {
        close_unit(port, io);
        puts("Default disk must be an FFS/DD ADF");
        return 10;
    }
    close_unit(port, io);

#ifdef __KICK13__
    if (!Execute((CONST_STRPTR)"C:Mount DN0:", 0, 0)) {
        puts("Cannot start static DN0: handler");
        return 20;
    }
#else
    if (state == 0) {
        ExpansionBase = (struct ExpansionBase *)OpenLibrary(
            (CONST_STRPTR)"expansion.library", 0);
        if (!ExpansionBase || !create_dn0()) {
            if (ExpansionBase) {
                CloseLibrary((struct Library *)ExpansionBase);
                ExpansionBase = NULL;
            }
            puts("Cannot create DN0: handler");
            return 20;
        }
        CloseLibrary((struct Library *)ExpansionBase);
        ExpansionBase = NULL;
    }
    if (state == 1 && !start_inactive_dn0()) {
        puts("Cannot start inactive DN0: handler");
        return 20;
    }
#endif
    puts("Default disk active on DN0: (FFS, DD)");
    return 0;
}
