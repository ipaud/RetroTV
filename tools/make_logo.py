#!/usr/bin/env python3
"""A channel logo for the web remote: 180x80 PNG, the logo fitted with a margin on a plain
background, named <channel id>.png for /retrotv/logos/ on the card (needs Pillow; SVGs go
through rsvg-convert). Use official or freely licensed logos, and note the source in
/retrotv/logos/SOURCES.txt.

    tools/make_logo.py <url|file> <channel id> <out_dir> [auto|dark|cream|#rrggbb]"""
import io, subprocess, sys, urllib.request
from PIL import Image
if len(sys.argv) < 4:
    sys.exit(__doc__)
src, cid, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
bg = sys.argv[4] if len(sys.argv) > 4 else "auto"
data = (urllib.request.urlopen(urllib.request.Request(src, headers={"User-Agent": "RETROTV/0.2 (personal TV)"}), timeout=30).read()
        if src.startswith("http") else open(src, "rb").read())
if src.lower().split("?")[0].endswith(".svg"):
    data = subprocess.run(["rsvg-convert", "-w", "600"], input=data, capture_output=True, check=True).stdout
im = Image.open(io.BytesIO(data)).convert("RGBA")
box = im.getbbox()
if box: im = im.crop(box)
im.thumbnail((164, 64), Image.LANCZOS)
if bg == "auto":  # dark logos on cream, light ones on near-black
    px = [p for p in im.get_flattened_data() if p[3] > 128]
    lum = sum(0.3 * r + 0.59 * g + 0.11 * b for r, g, b, _ in px) / max(1, len(px))
    bg = "cream" if lum < 110 else "dark"
color = {"dark": (24, 24, 24), "cream": (242, 236, 222)}.get(bg) or tuple(int(bg[i:i + 2], 16) for i in (1, 3, 5))
tile = Image.new("RGBA", (180, 80), color + (255,))
tile.alpha_composite(im, ((180 - im.width) // 2, (80 - im.height) // 2))
out = f"{out_dir.rstrip('/')}/{cid}.png"
tile.convert("RGB").save(out, optimize=True)
print(cid, bg, im.size)
