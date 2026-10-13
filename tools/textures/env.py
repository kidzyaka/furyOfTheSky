"""Environment textures: tileable ocean, cloud puff, sun glow (numpy only).

Usage (from the project root):  python3 tools/textures/env.py
Writes assets/textures/*.ftx and previews to build/previews/env_*.png.
See docs/decisions/0011-world-rendering.md for how they are used.
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

OUT = os.path.join(ROOT, "assets", "textures")
PREVIEW = os.path.join(ROOT, "build", "previews")


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def periodic_noise(size, cells, seed):
    """Value noise that tiles seamlessly: lattice indices wrap modulo `cells`."""
    rng = np.random.default_rng(seed)
    lattice = rng.random((cells, cells))
    coord = np.arange(size) * cells / size
    i0 = np.floor(coord).astype(int)
    f = coord - i0
    f = f * f * (3 - 2 * f)
    i1 = (i0 + 1) % cells
    a = lattice[np.ix_(i0, i0)]
    b = lattice[np.ix_(i0, i1)]
    c = lattice[np.ix_(i1, i0)]
    d = lattice[np.ix_(i1, i1)]
    fx, fy = f[None, :], f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def periodic_fbm(size, base_cells, octaves, seed):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * periodic_noise(size, base_cells * 2 ** o, seed + o * 31)
        norm += amp
        amp *= 0.5
    return total / norm


def ocean():
    """256x256 tile covering 64 m of sea: swell + chop + foam flecks."""
    size = 256
    n = periodic_fbm(size, 4, 5, seed=3)
    y, x = np.mgrid[0:size, 0:size] / size
    # Integer wave numbers keep the sines periodic over the tile.
    swell = (np.sin(2 * math.pi * (3 * x + 2 * y) + 4.0 * n) +
             0.6 * np.sin(2 * math.pi * (-2 * x + 5 * y) + 3.0 * n)) * 0.25 + 0.5
    chop = periodic_fbm(size, 16, 3, seed=11)
    h = 0.55 * swell + 0.45 * chop
    deep = np.array([16, 50, 82]) / 255
    mid = np.array([30, 80, 114]) / 255
    crest = np.array([96, 140, 168]) / 255
    img = deep + (mid - deep) * smoothstep(0.25, 0.65, h)[..., None]
    img = img + (crest - img) * smoothstep(0.68, 0.9, h)[..., None]
    foam = smoothstep(0.80, 0.93, chop * 0.6 + swell * 0.5) * 0.35
    img = img + (np.array([0.85, 0.9, 0.92]) - img) * foam[..., None]
    shown = ftx.export_quantized(os.path.join(OUT, "ocean.ftx"), img, max_levels=4)
    write_png(os.path.join(PREVIEW, "env_ocean.png"), np.tile(shown, (2, 2, 1)))


def cloud():
    """64x64 cumulus puff. CLUT = 16 brightness x 16 alpha levels, so alpha
    edges stay smooth without k-means; light comes from above."""
    size = 64
    y, x = (np.mgrid[0:size, 0:size] + 0.5) / size * 2 - 1
    detail = periodic_fbm(size, 4, 4, seed=5)
    r = np.sqrt(x * x + y * y) + 0.35 * (detail - 0.5)
    alpha = smoothstep(0.98, 0.45, r)
    light = np.clip(0.55 + 0.45 * (-y * 0.7 + 0.3) + 0.25 * (detail - 0.5), 0, 1)

    def index(lum, a):
        lq = np.clip(np.round(lum * 15), 0, 15).astype(np.uint8)
        aq = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
        return lq * 16 + aq

    levels, lum, a = [], light, alpha
    for i in range(ftx.mip_levels(size, size, 4)):
        if i:
            lum, a = ftx.downsample(lum[..., None])[..., 0], ftx.downsample(a[..., None])[..., 0]
        levels.append(index(lum, a))
    shadow, white = np.array([0.60, 0.65, 0.74]), np.array([1.0, 1.0, 1.0])
    clut = np.array([np.append(shadow + (white - shadow) * (l / 15), al / 15)
                     for l in range(16) for al in range(16)])
    ftx.export_indexed(os.path.join(OUT, "cloud.ftx"), levels, clut)
    write_png(os.path.join(PREVIEW, "env_cloud.png"), clut[levels[0]])


def sun():
    """64x64 sun glow: CLUT index = intensity, drawn with additive blending."""
    size = 64
    y, x = (np.mgrid[0:size, 0:size] + 0.5) / size * 2 - 1
    r = np.sqrt(x * x + y * y)
    glow = np.clip(np.exp(-r * r * 9.0) * 0.75 + smoothstep(0.16, 0.08, r), 0, 1) * smoothstep(1.0, 0.85, r)
    levels, g = [], glow
    for i in range(ftx.mip_levels(size, size, 4)):
        if i:
            g = ftx.downsample(g[..., None])[..., 0]
        levels.append(np.clip(np.round(g * 255), 0, 255).astype(np.uint8))
    warm, white = np.array([1.0, 0.82, 0.55]), np.array([1.0, 1.0, 0.97])
    t = np.arange(256) / 255
    clut = np.concatenate([warm + (white - warm) * (t ** 2)[:, None], t[:, None]], 1)
    ftx.export_indexed(os.path.join(OUT, "sun.ftx"), levels, clut)
    write_png(os.path.join(PREVIEW, "env_sun.png"), clut[levels[0]])


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    ocean()
    cloud()
    sun()
    for name in ("ocean", "cloud", "sun"):
        print(f"[env] {name}.ftx {os.path.getsize(os.path.join(OUT, name + '.ftx'))} bytes")
