/*
 * Munt386-CSE -- CSE storage backend.
 *
 * Milestone 1 (this build): a RAM-backed virtual disk.  The image lives in
 * banked SRAM behind the page cache (banking.c), exactly like any other
 * guest memory; it is volatile by design and never touches flash.
 *
 * Milestone 2 (designed, not enabled): persistence in the reserved flash
 * region (flash pages 0xEC-0xF3, see docs/CSE_MEMORY_MAP.md).  Flash writes
 * require the port-0x14 unlock plus execute-from-RAM erase/program routines
 * and are NEVER performed automatically -- only after an explicit user
 * action, verified on hardware, per docs/RECOVERY.md.  Until that on-device
 * verification happens, cse_storage_is_persistent() returns 0 and every
 * write stays in RAM.
 */
#include "cse_disk.h"
#include "cse_hardware.h"
#include "cse_ports.h"
#include "platform.h"
#include <string.h>

/* Capacity guard: the RAM disk must leave room for code + video + cache.
 * 2880 sectors = 1440 KiB (a standard floppy image).  The startup decides
 * the actual size from the measured SRAM budget; this is the ceiling. */
#define CSE_DISK_MAX_SECTORS 2880u

static uint32_t disk_sectors;
static int      disk_readonly;
static uint8_t *disk_ram;        /* behind the banking page cache */

uint32_t cse_disk_max_sectors(void) { return CSE_DISK_MAX_SECTORS; }

int cse_storage_is_persistent(void)
{
    /* Reserved region exists (docs/CSE_MEMORY_MAP.md) but the write path is
     * intentionally not enabled until hardware-verified. */
    return 0;
}

int cse_disk_attach(uint8_t *ram_backing, uint32_t sector_count, int readonly)
{
    if (!ram_backing) return -1;
    if (sector_count == 0 || sector_count > CSE_DISK_MAX_SECTORS) return -2;
    disk_ram = ram_backing;
    disk_sectors = sector_count;
    disk_readonly = readonly ? 1 : 0;
    return 0;
}

uint32_t cse_disk_sectors(void)
{
    return disk_sectors;
}

int cse_disk_read(uint8_t *buf, uint32_t lba, uint32_t count)
{
    if (!disk_ram || !buf || count == 0) return -1;
    if (lba >= disk_sectors || count > disk_sectors - lba) return -2;
    memcpy(buf, disk_ram + (size_t)lba * 512u, (size_t)count * 512u);
    return 0;
}

int cse_disk_write(const uint8_t *buf, uint32_t lba, uint32_t count)
{
    if (!disk_ram || !buf || count == 0) return -1;
    if (disk_readonly) return -3;
    if (lba >= disk_sectors || count > disk_sectors - lba) return -2;
    memcpy(disk_ram + (size_t)lba * 512u, buf, (size_t)count * 512u);
    return 0;
}
