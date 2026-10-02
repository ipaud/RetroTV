#!/usr/bin/env python3
"""Stickers with the web remote's QR code, to glue on the case: upright (36 x 46 mm) or, with
--apaisada, the back label (84 x 28 mm: QR, the RETROTV logo and how to use the TV).

The code carries http://retrotv.local, which works on any network (the IP changes between
home and the office). Phones that do not resolve .local names use the QR channel instead.

Usage (needs `pip install segno` and rsvg-convert, brew install librsvg):
    tools/make_remote_sticker.py <out_dir> [--url http://retrotv.local] [--apaisada]

Writes one sticker at real size (.svg and .pdf) and an A4 sheet of them with cut lines:
pegatina_mando / hoja_A4_pegatinas, or pegatina_trasera / hoja_A4_traseras. Print at 100 %,
no "fit to page"."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import segno

W, H, R = 36.0, 46.0, 3.0          # sticker, mm
MODULE = 1.0                       # mm per QR module
QUIET = 2                          # modules of white around the code, inside the sticker
QR_TOP = 2.5
INK, PAPER, CUT = "#111111", "#ffffff", "#bbbbbb"
A4_W, A4_H, GAP = 210.0, 297.0, 4.0


def sticker(url: str, x: float, y: float, cut_line: bool) -> str:
    qr = segno.make(url, error="m", micro=False)  # segno raises the level when it still fits: Q here
    rows = list(qr.matrix)
    side = (len(rows) + 2 * QUIET) * MODULE
    qx, qy = x + (W - side) / 2 + QUIET * MODULE, y + QR_TOP + QUIET * MODULE
    dots = "".join(f'<rect x="{qx + c * MODULE:.3f}" y="{qy + r * MODULE:.3f}" width="{MODULE}" height="{MODULE}"/>'
                   for r, row in enumerate(rows) for c, v in enumerate(row) if v)
    outline = f' stroke="{CUT}" stroke-width="0.2"' if cut_line else ""
    host = url.split("//", 1)[-1].rstrip("/")
    return (f'<rect x="{x}" y="{y}" width="{W}" height="{H}" rx="{R}" fill="{PAPER}"{outline}/>'
            f'<g fill="{INK}" shape-rendering="crispEdges">{dots}</g>'
            f'<text x="{x + W / 2}" y="{y + 38.2}" text-anchor="middle" font-family="Futura" font-weight="700" '
            f'font-size="6.2" letter-spacing="0.35" fill="{INK}">RETROTV</text>'
            f'<text x="{x + W / 2}" y="{y + 43.2}" text-anchor="middle" font-family="Menlo" font-size="2.3" '
            f'fill="{INK}">MANDO · {host}</text>')


# --- Back label: 84 x 28 mm, cream like an old set's rating plate ------------------------------
BW, BH, BR = 84.0, 28.0, 3.0
CREAM, NAVY, CYAN, MAGENTA, YELLOW = "#f4ecd8", "#1b2b6b", "#2ec4e8", "#e6399b", "#ffc93c"
BACK_MODULE = 0.8


def back_label(url: str, x: float, y: float, cut_line: bool) -> str:
    qr = segno.make(url, error="m", micro=False)
    rows = list(qr.matrix)
    qx, qy = x + 2.4 + QUIET * BACK_MODULE, y + 2.4 + QUIET * BACK_MODULE
    dots = "".join(f'<rect x="{qx + c * BACK_MODULE:.3f}" y="{qy + r * BACK_MODULE:.3f}" width="{BACK_MODULE}" '
                   f'height="{BACK_MODULE}"/>' for r, row in enumerate(rows) for c, v in enumerate(row) if v)
    outline = f' stroke="{CUT}" stroke-width="0.2"' if cut_line else ""
    host = url.split("//", 1)[-1].rstrip("/")
    lx = x + 30.0  # the logo side
    swoosh = "".join(
        f'<path d="M{lx - 1.0:.2f},{y + 16.4 + i * 0.95:.2f} Q{lx + 24:.2f},{y + 17.6 + i * 0.95:.2f} '
        f'{x + 81.0:.2f},{y + 12.4 + i * 0.95:.2f}" fill="none" stroke="{color}" stroke-width="0.8" '
        f'stroke-linecap="round"/>' for i, color in enumerate((CYAN, MAGENTA, YELLOW)))
    logo = (f'<g transform="translate({lx + 25.5:.2f},{y + 11.4:.2f}) skewX(-12)" font-family="Futura" '
            f'font-weight="800" font-stretch="condensed" font-size="10" text-anchor="middle" letter-spacing="0.2">'
            f'<text x="0.55" y="0.55" fill="{CYAN}">RETROTV</text><text fill="{NAVY}">RETROTV</text></g>')
    text = (f'<g font-family="Futura" font-weight="500" font-size="2.1" fill="{NAVY}">'
            f'<text x="{lx}" y="{y + 22.3:.2f}">MANDO: escanea el QR o {host}</text>'
            f'<text x="{lx}" y="{y + 25.0:.2f}">ON: cualquier tecla · OFF: CH- 2 s · USB-C</text></g>')
    return (f'<rect x="{x}" y="{y}" width="{BW}" height="{BH}" rx="{BR}" fill="{CREAM}"{outline}/>'
            f'<rect x="{x + 1.2}" y="{y + 1.2}" width="{BW - 2.4}" height="{BH - 2.4}" rx="{BR - 1}" fill="none" '
            f'stroke="{NAVY}" stroke-width="0.3"/>'
            f'<g fill="{NAVY}" shape-rendering="crispEdges">{dots}</g>'
            f'<line x1="{x + 27.6}" y1="{y + 3.5}" x2="{x + 27.6}" y2="{y + BH - 3.5}" stroke="{NAVY}" stroke-width="0.3"/>'
            f'{swoosh}{logo}{text}')


def svg(width: float, height: float, body: str) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}mm" height="{height}mm" '
            f'viewBox="0 0 {width} {height}">{body}</svg>\n')


def to_pdf(svg_path: Path) -> Path:
    pdf = svg_path.with_suffix(".pdf")
    subprocess.run(["rsvg-convert", "-f", "pdf", "-o", str(pdf), str(svg_path)], check=True)
    return pdf


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--url", default="http://retrotv.local")
    parser.add_argument("--apaisada", action="store_true", help="the 84 x 28 mm back label")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    draw, w, h, name, sheet_name = ((back_label, BW, BH, "pegatina_trasera", "hoja_A4_traseras") if args.apaisada
                                    else (sticker, W, H, "pegatina_mando", "hoja_A4_pegatinas"))

    single = args.out_dir / f"{name}.svg"
    single.write_text(svg(w, h, draw(args.url, 0, 0, cut_line=False)))
    to_pdf(single)

    cols, rows = int((A4_W - 20 + GAP) // (w + GAP)), int((A4_H - 20 + GAP) // (h + GAP))
    x0 = (A4_W - (cols * w + (cols - 1) * GAP)) / 2
    y0 = (A4_H - (rows * h + (rows - 1) * GAP)) / 2
    body = "".join(draw(args.url, x0 + c * (w + GAP), y0 + r * (h + GAP), cut_line=True)
                   for r in range(rows) for c in range(cols))
    sheet = args.out_dir / f"{sheet_name}.svg"
    sheet.write_text(svg(A4_W, A4_H, body))
    to_pdf(sheet)
    sheet.unlink()  # the PDF is what gets printed
    print(f"{single.with_suffix('.pdf')}\n{sheet.with_suffix('.pdf')} ({cols * rows} stickers {w:g} x {h:g} mm)")


if __name__ == "__main__":
    main()
