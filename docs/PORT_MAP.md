# Munt386-CSE port map

Legend: **PORT DIRECTLY** (compile unchanged) · **ADAPT** (small, semantics-
preserving edits) · **REWRITE** (reimplement at the same interface) ·
**NOT REQUIRED** (dropped, with reason).

The guiding rule from `docs/NSPIRE95_ANALYSIS.md` holds here: the x86 core and
virtual chipset are ported, never redesigned. All REWRITE entries are
*platform-layer* items — things the original project delegated to Ndless/the
host OS — not emulator internals.

## CPU

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Register file `cpu386_t` | `include/munt386.h` | PORT DIRECTLY | Plain C struct; `uint32_t` = SDCC `long` |
| Opcode interpreter (all real-mode groups, `0F` extensions) | `src/cpu.c` | PORT DIRECTLY | No host dependencies |
| ModR/M decoder | `src/cpu.c` `decode_rm()` | PORT DIRECTLY | |
| FLAGS engine (exact CF/PF/AF/ZF/SF/OF) | `src/cpu.c` | PORT DIRECTLY | Integer-only already |
| Shifts/rotates, string ops (REP/REPE/REPNE) | `src/cpu.c` | PORT DIRECTLY | |
| Protected mode: descriptors, segment loading, gates | `src/protected.c` | PORT DIRECTLY | |
| Paging (`paging_translate`) | `src/protected.c` | PORT DIRECTLY | |
| Fault messages (`snprintf`) | `src/cpu.c` | ADAPT | SDCC: replace with fixed-string copy (`plat_str_copy` in `cse_util.c`) when `MUNT386_CSE`; unchanged on host |
| Instruction throughput | — | — | Z80 reality: hundreds to thousands of guest insns/s. Optimisation order tracked in `docs/OVERNIGHT_REPORT.md` |

## MEMORY

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| `mem_pread*/mem_pwrite*` interface | `include/munt386.h` | PORT DIRECTLY | Interface only — backing is swappable |
| Linear/paged accessors `mem_lread*/mem_lwrite*` | `src/memory.c` | PORT DIRECTLY | Calls `paging_translate` exactly as before |
| `phys20()` + wrap-around | `src/memory.c` | PORT DIRECTLY | |
| Host backing: flat `calloc(1 MiB)` | `src/main.c` | PORT DIRECTLY (host) | stays the host implementation |
| CSE backing: banked RAM + paged flash page cache | new `firmware/cse/cse_mem.c`, `firmware/cse/banking.c` | REWRITE | Same interface, paged implementation. REWRITE is forced by the hardware: no 1 MiB flat RAM exists. Design: `docs/CSE_MEMORY_MAP.md` |
| `machine_init` 1 MiB allocation | `src/machine.c` | ADAPT | Split into `mem_backing_alloc/free` in `src/memflat.c` so the CSE can substitute its backend without touching the portable file |
| Guest RAM size claims | BIOS INT 12h = 640 KiB | PORT DIRECTLY | Documented honest distinction in `docs/MEMORY_MAP.md` |

## VIDEO

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Virtual VGA device (modes 0–7, 0Dh, 10h/12h stub, 13h) | `src/vga.c` | PORT DIRECTLY | Device is not "simplified for the CSE"; it keeps its own resolution/framebuffer |
| Abstract RGB framebuffer (`vga_t.fb`) | `src/vga.c` | PORT DIRECTLY | On CSE it lives in banked RAM (heap), 300 KiB |
| 8×8 font | `src/font.c` / `firmware/cse/cse_font.c` | PORT DIRECTLY (host) / REWRITE (CSE) | Same glyph data regenerated as SDCC-friendly arrays |
| `vga_render()` | `src/vga.c` | PORT DIRECTLY | |
| RGB888→RGB565 row converter | new in `src/vga.c` (`vga_rgb_row_to_rgb565`) + `firmware/cse/cse_video.c` | ADAPT | Bulk row conversion — never per-pixel calls — for the CSE blit |
| `vga_to_cse_lcd()` downscaler | `src/vga.c` | PORT DIRECTLY | Kept; CSE backend uses the row converter path |
| CSE LCD init/window/GRAM streaming | new `firmware/cse/cse_video.c` (`cse_video_init/update/set_pixel/blit`) | REWRITE | Platform work (Ndless did this on the Nspire). Protocol verified vs KnightOS `display-color.asm` |
| Panel axis/rotation | — | VERIFY | On-device check required; marked `VERIFY` in `cse_video.c` |

## INPUT

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| 8042-style controller (ports 60/64) + queue | `src/machine.c` | PORT DIRECTLY | |
| Scancode→ASCII decode, shift/ctrl/alt flags | `src/machine.c` | PORT DIRECTLY | |
| BIOS INT 16h + ring buffer | `src/bios.c` | PORT DIRECTLY | |
| Keypad matrix scan (port 1, active-low) | new `firmware/cse/cse_ports.c` | REWRITE | Platform work; scan pattern per KnightOS `keyboard.asm` |
| Keypad→PC set-1 scancode map | new `firmware/cse/cse_keymap.h` | REWRITE | Full table incl. make/break, Shift/Ctrl/Alt modifiers, F1–F5, arrow pairs |
| Keymap→ini config bridge | `cse_keys_get_keymap()` | REWRITE | Mirrors tiny386 ini semantics |
| Mouse | — | NOT REQUIRED | Original Nspire port has none; Windows 95 phase is out of scope here |

Unavoidable limitations (documented, not hidden):
* No keyboard LEDs feedback (guest writes are accepted and ignored).
* No numeric keypad, PrintScreen, Pause, or scroll-lock hardware keys.
* `Alt` arrives as a modifier pair; `Alt`-only key combos beyond Alt+letter
  depend on the guest tolerating the missing `E0` prefix block.
* Extended (`E0`) arrow make codes are supplied as a two-byte sequence;
  release codes use the same convention (set 1, non-`E0` release byte) which
  is what the emulated BIOS decode path consumes.

## TIMERS

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Virtual PIT model (1193182 Hz base, ticks counter) | `src/machine.c` | PORT DIRECTLY | |
| `pit_advance()` host pacing | `src/machine.c` | PORT DIRECTLY | `PIT_CALLS_PER_TICK` static; per-run counter now lives in `pc_t` (portable fix for reentrancy) |
| Host wall-clock timing | `src/main.c` (was) → `src/platform_host.c` | ADAPT | `clock_gettime(CLOCK_MONOTONIC)` behind `platform_time_us()` |
| CSE crystal timer 1/3 (~111 Hz mode 3) | new `firmware/cse/cse_ports.c` | REWRITE | Feeds `platform_time_us()` via 64-bit microsecond accumulator |
| Timer interrupt cadence | `Z80_TICKS_PER_IRQ0` in `include/platform.h` | ADAPT | Platform handler calls `machine_timer_tick()`; the virtual PIT stays hardware-agnostic |
| `platform_delay_us` | new | REWRITE | Busy-wait on `platform_time_us` (correct, if power-hungry) |

Architecture invariant (from the spec): **no CSE hardware register is ever
read by the virtual PIT.** Chain: crystal timer → `platform_time_us()` →
guest cadence → `machine_timer_tick()` → virtual PIC → guest IRQ0.

## INTERRUPTS

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Virtual 8259 (IRR/ISR/IMR, EOI, ICW2) | `src/machine.c` | PORT DIRECTLY | |
| IRQ delivery gated on guest EFLAGS.IF | `src/machine.c` | PORT DIRECTLY | |
| Real-mode IVT / protected-mode IDT delivery | `src/protected.c` | PORT DIRECTLY | |
| Exceptions with error codes, #PF | `src/protected.c` | PORT DIRECTLY | |
| Z80 `im 2` dispatch | new `firmware/cse/startup.s` + `cse_ports.c` | REWRITE | Physical interrupts only latch events/counter updates; they never mutate guest CPU state directly |

## STORAGE

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Block device `disk_read/write_sector` | `src/disk.c` | PORT DIRECTLY | Interface unchanged |
| CHS geometry derivation | `src/disk.c` | PORT DIRECTLY | |
| Host image loading (`fopen`/`malloc`) | `src/main.c` | PORT DIRECTLY (host) | host-only |
| CSE RAM-backed virtual disk | new `firmware/cse/cse_disk.c` | REWRITE | First milestone per spec. 1440 KiB floppy or 720 KiB min image in banked RAM behind the page cache; volatile by design |
| CSE persistent flash storage | `cse_storage_init` (stub, safe) | REWRITE | Region reserved at **flash page 0xEC–0xF3** (see `docs/CSE_MEMORY_MAP.md`) — far from boot page 0xFF, privileged page 0xFC, and KnightOS swap sectors 0xF7/0xF8. Never written automatically; requires explicit user action + RAM-execute unlock sequence. Stub returns "unavailable" until verified on hardware |
| Flash write protect rules | `docs/RECOVERY.md` | PORT DIRECTLY | No automatic erase/write, ever |
| Disk image file format | `tools/mkdisk` | PORT DIRECTLY | Same images work on host and CSE |

## BIOS

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Stub-block BIOS (IVT → `F000:1000` traps) | `src/cpu.c`, `src/machine.c` | PORT DIRECTLY | This *replaces* SeaBIOS on both host and CSE — same as the original architecture choice |
| INT 10h/11h/12h/13h/16h/1Ah/19h | `src/bios.c` | PORT DIRECTLY | |
| IRQ0 tick / IRQ1 keyboard handlers | `src/bios.c` | PORT DIRECTLY | |
| DOS INT 21h/20h shim | `src/dos.c` | PORT DIRECTLY | |
| SeaBIOS / VGA ROM binaries | `nspire95.zip` | NOT REQUIRED | LGPL-3 + needs a 32-bit guest CPU to POST; the trap-BIOS provides the services software actually calls. Never redistributed (`docs/reference/LICENSES.md`) |
| BIOS date string / CMOS defaults | `src/machine.c` | PORT DIRECTLY | |

## SOUND

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| PC speaker latch (port 61h bits) | `src/machine.c` | PORT DIRECTLY | Guest-visible behaviour preserved |
| Speaker tone synthesis | — | ADAPT | CSE has no PC-speaker-equivalent buzzer exposed by documented ports; `MUNT386_CSE` builds compile a **silent backend** that records the latched state so guest behaviour (timing loops that watch port 61h) stays correct without audio. Sound must never block boot |
| Adlib/SB16 (tiny386 features) | — | NOT REQUIRED | Not present in this implementation; out of scope |

## NETWORKING

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| NE2000 / any NIC | — | NOT REQUIRED | Not implemented in the source being ported; the Nspire binary's `TINY386 HARDDISK/CD-ROM/PIIX3` strings confirm no NIC was active in the port either. Tracked as a far-future phase |

## STARTUP

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| `main()` argv/files/PPM | `src/main.c` | ADAPT | Rewritten over `include/platform.h`; CLI behaviour identical |
| Host platform backend | new `src/platform_host.c` | REWRITE | libc + POSIX, host-only |
| CSE Z80 reset bootstrap (DI, IM 1→2, banks, speed, stack) | new `firmware/cse/startup.s` | REWRITE | Mirrors `firmware/munt386.asm` bring-up semantics |
| CSE hardware init (LCD, keypad, timers, backlight) | new `firmware/cse/cse_ports.c`, `cse_video.c` | REWRITE | Platform work |
| Diagnostic screen (MUNT386 / NSPIRE95-TINY386 PORT / INITIALIZING… + RAM/LCD/KEYPAD/TIMER/FLASH lines) | new `firmware/cse/startup.c` | REWRITE | Each line printed **only after** its self-test passes; failures halt with the subsystem name |
| ON-held recovery entry | `firmware/munt386.asm` semantics | PORT DIRECTLY (design) | Re-implemented in C as `cse_recovery_check()` |
| `cse_reboot()` | new | REWRITE | Returns to bootstrap without touching flash |

## BUILD

| Component | Source | Action | Notes |
| --- | --- | --- | --- |
| Host build (`make`, `make test`, `make emulator`, `make tools`) | `Makefile` | PORT DIRECTLY | Unchanged targets, still green |
| Platform-layer object split | `Makefile` | ADAPT | `platform_host.c` joins host builds; `main.c` excluded from `lib` |
| CSE build (`make cse`) | `Makefile` + `firmware/cse/*.c` | REWRITE | SDCC `mcs51-large --no-xram`, output `build/cse/munt386-cse.bin`; separate object tree, never mixed with host objects |
| CSE simulator (`make cse-sim`) | `Makefile`, `tests/cse/cse_sim_main.c` | ADAPT | Links `firmware/cse/*.c` (emulated ports) + `CSE_CORE` with the **paged** memory backend and runs `cse_startup` end-to-end on the host; verified: 116 checks + firmware boot simulation reaching `GUEST HALTED` |
| Z80 bring-up asm (`sh firmware/build.sh`) | `firmware/munt386.asm` | PORT DIRECTLY | Untouched; requires SPASM-ng/brass/sass |
| Z80 assembler availability | — | NOT REQUIRED (for CI) | `make cse` degrades to a clear SKIP message when SDCC is absent; documented in `docs/OVERNIGHT_REPORT.md` |
| SDCC | — | ADAPT | `tools/` dir has an apt hint; the port verifies presence and fails loudly rather than silently |

## Summary counts

* PORT DIRECTLY: 30 items — the entire x86 core, memory interface, chipset,
  BIOS, DOS shim, VGA device, disk device, tests (host suite still 200/0 after
  the port; CSE backend adds its own 116-check simulator suite).
* ADAPT: 11 items — fault strings, backing-store split, row converter,
  pacing counter, main(), Makefile, simulator, SDCC integration.
* REWRITE: 13 items — all confined to the platform layer (CSE memory
  backing, LCD, keypad, timers, storage, startup) mirroring what Ndless
  provided on the Nspire.
* NOT REQUIRED: 5 items — mouse, NIC, SeaBIOS ROMs, Adlib/SB16, Z80
  assembler as a hard dependency.

Every REWRITE sits below the `include/platform.h` seam; nothing above the
seam changed semantics.
