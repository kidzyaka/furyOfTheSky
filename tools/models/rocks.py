"""Rocks and islands of the North Sea map (docs/assets/rocks.md, ADR 0042).

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \\
      --python tools/models/rocks.py [-- --no-preview] [--only name,name]

Two kinds of shape, one representation (a "cylindrical height map"):
  - walls: a radius for each azimuth and height, up to a wall top that may
    vary with azimuth (a sea cliff high on one side, low on another);
  - a cap over the wall top: a polar height field (rings at fractions of the
    edge radius, a height per ring and azimuth).
Stacks (Old Man of Hoy, Stac Lee...) have an even wall top and a small cap.
Massifs (St Kilda, Mykines, Foula) are islands of 1-3 km: a hill terrain
cut off by the sea, so the cliff is as high as the hill where the sea meets
it - the way real sea cliffs are made.

The SAME tables become
  - the mesh: assets/models/rock_<name>.ftr (written as FTR1, then turned
    into FTR2 by ftr2.py - docs/formats/ftr2.md), a tree of chunks at levels
    of detail, skirted against cracks;
  - the textures: a stack's painted skin rock_<name>.ftx; a massif's vertex
    colours over the tiling details terrain_cliff.ftx / terrain_turf.ftx;
  - the collision shape and a grid of column tops for fast line-of-sight
    tests: src/world/rock_shapes.cpp (generated, do not edit).
So what is seen is exactly what is hit.

Coordinates: game axes (x right, y up, z aft); azimuth theta = atan2(z, x).
"""

import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "pipeline"))

import numpy as np  # noqa: E402

import ftx  # noqa: E402

try:
    import bpy  # noqa: E402
    from mathutils import Vector  # noqa: E402
    from mathutils.bvhtree import BVHTree  # noqa: E402
    HAVE_BLENDER = True
except ImportError:  # numpy-only run: no AO, no previews
    HAVE_BLENDER = False

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.join(ROOT, "assets", "models")
PREV = os.path.join(ROOT, "build", "previews")
HEADER_H = os.path.join(ROOT, "src", "world", "rock_shapes.h")
HEADER_CPP = os.path.join(ROOT, "src", "world", "rock_shapes.cpp")

BOTTOM = -14.0      # m: walls continue under the sea, so the waterline is a real intersection
CHUNK = 16          # quads along each side of a chunk, at every level
DETAIL_MEAN = 0.82  # mean of the tiling detail textures: vertex colours are divided by it
TILE_CLIFF = 40.0   # m per repeat of terrain_cliff
TILE_TURF = 48.0    # m per repeat of terrain_turf
PEAK_HEIGHT = 1.4   # massif peaks over the heights in MASSIFS
PEAK_SPREAD = 1.55  # ... over a wider base: gentle slopes
COAST_SCALE = 0.55  # coast (cliff) heights of the tilted plateau
NO_ROCK = -32768    # column-top grid: no rock in the cell / not solid from the bottom

# --- Stacks. Per shape: wall height and ring step (m), sectors, mean outline
# (height fraction, radius m), ellipse (x, z), joint-bounded polygon (sides,
# strength), crag noise amplitude, lean (dx, dz per m of height), cap rings
# (rho, rise m), cap roughness (m), texture size, basalt plinth top (m).
# After real stacks: Old Man of Hoy 137 m, Old Red Sandstone on a basalt
# plinth; Stac Lee 172 m and Stac an Armin 196 m (St Kilda), massive; the
# Drangarnir; skerries. Sized towards the big ones: a fighter hides behind them.
STACKS = [
    dict(name="hoy", height=175.0, step=6.0, sectors=36,
         profile=[(0.0, 48.0), (0.06, 45.0), (0.11, 30.0), (0.55, 26.0), (1.0, 20.5)],
         ellipse=(1.0, 0.88), facets=(6, 0.5), crag=1.0, lean=(0.015, 0.0),
         cap=[(1.0, 0.0), (0.8, 2.0), (0.5, 4.0), (0.2, 5.2), (0.0, 5.6)], cap_rough=1.2,
         tex=(512, 512), plinth=9.0, seed=11),
    dict(name="needle", height=125.0, step=6.0, sectors=24,
         profile=[(0.0, 23.0), (0.15, 17.5), (0.7, 12.5), (1.0, 8.0)],
         ellipse=(1.0, 0.8), facets=(5, 0.45), crag=1.0, lean=(0.05, 0.025),
         cap=[(1.0, 0.0), (0.6, 2.2), (0.0, 4.0)], cap_rough=0.8,
         tex=(256, 256), plinth=0.0, seed=23),
    dict(name="tower", height=185.0, step=7.0, sectors=40,
         profile=[(0.0, 71.0), (0.08, 64.0), (0.6, 53.0), (1.0, 39.0)],
         ellipse=(1.55, 0.85), facets=(7, 0.45), crag=1.1, lean=(0.0, 0.0),
         cap=[(1.0, 0.0), (0.85, 2.5), (0.6, 5.0), (0.3, 6.8), (0.0, 7.5)], cap_rough=1.8,
         tex=(512, 512), plinth=0.0, seed=37),
    dict(name="armin", height=205.0, step=7.0, sectors=48,
         profile=[(0.0, 104.0), (0.08, 96.0), (0.45, 80.0), (0.8, 62.0), (1.0, 44.0)],
         ellipse=(1.45, 0.75), facets=(8, 0.4), crag=1.2, lean=(-0.03, 0.0),
         cap=[(1.0, 0.0), (0.8, 4.0), (0.55, 8.0), (0.3, 11.0), (0.0, 12.0)], cap_rough=3.0,
         tex=(512, 512), plinth=0.0, seed=61),
    dict(name="islet", height=64.0, step=5.0, sectors=48,
         profile=[(0.0, 205.0), (0.12, 192.0), (0.7, 182.0), (1.0, 172.0)],
         ellipse=(1.45, 0.95), facets=(9, 0.3), crag=1.4, lean=(0.0, 0.0), vstretch=3.0,
         cap=[(1.0, 0.0), (0.94, 5.0), (0.85, 10.0), (0.73, 15.0), (0.6, 19.0), (0.46, 22.0), (0.32, 24.5),
              (0.18, 26.0), (0.0, 27.0)], cap_rough=7.0,
         tex=(512, 256), plinth=0.0, seed=41),
    dict(name="skerry", height=8.0, step=2.5, sectors=24,
         profile=[(0.0, 44.0), (0.5, 38.0), (1.0, 28.0)],
         ellipse=(1.3, 0.9), facets=(6, 0.3), crag=1.6, lean=(0.0, 0.0),
         cap=[(1.0, 0.0), (0.65, 2.0), (0.3, 3.4), (0.0, 3.9)], cap_rough=1.2,
         tex=(256, 256), plinth=0.0, seed=53),
]

# --- Massifs: islands that change how a fight goes. Each is a hill terrain
# cut off by the sea: a tilted plateau (azimuth of the high side, coast height
# on the low and the high side, m; half-span along the tilt, m), peaks (x, z,
# height m, radius m) and glens (x, z, depth, radius) on it, rugged by a
# domain-warped ridged multifractal; the cliff is the terrain's height where
# the coast cuts it. Outline: sea-level radius by azimuth (deg, m). After real
# islands (Ordnance Survey / Faroese heights): Hirta, St Kilda - Conachair
# 430 m, the highest sea cliff in Britain, Village Bay in the south-east;
# Boreray 384 m, cliffs all round; Mykines and the Faroese ridges - basalt
# with grass ledges; Foula - the Kame, 376 m, Old Red Sandstone. Sized to the
# map: a few kilometres, so a fight goes round, between and behind them.
MASSIFS = [
    dict(name="conachair", sectors=384, step=9.0, geology="gabbro", seed=101,
         outline=[(0, 1450), (25, 1300), (45, 980), (60, 830), (75, 920), (95, 1150), (130, 1180),
                  (160, 1350), (185, 1550), (215, 1350), (245, 1150), (270, 1120), (300, 1200), (330, 1350)],
         tilt=(270, 55, 260, 1250),
         peaks=[(-150, -820, 430, 760), (-720, -260, 360, 560), (860, -340, 290, 480), (-1240, 40, 358, 520)],
         glens=[(470, 560, 150, 680), (-650, -720, 90, 360)], batter=0.12),
    dict(name="boreray", sectors=256, step=8.0, geology="gabbro", seed=131,
         outline=[(0, 650), (40, 470), (90, 400), (140, 470), (180, 610), (220, 470), (270, 380), (320, 470)],
         tilt=(0, 70, 160, 600),
         peaks=[(-90, 0, 384, 700), (260, 30, 300, 480)],
         glens=[], batter=0.28),
    dict(name="mykines", sectors=384, step=9.0, geology="basalt", seed=151,
         outline=[(0, 1500), (15, 900), (35, 640), (60, 520), (90, 500), (120, 520), (145, 640), (165, 900),
                  (180, 1450), (195, 880), (215, 600), (240, 470), (270, 450), (300, 480), (325, 620), (345, 900)],
         tilt=(270, 60, 320, 480),
         peaks=[(300, -230, 460, 680), (-520, -190, 380, 540), (-1100, -60, 230, 400), (1050, -120, 300, 420)],
         glens=[(-180, 230, 70, 320)], batter=0.08),
    dict(name="foula", sectors=384, step=9.0, geology="sandstone", seed=171,
         outline=[(0, 1000), (40, 1050), (90, 1000), (140, 1100), (180, 1250), (210, 1150), (250, 1000),
                  (290, 950), (330, 1000)],
         tilt=(180, 25, 320, 1050),
         peaks=[(-300, -100, 418, 680), (-960, -260, 376, 460), (200, 300, 300, 470), (-720, 500, 248, 400)],
         glens=[(620, 0, 90, 520)], batter=0.10),
    dict(name="holm", sectors=192, step=6.0, geology="gabbro", seed=191,
         outline=[(0, 360), (60, 275), (120, 265), (180, 350), (240, 250), (300, 270)],
         tilt=(0, 100, 128, 300), peaks=[(-40, 0, 146, 260)], glens=[], batter=0.06),
    dict(name="dun", sectors=192, step=6.0, geology="sandstone", seed=211,
         outline=[(0, 320), (50, 200), (90, 165), (140, 195), (180, 300), (230, 190), (270, 150), (320, 190)],
         tilt=(180, 18, 150, 300), peaks=[(-150, -50, 176, 230)], glens=[], batter=0.10),
    # Soay (St Kilda, 378 m): a steep dome, cliffs nearly all round.
    dict(name="soay", sectors=256, step=8.0, geology="gabbro", seed=231,
         outline=[(0, 560), (50, 450), (100, 420), (150, 500), (200, 560), (250, 460), (300, 430), (330, 480)],
         tilt=(200, 110, 300, 450), peaks=[(-120, -40, 378, 520), (200, 120, 250, 330)], glens=[], batter=0.18),
    # Stroma (Pentland Firth): low and wide, 30-60 m cliffs - cover for a
    # fight down on the water, not above it.
    dict(name="stroma", sectors=256, step=6.0, geology="sandstone", seed=251,
         outline=[(0, 820), (40, 600), (90, 480), (140, 560), (180, 780), (220, 560), (270, 470), (320, 590)],
         tilt=(90, 18, 55, 600), peaks=[(-300, 100, 70, 400)], glens=[], batter=0.05),
    # Eysturoy-like Faroese ridge: long, a 560 m peak, cliffs on the north.
    dict(name="eysturoy", sectors=384, step=9.0, geology="basalt", seed=271,
         outline=[(0, 1600), (12, 1000), (30, 700), (60, 560), (90, 520), (120, 600), (150, 760), (168, 1050),
                  (180, 1500), (192, 1000), (215, 640), (245, 520), (270, 500), (300, 540), (330, 700), (348, 1050)],
         tilt=(270, 70, 300, 450),
         peaks=[(-400, -60, 560, 700), (650, -40, 420, 600), (-1200, 0, 300, 420)],
         glens=[(200, 250, 90, 300)], batter=0.10),
]

GEOLOGY = {
    # Hirta and Boreray: gabbro and granophyre, dark grey weathering brown.
    "gabbro": dict(dark=(0.28, 0.27, 0.25), mid=(0.44, 0.42, 0.38), lite=(0.60, 0.57, 0.51), tuff=None,
                   ledge=0.75, beds=(8.0, 30.0)),
    # The Faroes: stacked basalt flows with red tuff partings and grass ledges.
    "basalt": dict(dark=(0.22, 0.21, 0.20), mid=(0.34, 0.33, 0.30), lite=(0.48, 0.45, 0.40),
                   tuff=(0.46, 0.29, 0.21), ledge=1.0, beds=(10.0, 34.0)),
    # Foula and Hoy: Old Red Sandstone.
    "sandstone": dict(dark=(0.38, 0.24, 0.18), mid=(0.56, 0.36, 0.26), lite=(0.69, 0.51, 0.38), tuff=None,
                      ledge=0.6, beds=(3.0, 12.0)),
}


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def angdiff(a, b):
    return np.angle(np.exp(1j * (a - b)))


# ================================================================ noise

def _hash3(ix, iy, iz, seed):
    h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (seed * 2654435761)
    h = (h ^ (h >> 13)) * 1274126177
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def noise3(x, y, z, seed):
    """Smooth value noise at arbitrary points (arrays), ~0..1."""
    x, y, z = np.asarray(x, float), np.asarray(y, float), np.asarray(z, float)
    ix, iy, iz = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64), np.floor(z).astype(np.int64)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (fx if dx else 1 - fx) * (fy if dy else 1 - fy) * (fz if dz else 1 - fz)
                out = out + w * _hash3(ix + dx, iy + dy, iz + dz, seed)
    return out


def fbm3(x, y, z, seed, octaves=4, ridged=False):
    t, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        f = 2.0 ** o
        n = noise3(x * f, y * f, z * f, seed + 17 * o)
        if ridged:
            n = 1.0 - np.abs(2.0 * n - 1.0)
        t = t + amp * n
        norm += amp
        amp *= 0.5
    return t / norm


def periodic_noise(n, beta, seed, sx=1.0, sy=1.0):
    """Tileable fractal noise (n x n), zero mean, unit deviation: white noise
    shaped to a k^-beta power spectrum by FFT (periodic by construction).
    sy > 1 stretches the features vertically, sx > 1 horizontally."""
    rng = np.random.default_rng(seed)
    f = np.fft.fft2(rng.standard_normal((n, n)))
    fy = np.fft.fftfreq(n)[:, None] * n * sy
    fx = np.fft.fftfreq(n)[None, :] * n * sx
    k = np.sqrt(fx * fx + fy * fy)
    k[0, 0] = 1.0
    out = np.real(np.fft.ifft2(f * k ** (-beta / 2.0)))
    return (out - out.mean()) / max(out.std(), 1e-9)


# ================================================================ stacks

def ring_heights(s):
    n = int(math.ceil((s["height"] - BOTTOM) / s["step"])) + 1
    return BOTTOM + np.arange(n) * (s["height"] - BOTTOM) / (n - 1)


def profile_radius(s, y):
    pts = s["profile"]
    f = np.clip(y / s["height"], 0.0, 1.0)
    return np.interp(f, [p[0] for p in pts], [p[1] for p in pts])


def beds(s, top):
    """Bed boundaries: random thicknesses 3-11 m, each with its own recess."""
    rng = np.random.default_rng(s["seed"] + 5)
    y, out = 1.5, []
    while y < top + 10:
        out.append((y, rng.uniform(0.02, 0.09), rng.uniform(0.5, 1.4)))   # height, depth, recess band
        y += rng.uniform(3.0, 11.0)
    return out


def radius_table(s):
    """radius[ring, sector] in metres, for azimuths theta_i = 2 pi i / sectors.
    Mean outline x joint-bounded polygon x crags (3D fractal noise sampled on
    the surface itself, so features keep their size in metres) x vertical
    joint grooves x bed recesses x the wave-cut notch; blocky terracing."""
    rng = np.random.default_rng(s["seed"])
    ys = ring_heights(s)
    n = s["sectors"]
    sides, strength = s["facets"]
    corners = np.sort((np.arange(sides) + rng.uniform(-0.3, 0.3, sides)) * 2 * math.pi / sides)
    corner_r = rng.uniform(0.88, 1.12, sides)
    joints = [(rng.uniform(0, 2 * math.pi), rng.uniform(0.04, 0.09), rng.uniform(0.02, 0.06))
              for _ in range(rng.integers(4, 8))]
    bed_list = beds(s, ys[-1])
    over = 4                                   # oversample the local outline, resample onto global azimuths
    a = 2 * math.pi * np.arange(n * over) / (n * over)
    k = np.searchsorted(corners, a) % sides
    c0, c1 = corners[k - 1], corners[k]
    span = (c1 - c0) % (2 * math.pi)
    t = ((a - c0) % (2 * math.pi)) / span
    poly = np.cos(span / 2) / np.maximum(np.cos(np.abs(t - 0.5) * span), 1e-3) * (corner_r[k - 1] + (corner_r[k] - corner_r[k - 1]) * t)
    shape = 1.0 + strength * (poly - 1.0)
    out = np.zeros((len(ys), n))
    theta = 2 * math.pi * np.arange(n) / n
    ex, ez = s["ellipse"]
    for j, y in enumerate(ys):
        base = profile_radius(s, y)
        cx, cz = s["lean"][0] * max(y, 0.0), s["lean"][1] * max(y, 0.0)
        px0, pz0 = base * np.cos(a) * ex, base * np.sin(a) * ez
        # Features stretched vertically: buttresses and gullies, not rings.
        vs = s.get("vstretch", 1.0)   # >1: cliff faces that run unbroken from top to bottom
        big = fbm3(px0 / 22.0, np.full_like(a, y / (70.0 * vs)), pz0 / 22.0, s["seed"] + 1, 3) - 0.5
        mid = fbm3(px0 / 9.0, np.full_like(a, y / (26.0 * vs)), pz0 / 9.0, s["seed"] + 4, 3) - 0.5
        crag = fbm3(px0 / 5.0, np.full_like(a, y / (9.0 * vs)), pz0 / 5.0, s["seed"] + 2, 3, ridged=True) - 0.6
        r = base * shape * (1.0 + s["crag"] * (0.26 * big + 0.12 * mid + 0.06 * crag))
        for ang, w, depth in joints:
            d = np.angle(np.exp(1j * (a - ang - 0.03 * math.sin(y * 0.11))))
            r *= 1.0 - 1.6 * depth * np.exp(-(d / (0.6 * w)) ** 2)
        for by, depth, band in bed_list:
            if y > 0:
                # Only some faces of a bed weather back: a partial recess, not a ring.
                side = smoothstep(0.45, 0.8, noise3(np.cos(a) * 1.7 + by, np.sin(a) * 1.7, np.full_like(a, by), s["seed"] + 3))
                r *= 1.0 - depth * side * np.exp(-((y - by - band * 0.5) / band) ** 2)
        r *= 1.0 - 0.14 * math.exp(-((y - 2.5) / 2.2) ** 2)
        r *= 1.0 + 0.12 * float(smoothstep(0.0, -10.0, y))
        r = r + 0.45 * (np.round(r / 1.5) * 1.5 - r)
        px, pz = cx + r * np.cos(a) * ex, cz + r * np.sin(a) * ez
        ang_g = np.arctan2(pz, px) % (2 * math.pi)
        rad_g = np.hypot(px, pz)
        order = np.argsort(ang_g)
        al, rl = ang_g[order], rad_g[order]
        al = np.concatenate([al - 2 * math.pi, al, al + 2 * math.pi])
        rl = np.concatenate([rl, rl, rl])
        out[j] = np.interp(theta, al, rl)
    return np.round(out, 2)


def cap_heights(s, R):
    """Cap ring heights per sector: [k, i] (ring k = 0 at the wall top)."""
    top = ring_heights(s)[-1]
    n = s["sectors"]
    theta = 2 * math.pi * np.arange(n) / n
    out = np.zeros((len(s["cap"]), n))
    edge = R[-1]
    for k, (rho, rise) in enumerate(s["cap"]):
        x, z = edge * rho * np.cos(theta), edge * rho * np.sin(theta)
        bump = (fbm3(x / 25.0, np.zeros(n), z / 25.0, s["seed"] + 9, 3) - 0.5) * 2 * s["cap_rough"]
        h = top + rise + (bump * smoothstep(1.0, 0.85, rho) if k > 0 else 0.0)
        out[k] = h if k == 0 else np.maximum(h, out[k - 1] + 0.3)
    return np.round(out, 2)


def build_stack_mesh(s, R, CY, sector_step=1, ring_step=1, cap_rings=None):
    """Vertices (u, v, nx, ny, nz, x, y, z), triangles, faceted: corners only
    share a normal across edges under 38 deg (rock faces meet at edges, they
    are not clay). Coarser copies: every n-th sector / ring."""
    ys = ring_heights(s)
    rings = list(range(0, len(ys), ring_step))
    if rings[-1] != len(ys) - 1:
        rings.append(len(ys) - 1)
    n = s["sectors"]
    secs = list(range(0, n, sector_step)) + [n]
    caps = list(range(1, len(s["cap"]))) if cap_rings is None else [k for k in cap_rings if k > 0]
    W, H = s["tex"]
    cap_w = 0.25
    cap_h = 0.25 * W / H
    edge_max = float(R[-1].max())
    rows = [("wall", j, 1.0) for j in rings] + [("cap", k, s["cap"][k][0]) for k in caps]
    top = ys[-1]
    pos = np.zeros((len(rows), len(secs), 3))
    uv = np.zeros((len(rows), len(secs), 2))
    for r_, (kind, j, rho) in enumerate(rows):
        for c, i in enumerate(secs):
            th = 2 * math.pi * i / n
            if kind == "wall":
                rad, y = R[j, i % n], ys[j]
                uv[r_, c] = (WALL_U * i / n, (top - y) / (top - BOTTOM))
            else:
                rad, y = R[-1, i % n] * rho, CY[j, i % n]
                x, z = rad * math.cos(th), rad * math.sin(th)
                uv[r_, c] = (WALL_U + cap_w * (0.5 + 0.48 * x / edge_max), cap_h * (0.5 + 0.48 * z / edge_max))
            pos[r_, c] = (rad * math.cos(th), y, rad * math.sin(th))
    rows_n, cols_n = pos.shape[:2]
    P = pos.reshape(-1, 3)
    UV = uv.reshape(-1, 2)
    tris = []
    for r_ in range(rows_n - 1):
        for c in range(cols_n - 1):
            a_ = r_ * cols_n + c
            tris += [(a_, a_ + cols_n, a_ + 1), (a_ + 1, a_ + cols_n, a_ + cols_n + 1)]
    pos_key = np.arange(len(P))
    pos_key.reshape(rows_n, cols_n)[:, -1] = pos_key.reshape(rows_n, cols_n)[:, 0]
    # Orient outwards (GU_CCW, right-handed: gotchas #15) and get face normals.
    faces, fn = [], []
    for t in tris:
        p0, p1, p2 = P[list(t)]
        nrm = np.cross(p1 - p0, p2 - p0)
        if np.linalg.norm(nrm) < 1e-9:
            continue
        centre = (p0 + p1 + p2) / 3
        out = np.array([centre[0], max(centre[1] - top, 0.0) * 4.0, centre[2]])
        if np.dot(nrm, out) < 0:
            t = (t[0], t[2], t[1])
            nrm = -nrm
        faces.append(t)
        fn.append(nrm / np.linalg.norm(nrm))
    fn = np.array(fn)
    around = {}
    for f, t in enumerate(faces):
        for v in t:
            around.setdefault(pos_key[v], []).append(f)
    cos_crease = math.cos(math.radians(38.0))
    verts, index, lookup = [], [], {}
    for f, t in enumerate(faces):
        for v in t:
            near = [g for g in around[pos_key[v]] if np.dot(fn[g], fn[f]) > cos_crease]
            nrm = fn[near].sum(0)
            nrm /= max(np.linalg.norm(nrm), 1e-9)
            key = (v, round(nrm[0], 2), round(nrm[1], 2), round(nrm[2], 2))
            if key not in lookup:
                lookup[key] = len(verts)
                verts.append((UV[v, 0], UV[v, 1], nrm[0], nrm[1], nrm[2], P[v, 0], P[v, 1], P[v, 2]))
            index.append(lookup[key])
    return (np.array(verts, np.float32), np.array(index, np.int64).reshape(-1, 3), pos, rows)


WALL_U = 0.75   # stack skin: walls in u 0..0.75 (seam duplicated), the cap planar in the square to the right


def grid_ao(pos, idx_faces, verts, rays=40, distance=70.0):
    """Ambient occlusion at each grid point of a stack (rows x columns)."""
    rows_n, cols_n = pos.shape[:2]
    if not HAVE_BLENDER:
        return np.ones((rows_n, cols_n))
    tree = BVHTree.FromPolygons([Vector(p) for p in verts[:, 5:8].tolist()], idx_faces.tolist())
    du = np.roll(pos, -1, axis=1) - np.roll(pos, 1, axis=1)
    du[:, 0] = pos[:, 1] - pos[:, -2]
    du[:, -1] = du[:, 0]
    dv = np.zeros_like(pos)
    dv[1:-1] = pos[2:] - pos[:-2]
    dv[0] = pos[1] - pos[0]
    dv[-1] = pos[-1] - pos[-2]
    nrm = np.cross(dv, du)
    flip = np.einsum("ijk,ijk->ij", nrm, np.stack([pos[..., 0], np.zeros_like(pos[..., 0]), pos[..., 2]], -1)) < 0
    nrm[flip] *= -1
    nrm /= np.maximum(np.linalg.norm(nrm, axis=-1, keepdims=True), 1e-9)
    ao = np.ones((rows_n, cols_n))
    dirs_local = hemisphere(rays)
    for r in range(rows_n):
        for c in range(cols_n - 1):
            ao[r, c] = occlusion(tree, pos[r, c], nrm[r, c], dirs_local, distance, 0.4)
        ao[r, -1] = ao[r, 0]
    return ao


def hemisphere(rays, seed=7):
    rng = np.random.default_rng(seed)
    u1, u2 = rng.random(rays), rng.random(rays)
    return np.stack([np.sqrt(u1) * np.cos(2 * math.pi * u2), np.sqrt(u1) * np.sin(2 * math.pi * u2),
                     np.sqrt(1 - u1)], 1)


def occlusion(tree, p, n, local, distance, offset):
    """Share of a cosine hemisphere around n that escapes within distance."""
    t1 = np.cross(n, [0, 1, 0] if abs(n[1]) < 0.9 else [1, 0, 0])
    t1 /= max(np.linalg.norm(t1), 1e-9)
    t2 = np.cross(n, t1)
    dirs = local[:, :1] * t1 + local[:, 1:2] * t2 + local[:, 2:3] * n
    origin = Vector(p + n * offset)
    hits = sum(1 for d in dirs if tree.ray_cast(origin, Vector(d), distance)[0] is not None)
    return 1.0 - hits / len(dirs)


def paint_stack(s, R, CY, ao_grid, rows):
    """Skin in the mesh's UV layout: walls at u 0..0.75 (azimuth), v 0 (top
    ring) .. 1 (bottom); the cap planar in the square at u 0.75..1."""
    W, H = s["tex"]
    ys = ring_heights(s)
    top = ys[-1]
    n = s["sectors"]
    edge_max = float(R[-1].max())
    cap_h = 0.25 * W / H
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    wall = U < WALL_U
    capm = (U >= WALL_U) & (V < cap_h)
    th = np.where(wall, U / WALL_U * 2 * math.pi, 0.0)
    y = np.where(wall, top - V * (top - BOTTOM), top)
    cx = ((U - WALL_U) / 0.25 - 0.5) / 0.48 * edge_max
    cz = (V / cap_h - 0.5) / 0.48 * edge_max
    th = np.where(capm, np.arctan2(cz, cx) % (2 * math.pi), th)
    rho_px = np.hypot(cx, cz)
    si = th / (2 * math.pi) * n
    i0 = np.floor(si).astype(int) % n
    i1 = (i0 + 1) % n
    fi = si - np.floor(si)
    jf = np.clip((y - BOTTOM) / (top - BOTTOM) * (len(ys) - 1), 0, len(ys) - 1.001)
    j0 = jf.astype(int)
    fj = jf - j0
    rad = (R[j0, i0] * (1 - fi) + R[j0, i1] * fi) * (1 - fj) + (R[j0 + 1, i0] * (1 - fi) + R[j0 + 1, i1] * fi) * fj
    edge = R[-1, i0] * (1 - fi) + R[-1, i1] * fi
    rho = np.where(capm, rho_px / np.maximum(edge, 1e-3), 1.0)
    capk = np.clip(1.0 - rho, 0.0, 1.0)
    px, pz = rad * np.cos(th), rad * np.sin(th)
    px = np.where(capm, cx, px)
    pz = np.where(capm, cz, pz)
    seed = s["seed"]

    rows_n, cols_n = ao_grid.shape
    wall_rows = [k for k, rw in enumerate(rows) if rw[0] == "wall"]
    cap_rows = [k for k, rw in enumerate(rows) if rw[0] == "cap"]
    ring_y = np.array([ys[rows[k][1]] for k in wall_rows])
    col = th / (2 * math.pi) * (cols_n - 1)
    c0 = np.floor(col).astype(int).clip(0, cols_n - 2)
    cf = col - c0

    def grid_at(row_f):
        r0 = np.floor(row_f).astype(int).clip(0, rows_n - 2)
        rf = (row_f - r0).clip(0, 1)
        a = ao_grid[r0, c0] * (1 - cf) + ao_grid[r0, c0 + 1] * cf
        b = ao_grid[r0 + 1, c0] * (1 - cf) + ao_grid[r0 + 1, c0 + 1] * cf
        return a * (1 - rf) + b * rf
    row_wall = np.interp(y, ring_y, np.arange(len(wall_rows)))
    cap_rho = np.array([1.0] + [rows[k][2] for k in cap_rows])
    row_cap = np.interp(-rho, -cap_rho, np.arange(len(wall_rows) - 1, len(wall_rows) + len(cap_rows)))
    ao = np.where(capm, grid_at(row_cap), grid_at(row_wall))

    # Rock. Old Red Sandstone: beds of their own shade, finer laminae, stains.
    bed_list = beds(s, top)
    bed_h = np.array([b[0] for b in bed_list])
    k_bed = np.searchsorted(bed_h, y)
    tone = (np.sin(k_bed * 12.9898 + seed) * 43758.5453) % 1.0
    lam = 0.5 + 0.5 * np.sin(y * 2 * math.pi / 0.8 + 4.0 * fbm3(px / 9, y / 3, pz / 9, seed + 20, 2))
    patch = fbm3(px / 18, y / 14, pz / 18, seed + 21, 4)
    grain = fbm3(px / 2.5, y / 2.5, pz / 2.5, seed + 22, 3)
    dark_s = np.array([96, 58, 44]) / 255
    mid_s = np.array([146, 92, 66]) / 255
    lite_s = np.array([180, 132, 98]) / 255
    t = np.clip(0.2 + 0.45 * tone + 0.35 * (patch - 0.5) + 0.12 * (lam - 0.5) + 0.15 * (grain - 0.5), 0, 1)[..., None]
    img = np.where(t < 0.5, dark_s + (mid_s - dark_s) * (t / 0.5), mid_s + (lite_s - mid_s) * ((t - 0.5) / 0.5))
    part = np.zeros_like(y)
    side_n = fbm3(px / 14, y / 40, pz / 14, seed + 32, 2)
    for by, depth, band in bed_list:
        part = np.maximum(part, np.exp(-((y - by - band * 0.4) / (0.25 + band * 0.2)) ** 2) * (0.4 + 4 * depth))
    img *= (1.0 - 0.38 * np.clip(part, 0, 1) * smoothstep(0.35, 0.65, side_n) * wall)[..., None]
    vstreak = fbm3(px / 2.0, y / 35.0, pz / 2.0, seed + 33, 3)
    img *= (0.86 + 0.24 * vstreak * wall + 0.14 * (1 - wall))[..., None]
    if s["plinth"] > 0:
        basalt = np.array([58, 56, 54]) / 255 * (0.8 + 0.4 * grain)[..., None]
        kb = smoothstep(s["plinth"] + 1.2, s["plinth"] - 1.2, y)[..., None] * wall[..., None]
        img = img + (basalt - img) * kb
    crack = fbm3(px / 4.0, y / 22.0, pz / 4.0, seed + 23, 3, ridged=True)
    img *= (1.0 - 0.5 * smoothstep(0.82, 0.95, crack) * wall)[..., None]
    lip = np.zeros_like(y)
    for by, depth, band in bed_list:
        if by > 14.0:
            lip = np.maximum(lip, np.exp(-((y - by) / 0.35) ** 2) * (depth > 0.05))
    birds = smoothstep(0.58, 0.72, fbm3(px / 5, y / 8, pz / 5, seed + 24, 3))
    guano = lip * birds * wall
    drip = np.zeros_like(guano)
    rows_per_m = H / (top - BOTTOM)
    streak = smoothstep(0.5, 0.75, fbm3(px / 1.2, y / 30, pz / 1.2, seed + 25, 2))
    for k in range(1, int(9 * rows_per_m) + 1):
        drip = np.maximum(drip, np.roll(guano, k, axis=0) * (1 - k / (9 * rows_per_m + 1)) ** 1.5)
    drip *= streak * wall
    white = np.array([224, 222, 210]) / 255
    orange = np.array([200, 128, 46]) / 255
    sage = np.array([150, 160, 130]) / 255
    lichen_o = smoothstep(0.55, 0.7, fbm3(px / 3, y / 3, pz / 3, seed + 26, 3)) * smoothstep(1.5, 0.3, np.abs(y - (bed_h[np.clip(k_bed - 1, 0, len(bed_h) - 1)]))) * (y > 12) * wall
    lichen_g = smoothstep(0.6, 0.75, fbm3(px / 5, y / 5, pz / 5, seed + 27, 3)) * smoothstep(8.0, 25.0, y) * wall
    img = img + (sage - img) * (lichen_g * 0.45)[..., None]
    img = img + (orange - img) * (lichen_o * 0.6)[..., None]
    img = img + (white - img) * np.clip(guano * 0.95 + drip * 0.55, 0, 1)[..., None]
    wet = (smoothstep(7.0, 1.0, y) * wall)[..., None]
    img = img * (1.0 - 0.42 * wet)
    weed = np.array([44, 54, 34]) / 255
    img = img + (weed - img) * (smoothstep(1.6, 0.0, y) * (0.55 + 0.45 * grain) * wall)[..., None]
    barn = smoothstep(0.62, 0.74, fbm3(px / 0.8, y / 0.8, pz / 0.8, seed + 28, 2)) * smoothstep(-0.3, 0.6, y) * smoothstep(2.2, 1.3, y)
    img = img + (np.array([170, 164, 150]) / 255 - img) * (barn * 0.55 * wall)[..., None]
    murk = np.array([20, 34, 32]) / 255
    img = img + (murk - img) * (smoothstep(-1.0, -6.0, y) * wall)[..., None]
    turf_n = fbm3(px / 2.5, np.zeros_like(y), pz / 2.5, seed + 29, 4)
    green = np.array([80, 100, 52]) / 255 * (0.86 + 0.28 * turf_n)[..., None]
    dry = np.array([128, 124, 78]) / 255
    green = green + (dry - green) * (0.6 * smoothstep(0.5, 0.8, fbm3(px / 14, np.ones_like(y), pz / 14, seed + 30, 4)))[..., None]
    rock_cap = np.clip(smoothstep(0.12, 0.0, capk) + smoothstep(0.62, 0.72, fbm3(px / 12, np.full_like(y, 2.0), pz / 12, seed + 31, 3)) * 0.7, 0, 1)
    turf = capm * (1.0 - rock_cap)
    if s["name"] == "skerry":
        turf *= 0.0
        img = img * 0.8
    img = img + (green - img) * turf[..., None]
    img = img + (white - img) * (capm * smoothstep(0.2, 0.0, capk) * birds * 0.6)[..., None]
    unused = (~wall) & (~capm)
    img[unused] = img[wall].mean(0)
    grey = img.mean(-1, keepdims=True)
    img = img + (grey - img) * 0.18
    img = img * (0.32 + 0.68 * ao)[..., None]
    return np.clip(img, 0, 1)


def stack_shape(s):
    R = radius_table(s)
    CY = cap_heights(s, R)
    ys = ring_heights(s)
    return dict(name=s["name"], kind="stack", src=s, S=s["sectors"], ys=ys, R=R,
                WT=np.full(s["sectors"], float(ys[-1])), capRho=np.array([c[0] for c in s["cap"]]), CY=CY)


# ================================================================ massifs

def periodic_profile(theta, pts):
    a = np.radians(np.array([p[0] for p in pts], float)) % (2 * math.pi)
    v = np.array([p[1] for p in pts], float)
    o = np.argsort(a)
    a, v = a[o], v[o]
    return np.interp(theta, np.concatenate([a - 2 * math.pi, a, a + 2 * math.pi]), np.tile(v, 3))


def circular_blur(x, sigma):
    n = len(x)
    f = np.fft.fftfreq(n)
    return np.real(np.fft.ifft(np.fft.fft(x) * np.exp(-2.0 * (math.pi * sigma * f) ** 2)))


def massif_outline(m, theta):
    """Sea-level radius by azimuth: the control outline, smoothed, with
    headlands and coves on two scales."""
    seed = m["seed"]
    r = circular_blur(periodic_profile(theta, m["outline"]), 5.0)
    x, z = r * np.cos(theta), r * np.sin(theta)
    n1 = fbm3(x / 280.0, np.full_like(x, 0.5), z / 280.0, seed + 1, 4) - 0.5
    n2 = fbm3(x / 90.0, np.full_like(x, 2.5), z / 90.0, seed + 2, 3) - 0.5
    return r * (1.0 + 0.24 * n1) + 34.0 * n2


def massif_terrain(m, x, z):
    """The island's terrain (m above the sea) at (x, z): the tilted plateau
    and the peaks as a soft maximum, the glens cut in, then relief that grows
    with height - ridges, spurs and gullies (ridged multifractal on warped
    coordinates, so nothing repeats or lines up)."""
    seed = m["seed"]
    x, z = np.asarray(x, float), np.asarray(z, float)
    wx = x + 170.0 * (fbm3(x / 620.0, np.full_like(x, 0.3), z / 620.0, seed + 31, 3) - 0.5) * 2.0
    wz = z + 170.0 * (fbm3(x / 620.0, np.full_like(x, 1.3), z / 620.0, seed + 32, 3) - 0.5) * 2.0
    deg, low, high, span = m["tilt"]
    phi = math.radians(deg)
    s = (wx * math.cos(phi) + wz * math.sin(phi)) / span
    # Higher and gentler (author's feedback): lower coasts, the peaks taller
    # and broader, so the ground climbs from modest sea cliffs to the tops
    # instead of standing as a wall round a plateau.
    low, high = low * COAST_SCALE, high * COAST_SCALE
    t = low + (high - low) * smoothstep(-0.8, 0.95, s)
    acc = t ** 4
    for cx, cz, h, rad in m["peaks"]:
        d = np.hypot(wx - cx, wz - cz) / (rad * PEAK_SPREAD)
        acc = acc + (h * PEAK_HEIGHT * (1.0 - smoothstep(0.0, 1.0, d)) ** 1.6) ** 4
    t = acc ** 0.25
    for cx, cz, depth, rad in m["glens"]:
        t = t - depth * smoothstep(1.0, 0.0, np.hypot(wx - cx, wz - cz) / rad)
    hmax = max([p[2] * PEAK_HEIGHT for p in m["peaks"]] + [high])
    e = np.clip(t / hmax, 0.1, 1.0)
    r1 = fbm3(wx / 330.0, np.full_like(x, 4.5), wz / 330.0, seed + 6, 4, ridged=True) - 0.55
    r2 = fbm3(wx / 110.0, np.full_like(x, 5.5), wz / 110.0, seed + 8, 3, ridged=True) - 0.55
    r3 = fbm3(x / 34.0, np.full_like(x, 6.5), z / 34.0, seed + 7, 3) - 0.5
    t = t + (0.16 * t + 18.0) * r1 + 22.0 * e * r2 + 5.0 * r3
    return np.maximum(t, 12.0)


def massif_beds(m, top):
    lo, hi = GEOLOGY[m["geology"]]["beds"]
    rng = np.random.default_rng(m["seed"] + 5)
    y, out = 4.0, []
    while y < top + 20:
        out.append((y, rng.uniform(1.0, 4.0), rng.uniform(1.5, 4.0), rng.random()))  # height, recess m, band, tone
        y += rng.uniform(lo, hi)
    return out


class WallModel:
    """The cliff face of a massif, radius by azimuth and height: the
    waterline outline leaning back by a batter that varies along the coast;
    above a "knee" the face lays back into a steep grass slope (the slope-
    over-wall profile of Hebridean and Faroese cliffs); beds step back a few
    metres each on some faces (ledges); buttresses and gullies from 3D noise
    on the face; the wave-cut notch at the foot."""

    def __init__(self, m, theta, r0):
        self.m, self.theta, self.r0 = m, theta, r0
        seed = m["seed"]
        rng = np.random.default_rng(seed + 3)
        self.x0, self.z0 = r0 * np.cos(theta), r0 * np.sin(theta)
        self.gullies = [(rng.uniform(0, 2 * math.pi), rng.uniform(12.0, 30.0), rng.uniform(6.0, 20.0))
                        for _ in range(len(theta) // 12)]
        self.bat = m["batter"] * (0.35 + 1.4 * fbm3(self.x0 / 520.0, np.full_like(theta, 0.7), self.z0 / 520.0, seed + 15, 2))
        self.knee = 0.3 + 0.45 * fbm3(self.x0 / 420.0, np.full_like(theta, 1.9), self.z0 / 420.0, seed + 16, 2)
        self.lay = smoothstep(0.35, 0.65, fbm3(self.x0 / 600.0, np.full_like(theta, 2.9), self.z0 / 600.0, seed + 17, 2))

    def radius(self, yc, wt):
        m, x0, z0, theta = self.m, self.x0, self.z0, self.theta
        seed = m["seed"]
        y = np.maximum(yc, 0.0)
        r = self.r0 - self.bat * y
        knee = self.knee * wt
        over = np.maximum(y - knee, 0.0)
        r = r - 0.85 * self.lay * over * smoothstep(0.0, 30.0, over)
        steps = 0.0
        for by, depth, band, _ in massif_beds(m, float(np.max(wt))):
            side = smoothstep(0.4, 0.75, noise3(x0 / 170.0 + by, np.full_like(x0, by * 0.1), z0 / 170.0, seed + 14))
            steps = steps + depth * side * smoothstep(by - 1.5, by + 1.5, yc)
        r = r - steps
        big = fbm3(x0 / 110.0, yc / 120.0, z0 / 110.0, seed + 11, 3) - 0.5
        mid = fbm3(x0 / 40.0, yc / 45.0, z0 / 40.0, seed + 12, 3) - 0.5
        crag = fbm3(x0 / 16.0, yc / 18.0, z0 / 16.0, seed + 13, 3, ridged=True) - 0.6
        r = r + 36.0 * big + 13.0 * mid + 5.0 * crag
        for ang, w, depth in self.gullies:
            d = angdiff(theta, ang + 0.0006 * yc) * self.r0
            r = r - depth * np.exp(-(d / w) ** 2) * smoothstep(-5.0, 25.0, yc)
        r = r - 4.0 * np.exp(-((yc - 2.5) / 2.4) ** 2)
        r = r + 6.0 * smoothstep(0.0, -10.0, yc)
        return np.maximum(r, 20.0)


def massif_walls(wm, WT, ys):
    """radius[ring, sector]. Rings above a sector's wall top collapse onto it
    (same radius, y clamped) - the mesh drops them."""
    R = np.zeros((len(ys), len(WT)))
    for j, y in enumerate(ys):
        R[j] = wm.radius(np.minimum(y, WT), WT)
    return np.round(R, 1)


def massif_shape(m):
    S = m["sectors"]
    theta = 2 * math.pi * np.arange(S) / S
    r0 = massif_outline(m, theta)
    wm = WallModel(m, theta, r0)
    # The cliff top lies inward of the waterline (batter, lay-back): the wall
    # top is the terrain's height there - a few fixed-point passes.
    WT = massif_terrain(m, r0 * np.cos(theta), r0 * np.sin(theta))
    for _ in range(4):
        rt = wm.radius(WT, WT)
        WT = massif_terrain(m, rt * np.cos(theta), rt * np.sin(theta))
    WT = np.round(WT, 1)
    step = m["step"]
    J = int(math.ceil((WT.max() - BOTTOM) / step)) + 1
    ys = BOTTOM + np.arange(J) * step
    R = massif_walls(wm, WT, ys)
    # Cap rings in zones that halve the sector count towards the centre
    # (S, S/2, S/4, S/8), each spaced about twice its arc spacing, so the
    # triangles stay near 2:1 instead of converging into slivers.
    rho, zones, k = [1.0], [], 0
    for div, lo in ((1, 0.5), (2, 0.25), (4, 0.125), (8, 0.0)):
        k0 = len(rho) - 1
        if lo > 0.0:
            while rho[-1] * (1.0 - 4.0 * math.pi * div / S) > lo * 1.02:
                rho.append(rho[-1] * (1.0 - 4.0 * math.pi * div / S))
            rho.append(lo)
        else:
            n = max(2, int(math.ceil(rho[-1] / (4.0 * math.pi * div / S * rho[-1]))))
            rho += list(np.linspace(rho[-1], 0.0, n + 1)[1:])
        zones.append((div, k0, len(rho) - 1))
    rho = np.round(np.array(rho), 5)
    K = len(rho)
    edge = R[-1]
    CY = np.zeros((K, S))
    CY[0] = WT
    for k in range(1, K):
        CY[k] = massif_terrain(m, edge * rho[k] * np.cos(theta), edge * rho[k] * np.sin(theta))
    # Rings drawn at S/div sectors: the sectors between take the straight
    # edge the mesh draws, so the collision matches it along the ring.
    for div, k0, k1 in zones:
        if div == 1:
            continue
        for k in range(k0 + 1, k1 + 1):
            keep = CY[k, ::div]
            nxt = np.roll(keep, -1)
            for j in range(1, div):
                CY[k, j::div] = keep + (nxt - keep) * (j / div)
    CY = np.round(CY, 1)
    return dict(name=m["name"], kind="massif", src=m, S=S, ys=ys, R=R, WT=WT, capRho=rho, CY=CY, r0=r0, zones=zones)


def massif_grids(sh):
    """Wall grid [J, S+1] and cap grid [K, S+1] (column S repeats column 0)."""
    S, ys, R, WT, CY, rho = sh["S"], sh["ys"], sh["R"], sh["WT"], sh["CY"], sh["capRho"]
    th = 2 * math.pi * np.arange(S + 1) / S
    Rw = np.concatenate([R, R[:, :1]], 1)
    Yw = np.minimum(ys[:, None], np.concatenate([WT, WT[:1]])[None, :])
    wall = np.stack([Rw * np.cos(th), Yw, Rw * np.sin(th)], -1)
    edge = Rw[-1]
    Yc = np.concatenate([CY, CY[:, :1]], 1)
    rr = edge[None, :] * rho[:, None]
    cap = np.stack([rr * np.cos(th), Yc, rr * np.sin(th)], -1)
    return wall, cap


def grid_normals(P, kind):
    """Smooth normals by central differences; wall: outward (radial), cap: up.
    Collapsed rows (above the wall top) take the normal of the edge below."""
    Pu = P[:, :-1]
    dc = np.roll(Pu, -1, axis=1) - np.roll(Pu, 1, axis=1)
    dr = np.zeros_like(Pu)
    dr[1:-1] = Pu[2:] - Pu[:-2]
    dr[0] = Pu[1] - Pu[0]
    dr[-1] = Pu[-1] - Pu[-2]
    back = np.zeros_like(Pu)
    back[1:] = Pu[1:] - Pu[:-1]
    small = np.linalg.norm(dr, axis=-1) < 1e-3
    dr[small] = back[small]
    n = np.cross(dr, dc)
    ref = np.stack([Pu[..., 0], np.zeros_like(Pu[..., 0]), Pu[..., 2]], -1) if kind == "wall" else np.array([0.0, 1.0, 0.0])
    flip = np.sum(n * ref, -1) < 0
    n[flip] *= -1
    ln = np.linalg.norm(n, axis=-1)
    bad = ln < 1e-6
    n = n / np.maximum(ln, 1e-9)[..., None]
    for r in range(n.shape[0]):
        if r and bad[r].any():
            n[r][bad[r]] = n[r - 1][bad[r]]
        if r == 0 and bad[r].any():
            n[r][bad[r]] = [0.0, 1.0, 0.0]
    if kind == "cap":
        n[bad] = [0.0, 1.0, 0.0]
    return np.concatenate([n, n[:, :1]], 1)


def grid_tris(nr, nc):
    r = np.arange(nr - 1)[:, None]
    c = np.arange(nc - 1)[None, :]
    a = (r * nc + c).reshape(-1)
    b, cc = a + 1, a + nc
    d = cc + 1
    # Rows run up the walls (and in from the cap edge), columns towards +theta:
    # (a, c, b), (b, c, d) is counter-clockwise from outside (GU_CCW).
    return np.concatenate([np.stack([a, cc, b], 1), np.stack([b, cc, d], 1)])


def grid_strip(p, nr, nc):
    """One triangle strip over the grid: each row of quads as
    (r,0), (r+1,0), (r,1), (r+1,1)... - the same triangles and winding as
    grid_tris (the GE flips every other one) - rows joined by repeating the
    last and the next first index. Rows with nothing but collapsed quads are
    left out. The GE transforms and lights every index it is sent (no vertex
    cache): a strip costs ~1.1 vertices per triangle, a list 3."""
    out = []
    for r in range(nr - 1):
        row = []
        for c in range(nc):
            row += [r * nc + c, (r + 1) * nc + c]
        t = grid_tris(2, nc) + 0  # triangles of this row, in row-local numbering
        rp = p[[i for i in range(r * nc, (r + 2) * nc)]]
        if not np.any(tri_area(rp, t) > 0.05):
            continue
        if out:
            out += [out[-1], row[0]]
        out += row
    return np.array(out, np.int64)


def tri_area(p, t):
    return 0.5 * np.linalg.norm(np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]]), axis=1)


def massif_ao(wall, cap, nw, nc_, rays=24, distance=380.0):
    """Ambient occlusion at every grid point, ray-traced against the full
    island (Blender BVH), then lightly blurred to calm the sampling noise."""
    if not HAVE_BLENDER:
        return np.ones(wall.shape[:2]), np.ones(cap.shape[:2])
    pts, faces = [], []
    for P in (wall, cap):
        nr, ncol = P.shape[:2]
        base = len(pts)
        flat = P.reshape(-1, 3)
        t = grid_tris(nr, ncol)
        t = t[tri_area(flat, t) > 0.05]
        pts += flat.tolist()
        faces += (t + base).tolist()
    tree = BVHTree.FromPolygons([Vector(p) for p in pts], faces)
    local = hemisphere(rays, 11)
    out = []
    for P, N in ((wall, nw), (cap, nc_)):
        nr, ncol = P.shape[:2]
        ao = np.ones((nr, ncol))
        for r in range(nr):
            for c in range(ncol - 1):
                if r and np.allclose(P[r, c], P[r - 1, c]):
                    ao[r, c] = ao[r - 1, c]
                    continue
                ao[r, c] = occlusion(tree, P[r, c], N[r, c], local, distance, 1.0)
            ao[r, -1] = ao[r, 0]
        u = ao[:, :-1]
        u = (u + np.roll(u, 1, 1) + np.roll(u, -1, 1)) / 3.0
        v = u.copy()
        v[1:-1] = (u[:-2] + u[1:-1] + u[2:]) / 3.0
        out.append(np.concatenate([v, v[:, :1]], 1))
    return out[0], out[1]


def mix(a, b, t):
    return a + (np.asarray(b) - a) * np.asarray(t)[..., None]


def rock_colour(m, x, y, z):
    """The cliff rock: beds of their own shade, patches, grain, stains."""
    g = GEOLOGY[m["geology"]]
    seed = m["seed"]
    bed_list = massif_beds(m, float(np.max(y)) + 1.0)
    bed_y = np.array([b[0] for b in bed_list])
    k = np.clip(np.searchsorted(bed_y, y), 0, len(bed_list) - 1)
    tone = np.array([b[3] for b in bed_list])[k]
    patch = fbm3(x / 60.0, y / 40.0, z / 60.0, seed + 21, 4)
    grain = fbm3(x / 45.0, y / 20.0, z / 45.0, seed + 22, 3)
    t = np.clip(0.15 + 0.5 * tone + 0.4 * (patch - 0.5) + 0.15 * (grain - 0.5), 0, 1)
    dark, mid, lite = (np.array(g[c]) for c in ("dark", "mid", "lite"))
    col = np.where((t < 0.5)[..., None], mix(dark, mid, t / 0.5), mix(mid, lite, (t - 0.5) / 0.5))
    if g["tuff"] is not None:
        red = ((np.sin(k * 7.13 + seed) * 43758.5453) % 1.0) < 0.22
        col = mix(col, g["tuff"], red * 0.7 * (0.7 + 0.3 * patch))
    col = col * (0.86 + 0.24 * fbm3(x / 42.0, y / 80.0, z / 42.0, seed + 33, 3))[..., None]
    return col, patch, grain


def paint_massif_wall(m, P, N, ao, WT):
    g = GEOLOGY[m["geology"]]
    seed = m["seed"]
    x, y, z = P[..., 0], P[..., 1], P[..., 2]
    wt = np.concatenate([WT, WT[:1]])[None, :] + np.zeros_like(y)
    col, patch, grain = rock_colour(m, x, y, z)
    # Ledges facing up carry turf, thrift and sea campion.
    up = N[..., 1]
    ledge = smoothstep(0.55, 0.85, up) * smoothstep(8.0, 25.0, y) * g["ledge"]
    green = np.array([0.30, 0.38, 0.19]) * (0.85 + 0.3 * fbm3(x / 50.0, y / 30.0, z / 50.0, seed + 40, 2))[..., None]
    col = mix(col, green, ledge)
    # Seabird cliffs: guano streaking down below the ledges, orange Xanthoria
    # round the colonies, grey-green crustose lichen higher.
    birds = smoothstep(0.55, 0.7, fbm3(x / 30.0, y / 25.0, z / 30.0, seed + 24, 3)) * smoothstep(20.0, 45.0, y) \
        * smoothstep(wt, wt - 15.0, y)
    guano = birds * smoothstep(0.5, 0.78, fbm3(x / 30.0, y / 45.0, z / 30.0, seed + 25, 3))
    col = mix(col, (0.86, 0.85, 0.80), guano * 0.75)
    lichen_o = birds * smoothstep(0.6, 0.75, fbm3(x / 45.0, y / 30.0, z / 45.0, seed + 26, 3))
    col = mix(col, (0.74, 0.48, 0.20), lichen_o * 0.5)
    lichen_g = smoothstep(0.62, 0.76, fbm3(x / 50.0, y / 35.0, z / 50.0, seed + 27, 3)) * smoothstep(15.0, 60.0, y)
    col = mix(col, (0.56, 0.60, 0.50), lichen_g * 0.35)
    # Splash zone: wet dark to ~9 m, weed at the waterline, murk below.
    col = col * (1.0 - 0.45 * smoothstep(9.0, 1.0, y))[..., None]
    col = mix(col, (0.17, 0.21, 0.13), smoothstep(1.6, 0.0, y) * (0.55 + 0.45 * grain))
    col = mix(col, (0.08, 0.13, 0.12), smoothstep(-1.0, -6.0, y))
    return finish_colour(col, ao)


def paint_massif_cap(m, P, N, ao, rho, edge):
    g = GEOLOGY[m["geology"]]
    seed = m["seed"]
    x, y, z = P[..., 0], P[..., 1], P[..., 2]
    slope = 1.0 - np.clip(N[..., 1], 0.0, 1.0)
    from_edge = (1.0 - rho[:, None]) * np.concatenate([edge, edge[:1]])[None, :]
    n1 = fbm3(x / 110.0, np.full_like(x, 3.0), z / 110.0, seed + 41, 3)
    grass = np.array([0.29, 0.39, 0.18]) * (0.85 + 0.3 * n1)[..., None]
    dry = smoothstep(0.5, 0.8, fbm3(x / 140.0, np.full_like(x, 5.0), z / 140.0, seed + 42, 4))
    col = mix(grass, (0.50, 0.47, 0.29), dry * 0.55)
    # Heather and peat on the high ground, darker in hollows.
    heath = smoothstep(150.0, 320.0, y) * smoothstep(0.35, 0.6, fbm3(x / 90.0, np.full_like(x, 7.0), z / 90.0, seed + 43, 3))
    col = mix(col, (0.33, 0.27, 0.24), heath * 0.75)
    # Moor at every height in patches (purple moor grass, heather, peat
    # hags), bracken going rusty on the sheltered slopes.
    moor = smoothstep(0.48, 0.66, fbm3(x / 260.0, np.full_like(x, 8.0), z / 260.0, seed + 46, 4))
    col = mix(col, (0.38, 0.34, 0.24), moor * 0.6)
    bracken = smoothstep(0.08, 0.3, slope) * smoothstep(0.55, 0.7, fbm3(x / 150.0, np.full_like(x, 10.0), z / 150.0, seed + 47, 3))
    col = mix(col, (0.48, 0.33, 0.18), bracken * 0.55)
    # Bare rock where it is steep or on the knolls, scree under the tops.
    knoll = smoothstep(0.62, 0.74, fbm3(x / 130.0, np.full_like(x, 9.0), z / 130.0, seed + 44, 3, ridged=True))
    bare = np.clip(smoothstep(0.32, 0.62, slope) + knoll * 0.6 * smoothstep(60.0, 200.0, y), 0, 1)
    rock, _, _ = rock_colour(m, x, y, z)
    col = mix(col, rock * 1.08, bare)
    # Maritime turf at the cliff top, bright with thrift; bird colonies whiten it.
    rim = smoothstep(35.0, 5.0, from_edge)
    col = mix(col, (0.36, 0.47, 0.21), rim * 0.6)
    birds = smoothstep(0.62, 0.74, fbm3(x / 90.0, np.full_like(x, 11.0), z / 90.0, seed + 45, 3)) * rim
    col = mix(col, (0.78, 0.78, 0.72), birds * 0.5)
    _ = g
    return finish_colour(col, ao)


def finish_colour(col, ao):
    """Weathered, salt-crusted: a little greyer than a postcard; AO; divided
    by the detail texture's mean (the GE multiplies them)."""
    grey = col.mean(-1, keepdims=True)
    col = col + (grey - col) * 0.15
    col = col * (0.32 + 0.68 * ao)[..., None]
    return np.clip(col / DETAIL_MEAN, 0.0, 1.0)


def massif_uv(sh, wall, cap):
    """Walls: u along the waterline outline (whole tiles round the island),
    v down from the top ring; cap: planar."""
    r0 = sh["r0"]
    S = sh["S"]
    th = 2 * math.pi * np.arange(S + 1) / S
    rr = np.concatenate([r0, r0[:1]])
    px, pz = rr * np.cos(th), rr * np.sin(th)
    seg = np.hypot(np.diff(px), np.diff(pz))
    cum = np.concatenate([[0.0], np.cumsum(seg)])
    tiles = max(1, round(cum[-1] / TILE_CLIFF))
    tile = cum[-1] / tiles
    u = (cum / tile)[None, :] + np.zeros(wall.shape[:2])
    v = (sh["ys"][-1] - wall[..., 1]) / tile
    uv_w = np.stack([u, v], -1)
    uv_c = np.stack([cap[..., 0] / TILE_TURF, cap[..., 2] / TILE_TURF], -1)
    return uv_w, uv_c


def blur_grid(col, passes):
    """Box-blur a colour grid [rows, cols(+seam), 3] (columns wrap): vertex
    colours on 10-20 m spacing must not change sharply from one vertex to the
    next - the GE stretches each change over a whole tall triangle (combs of
    green down the cliffs, stripes on the slopes)."""
    u = col[:, :-1]
    for _ in range(passes):
        u = (u + np.roll(u, 1, 1) + np.roll(u, -1, 1)) / 3.0
        v = u.copy()
        v[1:-1] = (u[:-2] + u[1:-1] + u[2:]) / 3.0
        u = v
    return np.concatenate([u, u[:, :1]], 1)


def pack_colour(col):
    c = np.clip(np.round(col * 255.0), 0, 255).astype(np.uint32)
    return (0xFF000000 | (c[..., 2] << 16) | (c[..., 1] << 8) | c[..., 0]).astype(np.uint32)


# ================================================================ detail textures

def write_png(path, img):
    """8-bit RGB PNG without extra libraries (previews of generated textures)."""
    import zlib
    a = (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)
    h, w = a.shape[:2]
    raw = b"".join(b"\0" + a[r].tobytes() for r in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def cliff_detail(n, seed=21):
    """Jointed, bedded rock face, tileable: beds of uneven thickness (3-6 m
    over the 40 m tile) with dark partings, a shadow under each overhanging
    bed and a lit lip on top of the one below; vertical joints placed per bed
    (blocks like masonry, now and then a joint running through two beds);
    a tone per block, lighter at its top; rain streaks down the face, grain,
    lichen blotches."""
    rng = np.random.default_rng(seed)
    px_m = n / TILE_CLIFF
    th = rng.uniform(1.6, 7.0, 60) * px_m
    edges = np.cumsum(th)
    edges = edges[edges < n * 1.5]
    edges = np.concatenate([[0.0], edges[:max(np.searchsorted(edges, n), 2)]])
    edges = edges / edges[-1] * n                       # whole beds per tile, so it repeats
    nb = len(edges) - 1
    row = np.arange(n)[:, None].astype(float)
    col = np.arange(n)[None, :].astype(float)
    warp = periodic_noise(n, 3.6, seed + 1) * 5.0 + periodic_noise(n, 2.2, seed + 7) * 1.2
    Y = (row + warp) % n
    bed = np.clip(np.searchsorted(edges, Y, side="right") - 1, 0, nb - 1)
    top, bottom = edges[bed], edges[bed + 1]
    d_top, d_bot = Y - top, bottom - Y                  # px below the bed's top / above its base
    val = np.ones((n, n))
    # Blocks: joints per bed, a tone per block, lighter towards its top.
    joint_d = np.full((n, n), 99.0)
    block = np.zeros((n, n), np.int64)
    jwarp = periodic_noise(n, 3.0, seed + 2) * 2.4
    Xb = col + jwarp
    for k in range(nb):
        m = bed == k
        cnt = rng.integers(2, 6)
        xs = np.sort(rng.uniform(0, n, cnt))
        X = (Xb + rng.uniform(-0.35, 0.35) * (Y - top)) % n     # joints lean a little, each bed its own way
        dx = np.abs(X[m][:, None] - xs[None, :])
        dx = np.minimum(dx, n - dx)
        joint_d[m] = dx.min(1)
        block[m] = k * 16 + np.searchsorted(xs, X[m]) % cnt
    tone = ((np.sin(block * 12.9898 + seed) * 43758.5453) % 1.0) - 0.5
    hgt = np.maximum(bottom - top, 1.0)
    val += 0.11 * tone + 0.07 * (0.5 - d_top / hgt) + 0.06 * periodic_noise(n, 3.0, seed + 8)
    # Partings: dark line, shadow under the overhang, lit lip below the line.
    pinch = 0.55 + 0.45 * smoothstep(-0.8, 0.6, periodic_noise(n, 3.2, seed + 9))   # partings fade out along the bed
    val -= 0.42 * np.exp(-(d_top / 1.1) ** 2) * pinch
    val -= 0.16 * np.exp(-d_top / 4.0) * (d_top > 1.0)
    val += 0.08 * np.exp(-((d_bot - 1.5) / 1.2) ** 2)
    # Joints: dark, with a soft shadowed side.
    val -= 0.34 * np.exp(-(joint_d / 0.9) ** 2) + 0.06 * np.exp(-joint_d / 3.0)
    # Weathering: rain streaks running down, grain, lichen blotches.
    val += 0.07 * periodic_noise(n, 2.2, seed + 3, sy=7.0)
    val += 0.05 * periodic_noise(n, 0.9, seed + 4)
    lichen = smoothstep(1.5, 2.2, periodic_noise(n, 2.2, seed + 5))
    val += 0.13 * lichen - 0.06 * smoothstep(1.7, 2.4, periodic_noise(n, 2.4, seed + 6))
    img = np.clip(val, 0.35, 1.25)[..., None] * np.array([1.0, 0.985, 0.955])
    img = img + (np.array([1.02, 1.0, 0.9]) * val[..., None] - img) * (0.4 * lichen)[..., None]
    return img * (DETAIL_MEAN / img.mean())


def detail_textures():
    """terrain_cliff / terrain_turf: tiling, near-neutral detail around
    DETAIL_MEAN that the vertex colours tint (T8, 256 x 256, mips)."""
    n = 256
    cliff = cliff_detail(n)
    # Turf: tussocks with dark gaps, grass blades, peat scars, small stones;
    # luminance and a warm cast only - the vertex colour gives the green.
    tuss = smoothstep(-0.4, 0.7, periodic_noise(n, 1.7, 11))
    blades = periodic_noise(n, 0.6, 12, sy=2.5)
    mottle = periodic_noise(n, 2.6, 13)
    peat = smoothstep(1.5, 2.1, periodic_noise(n, 2.6, 14))
    stones = smoothstep(2.2, 2.7, periodic_noise(n, 1.3, 15))
    val = 0.72 + 0.34 * tuss + 0.08 * blades + 0.05 * mottle - 0.25 * peat + 0.25 * stones
    turf = np.clip(val, 0.35, 1.3)[..., None] * np.array([1.0, 1.0, 0.9])
    turf = turf + (np.array([1.04, 1.0, 0.86]) * val[..., None] - turf) * (0.5 * smoothstep(0.6, 1.0, tuss))[..., None]
    turf = turf + (np.array([0.9, 0.82, 0.74]) * val[..., None] - turf) * peat[..., None]
    turf = turf + (val[..., None] - turf) * stones[..., None]
    turf *= DETAIL_MEAN / turf.mean()
    shown = {}
    for name, img in (("terrain_cliff", cliff), ("terrain_turf", turf)):
        shown[name] = ftx.export_quantized(os.path.join(OUT, name + ".ftx"), np.clip(img, 0, 1), max_levels=5)
        write_png(os.path.join(PREV, name + "_2x2.png"), np.tile(shown[name], (2, 2, 1)))
        print(f"[rocks] {name}.ftx 256x256, mean {img.mean():.3f}")
    return shown


# ================================================================ chunks (FTR1)

def make_chunk(p, n, uv, col, tris):
    return dict(p=np.asarray(p, np.float32), n=np.asarray(n, np.float32), uv=np.asarray(uv, np.float32),
                c=np.asarray(col, np.uint32), tris=np.asarray(tris, np.int64), surface=len(p), error=0.0,
                children=[], level=0, material=0)


def compact(ch):
    used = np.unique(np.concatenate([ch["tris"].reshape(-1), ch.get("strip", np.zeros(0, np.int64))]))
    remap = -np.ones(len(ch["p"]), np.int64)
    remap[used] = np.arange(len(used))
    for k in ("p", "n", "uv", "c"):
        ch[k] = ch[k][used]
    ch["tris"] = remap[ch["tris"]]
    if "strip" in ch:
        ch["strip"] = remap[ch["strip"]]
    ch["natural"] = ch["natural"][used] if "natural" in ch else np.zeros(len(used), bool)
    ch["surface"] = len(used)


def add_skirt(ch, depth, kind):
    """Fins along the chunk's open edges, hanging into the rock (walls:
    towards the axis; cap: down), so a neighbour at another level of detail
    never opens a crack to the sky. Each fin faces away from its own chunk
    (edge p->q of a CCW triangle gives the fin q, p, p', q'), so whichever
    side the crack is seen from, one of the two fins faces the eye."""
    tris = ch["tris"]
    ch["surfaceTris"] = len(tris)
    p = ch["p"]
    count, directed = {}, {}
    for t in tris:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            key = (min(a, b), max(a, b))
            count[key] = count.get(key, 0) + 1
            directed[key] = (a, b)
    nat = ch["natural"]
    nxt = {}
    for key, cnt in count.items():
        if cnt != 1:
            continue
        a, b = directed[key]
        if (nat[a] and nat[b]) or np.linalg.norm(p[a] - p[b]) < 1e-3:
            continue
        nxt.setdefault(a, []).append(b)
    if not nxt:
        ch["skirt"] = np.zeros(0, np.int64)
        return
    extra = sorted(set(nxt) | {b for bs in nxt.values() for b in bs})
    base = len(p)
    made = {v: base + k for k, v in enumerate(extra)}
    # Chains along the directed open edges; each becomes a ribbon strip
    # p, p', q, q', r, r'... - for edge p->q its triangles (p, p', q) and
    # (q, p', q') are the fin facing away from the chunk.
    heads = [v for v in nxt if v not in {b for bs in nxt.values() for b in bs}] + list(nxt)
    ribbon = []
    for s0 in heads:
        while nxt.get(s0):
            chain, cur = [s0], s0
            while nxt.get(cur):
                cur = nxt[cur].pop()
                chain.append(cur)
                if cur == s0:
                    break
            seq = []
            for v in chain:
                seq += [v, made[v]]
            if ribbon:
                ribbon += [ribbon[-1], seq[0]]
            ribbon += seq
    src = np.array(extra)
    q = p[src].copy()
    if kind == "wall":
        rad = np.stack([q[:, 0], np.zeros(len(q)), q[:, 2]], 1)
        rad /= np.maximum(np.linalg.norm(rad, axis=1, keepdims=True), 1e-6)
        q = q - rad * depth
    else:
        q[:, 1] -= depth
    ch["p"] = np.concatenate([p, q.astype(np.float32)])
    for k in ("n", "uv", "c"):
        ch[k] = np.concatenate([ch[k], ch[k][src]])
    ch["skirt"] = np.array(ribbon, np.int64)


def chunk_error(P, rs, cs):
    """Largest gap between the full-resolution grid and this level's grid
    (bilinear in the parameters) over the chunk's footprint, m."""
    rs, cs = np.array(rs), np.array(cs)
    rr = np.arange(rs[0], rs[-1] + 1)
    cc = np.arange(cs[0], cs[-1] + 1)
    ri = np.clip(np.searchsorted(rs, rr, side="right") - 1, 0, len(rs) - 2)
    ci = np.clip(np.searchsorted(cs, cc, side="right") - 1, 0, len(cs) - 2)
    rt = ((rr - rs[ri]) / (rs[ri + 1] - rs[ri]))[:, None, None]
    ct = ((cc - cs[ci]) / (cs[ci + 1] - cs[ci]))[None, :, None]
    Q = P[np.ix_(rs, cs)]
    q00, q01 = Q[ri][:, ci], Q[ri][:, ci + 1]
    q10, q11 = Q[ri + 1][:, ci], Q[ri + 1][:, ci + 1]
    est = (q00 * (1 - ct) + q01 * ct) * (1 - rt) + (q10 * (1 - ct) + q11 * ct) * rt
    full = P[rr[0]:rr[-1] + 1, cc[0]:cc[-1] + 1]
    # 99th percentile: a single spike (one gully, one notch) should not keep
    # a whole chunk at full detail kilometres away.
    return float(np.percentile(np.linalg.norm(full - est, axis=-1), 99.0))


def chunk_zone(P, N, UV, C, kind, material, natural_row=None):
    """Chunked LOD over one grid zone (walls or cap): level l samples every
    2^l-th row and column and covers CHUNK * 2^l quads a side, so every chunk
    has about the same triangle count; levels go up until one chunk covers
    the zone. Returns the root."""
    rows, cols = P.shape[:2]
    qr, qc = rows - 1, cols - 1
    if natural_row is None:
        natural_row = 0 if kind == "wall" else rows - 1   # the sea bed / the cap's centre: no neighbour
    levels, l = [], 0
    facing = [0, 0]
    while True:
        step = 1 << l
        span = CHUNK * step
        na, nb = -(-qc // span), -(-qr // span)
        level = {}
        for a in range(na):
            for b in range(nb):
                c0, c1 = a * span, min((a + 1) * span, qc)
                r0, r1 = b * span, min((b + 1) * span, qr)
                cs = list(range(c0, c1, step)) + [c1]
                rs = list(range(r0, r1, step)) + [r1]
                sub = np.ix_(rs, cs)
                p = P[sub].reshape(-1, 3)
                t = grid_tris(len(rs), len(cs))
                t = t[tri_area(p, t) > 0.05]
                if len(t) == 0:
                    continue
                ch = make_chunk(p, N[sub].reshape(-1, 3), UV[sub].reshape(-1, 2), C[sub].reshape(-1), t)
                ch["strip"] = grid_strip(p, len(rs), len(cs))
                nat = np.zeros((len(rs), len(cs)), bool)
                if natural_row in rs:
                    nat[rs.index(natural_row)] = True
                ch["natural"] = nat.reshape(-1)
                compact(ch)
                ch.update(level=l, material=material, error=chunk_error(P, rs, cs) if l else 0.0)
                if l == 0:
                    tt, pp = ch["tris"], ch["p"]
                    fn = np.cross(pp[tt[:, 1]] - pp[tt[:, 0]], pp[tt[:, 2]] - pp[tt[:, 0]])
                    agree = np.sum(fn * (ch["n"][tt[:, 0]] + ch["n"][tt[:, 1]] + ch["n"][tt[:, 2]]), 1) > 0
                    facing[0] += int(agree.sum())
                    facing[1] += len(agree)
                level[(a, b)] = ch
        levels.append(level)
        if na == 1 and nb == 1:
            break
        l += 1
    assert facing[0] > 0.97 * facing[1], f"{kind}: only {facing[0]}/{facing[1]} triangles face out"
    maxerr = [max([c["error"] for c in lv.values()] or [0.0]) for lv in levels]
    for l, lv in enumerate(levels):
        coarser = maxerr[l + 1:l + 3] or [maxerr[l]]
        # Deep enough for a crack, no deeper: a fin hundreds of metres long
        # pokes out of the cliff wherever the face under the edge is cut in
        # (gullies, notches) - dark fans on the slopes.
        depth = min(2.0 + 2.5 * max(coarser), 18.0)
        for ch in lv.values():
            add_skirt(ch, depth, kind)
    for l in range(1, len(levels)):
        for (a, b), ch in levels[l].items():
            ch["children"] = [levels[l - 1][k] for k in ((2 * a, 2 * b), (2 * a + 1, 2 * b), (2 * a, 2 * b + 1),
                                                         (2 * a + 1, 2 * b + 1)) if k in levels[l - 1]]
    return levels[-1].get((0, 0)), [len(lv) for lv in levels], maxerr


def finish_chunk(ch):
    p = ch["p"]
    lo, hi = p.min(0), p.max(0)
    centre = (lo + hi) * 0.5
    ch["centre"] = centre
    ch["radius"] = float(np.max(np.linalg.norm(p - centre, axis=1)))
    surf = ch["tris"][np.all(ch["tris"] < ch["surface"], axis=1)]
    e = 0.0
    for i, j in ((0, 1), (1, 2), (2, 0)):
        e = max(e, float(np.max(np.linalg.norm(p[surf[:, i]] - p[surf[:, j]], axis=1))))
    ch["edge"] = e
    # Normal cone of the surface's faces: the renderer skips a chunk whose
    # every face looks away from the eye (the far side of an island). A cone
    # over ~85 deg (a whole stack, a crumpled chunk) is never culled.
    fn = np.cross(p[surf[:, 1]] - p[surf[:, 0]], p[surf[:, 2]] - p[surf[:, 0]])
    fn = fn[np.linalg.norm(fn, axis=1) > 1e-6]
    fn /= np.linalg.norm(fn, axis=1, keepdims=True)
    axis = fn.mean(0)
    if np.linalg.norm(axis) < 1e-3:
        ch["cone"] = (0.0, 1.0, 0.0, -1.0)
    else:
        axis /= np.linalg.norm(axis)
        cmin = float(np.min(fn @ axis))
        ch["cone"] = (*axis.tolist(), cmin if cmin > math.cos(math.radians(85.0)) else -1.0)


VERTEX = np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("n", "<f4", 3), ("p", "<f4", 3)])
assert VERTEX.itemsize == 36


def write_ftr(path, materials, roots):
    """FTR1 (docs/formats/ftr1.md), then converted to FTR2 (ftr2.py,
    docs/formats/ftr2.md): roots first, then breadth-first, so each
    chunk's children sit together."""
    order, queue = [], list(roots)
    order += queue
    head = 0
    while head < len(order):
        ch = order[head]
        head += 1
        ch["firstChild"] = len(order) if ch["children"] else 0
        order += ch["children"]
    for ch in order:
        finish_chunk(ch)
    nverts = sum(len(c["p"]) for c in order)
    for c in order:
        c.setdefault("surfaceTris", len(c["tris"]))
        strip = c.get("strip")
        if strip is None:        # a stack's level: a triangle list
            c["idx"], c["stripLen"] = c["tris"].reshape(-1), 0
        else:                    # the grid as one strip, the skirt ribbons joined on
            skirt = c.get("skirt", np.zeros(0, np.int64))
            join = [strip[-1], skirt[0]] if len(strip) and len(skirt) else []
            c["idx"] = np.concatenate([strip, np.array(join, np.int64), skirt])
            c["stripLen"] = len(c["idx"])
    nidx = sum(len(c["idx"]) for c in order)
    radius = max(float(np.linalg.norm(c["centre"])) + c["radius"] for c in order)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sIIIHHf8x", b"FTR1", len(order), nverts, nidx, len(roots), len(materials), radius))
        for m in materials:
            fh.write(m.encode("ascii")[:15].ljust(16, b"\0"))
        fv = fi = 0
        for ch in order:
            nv, ni = len(ch["p"]), len(ch["idx"])
            assert nv < 65536 and ni < 65536, "chunk too big"
            fh.write(struct.pack("<6fIIHHHHHHHH4f", *ch["centre"], ch["radius"], ch["error"], ch["edge"], fv, fi,
                                 nv, ni, ch["surface"], ch["material"], ch["level"], ch["firstChild"],
                                 len(ch["children"]), ch["stripLen"], *ch["cone"]))
            fv += nv
            fi += ni
        for ch in order:
            v = np.zeros(len(ch["p"]), VERTEX)
            v["u"], v["v"] = ch["uv"][:, 0], ch["uv"][:, 1]
            v["c"] = ch["c"]
            v["n"] = ch["n"]
            v["p"] = ch["p"]
            fh.write(v.tobytes())
        for ch in order:
            fh.write(ch["idx"].astype("<u2").tobytes())
        fh.write(b"\0" * ((-fh.tell()) % 16))
        size = fh.tell()
    # The game reads FTR2: 16-bit vertices, light baked per placed rock (ftr2.py).
    import ftr2
    size = ftr2.convert(path) or size
    l0 = sum(c["surfaceTris"] for c in order if c["level"] == 0)
    l0_idx = sum(len(c["idx"]) for c in order if c["level"] == 0)
    return dict(chunks=len(order), verts=nverts, l0=l0, l0_idx=l0_idx, size=size)


# ================================================================ collision

def wall_radius_at(sh, i0, i1, f, y):
    """Mirror of world::wallRadius: per sector, between the ring below and
    the next ring - clamped to the sector's wall top - then across sectors."""
    ys, R, WT = sh["ys"], sh["R"], sh["WT"]
    step = ys[1] - ys[0]
    j0 = np.clip(np.floor((y - ys[0]) / step).astype(int), 0, len(ys) - 2)
    out = 0.0
    for i, w in ((i0, 1.0 - f), (i1, f)):
        y0 = ys[j0]
        y1 = np.minimum(ys[j0 + 1], WT[i])
        t = np.clip((y - y0) / np.maximum(y1 - y0, 1e-3), 0.0, 1.0)
        out = out + w * (R[j0, i] + (R[j0 + 1, i] - R[j0, i]) * t)
    return out


def top_height(sh, x, z):
    """Highest solid point of the column at (x, z) in the shape's frame;
    -inf where there is no rock (vectorised)."""
    S, ys, R, WT, CY, rho = sh["S"], sh["ys"], sh["R"], sh["WT"], sh["CY"], sh["capRho"]
    th = np.arctan2(z, x) % (2 * math.pi)
    rr = np.hypot(x, z)
    si = th / (2 * math.pi) * S
    i0 = np.floor(si).astype(int) % S
    i1 = (i0 + 1) % S
    f = si - np.floor(si)
    wt = WT[i0] * (1 - f) + WT[i1] * f
    top = np.full(x.shape, -np.inf)
    for j in range(len(ys)):
        rj = R[j, i0] * (1 - f) + R[j, i1] * f
        ynext = ys[min(j + 1, len(ys) - 1)]
        inside = (rr < rj) & (ys[j] <= wt + 1e-3)
        top = np.where(inside, np.maximum(top, np.minimum(ynext, wt)), top)
    edge = R[-1, i0] * (1 - f) + R[-1, i1] * f
    frac = rr / np.maximum(edge, 1e-3)
    capm = frac < 1.0
    k = np.clip(np.searchsorted(-rho, -frac, side="right") - 1, 0, len(rho) - 2)
    t = np.clip((rho[k] - frac) / np.maximum(rho[k] - rho[k + 1], 1e-6), 0, 1)
    h0 = CY[k, i0] * (1 - f) + CY[k, i1] * f
    h1 = CY[k + 1, i0] * (1 - f) + CY[k + 1, i1] * f
    h = h0 + (h1 - h0) * t
    # Within the top outline the cap is the column's top (it may dip below
    # the cliff tops); outside it, the walls' (overhangs, the batter).
    return np.where(capm, h, top)


def column_grid(sh):
    """Per cell: the highest rock (rounded up, with a margin) and the height
    the column is solid to from the sea bed (rounded down; NO_ROCK where any
    of the cell is open water) - Terrain::segment skips cells it passes over
    and accepts those it passes through without marching."""
    cell = 32.0 if sh["kind"] == "massif" else 16.0
    sub = 4
    ext = float(sh["R"].max()) + cell
    w = int(math.ceil(2 * ext / cell))
    x0 = -w * cell / 2
    xs = x0 + np.arange(w * sub + 1) * (cell / sub)
    X, Z = np.meshgrid(xs, xs)
    top = top_height(sh, X, Z)
    margin = 4.0 + cell / sub
    tmax = np.full((w, w), NO_ROCK, np.int32)
    tmin = np.full((w, w), NO_ROCK, np.int32)
    for cz in range(w):
        for cx in range(w):
            blk = top[max(cz * sub - 1, 0):cz * sub + sub + 2, max(cx * sub - 1, 0):cx * sub + sub + 2]
            fin = np.isfinite(blk)
            if fin.any():
                tmax[cz, cx] = int(math.ceil(blk[fin].max() + margin))
            if fin.all():
                tmin[cz, cx] = max(int(math.floor(blk.min() - margin)), int(BOTTOM))
    return dict(x0=x0, cell=cell, w=w, tmax=tmax, tmin=tmin)


def fmt(values, prec):
    return ",".join(("%.*f" % (prec, v)).rstrip("0").rstrip(".") if prec else "%d" % v for v in values)


def write_tables(shapes):
    with open(HEADER_H, "w") as fh:
        fh.write("#pragma once\n\n// Generated by tools/models/rocks.py - do not edit (docs/decisions/0042).\n"
                 "// Collision shapes of the rocks and islands: the tables their meshes were built from.\n\n"
                 '#include "world/rock_shape.h"\n\nnamespace world {\n\n'
                 f"constexpr int kRockShapeCount = {len(shapes)};\n"
                 "extern const RockShape kRockShapes[kRockShapeCount];\n\n}  // namespace world\n")
    lines = ["// Generated by tools/models/rocks.py - do not edit (docs/decisions/0042).", "",
             '#include "world/rock_shapes.h"', "", "namespace world {", "namespace {", ""]
    for sh in shapes:
        nm, R, CY, g = sh["name"], sh["R"], sh["CY"], sh["grid"]
        prec = 2 if sh["kind"] == "stack" else 1
        lines.append(f"// {nm}: {R.shape[0]} rings x {R.shape[1]} sectors, walls {BOTTOM:.0f}..{sh['ys'][-1]:.1f} m, "
                     f"apex {sh['apex']:.1f} m, column grid {g['w']}x{g['w']} x {g['cell']:.0f} m")
        lines.append(f"const float k_{nm}_radius[] = {{")
        for row in R:
            lines.append(fmt(row, prec) + ",")
        lines.append("};")
        lines.append(f"const float k_{nm}_wallTop[] = {{{fmt(sh['WT'], prec)}}};")
        lines.append(f"const float k_{nm}_capRho[] = {{{fmt(sh['capRho'], 4)}}};")
        lines.append(f"const float k_{nm}_capY[] = {{")
        for row in CY:
            lines.append(fmt(row, prec) + ",")
        lines.append("};")
        for key in ("tmax", "tmin"):
            lines.append(f"const int16_t k_{nm}_{key}[] = {{")
            for row in g[key]:
                lines.append(fmt(row, 0) + ",")
            lines.append("};")
        lines.append("")
    lines += ["}  // namespace", "", "const RockShape kRockShapes[kRockShapeCount] = {"]
    for sh in shapes:
        nm, R, g = sh["name"], sh["R"], sh["grid"]
        step = float(sh["ys"][1] - sh["ys"][0])
        lines.append(f'    {{"{nm}", {R.shape[1]}, {R.shape[0]}, {BOTTOM:.1f}f, {step:.6f}f, {len(sh["capRho"])}, '
                     f"k_{nm}_radius, k_{nm}_wallTop, k_{nm}_capRho, k_{nm}_capY, {sh['maxRadius']:.2f}f, "
                     f"{sh['apex']:.2f}f, {g['x0']:.1f}f, {g['cell']:.1f}f, {g['w']}, k_{nm}_tmax, k_{nm}_tmin}},")
    lines += ["};", "", "}  // namespace world", ""]
    with open(HEADER_CPP, "w") as fh:
        fh.write("\n".join(lines))


# ================================================================ previews

def preview_stack(name, verts, idx, img):
    """Workbench render of a stack with its skin on a sea plane."""
    me = bpy.data.meshes.new(name)
    pos = [(x, -z, y) for x, y, z in verts[:, 5:8].tolist()]   # game -> Blender
    me.from_pydata(pos, [], idx.tolist())
    uvl = me.uv_layers.new(name="UV")
    loop_v = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", loop_v)
    uvl.data.foreach_set("uv", np.stack([verts[loop_v, 0], 1.0 - verts[loop_v, 1]], -1).reshape(-1))
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    h, w = img.shape[:2]
    bimg = bpy.data.images.new(name + "_skin", w, h)
    px = np.ones((h, w, 4), np.float32)
    px[..., :3] = img
    bimg.pixels.foreach_set(px[::-1].reshape(-1))
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = bimg
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    me.materials.append(mat)
    return obj


def preview_massif(name, chunks, details):
    """The finest level of a massif as the PSP draws it: vertex colour times
    the tiling detail texture of its material (walls: cliff, cap: turf)."""
    pos, faces, cols, uvs, mats = [], [], [], [], []
    for ch in chunks:
        base = len(pos)
        ns = ch["surface"]
        pos += [(x, -z, y) for x, y, z in ch["p"][:ns].tolist()]
        t = ch["tris"][np.all(ch["tris"] < ns, axis=1)]
        faces += (t + base).tolist()
        mats += [ch["material"]] * len(t)
        c = ch["c"][:ns]
        cols.append(np.stack([(c & 255), (c >> 8) & 255, (c >> 16) & 255], 1) / 255.0)
        uvs.append(ch["uv"][:ns])
    me = bpy.data.meshes.new(name)
    me.from_pydata(pos, [], faces)
    cols, uvs = np.concatenate(cols), np.concatenate(uvs)
    attr = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
    rgba = np.ones((len(pos), 4), np.float32)
    rgba[:, :3] = cols ** 2.2     # sRGB -> linear (a product commutes with the gamma)
    attr.data.foreach_set("color", rgba.reshape(-1))
    uvl = me.uv_layers.new(name="UV")
    loop_v = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", loop_v)
    uvl.data.foreach_set("uv", np.stack([uvs[loop_v, 0], -uvs[loop_v, 1]], -1).reshape(-1))
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    me.polygons.foreach_set("material_index", mats)
    for key in ("terrain_cliff", "terrain_turf"):
        img = details[key]
        h, w = img.shape[:2]
        bimg = bpy.data.images.new(key, w, h)
        px = np.ones((h, w, 4), np.float32)
        px[..., :3] = img
        bimg.pixels.foreach_set(px[::-1].reshape(-1))
        mat = bpy.data.materials.new(key)
        mat.use_nodes = True
        nt = mat.node_tree
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = bimg
        ca = nt.nodes.new("ShaderNodeVertexColor")
        ca.layer_name = "Col"
        mul = nt.nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs["Factor"].default_value = 1.0
        nt.links.new(ca.outputs["Color"], mul.inputs[6])
        nt.links.new(tex.outputs["Color"], mul.inputs[7])
        bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        nt.links.new(mul.outputs[2], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.95
        me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def render_views(path, size, color_type):
    scene = bpy.context.scene
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.color = (0.55, 0.65, 0.78)
    sun = None
    if color_type == "CYCLES":
        scene.render.engine = "CYCLES"
        scene.cycles.samples = 24
        scene.cycles.device = "CPU"
        scene.view_settings.view_transform = "Standard"
        scene.render.resolution_x, scene.render.resolution_y = 960, 560
        scene.world.use_nodes = True
        bg = scene.world.node_tree.nodes.get("Background")
        bg.inputs[0].default_value = (0.45, 0.58, 0.78, 1.0)
        bg.inputs[1].default_value = 0.9
        sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
        sun.data.energy = 3.2
        sun.data.angle = math.radians(1.0)
        sun.rotation_euler = (math.radians(55), 0.0, math.radians(35))   # day sun: 35 deg up, behind-right
        scene.collection.objects.link(sun)
    else:
        scene.render.engine = "BLENDER_WORKBENCH"
        sh = scene.display.shading
        sh.light = "STUDIO"
        sh.color_type = color_type
        sh.show_shadows = True
        sh.shadow_intensity = 0.45
        sh.show_cavity = False
        scene.render.resolution_x, scene.render.resolution_y = 1200, 700
    sea = bpy.data.meshes.new("sea")
    s = size * 6
    sea.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    sea_obj = bpy.data.objects.new("sea", sea)
    m = bpy.data.materials.new("sea")
    m.diffuse_color = (0.07, 0.2, 0.25, 1.0)
    m.use_nodes = True
    sb = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    sb.inputs["Base Color"].default_value = (0.01, 0.035, 0.05, 1.0)
    sb.inputs["Roughness"].default_value = 0.25
    sea.materials.append(m)
    scene.collection.objects.link(sea_obj)
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        scene.collection.objects.link(cam)
        scene.camera = cam
    for view, pos, target in (("side", (size * 1.6, -size * 1.9, size * 0.35), (0, 0, size * 0.12)),
                              ("high", (size * 1.1, -size * 1.1, size * 1.2), (0, 0, 0)),
                              ("north", (-size * 0.5, size * 2.2, size * 0.3), (0, 0, size * 0.12)),
                              ("low", (-size * 0.95, size * 0.95, size * 0.06), (0, size * 0.3, size * 0.1))):
        cam.location = pos
        cam.rotation_euler = (Vector(target) - Vector(pos)).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 35
        cam.data.clip_end = size * 30
        scene.render.filepath = path + f"_{view}.png"
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(sea_obj)
    if sun is not None:
        bpy.data.objects.remove(sun)


# ================================================================ main

def stack_chunks(s, sh, want):
    """Stacks: a chain of whole-rock levels (full, half, quarter density)."""
    R, CY = sh["R"], sh["CY"]
    full_v, full_i, pos, rows = build_stack_mesh(s, R, CY)
    levels = [(full_v, full_i)]
    levels.append(build_stack_mesh(s, R, CY, sector_step=2, ring_step=2,
                                   cap_rings=sorted({0, len(s["cap"]) // 2, len(s["cap"]) - 1}))[:2])
    levels.append(build_stack_mesh(s, R, CY, sector_step=4, ring_step=4, cap_rings=[0, len(s["cap"]) - 1])[:2])
    chunks = []
    white = 0xFFFFFFFF
    for l, (v, i) in enumerate(levels):
        ch = make_chunk(v[:, 5:8], v[:, 2:5], v[:, 0:2], np.full(len(v), white, np.uint32), i)
        ch["level"] = l
        chunks.append(ch)
    tree = BVHTree.FromPolygons([Vector(p) for p in full_v[:, 5:8].tolist()], full_i.tolist()) if HAVE_BLENDER else None
    for l in (1, 2):
        if HAVE_BLENDER:
            coarse = BVHTree.FromPolygons([Vector(p) for p in levels[l][0][:, 5:8].tolist()], levels[l][1].tolist())
            chunks[l]["error"] = max(coarse.find_nearest(Vector(p))[3] for p in full_v[:, 5:8].tolist())
        else:
            chunks[l]["error"] = 0.6 * s["step"] * l
    _ = tree
    chunks[2]["children"] = [chunks[1]]
    chunks[1]["children"] = [chunks[0]]
    img = None
    if want:
        ao = grid_ao(pos, full_i, full_v)
        img = paint_stack(s, R, CY, ao, rows)
    return chunks, img, full_v, full_i


def main():
    t0 = time.time()
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREV, exist_ok=True)
    want_preview = HAVE_BLENDER and "--no-preview" not in ARGS
    only = None
    if "--only" in ARGS:
        only = set(ARGS[ARGS.index("--only") + 1].split(","))
    if HAVE_BLENDER:
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj)
    details = detail_textures()
    shapes = [stack_shape(s) for s in STACKS] + [massif_shape(m) for m in MASSIFS]
    for sh in shapes:
        sh["maxRadius"] = float(sh["R"].max())
        sh["apex"] = float(max(sh["CY"].max(), sh["WT"].max()))
        sh["grid"] = column_grid(sh)
    write_tables(shapes)
    print(f"[rocks] {HEADER_CPP} ({time.time() - t0:.0f}s)")
    for sh in shapes:
        nm = sh["name"]
        if only is not None and nm not in only:
            continue
        path = os.path.join(OUT, f"rock_{nm}.ftr")
        if sh["kind"] == "stack":
            s = sh["src"]
            chunks, img, fv, fi = stack_chunks(s, sh, True)
            shown = ftx.export_quantized(os.path.join(OUT, f"rock_{nm}.ftx"), img, max_levels=4)
            st = write_ftr(path, [f"rock_{nm}"], [chunks[2]])
            info = f"errors {chunks[1]['error']:.1f}/{chunks[2]['error']:.1f} m"
            if want_preview:
                for o in list(bpy.data.objects):
                    bpy.data.objects.remove(o)
                preview_stack(nm, fv, fi, shown)
                render_views(os.path.join(PREV, f"rock_{nm}"), max(sh["maxRadius"], sh["apex"]) * 1.2, "TEXTURE")
        else:
            m = sh["src"]
            wall, cap = massif_grids(sh)
            nw, nc_ = grid_normals(wall, "wall"), grid_normals(cap, "cap")
            ao_w, ao_c = massif_ao(wall, cap, nw, nc_)
            uv_w, uv_c = massif_uv(sh, wall, cap)
            col_w = pack_colour(blur_grid(paint_massif_wall(m, wall, nw, ao_w, sh["WT"]), 2))
            col_c = pack_colour(blur_grid(paint_massif_cap(m, cap, nc_, ao_c, sh["capRho"], sh["R"][-1]), 1))
            root_w, counts_w, err_w = chunk_zone(wall, nw, uv_w, col_w, "wall", 0)
            roots = [root_w]
            info = f"walls {counts_w} chunks, errors {['%.0f' % e for e in err_w]} m; cap"
            for div, k0, k1 in sh["zones"]:
                sub = (slice(k0, k1 + 1), slice(0, None, div))
                last = k1 if div == 8 else None
                r, counts, err = chunk_zone(cap[sub], nc_[sub], uv_c[sub], col_c[sub], "cap", 1,
                                            natural_row=(k1 - k0) if last is not None else -1)
                roots.append(r)
                info += f" /{div}: {counts} {['%.0f' % e for e in err]} m"
            st = write_ftr(path, ["terrain_cliff", "terrain_turf"], [r for r in roots if r])
            if want_preview:
                for o in list(bpy.data.objects):
                    bpy.data.objects.remove(o)
                l0 = []

                def walk(ch):
                    if ch["level"] == 0:
                        l0.append(ch)
                    for c in ch["children"]:
                        walk(c)
                for r in roots:
                    if r:
                        walk(r)
                preview_massif(nm, l0, details)
                render_views(os.path.join(PREV, f"rock_{nm}"), max(sh["maxRadius"], sh["apex"]) * 1.1, "CYCLES")
        print(f"[rocks] {nm}: {st['l0']} tris at full detail ({st['l0_idx'] / max(st['l0'], 1):.2f} indices each), {st['chunks']} chunks, {st['verts']} verts, "
              f"{st['size'] / 1024:.0f} KB; apex {sh['apex']:.0f} m, radius {sh['maxRadius']:.0f} m; {info} "
              f"({time.time() - t0:.0f}s)")


main()
