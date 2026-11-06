#!/usr/bin/env python3
"""Render ELooper's macOS app icon -> Resources/AppIcon.png (1024x1024 RGBA)."""
import math
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 4                      # supersampling factor
N = 1024 * S
TILE = 824 * S
OFF = (N - TILE) // 2
RADIUS = int(TILE * 0.225)
C = N / 2

COLORS = ["#ff5252", "#ffab40", "#ffee58", "#69f0ae", "#40c4ff", "#7c4dff"]


def hex2rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def squircle_mask(size, box, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=255)
    return m


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "Resources", "AppIcon.png")
    box = (OFF, OFF, OFF + TILE - 1, OFF + TILE - 1)
    mask = squircle_mask(N, box, RADIUS)

    canvas = Image.new("RGBA", (N, N), (0, 0, 0, 0))

    # drop shadow
    sh = Image.new("L", (N, N), 0)
    sh.paste(mask.point(lambda v: int(v * 0.55)), (0, 12 * S))
    sh = sh.filter(ImageFilter.GaussianBlur(18 * S))
    shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    shadow.putalpha(sh)
    canvas = Image.alpha_composite(canvas, shadow)

    # tile gradient
    grad = Image.new("RGBA", (N, N))
    gd = ImageDraw.Draw(grad)
    top, bot = (0x14, 0x14, 0x18), (0, 0, 0)
    for y in range(OFF, OFF + TILE):
        t = (y - OFF) / TILE
        col = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        gd.line([(0, y), (N, y)], fill=col + (255,))
    tile = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)

    # inner highlight edge
    inner = squircle_mask(N, (box[0] + 3 * S, box[1] + 3 * S, box[2] - 3 * S, box[3] - 3 * S), RADIUS - 3 * S)
    edge = ImageChops.subtract(mask, inner)
    vg = Image.linear_gradient("L").resize((N, N))          # black top -> white bottom
    vg = ImageChops.invert(vg).point(lambda v: int(v * 0.16))  # strong at top
    edge_a = ImageChops.multiply(edge, vg)
    hl = Image.new("RGBA", (N, N), (255, 255, 255, 0))
    hl.putalpha(edge_a)
    tile = Image.alpha_composite(tile, hl)
    canvas = Image.alpha_composite(canvas, tile)

    # ring made of arc segments
    R = 262 * S            # centre radius of ring
    W = 78 * S             # stroke thickness
    gap = 21.0             # degrees between cap centres (round caps eat ~17 deg)
    n = len(COLORS)
    seg = 360 / n
    ring = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    bb = (C - R - W / 2, C - R - W / 2, C + R + W / 2, C + R + W / 2)
    start0 = -90 + 30 - seg  # rotate so the palette flows clockwise from ~top
    for i, col in enumerate(COLORS):
        a0 = start0 + i * seg + gap / 2
        a1 = start0 + (i + 1) * seg - gap / 2
        rd.arc(bb, a0, a1, fill=hex2rgb(col) + (255,), width=W)
        # rounded caps
        for a in (a0, a1):
            x = C + R * math.cos(math.radians(a))
            y = C + R * math.sin(math.radians(a))
            rd.ellipse((x - W / 2, y - W / 2, x + W / 2, y + W / 2), fill=hex2rgb(col) + (255,))
    # glow
    glow = ring.filter(ImageFilter.GaussianBlur(28 * S))
    ga = glow.getchannel("A").point(lambda v: min(255, int(v * 1.1)))
    glow.putalpha(ga)
    canvas = Image.alpha_composite(canvas, glow)
    canvas = Image.alpha_composite(canvas, ring)

    # playhead at 1 o'clock (-60 degrees)
    pa = math.radians(-60)
    px, py = C + R * math.cos(pa), C + R * math.sin(pa)
    ph_glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    pr = 56 * S
    ImageDraw.Draw(ph_glow).ellipse((px - pr, py - pr, px + pr, py + pr), fill=(255, 255, 255, 255))
    ph_glow = ph_glow.filter(ImageFilter.GaussianBlur(14 * S))
    canvas = Image.alpha_composite(canvas, ph_glow)
    pd = ImageDraw.Draw(canvas)
    r1 = 50 * S
    pd.ellipse((px - r1, py - r1, px + r1, py + r1), fill=(10, 10, 14, 255))
    r2 = 42 * S
    pd.ellipse((px - r2, py - r2, px + r2, py + r2), fill=(255, 255, 255, 255))

    # waveform bars
    heights = [110, 200, 300, 380, 300, 200, 110]
    bw, bg = 40 * S, 22 * S
    total = len(heights) * bw + (len(heights) - 1) * bg
    x0 = C - total / 2
    wv = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wv)
    for i, h in enumerate(heights):
        h *= S
        x = x0 + i * (bw + bg)
        wd.rounded_rectangle((x, C - h / 2, x + bw, C + h / 2), radius=bw // 2, fill=(246, 246, 250, 255))
    soft = wv.filter(ImageFilter.GaussianBlur(10 * S))
    soft.putalpha(soft.getchannel("A").point(lambda v: int(v * 0.35)))
    canvas = Image.alpha_composite(canvas, soft)
    canvas = Image.alpha_composite(canvas, wv)

    img = canvas.resize((1024, 1024), Image.LANCZOS)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out, optimize=True)
    for s in (128, 32):
        img.resize((s, s), Image.LANCZOS).save(f"/tmp/claude-0/icon-preview-{s}.png")


if __name__ == "__main__":
    main()
