"""Effect textures: glow, smoke, afterburner flame, vapor, ring, glint,
aircraft shadow, HUD stroke and trail profiles, ocean wave highlights (numpy only).

Usage (from the project root):  python3 tools/textures/fx.py
Writes assets/textures/fx_*.ftx and previews to build/previews/fx_*.png.
How they are used: docs/decisions/0016-effects.md, 0018-hud.md, 0019-lighting.md.

Most are "intensity" textures: the CLUT is white (or a colour ramp) with the
alpha channel carrying the shape, so one texture serves many tints through
the vertex colour (GU_TFX_MODULATE).
"""

import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "pipeline"))
sys.path.insert(0, HERE)

import ftx  # noqa: E402
from env import periodic_fbm, smoothstep  # noqa: E402
from png import write_png  # noqa: E402

OUT = os.path.join(ROOT, "assets", "textures")
PREVIEW = os.path.join(ROOT, "build", "previews")


def grid(w, h):
    """Texel centres in -1..1."""
    y, x = (np.mgrid[0:h, 0:w] + 0.5)
    return x / w * 2 - 1, y / h * 2 - 1


def mips(img, levels):
    out, cur = [img], img
    for _ in range(1, ftx.mip_levels(img.shape[1], img.shape[0], levels)):
        cur = ftx.downsample(cur[..., None])[..., 0]
        out.append(cur)
    return out


def write_alpha(name, alpha, color=(1.0, 1.0, 1.0), levels=4):
    """CLUT index = alpha level, CLUT colour constant: shape-only texture."""
    lv = [np.clip(np.round(a * 255), 0, 255).astype(np.uint8) for a in mips(alpha, levels)]
    t = np.arange(256) / 255
    clut = np.concatenate([np.tile(np.array(color, float), (256, 1)), t[:, None]], 1)
    ftx.export_indexed(os.path.join(OUT, f"fx_{name}.ftx"), lv, clut)
    write_png(os.path.join(PREVIEW, f"fx_{name}.png"), clut[lv[0]][..., :3] * clut[lv[0]][..., 3:])


def write_ramp(name, value, ramp, levels=4):
    """CLUT index = value; colour and alpha from `ramp(t)` -> (r, g, b, a)."""
    lv = [np.clip(np.round(a * 255), 0, 255).astype(np.uint8) for a in mips(value, levels)]
    clut = np.array([ramp(i / 255) for i in range(256)], float)
    ftx.export_indexed(os.path.join(OUT, f"fx_{name}.ftx"), lv, clut)
    write_png(os.path.join(PREVIEW, f"fx_{name}.png"), clut[lv[0]][..., :3] * clut[lv[0]][..., 3:])


def glow():
    """Soft radial glow: hot core + wide falloff (flares, nozzles, diamonds, lens flare)."""
    x, y = grid(64, 64)
    r = np.sqrt(x * x + y * y)
    g = 0.65 * np.exp(-r * r * 7.0) + 0.35 * np.exp(-r * r * 40.0)
    write_alpha("glow", np.clip(g / g.max(), 0, 1) * smoothstep(1.0, 0.8, r))


def smoke():
    """Puff for smoke/spray/vapor: alpha blob with billowy noise, lit from above.
    CLUT = 16 brightness x 16 alpha (as the cloud), so edges stay smooth."""
    size = 64
    x, y = grid(size, size)
    detail = periodic_fbm(size, 4, 4, seed=21)
    r = np.sqrt(x * x + y * y) + 0.30 * (detail - 0.5)
    alpha = smoothstep(0.95, 0.30, r) * 0.95
    light = np.clip(0.72 + 0.28 * (-y * 0.6 + 0.2) + 0.25 * (detail - 0.5), 0, 1)
    lv, lum, a = [], light, alpha
    for i in range(ftx.mip_levels(size, size, 4)):
        if i:
            lum, a = ftx.downsample(lum[..., None])[..., 0], ftx.downsample(a[..., None])[..., 0]
        lq = np.clip(np.round(lum * 15), 0, 15).astype(np.uint8)
        aq = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
        lv.append(lq * 16 + aq)
    shadow, white = np.array([0.70, 0.73, 0.78]), np.array([1.0, 1.0, 1.0])
    clut = np.array([np.append(shadow + (white - shadow) * (l / 15), al / 15) for l in range(16) for al in range(16)])
    ftx.export_indexed(os.path.join(OUT, "fx_smoke.ftx"), lv, clut)
    write_png(os.path.join(PREVIEW, "fx_smoke.png"), clut[lv[0]][..., :3] * clut[lv[0]][..., 3:])


def flame():
    """Afterburner jet, u around the cone, v along it (0 = nozzle exit):
    bright core fading to the tip, with shock diamonds as bright bands."""
    w, h = 32, 64
    v = (np.arange(h) + 0.5) / h
    u = (np.arange(w) + 0.5) / w
    vv, uu = np.meshgrid(v, u, indexing="ij")
    streaks = periodic_fbm(w, 4, 2, seed=8)[0][None, :] * np.ones((h, 1))
    body = (1.0 - vv) ** 1.3
    diamonds = np.clip(np.cos(vv * math.pi * 2 * 5.5) * 0.5 + 0.5, 0, 1) ** 6 * (1 - vv) ** 0.7
    val = np.clip(0.75 * body * (0.8 + 0.4 * streaks) + 0.45 * diamonds, 0, 1)
    val *= smoothstep(1.0, 0.75, vv) * smoothstep(0.0, 0.04, vv)

    def ramp(t):
        # Deep orange -> pink-orange -> yellow -> white; alpha follows intensity.
        stops = [(0.0, (0.55, 0.12, 0.05)), (0.35, (1.0, 0.38, 0.16)), (0.7, (1.0, 0.78, 0.45)),
                 (1.0, (1.0, 0.97, 0.9))]
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1:
                k = (t - t0) / (t1 - t0)
                c = [a + (b - a) * k for a, b in zip(c0, c1)]
                return (*c, t)
        return (*stops[-1][1], 1.0)

    write_ramp("flame", val, ramp, levels=3)


def vapor():
    """Transonic condensation, u across the flow, v along it. Only texture
    (streaks along the flow + mottling): the density profile with its sharp
    edge at the shock is in the vertex alpha (docs/decisions/0020)."""
    w, h = 64, 32
    streak = periodic_fbm(w, 8, 3, seed=33)[0][None, :]   # varies across the flow only
    n = periodic_fbm(w, 8, 3, seed=31)[:h]                # w >= h: crop the square noise
    alpha = np.clip(0.86 + 0.45 * (streak - 0.5) + 0.3 * (n - 0.5), 0.45, 1.0) * np.ones((h, 1))
    write_alpha("vapor", alpha, levels=3)


def ring():
    """Soft annulus: shock ring of the sonic boom, lens-flare halo."""
    x, y = grid(64, 64)
    r = np.sqrt(x * x + y * y)
    a = np.exp(-((r - 0.78) / 0.09) ** 2) + 0.25 * np.exp(-((r - 0.6) / 0.2) ** 2)
    write_alpha("ring", np.clip(a, 0, 1) * smoothstep(1.0, 0.92, r))


def glint():
    """Sun glitter on the sea: sparkles inside a soft elongated falloff."""
    size = 64
    x, y = grid(size, size)
    rng = np.random.default_rng(5)
    sparkle = (rng.random((size, size)) ** 18)
    sparkle = np.maximum(sparkle, np.roll(sparkle, 1, 1) * 0.6)
    env = np.exp(-(x * x * 3.0 + y * y * 1.4))
    a = np.clip(env * (0.28 + 2.4 * sparkle), 0, 1) * smoothstep(1.0, 0.85, np.maximum(abs(x), abs(y)))
    write_alpha("glint", a, color=(1.0, 0.95, 0.85))


def shadow():
    """Top-view silhouette of a Flanker, blurred: the shadow on the sea.
    u = span (x), v = length (nose at v = 0). Planform from docs/assets/su35s.md."""
    size = 64
    x, y = grid(size, size)
    X = x * 11.0                       # m, half span 11 m across the texture
    Y = y * 11.0                       # m, nose at -11
    poly_r = [(0.0, -11.0), (0.55, -8.0), (0.85, -4.5), (2.15, -0.5), (7.05, 4.0), (7.05, 5.6),
              (2.3, 4.4), (2.3, 6.4), (4.94, 9.1), (4.94, 10.1), (1.2, 10.0), (0.3, 10.9), (0.0, 10.9)]
    poly = poly_r + [(-px, py) for px, py in reversed(poly_r)]
    inside = np.zeros(X.shape, bool)
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        cond = (y1 > Y) != (y2 > Y)
        xint = (x2 - x1) * (Y - y1) / ((y2 - y1) if y2 != y1 else 1e-9) + x1
        inside ^= cond & (X < xint)
    a = inside.astype(float)
    for _ in range(2):                 # soft edge
        p = np.pad(a, 1, mode="edge")
        a = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] + 4 * p[1:-1, 1:-1]) / 8
    write_alpha("shadow", a, color=(0.0, 0.0, 0.0))


def line():
    """HUD stroke profile across u (constant along v): a bright core with a dark
    halo, so one alpha-blended pass stays readable over bright sky and dark sea.
    16 columns, CLUT entry = column."""
    cols = 16
    d = np.abs((np.arange(cols) + 0.5) / (cols / 2) - 1.0)
    rgb = np.where(d < 0.30, 1.0, np.clip(1.0 - (d - 0.30) / 0.15, 0.0, 1.0))
    alpha = np.where(d < 0.30, 1.0, np.where(d < 0.45, 1.0 - (d - 0.30) / 0.15 * 0.4,
                                             np.clip(0.6 * (0.95 - d) / 0.5, 0.0, 0.6)))
    clut = np.zeros((256, 4))
    clut[:cols, :3] = rgb[:, None]
    clut[:cols, 3] = alpha
    level = np.tile(np.arange(cols, dtype=np.uint8), (16, 1))
    ftx.export_indexed(os.path.join(OUT, "fx_line.ftx"), [level], clut)
    write_png(os.path.join(PREVIEW, "fx_line.png"), np.repeat(np.repeat(clut[level][..., :3] * clut[level][..., 3:],
                                                                         8, 0), 8, 1))


def ribbon():
    """Soft profile across u for trails (vortices, contrails, flare smoke)."""
    x, _ = grid(16, 16)
    write_alpha("ribbon", np.clip(smoothstep(1.0, 0.2, np.abs(x)), 0, 1), levels=1)


def waves():
    """Moving wave highlights for the second ocean pass (additive)."""
    size = 128
    n = periodic_fbm(size, 8, 4, seed=41)
    m = periodic_fbm(size, 16, 3, seed=43)
    crest = smoothstep(0.62, 0.80, n * 0.7 + m * 0.4)
    write_alpha("waves", np.clip(crest * 0.9, 0, 1), color=(0.85, 0.92, 1.0))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    for make in (glow, smoke, flame, vapor, ring, glint, shadow, line, ribbon, waves):
        make()
    for name in ("glow", "smoke", "flame", "vapor", "ring", "glint", "shadow", "line", "ribbon", "waves"):
        print(f"[fx] fx_{name}.ftx {os.path.getsize(os.path.join(OUT, f'fx_{name}.ftx'))} bytes")
