# Munt386 CSE firmware (`firmware/`)

This directory holds the **Z80 bare-metal backend** for the TI-84 Plus CSE:
the code that initialises the calculator hardware without TI-OS and prints the
mandated diagnostic screen.

## Honest status

- **Two firmware code paths exist.**
  1. `cse/` — the C platform backend, now **verified on the host simulator**:
     `make cse-sim` compiles it with emulated hardware plus the portable core
     and runs the real startup (self-tests → guest boot) — 116 checks, 0
     failures, park `GUEST HALTED`.  The bare-metal SDCC build is prepared
     (`make cse`; installs cleanly with `apt install sdcc` where permitted).
  2. `munt386.asm` — the Z80 bring-up assembly.  Not yet assembled here (no
     Z80 assembler bundled); `build.sh` tells you which one to provide.  Every
     hardware detail that could not be verified from documentation is marked
     `;; VERIFY:`.
- Nothing has run on real calculator hardware from this repository yet.  The
  on-device VERIFY items (measured SRAM, LCD axis mapping, ON-key matrix bit,
  real timer period) are listed in `../docs/CSE_MEMORY_MAP.md` §7.
- The **host emulator** (`../src`, `make test`) is fully working and is where
  compatibility work happens.  The simulator (`make cse-sim`) is the middle
  rung: firmware logic, no hardware.

## What `munt386.asm` does

1. `di` + `im 1`, memory-timer/speed configuration, flash high-bank clear.
2. Bank layout: `0x0000` flash p0, `0x4000` flash p1, `0x8000` RAM p1,
   `0xC000` RAM p0 (via port `0x07`, bit 7 = RAM).
3. Stack in the RAM window, CPU raised to 15 MHz — the highest stable
   software-selectable speed (port `0x20` takes the speed INDEX: `1` =
   15 MHz; register values 2/3 are unimplemented 20/25 MHz selections that
   measure ~15 MHz and must not be used) — ON-key interrupt enabled.
4. The C startup re-reads port `0x20` (`test_cpu_speed`) and halts with
   `CPU: SPEED FAIL` if the 15 MHz selection did not latch.
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

- SDCC (for `make cse`) and an external Z80 assembler (for `build.sh`) — both
  degrade to clear SKIP messages when absent.
- On-device VERIFY items: measured SRAM budget, LCD axis mapping, ON-key
  matrix bit, real crystal-timer period (`;; VERIFY:` markers in the sources,
  checklist in `../docs/CSE_MEMORY_MAP.md` §7).
- Flash-backed page eviction (milestone 2) if a device configuration cannot
  hold the full 1 MiB guest working set in SRAM; without it, a reduced cache
  halts honestly instead of losing guest memory.

These are tracked as Phase 16 in `../docs/ROADMAP.md`.
