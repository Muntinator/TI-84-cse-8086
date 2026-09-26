/* Munt386 -- disk block-device tests. */
#include "../test_util.h"

void test_disk(void);

void test_disk(void)
{
    /* Geometry derivation for a 1.44 MB floppy-sized image. */
    {
        disk_t d;
        memset(&d, 0, sizeof d);
        uint8_t *img = (uint8_t *)calloc(1, 9 * 2 * 80 * 512);
        CHECK_EQ(disk_attach(&d, img, 9 * 2 * 80 * 512, 0, 0), 0);
        uint16_t cyl; uint8_t heads, spt;
        CHECK_EQ(disk_get_geometry(&d, &cyl, &heads, &spt), 0);
        CHECK_EQ(cyl, 80);
        CHECK_EQ(heads, 2);
        CHECK_EQ(spt, 9);
        free(img);
    }
    /* Read/write round trip */
    {
        disk_t d;
        memset(&d, 0, sizeof d);
        uint8_t *img = (uint8_t *)calloc(1, 20 * 512);
        disk_attach(&d, img, 20 * 512, 0, 1);
        uint8_t buf[512];
        memset(buf, 0xAB, sizeof buf);
        CHECK_EQ(disk_write_sector(&d, 5, buf), 0);
        uint8_t rd[512];
        CHECK_EQ(disk_read_sector(&d, 5, rd), 0);
        CHECK_EQ(rd[0], 0xAB);
        CHECK_EQ(rd[511], 0xAB);
        free(img);
    }
    /* Read-only device refuses writes */
    {
        disk_t d;
        memset(&d, 0, sizeof d);
        uint8_t *img = (uint8_t *)calloc(1, 4 * 512);
        disk_attach(&d, img, 4 * 512, 1, 0);
        uint8_t buf[512] = { 0 };
        CHECK(disk_write_sector(&d, 0, buf) != 0);
        free(img);
    }
    /* Out-of-range access is rejected */
    {
        disk_t d;
        memset(&d, 0, sizeof d);
        uint8_t *img = (uint8_t *)calloc(1, 4 * 512);
        disk_attach(&d, img, 4 * 512, 0, 0);
        uint8_t buf[512];
        CHECK(disk_read_sector(&d, 4, buf) != 0);
        CHECK(disk_read_sector(&d, 100, buf) != 0);
        free(img);
    }
    /* Invalid size rejected */
    {
        disk_t d;
        memset(&d, 0, sizeof d);
        uint8_t img[100];
        CHECK(disk_attach(&d, img, 100, 0, 0) != 0);
    }
}
