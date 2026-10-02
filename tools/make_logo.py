#!/usr/bin/env python3
"""A channel logo for the web remote, in three versions with a transparent background: 360x160 PNGs
(twice the size they are shown at, for sharp phone screens) for /retrotv/logos/ on the card
(needs Pillow; SVGs go through rsvg-convert). Use official or freely licensed logos, and note the
source in /retrotv/logos/SOURCES.txt.

    tools/make_logo.py <url|file> <channel id> <out_dir> [auto|keep|lift|mono] [--black M] [--white M]

  <id>.png        in colour, for dark keys:
                    auto  (default) lift when most of the logo is black or dark grey, else keep
                    keep  colours as they are
                    lift  dark parts (navy text, black lettering) become cream
                    mono  white-on-black artwork (printed on a black box): the black becomes transparent
  <id>.black.png  one black ink, for light keys
  <id>.white.png  one white ink, for dark keys that want a plain look
                    M = solid: the whole shape in the ink
                        tonal: shades of the ink, so letters with an outline keep their outline
                        (default: tonal when the logo has light and dark parts, else solid)
Logos are sized by visual weight, not by their box: a thin wordmark gets more room than a solid
badge, so a row of keys looks even. The three versions sit in exactly the same place.
    tools/make_logo.py --self-test"""
import argparse, io, math, subprocess, sys, urllib.request
from PIL import Image

W, H = 360, 160
BOX_W, BOX_H = 344, 136       # the logo never touches the edges
TARGET = 0.5 * BOX_W * BOX_H   # visual weight: area x sqrt(ink density)
CREAM = (236, 230, 218)       # --card-ink in src/web/RemotePage.h
DARK_LUM = 70                 # below this a pixel disappears on a dark key
SPREAD = 60                   # a logo whose light and dark parts differ less than this is one flat colour
FLOOR = 0.22                  # the faintest shade of a tonal logo: its shape never vanishes
MAX_BYTES = 32 * 1024         # WEB_LOGO_MAX_BYTES in include/config.h


def lum(r, g, b):
    return 0.3 * r + 0.59 * g + 0.11 * b


def load(data: bytes) -> Image.Image:
    if b"<svg" in data[:400]:
        data = subprocess.run(["rsvg-convert", "-w", "1600"], input=data, capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(data)).convert("RGBA")


def mono(im: Image.Image) -> Image.Image:
    """White-on-black: keep the black box only (it may sit on a white margin), then brightness = alpha."""
    grey = im.convert("L")
    box = grey.point(lambda v: 255 if v < 128 else 0).getbbox()
    grey = grey.crop(box) if box else grey
    out = Image.new("RGBA", grey.size, (255, 255, 255, 0))
    out.putalpha(grey.point(lambda v: max(0, v - 32) * 255 // 223))  # a "black" that is not quite black
    return out


def lift(im: Image.Image) -> Image.Image:
    """Dark pixels take the cream colour, keeping their alpha (anti-aliased edges stay smooth)."""
    px = [(CREAM + (a,)) if a and lum(r, g, b) < DARK_LUM else (r, g, b, a) for r, g, b, a in im.get_flattened_data()]
    out = Image.new("RGBA", im.size)
    out.putdata(px)
    return out


def dark_share(im: Image.Image) -> float:
    """Share of the logo that is black or dark grey (a dark purple or red still reads on the key)."""
    ink = [p for p in im.get_flattened_data() if p[3] > 128]
    grey = lambda r, g, b: max(r, g, b) - min(r, g, b) < 40
    return sum(1 for r, g, b, _ in ink if lum(r, g, b) < DARK_LUM and grey(r, g, b)) / max(1, len(ink))


def lum_range(im: Image.Image) -> tuple:
    """The 3rd and 97th percentile brightness of the logo's opaque pixels."""
    ls = sorted(lum(r, g, b) for r, g, b, a in im.get_flattened_data() if a > 128)
    if not ls:
        return 0.0, 0.0
    return ls[len(ls) * 3 // 100], ls[len(ls) * 97 // 100]


def one_ink(im: Image.Image, ink: tuple, mode: str) -> tuple:
    """The logo in one colour. tonal: the shade follows brightness (dark ink: dark parts strongest)."""
    lo, hi = lum_range(im)
    if mode == "auto":
        mode = "tonal" if hi - lo >= SPREAD else "solid"
    dark_ink = lum(*ink) < 128
    px = []
    for r, g, b, a in im.get_flattened_data():
        if mode == "solid" or hi <= lo:
            k = 1.0
        else:
            n = min(1.0, max(0.0, (lum(r, g, b) - lo) / (hi - lo)))
            n = n * n * (3 - 2 * n)  # smoothstep: mid tones lean to one end, so the logo is not muddy
            k = FLOOR + (1 - FLOOR) * ((1 - n) if dark_ink else n)
        px.append(ink + (round(a * k),))
    out = Image.new("RGBA", im.size)
    out.putdata(px)
    return out, mode


def geometry(art: Image.Image) -> tuple:
    """Where the logo is in its artwork and how much to scale it: shared by the three versions."""
    box = art.getchannel("A").point(lambda a: 255 if a > 10 else 0).getbbox() or (0, 0, art.width, art.height)
    im = art.crop(box)
    density = sum(1 for a in im.getchannel("A").get_flattened_data() if a > 128) / (im.width * im.height)
    scale = min(BOX_W / im.width, BOX_H / im.height,
                math.sqrt(TARGET / (im.width * im.height * math.sqrt(max(density, 0.05)))))
    return box, scale


def fit(im: Image.Image, box: tuple, scale: float) -> Image.Image:
    im = im.crop(box)
    im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
    tile = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    tile.alpha_composite(im, ((W - im.width) // 2, (H - im.height) // 2))
    return tile


def encode(tile: Image.Image) -> bytes:
    out = io.BytesIO()
    tile.save(out, "PNG", optimize=True)
    if out.tell() <= MAX_BYTES:
        return out.getvalue()
    out = io.BytesIO()  # too big for the TV: 256 colours with alpha look the same at this size
    tile.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(out, "PNG", optimize=True)
    return out.getvalue()


def make(data: bytes, tone: str = "auto", black: str = "auto", white: str = "auto") -> dict:
    """{"": colour PNG, "black": ..., "white": ...} plus the choices made, under "tone", "black_mode", "white_mode"."""
    art = load(data)
    if tone == "mono":
        art = mono(art)
    box, scale = geometry(art)
    colour = art
    if tone == "lift" or (tone == "auto" and dark_share(art) > 0.5):
        tone = "lift"
        colour = lift(art)
    dark, black = one_ink(art, (0, 0, 0), black)
    light, white = one_ink(art, (255, 255, 255), white)
    return {"": encode(fit(colour, box, scale)), "black": encode(fit(dark, box, scale)), "white": encode(fit(light, box, scale)),
            "tone": "keep" if tone == "auto" else tone, "black_mode": black, "white_mode": white}


def self_test() -> None:
    open_png = lambda b: Image.open(io.BytesIO(b)).convert("RGBA")
    # A black wordmark is lifted to cream; a white-on-black box keeps only the white letters.
    black = Image.new("RGBA", (200, 40), (0, 0, 0, 0))
    black.paste((20, 20, 24, 255), (20, 10, 180, 30))
    got = make(_png(black))
    out = open_png(got[""])
    assert got["tone"] == "lift" and out.size == (W, H), got["tone"]
    assert out.getpixel((0, 0))[3] == 0 and out.getpixel((W // 2, H // 2))[:3] == CREAM, out.getpixel((W // 2, H // 2))
    boxed = Image.new("RGBA", (300, 200), (255, 255, 255, 255))
    boxed.paste((0, 0, 0, 255), (50, 50, 250, 150))
    boxed.paste((255, 255, 255, 255), (100, 90, 200, 110))
    out = open_png(make(_png(boxed), "mono")[""])
    assert out.getpixel((0, 0))[3] == 0 and out.getpixel((W // 2, H // 2)) == (255, 255, 255, 255)
    # A thin wordmark gets more room than a solid block of the same shape.
    thin, solid = Image.new("RGBA", (400, 100)), Image.new("RGBA", (400, 100), (200, 0, 0, 255))
    for x in range(0, 400, 8):
        thin.paste((200, 0, 0, 255), (x, 0, x + 1, 100))
    width = lambda img: open_png(make(_png(img))[""]).getchannel("A").getbbox()[2:]
    assert width(thin) > width(solid), (width(thin), width(solid))
    # One ink: a flat logo is solid; yellow letters with a dark outline are tonal, and the black
    # version keeps the outline strong and the letters faint (the white version the other way).
    got = make(_png(solid))
    assert got["black_mode"] == "solid" and open_png(got["black"]).getpixel((W // 2, H // 2)) == (0, 0, 0, 255)
    outlined = Image.new("RGBA", (300, 100), (0, 0, 0, 0))
    outlined.paste((60, 20, 20, 255), (0, 0, 300, 100))
    outlined.paste((250, 220, 40, 255), (20, 20, 280, 80))
    got = make(_png(outlined))
    blk, wht = open_png(got["black"]), open_png(got["white"])
    edge, mid = (W // 2, H // 2 - 40), (W // 2, H // 2)
    assert got["black_mode"] == got["white_mode"] == "tonal"
    assert blk.getpixel(edge)[3] > 200 and blk.getpixel(mid)[3] < 80, (blk.getpixel(edge), blk.getpixel(mid))
    assert wht.getpixel(mid)[3] > 200 and wht.getpixel(edge)[3] < 80, (wht.getpixel(mid), wht.getpixel(edge))
    solid_box = lambda im: im.getchannel("A").point(lambda v: 255 if v > 40 else 0).getbbox()
    assert solid_box(blk) == solid_box(open_png(got[""]))  # the versions sit in the same place
    print("make_logo self-test: OK")


def _png(im: Image.Image) -> bytes:
    out = io.BytesIO()
    im.save(out, "PNG")
    return out.getvalue()


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return 0
    p = argparse.ArgumentParser(usage=__doc__)
    p.add_argument("src")
    p.add_argument("cid")
    p.add_argument("out_dir")
    p.add_argument("tone", nargs="?", default="auto", choices=("auto", "keep", "lift", "mono"))
    p.add_argument("--black", default="auto", choices=("auto", "solid", "tonal"))
    p.add_argument("--white", default="auto", choices=("auto", "solid", "tonal"))
    a = p.parse_args()
    data = (urllib.request.urlopen(urllib.request.Request(a.src, headers={"User-Agent": "RETROTV/0.2 (personal TV)"}),
                                   timeout=30).read() if a.src.startswith("http") else open(a.src, "rb").read())
    got = make(data, a.tone, a.black, a.white)
    base = f"{a.out_dir.rstrip('/')}/{a.cid}"
    for suffix in ("", "black", "white"):
        open(f"{base}{'.' + suffix if suffix else ''}.png", "wb").write(got[suffix])
    kb = lambda k: len(got[k]) // 1024
    print(f"{a.cid:20} {got['tone']:5} {kb(''):2d} KB · black {got['black_mode']:5} {kb('black'):2d} KB"
          f" · white {got['white_mode']:5} {kb('white'):2d} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
