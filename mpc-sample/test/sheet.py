# contact sheet: each screenshot with the pad LEDs beside it
import sys, glob, os
from PIL import Image, ImageDraw, ImageFont
d = sys.argv[1]; out = sys.argv[2]; names = sys.argv[3:]
f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
tiles = []
for n in names:
    im = Image.open(os.path.join(d, n + '.ppm')).convert('RGB')
    W = 320 + 110; t = Image.new('RGB', (W, 262), (40, 40, 44)); t.paste(im, (0, 0))
    dr = ImageDraw.Draw(t); dr.text((4, 244), n, font=f, fill=(230, 230, 230))
    leds = {}
    try:
        for line in open(os.path.join(d, n + '.leds')):
            a = line.split()
            if a[0] == 'pad': leds[int(a[1])] = tuple(min(255, int(v) * 2) for v in a[2:5])
    except FileNotFoundError: pass
    for p in range(16):
        c = leds.get(p, (0, 0, 0)); col, row = p % 4, 3 - p // 4
        x, y = 330 + col * 24, 20 + row * 24
        dr.rectangle((x, y, x + 20, y + 20), fill=c, outline=(90, 90, 96))
    tiles.append(t)
cols = 2; rows = (len(tiles) + 1) // 2
sheet = Image.new('RGB', (cols * 430 + 10, rows * 268 + 6), (20, 20, 22))
for i, t in enumerate(tiles): sheet.paste(t, (6 + (i % 2) * 434, 6 + (i // 2) * 268))
sheet.save(out)
