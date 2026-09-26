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

## Platform-port re-baseline (after the platform-layer extraction)

Recorded after the port work (platform seam, CSE backend, CSE simulator).
Same environment as above.

| Item | Value |
| --- | --- |
| Build commands | `make`, `make test`, `make emulator`, `make tools`, `make cse-sim`, `make cse`, `make firmware` |
| Host suite | **200 checks, 0 failures** (unchanged from the pre-port baseline) |
| CSE simulator suite | **116 checks, 0 failures** + firmware boot simulation to `GUEST HALTED` |
| `build/host/cse_sim` size | 462,632 bytes (host-simulated CSE backend + full portable core) |
| `make cse` without SDCC | clean documented SKIP (exit 2, message names the package to install) |

### What the CSE simulator proves

The `cse-sim` binary links `firmware/cse/*.c` with the **paged** guest-memory
backend (never the host's flat one), the emulated-hardware port layer, and the
portable core.  It runs the firmware's own startup: banner, per-subsystem
self-tests (RAM/LCD/KEYPAD/TIMER/FLASH), recovery check, then the guest boot
loop with live keypad.  Exercised end-to-end on the host:

* keypad → set-1 scancodes → virtual 8042 → BIOS ring buffer (make/break,
  transient Shift, extended 0xE0 pairs)
* paged guest memory (wrap, 16/32-bit, pinned BIOS/boot pages, no aliasing)
* RAM virtual disk (bounds, readonly, multi-sector)
* a real boot sector executing through the paged backend and printing via
  INT 10h into CGA memory, presented through the RGB888→RGB565 panel path
* the diagnostic screen text visible in the panel shadow buffer

Sim-only services (never on the device): a 100 Hz timer feed implementing the
simulated crystal-counter register, and the `cse_sim_park(reason)` hook that
lets the harness regain control at each firmware park site (`GUEST HALTED` is
the success park; any subsystem failure name is a failure park).

On-device items remain VERIFY per `docs/CSE_MEMORY_MAP.md`: measured SRAM,
LCD axis mapping, ON-key matrix bit, and the real timer-1 period.

Baseline command set recorded: `make`, `make test`, `make emulator`,
`make tools`, `make cse-sim`, `make cse`, `make firmware` (requires an
external Z80 assembler; `make cse` requires SDCC and degrades to a clear
SKIP when absent — see `docs/PORT_MAP.md`).

## CPU speed baseline (highest stable speed)

The request "overclock the CPU to the highest stable speed" was verified
against the vendored KnightOS sources and community hardware documentation
before changing any code:

* Port `0x20` takes the speed **index** (0 = 6 MHz, 1 = 15 MHz), not a
  bitmask. The firmware had been writing `0x02` — the undocumented register
  value meant for 20 MHz that TI left unimplemented before production.
* Measured on real silicon (WikiTI "83Plus:Ports:20", crystal-timer method):
  index 0 ≈ 6.09 MHz, index 1 ≈ 14.97 MHz, index 2 ≈ 14.98 MHz, index 3 ≈
  14.99 MHz — **no software-selectable value exceeds 15 MHz**. Indexes 2/3
  additionally select different default delay states (ports `0x29`–`0x2F`)
  that can break LCD timing and opcode fetch, and newer ASICs ignore them
  entirely.
* The hidden 20/25 MHz modes exist only on solder-modded ASICs (TA3 pins;
  unstable above ~22–23 MHz; flash rated 20 MHz) — a hardware mod, not a
  firmware option.

**Conclusion: 15 MHz (speed index 1) is the highest stable speed; the firmware
now selects it correctly and proves it at boot.** Changes:

1. `firmware/cse/startup.s` + `cse_ports.c`: write `1` (index) instead of
   the invalid `0x02`; comments and constants updated (`CPUSPEED_6MHZ/15MHZ/
   HIGHEST` in `cse_hardware.h`).
2. New boot self-test `test_cpu_speed()` reads port `0x20` back and halts with
   `CPU: SPEED FAIL` if the 15 MHz selection did not take; the diagnostic
   screen gains the line `CPU 15MHZ OK` before the RAM test.
3. The simulator models the port (only documented indexes latch; writing 2/3
   keeps the previous speed) and `tests/cse/test_cse.c` gained a 10-check
   regression suite (`test_cpu_speed`) covering select/read-back, the
   unimplemented-mode behaviour, the invalid-index guard, and boot init
   leaving the CPU at the highest speed.

Device build impact: `build/cse/munt386-cse.bin` 207,250 bytes (was 206,984);
bootstrap disassembly confirms `ld a,#0x01 / out (0x20),a`.

## CSE bare-metal build (SDCC) baseline

Recorded the first time `make cse` produced a linked firmware image.

### Toolchain

| Item | Value |
| --- | --- |
| Compiler | **SDCC 4.2.0 #13081** (z80 target), installed in `/usr/local/bin` |
| Assembler/linker | sdasz80 / sdld (bundled with SDCC 4.2.0) |
| Why not the distro SDCC | Ubuntu 22.04 ships SDCC 4.0.0, which fails on `src/cpu.c` with duplicate-symbol errors for variables declared inside switch-case blocks (a 4.0.0 frontend bug, fixed upstream by 4.2). SDCC 4.5.0 was also tried but its Debian binaries require glibc 2.38 (this host has 2.35); 4.2.0 runs and compiles every source cleanly |
| Compile flags | `-mz80 --no-xram --std-c99 --disable-warning 110 --nogcse` |
| `--nogcse` reason | SDCC 4.2.0 z80 optimizer hits a fatal internal error (`gen.c:5733 Unimplemented`) in the `for` loop of `dos_puts()` (`src/dos.c`) at `-O` default settings; disabling global common-subexpression elimination avoids it. Pure optimization toggle, no semantic change |
| Link | `--no-std-crt0 --code-loc 0x0200 --data-loc 0xC000` (reset vector 0x0000 = `firmware/cse/startup.s`) |

### Build results

| Item | Value |
| --- | --- |
| Build command | `make cse` |
| Output | `build/cse/munt386-cse.bin` — **206,984 bytes** |
| Image layout | `_HEADER`+`_HOME`+`_CODE` from 0x0200, code total ≈ 85.4 KB; `_DATA` 36.2 KB at 0xC000 (SRAM); `_INITIALIZED` 10 bytes; `_GSINIT` 12 bytes |
| Code sections | CODE 0x14DBA (85,434) + HOME 0x5D2 (1,490) bytes |
| Compile warnings | warning 93 (`double` → `float` in `include/munt386.h:172`), warning 117 (`--no-xram` unrecognised by 4.2.0 — harmless, no xram is used), two warning 165 (integer-overflow-in-expression notes in `platform_common.c`/`vga.c`, same expression GCC compiles with `-Wextra`) |

### Verification at this baseline

| Check | Result |
| --- | --- |
| `make cse` | links `build/cse/munt386-cse.bin`, exit 0 |
| `make cse-sim` | 116 checks, 0 failures, park `GUEST HALTED` |
| `make test` (host suite, after `machine.c` prototype fix) | 200 checks, 0 failures |

### Fixes required to reach the link (all recorded)

1. Install SDCC 4.2.0 (see toolchain table) — 4.0.0 cannot compile the core.
2. `Makefile`: add `--nogcse` to `SDCFLAGS` (dos.c optimizer workaround).
3. `Makefile`: add `firmware/cse/cse_main.c` (the platform-layer implementation:
   `platform_init/video/keys/disk/debug`, `mem_backing_alloc/free`) to the
   `cse` object list — it existed but was never linked on the device path.
4. `Makefile`: exclude `firmware/cse/main.c` from `cse-sim` (its raw
   `__asm__("di")` is Z80-only and broke the host simulator build).
5. `src/machine.c`: declare `mem_clear_all(pc_t *)` under `MUNT386_CSE*
   before use (the prototype lives in the backend header; SDCC rejects the
   implicit declaration GCC tolerated).
6. Remove the distro SDCC 4.0.0 libraries (their `/usr/share/sdcc` path was
   still visible to the linker and duplicated every runtime symbol with the
   4.2.0 libraries).

Nothing here yet executes on real CSE hardware; flashing and the on-device
VERIFY items (SRAM size, LCD axis mapping, ON-key matrix bit, timer period)
remain open per `docs/CSE_MEMORY_MAP.md` §7 and `docs/RECOVERY.md`.
