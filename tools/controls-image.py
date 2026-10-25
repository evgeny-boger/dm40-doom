#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Evgeny Boger
"""Draw docs/img/controls.png: the touch zones of port/input.c over a game screenshot.

usage: tools/controls-image.py docs/img/emu-demo.png docs/img/controls.png
"""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageEnhance
src, out = sys.argv[1], sys.argv[2]
S = 2                                  # drawing scale
KW = 180                               # side-key column width (unscaled)
W, H = KW + 480 + 20, 320 + 40
img = Image.new('RGB', (W * S, H * S), (24, 26, 30))
shot = Image.open(src).convert('RGB').resize((480 * S, 320 * S), Image.NEAREST)
shot = ImageEnhance.Brightness(shot).enhance(0.45)
ox, oy = KW * S, 20 * S
img.paste(shot, (ox, oy))
d = ImageDraw.Draw(img, 'RGBA')
B = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15 * S)
R = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12 * S)
def box(x0, y0, x1, y1, col, title, sub=''):
    d.rectangle([ox + x0 * S, oy + y0 * S, ox + x1 * S - 1, oy + y1 * S - 1], fill=col + (70,), outline=col + (255,), width=2 * S)
    cx, cy = ox + (x0 + x1) * S // 2, oy + (y0 + y1) * S // 2
    d.text((cx, cy - (8 * S if sub else 0)), title, font=B, fill='white', anchor='mm')
    if sub:
        d.text((cx, cy + 10 * S), sub, font=R, fill=(230, 230, 230), anchor='mm')
# centre column, right column (input.c)
box(180, 0, 300, 90, (90, 140, 255), 'MENU')
box(180, 90, 300, 230, (180, 110, 255), 'NEXT', 'weapon')
box(180, 230, 300, 320, (90, 200, 200), 'MAP')
box(300, 0, 480, 150, (90, 210, 110), 'USE', 'doors, switches')
box(300, 150, 480, 320, (240, 80, 70), 'FIRE', 'menu: select')
# stick: x < 180, centre (90, 220), dead zone 22
box(0, 0, 180, 320, (240, 190, 60), '')
d.text((ox + 90 * S, oy + 40 * S), 'STICK', font=B, fill='white', anchor='mm')
d.text((ox + 90 * S, oy + 60 * S), 'slide from the centre', font=R, fill=(230, 230, 230), anchor='mm')
cx, cy, r = ox + 90 * S, oy + 220 * S, 22 * S
d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 220, 90, 255), width=2 * S, fill=(255, 220, 90, 60))
L = 62 * S
for dx, dy, t in ((0, -1, 'forward'), (0, 1, 'back'), (-1, 0, 'turn'), (1, 0, 'turn')):
    x1, y1 = cx + dx * L, cy + dy * L
    d.line([cx + dx * r, cy + dy * r, x1, y1], fill=(255, 220, 90, 255), width=3 * S)
    a = 8 * S
    if dx == 0:
        d.polygon([(x1, y1 + dy * a), (x1 - a // 1.4, y1 - dy * 0), (x1 + a // 1.4, y1)], fill=(255, 220, 90, 255))
        d.text((x1, y1 + dy * 16 * S), t, font=R, fill='white', anchor='mm')
    else:
        d.polygon([(x1 + dx * a, y1), (x1, y1 - a // 1.4), (x1, y1 + a // 1.4)], fill=(255, 220, 90, 255))
        d.text((x1 + dx * 2 * S, y1 - 14 * S), t, font=R, fill='white', anchor='mm')
# side keys, top to bottom as on the case
keys = (('MENU', 'menu'), ('RUN', 'fire'), ('AUTO', 'use'), ('MF', 'next weapon'))
for i, (k, a) in enumerate(keys):
    y = oy + (40 + i * 80) * S
    d.rounded_rectangle([14 * S, y - 16 * S, 64 * S, y + 16 * S], radius=6 * S, outline=(200, 200, 200), width=2 * S)
    d.text((39 * S, y), k, font=R, fill='white', anchor='mm')
    d.text((72 * S, y), a, font=R, fill=(220, 220, 220), anchor='lm')
d.text((ox, 10 * S), 'Touch zones (480 x 320)', font=R, fill=(200, 200, 200), anchor='lm')
d.text((14 * S, 10 * S), 'Side keys', font=R, fill=(200, 200, 200), anchor='lm')
d.text((ox, oy + 330 * S), 'POWER held 1.5 s: off.  Two fingers work at once (move + fire).', font=R, fill=(200, 200, 200), anchor='lm')
img.save(out, optimize=True)
print(img.size)
