# Munt386

Experimental replacement firmware for the **TI-84 Plus C Silver Edition** that
aims to run a genuine virtual x86 PC — BIOS → DOS → Windows 3.x — on the
calculator, without TI-OS.

This is **not** a Windows-themed calculator program and **not** a desktop
imitation. The goal is real x86 execution. Nothing is claimed to work until
binaries actually execute.

## Building

```sh
make            # build the host emulator + test runner
make test       # run the host test suite
make cse-sim    # build + run the CSE backend simulator (no hardware needed)
make cse        # build the bare-metal CSE image (needs SDCC)
make emulator   # just build ./build/munt386
```

Requirements: a C99 compiler and `make`. No external libraries.

## Running the host emulator

```sh
./build/munt386 --hdd mydisk.img --steps 20000000 --ppm screen.ppm
```

Attach a disk image you legally obtained (Munt386 ships **no** operating-system
binaries). The emulator boots sector 0 at `0000:7C00` like a real PC and dumps the
framebuffer as a PPM when asked.

## Layout

```
include/        public contract (munt386.h)
src/            CPU core, memory, chipset, BIOS, DOS, VGA, disk, host CLI
tests/          automated tests (cpu, memory, protected, bios, video, disk, pc)
tools/          disk-image utilities (create/inspect)
firmware/       Z80 bare-metal CSE backend (bring-up)
docs/           architecture, memory map, x86, hardware, boot, roadmap, recovery
docs/reference/ analysis of tiny386 / nspire95 / DOSBox-X / 86Box + licensing
reference/      analysed archives (not built)
```

## What is implemented today

- A real-mode x86 interpreter with a genuine ModR/M decoder, exact FLAGS
  behaviour, full string/rep support, and interrupt handling.
- A virtual PC: 20-bit memory with wrap-around, 8259 PIC, 8254 PIT, 8042
  keyboard, CMOS, CGA/VGA video modes, block-device disk, PC boot path.
- BIOS (INT 10h/11h/12h/13h/16h/19h/1Ah) and a hosted DOS INT 21h shim.
- A 320×240 RGB565 downscaler for the CSE LCD.
- A complete CSE platform backend (`firmware/cse/`): paged guest memory, RAM
  virtual disk, keypad→PC-scancode translation, LCD backend, bare-metal startup
  with self-tests — verified end-to-end on the host simulator (`make cse-sim`).
- `200 checks, 0 failures` (host suite) and `116 checks, 0 failures` + firmware
  boot simulation (CSE simulator) — see `docs/TESTING.md`.

## What is **not** implemented yet

Protected mode, paging, 32-bit operands, VGA register-level emulation, a DOS
filesystem/EXE loader, networking, and running the guest on real CSE hardware.
See `docs/ROADMAP.md` for the honest status and the documented limitation that a
6–15 MHz Z80 cannot emulate a 386 fast enough for Windows 3.11.

## Safety

Replacing firmware is risky. Munt386 never erases or writes flash automatically
and provides a recovery/diagnostic boot path. Read `docs/RECOVERY.md` **before**
flashing anything.

## Legal

No Microsoft or other copyrighted operating-system binaries are included. Software
images are supplied by the user. See `docs/reference/LICENSES.md`.
