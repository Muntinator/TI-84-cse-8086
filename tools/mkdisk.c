/*
 * Munt386 -- disk image utility.
 *
 * Creates EMPTY disk images, inspects geometry, and installs a boot sector from
 * a raw file.  It never generates or embeds operating-system binaries; users
 * supply legally obtained media separately.
 *
 *   mkdisk new  <path> <cylinders> <heads> <sectors>   create an empty image
 *   mkdisk info <path>                                 print size / geometry
 *   mkdisk boot <path> <bootsector.bin>                write a 512-byte boot sector
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECTOR 512

static int cmd_new(const char *path, int cyl, int heads, int spt)
{
    if (cyl <= 0 || heads <= 0 || spt <= 0 || spt > 63) {
        fprintf(stderr, "mkdisk: invalid geometry\n");
        return 1;
    }
    uint32_t total = (uint32_t)cyl * heads * spt;
    FILE *f = fopen(path, "wb");
    if (!f) { perror("mkdisk"); return 1; }
    uint8_t zero[SECTOR];
    memset(zero, 0, sizeof zero);
    for (uint32_t i = 0; i < total; i++) fwrite(zero, 1, SECTOR, f);
    fclose(f);
    printf("mkdisk: created %s (%u sectors, %.2f MiB)\n",
           path, total, (double)total * SECTOR / (1024.0 * 1024.0));
    return 0;
}

static int cmd_info(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("mkdisk"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    if (n < SECTOR) { fprintf(stderr, "mkdisk: image too small\n"); return 1; }
    printf("mkdisk: %s\n", path);
    printf("  size      : %ld bytes (%.2f MiB)\n", n, n / (1024.0 * 1024.0));
    printf("  sectors   : %ld\n", n / SECTOR);
    /* Detect a boot signature. */
    uint8_t sig[2] = { 0, 0 };
    f = fopen(path, "rb");
    if (f) {
        fseek(f, 510, SEEK_SET);
        if (fread(sig, 1, 2, f) != 2) { /* short read: leave signature unset */ }
        fclose(f);
    }
    printf("  boot sig  : %s\n", (sig[0] == 0x55 && sig[1] == 0xAA) ? "0x55AA (bootable)" : "none");
    return 0;
}

static int cmd_boot(const char *path, const char *src)
{
    FILE *in = fopen(src, "rb");
    if (!in) { perror("mkdisk"); return 1; }
    uint8_t buf[SECTOR];
    memset(buf, 0, sizeof buf);
    size_t got = fread(buf, 1, SECTOR, in);
    fclose(in);
    if (got < 1) { fprintf(stderr, "mkdisk: empty boot sector\n"); return 1; }
    if (buf[510] != 0x55 || buf[511] != 0xAA) {
        fprintf(stderr, "mkdisk: warning: boot sector lacks 0x55AA signature\n");
    }
    FILE *f = fopen(path, "r+b");
    if (!f) { perror("mkdisk"); return 1; }
    fwrite(buf, 1, SECTOR, f);
    fclose(f);
    printf("mkdisk: installed boot sector from %s into %s\n", src, path);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  mkdisk new  <path> <cylinders> <heads> <sectors>\n"
        "  mkdisk info <path>\n"
        "  mkdisk boot <path> <bootsector.bin>\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(); return 1; }
    if (!strcmp(argv[1], "new") && argc == 6)
        return cmd_new(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    if (!strcmp(argv[1], "info") && argc == 3)
        return cmd_info(argv[2]);
    if (!strcmp(argv[1], "boot") && argc == 4)
        return cmd_boot(argv[2], argv[3]);
    usage();
    return 1;
}
