# DINE app icon from the DINE Identity v1: the graphite tile (#2C2C2E -> #121214) with the
# split D in Paper, on Apple's 1024 icon grid (an 824 pt tile, 100 pt clear on every side).
from PIL import Image, ImageDraw, ImageFilter
SS = 4; N = 1024 * SS
tile = 824 * SS; off = (N - tile) // 2; radius = int(tile * 38 / 168)
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
# soft drop shadow, as the identity's dock icon carries
sh = Image.new("RGBA", (N, N), (0, 0, 0, 0))
ImageDraw.Draw(sh).rounded_rectangle([off, off + 12*SS, off + tile, off + tile + 12*SS], radius, fill=(0, 0, 0, 150))
img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(18*SS)))
# the gradient tile
grad = Image.new("RGBA", (N, N))
top, bot = (0x2C, 0x2C, 0x2E), (0x12, 0x12, 0x14)
gd = ImageDraw.Draw(grad)
for y in range(off, off + tile):
    t = (y - off) / tile
    gd.line([(0, y), (N, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,))
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([off, off, off + tile, off + tile], radius, fill=255)
img.paste(grad, (0, 0), mask)
# the top edge's hairline of light
hl = Image.new("RGBA", (N, N), (0, 0, 0, 0))
hd = ImageDraw.Draw(hl)
hd.rounded_rectangle([off, off, off + tile, off + tile], radius, outline=(255, 255, 255, 46), width=3*SS)
fade = Image.new("L", (N, N), 0)
fd = ImageDraw.Draw(fade)
for y in range(off, off + tile // 3):
    fd.line([(0, y), (N, y)], fill=int(255 * (1 - (y - off) / (tile / 3))))
img = Image.alpha_composite(img, Image.composite(hl, Image.new("RGBA", (N, N), (0, 0, 0, 0)), fade))
# the split D: stem 1 unit, split 0.4, bowl 2 units, 4 units tall; 0.86 of the identity's 96 on a 168 tile
h = tile * (96 * 0.86) / 168; u = h / 96
w = 82 * u
x0 = (N - w) / 2 + tile * (6 * 0.86) / 168; y0 = (N - h) / 2
d = ImageDraw.Draw(img)
paper = (0xF5, 0xF5, 0xF7, 255)
d.rounded_rectangle([x0, y0, x0 + 24*u, y0 + h], 3*u, fill=paper)
bx = x0 + 34*u
d.rectangle([bx, y0, bx + 1, y0 + h], fill=paper)
d.pieslice([bx - 48*u, y0, bx + 48*u, y0 + h], -90, 90, fill=paper)
out = img.resize((1024, 1024), Image.LANCZOS)
out.save("app/resources/AppIcon.png")   # run from the repo root: python3 scripts/brand-icon.py
print("ok")
