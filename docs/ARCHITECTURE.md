# Munt386 architecture

Munt386 is experimental replacement firmware for the TI-84 Plus C Silver Edition
whose goal is to run a genuine virtual x86 PC — BIOS → DOS → Windows 3.1 — on top
of the calculator hardware, **without relying on TI-OS**.

## Layering

```
        +------------------------------------------------------+
        |  guest software: DOS / Windows 3.1 / DOS programs     |
        +------------------------------------------------------+
        |  compatibility layer                                  |
        |    src/bios.c   INT 10h / 13h / 16h / 1Ah / 19h        |
        |    src/dos.c    INT 21h / 20h                          |
        +------------------------------------------------------+
        |  virtual PC hardware                                  |
        |    src/machine.c  PIC 8259, PIT 8254, 8042, CMOS, boot |
        |    src/vga.c      CGA/VGA modes -> RGB framebuffer     |
        |    src/disk.c     block device (CHS geometry)          |
        +------------------------------------------------------+
        |  memory subsystem                                     |
        |    src/memory.c   20-bit real-mode, 32-bit access      |
        +------------------------------------------------------+
        |  CPU core                                             |
        |    src/cpu.c      x86 interpreter (real mode today)    |
        +------------------------------------------------------+
        |  platform HAL  (the ONLY place that touches the host) |
        |    host : src/main.c        (SDL-free, PPM dump)       |
        |    CSE  : firmware/         (Z80, planned)             |
        +------------------------------------------------------+
```

**Hard rule:** nothing in `src/cpu.c`, `src/memory.c`, `src/bios.c`, `src/dos.c`
or `src/machine.c` knows a TI-84+CSE exists. The only CSE-specific code is the
future `firmware/` backend plus `vga_to_cse_lcd()`, which converts the abstract
framebuffer to a 320×240 RGB565 panel buffer.

## Modules

| File | Responsibility |
| --- | --- |
| `include/munt386.h` | The single contract shared by all modules. |
| `src/memory.c` | Guest address space: `phys20()`, `mem_read8/16/32`, `mem_write8/16/32`, wrap-around. |
| `src/cpu.c` | x86 interpreter: register file, ModR/M decode, ALU, flags, shifts, strings, interrupts, trap handling. |
| `src/machine.c` | Virtual chipset + I/O ports + reset/boot + PIC/PIT/keyboard/CMOS. |
| `src/bios.c` | BIOS interrupt services and IRQ handlers. |
| `src/dos.c` | DOS INT 21h/20h compatibility shim. |
| `src/vga.c` | Virtual video adapter + CSE LCD downscaler + bundled font. |
| `src/disk.c` | Block device with CHS geometry. |
| `src/font.c` | Our own 8×8 bitmap font (public-domain data). |
| `src/main.c` | Host front end (image loading, run loop, PPM dump). |

## Interrupt model

The BIOS owns a stub block in segment `F000` at offset `0x1000`, one 4-byte stub
per vector. `machine_reset()` fills the IVT to point at these stubs and writes a
`0xF1` marker byte in each. When the CPU reaches a stub it **traps** to
`bios_handle_int()` / `dos_handle_int()` in C, then performs an IRET-equivalent
return (computed flags, restored IF/TF).

If guest software installs its **own** handler — as real DOS does for INT 21h —
the IVT no longer points at our stubs, so genuine x86 code executes instead. The
hosted DOS layer is therefore an *optional* compatibility shim, not a
replacement for real DOS.

Hardware IRQs go through the 8259 model (`pic_raise` / `pic_deliver`) and are
delivered only when `EFLAGS.IF` is set.

## Boot path

```
reset vector F000:FFF0 = CD 19        (INT 19h)
   -> trap -> machine_boot()
      -> INT 13h-style read of sector 0 of drive 0 into 0000:7C00
      -> verify 0x55AA signature
      -> set CS:IP = 0000:7C00, DL = boot drive
   -> IRET returns to the boot sector, which runs as real x86 code
```

`SS:SP` is deliberately left untouched by the loader so the pending return frame
is intact; boot sectors set up their own stack, as on real hardware.

## CSE backend plan (firmware/)

The Z80 cannot hold the guest address space in RAM, so the backend implements the
`mem_*` interface over a **paged window**:

- A 32 KiB (or larger, see `docs/MEMORY_MAP.md`) RAM window plus paged flash.
- A page cache mapping the 16-bit physical pages the CPU is currently touching.
- Video memory kept in flash-backed or RAM-backed pages; only the visible page is
  materialised.

The same `src/*.c` logic is the reference for the Z80 port; whether it is
translated to Z80 assembly wholesale or replaced by a hand-written interpreter
with the same semantics is decided in Phase 16 (see `docs/ROADMAP.md`).

## Honest performance statement

The reference emulator (tiny386/nspire95) runs a full 386 on a 240–400 MHz ARM/
Xtensa host with megabytes of external RAM. The CSE is a 6–15 MHz Z80 with a
32 KiB window. A full 386 + VGA + IDE emulation at Windows-3.11 speed is **not
feasible on the CSE**; Munt386 therefore delivers genuine real-mode DOS execution
on hardware and develops/test the protected-mode path on the host.
