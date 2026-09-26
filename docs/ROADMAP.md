# Munt386 roadmap and status

Status is reported truthfully: a milestone is only marked done when its tests
pass. Nothing is marked complete because code exists.

## Milestone status board

| # | Milestone | Status |
| --- | --- | --- |
| 0 | Repository builds (`make`, `make test`, `make emulator`) | **DONE** |
| 1 | 386-class CPU core (register file, ModR/M, instruction groups) | **PARTIAL** — real-mode 16-bit core complete; 32-bit/protected/paging pending |
| 2 | Automated CPU test suite | **DONE** (200 checks, 0 failures) |
| 3 | Real-mode behaviour (segment:offset, 20-bit, wrap, INT/BIOS/DOS) | **DONE** |
| 4 | Protected mode (GDT/IDT/descriptors/privilege) | **NOT STARTED** |
| 5 | Paging (CR3, page tables, faults) | **NOT STARTED** |
| 6 | Virtual PC memory system | **DONE** (host flat backing; CSE paging design documented) |
| 7 | PC hardware (PIC/PIT/KBC/RTC/disk/VGA/speaker) | **PARTIAL** — PIC, PIT, KBC, CMOS, disk, CGA/VGA-modes, speaker latch done; full VGA registers, DMA pending |
| 8 | VGA subsystem + CSE LCD backend | **PARTIAL** — text modes 0–3/7, CGA 4/5/6, mode 13h, downscale done; EGA/VGA-accelerated modes pending |
| 9 | Disk (block device + images + tools) | **PARTIAL** — block device + INT 13h done; image tools in progress |
| 10 | BIOS (INT 10h/13h/16h/1Ah) | **DONE** for the implemented subfunctions |
| 11 | DOS (`C:>`, COM/EXE, console, memory) | **PARTIAL** — hosted INT 21h console/memory/vector subset; no filesystem, no EXE loader, no real DOS boot yet |
| 12 | Windows 3.x starts | **NOT STARTED** |
| 13 | Windows for Workgroups 3.11 → Program Manager | **NOT STARTED** |
| 14 | Virtual mouse | **NOT STARTED** |
| 15 | Virtual NIC (NE2000) | **NOT STARTED** |
| 16 | CSE backend + ESP32-C3 bridge | **PARTIAL** — Z80 firmware bring-up source provided; guest execution on hardware not yet possible (see limitation below) |

## Spec "success levels" mapping

| Level | Meaning | Current |
| --- | --- | --- |
| 1 | Repository builds | **REACHED** |
| 2 | 386 CPU passes meaningful tests | **REACHED for the real-mode subset**; 32-bit tests pending |
| 3 | Protected mode works | not reached |
| 4 | Virtual PC boots BIOS | **REACHED** (reset → INT 19h → boot sector) |
| 5 | DOS boots | not reached (no DOS image bundled; loader path exists) |
| 6 | DOS programs run | partial (hosted INT 21h shim; tested via unit tests) |
| 7–13 | Windows, WfW, networking, CSE execution | not reached |

## The unavoidable limitation

The reference emulator needs a 240–400 MHz 32-bit host with several MiB of RAM.
The CSE is a 6–15 MHz Z80 with a 32 KiB memory window. A faithful 386 + VGA + IDE
emulation at Windows-3.11 speed is **not achievable on the CSE**. Munt386's honest
plan is:

- **On the CSE:** genuine real-mode DOS-class execution (the achievable ceiling),
  delivered as replacement firmware with a recovery path.
- **On the host:** the full 286/386 protected-mode compatibility work (Phases
  3–5, 12–13), where speed is irrelevant, so the work is real and testable.

This limitation is recorded rather than hidden; if hardware testing later proves a
path to protected mode at acceptable speed, this document will be revised with
measurements.

## Next automatically actionable steps

1. **Phase 3 (protected mode):** implement `CR0.PE`, GDT/LDT/IDT loading, segment
   descriptor parsing, privilege checks, and PUSH-fault exceptions; add
   `tests/protected/` cases (real↔protected transitions, descriptor validation).
2. **Phase 4 (386 extensions):** `0x66`/`0x67` prefixes, 32-bit register/operand
   forms, FS/GS via `8C`/`8E`, extended ModR/M.
3. **Phase 9 tools:** `tools/mkdisk` (create/inspect images), EXE/COM loader in
   `src/dos.c`, and a filesystem layer.
4. **Phase 7/8:** VGA CRTC/sequencer registers and EGA/VGA modes needed by
   Windows drivers.
5. **Phase 16:** verify the Z80 firmware brings up LCD/RAM/keyboard and measure
   real RAM; update `docs/MEMORY_MAP.md`.

## Changelog

- **Initial bring-up:** host x86 core (real mode), memory subsystem, chipset,
  BIOS, DOS shim, VGA + CSE downscaler, disk device, host CLI, 200-test suite,
  Phase 0 documentation, Z80 firmware bring-up source.
