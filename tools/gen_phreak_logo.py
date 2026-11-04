#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The PHREAK logo: a lightning bolt in the four track colours (blue, green, yellow, orange) striking
down through the open bottom of a 270-degree dial (the knobs of the UI), over a geometric monoline
wordmark, "phreak", drawn like SLOOP's (tools/gen_logo.py).

  tools/gen_phreak_logo.py OUT.h          firmware boot splash (RLE, 16-colour palette, RGB565)
  tools/gen_phreak_logo.py --assets DIR   phreak-logo.svg/png, phreak-icon.svg/png, phreak-splash.png
  tools/gen_phreak_logo.py --web DIR      the web app's icons: icon-192.png, icon-512.png, icon-maskable-512.png
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

BLUE, GREEN, YELLOW, ORANGE = (40, 124, 255), (30, 204, 112), (255, 198, 24), (255, 98, 26)
WHITE, BLACK = (242, 242, 242), (0, 0, 0)
BANDS = [BLUE, GREEN, YELLOW, ORANGE]

# ---- geometry (units: the icon is 240 x 240, the wordmark x-height is 72) -------------------
DIAL_R, DIAL_W = 106, 12
# the bolt: down from the top right, a step to the right, out through the dial's open bottom
BOLT = [(140, 30), (78, 119), (120, 119), (100, 220), (176, 92), (134, 92), (166, 30)]
BAND_GAP = 6
XH, SW, ASC, DESC, GAP = 72, 12, 32, 36, 16      # x-height, stroke, ascender, descender, letter gap


def draw_icon(im, ox, oy, k):
    d = ImageDraw.Draw(im)
    cx, cy, r, w = ox + 120 * k, oy + 120 * k, DIAL_R * k, DIAL_W * k
    d.arc([cx - r, cy - r, cx + r, cy + r], start=135, end=405, fill=WHITE, width=max(1, round(w)))
    for a in (135, 405):
        x = cx + (r - w / 2) * math.cos(math.radians(a))
        y = cy + (r - w / 2) * math.sin(math.radians(a))
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=WHITE)
    # the bolt, cut across into four bands, one per track colour
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).polygon([(ox + x * k, oy + y * k) for x, y in BOLT], fill=255)
    top, bot = min(y for _, y in BOLT), max(y for _, y in BOLT)
    bh = (bot - top - 3 * BAND_GAP) / 4
    for i, c in enumerate(BANDS):
        y0 = oy + (top + i * (bh + BAND_GAP)) * k
        y1 = oy + (top + i * (bh + BAND_GAP) + bh) * k if i < 3 else im.size[1]
        band = Image.new("L", im.size, 0)
        ImageDraw.Draw(band).rectangle([0, y0, im.size[0], y1], fill=255)
        im.paste(c, mask=ImageChops.multiply(mask, band))


def word_width(k=1.0):
    return (72 + 60 + 44 + 72 + 72 + 54 + 5 * GAP) * k


def draw_word(d, ox, oy, k, col=WHITE):
    w, xh = SW * k, XH * k
    base = oy + xh
    g = GAP * k

    def cap(x, y):
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col)

    def line(x0, y0, x1, y1):
        d.line([(x0, y0), (x1, y1)], fill=col, width=max(1, round(w)))
        cap(x0, y0)
        cap(x1, y1)

    def arc(box, a, b, ends=True):
        d.arc(box, start=a, end=b, fill=col, width=max(1, round(w)))
        if ends:
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            rx, ry = (box[2] - box[0]) / 2 - w / 2, (box[3] - box[1]) / 2 - w / 2
            for t in (a, b):
                cap(cx + rx * math.cos(math.radians(t)), cy + ry * math.sin(math.radians(t)))

    x = ox
    R = xh / 2
    # p: a bowl, the stem down on its left
    d.ellipse([x, oy, x + 2 * R, oy + 2 * R], outline=col, width=max(1, round(w)))
    line(x + w / 2, oy + R, x + w / 2, base + DESC * k)
    x += 2 * R + g
    # h: the stem up, an arch, the right leg
    hw = 60 * k
    line(x + w / 2, oy - ASC * k, x + w / 2, base - w / 2)
    arc([x, oy, x + hw, oy + hw], 180, 360, ends=False)
    line(x + hw - w / 2, oy + hw / 2, x + hw - w / 2, base - w / 2)
    x += hw + g
    # r: the stem, a shoulder
    rw = 44 * k
    line(x + w / 2, oy + R, x + w / 2, base - w / 2)
    arc([x, oy, x + 2 * rw, oy + 2 * R], 180, 270)
    x += rw + g
    # e: a bowl open at the lower right, its bar across the middle
    arc([x, oy, x + 2 * R, oy + 2 * R], 40, 360)
    line(x + w / 2, oy + R, x + 2 * R - w / 2, oy + R)
    x += 2 * R + g
    # a: a bowl, the stem on its right
    d.ellipse([x, oy, x + 2 * R, oy + 2 * R], outline=col, width=max(1, round(w)))
    line(x + 2 * R - w / 2, oy + w / 2, x + 2 * R - w / 2, base - w / 2)
    x += 2 * R + g
    # k: the stem up, the arm, the leg
    kw = 54 * k
    line(x + w / 2, oy - ASC * k, x + w / 2, base - w / 2)
    line(x + w / 2, oy + 0.62 * xh, x + kw - w / 2, oy + w / 2)
    line(x + 0.36 * kw, oy + 0.42 * xh, x + kw - w / 2, base - w / 2)


def render(kind, W, H, k, bg=BLACK, ss=4):
    im = Image.new("RGB", (W * ss, H * ss), bg)
    d = ImageDraw.Draw(im)
    K = k * ss
    if kind == "h":                                   # icon + wordmark side by side
        draw_icon(im, 10 * K, (H * ss - 240 * K) / 2, K)
        draw_word(d, 280 * K, H * ss / 2 - 36 * K, K)
    elif kind == "v":                                 # icon over the wordmark (the boot splash)
        draw_icon(im, (W * ss - 240 * K) / 2, 0, K)
        draw_word(d, (W * ss - word_width(K)) / 2, (240 + 18 + ASC) * K, K)
    else:
        draw_icon(im, (W * ss - 240 * K) / 2, (H * ss - 240 * K) / 2, K)
    return im.resize((W, H), Image.LANCZOS)


# ---- SVG (stroke centred on the path; the same shapes as draw_icon / draw_word) -----------------------
HEX = lambda c: "#%02X%02X%02X" % c


def svg_icon(ox, oy, uid="phk"):
    cx, cy, w = ox + 120, oy + 120, DIAL_W
    r = DIAL_R - w / 2
    a0, a1 = math.radians(135), math.radians(405)
    p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
    p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
    s = [f'<path d="M{p0[0]:.2f} {p0[1]:.2f} A{r:.2f} {r:.2f} 0 1 1 {p1[0]:.2f} {p1[1]:.2f}" fill="none" '
         f'stroke="{HEX(WHITE)}" stroke-width="{w}" stroke-linecap="round"/>']
    pts = " ".join(f"{ox + x:.2f},{oy + y:.2f}" for x, y in BOLT)
    s.append(f'<clipPath id="{uid}-bolt"><polygon points="{pts}"/></clipPath>')
    top, bot = min(y for _, y in BOLT), max(y for _, y in BOLT)
    bh = (bot - top - 3 * BAND_GAP) / 4
    for i, c in enumerate(BANDS):
        y0 = oy + top + i * (bh + BAND_GAP)
        h = bh if i < 3 else bot - top - 3 * (bh + BAND_GAP) + 1
        s.append(f'<rect x="{ox:.2f}" y="{y0:.2f}" width="240" height="{h:.2f}" fill="{HEX(c)}" '
                 f'clip-path="url(#{uid}-bolt)"/>')
    return s


def svg_word(ox, oy):
    w, xh, col, R = SW, XH, HEX(WHITE), XH / 2
    base, x = oy + xh, ox
    s = []

    def line(x0, y0, x1, y1):
        s.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{col}" stroke-width="{w}" '
                 f'stroke-linecap="round"/>')

    def ring(cx, cy):
        s.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{R - w / 2:.2f}" fill="none" stroke="{col}" stroke-width="{w}"/>')

    def arc(box, a, b, cap="round"):                # PIL's arc: clockwise from a to b, the stroke inside the box
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        rx, ry = (box[2] - box[0]) / 2 - w / 2, (box[3] - box[1]) / 2 - w / 2
        p = (cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))
        q = (cx + rx * math.cos(math.radians(b)), cy + ry * math.sin(math.radians(b)))
        s.append(f'<path d="M{p[0]:.2f} {p[1]:.2f} A{rx:.2f} {ry:.2f} 0 {1 if b - a > 180 else 0} 1 {q[0]:.2f} {q[1]:.2f}" '
                 f'fill="none" stroke="{col}" stroke-width="{w}" stroke-linecap="{cap}"/>')
    ring(x + R, oy + R)                             # p
    line(x + w / 2, oy + R, x + w / 2, base + DESC)
    x += 2 * R + GAP
    hw = 60                                         # h
    line(x + w / 2, oy - ASC, x + w / 2, base - w / 2)
    arc([x, oy, x + hw, oy + hw], 180, 360, "butt")
    line(x + hw - w / 2, oy + hw / 2, x + hw - w / 2, base - w / 2)
    x += hw + GAP
    rw = 44                                         # r
    line(x + w / 2, oy + R, x + w / 2, base - w / 2)
    arc([x, oy, x + 2 * rw, oy + 2 * R], 180, 270)
    x += rw + GAP
    arc([x, oy, x + 2 * R, oy + 2 * R], 40, 360)    # e
    line(x + w / 2, oy + R, x + 2 * R - w / 2, oy + R)
    x += 2 * R + GAP
    ring(x + R, oy + R)                             # a
    line(x + 2 * R - w / 2, oy + w / 2, x + 2 * R - w / 2, base - w / 2)
    x += 2 * R + GAP
    kw = 54                                         # k
    line(x + w / 2, oy - ASC, x + w / 2, base - w / 2)
    line(x + w / 2, oy + 0.62 * xh, x + kw - w / 2, oy + w / 2)
    line(x + 0.36 * kw, oy + 0.42 * xh, x + kw - w / 2, base - w / 2)
    return s


def svg(kind, bg=True):
    """kind "h": the icon and the wordmark side by side; "i": the icon. bg: a black square first (the installer's
    page makes it transparent: index_pkg.html .logo rect:first-child)"""
    if kind == "h":
        W, H = 820, 260
        body = svg_icon(10, 10) + svg_word(280, H / 2 - 36)
    else:
        W, H = 240, 240
        body = svg_icon(0, 0)
    head = f'<rect width="{W}" height="{H}" fill="#000"/>\n' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
            f'aria-label="PHREAK">\n' + head + "\n".join(body) + "\n</svg>\n")


# ---- firmware splash ---------------------------------------------------------------------------
SPLASH_W, SPLASH_H = 240, 188
SPLASH_K = 0.46
assert (240 + 18 + ASC + XH + DESC + SW / 2) * SPLASH_K <= SPLASH_H, "the wordmark's p must fit the splash"
assert word_width(SPLASH_K) + SW * SPLASH_K <= SPLASH_W, "the wordmark must fit the splash"


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def splash_header(path):
    im = render("v", SPLASH_W, SPLASH_H, SPLASH_K)
    # a fixed palette: black, and each logo colour at 1/3, 2/3 and full (the anti-aliased edges)
    cols = [BLACK]
    for c in (WHITE, BLUE, GREEN, YELLOW, ORANGE):
        cols += [tuple(round(v * f) for v in c) for f in (1 / 3, 2 / 3, 1.0)]
    src = im.tobytes()
    px = []
    cache = {}
    for i in range(0, len(src), 3):
        p = (src[i], src[i + 1], src[i + 2])
        if p not in cache:
            cache[p] = min(range(16), key=lambda k: sum((p[j] - cols[k][j]) ** 2 for j in range(3)))
        px.append(cache[p])
    rle = bytearray()                                 # (run - 1) << 4 | index, runs of 1..16
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 16:
            j += 1
        rle.append(((j - i - 1) << 4) | px[i])
        i = j
    L = ["/* generated by tools/gen_phreak_logo.py: the PHREAK boot splash */", "#pragma once", "#include <stdint.h>",
         f"#define PHREAK_SPLASH_W {SPLASH_W}", f"#define PHREAK_SPLASH_H {SPLASH_H}",
         "static const uint16_t PHREAK_SPLASH_PAL[16] = {" + ", ".join(f"0x{rgb565(c):04X}" for c in cols) + "};",
         f"static const uint8_t PHREAK_SPLASH_RLE[{len(rle)}] = {{"]
    for k in range(0, len(rle), 24):
        L.append("    " + ", ".join(str(b) for b in rle[k:k + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")
    print(f"logo: phreak splash {SPLASH_W}x{SPLASH_H}, {len(rle)} B RLE -> {path}")


def assets(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "phreak-logo.svg").write_text(svg("h"))
    (out / "phreak-icon.svg").write_text(svg("i"))
    render("h", 1640, 520, 2.0).save(out / "phreak-logo.png")
    render("i", 512, 512, 512 / 240).save(out / "phreak-icon.png")
    render("v", SPLASH_W, SPLASH_H, SPLASH_K).resize((SPLASH_W * 2, SPLASH_H * 2), Image.NEAREST).save(out / "phreak-splash.png")
    print(f"logo: phreak assets in {out}")


def web_icons(out):
    """the web app's icons (web/make_site.py): the bolt and dial on black; maskable: inside the safe circle (80 %)"""
    out = Path(out)
    render("i", 192, 192, 192 / 240).save(out / "icon-192.png")
    render("i", 512, 512, 512 / 240).save(out / "icon-512.png")
    render("i", 512, 512, 512 * 0.72 / 240).save(out / "icon-maskable-512.png")
    print(f"logo: phreak web icons in {out}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--assets":
        assets(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "--web":
        web_icons(sys.argv[2])
    elif len(sys.argv) == 2:
        splash_header(sys.argv[1])
    else:
        sys.exit(__doc__)
