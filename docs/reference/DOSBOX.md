# Reference analysis — DOSBox-X

Sources are **not present in this repository**. The spec lists DOSBox-X as a
*behavioural* reference, so this document records the design decisions to study
and how Munt386 applies them; no DOSBox-X code is copied (it is GPL-2, which
would be incompatible with this project's permissive licensing — see
`docs/reference/LICENSES.md`).

## Why DOSBox-X is the DOS-compatibility reference

DOSBox-X is a large, mature DOS/Win9x emulator whose value here is its
**compatibility layer**, not its hardware accuracy:

- A complete INT 21h implementation, including the quirks real DOS programs and
  Microsoft C runtimes depend on.
- Local DOS (`DOSBox`'s built-in "DOS" shell) showing that a *hosted* DOS
  implementation is a legitimate architecture — exactly Munt386's `src/dos.c`.
- `MOUNT`/image support and INT 13h behaviour that many games rely on.
- Documented lists of which BIOS INT 10h/13h/16h subfunctions real software
  actually calls, which is the spec's "implement only what the software needs"
  rule in concrete form.

## Behaviour Munt386 adopts

| Behaviour | Where Munt386 implements it |
| --- | --- |
| INT 21h console services (`09h`, `02h`, `01h`, `0Bh`) | `src/dos.c` — done |
| INT 21h memory (`48h`, `49h`, `4Ah`) with a real arena | `src/dos.c` — basic bump arena |
| INT 21h version (`30h`) = DOS 6.0 shim | `src/dos.c` — done |
| INT 21h vector get/set (`25h`, `35h`) | `src/dos.c` — done |
| INT 10h teletype/scroll/cursor | `src/bios.c` — done |
| INT 13h CHS read/write + geometry | `src/bios.c` + `src/disk.c` — done |
| Missing math coprocessor is acceptable (x87 optional) | CPU core treats ESC as ModR/M-consuming no-op |
| DOS returns carry + `AX` error codes | `dos_fail()`/`dos_ok()` convention |

## Behaviours deliberately deferred

- Full file-system services (INT 21h `3Ch`–`62h`) require a FAT implementation
  and an installable DOS image; stubbed to return "not found" so callers fail
  cleanly.
- Batch/EXEC (`4Bh`), TSRs (`31h`), FCB calls, and country info.

## Lesson

DOSBox-X proves the hosted-DOS approach works at scale. Munt386 starts with the
console/memory/vector core that simple DOS programs and the first Windows
startup stage need, and grows it driven by concrete failures.
