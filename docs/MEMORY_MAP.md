# Munt386 memory maps

Two different memory systems are in play and must never be confused:

1. **The guest (virtual PC) address space** — what x86 software sees.
2. **The CSE (host) physical memory** — what the Z80 and flash actually provide.

Munt386 keeps them strictly separate. The guest is *virtualised*, not pretended.

---

## 1. Guest x86 address space (real mode)

20-bit physical addressing, `phys = (segment << 4) + offset`, masked to 20 bits.

| Physical range | Size | Use |
| --- | --- | --- |
| `0x00000`–`0x003FF` | 1 KiB | Interrupt vector table (256 × 4 bytes) |
| `0x00400`–`0x004FF` | 256 B | BIOS data area (equipment, KB size, keyboard ring, tick count) |
| `0x00500`–`0x7BFF` | ~30 KiB | Available conventional memory / DOS arena |
| `0x7C00`–`0x7DFF` | 512 B | Boot sector load address |
| `0x7E00`–`0x9FBFF` | ~600 KiB | Conventional RAM (< 640 KiB) |
| `0x9FC00`–`0x9FFFF` | 1 KiB | Extended BIOS data area (reserved) |
| `0xA0000`–`0xBFFFF` | 128 KiB | Video memory (VGA linear framebuffer base `0xA0000`) |
| `0xB0000`–`0xB7FFF` | 32 KiB | MDA/CGA mono text base |
| `0xB8000`–`0xBFFFF` | 32 KiB | CGA colour text/graphics base |
| `0xC0000`–`0xEFFFF` | 192 KiB | Option ROM area (unused today) |
| `0xF0000`–`0xFFFFF` | 64 KiB | BIOS ROM region (reset vector + INT stubs) |

On the host all of this is one flat 1 MiB allocation. The CPU never assumes the
host layout.

The BIOS stub block lives at `F000:1000 + vector*4` (see `docs/ARCHITECTURE.md`).

---

## 2. CSE physical memory (host)

| Resource | Reality | Source / note |
| --- | --- | --- |
| Flash | **4 MiB** | Confirmed by the KnightOS `TI84pCSE` linker `LENGTH = 0x400000` and its `FLASH4MB` platform flags. |
| Flash paging | Ports `6`/`7` (low page) and `0x0E`/`0x0F` (high bit) select the 16 KiB flash page mapped into `0x4000`–`0x7FFF`. | KnightOS `include/platforms.inc`. |
| RAM | Banked memory mapped into `0x8000`–`0xFFFF`; the bank register is port `5`. KnightOS assumes **at least 32 KiB**. | KnightOS `src/00/boot.asm`, `doc/memory`. |
| Exact CSE RAM size | **Not yet measured on hardware.** | The bring-up step in `docs/BOOT.md` prints the detected size; this document must be updated with the real number rather than guessing. |
| LCD | 320×240×16-bit RGB565, GRAM written 2 bytes/pixel via ports `0x10`/`0x11`. | KnightOS `src/00/display-color.asm`. |

**No conventional 640 KiB PC RAM physically exists on the CSE.** The guest's
conventional memory is backed by:

1. whatever RAM the CSE provides (the working set), plus
2. paged flash used as a backing store / swap area, plus
3. demand-loading of disk sectors.

### Planned CSE backing strategy

```
guest physical page (4 KiB)
        |
        v
   +---------+     hit      +---------------------+
   | page    |------------->| CSE RAM window      |
   | cache   |              | (banked 0x8000-...) |
   +---------+              +---------------------+
        | miss
        v
   +---------------------+   evict   +----------------+
   | flash-backed swap   |<----------| LRU page table |
   | (4 MiB flash)       |           +----------------+
   +---------------------+
```

- Page size 4 KiB keeps the translation table small.
- The page cache holds the guest pages the CPU is actively touching (code, stack,
  BIOS data, the visible video page).
- Read-only regions (BIOS ROM, video ROM) are never written back.
- Exact cache size and eviction policy are tuned once the real RAM figure is
  known and profiled.

### Video memory on the CSE

The guest framebuffer (`0xA0000`, up to 128 KiB for VGA modes) does **not** fit in
the CSE RAM window alongside code. It is therefore kept in flash-backed pages and
only the scan lines being displayed are materialised into the CSE LCD buffer by
`vga_to_cse_lcd()`.

---

## 3. Guest memory figures we can truthfully claim

- **Virtual x86 address space:** 1 MiB (20-bit), as on a real 8086.
- **Conventional memory reported to DOS:** 640 KiB (BIOS INT 12h returns 640).
- **Physical RAM actually available to the emulator on the CSE:** to be measured;
  **not** 640 KiB and **not** an IBM PC's worth. This document must not claim
  otherwise.
