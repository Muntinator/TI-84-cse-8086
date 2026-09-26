# Reference analysis — nspire95

Archive: `nspire95.zip` (referred to as `nspire95(1).zip` in the task).

Contents:

| File | Size | What it is |
| --- | --- | --- |
| `nspire95.tns` | 323,764 B | The emulator itself: an ARM (`PRG` header, ARM code) Ndless program. |
| `tiny386.ini.tns` | 197 B | Plain-text INI passed to the emulator. |
| `bios.bin.tns` | 131,072 B | SeaBIOS image (x86 machine code, `0f 02` etc. visible). |
| `vgabios.bin.tns` | 39,424 B | SeaBIOS VGA ROM; starts with `55 aa 4d e9 …`. |

## Identification

Binary strings give it away:

```
tiny386 CX II 0.0.96-hotaccum
hchunhui / Ndless port
tiny386 native Ndless CX II build 0.0.96-hotaccum
TINY386 HARDDISK / TINY386 CD-ROM / PIIX3 IDE
SeaBIOS
bios.bin / vgabios.bin / linuxstart.bin
cpui386_new
../i386.c  ../pc.c
```

So nspire95 is **not an independent emulator** — it is a port of tiny386 to the
TI-Nspire CX II using Ndless (the community native-code loader for Nspire). The
`.tns` extension is just the Nspire document container; inside is an ARM
executable.

## Recovered configuration

```
[pc]       bios=bios.bin.tns  vga_bios=vgabios.bin.tns  mem_size=8M
           vga_mem_size=256K  hda=win95.img.tns  fill_cmos=1  vga_force_8dm=0
[display]  width=320  height=240
[cpu]      gen=4  fpu=0
```

## Architecture (as used on the calculator)

- **CPU core:** tiny386 `i386.c` (interpreter).
- **Platform layer:** Ndless: reads files from the calculator filesystem, owns
  the LCD framebuffer, forwards key events.
- **Firmware:** real SeaBIOS + VGA BIOS binaries shipped alongside.
- **Storage:** the guest disk image is a `.tns` file on the calculator.
- **Input:** Nspire keypad mapped to PC scan codes.
- **Display:** 320×240 Nspire CX II LCD downscaled from the VGA output.
- **Optimization:** the `hotaccum` build tag and a fixed set of guest RAM/VGA
  sizes tuned for the device.

## Transferable lessons for Munt386

1. **Binary-size discipline.** The whole emulator *plus* SeaBIOS fits in ~0.5 MB.
   Munt386 must be even leaner: the Z80 image is measured in tens of KiB.
2. **Framebuffer downscaling to a fixed 320×240 panel is the established
   pattern.** Munt386 implements the same idea in `vga_to_cse_lcd()` but keeps
   the virtual adapter resolution independent of the physical LCD.
3. **File-based firmware/disk loading.** Munt386 keeps the same separation: BIOS
   logic is in the emulator, but *disk images are user-supplied files*, never
   bundled.
4. **Keypad → scan-code mapping is a thin layer**, exactly what `src/machine.c`
   + the future CSE backend implement.

## What does NOT transfer

- The ARM code itself (wrong ISA).
- SeaBIOS/VGA ROMs (they require a 32-bit host to execute POST; also LGPL-3 and
  redistribution concerns). Munt386 provides BIOS services in software.
- 8 MiB guest RAM (impossible on Z80; Munt386 virtualises and documents the real
  budget in `docs/MEMORY_MAP.md`).

## Ethical / legal note

`nspire95.tns` contains no Microsoft binaries. The `bios.bin`/`vgabios.bin` are
SeaBIOS builds (LGPL-3). Munt386 does **not** copy or redistribute any of these
files; only documented behaviour is used. See `docs/reference/LICENSES.md`.
