# Reference analysis — 86Box

Sources are **not present in this repository**. 86Box is the hardware-accuracy
reference named by the spec; it is GPL-2, so no code is copied (see
`docs/reference/LICENSES.md`). This document records which of its behaviours
matter and where Munt386 approximates them.

## Why 86Box is the hardware reference

86Box emulates real PC hardware cycle-faithfully: 8259/8254/8042/8237, genuine
VGA register behaviour, real BIOS ROMs, and real disk controllers. Munt386 is the
opposite philosophy — *implement the register model well enough for the software
under test, and no more* — but 86Box is the authority to consult when a piece of
software breaks and the cause is ambiguous.

## Register-level models worth mirroring

| Device | 86Box behaviour to respect | Munt386 status |
| --- | --- | --- |
| 8259 PIC | ICW1/ICW2 init, IRR/ISR/IMR, non-specific EOI (0x20) | `src/machine.c` — simplified but correct for IRQ0/IRQ1 |
| 8254 PIT | Channel 0 mode 3, 1193182 Hz, reload latch | `src/machine.c` — fixed 18.2 Hz model |
| 8042 KBC | Status bit 0 (OBF), scancode set 1, make/break codes | `src/machine.c` — status/data + BIOS ring buffer |
| CMOS/RTC | BCD time/date regs, equipment + memory-size regs | `src/machine.c` — sane defaults, `fill_cmos`-style values |
| VGA | Sequencer/CRTC/graphics registers, text vs graphics | `src/vga.c` — mode-level model (no CRTC register emulation yet) |
| ATA/IDE | CHS translation, drive/head/latch registers | `src/disk.c` — CHS geometry only |
| PC speaker | PIT channel 2 + port 61h gate | `src/machine.c` — latched, not synthesised |

## Where Munt386 is knowingly less accurate (and why it is acceptable)

- **No VGA register emulation.** Real Windows 3.1 drivers poke VGA registers; a
  register-level VGA is required for Phase 7/12 and is listed in the roadmap.
- **No CRTC timing / retrace accuracy.** Port 3DAH toggles a synthetic retrace
  bit, enough for games that busy-wait but not for exact timing.
- **No DMA controller yet.** Only needed once floppy streaming or sound requires
  it; INT 13h does programmed I/O instead.
- **No hardware IRQ latency model.** The PIC delivers immediately when IF is set.

## Lesson

Use 86Box as an oracle: when DOS/Windows fails, consult the real register
semantics, then implement the *minimum* that unblocks the software, guided by the
spec's "do not emulate every IBM PC peripheral" rule.
