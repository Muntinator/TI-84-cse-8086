# Reference licensing and reuse policy

Munt386 is developed **independently** from documented behaviour. No source code
from any referenced emulator has been copied into this repository.

## Licenses of the referenced projects

| Project | License | Copied into Munt386? |
| --- | --- | --- |
| **tiny386** | BSD-3-Clause | No source copied. Architecture and techniques studied. |
| **nspire95** | Derived from tiny386 (BSD-3-Clause) + Ndless tooling | No code copied. Binary analysed only. |
| **SeaBIOS** (shipped inside the nspire95 archive) | GNU LGPL-3 | **Not copied, not bundled.** |
| **DOSBox-X** | GNU GPL-2 | No code copied (copyleft). Behaviour studied only. |
| **86Box** | GNU GPL-2 | No code copied (copyleft). Behaviour studied only. |
| **KnightOS kernel** (`reference/kernel-master`, used for CSE hardware facts) | MIT | Not copied verbatim; hardware constants cross-checked. MIT is compatible, so small factual tables (e.g. LCD register numbers, I/O port numbers) may be reproduced with attribution if ever needed. |

## Rules applied

1. **Copyleft (GPL) projects are never used as a source of code.** Only their
   *documented behaviour* informs Munt386, which re-implements functionality from
   public specifications (Intel 8086/386 manuals, 8259/8254/8042 datasheets).
2. **Permissive (BSD/MIT) projects** could in principle be reused with their
   notice preserved. Munt386 does not currently incorporate any of their source.
3. **Firmware ROMs (SeaBIOS, VGA BIOS) are never bundled.** Munt386 implements
   BIOS services as emulator software (`src/bios.c`).
4. **No Microsoft binaries, DOS images, ROM images, or other copyrighted
   operating-system media are stored in this repository.** Users supply legally
   obtained disk images; `tools/` can create *empty* and *self-authored test*
   images only.
5. **Any third-party source actually added in the future** must be accompanied by
   its license text in `third_party/<project>/` and a `NOTICE` entry.

## Attribution

Factual hardware details (LCD register indices, CSE I/O port numbers, flash paging
semantics) were cross-checked against the KnightOS kernel (MIT, © 2014 The
KnightOS Group). Where these appear in Munt386 they are constants and register
numbers — facts, not creative expression — but credit is given here regardless.
