/*
 * Munt386-CSE -- CSE storage backend interface.
 */
#ifndef CSE_DISK_H
#define CSE_DISK_H

#include <stdint.h>

/* Attach a RAM-backed virtual disk.  ram_backing must hold
 * sector_count * 512 bytes inside the banked SRAM arena. */
int      cse_disk_attach(uint8_t *ram_backing, uint32_t sector_count, int readonly);
uint32_t cse_disk_sectors(void);
uint32_t cse_disk_max_sectors(void);

/* Sector I/O used by the disk_attach() shim in cse_main.c. */
int cse_disk_read (uint8_t *buf, uint32_t lba, uint32_t count);
int cse_disk_write(const uint8_t *buf, uint32_t lba, uint32_t count);

/* Persistent flash storage status (0 until hardware-verified). */
int cse_storage_is_persistent(void);

#endif /* CSE_DISK_H */
