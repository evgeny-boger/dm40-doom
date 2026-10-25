# Doom for the Alientek DM40

Firmware for the Alientek DM40 multimeter (GD32F427, 200 MHz, 256 KiB RAM,
ILI9488 480x320) that runs shareware Doom. It is installed with the vendor
updater, and the way back is the same updater. While Doom is installed the
meter is not a meter: install opendm40 or the stock firmware again to
measure.

> **Use at your own risk.** This is an unofficial hobby firmware, provided
> AS IS without any warranty (see [LICENSE](LICENSE)); installing it may
> void the meter's warranty. It erases and rewrites internal flash sectors
> 6-8 at every level start ([Flash use](#flash-use)); the wear this causes
> over time is not known. It was run on one meter only
> ([Tested on](#tested-on)).

![E1M1 in the emulator](docs/img/emu-demo.png)

Based on [next-hack's Doom port for the EFR32MG24](https://github.com/next-hack/MG24_Doom_BLE),
commit [fc02a55](https://github.com/next-hack/MG24_Doom_BLE/tree/fc02a55340d1d5b1a5155b12ac049436ef5d4f7a)
(itself based on [doomhack's GBADoom](https://github.com/doomhack/GBADoom)).
`doom/` is its `DoomMG24BLE/Doom/`, `port/` started from its
`DoomMG24BLE/src/`, `wadutil/` is its `MCUDoomWadUtil/`; files changed here
say so at the top, and the changes are in the git history.
That port already keeps the WAD in an external SPI flash that is not
memory-mapped and fits in 256 KiB of RAM; this tree adds a DM40 hardware
layer and the changes below.

Status: runs on a DM40A, installed through the vendor FS-loader: title,
menus and E1M1 played with the touch screen, about 25 FPS on E1M1.

### Tested on

One DM40A with the Hynitron touch controller, installed from opendm40
through the vendor loader. Other models (DM40B / C), the Goodix touch
controller and the vendor loader versions of other units are not tested.

![Menus and E1M1 in the emulator](docs/img/emu-play-sheet.png)

Known limits:

- shareware episode 1 only (the shareware WAD);
- no sound, no music;
- no saved games (Load / Save show a message);
- only the Hynitron touch controller (the one on the tested unit) is handled,
  not the Goodix GT1158 opendm40 also supports;
- about 25 FPS on E1M1. Wall patches and flats that do not fit a level's
  region are read from the SPI flash while drawing (polled, 25 MHz after a
  boot-time check as in opendm40, else 12.5 MHz; the MG24 port reads its
  flash at 2 x 40 MHz with DMA), and the frame is sent to the panel
  synchronously. The top line shows the frame time split (game tic, 3D
  view, SPI reads inside it, panel transfer) and the SPI divider. Only the low 512 KiB of the GD32F4 flash are
  zero-wait (to 0x08080000): level data past the first 128 KiB of a level's
  cache (sector 8) is read with wait states.

## What is different from the MG24 port

| | MG24 port | DM40 |
|---|---|---|
| WAD | raw in two interleaved SPI flash chips, DMA | `DOOMMCU.WAD` file on the meter's FAT16 volume (W25Q128, 4096-byte sectors), read through its cluster runs, polled SPI1 at 25 MHz (12.5 MHz if the boot-time check fails) |
| flash cache | ~1 MiB of internal flash, rewritten at each level start | sector 6 (128 KiB) WAD-wide data, sectors 7 + 8 (256 KiB) the current level, rewritten at each level start |
| flats that do not fit | always fit | LRU of 10 flats in TCM, loaded from the SPI flash on a miss |
| display | SPI panel, DMA line buffers | 16-bit FSMC, 320x200 shown 1.5x (480x300), synchronous blit; as in the MG24 port, the status bar rows are sent only when it changed |
| RAM | 256 KiB contiguous | SRAM 192 KiB (frame buffers, zone; the port's 16-bit pointers need it) + TCM 64 KiB (stack, flat cache, render buffers) |
| input | buttons / shift registers | touch screen zones + side keys (Hynitron controller, I2C2) |
| network, sound | BLE multiplayer, OPL2 music, SFX | none |

The image is built with `-Os` (the renderer `-O2`) and reduced math tables to
leave room for the cache.

## Flash use

| Address | Sector | Content |
|---|---|---|
| 0x08000000 | 0 | ROM bootloader (vendor), never touched |
| 0x08004000 .. 0x08040000 | 1 .. 5 | this image (~225 KiB; link.ld stops the build if it reaches sector 6) |
| 0x08008000 | 2 | 12-byte loader marker; zeroed only to return to the updater |
| 0x08040000 | 6 | WAD-wide cache, a signature of the WAD in its last 16 bytes |
| 0x08060000 | 7, 8 | the current level's cache |
| 0x080A0000 .. | 9 .. 11 | vendor FS-loader, UID data, calibration, never touched |

As in the MG24 port, the level data (vertexes, lines, sides, segs, nodes,
blockmap, sectors ...) is converted from the WAD at every level start and
kept in internal flash, which the renderer reads like RAM; there is no RAM
for it (E1M6: ~220 KiB). Sectors 7 and 8 are erased and rewritten at every
level start, also a restart after death; the level's geometry comes first,
flats and wall patches fill the rest of the 256 KiB, and what does not fit
is read from the SPI flash while drawing. Sector 6 is rewritten only when
its signature does not match the WAD (first start, another WAD). The
vendor updater erases sectors 1 to 8 when it installs an image, so the
cache is rebuilt after every install. Each level start is one erase cycle
of sectors 7 and 8; the rated cycle count of this flash is not checked here.

Level geometry per map, KiB: E1M1 81, E1M2 167, E1M3 163, E1M4 130, E1M5
131, E1M6 220, E1M7 155, E1M8 75, E1M9 108. A level whose geometry does not
fit the region stops with a message.

## Controls

![Touch zones and side keys](docs/img/controls.png)

The stick is centred at (90, 220) with a dead zone of 22 px; the zones are in
[port/input.c](port/input.c), the picture is drawn by
[tools/controls-image.py](tools/controls-image.py). In menus the stick moves
the cursor, FIRE selects, MENU goes back.

## Building

Linux (or WSL on Windows; macOS lacks `md5sum` / `dpkg-deb`, use a Linux
container): `arm-none-eabi-gcc` 14.2, `make`, a host `cc`, `python3`,
`curl` and `dpkg-deb` (only to unpack Debian's WAD package; with your own
`doom1.wad` as the argument it is not needed).

```sh
make                      # build/dm40doom.bin (update file), build/dm40-doom.elf (arm-none-eabi-gcc 14.2)
tools/make-wad.sh         # build/DOOMMCU.WAD
```

[tools/make-wad.sh](tools/make-wad.sh) builds the converter
(`wadutil/`, next-hack's MCUDoomWadUtil) and converts the shareware
`doom1.wad` v1.9 (md5 `f0cefca49926d00903cf57551d901abe`), downloaded from
Debian's `doom-wad-shareware` package or given as an argument; the result
always has the same md5, which the script checks (the converter here
allocates zeroed memory, so padding bytes no longer differ between runs).
[tools/dm40fw.py](tools/dm40fw.py) (from opendm40) packs the update file.

CI ([.github/workflows/build.yml](.github/workflows/build.yml)) builds the
firmware and the update file and checks the WAD conversion on every push;
a `v*` tag also publishes a GitHub release with `dm40doom.bin`,
`dm40-doom.elf` and `SHA256SUMS`.

## Running in the emulator

The emulator (Renode) is part of opendm40, which is not public yet. From an
opendm40 checkout (its `emu/`):

```sh
emu/run.py fwrs --name doom --app ../dm40-doom/build/dm40doom.bin \
    --disk-add ../dm40-doom/build/DOOMMCU.WAD::DOOMMCU.WAD --run-for 30 --shot doom.png
```

`--disk-add` copies the WAD into a private copy of the emulated disk. Taps and
keys: `--key menu@8 --tap 400,250@9.5`, or `monitor sysbus.i2c3.touch Press
X Y` / `Release` lines in a `--script` file. [tools/prof.sh](tools/prof.sh)
samples the PC; [tools/dbglog.py](tools/dbglog.py) decodes the `dbg_log` RAM
ring (the port's `printf` output) from a run that read it with
`sysbus ReadBytes`.

## Installing on a meter

From a [release](https://github.com/evgeny-boger/dm40-doom/releases):
`dm40doom.bin` (the update file) and `dm40-doom.elf` (for `addr2line`, see
below). `DOOMMCU.WAD` is not attached: make it with `tools/make-wad.sh`
([Building](#building)).

![Installing](docs/img/install.png)

Charge the battery first. The vendor loader rewrites only the application
area (sectors 1-8); its own code, the unit's boot data and the calibration
(sectors 9-11) are not touched, and Doom writes only sectors 6-8.

1. Get the meter's USB disk:
   - from opendm40: `DBG:UPDATE`; the meter restarts into the vendor
     loader's update screen with its own USB disk;
   - from the stock firmware: Settings > Other Settings > USB Connect
     (not tried with this image).
2. Copy `DOOMMCU.WAD` and `dm40doom.bin` to that disk. The name must match
   `dm40*.bin`, and it must be the only such file in the root.
3. On the update screen press "update"; on the stock firmware eject and
   power the meter off and on.
4. Doom starts. The first start after an install builds the WAD-wide cache
   (sector 6) before the title screen; then MENU, New Game.

### Going back

![Going back to the updater](docs/img/go-back.png)

Switch the meter off (POWER 1.5 s) and on, then hold POWER for 3 s while the
start screen shows "Hold POWER 3s now". Doom zeroes the loader marker and
resets into the update screen; copy an opendm40 or stock `dm40*.bin` there
and press Update. After a watchdog reset the fault screen below comes
first, and POWER goes to the same update screen.

The stock update file comes from Alientek (V2.6: `dm40_v_2_6.bin`; where
Alientek publishes it is not described here). Keep a copy of the update
file you want to go back to before installing Doom.

If Doom does not get as far as its start screen (a black screen with the
backlight on), neither of the above works. The vendor bootloader then
still opens the update screen when the debug header's TX pad (PA2) is
held to GND at power-on (the case has to be opened).

## Faults and hangs

A fault draws a red bar at the bottom of the panel and stops; a hang stops
feeding the watchdog. Either way the watchdog resets the meter about 20 s
later, and instead of going straight to the updater Doom shows what the
last session left in RAM (TCM `.noinit`, kept over the reset): the PC / LR
the last SysTick interrupted, the fault's number, PC, LR, CFSR, HFSR,
MMFAR, BFAR, and the last lines of the `dbg_log` ring (level set-up prints
each step). POWER then goes to the updater.

![Fault screen on a DM40A (first device run: UNALIGNED in fastOpeningsCopy)](docs/img/fault-screen.jpg)

`arm-none-eabi-addr2line -f -i -e dm40-doom.elf <PC>` names the function,
with the ELF of the installed image (attached to each release; keep your
own for local builds). The photo above was taken from an earlier build,
which cut the register lines at 60 characters.

The emulator does not raise the Cortex-M4 UNALIGNED fault for LDRD / STRD /
LDM / STM on an address that is not word-aligned; the meter does (the MG24
`fastOpeningsCopy` faulted that way on the first frame, now a `memcpy`).

## Problems

Open an [issue](https://github.com/evgeny-boger/dm40-doom/issues) with the
meter model, the release or commit, and a photo of the fault screen if there
was one.

## Credits

- id Software: Doom and its source release;
- the PrBoom, BOOM, LxDoom and LSDLDoom authors;
- [doomhack](https://github.com/doomhack): GBADoom and GbaWadUtil; Kippykip:
  GBADoom additions;
- [next-hack](https://github.com/next-hack) (Nicola Wrachien): the
  EFR32MG24 port and MCUDoomWadUtil, which this firmware is built on;
- Marco Paland: printf; Daniel Hepper: font8x8;
- opendm40 (Wiren Board): the DM40 hardware details and `tools/dm40fw.py`.

## License

GPL v2 or later (Doom source release, PrBoom, GBADoom, the MG24 port,
MCUDoomWadUtil with its own [wadutil/LICENSE](wadutil/LICENSE)); see
[LICENSE](LICENSE). Files written for this port carry an SPDX line and
their copyright. Other licenses inside:

- [port/printf.c](port/printf.c), [port/printf.h](port/printf.h) (Marco
  Paland): MIT;
- [port/font8x8.c](port/font8x8.c): public domain;
- [tools/dm40fw.py](tools/dm40fw.py) (from opendm40): MIT, the text is in
  the file.

Game data is not part of this repository and not under the GPL:
`doom1.wad` is id Software's shareware Doom, which may be shared only as
the unmodified shareware package, so `DOOMMCU.WAD` (converted from it) is
made locally by `tools/make-wad.sh` and is not attached to releases.
[wadutil/mcudoom_0_4.wad](wadutil/mcudoom_0_4.wad) comes with next-hack's
MCUDoomWadUtil: its CREDITS lump names doomhack, Kippykip, next-hack and
"Doom II status bar by Torus Games"; which of its lumps are the Torus Games
graphics (the small `STGANUM` digits, perhaps) is not known. The GBA status
bar images in `st_gfx.c` were removed; the DM40 build draws `STBAR` from the
WAD.
