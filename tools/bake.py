#!/usr/bin/env python3
"""Bake the Doom flash cache into the image.

Runs build/dm40-doom.elf in the opendm40 emulator with the bake key set
(hal.c fcache_bake_key): the firmware sets up every level of port/levels.c
(levels.c fcache_bake_levels), which writes the WAD-wide and the per-level
cache regions onto the erased flash after the code. The emulator saves the
internal flash; the image up to the last written byte becomes
build/dm40-doom-baked.bin, and the packed update file build/dm40doom.bin.

The code part of the result must equal build/dm40-doom.bin (the cache holds
absolute addresses, so code and cache only fit together); the script checks
that, and that the run ended with "Bake done".

usage: tools/bake.py OPENDM40_DIR DOOMMCU.WAD [--run-for SECONDS] [--build DIR]
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_BASE, LOADER_BASE, MARKER = 0x08004000, 0x080A0000, 0x08008000

ap = argparse.ArgumentParser()
ap.add_argument('opendm40')
ap.add_argument('wad')
ap.add_argument('--run-for', type=float, default=60)
ap.add_argument('--build', default=os.path.join(HERE, 'build'), help='build directory (default: build)')
a = ap.parse_args()
BUILD = os.path.abspath(a.build)
ELF = os.path.join(BUILD, 'dm40-doom.elf')
BIN = os.path.join(BUILD, 'dm40-doom.bin')

syms = {}
for line in subprocess.check_output(['arm-none-eabi-nm', ELF], text=True).splitlines():
    v, _, name = line.split(None, 2) if len(line.split()) == 3 else (None, None, None)
    if name:
        syms[name] = int(v, 16)
key, log, log_pos, start = (syms[n] for n in ('fcache_bake_key', 'dbg_log', 'dbg_log_pos', '_fcache_start'))

out = os.path.join(BUILD, 'bake')
os.makedirs(out, exist_ok=True)
flash_out = os.path.join(out, 'flash.bin')
script = os.path.join(out, 'bake.txt')
t_end = a.run_for - 0.1
with open(script, 'w') as f:
    f.write('0 monitor logLevel 3 sysbus.flash_controller\n')
    f.write('0 monitor sysbus WriteDoubleWord 0x%08x 0x454B4142\n' % key)
    f.write('0 monitor sysbus WriteDoubleWord 0x%08x 0x4D4F4F44\n' % (key + 4))
    f.write('%.1f monitor sysbus ReadDoubleWord 0x%08x\n' % (t_end, log_pos))
    f.write('%.1f monitor sysbus ReadBytes 0x%08x 4096\n' % (t_end, log))
if os.path.exists(flash_out):
    os.remove(flash_out)
run = subprocess.run([sys.executable, os.path.join(a.opendm40, 'emu', 'run.py'), 'fwrs', '--name', 'doombake-' + os.path.basename(BUILD),
                      '--app', ELF, '--disk-add', '%s::DOOMMCU.WAD' % os.path.abspath(a.wad),
                      '--run-for', str(a.run_for), '--script', script, '--save-flash', flash_out],
                     cwd=a.opendm40, capture_output=True, text=True)
with open(os.path.join(out, 'run.log'), 'w') as f:
    f.write(run.stdout + run.stderr)

# dbg_log ring (as tools/dbglog.py)
s = run.stdout
m = re.search(r'^(0x[0-9A-Fa-f]{8})\s*$', s, re.M)
if not m:
    sys.exit('bake: no dbg_log in the emulator output (%s)' % os.path.join(out, 'run.log'))
pos = int(m.group(1), 16)
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2}),', s[m.end():]))[:4096]
data = data[pos % 4096:] + data[:pos % 4096] if pos > 4096 else data[:pos]
text = data.decode('latin-1')
with open(os.path.join(out, 'dbg.log'), 'w') as f:
    f.write(text)
for line in text.splitlines():
    if line.startswith('DM40 '):
        print(line)
if 'Bake done' not in text:
    sys.exit('bake: no "Bake done" (see %s)' % os.path.join(out, 'dbg.log'))

flash = open(flash_out, 'rb').read()
code = open(BIN, 'rb').read()
body = bytearray(flash[APP_BASE - 0x08000000:LOADER_BASE - 0x08000000])
end = len(body.rstrip(b'\xff'))
end = (end + 3) & ~3
body = body[:max(end, len(code))]
m0 = MARKER - APP_BASE
body[m0:m0 + 12] = code[m0:m0 + 12]   # the emulator's marker, not part of the image
if body[:len(code)] != code:
    sys.exit('bake: code part differs from %s' % BIN)
if any(b != 0xFF for b in body[len(code):start - APP_BASE]):
    sys.exit('bake: data between the code and _fcache_start')
baked = os.path.join(BUILD, 'dm40-doom-baked.bin')
open(baked, 'wb').write(body)
print('%s: %d bytes (code %d, cache %d, image ends 0x%08x)' % (os.path.relpath(baked), len(body), len(code),
      len(body) - (start - APP_BASE), APP_BASE + len(body)))
packed = os.path.join(BUILD, 'dm40doom.bin')
subprocess.check_call([sys.executable, os.path.join(a.opendm40, 'stock', 'dm40fw.py'), 'pack', baked, packed, '--pad'])
print(os.path.relpath(packed))
