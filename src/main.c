/*
 * Munt386 -- host front end.
 *
 * Development harness: it builds a virtual PC, optionally attaches a disk
 * image (supplied by the user -- no copyrighted images ship with the project),
 * runs the guest, and dumps the framebuffer as a PPM so the result can be
 * inspected without a GUI.
 *
 * All interaction with the machine it runs on goes through include/platform.h:
 * this file is the same logic the CSE startup mirrors (config, attach, reset,
 * run, present), minus the command line.
 *
 * Usage:
 *   munt386 [--floppy|--hdd] <image> [--steps N] [--ppm out.ppm] [--trace]
 */
#include "munt386.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load_file(const char *path, uint32_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size_out = (uint32_t)n;
    return buf;
}

static void write_ppm(const vga_t *v, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", v->fb_w, v->fb_h);
    fwrite(v->fb, 1, (size_t)v->fb_w * v->fb_h * 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    const char *ppm = NULL;
    int is_hdd = 1;
    pc_config cfg;

    pc_config_defaults(&cfg);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--floppy")) { is_hdd = 0; }
        else if (!strcmp(argv[i], "--hdd")) { is_hdd = 1; }
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) { cfg.max_steps = strtoull(argv[++i], NULL, 0); }
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) { ppm = argv[++i]; }
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: munt386 [--floppy|--hdd] <image> [--steps N] [--ppm out.ppm]\n");
            return 0;
        }
        else image = argv[i];
    }

    platform_init();
    platform_video_init();

    pc_t pc;
    machine_init(&pc);

    if (image) {
        uint32_t size = 0;
        uint8_t *buf = load_file(image, &size);
        if (!buf) {
            fprintf(stderr, "munt386: cannot read image '%s'\n", image);
            machine_free(&pc);
            platform_exit(1);
        }
        int slot = is_hdd ? 2 : 0;
        if (disk_attach(&pc.disk[slot], buf, size, cfg.readonly_disk, is_hdd) != 0) {
            fprintf(stderr, "munt386: not a valid disk image (size = %u)\n", size);
            free(buf);
            machine_free(&pc);
            platform_exit(1);
        }
        cfg.disk_sectors = size / DISK_SECTOR_SIZE;
        printf("munt386: attached %s image '%s' (%u bytes)\n",
               is_hdd ? "HDD" : "floppy", image, size);
    } else {
        printf("munt386: no image supplied; boot will stop (attach one with a path)\n");
    }

    machine_reset(&pc);
    uint64_t steps = cpu_run(&pc, cfg.max_steps);

    printf("munt386: executed %llu instructions\n", (unsigned long long)steps);
    if (pc.cpu.fault)
        printf("munt386: CPU fault: %s\n", pc.cpu.fault_msg);
    printf("munt386: CS:IP=%04X:%04X  running=%d  exit=%d  ticks=%llu\n",
           pc.cpu.sreg[SREG_CS], (uint16_t)pc.cpu.eip, pc.running,
           pc.exit_code, (unsigned long long)pc.pit.ticks);

    vga_render(&pc);
    platform_video_update(pc.vga.fb, pc.vga.fb_w, pc.vga.fb_h);
    if (ppm) { write_ppm(&pc.vga, ppm); printf("munt386: wrote framebuffer to %s\n", ppm); }

    int code = pc.cpu.fault ? 2 : 0;
    machine_free(&pc);
    platform_video_shutdown();
    platform_shutdown();
    return code;
}
