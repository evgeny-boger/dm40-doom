# Doom for the Alientek DM40

A separate firmware for the Alientek DM40 multimeter (GD32F427, 200 MHz,
256 KiB RAM, ILI9488 480x320) that runs shareware Doom. It does not replace
or change [opendm40](https://github.com/wirenboard/opendm40): it is a
different image installed the same way, and the way back is the vendor
updater.

![E1M1 in the emulator](docs/img/emu-demo.png)

Based on [next-hack's Doom port for the EFR32MG24](https://github.com/next-hack/MG24_Doom_BLE)
(itself based on [doomhack's GBADoom](https://github.com/doomhack/GBADoom)).
That port already keeps the WAD in an external SPI flash that is not
memory-mapped and fits in 256 KiB of RAM; this tree adds a DM40 hardware
layer and the changes below.

Status: runs in the [opendm40 emulator](https://github.com/wirenboard/opendm40/tree/main/emu)
(title, menus, E1M1 and E1M2 played with the touch screen, installed through
the vendor ROM and FS-loader). Not run on a device yet.

Only E1M1 and E1M2 are in the image: the level data the port keeps in
internal flash is baked into the firmware file (below), and the image has room
for about 340 KiB of it.

Known limits:

- two levels, E1M1 and E1M2 ([port/levels.c](port/levels.c)); leaving E1M2
  ends the episode (finale text); no demos (they play E1M5, E1M3, E1M7);
- no sound, no music;
- no saved games (Load / Save show a message);
- only the Hynitron touch controller (the one on the tested unit) is handled,
  not the Goodix GT1158 opendm40 also supports;
- SPI1 runs at 12.5 MHz without opendm40's boot-time clock check; garbled
  textures on a device would point at the SPI clock (`0x031F` = /16 in
  `sf_init`);
- the frame rate on a device is unknown: the emulator's timing is not
  cycle-exact, and the MG24 port reads its flash at 2 x 40 MHz with DMA.
  Wall patches and flats that do not fit a level's region are read from the
  SPI flash while drawing.

## What is different from the MG24 port

| | MG24 port | DM40 |
|---|---|---|
| WAD | raw in two interleaved SPI flash chips, DMA | `DOOMMCU.WAD` file on the meter's FAT16 volume (W25Q128, 4096-byte sectors), read through its cluster runs, polled SPI1 at 12.5 MHz |
| flash cache | ~1 MiB of internal flash, rewritten at each level start | baked into the image: 60 KiB WAD-wide data + one fixed region per level; the firmware never writes it |
| flats that do not fit | always fit | LRU of 10 flats in TCM, loaded from the SPI flash on a miss |
| display | SPI panel, DMA line buffers | 16-bit FSMC, 320x200 shown 1.5x (480x300), synchronous blit |
| RAM | 256 KiB contiguous | SRAM 192 KiB (frame buffers, zone; the port's 16-bit pointers need it) + TCM 64 KiB (stack, flat cache, render buffers) |
| input | buttons / shift registers | touch screen zones + side keys (Hynitron controller, I2C2) |
| network, sound | BLE multiplayer, OPL2 music, SFX | none |

The image is built with `-Os` (the renderer `-O2`) and reduced math tables to
leave room for the cache.

## Flash use

| Address | Sector | Content |
|---|---|---|
| 0x08000000 | 0 | ROM bootloader (vendor), never touched |
| 0x08004000 .. | 1 .. | this image: code (~225 KiB), then the baked cache (to 0x0809C3xx) |
| 0x08008000 | 2 | 12-byte loader marker; zeroed only to return to the updater |
| 0x080A0000 .. | 9 .. 11 | vendor FS-loader, UID data, calibration, never touched |

The MG24 port converts the level data (vertexes, lines, sides, segs, nodes,
blockmap, sectors ...) on every level start and keeps it in internal flash,
which the renderer reads like RAM; on the DM40 there is no RAM for it (E1M2:
~170 KiB). Here that data is produced once, in the emulator, and shipped in
the image:

- `_fcache_start` (after the code): WAD-wide region, 60 KiB;
- then one fixed region per level of [port/levels.c](port/levels.c)
  (E1M1 120 KiB, E1M2 the rest). The level's geometry is mandatory; the rest
  of its region holds flats and wall patches.

[tools/bake.py](tools/bake.py) runs the image in the emulator with a bake key
in RAM (hal.c `fcache_bake_key`); the firmware sets up each level, which
programs the regions onto erased flash, and the emulator saves the flash. On a
meter the key is absent: the same level setup code compares each word with
the flash instead of writing it (w_wad.c `programFlashWord`), and stops with
"baked data differs" on a difference. Level restarts and other skills give the
same words (the skill-dependent thing list stays in RAM; a buffer whose
length is not a multiple of 4 is padded with zeros, not with the zone bytes
after it). In the emulator this was run with SRAM and TCM filled with a
pattern before start, through E1M1 -> intermission -> E1M2 -> finale. The image must be
baked again after every build: the cache holds absolute addresses, and its
signature includes the build time.

The only runtime flash write left is `marker_zero` (three words at
0x08008000), on the way back to the updater.

## Controls

| Where | Action |
|---|---|
| left third of the screen | stick: up = forward, down = back, left / right = turn |
| centre column | top: menu; middle: next weapon; bottom: automap |
| right third | top: use (doors, switches); bottom: fire / menu select |
| side keys | MENU = menu, RUN = fire, AUTO = use, MF = next weapon |
| power key, held 1.5 s | switch off |

## Building

```sh
make                      # build/dm40-doom.elf, build/dm40-doom.bin (arm-none-eabi-gcc)
cc -std=gnu99 -O2 -w -Wno-error=incompatible-pointer-types -o wadutil/mcudoomwadutil wadutil/*.c
(cd wadutil && ./mcudoomwadutil /path/to/doom1.wad DOOMMCU.WAD)
tools/bake.py ../opendm40 wadutil/DOOMMCU.WAD   # build/dm40-doom-baked.bin, build/dm40doom.bin
```

`mcudoomwadutil` must run in the directory that holds `mcudoom_0_4.wad`.
`tools/bake.py` needs an opendm40 checkout (its emulator and
`stock/dm40fw.py`) and takes about a minute; it also packs the update file.
The shareware `doom1.wad` (v1.9, md5 `f0cefca49926d00903cf57551d901abe`) is
in Debian's `doom-wad-shareware` package.

## Running in the emulator

From an opendm40 checkout (its `emu/`):

```sh
emu/run.py fwrs --name doom --app ../dm40-doom/build/dm40doom.bin \
    --disk-add DOOMMCU.WAD::DOOMMCU.WAD --run-for 30 --shot doom.png
```

The baked image is needed: the plain `build/dm40-doom.elf` stops with "No
flash cache for this build and WAD".

`--disk-add` copies the WAD into a private copy of the emulated disk. Taps and
keys: `--key menu@8 --tap 400,250@9.5`, or `monitor sysbus.i2c3.touch Press
X Y` / `Release` lines in a `--script` file. [tools/prof.sh](tools/prof.sh)
samples the PC; [tools/dbglog.py](tools/dbglog.py) decodes the `dbg_log` RAM
ring (the port's `printf` output) from a run that read it with
`sysbus ReadBytes`.

## Installing on a meter

Not done on a device yet. In the emulator, steps 1-2 were run through the
vendor ROM and FS-loader (`emu/run.py chain --zero-marker --app dump` with
both files on the disk): the loader installed the 609 KiB image, the flash
matched `build/dm40-doom-baked.bin`, and Doom started.

1. From opendm40: `DBG:UPDATE` (or from the stock firmware: its update
   function). The meter restarts into the vendor loader's update screen with
   its own USB disk.
2. Copy `DOOMMCU.WAD` and `build/dm40doom.bin` (from `tools/bake.py`) to
   that disk, press update. The name must match `dm40*.bin`, and it must be
   the only such file in the root.
3. To go back: hold the power key 3 s in the window after start-up (the
   start screen says so), or let the watchdog fire. Doom zeroes the loader
   marker and resets into the same update screen; copy an opendm40 or stock
   `dm40*.bin` there.

## License

GPL v2 or later (Doom source release, PrBoom, GBADoom, the MG24 port); see
[LICENSE](LICENSE). Marco Paland's printf: MIT. Font8x8: public domain.
