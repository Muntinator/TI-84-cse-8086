# Munt386 testing

## Running the suite

```sh
make test        # build and run all automated tests
make emulator    # build only the host emulator
```

The suite is a single portable C binary (`build/run_tests`) with no external
dependencies, so the CPU core can be developed **without flashing the
calculator** — the "emulator test mode" the spec requires.

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
```

## Conventions

- Every discovered bug gets a regression test **before** the fix.
- Tests assert exact values (flags as bitmasks, registers, memory bytes), not
  "looks plausible".
- Where a feature is unimplemented, the suite says so explicitly (see
  `tests/protected/test_protected.c`) rather than reporting false passes.

## Current status

```
200 checks, 0 failures
```

Coverage highlights: all documented FLAGS edge cases; every implemented
instruction group; ModR/M addressing with displacements and segment overrides;
string ops with REP/REPE/REPNE and DF; full reset → INT 19h → boot sector → INT
10h → halt path; INT 13h sector reads against a synthetic image; VGA text and
mode 13h pixels; CSE LCD downscale producing a non-empty buffer.

## Adding a regression test

1. Add a `CHECK`/`CHECK_EQ` block to the relevant `tests/*/test_*.c`.
2. If it is a CPU bug, prefer a minimal hand-assembled byte sequence with a
   comment decoding each byte.
3. Run `make test`; leave the suite green.
