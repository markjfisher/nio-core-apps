#include "fujinet_disk_iface.h"

#include <clib/alib_protos.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <exec/io.h>
#include <libraries/expansion.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include <stdio.h>
#include <string.h>

#include <fujinet-amiga-disk/support.h>

typedef struct node_snapshot {
    BOOL present;
    BOOL active;
    ULONG bpt, surfaces, low, high, dostype;
    struct MsgPort *task;
} node_snapshot_t;
struct ExpansionBase *ExpansionBase;

static void usage(void) { puts("Usage: FMOUNT slot [DN0:|...|DN7:] [RO|RW]"); }
static int drive_to_unit(const char *s) {
    if (!s || !*s)
        return -1;
    if (s[0] >= '0' && s[0] <= '7' && s[1] == '\0')
        return s[0] - '0';
    if ((s[0] == 'D' || s[0] == 'd') && (s[1] == 'N' || s[1] == 'n') && s[2] >= '0' &&
        s[2] <= '7' && (s[3] == ':' || s[3] == '\0'))
        return s[2] - '0';
    return -1;
}
static int is_ro(const char *s) {
    return s && (s[0] == 'R' || s[0] == 'r') && (s[1] == 'O' || s[1] == 'o') && !s[2];
}
static int is_rw(const char *s) {
    return s && (s[0] == 'R' || s[0] == 'r') && (s[1] == 'W' || s[1] == 'w') && !s[2];
}

static void snapshot_node(int unit, node_snapshot_t *out) {
    char name[4];
    struct DosList *list, *entry;
    struct FileSysStartupMsg *startup;
    struct DosEnvec *env;
    memset(out, 0, sizeof(*out));
    sprintf(name, "DN%d", unit);
    list = LockDosList(LDF_READ | LDF_DEVICES);
    if (!list)
        return;
    entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
    if (entry) {
        out->present = TRUE;
        out->task = entry->dol_Task;
        out->active = out->task != NULL;
        startup = entry->dol_misc.dol_handler.dol_Startup
                      ? (struct FileSysStartupMsg *)BADDR(entry->dol_misc.dol_handler.dol_Startup)
                      : NULL;
        env = startup && startup->fssm_Environ ? (struct DosEnvec *)BADDR(startup->fssm_Environ)
                                               : NULL;
        if (env) {
            out->bpt = env->de_BlocksPerTrack;
            out->surfaces = env->de_Surfaces;
            out->low = env->de_LowCyl;
            out->high = env->de_HighCyl;
            out->dostype = env->de_DosType;
        }
    }
    UnLockDosList(LDF_READ | LDF_DEVICES);
}

static int retire_handler(int unit, const node_snapshot_t *snapshot) {
    int tries;
    node_snapshot_t current;
    if (!snapshot->active)
        return 0;
    (void)DoPkt(snapshot->task, ACTION_DIE, 0, 0, 0, 0, 0);
    for (tries = 0; tries < 20; ++tries) {
        snapshot_node(unit, &current);
        if (current.present && !current.active)
            return 0;
        Delay(1);
    }
    fprintf(stderr, "Cannot retire DN%d handler\n", unit);
    return 30;
}

static int update_inactive_envec(int unit, const fujinet_disk_media_profile_t *profile,
                                 uint32_t dostype) {
    char name[4];
    struct DosList *list, *entry;
    struct FileSysStartupMsg *startup;
    struct DosEnvec *env;
    sprintf(name, "DN%d", unit);
    list = LockDosList(LDF_READ | LDF_DEVICES);
    if (!list)
        return 30;
    entry = FindDosEntry(list, (CONST_STRPTR)name, LDF_DEVICES);
    startup = entry && entry->dol_misc.dol_handler.dol_Startup
                  ? (struct FileSysStartupMsg *)BADDR(entry->dol_misc.dol_handler.dol_Startup)
                  : NULL;
    env = startup && startup->fssm_Environ ? (struct DosEnvec *)BADDR(startup->fssm_Environ) : NULL;
    if (!entry || entry->dol_Task || !env) {
        UnLockDosList(LDF_READ | LDF_DEVICES);
        return 30;
    }
    env->de_Surfaces = profile->surfaces;
    env->de_BlocksPerTrack = profile->blocks_per_track;
    env->de_LowCyl = profile->low_cylinder;
    env->de_HighCyl = profile->high_cylinder;
    env->de_DosType = dostype;
    UnLockDosList(LDF_READ | LDF_DEVICES);
    return 0;
}

static int create_node(int unit, const fujinet_disk_media_profile_t *profile, uint32_t dostype) {
    static char names[8][4];
    static const char device[] = FUJINET_DISK_DEVICE_NAME;
    fujinet_disk_dos_envec_t env;
    ULONG packet[24];
    struct DeviceNode *node;
    if (fujinet_disk_build_dos_envec(profile, dostype, &env) != FN_OK)
        return 30;
    printf("FMOUNT NODE before_make unit=%d\n", unit);
    sprintf(names[unit], "DN%d", unit);
    packet[0] = (ULONG)names[unit];
    packet[1] = (ULONG)device;
    packet[2] = (ULONG)unit;
    packet[3] = 0;
    fujinet_disk_serialize_dos_envec(&env, packet + 4);
    node = MakeDosNode(packet);
    if (!node) {
        puts("FMOUNT NODE make=failed");
        return 30;
    }
    node->dn_StackSize = 32768;
    node->dn_Priority = 5;
    node->dn_GlobalVec = (BPTR)-1;
    node->dn_Handler = 0;
    if (!AddDosNode(0, 0, node)) {
        puts("FMOUNT NODE add=failed");
        return 30;
    }
    printf("FMOUNT NODE add=ok unit=%d bpt=%lu dostype=%08lx\n", unit,
           (unsigned long)env.de_BlocksPerTrack, (unsigned long)env.de_DosType);
    return 0;
}

static void print_added_node(int unit) {
    node_snapshot_t node;
    snapshot_node(unit, &node);
    printf("FMOUNT NODE post_add present=%ld task=%08lx bpt=%lu dostype=%08lx\n",
           (long)node.present, (unsigned long)node.task, (unsigned long)node.bpt,
           (unsigned long)node.dostype);
}

int main(int argc, char **argv) {
    long slot_val = 0;
    uint8_t slot, readonly = 0;
    int unit, i;
    char dos_name[5];
    struct MsgPort *port;
    struct IOExtTD *request;
    struct fujinet_disk_catalog_mount catalog;
    struct fujinet_disk_catalog_inspection inspection;
    fujinet_disk_media_profile_t profile;
    node_snapshot_t node;
    uint32_t dostype;
    BOOL compatible, inhibited = FALSE;
    LONG result, uninhibit;
    if (argc < 2 || argc > 4 || argv[1][0] == '?') {
        usage();
        return 10;
    }
    for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; ++i)
        slot_val = slot_val * 10 + argv[1][i] - '0';
    if (argv[1][i] || slot_val > 255) {
        puts("Bad slot");
        return 10;
    }
    slot = (uint8_t)slot_val;
    unit = (int)slot;
    for (i = 2; i < argc; ++i) {
        int parsed = drive_to_unit(argv[i]);
        if (parsed >= 0)
            unit = parsed;
        else if (is_ro(argv[i]))
            readonly = 1;
        else if (is_rw(argv[i]))
            readonly = 0;
        else {
            usage();
            return 10;
        }
    }
    if (unit < 0 || unit > FUJINET_DISK_MAX_UNIT) {
        puts("Bad drive");
        return 10;
    }
    port = CreatePort(NULL, 0);
    request = port ? (struct IOExtTD *)CreateExtIO(port, sizeof(*request)) : NULL;
    if (!request || OpenDevice((CONST_STRPTR)FUJINET_DISK_DEVICE_NAME, (ULONG)unit,
                               (struct IORequest *)request, 0) != 0) {
        fprintf(stderr, "FMOUNT OPEN_DEVICE failed request=%p error=%ld\n", (void *)request,
                request ? (long)request->iotd_Req.io_Error : (long)IoErr());
        return 20;
    }
    memset(&inspection, 0, sizeof(inspection));
    inspection.catalog_slot = slot;
    request->iotd_Req.io_Command = FUJINET_DISK_CMD_INSPECT_CATALOG;
    request->iotd_Req.io_Data = &inspection;
    request->iotd_Req.io_Length = sizeof(inspection);
    result = DoIO((struct IORequest *)request);
    if (result != 0 ||
        fujinet_disk_classify_media_profile(&inspection.inspection.media, &profile) != FN_OK ||
        fujinet_disk_classify_filesystem(inspection.inspection.boot_bytes,
                                         inspection.inspection.boot_length, &dostype) != FN_OK) {
        fprintf(
            stderr, "FMOUNT INSPECT failed io=%ld profile=%u filesystem=%u\n", (long)result,
            (unsigned)fujinet_disk_classify_media_profile(&inspection.inspection.media, &profile),
            (unsigned)fujinet_disk_classify_filesystem(
                inspection.inspection.boot_bytes, inspection.inspection.boot_length, &dostype));
        goto fail;
    }
    puts("FMOUNT INSPECT=ok");
    snapshot_node(unit, &node);
    {
        BOOL create_after_mount = !node.present;
        compatible = !node.present ||
                     (node.bpt == profile.blocks_per_track && node.surfaces == profile.surfaces &&
                      node.low == profile.low_cylinder && node.high == profile.high_cylinder &&
                      node.dostype == dostype);
        sprintf(dos_name, "DN%d:", unit);
        if (compatible && node.active) {
            if (!Inhibit((CONST_STRPTR)dos_name, DOSTRUE)) {
                fprintf(stderr, "Cannot inhibit %s\n", dos_name);
                goto fail;
            }
            inhibited = TRUE;
        } else if (!compatible && retire_handler(unit, &node) != 0)
            goto fail;
        catalog.catalog_slot = slot;
        catalog.writable = readonly ? 0 : 1;
        request->iotd_Req.io_Command = FUJINET_DISK_CMD_MOUNT_CATALOG;
        request->iotd_Req.io_Data = &catalog;
        request->iotd_Req.io_Length = sizeof(catalog);
        result = DoIO((struct IORequest *)request);
        printf("FMOUNT MOUNT_CATALOG rc=%ld\n", (long)result);
        if (result == 0 && create_after_mount) {
            ExpansionBase =
                (struct ExpansionBase *)OpenLibrary((CONST_STRPTR) "expansion.library", 0);
            if (!ExpansionBase || create_node(unit, &profile, dostype) != 0) {
                fprintf(stderr, "Mounted media but cannot create DN%d\n", unit);
                result = 30;
            } else {
                print_added_node(unit);
            }
            if (ExpansionBase) {
                CloseLibrary((struct Library *)ExpansionBase);
                ExpansionBase = NULL;
            }
        }
        if (result == 0 && !compatible && update_inactive_envec(unit, &profile, dostype) != 0)
            result = 30;
        if (inhibited) {
            uninhibit = Inhibit((CONST_STRPTR)dos_name, DOSFALSE);
            if (result == 0 && !uninhibit)
                result = 31;
        }
        CloseDevice((struct IORequest *)request);
        DeleteExtIO((struct IORequest *)request);
        DeletePort(port);
        if (result != 0) {
            fprintf(stderr, "Mount failed (%ld)\n", result);
            return 10;
        }
        printf("Mounted slot %u on DN%d:\n", (unsigned)slot, unit);
        return 0;
    }
fail:
    CloseDevice((struct IORequest *)request);
    DeleteExtIO((struct IORequest *)request);
    DeletePort(port);
    return 10;
}
