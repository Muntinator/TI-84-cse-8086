# Munt386 boot process

## Host emulator boot (implemented)

1. `machine_init()` allocates the guest address space, defines CMOS defaults,
   initialises video, and calls `machine_reset()`.
2. `machine_reset()` clears guest memory, installs the IVT (all 256 vectors point
   at BIOS stubs in `F000:1000+`), fills the BIOS data area, writes the date
   string, and writes `CD 19` (INT 19h) at the reset vector `F000:FFF0`.
3. The CPU starts at `F000:FFF0`. It executes `INT 19h`, whose vector points at a
   stub; the CPU traps into `bios_handle_int(0x19)`.
4. `machine_boot()` reads sector 0 of drive 0 through the block device, copies it
   to `0000:7C00`, verifies the `0x55AA` signature, and sets `CS:IP = 0000:7C00`
   and `DL` to the boot drive. The pending interrupt frame is patched so the
   IRET-equivalent return lands at the boot sector.
5. The boot sector executes as **real x86 machine code**. It typically issues
   `INT 13h` to load more sectors, `INT 10h` for output, then jumps to the OS
   loader.

Verified end-to-end by `tests/pc/test_pc.c`, which boots a self-authored boot
sector and confirms it prints `MUNT386 OK` through the BIOS into video memory.

## Diagnostic screen (CSE firmware)

The bare-metal firmware must prove each hardware block before anything else.
Intended first-light screen (matches the spec):

```
MUNT386
TI-84 PLUS CSE

Z80 ........ OK
LCD ........ OK
RAM ........ OK
FLASH ...... OK
X86 CORE ... OK      <- only once the core is ported

BOOT: recovery = hold ON
```

Each line is printed only after the corresponding self-test genuinely passes:
LCD by writing and reading back a pattern; RAM by writing/reading a march pattern
across each bank and **reporting the measured size**; FLASH by reading the ID /
verifying a page checksum (read-only — never erase during boot).

## CSE bring-up order (planned, `firmware/`)

1. `di` / stack init in RAM.
2. Memory mapping (KnightOS-style): bank 0 = flash page 0, bank 1 = flash page *,
   bank 2 = RAM page 1, bank 3 = RAM page 0 — with the CSE high-bit ports
   (`0x0E`/`0x0F`) handled.
3. CPU speed 15 MHz (port `0x20`).
4. Interrupt mask and timer configuration.
5. LCD power-on sequence and backlight.
6. Keyboard scan loop.
7. Flash read check.
8. Diagnostic screen.
9. Recovery check: if ON is held at boot, enter recovery/diagnostic mode instead
   of the normal payload.

## Boot-time recovery path (mandatory)

- Holding **ON** during power-up must enter recovery mode (see
  `docs/RECOVERY.md`), never the experimental payload.
- On any self-test failure the firmware prints the failing subsystem and halts
  rather than proceeding to run the guest.
- The firmware never erases or writes flash during boot.
