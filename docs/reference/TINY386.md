# Reference analysis — tiny386

Source: <https://github.com/hchunhui/tiny386> (BSD-3-Clause). The Nspire archive
in `reference/` is a *binary port* of tiny386, so this analysis is based on the
project's own documentation and the metadata recovered from the `.tns` files.

## What it is

A from-scratch x86 PC emulator written in C99. The author describes the CPU core
as "built-from-scratch, simple and stupid", kept at roughly 6K lines of code.
It boots Windows 9x/NT on microcontrollers (ESP32-S3, Xtensa @ 240 MHz) and, in
the Nspire port, on an ARM Cortex-A9.

## Findings by area

| Area | tiny386 approach | Relevance to Munt386 (Z80) |
| --- | --- | --- |
| CPU architecture | i386 interpreter, extensible to 486/586/686 via a `gen` config | **Conceptually reusable**; the instruction semantics are the reference. Not reusable as code (C, 32-bit, different ABI). |
| CPU state | Flat register/segment struct, `gen`-driven feature flags | Model reused in `cpu386_t`. |
| Instruction dispatch | Large switch on opcode + ModR/M decoder | Reused as a *design pattern*; Munt386 implements it independently in `src/cpu.c`. |
| Memory model | Flat host allocation for guest RAM (`bigmalloc`), optional KVM `mmap` | **Not reusable on CSE** — 1 MiB+ flat RAM does not exist on a Z80. Munt386 uses a banked/paged backing. |
| Protected mode | Real 386 protected mode, paging, some permission checks intentionally omitted | Reference for Phase 3/4. |
| Interrupts | 8259 PIC + IDT | `src/machine.c` PIC mirrors the 8259 register model. |
| PIC | Ported from TinyEMU | Concept reused. |
| PIT | 8254 PIT | Concept reused (channel 0, 18.2 Hz). |
| DMA | 8257 ISA DMA (only where needed) | Deferred; not required for DOS text programs. |
| Keyboard | 8042 keyboard controller; input forwarded over WiFi or USB HID | Munt386 models the 8042 status/data ports and a BIOS ring buffer. |
| VGA | ISA VGA **with Bochs VBE** — deliberately avoids emulating real VGA BIOS internals | Munt386 takes a simpler path: CGA/VGA media with a linear framebuffer, expanded per mode as software requires it. |
| BIOS | Modified **SeaBIOS** (LGPL-3) + SeaBIOS VGA ROM | **Cannot be used on CSE** (needs 32-bit CPU to run BIOS POST). Munt386 implements BIOS services directly in the emulator (trap-based). |
| Disk | IDE controller + ATA/ATAPI, block backend | Munt386 exposes `disk_read_sector`/`write_sector`/`get_geometry` and an INT 13h layer. |
| Networking | NE2000 ISA NIC | Reference for Phase 15 of the spec. |
| Sound | PC speaker, Adlib OPL2, SoundBlaster 16 | Out of scope for the CSE target. |
| Optimization | `gen` config, ported peripherals, optional KVM acceleration, PSRAM/partition allocators on ESP32 | The *techniques* (specialisation, avoiding host allocation) inform Phase 16. |
| Platform abstraction | A small HAL (`get_uticks`, `bigmalloc`, framebuffer blit) | The exact pattern Munt386 copies: `vga_t` + `vga_to_cse_lcd()`. |

## Nspire-specific port parameters (recovered from `tiny386.ini.tns`)

```
[pc]    mem_size = 8M   vga_mem_size = 256K   gen = 4   fpu = 0   fill_cmos = 1
[display] width = 320  height = 240
```

Binary strings confirm: `tiny386 CX II 0.0.96-hotaccum`, `hchunhui / Ndless port`,
`SeaBIOS`, `TINY386 HARDDISK`, `cpui386_new`.

## The decisive engineering finding

tiny386 needs an **Xtensa @ 240 MHz / ARM Cortex-A9** class host with **external
RAM** to emulate a 386 PC. The TI-84+CSE has a **Z80 @ 6–15 MHz** with **32 KiB of
addressable RAM per bank and no MMU**. That is roughly three orders of magnitude
less compute and two-plus orders of magnitude less memory.

**Consequence, stated plainly:** running a full 386 + VGA + IDE emulation fast
enough for Windows 3.11 on the CSE is **not achievable at usable speed**. Munt386
therefore targets the closest technically valid alternative:

1. A **correct real-mode x86 core** (8086/80186 subset) that can boot DOS and run
   real DOS software — this is what a Z80 can plausibly drive.
2. A **host-verified 286/386 protected-mode core** developed on the development
   machine (Phases 3–5), where speed is irrelevant, so the compatibility work is
   real and testable.
3. On the CSE itself, the realistic ceiling is documented per milestone in
   `docs/ROADMAP.md`; where Windows 3.11 truly cannot run, that limitation is
   recorded rather than faked.

## What Munt386 reuses

- ABSOLUTELY NOTHING as literal source (different ISA, license hygiene, and the
  goal of working from documented behaviour).
- The **architecture**: modular cpu/memory/video/disk/BIOS split, HAL boundary,
  trap-based BIOS, configuration via a simple ini/struct, and the "implement only
  what the target software needs" rule.
