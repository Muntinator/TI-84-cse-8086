/*
 * Munt86 -- virtual block device.
 *
 * The DOS/Windows image is NEVER hard-coded in the emulator.  A disk is a
 * block device the firmware or host attaches: on the CSE it is backed by flash
 * storage, on the host by an attached image buffer (loaded from a file during
 * development).
 *
 * Geometry follows the classic CHS convention:
 *   LBA = (cylinder * heads + head) * sectors + (sector - 1)
 */
#include "munt386.h"
#include <string.h>

int disk_attach(disk_t *d, uint8_t *image, uint32_t size, int readonly, int is_hdd)
{
    if (!d || !image || size < DISK_SECTOR_SIZE)
        return -1;
    if (size % DISK_SECTOR_SIZE)
        return -2;

    d->image = image;
    d->image_size = size;
    d->readonly = readonly ? 1 : 0;
    d->present = 1;

    if (is_hdd) {
        /* Derive plausible geometry from the image size. */
        uint32_t total = size / DISK_SECTOR_SIZE;
        uint8_t  spt = 17;
        uint8_t  heads = 4;
        uint16_t cyls = (uint16_t)(total / ((uint32_t)spt * heads));
        if (cyls == 0) { cyls = 1; }
        d->sectors = spt;
        d->heads = heads;
        d->cylinders = cyls;
    } else {
        d->sectors = 9;
        d->heads = 2;
        d->cylinders = (uint16_t)((size / DISK_SECTOR_SIZE) / (9u * 2u));
        if (d->cylinders == 0) d->cylinders = 1;
    }
    return 0;
}

void disk_detach(disk_t *d)
{
    if (!d) return;
    d->present = 0;
    d->image = NULL;
    d->image_size = 0;
}

int disk_get_geometry(disk_t *d, uint16_t *cylinders, uint8_t *heads, uint8_t *sectors)
{
    if (!d || !d->present)
        return -1;
    if (cylinders) *cylinders = d->cylinders;
    if (heads)     *heads = d->heads;
    if (sectors)   *sectors = d->sectors;
    return 0;
}

int disk_read_sector(disk_t *d, uint32_t lba, uint8_t *buf)
{
    if (!d || !d->present || !buf)
        return -1;
    if ((lba + 1u) * DISK_SECTOR_SIZE > d->image_size)
        return -2;
    memcpy(buf, d->image + (size_t)lba * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    return 0;
}

int disk_write_sector(disk_t *d, uint32_t lba, const uint8_t *buf)
{
    if (!d || !d->present || !buf)
        return -1;
    if ((lba + 1u) * DISK_SECTOR_SIZE > d->image_size)
        return -2;
    if (d->readonly)
        return -3;
    memcpy(d->image + (size_t)lba * DISK_SECTOR_SIZE, buf, DISK_SECTOR_SIZE);
    return 0;
}
