# Munt386 CSE firmware (`firmware/`)

This directory holds the **Z80 bare-metal backend** for the TI-84 Plus CSE:
the code that initialises the calculator hardware without TI-OS and prints the
mandated diagnostic screen.

## Honest status

- **Written but not yet assembled or run on hardware from this repository.**
  No Z80 assembler is bundled here, so `build.sh` will tell you which one to
  provide. Every hardware detail that could not be verified from documentation is
  marked `;; VERIFY:` in `munt386.asm`.
- The **host emulator** (`../src`, `make test`) is fully working and is where
  compatibility work happens. The firmware is the hardware bring-up path.

## What `munt386.asm` does

1. `di` + `im 1`, memory-timer/speed configuration, flash high-bank clear.
2. Bank layout: `0x0000` flash p0, `0x4000` flash p1, `0x8000` RAM p1,
   `0xC000` RAM p0 (via port `0x07`, bit 7 = RAM).
3. Stack in the RAM window, CPU raised to 15 MHz, ON-key interrupt enabled.
4. Colour LCD power-on sequence + backlight (documented CSE sequence).
5. RAM self-test that measures usable 512-byte blocks.
6. Flash self-test — **read only** (checksum of the mapped page).
7. Keypad scan; if ON is held, jump to **recovery mode**.
8. Print the diagnostic screen:
   `MUNT386 / Z80 OK / LCD OK / RAM OK / FLASH OK / X86 CORE NOT STARTED`.

## Building

```sh
sh ./firmware/build.sh
```

Requires one of `sass`, `brass`, `spasm-ng`, or `spasm` on `PATH`.

## Installing (read this first)

Replacing firmware is destructive and the current image is a bring-up build.
Follow `../docs/RECOVERY.md`: never overwrite the original OS until the
replacement has been independently tested, and always keep a working way back
(the CSE can be restored with legitimate TI tooling and a legally obtained OS
file).

## Missing before the guest can run on hardware

- A verified LCD axis mapping (`;; VERIFY:` items).
- The page cache that maps the guest 1 MiB address space onto the CSE RAM
  window + paged flash (see `../docs/MEMORY_MAP.md`).
- A Z80 or translated implementation of the x86 core, plus the platform glue
  that connects `vga_to_cse_lcd()` to the panel.

These are tracked as Phase 16 in `../docs/ROADMAP.md`.
