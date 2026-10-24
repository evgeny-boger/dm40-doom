#!/usr/bin/env python3
"""Decode the dbg_log ring (hal.c) from a run.py output that contains the
monitor lines 'sysbus ReadDoubleWord <dbg_log_pos>' and
'sysbus ReadBytes <dbg_log> 4096'."""
import re, sys
s = open(sys.argv[1], errors='replace').read()
m = re.search(r'^(0x[0-9A-Fa-f]{8})\s*$', s, re.M)
pos = int(m.group(1), 16)
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2}),', s[m.end():]))[:4096]
if pos > len(data):
    i = pos % len(data)
    data = data[i:] + data[:i]
else:
    data = data[:pos]
sys.stdout.write(data.decode('latin-1'))
