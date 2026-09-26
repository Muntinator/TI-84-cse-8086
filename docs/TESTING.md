# Munt386 testing

## Running the suite

```sh
make test        # build and run the host test suite (200 checks)
make cse-sim     # build and run the CSE backend simulator suite (116 checks)
make emulator    # build only the host emulator
```

The host suite is a single portable C binary (`build/host/run_tests`) with no
external dependencies.  The CSE backend has its own suite (`build/host/cse_sim`)
which compiles `firmware/cse/*.c` with simulated hardware and the paged memory
backend, so the platform logic is developed and verified **without flashing
anything** — the "emulator test mode" the spec requires, extended to the CSE
backend itself.

## Layout

```
tests/
  test_main.c          runner; aggregates every suite below
  test_util.h          CHECK/CHECK_EQ harness + machine helpers
  cpu/                 instruction tests, flags, string ops
  memory/              20-bit addressing, wrap-around, 32-bit access
  protected/           protected-mode invariants (Phase 3; currently minimal)
  bios/                INT 10h/11h/12h/13h/16h/1Ah, IRQ handlers
  video/               text + mode 13h rendering, CSE downscale
  disk/                block device, geometry, read-only, bounds
  pc/                  whole-machine boot of a real boot sector
  cse/                 CSE backend suites (keymap/LCD/disk/paged memory/
                       platform wiring/whole-boot) + cse_sim_main.c harness
```

## Conventions

- Every discovered bug gets a regression test **before** the fix.
- Tests assert exact values (flags as bitmasks, registers, memory bytes), not
  "looks plausible".
- Where a feature is unimplemented, the suite says so explicitly (see
  `tests/protected/test_protected.c`) rather than reporting false passes.
- The CSE simulator's firmware boot path ends in a park with an explicit
  reason: `GUEST HALTED` is success; a subsystem name means that subsystem
  failed its self-test.  The sim never fakes a pass.

## Current status

```
make test     -> 200 checks, 0 failures
make cse-sim  -> 116 checks, 0 failures, firmware park: GUEST HALTED
```

Coverage highlights: all documented FLAGS edge cases; every implemented
instruction group; ModR/M addressing with displacements and segment overrides;
string ops with REP/REPE/REPNE and DF; full reset → INT 19h → boot sector → INT
10h → halt path; INT 13h sector reads against a synthetic image; VGA text and
mode 13h pixels; CSE LCD downscale producing a non-empty buffer.  The CSE suite
adds keypad→scancode translation (make/break, transient modifiers, extended
0xE0 pairs), LCD pixel/blit/text diagnostics, RAM disk bounds, paged guest
memory (wrap, 16/32-bit, pinned pages, no aliasing, reset clearing), platform
wiring, and a whole boot sector executed through the paged backend with output
presented through the RGB888→RGB565 panel path.

## Adding a regression test

1. Add a `CHECK`/`CHECK_EQ` block to the relevant `tests/*/test_*.c`.
2. If it is a CPU bug, prefer a minimal hand-assembled byte sequence with a
   comment decoding each byte.
3. Run `make test` (host) or `make cse-sim` (CSE backend); leave the suite green.
