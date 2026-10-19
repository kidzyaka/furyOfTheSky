"""Menu skin: metal buttons, panels and widgets (numpy + Pillow).

Usage (from the project root):  python3 tools/textures/ui.py [--preview-only]
Writes assets/ui/*.ftx (+ the fonts, the HUD's warning plates) and previews to
build/previews/ui_*.png.
Style and layout: docs/decisions/0035-menus-ui.md.

The look (author's reference: War Thunder's "To Battle!" button): a plate of
greenish aluminium - 1 px dark outline, 1 px light bevel on top, a dark one
at the bottom, a linear vertical gradient with a soft sheen below the top
edge (slightly glossy, no glass split). Age is in the metal itself, as
half-tones, not drawn-on damage: brushed grain, faded/oxidised patches,
edges and corners polished brighter by hands, fine hairline scratches.

All noise is periodic in x, so a button strip can be cut and re-joined at
any width without a visible seam in the grain.
"""

import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "pipeline"))

import ftx  # noqa: E402
from png import write_png  # noqa: E402

PREVIEW = os.path.join(ROOT, "build", "previews")
OUT = os.path.join(ROOT, "assets", "ui")
HEADER = os.path.join(ROOT, "src", "ui", "ui_atlas.h")

OUTLINE = np.array([8, 10, 9]) / 255

# Metal finishes: (top, bottom) of the gradient, 0..255, and the gloss strength.
FINISH = {
    "metal":   ((104, 112, 106), (52, 58, 54), 1.0),      # ordinary button: gunmetal aluminium, green cast
    "focus":   ((196, 204, 192), (122, 132, 120), 1.0),   # cursor on it: polished, lit; dark text
    "sub":     ((80, 86, 82), (38, 43, 40), 0.9),         # second-level rows: the same metal, darker
    "primary": ((128, 48, 40), (58, 20, 18), 1.0),        # the one action that starts a sortie
    "primary_focus": ((164, 66, 52), (80, 28, 24), 1.25),
    "panel":   ((60, 66, 62), (36, 40, 38), 0.45),        # darker plate under groups
    "plate":   ((120, 128, 122), (78, 86, 80), 0.8),      # nameplates
}


# ------------------------------------------------------------------ noise

def periodic_noise(h, w, cell, seed, period_x=None, cell_y=None):
    """Smooth value noise, periodic in x with `period_x` (default w).
    Cosine-interpolated random lattice; rows are not periodic. cell_y
    different from cell makes it anisotropic (brushed grain)."""
    period_x = period_x or w
    cell_y = cell_y or cell
    gw = max(1, int(round(period_x / cell)))
    gh = int(h / cell_y) + 2
    rng = np.random.default_rng(seed)
    g = rng.random((gh + 1, gw))
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    fx = x / period_x * gw
    fy = y / cell_y
    x0 = np.floor(fx).astype(int)
    y0 = np.floor(fy).astype(int)
    tx = fx - x0
    ty = fy - y0
    tx = (1 - np.cos(tx * math.pi)) * 0.5
    ty = (1 - np.cos(ty * math.pi)) * 0.5
    x0m, x1m = x0 % gw, (x0 + 1) % gw
    a = g[y0, x0m] * (1 - tx) + g[y0, x1m] * tx
    b = g[y0 + 1, x0m] * (1 - tx) + g[y0 + 1, x1m] * tx
    return a * (1 - ty) + b * ty


def fbm(h, w, cell, seed, octaves=3, period_x=None):
    out = np.zeros((h, w))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        c = max(1, cell >> o)
        out += amp * periodic_noise(h, w, c, seed + 17 * o, period_x)
        total += amp
        amp *= 0.5
    return out / total


def uniform(n):
    """Rank-normalise a noise field to 0..1 so a threshold is a probability."""
    flat = n.reshape(-1)
    r = np.empty(flat.size)
    r[np.argsort(flat, kind="stable")] = np.arange(flat.size) / flat.size
    return r.reshape(n.shape)


def grain(h, w, seed):
    return np.random.default_rng(seed).normal(0.0, 1.0, (h, w))


# ------------------------------------------------------------------ shapes

def chamfer_mask(h, w, c):
    """Rectangle with 45-degree cut corners of c pixels."""
    y, x = np.mgrid[0:h, 0:w]
    xr, yb = w - 1 - x, h - 1 - y
    cut = (x + y < c) | (xr + y < c) | (x + yb < c) | (xr + yb < c)
    return ~cut


def erode(mask):
    m = mask.copy()
    m[1:, :] &= mask[:-1, :]
    m[:-1, :] &= mask[1:, :]
    m[:, 1:] &= mask[:, :-1]
    m[:, :-1] &= mask[:, 1:]
    m[0, :] = m[-1, :] = False
    m[:, 0] = m[:, -1] = False
    return m


def edge_distance(mask, steps=8):
    """Pixels to the outside of `mask`, capped at `steps` (repeated erosion)."""
    d = np.zeros(mask.shape)
    cur = mask
    for i in range(steps):
        d += cur
        cur = erode(cur)
    return d


def scratches(h, w, count, seed, angle_sd=0.35, length=(3, 16), period_x=None):
    """Anti-aliased hairline scratches, mostly along the plate (x)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    px = period_x or w
    for _ in range(count):
        x0, y0 = rng.uniform(0, px), rng.uniform(0, h)
        a = rng.normal(0.0, angle_sd) + (math.pi if rng.random() < 0.5 else 0.0)
        n = int(rng.uniform(*length))
        depth = rng.uniform(0.2, 0.75)
        for i in range(n * 2):
            t = i * 0.5
            xf, yf = x0 + math.cos(a) * t, y0 + math.sin(a) * t
            xi, yi = int(math.floor(xf)), int(math.floor(yf))
            fx, fy = xf - xi, yf - yi
            fade = depth * min(1.0, (n - t) / 3.0, (t + 1) / 3.0)
            for dx, dy, wgt in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
                yy = yi + dy
                if 0 <= yy < h:
                    xx = (xi + dx) % px
                    if xx < w:
                        out[yy, xx] = max(out[yy, xx], wgt * fade * 1.6)
    return np.clip(out, 0, 1)


# ------------------------------------------------------------------ plates

def metal_plate(h, w, finish, seed, chamfer=2, wear=1.0, period_x=None, bevel=True, rivets=False):
    """RGBA plate of aged aluminium (see the module docstring)."""
    top, bottom, gloss = FINISH[finish]
    top, bottom = np.array(top) / 255, np.array(bottom) / 255
    mask = chamfer_mask(h, w, chamfer)
    inner = erode(mask)
    yy, xx = np.mgrid[0:h, 0:w]
    t = np.clip((yy - 1) / max(1, h - 3), 0, 1)
    body = top[None, None] * (1 - t[..., None]) + bottom[None, None] * t[..., None]
    # Gloss of polished metal reflecting a lit room: a bright band in the
    # upper third, a darker band below the middle, a faint bounce at the
    # bottom edge. Streaked along x by the brushing, so it reads as metal.
    brush = periodic_noise(h, w, 40, seed + 1, period_x, cell_y=1)
    brush2 = periodic_noise(h, w, 7, seed + 2, period_x, cell_y=1)
    refl = (0.20 * np.exp(-((t - 0.20) / 0.15) ** 2) - 0.07 * np.exp(-((t - 0.64) / 0.14) ** 2)
            + 0.05 * np.exp(-((t - 0.96) / 0.07) ** 2))
    refl = refl * (0.75 + 0.5 * brush)
    body = body + gloss * refl[..., None] * (0.55 + 0.45 * body.mean(-1, keepdims=True) / max(top.mean(), 1e-3))
    lum = np.ones((h, w))
    # Brushed grain: fine faint streaks along the plate.
    lum += 0.020 * (brush - 0.5) * 2 + 0.016 * (brush2 - 0.5) * 2

    # Fading: broad patches where the anodising went dull and pale.
    fade = fbm(h, w, 20, seed + 3, 3, period_x)
    fade = np.clip((fade - 0.45) * 2.2, 0, 1) * wear
    grey = body.mean(-1, keepdims=True)
    body = body * (1 - 0.30 * fade[..., None]) + (grey * 1.06) * (0.30 * fade[..., None])
    # Oxidation: a darker, mottled half-tone settling towards the bottom.
    ox = fbm(h, w, 6, seed + 4, 3, period_x)
    lum -= 0.08 * wear * np.clip((ox - 0.5) * 2.5, 0, 1) * (0.35 + 0.65 * t)
    # Edges and corners polished by hands: brighter, smoother metal.
    dist = edge_distance(inner, 8)
    dx = np.minimum(xx, w - 1 - xx)  # a strip's ends are the button's real corners
    dy = np.minimum(yy, h - 1 - yy)
    corner = np.exp(-np.hypot(np.minimum(dx, 40), dy) / 6.0)
    edge = np.exp(-(dist - 1) / 1.6)
    rub = periodic_noise(h, w, 10, seed + 5, period_x)
    lum += wear * (0.06 * edge * rub + 0.07 * corner)
    # Hairline scratches: mostly brighter (cut into the oxide), some darker.
    sc_light = scratches(h, w, int(w * h / 380 * wear), seed + 6, period_x=period_x)
    sc_dark = scratches(h, w, int(w * h / 900 * wear), seed + 7, period_x=period_x, angle_sd=0.8)
    lum += 0.10 * sc_light - 0.07 * sc_dark
    # Pitting: rare tiny dark specks.
    pit = np.random.default_rng(seed + 8).random((h, w)) < 0.004 * wear
    lum -= 0.12 * pit
    lum += 0.012 * grain(h, w, seed + 9)
    body = body * lum[..., None]

    if bevel:
        # Light from above (War Thunder's plates): 1 px light along the top,
        # 1 px dark along the bottom, faint on the sides.
        first = inner & ~np.roll(inner, 1, 0)
        last = inner & ~np.roll(inner, -1, 0)
        left = inner & ~np.roll(inner, 1, 1)
        right = inner & ~np.roll(inner, -1, 1)
        body = np.where(first[..., None], np.clip(body * 1.22 + 0.04, 0, 1), body)
        body = np.where(last[..., None], body * 0.72, body)
        body = np.where((left & ~first & ~last)[..., None], np.clip(body * 1.06, 0, 1), body)
        body = np.where((right & ~first & ~last)[..., None], body * 0.88, body)

    if rivets:
        metal = np.array([150, 160, 148]) / 255
        for cx, cy in ((4, 4), (w - 5, 4), (4, h - 5), (w - 5, h - 5)):
            r = np.hypot(xx - cx, yy - cy)
            head = np.clip(1.9 - r, 0, 1)
            shade = np.clip(0.5 + 0.35 * ((cx - xx) + (cy - yy)), 0, 1)
            col = metal * (0.55 + 0.55 * shade[..., None])
            body = body * (1 - head[..., None]) + col * head[..., None]
            ring = np.clip(1 - abs(r - 2.4), 0, 1)
            body *= (1 - 0.35 * ring[..., None])

    rgb = np.where(inner[..., None], body, OUTLINE[None, None])
    alpha = np.where(mask, np.where(inner, 1.0, 0.70), 0.0)
    return np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], -1)


def lamp(size, color, on=True):
    """Small annunciator lamp set into the plate."""
    y, x = (np.mgrid[0:size, 0:size] + 0.5) - size / 2
    r = np.hypot(x, y) / (size / 2)
    c = np.array(color) / 255
    lit = c * (1.25 - 0.55 * r[..., None]) if on else c * 0.25
    rgb = np.where((r < 0.72)[..., None], np.clip(lit, 0, 1), OUTLINE * 1.4)
    a = np.clip((1.0 - r) * size / 2, 0, 1)
    return np.concatenate([rgb, a[..., None]], -1)


# ------------------------------------------------------------------ preview helpers

def over(dst, src, x, y):
    h, w = src.shape[:2]
    a = src[..., 3:4]
    dst[y:y + h, x:x + w, :3] = dst[y:y + h, x:x + w, :3] * (1 - a) + src[..., :3] * a


def strip_button(strip, w, cap=16):
    """What the game draws for a button of width w from a long strip: the
    left part, then the right cap (the noise is periodic, no visible seam)."""
    out = np.concatenate([strip[:, :w - cap], strip[:, -cap:]], 1)
    return out




# ================================================================== font

FONT_TTC = "/System/Library/Fonts/Supplemental/PTSans.ttc"   # ships with macOS; PT Sans is OFL
FONT_INDEX = 2                                                 # PT Sans Narrow Bold
SHADOW = 0.8                                                   # drop shadow opacity (1 px down-right)

ASCII = [chr(c) for c in range(32, 127)]
CYRILLIC = [chr(c) for c in range(0x410, 0x450)] + ["Ё", "ё"]
EXTRA = list("№°×—–«»•…")
FULL = ASCII + CYRILLIC + EXTRA
UPPER = [c for c in ASCII if not ("a" <= c <= "z")] + list("×—•")

# Faces: (C++ name, pixel size, charset). Order = FontFace enum in ui_atlas.h.
FACES = [
    ("Small", 13, FULL),    # tags, captions (never smaller: PSP-3000 LCD)
    ("Body", 15, FULL),     # body text, button hints, stats
    ("Label", 18, FULL),    # second-level buttons, list rows, panel titles
    ("Button", 20, FULL),   # main buttons, values
    ("Title", 24, FULL),    # screen titles
    ("Huge", 30, UPPER),    # big numbers and stamps
]


def rasterize_glyph(font, ch, size):
    """Coverage bitmap of one glyph and its placement relative to the pen at
    the top of the line (PIL anchor 'la'). None for blank glyphs."""
    from PIL import Image, ImageDraw
    pad = size
    im = Image.new("L", (size * 3, size * 3), 0)
    ImageDraw.Draw(im).text((pad, pad), ch, 255, font=font, anchor="la")
    a = np.asarray(im, np.float64) / 255
    ys, xs = np.nonzero(a > 0.0)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return a[y0:y1, x0:x1], x0 - pad, y0 - pad


def encode_glyph(cov):
    """T8 index per texel: high nibble = fill coverage, low nibble = the drop
    shadow (fill shifted 1 px down-right). The cell grows by 1 px for it."""
    h, w = cov.shape
    fill = np.zeros((h + 1, w + 1))
    fill[:h, :w] = cov
    shadow = np.zeros_like(fill)
    shadow[1:, 1:] = cov
    f = np.round(fill * 15).astype(np.uint8)
    s = np.round(shadow * 15).astype(np.uint8)
    return (f << 4) | s


def font_clut():
    """Composite CLUT for previews and the default look: white fill over a
    black shadow. The game builds its own CLUTs from the nibbles (fill only,
    shadow only) for other styles (src/render/font.cpp)."""
    clut = np.zeros((256, 4))
    for i in range(256):
        af, ash = (i >> 4) / 15, (i & 15) / 15 * SHADOW
        a = af + ash * (1 - af)
        clut[i] = (af / a if a > 0 else 0,) * 3 + (a,)
    return clut


class Packer:
    """Shelf packer: rows of glyphs sorted by height."""

    def __init__(self, w, h, pad=1):
        self.w, self.h, self.pad = w, h, pad
        self.x = self.y = self.row = 0

    def place(self, w, h):
        if self.x + w > self.w:
            self.x, self.y, self.row = 0, self.y + self.row + self.pad, 0
        if self.y + h > self.h:
            raise RuntimeError("atlas full")
        pos = (self.x, self.y)
        self.x += w + self.pad
        self.row = max(self.row, h)
        return pos


def build_font():
    from PIL import ImageFont
    W, H = 512, 512
    atlas = np.zeros((H, W), np.uint8)
    packer = Packer(W, H)
    faces, glyphs = [], []
    jobs = []
    for fi, (name, size, charset) in enumerate(FACES):
        font = ImageFont.truetype(FONT_TTC, size, index=FONT_INDEX)
        ascent, descent = font.getmetrics()
        cap = rasterize_glyph(font, "H", size)
        cap_top, cap_h = cap[2], cap[0].shape[0]
        face = dict(name=name, size=size, line=ascent + descent, ascent=ascent, cap_top=cap_top, cap_h=cap_h,
                    glyphs=[])
        faces.append(face)
        for ch in sorted(set(charset), key=ord):
            r = rasterize_glyph(font, ch, size)
            adv = font.getlength(ch)
            if r is None:
                face["glyphs"].append(dict(cp=ord(ch), x=0, y=0, w=0, h=0, ox=0, oy=0, adv=adv))
                continue
            cells = encode_glyph(r[0])
            g = dict(cp=ord(ch), w=cells.shape[1], h=cells.shape[0], ox=r[1], oy=r[2], adv=adv)
            face["glyphs"].append(g)
            jobs.append((g, cells))
    for g, cells in sorted(jobs, key=lambda j: -j[1].shape[0]):
        g["x"], g["y"] = packer.place(g["w"], g["h"])
        atlas[g["y"]:g["y"] + g["h"], g["x"]:g["x"] + g["w"]] = cells
    used = packer.y + packer.row
    H2 = 1 << (used - 1).bit_length()
    H2 = max(H2, 64)
    atlas = atlas[:H2]
    clut = font_clut()
    ftx.export_indexed(os.path.join(OUT, "font.ftx"), [atlas], clut)
    write_font_file(os.path.join(OUT, "font.fnt"), faces, W, H2)
    show = clut[atlas]
    write_png(os.path.join(PREVIEW, "ui_font_atlas.png"), show[..., :3] * show[..., 3:])
    n = sum(len(f["glyphs"]) for f in faces)
    print(f"[ui] font: {len(faces)} faces, {n} glyphs, atlas {W}x{H2} ({W * H2 // 1024} KB)")
    return faces


def write_font_file(path, faces, w, h):
    """FFNT v1 (docs/formats/ffnt.md)."""
    import struct
    total = sum(len(f["glyphs"]) for f in faces)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sHHHHI", b"FFNT", 1, len(faces), w, h, total))
        first = 0
        for f in faces:
            fh.write(struct.pack("<HHHhHHI", f["size"], f["line"], f["ascent"], f["cap_top"], f["cap_h"],
                                 len(f["glyphs"]), first))
            first += len(f["glyphs"])
        for f in faces:
            for g in f["glyphs"]:
                adv = int(round(g["adv"] * 4))
                assert 0 <= adv < 256 and -128 <= g["ox"] < 128 and -128 <= g["oy"] < 128
                fh.write(struct.pack("<IHHBBbbB3x", g["cp"], g["x"], g["y"], g["w"], g["h"], g["ox"], g["oy"], adv))


# ================================================================== HUD font and warning plates
# HUD v3 (docs/decisions/0049): the flight HUD's text is bitmap too, from the
# same PT Sans Narrow Bold, but in its own small atlas (font.ftx is full) and
# with two differences from the menu faces:
# - the second nibble holds a 1 px OUTLINE all round, not a drop shadow: the
#   text stands on sky, cloud, sea and sunset with no backdrop under it;
# - digits share one advance (tabular), so a changing number doesn't jitter.

HUD_UPPER = [c for c in ASCII if not ("a" <= c <= "z")] + [chr(c) for c in range(0x410, 0x430)] + ["Ё", "°", "×"]
HUD_FACES = [
    ("Small", 13, HUD_UPPER),   # captions, values, labels (never smaller: 0031)
    ("Large", 20, HUD_UPPER),   # messages, the selected-weapon value
    ("Number", 22, HUD_UPPER),  # speed and altitude
]


def encode_glyph_outline(cov):
    """T8 index: high nibble = fill, low nibble = the fill dilated by 1 px
    (3x3 max). The cell grows by 1 px on every side."""
    h, w = cov.shape
    fill = np.zeros((h + 2, w + 2))
    fill[1:h + 1, 1:w + 1] = cov
    out = fill.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out = np.maximum(out, np.roll(np.roll(fill, dy, 0), dx, 1))
    f = np.round(fill * 15).astype(np.uint8)
    o = np.round(out * 15).astype(np.uint8)
    return (f << 4) | o


def build_hud_font():
    from PIL import ImageFont
    W, H = 256, 256
    atlas = np.zeros((H, W), np.uint8)
    packer = Packer(W, H)
    faces, jobs = [], []
    for name, size, charset in HUD_FACES:
        font = ImageFont.truetype(FONT_TTC, size, index=FONT_INDEX)
        ascent, descent = font.getmetrics()
        cap = rasterize_glyph(font, "H", size)
        face = dict(name=name, size=size, line=ascent + descent, ascent=ascent, cap_top=cap[2],
                    cap_h=cap[0].shape[0], glyphs=[])
        faces.append(face)
        digit_adv = max(font.getlength(d) for d in "0123456789")
        for ch in sorted(set(charset), key=ord):
            r = rasterize_glyph(font, ch, size)
            adv = font.getlength(ch)
            shift = 0
            if ch.isdigit():  # tabular: centre the digit in the widest digit's advance
                shift = int(round((digit_adv - adv) / 2))
                adv = digit_adv
            if r is None:
                face["glyphs"].append(dict(cp=ord(ch), x=0, y=0, w=0, h=0, ox=0, oy=0, adv=adv))
                continue
            cells = encode_glyph_outline(r[0])
            g = dict(cp=ord(ch), w=cells.shape[1], h=cells.shape[0], ox=r[1] - 1 + shift, oy=r[2] - 1, adv=adv)
            face["glyphs"].append(g)
            jobs.append((g, cells))
    for g, cells in sorted(jobs, key=lambda j: -j[1].shape[0]):
        g["x"], g["y"] = packer.place(g["w"], g["h"])
        atlas[g["y"]:g["y"] + g["h"], g["x"]:g["x"] + g["w"]] = cells
    used = packer.y + packer.row
    H2 = max(1 << (used - 1).bit_length(), 64)
    atlas = atlas[:H2]
    clut = font_clut()
    ftx.export_indexed(os.path.join(OUT, "hud_font.ftx"), [atlas], clut)
    write_font_file(os.path.join(OUT, "hud_font.fnt"), faces, W, H2)
    show = clut[atlas]
    write_png(os.path.join(PREVIEW, "ui_hud_font_atlas.png"), show[..., :3] * show[..., 3:])
    print(f"[ui] hud font: {len(faces)} faces, atlas {W}x{H2} ({W * H2 // 1024} KB)")
    return faces


# Ace Combat-style warning plates: a filled plate with the text cut out of it,
# the cut darkened (a pure hole loses the letters on a blue sky or a sunset -
# compared in docs/mockups/hud-v3/warning_plates.png). White in the texture,
# the game tints it red / amber / green with the vertex colour.
# (name, text, font px, plate height, side padding)
HUD_PLATES = [
    ("PullUp", "PULL UP", 20, 26, 12), ("Missile", "MISSILE", 20, 26, 12), ("Stall", "STALL", 20, 26, 12),
    ("Crashed", "CRASHED", 20, 26, 12), ("RadarLock", "RADAR LOCK", 20, 26, 12),
    ("ReturnToArea", "RETURN TO MISSION AREA", 20, 26, 12), ("TurningBack", "TURNING BACK", 20, 26, 12),
    ("MissionComplete", "MISSION COMPLETE", 24, 32, 16), ("MissionFailed", "MISSION FAILED", 24, 32, 16),
]
PLATE_BODY_A = 215 / 255   # the plate
PLATE_HOLE_A = 120 / 255   # the cut letters: dark glass, the scene shows through
PLATE_RIM = (0.55, 235 / 255)  # 1 px darker rim: holds the edge on a red sunset too
PLATE_LEVELS = 14          # CLUT 0..13: letter coverage, 14: rim, 15+: empty


def plate_clut():
    clut = np.zeros((256, 4))
    for i in range(PLATE_LEVELS):
        c = i / (PLATE_LEVELS - 1)
        clut[i] = (1 - c, 1 - c, 1 - c, PLATE_BODY_A + (PLATE_HOLE_A - PLATE_BODY_A) * c)
    clut[PLATE_LEVELS] = (PLATE_RIM[0],) * 3 + (PLATE_RIM[1],)
    return clut


def build_hud_plates():
    from PIL import Image, ImageDraw, ImageFont
    W, H = 256, 256
    atlas = np.full((H, W), 255, np.uint8)
    packer = Packer(W, H, pad=2)
    rects = {}
    for name, text, px, ph, pad in HUD_PLATES:
        font = ImageFont.truetype(FONT_TTC, px, index=FONT_INDEX)
        tw = int(math.ceil(font.getlength(text)))
        w = tw + 2 * pad
        mask = Image.new("L", (w, ph), 0)
        cap = rasterize_glyph(font, "H", px)
        # Capitals centred vertically: PIL's 'la' anchor puts the line top at y.
        ImageDraw.Draw(mask).text((w / 2, (ph - cap[0].shape[0]) / 2 - cap[2]), text, 255, font=font, anchor="ma")
        cov = np.asarray(mask, np.float64) / 255
        cells = np.round(cov * (PLATE_LEVELS - 1)).astype(np.uint8)
        cells[0, :] = cells[-1, :] = PLATE_LEVELS
        cells[:, 0] = cells[:, -1] = PLATE_LEVELS
        x, y = packer.place(w, ph)
        atlas[y:y + ph, x:x + w] = cells
        rects[name] = (x, y, w, ph)
    used = packer.y + packer.row
    H2 = max(1 << (used - 1).bit_length(), 64)
    atlas = atlas[:H2]
    clut = plate_clut()
    ftx.export_indexed(os.path.join(OUT, "hud_plates.ftx"), [atlas], clut)
    tint = np.array([1.0, 0.31, 0.24])  # preview in the warning red
    show = clut[atlas]
    write_png(os.path.join(PREVIEW, "ui_hud_plates.png"), checker_under(np.concatenate(
        [show[..., :3] * tint, show[..., 3:]], -1)))
    print(f"[ui] hud plates: {len(rects)} plates, atlas {W}x{H2}")
    return rects


# ================================================================== skin atlas

SKIN_W, SKIN_H = 256, 512


def build_skin():
    """Button strips, bars, panels: RGBA, k-means CLUT, one level."""
    img = np.zeros((SKIN_H, SKIN_W, 4))
    rects = {}

    def put(name, tile, x, y):
        h, w = tile.shape[:2]
        img[y:y + h, x:x + w] = tile
        rects[name] = (x, y, w, h)

    y = 0
    strips = [  # (name, height, finish, seed)
        ("Btn32", 32, "metal", 11), ("Btn32Focus", 32, "focus", 18),
        ("Btn32Primary", 32, "primary", 25), ("Btn32PrimaryFocus", 32, "primary_focus", 32),
        ("Btn28", 28, "metal", 40), ("Btn28Sub", 28, "sub", 41), ("Btn28Focus", 28, "focus", 42),
        ("Tab24", 24, "sub", 50), ("Tab24On", 24, "focus", 51),
    ]
    for name, h, finish, seed in strips:
        put(name, metal_plate(h, SKIN_W, finish, seed, period_x=SKIN_W), 0, y)
        y += h
    put("Bar26", metal_plate(26, SKIN_W, "panel", 91, chamfer=0, wear=0.4, period_x=SKIN_W), 0, y)
    y += 26
    # 9-slice plates: the dark panel under groups, the light nameplate, an inset well.
    put("Panel", metal_plate(96, 96, "panel", 77, chamfer=3, rivets=True), 0, y)
    put("Plate", metal_plate(96, 96, "plate", 78, chamfer=3, rivets=True), 96, y)
    put("Inset", inset_well(64, 64, 79), 192, y)
    y += 96
    # Progress bar: recessed track and a lit amber fill.
    put("Track8", inset_well(8, SKIN_W, 81, chamfer=1), 0, y)
    y += 8
    put("Fill8", metal_plate(8, SKIN_W, "focus", 82, chamfer=1, wear=0.3, period_x=SKIN_W) * [1.0, 0.82, 0.45, 1.0],
        0, y)
    y += 8
    assert y <= SKIN_H
    rgba = np.clip(img, 0, 1)
    shown = ftx.export_quantized(os.path.join(OUT, "skin.ftx"), rgba, max_levels=1)
    write_png(os.path.join(PREVIEW, "ui_skin_atlas.png"), checker_under(shown))
    print(f"[ui] skin: {len(rects)} pieces, atlas {SKIN_W}x{SKIN_H}, used to row {y}")
    return rects


def inset_well(h, w, seed, chamfer=2):
    """A recessed area (list background, image frame, bar track): darker
    metal with an inner shadow along the top and left."""
    p = metal_plate(h, w, "panel", seed, chamfer=chamfer, wear=0.5, bevel=False)
    yy, xx = np.mgrid[0:h, 0:w]
    sh = np.maximum(np.exp(-(yy - 1) / 2.0), 0.6 * np.exp(-(xx - 1) / 2.0))
    p[..., :3] *= (0.62 * (1 - 0.6 * sh))[..., None]
    last = (yy == h - 2) & (xx > 0) & (xx < w - 1)
    p[..., :3] = np.where(last[..., None], np.clip(p[..., :3] * 1.9 + 0.05, 0, 1), p[..., :3])
    return p


def checker_under(rgba):
    h, w = rgba.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    bg = np.where(((yy // 8 + xx // 8) % 2)[..., None] == 0, 0.30, 0.38) * np.ones(3)
    return bg * (1 - rgba[..., 3:]) + rgba[..., :3] * rgba[..., 3:]


# ================================================================== icons atlas

ICON_W, ICON_H = 256, 256
SS = 4  # supersampling for vector-drawn icons


class Canvas:
    """RGBA drawing at SS x resolution with PIL, box-filtered down."""

    def __init__(self, w, h):
        from PIL import Image, ImageDraw
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    @staticmethod
    def c(rgb, a=255):
        return tuple(int(v) for v in rgb) + (int(a),)

    def s(self, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def poly(self, pts, rgb, a=255):
        self.d.polygon(self.s(pts), fill=self.c(rgb, a))

    def circle(self, cx, cy, r, rgb, a=255, width=0):
        box = [(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS]
        if width:
            self.d.ellipse(box, outline=self.c(rgb, a), width=int(width * SS))
        else:
            self.d.ellipse(box, fill=self.c(rgb, a))

    def line(self, pts, rgb, width, a=255):
        self.d.line(self.s(pts), fill=self.c(rgb, a), width=int(round(width * SS)), joint="curve")

    def rect(self, x0, y0, x1, y1, rgb, a=255, radius=0):
        box = [x0 * SS, y0 * SS, x1 * SS, y1 * SS]
        if radius:
            self.d.rounded_rectangle(box, radius * SS, fill=self.c(rgb, a))
        else:
            self.d.rectangle(box, fill=self.c(rgb, a))

    def text(self, x, y, s, size, rgb, anchor="mm"):
        from PIL import ImageFont
        f = ImageFont.truetype(FONT_TTC, size * SS, index=FONT_INDEX)
        self.d.text((x * SS, y * SS), s, fill=self.c(rgb), font=f, anchor=anchor)

    def rgba(self):
        a = np.asarray(self.im, np.float64) / 255
        a = a.reshape(self.h, SS, self.w, SS, 4)
        rgb = (a[..., :3] * a[..., 3:]).mean((1, 3))
        al = a[..., 3].mean((1, 3))
        out = np.zeros((self.h, self.w, 4))
        out[..., :3] = np.where(al[..., None] > 0, rgb / np.maximum(al[..., None], 1e-6), 0)
        out[..., 3] = al
        return out


def star_pts(cx, cy, r, inner=0.40, n=5, rot=-math.pi / 2):
    pts = []
    for i in range(n * 2):
        rr = r if i % 2 == 0 else r * inner
        a = rot + i * math.pi / n
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def psp_button(symbol):
    """Face button: dark disc, bevel ring, the symbol in its PS colour."""
    cv = Canvas(18, 18)
    cv.circle(9, 9, 8.6, (14, 16, 15))
    cv.circle(9, 9, 7.8, (46, 50, 48))
    cv.circle(9, 9.6, 7.0, (30, 33, 32))
    col = {"cross": (120, 170, 255), "circle": (255, 100, 92), "triangle": (90, 220, 170),
           "square": (240, 140, 210)}[symbol]
    if symbol == "cross":
        cv.line([(5.6, 5.6), (12.4, 12.4)], col, 1.8)
        cv.line([(12.4, 5.6), (5.6, 12.4)], col, 1.8)
    elif symbol == "circle":
        cv.circle(9, 9, 3.9, col, width=1.7)
    elif symbol == "triangle":
        cv.line([(9, 4.8), (13.2, 12), (4.8, 12), (9, 4.8)], col, 1.6)
    else:
        cv.line([(5.4, 5.4), (12.6, 5.4), (12.6, 12.6), (5.4, 12.6), (5.4, 5.4)], col, 1.6)
    return cv.rgba()


def shoulder_button(letter):
    cv = Canvas(26, 16)
    cv.rect(0.5, 0.5, 25.5, 15.5, (14, 16, 15), radius=4)
    cv.rect(1.5, 1.5, 24.5, 14.5, (52, 56, 54), radius=3.5)
    cv.rect(1.5, 3.0, 24.5, 14.5, (32, 35, 34), radius=3.5)
    cv.text(13, 8.6, letter, 11, (226, 230, 220))
    return cv.rgba()


def dpad(highlight):
    cv = Canvas(18, 18)
    arm = [(6, 0.6), (12, 0.6), (12, 6), (17.4, 6), (17.4, 12), (12, 12), (12, 17.4), (6, 17.4), (6, 12),
           (0.6, 12), (0.6, 6), (6, 6)]
    cv.poly(arm, (14, 16, 15))
    inner = [(7, 1.6), (11, 1.6), (11, 7), (16.4, 7), (16.4, 11), (11, 11), (11, 16.4), (7, 16.4), (7, 11),
             (1.6, 11), (1.6, 7), (7, 7)]
    cv.poly(inner, (50, 54, 52))
    lit = (226, 230, 220)
    if "lr" in highlight:
        cv.poly([(2.4, 9), (5.6, 6.6), (5.6, 11.4)], lit)
        cv.poly([(15.6, 9), (12.4, 6.6), (12.4, 11.4)], lit)
    if "ud" in highlight:
        cv.poly([(9, 2.4), (6.6, 5.6), (11.4, 5.6)], lit)
        cv.poly([(9, 15.6), (6.6, 12.4), (11.4, 12.4)], lit)
    return cv.rgba()


def chevron(direction):
    cv = Canvas(14, 24)
    pts = [(3, 2), (12, 12), (3, 22)] if direction > 0 else [(11, 2), (2, 12), (11, 22)]
    cv.line(pts, (10, 12, 11), 5.0)
    cv.line(pts, (230, 234, 224), 2.8)
    return cv.rgba()


def padlock():
    cv = Canvas(14, 16)
    cv.d.arc([3 * SS, 0.8 * SS, 11 * SS, 10 * SS], 180, 360, fill=Canvas.c((200, 204, 196)), width=int(1.8 * SS))
    cv.line([(3.9, 5.4), (3.9, 7.5)], (200, 204, 196), 1.8)
    cv.line([(10.1, 5.4), (10.1, 7.5)], (200, 204, 196), 1.8)
    cv.rect(1.5, 6.8, 12.5, 15.2, (12, 14, 13), radius=1.5)
    cv.rect(2.3, 7.6, 11.7, 14.4, (196, 160, 80), radius=1.0)
    cv.rect(2.3, 7.6, 11.7, 9.6, (232, 200, 120), radius=1.0)
    cv.rect(6.3, 10.0, 7.7, 13.0, (40, 30, 14))
    return cv.rgba()


def check():
    cv = Canvas(16, 16)
    pts = [(2.5, 8.5), (6.5, 12.5), (13.5, 3.5)]
    cv.line(pts, (10, 12, 11), 4.4)
    cv.line(pts, (150, 230, 120), 2.4)
    return cv.rgba()


def medal(tier):
    """Difficulty a mission was completed on: bronze / silver / gold disc
    with 1-3 stars; tier -1 = empty slot."""
    cv = Canvas(16, 16)
    if tier < 0:
        cv.circle(8, 8, 6.8, (110, 116, 108), a=170, width=1.4)
        return cv.rgba()
    base = [(176, 110, 62), (178, 186, 190), (232, 188, 76)][tier]
    cv.circle(8, 8, 7.6, (12, 14, 13))
    cv.circle(8, 8, 6.6, tuple(v * 0.75 for v in base))
    cv.circle(7.6, 7.4, 5.6, base)
    star = tuple(min(255, v * 0.45) for v in base)
    cv.poly(star_pts(8, 8.3, 4.6), star)
    return cv.rgba()


def plane_icon(color=(226, 230, 220)):
    """Top view of a twin-fin fighter, nose up."""
    cv = Canvas(20, 20)
    body = [(10, 0.6), (11.2, 4), (11.4, 7.5), (18.6, 12.4), (18.6, 13.6), (11.4, 12.6), (11.2, 15.6),
            (14.2, 18.2), (14.2, 19.2), (10, 18.4), (5.8, 19.2), (5.8, 18.2), (8.8, 15.6), (8.6, 12.6),
            (1.4, 13.6), (1.4, 12.4), (8.6, 7.5), (8.8, 4)]
    cv.poly(body, color)
    return cv.rgba()


def heli_icon(color=(226, 230, 220)):
    """Top view of a coaxial attack helicopter: rotor disc, fuselage, tail."""
    cv = Canvas(20, 20)
    cv.circle(10, 8.6, 8.2, color, a=70)
    cv.circle(10, 8.6, 8.2, color, a=200, width=0.8)
    cv.poly([(9, 2.4), (11, 2.4), (11.8, 6), (11.6, 11.5), (10.6, 18), (9.4, 18), (8.4, 11.5), (8.2, 6)], color)
    cv.poly([(6.4, 16.4), (13.6, 16.4), (13.6, 17.6), (6.4, 17.6)], color)
    cv.poly([(5.2, 8.4), (14.8, 8.4), (14.8, 9.6), (5.2, 9.6)], color)
    return cv.rgba()


def line_icon(kind):
    """24x24 white line icons for weather and time of day (tinted by the game)."""
    cv = Canvas(24, 24)
    W = (236, 240, 230)

    def sun(cx, cy, r, rays=True, a=255):
        cv.circle(cx, cy, r, W, a=a)
        if rays:
            for i in range(8):
                ang = i * math.pi / 4
                cv.line([(cx + math.cos(ang) * (r + 2.2), cy + math.sin(ang) * (r + 2.2)),
                         (cx + math.cos(ang) * (r + 4.6), cy + math.sin(ang) * (r + 4.6))], W, 1.6, a)

    def cloud(cx, cy, s=1.0, a=255, fill=(236, 240, 230)):
        for dx, dy, r in ((-4.6, 1.4, 3.6), (0, -1.2, 5.0), (4.8, 1.0, 3.8)):
            cv.circle(cx + dx * s, cy + dy * s, r * s, fill, a=a)
        cv.rect(cx - 8.2 * s, cy + 1.0 * s, cx + 8.6 * s, cy + 4.8 * s, fill, a=a, radius=1.5 * s)

    if kind == "clear":
        sun(12, 12, 4.4)
    elif kind == "cloudy":
        sun(15.5, 8, 3.4)
        cloud(10.5, 14.5, 0.85, fill=(10, 12, 11))
        cloud(10.5, 14.5, 0.75)
    elif kind == "overcast":
        cloud(14.5, 9.5, 0.7, a=150)
        cloud(10.5, 14.5, 0.85, fill=(10, 12, 11))
        cloud(10.5, 14.5, 0.75)
    elif kind == "fog":
        for i, y in enumerate((7, 11, 15, 19)):
            cv.line([(3 + (i % 2) * 2, y), (21 - ((i + 1) % 2) * 2, y)], W, 1.8)
    elif kind == "rain":
        cloud(12, 9, 0.8)
        for x in (7.5, 12, 16.5):
            cv.line([(x, 15.5), (x - 1.8, 21)], W, 1.6)
    elif kind == "snow":
        cloud(12, 9, 0.8)
        for x, y in ((7.5, 17), (12, 20), (16.5, 17)):
            cv.circle(x, y, 1.5, W)
    elif kind == "dawn":
        cv.line([(2, 17.5), (22, 17.5)], W, 1.6)
        cv.d.pieslice([(12 - 5.2) * SS, (17.5 - 5.2) * SS, (12 + 5.2) * SS, (17.5 + 5.2) * SS], 180, 360,
                      fill=Canvas.c(W))
        cv.line([(12, 3.5), (12, 9)], W, 1.6)
        cv.poly([(12, 2.5), (9.5, 5.5), (14.5, 5.5)], W)
    elif kind == "day":
        sun(12, 12, 4.4)
        cv.line([(3, 21.5), (21, 21.5)], W, 1.2, 150)
    elif kind == "dusk":
        cv.line([(2, 17.5), (22, 17.5)], W, 1.6)
        cv.d.pieslice([(12 - 5.2) * SS, (17.5 - 5.2) * SS, (12 + 5.2) * SS, (17.5 + 5.2) * SS], 180, 360,
                      fill=Canvas.c(W))
        cv.line([(12, 3), (12, 8.5)], W, 1.6)
        cv.poly([(12, 9.5), (9.5, 6.5), (14.5, 6.5)], W)
    elif kind == "night":
        cv.circle(11, 12, 6.8, W)
        cv.circle(14.6, 9.6, 6.0, (0, 0, 0), a=0)
        cv.d.ellipse([(14.4 - 6.0) * SS, (9.6 - 6.0) * SS, (14.4 + 6.0) * SS, (9.6 + 6.0) * SS], fill=(0, 0, 0, 0))
        for x, y in ((18.5, 15.5), (19.5, 5), (6, 4.5)):
            cv.poly(star_pts(x, y, 1.6, 0.45, 4), W)
    elif kind == "enemy_air":
        body = [(12, 21.5), (13.4, 17.5), (13.6, 13.5), (21.5, 8.5), (21.5, 7.2), (13.6, 8.8), (13.2, 5.2),
                (16.5, 2.6), (16.5, 1.6), (12, 2.4), (7.5, 1.6), (7.5, 2.6), (10.8, 5.2), (10.4, 8.8),
                (2.5, 7.2), (2.5, 8.5), (10.4, 13.5), (10.6, 17.5)]
        cv.poly(body, W)
    elif kind == "enemy_ground":
        cv.circle(12, 12, 8.4, W, width=1.6)
        cv.line([(12, 1.5), (12, 6.5)], W, 1.6)
        cv.line([(12, 17.5), (12, 22.5)], W, 1.6)
        cv.line([(1.5, 12), (6.5, 12)], W, 1.6)
        cv.line([(17.5, 12), (22.5, 12)], W, 1.6)
        cv.rect(8, 11, 16, 15, W, radius=1)
        cv.rect(10, 9, 14.5, 11.5, W, radius=1)
    elif kind == "lan":
        # Two handhelds side by side, the ad-hoc link between them.
        for x in (5.5, 18.5):
            cv.rect(x - 5, 15, x + 5, 21.5, W, radius=2.5)
            cv.rect(x - 2.6, 16.3, x + 2.6, 20.2, (40, 44, 42))
        for r in (2.6, 5.4, 8.2):
            cv.d.arc([(12 - r) * SS, (11 - r) * SS, (12 + r) * SS, (11 + r) * SS], 225, 315,
                     fill=Canvas.c(W), width=int(1.5 * SS))
        cv.circle(12, 11, 1.4, W)
    elif kind == "internet":
        cv.circle(12, 12, 9, W, width=1.5)
        cv.d.ellipse([(12 - 3.8) * SS, 3 * SS, (12 + 3.8) * SS, 21 * SS], outline=Canvas.c(W), width=int(1.3 * SS))
        cv.line([(3, 12), (21, 12)], W, 1.3)
        cv.line([(4.8, 7.5), (19.2, 7.5)], W, 1.1)
        cv.line([(4.8, 16.5), (19.2, 16.5)], W, 1.1)
    return cv.rgba()


def locked_medallion():
    """48x48 roundel with a padlock: a locked campaign location / slot."""
    from PIL import Image
    cv = Canvas(48, 48)
    cv.circle(24, 24, 23.6, (10, 12, 11))
    cv.circle(24, 24, 22.4, (150, 158, 150))
    cv.circle(24, 24.6, 21.2, (96, 104, 98))
    cv.circle(24, 24, 18.6, (16, 18, 17))
    cv.circle(24, 24, 17.6, (40, 44, 42))
    lk = Image.fromarray((padlock() * 255).astype(np.uint8), "RGBA").resize((28, 32), Image.LANCZOS)
    base = Image.fromarray((np.clip(cv.rgba(), 0, 1) * 255).astype(np.uint8), "RGBA")
    base.alpha_composite(lk, (10, 8))
    return np.asarray(base, np.float64) / 255


# ------------------------------------------------------------------ rank plates
# Metal plates with the rank's insignia (author's pick "B" of three, 2026-10-14):
# light-blue-less Russian officer marks - stripes as shallow engraved grooves,
# stars raised above them - on steel (junior officers), brass (senior), gold
# (general). Order = meta::rankName.

RANKS = [  # (stripes, stars, big stars)
    (1, 2, False),   # LIEUTENANT
    (1, 3, False),   # SENIOR LIEUTENANT
    (1, 4, False),   # CAPTAIN
    (2, 1, True),    # MAJOR
    (2, 2, True),    # LIEUTENANT COLONEL
    (2, 3, True),    # COLONEL
    (0, 1, True),    # MAJOR GENERAL (zigzag braid)
]
FINISH.update({
    "steel": ((200, 206, 200), (116, 122, 116), 0.9),
    "brass": ((214, 178, 104), (120, 88, 40), 0.9),
    "gold": ((240, 206, 112), (150, 108, 34), 1.0),
})


def coverage(draw_fn, w, h):
    """Anti-aliased mask of whatever draw_fn(ImageDraw) paints at SS x."""
    from PIL import Image, ImageDraw
    im = Image.new("L", (w * SS, h * SS), 0)
    draw_fn(ImageDraw.Draw(im))
    return np.asarray(im, float).reshape(h, SS, w, SS).mean((1, 3)) / 255


def emboss(plate, m, strength=1.0):
    """Raised metal: a slightly lighter face, light rim up-left, shadow down-right."""
    rgb = plate[..., :3]
    shifted = np.zeros_like(m)
    shifted[1:, 1:] = m[:-1, :-1]
    rim = np.clip(m - shifted, 0, 1)
    drop = np.clip(shifted - m, 0, 1)
    rgb[:] = rgb * (1 - 0.55 * drop[..., None] * strength)
    rgb[:] = rgb * (1 + 0.10 * m[..., None])
    rgb[:] = rgb + (1 - rgb) * (0.55 * rim[..., None] * strength)
    plate[..., :3] = np.clip(rgb, 0, 1)


def groove(plate, m):
    """A shallow cut: darker inside, a light lip along its lower edge."""
    rgb = plate[..., :3]
    below = np.zeros_like(m)
    below[1:, :] = m[:-1, :]
    lip = np.clip(below - m, 0, 1)
    rgb[:] = rgb * (1 - 0.45 * m[..., None])
    rgb[:] = rgb + (1 - rgb) * (0.35 * lip[..., None])
    plate[..., :3] = np.clip(rgb, 0, 1)


def screw_heads(plate, pts):
    h, w = plate.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    for cx, cy in pts:
        r = np.hypot(xx - cx, yy - cy)
        head = np.clip(2.1 - r, 0, 1)
        shade = np.clip(0.5 + 0.35 * ((cx - xx) + (cy - yy)), 0, 1)
        col = plate[..., :3] * 0.55 + 0.25 * shade[..., None]
        plate[..., :3] = plate[..., :3] * (1 - head[..., None]) + col * head[..., None]
        slot = (np.abs((xx - cx) - (yy - cy)) < 0.6) & (r < 1.6)
        plate[..., :3] = np.where(slot[..., None], plate[..., :3] * 0.45, plate[..., :3])


def rank_plate(rank):
    stripes, stars, big = RANKS[rank]
    w, h = 70, 24
    x0, x1, cy = 10, w - 10, h / 2
    finish = "steel" if rank < 3 else ("brass" if rank < 6 else "gold")
    p = metal_plate(h, w, finish, 11 + rank, chamfer=3, wear=0.6)

    def lines(d):
        if stripes == 0:  # general: zigzag braid
            pts = [((x0 + i * 3) * SS, (cy + (-2.4 if i % 2 else 2.4)) * SS) for i in range(int((x1 - x0) / 3) + 1)]
            d.line(pts, fill=255, width=int(1.2 * SS))
        for k in range(stripes):
            y = cy if stripes == 1 else (cy - 3.6 if k == 0 else cy + 3.6)
            d.rectangle([x0 * SS, (y - 0.7) * SS, x1 * SS, (y + 0.7) * SS], fill=255)

    def star_marks(d):
        if big:
            span = x1 - x0
            xs = [x0 + span / 2] if stars == 1 else [x0 + span * (i + 1) / (stars + 1) for i in range(stars)]
            for x in xs:
                d.polygon([(px * SS, py * SS) for px, py in star_pts(x, cy, 7.4, 0.44)], fill=255)
        else:
            pos = {2: [(-1, -5.6), (-1, 5.6)], 3: [(-1, -5.6), (-1, 5.6), (9, 0)],
                   4: [(-1, -5.6), (-1, 5.6), (9, 0), (19, 0)]}[stars]
            for dx, dy in pos:
                d.polygon([(px * SS, py * SS) for px, py in star_pts(x0 + 8 + dx, cy + dy, 4.4, 0.44)], fill=255)

    ms, mz = coverage(lines, w, h), coverage(star_marks, w, h)
    groove(p, ms * (1 - mz))
    emboss(p, mz, 1.3)
    screw_heads(p, [(5, 5), (w - 6, 5), (5, h - 6), (w - 6, h - 6)])
    return p


# ------------------------------------------------------------------ emblems
# Pilot emblems (docs/decisions/0039): a roundel with a brushed metal rim, a
# coloured field and a light symbol. Same order as meta::emblem (catalog.cpp);
# the first four are open from the start, the rest come with achievements.

EMBLEM_SIZE = 56
EMBLEMS = [  # (field colour, symbol)
    ((52, 74, 54), "winged_star"), ((50, 66, 92), "chevrons"), ((96, 46, 40), "lightning"),
    ((120, 100, 62), "compass"),
    ((70, 36, 34), "crosshair"), ((44, 58, 52), "target"), ((84, 64, 44), "hawk"), ((24, 26, 30), "spade"),
    ((46, 60, 84), "wings"), ((70, 74, 78), "iron_wings"),
    ((34, 58, 86), "anchor"), ((74, 40, 30), "volcano"), ((128, 70, 40), "mesa"), ((70, 96, 120), "snowflake"),
    ((30, 44, 70), "storm"), ((60, 22, 18), "flame"), ((150, 104, 44), "desert_sun"), ((46, 84, 104), "crystal"),
    ((40, 64, 46), "laurel_star"), ((72, 30, 30), "wreath"), ((36, 38, 62), "eagle"), ((40, 30, 24), "general_star"),
    ((56, 62, 70), "shield"), ((64, 30, 70), "crown"),
    ((42, 56, 64), "shark"), ((34, 44, 74), "daybreak"), ((56, 60, 44), "arrowhead"), ((32, 44, 58), "squadron"),
]
LIGHT = (232, 228, 206)
GOLD = (236, 196, 96)
RED = (214, 64, 52)


def emblem_art(i):
    S = EMBLEM_SIZE
    c = S / 2
    k = S / 48.0  # the symbols were laid out on the 48 px roundel
    cv = Canvas(S, S)
    cv.circle(c, c, c - 0.4, (10, 12, 11))
    cv.circle(c, c, c - 1.6, (150, 158, 150))
    cv.circle(c, c + 0.6, c - 2.8, (96, 104, 98))
    cv.circle(c, c, c - 5.4 * k, (16, 18, 17))
    field, kind = EMBLEMS[i]
    fr = c - 6.4 * k
    cv.circle(c, c, fr, field)
    cv.circle(c, c - 3 * k, fr - 3.6 * k, tuple(min(255, v * 1.18) for v in field), a=120)

    def P(pts):  # 48-grid coordinates -> this canvas
        return [((x - 24) * k + c, (y - 24) * k + c) for x, y in pts]

    def poly(pts, col=LIGHT, a=255):
        cv.poly(P(pts), col, a)

    def circ(x, y, r, col=LIGHT, a=255, width=0):
        cv.circle((x - 24) * k + c, (y - 24) * k + c, r * k, col, a=a, width=width * k if width else 0)

    def line(pts, width, col=LIGHT, a=255):
        cv.line(P(pts), col, width * k, a)

    def star(x, y, r, col=LIGHT, inner=0.42, n=5, rot=-math.pi / 2):
        poly(star_pts(x, y, r, inner, n, rot), col)

    def plane(x, y, sc, col=LIGHT):  # top view, nose up (plane_icon's outline)
        body = [(10, 0.6), (11.2, 4), (11.4, 7.5), (18.6, 12.4), (18.6, 13.6), (11.4, 12.6), (11.2, 15.6),
                (14.2, 18.2), (14.2, 19.2), (10, 18.4), (5.8, 19.2), (5.8, 18.2), (8.8, 15.6), (8.6, 12.6),
                (1.4, 13.6), (1.4, 12.4), (8.6, 7.5), (8.8, 4)]
        poly([(x + (px - 10) * sc, y + (py - 10) * sc) for px, py in body], col)

    def wing(sgn, y0, col=LIGHT):  # a pilot's-wings wing: four feathers
        for j, (dy, ln) in enumerate(((0, 15), (2.6, 13), (5.2, 10.5), (7.8, 8))):
            yy = y0 + dy
            poly([(24 + sgn * 4, yy - 1.4), (24 + sgn * (4 + ln), yy - 3.4 + j * 0.5),
                  (24 + sgn * (4 + ln - 1.6), yy + 0.8), (24 + sgn * 4, yy + 1.2)], col)

    def laurel(sgn, col=(170, 196, 120)):  # a branch up one side of the roundel
        for j in range(6):
            t = j / 5.0
            ang = math.radians(100 + 120 * t) if sgn < 0 else math.radians(80 - 120 * t)
            x, y = 24 + 14 * math.cos(ang), 25 + 14 * math.sin(ang)
            tx, ty = -math.sin(ang) * sgn, math.cos(ang) * sgn   # along the branch, upwards
            for side in (-1, 1):  # a pair of leaves pointing out and in
                nx, ny = math.cos(ang) * side, math.sin(ang) * side
                lx, ly = x + nx * 3.6 - tx * 1.2, y + ny * 3.6 - ty * 1.2
                poly([(x, y), (x + nx * 1.6 + tx * 1.4, y + ny * 1.6 + ty * 1.4), (lx, ly),
                      (x + nx * 1.4 - tx * 1.6, y + ny * 1.4 - ty * 1.6)], col)

    if kind == "winged_star":
        for sgn in (-1, 1):
            for j, (dy, ln) in enumerate(((0, 13.5), (3.2, 11), (6.4, 8.5))):
                y0 = 22 + dy
                poly([(24 + sgn * 5, y0 - 1.6), (24 + sgn * (5 + ln), y0 - 3.6 + j * 0.4),
                      (24 + sgn * (5 + ln - 1.5), y0 + 0.6), (24 + sgn * 5, y0 + 1.4)])
        star(24, 23, 8.4, (16, 18, 17))
        star(24, 23, 7.2, RED)
    elif kind == "chevrons":
        for j in range(3):
            y = 15 + j * 7
            poly([(13, y + 6), (24, y), (35, y + 6), (35, y + 10), (24, y + 4), (13, y + 10)])
    elif kind == "lightning":
        poly([(27.5, 9), (16, 26), (23.2, 26), (19.5, 39), (32.5, 20.5), (25, 20.5), (29.5, 9)])
    elif kind == "compass":
        star(24, 24, 15, LIGHT, 0.18, 4)
        star(24, 24, 9, (200, 186, 140), 0.28, 4, rot=-math.pi / 4)
        circ(24, 24, 2.0, field)
    elif kind == "crosshair":
        circ(24, 24, 11, LIGHT, width=2.2)
        for a in range(4):
            ang = a * math.pi / 2
            line([(24 + 7 * math.cos(ang), 24 + 7 * math.sin(ang)), (24 + 16 * math.cos(ang), 24 + 16 * math.sin(ang))], 2.2)
        circ(24, 24, 2.6, RED)
    elif kind == "target":
        circ(24, 24, 15, LIGHT)
        circ(24, 24, 11.4, field)
        circ(24, 24, 8, LIGHT)
        circ(24, 24, 4.6, RED)
    elif kind == "hawk":  # bird of prey stooping, wings swept back
        poly([(24, 9), (27, 15), (26.5, 26), (24, 38), (21.5, 26), (21, 15)])
        for sgn in (-1, 1):
            poly([(24 + sgn * 2.5, 16), (24 + sgn * 17, 23), (24 + sgn * 15.5, 26), (24 + sgn * 11, 25),
                  (24 + sgn * 12.5, 28.5), (24 + sgn * 8, 27.5), (24 + sgn * 2.5, 24)])
        poly([(24, 34), (28, 40), (24, 38.5), (20, 40)])
        circ(24, 11.5, 1.1, field)
    elif kind == "spade":
        poly([(24, 9), (34, 21), (35.5, 26), (32.5, 30.5), (27.5, 30), (25.5, 28), (27, 37), (21, 37), (22.5, 28),
              (20.5, 30), (15.5, 30.5), (12.5, 26), (14, 21)])
        line([(18, 40), (30, 40)], 1.4, (200, 60, 50))
    elif kind == "wings":
        for sgn in (-1, 1):
            wing(sgn, 21)
        circ(24, 22.5, 5.2, GOLD)
        star(24, 22.5, 3.6, (90, 60, 20))
    elif kind == "iron_wings":
        for sgn in (-1, 1):
            wing(sgn, 21, (200, 206, 210))
        for t in range(8):  # a cog in the middle
            ang = t * math.pi / 4
            poly([(24 + 4.6 * math.cos(ang - 0.25), 22.5 + 4.6 * math.sin(ang - 0.25)),
                  (24 + 7.4 * math.cos(ang - 0.18), 22.5 + 7.4 * math.sin(ang - 0.18)),
                  (24 + 7.4 * math.cos(ang + 0.18), 22.5 + 7.4 * math.sin(ang + 0.18)),
                  (24 + 4.6 * math.cos(ang + 0.25), 22.5 + 4.6 * math.sin(ang + 0.25))], (200, 206, 210))
        circ(24, 22.5, 5.0, (200, 206, 210))
        circ(24, 22.5, 2.2, field)
    elif kind == "anchor":
        circ(24, 11.5, 3.2, LIGHT, width=1.8)
        line([(24, 14.5), (24, 36)], 2.6)
        line([(17, 18.5), (31, 18.5)], 2.2)
        cv.d.arc([((24 - 12) - 24) * k * SS + c * SS, ((24) - 24) * k * SS + c * SS,
                  ((24 + 12) - 24) * k * SS + c * SS, ((24 + 14) - 24) * k * SS + c * SS], 20, 160,
                 fill=Canvas.c(LIGHT), width=int(2.6 * k * SS))
        poly([(11, 29), (14.5, 25.5), (15.5, 31.5)])
        poly([(37, 29), (33.5, 25.5), (32.5, 31.5)])
    elif kind == "volcano":
        for x, y, r in ((27, 12, 3.6), (30.5, 9, 3.0), (25, 8, 2.6)):
            circ(x, y, r, (170, 168, 160))
        poly([(9, 37), (20, 17), (28, 17), (39, 37)], (60, 52, 48))
        poly([(20, 17), (28, 17), (26.5, 20), (21.5, 20)], (240, 140, 50))
        poly([(23.5, 20), (25, 20), (27, 30), (24, 37), (25, 29)], (240, 120, 40))
    elif kind == "mesa":
        circ(30, 17, 7, (246, 180, 80))
        poly([(8, 37), (13, 26), (15, 22), (25, 22), (27, 26), (40, 37)], (90, 40, 26))
        poly([(15, 22), (25, 22), (24.5, 24), (15.5, 24)], (176, 92, 50))
    elif kind == "snowflake":
        for t in range(6):
            ang = t * math.pi / 3 - math.pi / 2
            ex, ey = 24 + 15 * math.cos(ang), 24 + 15 * math.sin(ang)
            line([(24, 24), (ex, ey)], 2.2)
            for f in (0.55, 0.78):
                bx, by = 24 + 15 * f * math.cos(ang), 24 + 15 * f * math.sin(ang)
                for s2 in (-1, 1):
                    ba = ang + s2 * 0.75
                    line([(bx, by), (bx + 4.2 * math.cos(ba), by + 4.2 * math.sin(ba))], 1.8)
    elif kind == "storm":
        for j, y in enumerate((30, 35)):
            pts = [(9 + t * 1.5, y + 2.2 * math.sin(t * 0.55)) for t in range(21)]
            line(pts, 2.2, (150, 196, 230))
        poly([(28, 7), (17, 23), (23.5, 23), (19.5, 34), (31.5, 18), (25, 18), (30, 7)], (250, 226, 120))
    elif kind == "flame":
        poly([(24, 7), (30, 16), (34, 24), (32.5, 33), (27, 39), (21, 39), (15.5, 33), (14, 25), (18, 18), (20, 22),
              (21.5, 14)], (240, 120, 40))
        poly([(24, 18), (28, 26), (28.5, 33), (24.5, 37.5), (20.5, 34), (20.5, 28), (22.5, 25)], (252, 206, 90))
    elif kind == "desert_sun":
        for t in range(12):
            ang = t * math.pi / 6
            poly([(24 + 8.5 * math.cos(ang - 0.17), 24 + 8.5 * math.sin(ang - 0.17)),
                  (24 + 17 * math.cos(ang), 24 + 17 * math.sin(ang)),
                  (24 + 8.5 * math.cos(ang + 0.17), 24 + 8.5 * math.sin(ang + 0.17))], (250, 214, 120))
        circ(24, 24, 8, (252, 196, 90))
    elif kind == "crystal":
        poly([(24, 7), (32, 14), (32, 33), (24, 41), (16, 33), (16, 14)], (200, 232, 246))
        poly([(24, 7), (32, 14), (24, 19), (16, 14)], (240, 250, 255))
        poly([(24, 19), (32, 14), (32, 33), (24, 41)], (150, 196, 222))
    elif kind == "laurel_star":
        laurel(-1)
        laurel(1)
        star(24, 21, 8, GOLD)
    elif kind == "wreath":
        laurel(-1, (206, 184, 110))
        laurel(1, (206, 184, 110))
        star(24, 13.5, 3.6, GOLD)
        line([(19, 37.5), (29, 37.5)], 2.0, RED)
    elif kind == "eagle":
        poly([(24, 13), (26.5, 17), (26, 31), (24, 37), (22, 31), (21.5, 17)])
        for sgn in (-1, 1):
            for j in range(4):
                y = 17 + j * 3.2
                poly([(24 + sgn * 2, y), (24 + sgn * (17 - j * 2.4), y - 5 + j * 1.2), (24 + sgn * (16 - j * 2.4), y - 1.5 + j),
                      (24 + sgn * 2, y + 2.6)])
        circ(24, 12.2, 2.6)
        poly([(26, 11.5), (29, 12.5), (26, 13.5)], GOLD)
        poly([(21, 36), (24, 40), (27, 36)])
    elif kind == "general_star":
        for t in range(16):
            ang = t * math.pi / 8
            r2 = 17 if t % 2 == 0 else 13
            poly([(24 + 6 * math.cos(ang - 0.12), 24 + 6 * math.sin(ang - 0.12)), (24 + r2 * math.cos(ang), 24 + r2 * math.sin(ang)),
                  (24 + 6 * math.cos(ang + 0.12), 24 + 6 * math.sin(ang + 0.12))], (200, 170, 90))
        star(24, 24.5, 11, (60, 40, 14))
        star(24, 24.5, 9.6, GOLD)
    elif kind == "shield":
        poly([(13, 10), (35, 10), (35, 24), (31, 33), (24, 39), (17, 33), (13, 24)], (190, 196, 200))
        poly([(15, 12), (33, 12), (33, 23.5), (29.5, 31.5), (24, 36.5), (18.5, 31.5), (15, 23.5)], (40, 52, 76))
        poly([(15, 19), (24, 26), (33, 19), (33, 23.5), (24, 30.5), (15, 23.5)], RED)
    elif kind == "crown":
        poly([(12, 32), (12, 17), (18, 24), (24, 13), (30, 24), (36, 17), (36, 32)], GOLD)
        poly([(12, 32), (36, 32), (36, 36), (12, 36)], (210, 170, 70))
        for x, col in ((18, RED), (24, (90, 150, 230)), (30, RED)):
            circ(x, 34, 1.5, col)
        for x, y in ((12, 15.5), (24, 11.5), (36, 15.5)):
            circ(x, y, 1.8, GOLD)
    elif kind == "shark":  # classic nose art: the mouth and an eye
        poly([(9, 27), (18, 21), (30, 20), (40, 23), (33, 31), (20, 33)], (210, 50, 44))
        for j in range(6):
            x = 12 + j * 4.6
            poly([(x, 23.6 - 0.5 * j * 0.2), (x + 2.3, 27), (x + 4.6, 23.1 - 0.5 * j * 0.2)], (246, 244, 236))
            poly([(x + 1, 31.6 - 0.4 * j), (x + 3.3, 27.6), (x + 5.6, 31.2 - 0.4 * j)], (246, 244, 236))
        circ(31, 14.5, 3.2, (246, 244, 236))
        circ(31.6, 14.5, 1.6, (10, 10, 10))
    elif kind == "daybreak":  # a half sun on the horizon, short separate rays
        for t in range(7):
            ang = math.pi + (t + 0.5) * math.pi / 7
            poly([(24 + 11.5 * math.cos(ang - 0.07), 30 + 11.5 * math.sin(ang - 0.07)),
                  (24 + 16 * math.cos(ang), 30 + 16 * math.sin(ang)),
                  (24 + 11.5 * math.cos(ang + 0.07), 30 + 11.5 * math.sin(ang + 0.07))], (246, 206, 110))
        cv.d.pieslice([((24 - 9) - 24) * k * SS + c * SS, ((30 - 9) - 24) * k * SS + c * SS,
                       ((24 + 9) - 24) * k * SS + c * SS, ((30 + 9) - 24) * k * SS + c * SS], 180, 360,
                      fill=Canvas.c((250, 196, 90)))
        poly([(8, 30), (40, 30), (40, 32), (8, 32)], (230, 226, 210))
        poly([(13, 35), (35, 35), (35, 36.6), (13, 36.6)], (230, 226, 210), 160)
    elif kind == "arrowhead":
        poly([(24, 8), (38, 36), (24, 30), (10, 36)])
        poly([(24, 16), (31, 31), (24, 27.5), (17, 31)], field)
    elif kind == "squadron":
        plane(24, 15, 0.62)
        plane(15.5, 27, 0.62)
        plane(32.5, 27, 0.62)
    return cv.rgba()


def build_emblems():
    S = EMBLEM_SIZE
    cols = 9
    W, H = 512, 256
    img = np.zeros((H, W, 4))
    rects = []
    for i in range(len(EMBLEMS)):
        x, y = (i % cols) * S, (i // cols) * S
        img[y:y + S, x:x + S] = emblem_art(i)
        rects.append((x, y, S, S))
    assert (len(EMBLEMS) - 1) // cols * S + S <= H
    shown = ftx.export_quantized(os.path.join(OUT, "emblems.ftx"), np.clip(img, 0, 1), max_levels=1)
    write_png(os.path.join(PREVIEW, "ui_emblems_atlas.png"), checker_under(shown))
    print(f"[ui] emblems: {len(rects)} x {S} px, atlas {W}x{H}")
    return rects


def build_icons():
    tiles = []
    for s in ("cross", "circle", "triangle", "square"):
        tiles.append(("Btn" + s.capitalize(), psp_button(s)))
    tiles += [("BtnL", shoulder_button("L")), ("BtnR", shoulder_button("R")),
              ("DpadLR", dpad("lr")), ("DpadUD", dpad("ud")),
              ("ArrowLeft", chevron(-1)), ("ArrowRight", chevron(1)), ("Lock", padlock()), ("Check", check()),
              ("MedalNone", medal(-1)), ("MedalEasy", medal(0)), ("MedalNormal", medal(1)), ("MedalHard", medal(2)),
              ("Plane", plane_icon()), ("Heli", heli_icon())]
    for k in ("clear", "cloudy", "overcast", "fog", "rain", "snow", "dawn", "day", "dusk", "night",
              "enemy_air", "enemy_ground", "lan", "internet"):
        tiles.append(("".join(p.capitalize() for p in k.split("_")), line_icon(k)))
    tiles.append(("AvatarLocked", locked_medallion()))
    for r in range(len(RANKS)):
        tiles.append((f"Rank{r}", rank_plate(r)))

    img = np.zeros((ICON_H, ICON_W, 4))
    packer = Packer(ICON_W, ICON_H, pad=2)
    rects = {}
    for name, t in sorted(tiles, key=lambda t: -t[1].shape[0]):
        h, w = t.shape[:2]
        x, y = packer.place(w, h)
        img[y:y + h, x:x + w] = t
        rects[name] = (x, y, w, h)
    shown = ftx.export_quantized(os.path.join(OUT, "icons.ftx"), np.clip(img, 0, 1), max_levels=1)
    write_png(os.path.join(PREVIEW, "ui_icons_atlas.png"), checker_under(shown))
    print(f"[ui] icons: {len(rects)} icons, atlas {ICON_W}x{ICON_H}, used to row {packer.y + packer.row}")
    return rects


# ================================================================== header

def write_header(skin, icons, faces, emblems, hud_faces, plates):
    lines = [
        "// GENERATED by tools/textures/ui.py - do not edit. Atlas layout of",
        "// assets/ui/skin.ftx, icons.ftx and the faces of font.fnt (docs/decisions/0035).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace ui {",
        "",
        "struct AtlasRect {",
        "    uint16_t x, y, w, h;",
        "};",
        "",
        "namespace skin {",
    ]
    for name, (x, y, w, h) in skin.items():
        lines.append(f"constexpr AtlasRect k{name}{{{x}, {y}, {w}, {h}}};")
    lines += ["}  // namespace skin", "", "namespace icons {"]
    for name, (x, y, w, h) in sorted(icons.items()):
        lines.append(f"constexpr AtlasRect k{name}{{{x}, {y}, {w}, {h}}};")
    lines.append(f"constexpr int kRankCount = {len(RANKS)};")
    lines.append("constexpr const AtlasRect* kRanks[] = {" + ", ".join(f"&kRank{i}" for i in range(len(RANKS))) + "};")
    lines += ["}  // namespace icons", "", "// Pilot emblems, assets/ui/emblems.ftx: same order as meta::emblem.",
              "namespace emblems {", f"constexpr int kSize = {EMBLEM_SIZE};", f"constexpr int kCount = {len(emblems)};",
              "constexpr AtlasRect kRects[kCount] = {" + ", ".join(f"{{{x}, {y}, {w}, {h}}}" for x, y, w, h in emblems) + "};",
              "}  // namespace emblems", "", "// Faces of assets/ui/font.fnt, in file order.",
              "enum class Face : uint8_t {"]
    for f in faces:
        lines.append(f"    {f['name']},  // {f['size']} px")
    lines += ["    Count", "};", "", "// Flight HUD (docs/decisions/0049): faces of assets/ui/hud_font.fnt (outlined,",
              "// tabular digits) and the warning plates of assets/ui/hud_plates.ftx.",
              "enum class HudFace : uint8_t {"]
    for f in hud_faces:
        lines.append(f"    {f['name']},  // {f['size']} px")
    lines += ["    Count", "};", "", "namespace plates {"]
    for name, (x, y, w, h) in plates.items():
        lines.append(f"constexpr AtlasRect k{name}{{{x}, {y}, {w}, {h}}};")
    lines += ["}  // namespace plates", "", "}  // namespace ui", ""]
    os.makedirs(os.path.dirname(HEADER), exist_ok=True)
    with open(HEADER, "w") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    os.makedirs(PREVIEW, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    faces = build_font()
    skin = build_skin()
    icons = build_icons()
    emblems = build_emblems()
    hud_faces = build_hud_font()
    plates = build_hud_plates()
    write_header(skin, icons, faces, emblems, hud_faces, plates)
    print(f"[ui] header -> {os.path.relpath(HEADER, ROOT)}")
