# TI-84 Plus CSE memory map (verified against KnightOS sources)

Constants below were cross-checked against the vendored KnightOS kernel
(`reference/kernel-master/`, MIT) — `Makefile` TI84pCSE target,
`include/constants.asm`, `src/00/boot.asm`, `src/00/display-color.asm`,
`src/00/keyboard.asm` — facts only, no code copied. Items that cannot be
verified without a physical device are marked **VERIFY** and are not assumed
by any code path.

## 1. Physical resources

| Resource | Value | Source |
| --- | --- | --- |
| CPU | Z80-compatible, 6 MHz or 15 MHz (port `0x20` = speed INDEX: 0 = 6 MHz, 1 = 15 MHz — highest stable; see `docs/CSE_HARDWARE.md`) | `constants.asm CPUSPEED_15MHZ` (mask 1), WikiTI "83Plus:Ports:20" |
| Flash | 4 MiB (`0x400000`), paged in 16 KiB pages → **256 pages** | KnightOS `Makefile` `LENGTH := 0x400000`, `FLASH4MB` |
| SRAM | Banked through the `0x8000`–`0xFFFF` window; KnightOS maps page 1 at `0x8000` and page 0 at `0xC000` | `boot.asm` |
| Total SRAM | **≥ 32 KiB; exact size VERIFY on hardware** (self-test counts usable pages) | `docs/MEMORY_MAP.md`, bring-up prints it |
| LCD GRAM | 320×240×16-bit RGB565, external to the CPU bus (ports `0x10/0x11`) | `display-color.asm` |

## 2. Z80 64 KiB address space (normal banking)

| Window | Contents | How switched |
| --- | --- | --- |
| `0x0000–0x3FFF` | Bank A — flash page (page 0 at reset) | port `6` low bits, port `0x0E` high bit |
| `0x4000–0x7FFF` | Bank B — flash page (page 1 at reset) or **RAM page** when bit 7 set | port `7` (bit 7 = RAM + 15 MHz flag `BANKB_ISRAM_CPU15`), port `0x0F` high bit |
| `0x8000–0xBFFF` | RAM window (page 1 in KnightOS layout) | port `5` (`PORT_RAM_PAGING`) |
| `0xC000–0xFFFF` | RAM window (page 0 in KnightOS layout) | port `5` |

Execution limits: ports `0x22/0x23` (flash) and `0x25/0x26` (RAM) bound where
code may execute; `startup.s` widens the RAM window for the C stack, matching
KnightOS `unprotectRAM` semantics. **VERIFY** exact required values on
hardware.

## 3. Flash page allocation (4 MiB)

| Pages | Range | Use |
| --- | --- | --- |
| `0x00–0xEB` | `0x000000–0xFAFFFF` | **Available to Munt386-CSE** (code, guest page cache backing, virtual disk) |
| `0xEC–0xF3` | `0xFB0000–0xFCFFFF` | Reserved: future persistent guest-disk storage (never written automatically; explicit user action + unlock sequence only) |
| `0xF7, 0xF8` | `0xFD4000–0xFE1FFF` | KnightOS filesystem swap sectors — avoided |
| `0xFC` (`0x3F0000`) | privileged page | OS-critical — never touched |
| `0xFF` (`0x3FC000`) | boot page | OS-critical — never touched |

The privileged/boot page addresses are taken from the KnightOS Makefile
(`PRIVILEGED := 3F0000`, `BOOT := 3FC000`).

## 4. Memory-mapped I/O ports used by the CSE backend

| Port | Function | Source |
| --- | --- | --- |
| `0x01` | Keypad: drive row mask out, read active-low columns | `keyboard.asm` |
| `0x03` | Interrupt mask (`INT_ON` bit 0, `INT_TIMER1` bit 1, `INT_TIMER2` bit 2) | `constants.asm` |
| `0x04` | Interrupt trigger/ack; also memory-timer speed | `constants.asm` |
| `0x05` | RAM paging (window `0x8000`+) | `boot.asm` |
| `0x06`, `0x0E` | Bank A flash page low/high | `boot.asm`, platforms.inc |
| `0x07`, `0x0F` | Bank B flash/RAM page low/high | `boot.asm` |
| `0x10`, `0x11` | LCD index (written twice) / 16-bit data (H then L) | `display-color.asm writeLcdRegister` |
| `0x14` | Flash write unlock (`FLASHRWCONTROL_ENABLEWRITE` bit 0) — used only by explicit storage code | `constants.asm` |
| `0x20` | CPU speed select (speed index: `0` = 6 MHz, `1` = 15 MHz; firmware writes `1` — the highest stable speed) | `constants.asm`, WikiTI "83Plus:Ports:20" |
| `0x21` | Flash/RAM size configuration | `docs/CSE_HARDWARE.md` |
| `0x22/0x23`, `0x25/0x26` | Flash/RAM execute limits | `docs/CSE_HARDWARE.md` |
| `0x30–0x38` | Crystal timers 1–3: freq, loop, counter | `boot.asm` |
| `0x39/0x3A` | GPIO config / rw (bit 5 = backlight) | `constants.asm GPIO_RW_BACKLIGHT` |

## 5. Munt386-CSE runtime layout (inside the SRAM windows)

Planned steady-state occupancy of the banked SRAM (see
`firmware/cse/banking.c`; sizes in bytes, assuming 64+ KiB usable SRAM —
reduced gracefully when the self-test finds less):

| Region | Size | Purpose |
| --- | --- | --- |
| Z80 stack | 1 KiB | C stack (grows down from window top) |
| SDCC data/bss (ZP/data) | ~2 KiB | Globals, `pc_t`, chipset state |
| SDCC heap arena | remainder of first RAM window | `banking_alloc()` — VGA framebuffer, LCD buffer, disk cache |
| Guest page cache | 48 KiB (6 × 8 KiB pages) | `cse_mem.c` working set of guest 8 KiB pages |
| LCD RGB565 buffer | 150 KiB | `320*240*2`, streamed to GRAM |
| VGA RGB framebuffer | 300 KiB (640×480×3 worst-case; 192 KiB for 640×200 modes) | `vga_t.fb` |
| RAM virtual disk | image-size dependent (≥ 720 KiB) | `cse_disk.c` |

On a minimal 32 KiB configuration the page cache is trimmed to 2 pages and the
disk defaults to 360 KiB; the self-test reports the detected budget on the
diagnostic screen.

## 6. Guest (virtual PC) address space vs host

Unchanged from `docs/MEMORY_MAP.md` §1: the guest sees a real 1 MiB 20-bit
space; on the CSE it is *virtualised* through the page cache + paged flash,
never faked into a smaller flat buffer. Guest physical pages map as:

```
guest page (8 KiB, 128 pages cover 1 MiB)
  ├─ cached    → SRAM page-cache slot (ports 5/6/7 selected by banking.c)
  ├─ read-only → flash-backed page (BIOS stubs, option-ROM area)
  └─ dirty     → evicted to the flash storage region (0xEC–0xF3) — future
```

`cse_mem.c` implements `mem_pread*/mem_pwrite*` over this table; `src/memory.c`
and the CPU core are unchanged and unaware of it.

## 7. Verification checklist (must pass on hardware before flashing)

1. RAM self-test reports total usable SRAM ≥ the configured budget.
2. LCD self-test: write/read-back pattern via GRAM after full power-on sequence.
3. Keypad scan returns no phantom keys with no keys held; ON position matches
   the expected matrix bit (**VERIFY** exact bit).
4. Crystal timer tick measured within tolerance of the configured rate.
5. Flash checksum of mapped pages stable across reboots (read-only check).
6. ON-held at reset enters recovery mode; no flash write occurs in any path.
