# Munt386 baseline (host)

Recorded before any porting work, exactly as the repository stood.
Environment: Ubuntu 22.04, GCC 11.4.0 (`cc`), GNU Make 4.3, C99, no external
libraries.

## Build

| Item | Value |
| --- | --- |
| Build command | `make` (all) / `make emulator` / `make test` |
| Compiler flags | `-std=c99 -O2 -Wall -Wextra -Iinclude -g` |
| Compiler | cc → GCC 11.4.0 |
| `build/munt386` size (with debug info) | 347,144 bytes |
| `build/munt386` size (stripped) | 59,536 bytes |
| `build/munt386` text / data / bss | 52,030 / 722 / 808 bytes |
| `build/run_tests` size | 533,936 bytes (text 83,419) |
| `build/mkdisk` size | 26,472 bytes |

## Test results

```
$ make test
[bios]     31 checks
[video]    10 checks
[disk]     13 checks
[pc]       22 checks
==================
200 checks, 0 failures
```

(The runner prints each suite; all nine suites — memory, cpu, flags, strings,
protected, bios, video, disk, pc — report 0 failures.)

## CPU performance

Workload: a self-authored 6-byte boot sector (`B8 00 00` = `mov ax,0`,
`EB FD` = `jmp -1`) in a 272-sector image, 20,000,000 instructions requested.

```
$ time ./build/munt386 --floppy hdd.img --steps 20000000
munt386: executed 20000000 instructions
real    0m0.422s
```

**Baseline throughput ≈ 47.4 million x86 instructions/second** on the host
(single core, -O2). This is the number the CSE port is measured against and
the number profiling must try to approach on the Z80 (realistically several
orders of magnitude slower — see `docs/PORT_MAP.md`).

Notes:
* `--hdd` attaches to `disk[2]` while the INT 19h bootstrap reads `disk[0]`;
  boot-from-HDD selection is pre-existing behaviour and is unchanged by the
  port. Use `--floppy` for the benchmark.
* No VGA rendering occurs during the run loop (`vga_render` is called once at
  exit), so the figure measures pure CPU-core speed.

## Memory usage (host)

| Consumer | Size |
| --- | --- |
| Guest address space | 1,048,576 B (`X86_MEM_SIZE`, one flat allocation) |
| VGA framebuffer | 640×480×3 = 921,600 B (max mode) |
| `pc_t` + chipset state | ~1.5 KiB static |
| Peak RSS (host process) | ≈ 2 MiB |

## Test-bench reproducibility

```
printf '\xb8\x00\x00\x40\xeb\xfd' > boot.bin   # mov ax,40h; jmp $
truncate -s 512 boot.bin
printf '\x55\xaa' | dd of=boot.bin bs=1 seek=510 conv=notrunc
./build/mkdisk new hdd.img 4 4 17
./build/mkdisk boot hdd.img boot.bin
time ./build/munt386 --floppy hdd.img --steps 20000000
```

Baseline command set recorded: `make`, `make test`, `make emulator`,
`make tools`, `sh firmware/build.sh` (requires an external Z80 assembler;
see `docs/PORT_MAP.md` for the new `make cse` targets added by the port).
