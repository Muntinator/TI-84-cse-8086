# nspire95 / tiny386 — complete analysis and CSE mapping

The archive the task calls `nspire95/tiny386` is **`nspire95.zip`** at the
repository root. It was extracted and every member inspected. This document
records what is in it, what each part is, and the exact mapping to the
TI-84 Plus CSE port (Munt386-CSE).

---

## 1. What the archive contains

| Member | Size | Content |
| --- | --- | --- |
| `nspire95.tns` | 323,764 B | The emulator binary: an **ARM** executable with an Ndless `PRG` header. |
| `tiny386.ini.tns` | 197 B | Plain-text INI consumed by the emulator. |
| `bios.bin.tns` | 131,072 B | SeaBIOS x86 machine code (guest BIOS ROM image). |
| `vgabios.bin.tns` | 39,424 B | SeaBIOS VGA ROM (`55 AA 4D E9…` option-ROM header). |

No C sources, no makefiles, no documentation ship in the archive. The
`nspire95.tns` binary identifies itself via embedded strings:

```
tiny386 CX II 0.0.96-hotaccum
hchunhui / Ndless port
TINY386 HARDDISK / TINY386 CD-ROM / PIIX3 IDE
SeaBIOS
```

**Conclusion: nspire95 is a binary port of hchunhui's tiny386 to the TI-Nspire
CX II via Ndless.** There is no portable source code inside the archive to
compile — the upstream deliverable is ARM machine code plus two guest ROM
binaries. This is documented in `docs/reference/NSPIRE95.md`.

### The source that *is* usable

This repository (`src/`, `include/`, `tests/`, `tools/`) is the project's own
portable **C reimplementation of the tiny386/nspire95 design**: a real x86
interpreter, IBM-PC chipset, BIOS services, VGA, and disk device, kept
completely host-independent. It is the "existing working x86/PC emulation
code" the port preserves. Every subsystem was inspected file-by-file
(`src/cpu.c`, `src/memory.c`, `src/machine.c`, `src/bios.c`, `src/dos.c`,
`src/protected.c`, `src/vga.c`, `src/disk.c`, `src/font.c`, `src/main.c`,
`include/munt386.h`).

### Upstream configuration recovered

`tiny386.ini.tns` (197 bytes, plain text) configures the guest:

```
[pc]       bios=...  vga_bios=...  mem_size=8M  vga_mem_size=256K
           hda=...   fill_cmos=1   vga_force_8dm=0
[display]  width=320  height=240
[cpu]      gen=4  fpu=0
```

The `[display] 320x240` line is why the CSE backend's first target is the same
320×240 RGB565 panel.

---

## 2. Classification of the original implementation

The task asks: which parts are portable, TI-Nspire-specific, ARM-specific,
display-, input-, storage-, timing-, and build-specific. In the archive:

| Component in archive | Classification |
| --- | --- |
| `nspire95.tns` emulator body | **ARM-specific** (ARM machine code, Ndless ABI). Not portable as code; portable as *behaviour*. |
| Ndless runtime it links against | **TI-Nspire-specific** (file I/O, LCD, keypad services). |
| `bios.bin.tns` / `vgabios.bin.tns` | **x86 guest ROMs** (SeaBIOS, LGPL-3). Never redistributed; behaviour studied only. |
| `tiny386.ini.tns` | Portable configuration format — mirrored by `pc_config` in `include/platform.h`. |
| tiny386 C sources (upstream repo) | BSD-3-Clause C99, 32-bit host. Design is portable; literal code is not used here (different ISA target, and hygiene per `docs/reference/LICENSES.md`). |

In this repository's implementation:

| Component | File(s) | Portable? |
| --- | --- | --- |
| x86 CPU core (registers, ModR/M decoder, ALU, flags, string ops, interrupts, protected-mode transitions) | `src/cpu.c`, `src/protected.c` | **PORTABLE** — pure C99, zero host references |
| Guest memory subsystem (20-bit phys, wrap, paging translation) | `src/memory.c`, `src/protected.c` | **PORTABLE** interface; backing storage is platform-dependent (flat `malloc` today) |
| Virtual chipset: 8259 PIC, 8254 PIT, 8042 keyboard, CMOS, ports, boot path | `src/machine.c` | **PORTABLE** |
| BIOS services (INT 10h/11h/12h/13h/16h/1Ah/19h + IRQ0/IRQ1 handling) | `src/bios.c` | **PORTABLE** |
| DOS INT 21h/20h compatibility shim | `src/dos.c` | **PORTABLE** |
| Virtual VGA: text modes, CGA 4/5/6, mode 13h, abstract RGB framebuffer | `src/vga.c`, `src/font.c` | **PORTABLE** (device side); the *sink* of the framebuffer is platform work |
| Block device + CHS geometry | `src/disk.c` | **PORTABLE** (device side); the *image source* is platform work |
| Host front end (argv parsing, file loading, PPM dump) | `src/main.c` | **HOST-ONLY** — now isolated behind the platform layer |
| 8×8 font | `src/font.c` | Portable data, duplicated as the CSE glyph source |

---

## 3. Component-by-component mapping to the CSE

### CPU

| Original | CSE replacement |
| --- | --- |
| tiny386 `i386.c` interpreter (ARM binary on Nspire) | `src/cpu.c` + `src/protected.c` compiled unchanged by the Z80 C compiler |
| 32-bit register file | Same `cpu386_t` layout; Z80 backend uses `long` = 32-bit |
| Segmented fetch via `seg_read8(pc, SREG_CS, eip)` | Identical calls; linear/physical translation supplied by the CSE memory backend |
| BIOS/DOS stub trap at `F000:1000+4n` | Identical — the stub block is just guest memory contents |
| Emulator-side `cpu_run` chunked loop with `pit_advance`/`pic_deliver` | Identical |

The x86 core **never learns which machine it runs on**: it contains no I/O
ports, no LCD, no keypad, and no timing calls. This was verified by inspection:
`cpu.c`/`protected.c` reference only `munt386.h` types and `mem_*`/`seg_*`
helpers.

### Memory

| Original | CSE replacement |
| --- | --- |
| Host flat `calloc(1 MiB)` at `pc->mem` | `firmware/cse/cse_mem.c`: banked RAM window + paged flash with a page cache; same `mem_pread/write` interface |
| `mem_pread8/16/32`, `mem_pwrite8/16/32` with 1 MiB wrap | Same signatures implemented over the paged backend |
| Guest 1 MiB address space | Preserved: 8 KiB guest pages mapped through a 48 KiB RAM page cache (see `docs/CSE_MEMORY_MAP.md`) |

### Video

| Original | CSE replacement |
| --- | --- |
| tiny386: VGA + VBE → Ndless framebuffer blit | `src/vga.c` renders modes 0–7/13h into the abstract RGB framebuffer (unchanged) |
| Ndless 320×240 RGB565 panel | `firmware/cse/cse_video.c`: `cse_video_init/update/set_pixel/blit` write the framebuffer through LCD ports `0x10/0x11` using hardware window + GRAM streaming |
| `vga_to_cse_lcd()` RGB888→RGB565 downscaler | Kept; extended with a bulk row converter used by the CSE backend |

The virtual VGA is **not** rewritten for the CSE: it keeps its own resolution
and framebuffer, and the conversion happens entirely in the CSE video backend.

### Input

| Original | CSE replacement |
| --- | --- |
| Ndless keypad events → scancodes | `firmware/cse/cse_keymap.h`: complete keypad→scan-code table; `cse_keys_scan()` produces set-1 make/break pairs |
| 8042 controller + BIOS ring buffer | `src/machine.c`/`src/bios.c` unchanged: `kbd_push_scancode()` → IRQ1 → `kbd_decode_scancode()` → BDA ring |
| tiny386 ini key map | `cse_keys_get_keymap()` returns the ini-compatible mapping |

Letters, digits, arrows, Enter, Backspace, Escape, Tab, and modifier pairs are
mapped (2nd = Shift, Alpha-lock holds Shift, 5th = Ctrl, 4th = Alt), plus
F1–F5 equivalents. Limitations are documented in `docs/PORT_MAP.md`.

### Storage

| Original | CSE replacement |
| --- | --- |
| Ndless file I/O: `hda=...img.tns` read from the Nspire filesystem | `firmware/cse/cse_disk.c`: RAM-backed virtual disk first (spec: "RAM-backed disk first"), persistent flash region designed and stubbed behind `cse_storage_init` |
| ATA/IDE controller | Out of scope of the first milestone; INT 13h block device is what the BIOS uses (as in the original port's boot path) |
| BIOS image loaded from file | BIOS services remain emulator-side (`src/bios.c`); no SeaBIOS ROM is bundled (LGPL-3, and the stub-trap design makes it unnecessary) |

### Timing

| Original | CSE replacement |
| --- | --- |
| tiny386 `get_uticks()` microsecond tick | `platform_time_us()` — microseconds, `uint64_t`, wrap-safe diffing; `platform_delay_us()` |
| 8254 PIT at 1193182 Hz, IRQ0 ≈ 18.2 Hz | Virtual PIT unchanged in `src/machine.c`; driven by `machine_timer_tick()` from the platform cadence |
| Nspire ARM timers | CSE crystal timers (ports `0x30`–`0x38`, ~111 Hz mode 3) feed `platform_time_us`; timer-interrupt cadence is the `Z80_TICKS_PER_IRQ0` constant |

Physical CSE timer registers are touched **only** by `firmware/cse/cse_ports.c`
and the Z80 bootstrap — never by the virtual PIT. The layering is:
CSE crystal timer → `platform_time_us` → guest cadence → `machine_timer_tick`
→ virtual PIC → virtual x86 IRQ0.

### Interrupts

| Original | CSE replacement |
| --- | --- |
| ARM interrupt dispatch (Ndless) | Z80 `im 2` vector table in the CSE startup → platform handler → `machine_timer_tick()` |
| 8259 PIC model (`pic_raise`/`pic_deliver`, EOI, ICW2) | Unchanged in `src/machine.c` |
| IDT/gates/exceptions | Unchanged in `src/protected.c` |

Z80 interrupt state and x86 interrupt state never mix: the Z80 handler only
updates counters and latches events; the virtual PIC delivers only when the
*guest* EFLAGS.IF allows, exactly as on the host.

### Startup / platform

| Original | CSE replacement |
| --- | --- |
| Ndless program entry (`PRG` header, ARM) | `firmware/cse/startup.s`: DI/IM 1, speed + banks, stack, ZP clear, `cse_startup()` |
| Ndless exit | `platform_shutdown()` + `cse_reboot()` |
| Host `main()` (argv, files, PPM) | `src/main.c` rewritten over `include/platform.h`; identical CLI behaviour |

### Build

| Original | CSE replacement |
| --- | --- |
| Ndless toolchain (ARM GCC) | `make host\|test\|emulator\|tools` — cc; `make cse\|cse-sim` — SDCC 4.x `mcs51-large` model, `--no-xram`; `make firmware` — SPASM-ng (bring-up asm, unchanged) |
| `.tns` container | `munt386-cse.bin` raw image (hosting/packaging is a later step) |

---

## 4. What compiles on the CSE, and how

The available Z80 C toolchain in this environment is **SDCC 4.x**. The port
targets `mcs51-large` with `--no-xram`:

- `long` is 32-bit → the `uint32_t` register file is natural.
- Function pointers are banked → dispatch stays via `switch`, not tables.
- The "large" model places locals/parameters in external RAM, which SDCC maps
  onto the CSE's paged SRAM window.
- `--no-xram` keeps SDCC from assuming a separate XRAM region; the CSE maps
  SRAM into the SDCC external address space instead.
- `malloc` is not used on the CSE: all state is static (`banking.c`,
  `cse_disk.c`, framebuffers).

Host and CSE object files are never mixed: the Makefile builds
`build/host/` and `build/cse/` trees separately, and `make cse` links
**only** `firmware/cse/*.c` (the CSE backend is a C port of the platform
layer; `src/main.c` is host-only and excluded).

---

## 5. Verification status of each hardware fact

Every CSE hardware constant used by the port was checked against the
KnightOS kernel sources vendored at `reference/kernel-master/` (MIT; facts
only, no code copied):

| Fact | Value used | Verified where |
| --- | --- | --- |
| Flash size / total image space | 4 MiB (`LENGTH := 0x400000`) | `reference/kernel-master/Makefile` TI84pCSE target |
| Boot page / privileged page | `0x3FC000` / `0x3F0000` | same Makefile (`BOOT`, `PRIVILEGED`) |
| Bank B = RAM + 15 MHz flag | port `7`, bit 7 `BANKB_ISRAM_CPU15` | `include/constants.asm` (mask 7), `src/00/boot.asm` |
| RAM window | `0x8000`–`0xFFFF` = RAM page 1, page 0 below | `src/00/boot.asm` bank comments |
| Flash paging ports | `6`/`7` low, `0x0E`/`0x0F` high bits | `docs/CSE_HARDWARE.md` + boot.asm |
| LCD command/data protocol | `0x10` index twice, `0x11` data H then L | `src/00/display-color.asm` `writeLcdRegister` |
| LCD window registers | `0x50–0x53` vertical/horizontal, inclusive | `setLcdWindow` in the same file |
| Backlight GPIO bit | port `0x3A` bit 5 (`GPIO_RW_BACKLIGHT`) | `include/constants.asm` |
| Keypad scan | port `1`, active-low, drive-then-read | `src/00/keyboard.asm` |
| Crystal timers | ports `0x30`–`0x38`, mode-3 ~111 Hz tick | `docs/CSE_HARDWARE.md`, boot.asm timer setup |

Remaining hardware unknowns that **must** be measured on a real device before
firmware is flashed are explicitly marked in `docs/CSE_MEMORY_MAP.md` and in
`firmware/cse/cse_ports.c` as `VERIFY` items. Nothing claims hardware works
until the corresponding self-test passes on the device.
