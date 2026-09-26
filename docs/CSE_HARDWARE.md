# TI-84 Plus C Silver Edition hardware notes

These are the hardware facts Munt386's firmware layer depends on. Register and
port numbers were cross-checked against the KnightOS kernel (MIT) `TI84pCSE`
platform build, which is the most complete open Z80 OS for this device.

## Processor and clock

- Z80-compatible CPU at up to 15 MHz (6 MHz base), selected via port `0x20`.
- **Port `0x20` takes the speed INDEX, not a bitmask**: `0` = 6 MHz,
  `1` = 15 MHz (KnightOS `constants.asm`: `CPUSPEED_6MHZ`/`CPUSPEED_15MHZ` are
  masks 0/1, i.e. the index itself).
- **15 MHz (index 1) is the highest stable software-selectable speed.** Port
  values `2` and `3` were intended for 20/25 MHz but left unimplemented before
  production: crystal-timer measurements show ~15.0 MHz for them (vs ~14.97 MHz
  for index 1, ~6.09 MHz for index 0), with different default delay states
  (ports `0x29`–`0x2F`) that can break LCD timing and opcode fetch; newer
  ASICs show no difference between `01`–`03` at all. Reaching a real 20/25 MHz
  requires soldering to ASIC pins (TA3 only) and is unstable above ~22–23 MHz —
  the flash chip is only rated 20 MHz. Firmware therefore uses index 1 and
  confirms it by reading the port back at boot (`test_cpu_speed`,
  `cse_cpu_speed_get/set` in `firmware/cse/cse_ports.c`).
  (Source: WikiTI "83Plus:Ports:20", crystal-timer measurements by the
  community; consistent with the KnightOS comment "there are also 2 and 3,
  but they should not be used".)
- Port `0x21` selects the flash/RAM size configuration
  (`FLASHRAMSIZE_FLASHCHIP`).

## Memory and paging

- **Flash: 4 MiB**, paged by 16 KiB. Low bank page register port `6`, high bit
  port `0x0E` (`PORT_MEMA_HIGH`); second bank port `7`, high bit port `0x0F`.
- **RAM: banked** into `0x8000`–`0xFFFF`, bank register port `5`
  (`PORT_RAM_PAGING`). KnightOS maps RAM page 1 at `0x8000` and page 0 at
  `0xC000` and only relies on 32 KiB, but the device provides more. **The exact
  amount must be measured during bring-up** — this document will be updated with
  the measured value.
- Memory/execution limits are enforced by ports `0x22`/`0x23` (flash execute
  limits) and `0x25`/`0x26` (RAM execute limits); code can only execute inside
  these windows, which matters for running routines from RAM.
- Flash write protection: port `0x14` (`FLASHRWCONTROL_ENABLEWRITE`), plus
  exclusion port `0x16` on some models. **Never erase flash automatically.**

## LCD (320×240 colour)

- Data written through port `0x10` (command/index, written twice per access) and
  `0x11` (data).
- 16-bit-per-pixel RGB565 GRAM. Key registers:
  `LCDREG_DISPCONTROL1=7`, `LCDREG_POWERCONTROL1=0x10`, `LCDREG_ENTRYMODE=3`,
  `LCDREG_CURSOR_ROW=0x20`, `LCDREG_CURSOR_COLUMN=0x21`, `LCDREG_GRAM=0x22`,
  `LCDREG_WINDOW_HORIZ_START=0x50` … `WINDOW_VERT_END=0x53`,
  `LCDREG_FRAMERATE=0x2B`.
- Full power-on sequence (power control, gamma, counters, frame rate) is in
  KnightOS `src/00/display-color.asm`; Munt386's firmware will follow the same
  documented sequence.
- Backlight is GPIO bit 5 via ports `0x39` (config) / `0x3A` (read-write).

## Keyboard

- Keypad port `1`. The driver drives rows and reads columns: write `0xFF` then a
  row mask, read the column byte; active-low. KnightOS `src/00/keyboard.asm`
  contains the canonical scan loop.
- An ON-key interrupt is available on port `3`/`4` (`INT_ON`).

## Timers and interrupts

- Interrupt mask port `3` (`INT_ON`, `INT_TIMER1`, `INT_TIMER2`, `INT_LINK`).
- Interrupt trigger/acknowledge port `4`; also selects the memory timer speed
  (`MEM_TIMER_SPEED`).
- Crystal timers 1–3 via ports `0x30`–`0x38` (frequency, loop, counter), usable
  for a ~100 Hz system tick.
- TI timer 1 is a little over 100 Hz on real hardware — useful as the guest PIT
  and BIOS tick source.

## Flash access

- Read through the paged windows; write requires unlocking via port `0x14` and
  executing the flash program/erase routines from RAM (subject to the RAM execute
  limits), as KnightOS does.

## Recovery-relevant facts

- The OS upgrade mechanism (`.8cu` files) can restore the original firmware; see
  `docs/RECOVERY.md`.
- The boot page and privileged page locations (`0xFC000` / `0x3F0000` for the
  CSE per KnightOS) define where the OS entry points live. Munt386 must not
  overwrite them until it is independently tested.
