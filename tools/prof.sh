#!/bin/sh
# PC profile of the Doom image in the opendm40 emulator.
# usage: tools/prof.sh <t0> <t1> [extra run.py args]   (run from the opendm40 checkout)
set -e
T0=$1; T1=$2; shift 2
D=$(dirname "$0")/..
E=$D/build/dm40-doom.elf
OUT=${PROF_OUT:-/tmp/dm40-doom-prof}
mkdir -p "$OUT"
python3 -c "
import sys
t0,t1=float(sys.argv[1]),float(sys.argv[2])
print('0 monitor logLevel 3 sysbus.flash_controller')
t=t0
while t<t1:
    print('%.3f monitor sysbus.cpu PC' % t); t+=0.02
" "$T0" "$T1" > "$OUT/prof.txt"
rm -f emu/out/doomprof.log
DM40_EMU_LOG_MAX_MB=50 python3 emu/run.py fwrs --name doomprof --app "$E" \
    --disk-add "$D/../doom-wad-dl/mcudoom1.wad::DOOMMCU.WAD" --run-for "$T1" \
    --script "$OUT/prof.txt" "$@" > "$OUT/run.log" 2>&1
grep -oE "^0x[0-9A-Fa-f]+" "$OUT/run.log" | while read pc; do
    arm-none-eabi-addr2line -f -e "$E" "$pc" | head -1
done | sort | uniq -c | sort -rn | head -15
