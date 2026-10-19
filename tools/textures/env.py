"""Environment textures: sea ripples and whitecaps, the overcast deck, cloud
puff, sun glow (numpy only).

Usage (from the project root):  python3 tools/textures/env.py
Writes assets/textures/*.ftx and previews to build/previews/env_*.png.
See docs/decisions/0040-ocean-v2.md, 0041-weather-time-of-day.md and
0011-world-rendering.md for how they are used.
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


# Wind direction the sea's textures are built for: the swell in
# src/render/ocean.cpp runs towards (sin 0.6, -cos 0.6) in world x/z, and the
# textures map u -> x, v -> z.
WIND = 0.6


def spectral_sea(size, peak_cycles, seed, spread=2.0, cutoff_cycles=None):
    """Tileable sea height field from a directional wind-wave spectrum
    (Phillips-like: k^-4 above the peak, cos^spread of the angle to the wind,
    waves running against the wind removed), random phases, inverse FFT -
    periodic by construction. Returns height and its gradient per texel."""
    rng = np.random.default_rng(seed)
    k = np.fft.fftfreq(size, 1.0 / size)          # cycles per tile
    kx, ky = np.meshgrid(k, k)
    kk = np.hypot(kx, ky)
    kk[0, 0] = 1.0
    wx, wy = math.sin(WIND), -math.cos(WIND)
    cos_a = (kx * wx + ky * wy) / kk
    spec = np.exp(-(peak_cycles / kk) ** 2) / kk ** 4 * np.clip(cos_a, 0, 1) ** spread
    if cutoff_cycles:
        spec *= np.exp(-(kk / cutoff_cycles) ** 2)
    spec[0, 0] = 0.0
    amp = np.sqrt(spec) * (rng.normal(size=(size, size)) + 1j * rng.normal(size=(size, size)))
    h = np.real(np.fft.ifft2(amp))
    gx = np.real(np.fft.ifft2(amp * 2j * math.pi * kx / size))
    gy = np.real(np.fft.ifft2(amp * 2j * math.pi * ky / size))
    return h / h.std(), gx / h.std(), gy / h.std()


def write_grey(name, value, levels=4, tint=(1.0, 1.0, 1.0)):
    """CLUT index = brightness 0..1 (exact 256-step ramp, no k-means)."""
    lv, cur = [], value
    for i in range(ftx.mip_levels(value.shape[1], value.shape[0], levels)):
        if i:
            cur = ftx.downsample(cur[..., None])[..., 0]
        lv.append(np.clip(np.round(cur * 255), 0, 255).astype(np.uint8))
    t = np.arange(256) / 255
    clut = np.concatenate([t[:, None] * np.array(tint)[None, :], np.ones((256, 1))], 1)
    ftx.export_indexed(os.path.join(OUT, f"{name}.ftx"), lv, clut)
    return clut[lv[0]][..., :3]


def sea_detail():
    """256x256 over 24 m: wind ripples and short waves as micro-slope
    brightness around a mean of 0.80 (src/render/ocean.h kDetailMean): the
    per-vertex sea colour is multiplied by it. Facets tilted away from the
    viewer reflect more sky (brighter), towards him show the dark body."""
    size = 256
    h, gx, gy = spectral_sea(size, 6.0, seed=21, spread=2.0, cutoff_cycles=70)
    _, rx, ry = spectral_sea(size, 18.0, seed=22, spread=1.0, cutoff_cycles=110)
    gx, gy = gx / gx.std() + 0.45 * rx / rx.std(), gy / gy.std() + 0.45 * ry / ry.std()
    wx, wy = math.sin(WIND), -math.cos(WIND)
    along = gx * wx + gy * wy            # slope along the wind: lee / stoss faces
    across = -gx * wy + gy * wx
    lum = 0.75 * along / along.std() + 0.35 * across / across.std() + 0.25 * h
    lum = 0.80 + 0.10 * np.tanh(lum * 0.7)
    lum += 0.80 - lum.mean()
    shown = write_grey("sea_detail", np.clip(lum, 0, 1))
    print(f"[env] sea_detail mean {lum.mean():.3f}, range {lum.min():.2f}..{lum.max():.2f}")
    write_png(os.path.join(PREVIEW, "env_sea_detail.png"), np.tile(shown, (2, 2, 1)))


def sea_foam():
    """256x256 over 56 m, additive (CLUT white, alpha = intensity): breaking
    crests - small whitecaps where the steep wave field peaks, with a fading
    streak of foam trailing downwind. The vertex alpha sets how many show."""
    size = 256
    h, _, _ = spectral_sea(size, 5.0, seed=31, spread=3.0, cutoff_cycles=40)
    detail = periodic_fbm(size, 32, 3, seed=32)
    crest = smoothstep(1.55, 2.4, h + 0.6 * (detail - 0.5))
    # Foam left behind a breaker drifts downwind and decays: smear upwind copies.
    wx, wy = math.sin(WIND), -math.cos(WIND)
    trail = np.zeros_like(crest)
    for k in range(1, 14):
        sx, sy = int(round(-wx * k * 1.6)), int(round(-wy * k * 1.6))
        trail = np.maximum(trail, np.roll(np.roll(crest, sy, 0), sx, 1) * (1.0 - k / 14) ** 1.5)
    foam = np.maximum(crest, 0.45 * trail * smoothstep(0.35, 0.75, detail))
    alpha = np.clip(foam, 0, 1)
    lv, cur = [], alpha
    for i in range(ftx.mip_levels(size, size, 4)):
        if i:
            cur = ftx.downsample(cur[..., None])[..., 0]
        lv.append(np.clip(np.round(cur * 255), 0, 255).astype(np.uint8))
    t = np.arange(256) / 255
    clut = np.concatenate([np.ones((256, 3)), t[:, None]], 1)
    ftx.export_indexed(os.path.join(OUT, "sea_foam.ftx"), lv, clut)
    print(f"[env] sea_foam mean alpha {alpha.mean():.3f}, crest cover {(crest > 0.5).mean() * 100:.1f}%")
    preview = np.array([0.06, 0.18, 0.22]) + alpha[..., None] * 0.9
    write_png(os.path.join(PREVIEW, "env_sea_foam.png"), np.tile(np.clip(preview, 0, 1), (2, 2, 1)))


def deck():
    """256x256 over 640 m: the underside / top of the overcast deck, soft
    lumps 0.7..1.0 (multiplied by the deck colour of the weather)."""
    size = 256
    n = periodic_fbm(size, 4, 5, seed=51)
    m = periodic_fbm(size, 16, 3, seed=52)
    v = 0.70 + 0.30 * smoothstep(0.25, 0.75, 0.75 * n + 0.35 * m)
    shown = write_grey("deck", np.clip(v, 0, 1))
    write_png(os.path.join(PREVIEW, "env_deck.png"), np.tile(shown, (2, 2, 1)))


def _worley(size, nx, ny, seed, warp=None):
    """Tileable cellular noise: one jittered point per cell of an nx x ny grid,
    distances wrap round the tile. Returns F1 and F2 in units of a cell."""
    rng = np.random.default_rng(seed)
    px = (np.arange(nx)[None, :] + rng.uniform(0.15, 0.85, (ny, nx))) / nx
    pz = (np.arange(ny)[:, None] + rng.uniform(0.15, 0.85, (ny, nx))) / ny
    u = (np.arange(size)[None, :] + 0.5) / size + np.zeros((size, 1))
    v = (np.arange(size)[:, None] + 0.5) / size + np.zeros((1, size))
    if warp is not None:
        u, v = u + warp[0], v + warp[1]
    f1 = np.full((size, size), 9.0)
    f2 = np.full((size, size), 9.0)
    for j in range(ny):
        for i in range(nx):
            du = (u - px[j, i] + 0.5) % 1.0 - 0.5
            dv = (v - pz[j, i] + 0.5) % 1.0 - 0.5
            d = np.hypot(du * nx, dv * ny)   # in cell units (cells may be elongated)
            f2 = np.where(d < f1, f1, np.minimum(f2, d))
            f1 = np.minimum(f1, d)
    return f1, f2


def deck_base():
    """256x256 over 2800 m: the underside of a stratocumulus deck - rounded
    dark lumps of uneven size (two scales of cells, warped), each darkest at
    its middle and thinning towards the edge, with soft lighter gaps between
    them that are wide in places and closed in others; some lumps merged
    into larger dark masses; billowy mottling. 0.4 (thick) .. ~0.95 (gaps),
    multiplied by the deck colour of the weather (src/render/cloud_deck.cpp)."""
    size = 256
    w1 = (0.11 * (periodic_fbm(size, 3, 3, 61) - 0.5) * 2, 0.11 * (periodic_fbm(size, 3, 3, 62) - 0.5) * 2)
    f1a, _ = _worley(size, 4, 6, 63, w1)
    w2 = (0.06 * (periodic_fbm(size, 6, 3, 67) - 0.5) * 2, 0.06 * (periodic_fbm(size, 6, 3, 68) - 0.5) * 2)
    f1b, _ = _worley(size, 9, 13, 69, w2)
    dome_a = 1.0 - smoothstep(0.1, 0.85, f1a)               # big lumps
    dome_b = 1.0 - smoothstep(0.05, 0.85, f1b)              # small ones between and on them
    thick = 1.0 - (1.0 - dome_a) * (1.0 - 0.7 * dome_b)     # soft union: lumps run into each other
    merge = smoothstep(0.5, 0.72, periodic_fbm(size, 3, 3, 64))
    thick = np.clip(thick + 0.55 * merge, 0.0, 1.0)
    gap = 0.75 + 0.25 * periodic_fbm(size, 5, 3, 70)       # gaps not all equally bright
    billow = np.abs(2.0 * periodic_fbm(size, 10, 4, 65) - 1.0)
    v = gap * (1.0 - 0.52 * thick) - 0.1 * thick * (1.0 - billow) + 0.03 * (periodic_fbm(size, 24, 2, 66) - 0.5)
    shown = write_grey("deck_base", np.clip(v, 0.0, 1.0))
    write_png(os.path.join(PREVIEW, "env_deck_base.png"), np.tile(shown, (2, 2, 1)))


def deck_storm():
    """256x256 over 2800 m: nimbostratus from below - a dark, thick, nearly
    uniform base: broad soft variation and long soft streaks along the wind
    (u). 0.42 .. 0.82."""
    size = 256
    broad = periodic_fbm(size, 3, 4, 71)
    src = periodic_fbm(size, 6, 4, 72)
    streak = np.repeat(src[:, ::4], 4, axis=1)            # 4x longer along u
    for _ in range(3):
        streak = (streak + np.roll(streak, 2, 1) + np.roll(streak, -2, 1)) / 3.0
    v = 0.62 + 0.14 * (broad - 0.5) * 2 - 0.12 * (streak - 0.5) * 2 + 0.03 * (periodic_fbm(size, 20, 3, 73) - 0.5)
    shown = write_grey("deck_storm", np.clip(v, 0.0, 1.0))
    write_png(os.path.join(PREVIEW, "env_deck_storm.png"), np.tile(shown, (2, 2, 1)))


def deck_scud():
    """256x256 over 1600 m: ragged fractus under the deck - a third of the
    tile covered by torn patches with soft edges (alpha), denser cores. The
    CLUT is a ramp: index = coverage, grey a little darker in the cores."""
    size = 256
    warp_u = periodic_fbm(size, 5, 3, 81)
    n = periodic_fbm(size, 5, 5, 82)
    n = 0.7 * n + 0.3 * np.roll(np.roll(warp_u, 17, 0), 9, 1)
    rag = np.abs(2.0 * periodic_fbm(size, 16, 3, 83) - 1.0)
    a = smoothstep(0.52, 0.7, n) * (0.55 + 0.45 * rag)
    idx = []
    cur = a
    for i in range(ftx.mip_levels(size, size, 4)):
        if i:
            cur = ftx.downsample(cur[..., None])[..., 0]
        idx.append(np.clip(np.round(cur * 255), 0, 255).astype(np.uint8))
    t = np.arange(256) / 255
    grey = 1.0 - 0.18 * t
    clut = np.stack([grey, grey, grey, t], 1)
    ftx.export_indexed(os.path.join(OUT, "deck_scud.ftx"), idx, clut)
    write_png(os.path.join(PREVIEW, "env_deck_scud.png"), np.tile((a * grey)[..., None].repeat(3, 2), (2, 2, 1)))


def _shift(a, dx, dy, dz):
    """a shifted by whole voxels (x, y, z axes = 2, 1, 0), zero fill."""
    out = np.zeros_like(a)
    nz, ny, nx = a.shape
    src = [slice(max(-d, 0), n - max(d, 0)) for d, n in ((dz, nz), (dy, ny), (dx, nx))]
    dst = [slice(max(d, 0), n - max(-d, 0)) for d, n in ((dz, nz), (dy, ny), (dx, nx))]
    out[tuple(dst)] = a[tuple(src)]
    return out


def _noise3(shape, cells, seed):
    """Smooth 3D value noise (trilinear, smoothstepped) over a (z, y, x) grid."""
    rng = np.random.default_rng(seed)
    lat = rng.random((cells + 1, cells + 1, cells + 1))
    grids = [np.linspace(0, cells - 1e-6, n) for n in shape]
    gz, gy, gx = np.meshgrid(*grids, indexing="ij")
    iz, iy, ix = gz.astype(int), gy.astype(int), gx.astype(int)
    fz, fy, fx = [f * f * (3 - 2 * f) for f in (gz - iz, gy - iy, gx - ix)]
    out = 0.0
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                w = (fz if dz else 1 - fz) * (fy if dy else 1 - fy) * (fx if dx else 1 - fx)
                out = out + w * lat[iz + dz, iy + dy, ix + dx]
    return out


def _billow3(shape, cells, octaves, seed):
    """Billowy fractal noise: sum of |2n - 1| octaves (rounded lumps with
    creases between them - the texture of a cumulus surface), 0..1."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        n = _noise3(shape, cells * 2 ** o, seed + 13 * o)
        total = total + amp * np.abs(2.0 * n - 1.0)
        norm += amp
        amp *= 0.55
    return total / norm


def _fbm3(shape, cells, octaves, seed):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * _noise3(shape, cells * 2 ** o, seed + 29 * o)
        norm += amp
        amp *= 0.5
    return total / norm


def _puff_density(seed, kind, n=128, depth=96):
    """Density of one cloud element on a (z, y, x) grid in [-1, 1]^3. No
    spheres: a lumpy envelope bent by low-frequency domain warping, eaten at
    its edge by billowy fractal noise over four octaves - so the outline is
    ragged and the surface cauliflowered at every scale, like a real cumulus.
    kind "cumulus": a heaped element with a flat underside; "wisp": a flat,
    fibrous scrap (bases, edges, stratocumulus)."""
    rng = np.random.default_rng(seed)
    shape = (depth, n, n)
    z, y, x = np.meshgrid(np.linspace(-1, 1, depth), np.linspace(1, -1, n), np.linspace(-1, 1, n), indexing="ij")
    # Domain warp: shove the coordinates around by a smooth noise field.
    wx = _fbm3(shape, 2, 2, seed + 1) - 0.5
    wy = _fbm3(shape, 2, 2, seed + 2) - 0.5
    wz = _fbm3(shape, 2, 2, seed + 3) - 0.5
    if kind == "cumulus":
        xw, yw, zw = x + 0.6 * wx, y + 0.5 * wy, z + 0.5 * wz
        stretch = rng.uniform(0.95, 1.2)
        r = np.sqrt((xw / (0.8 * stretch)) ** 2 + (np.maximum(yw + 0.05, 0) / 0.78) ** 2 +
                    (np.minimum(yw + 0.05, 0) / 0.42) ** 2 + (zw / 0.7) ** 2)
        envelope = 1.0 - r
        detail = _billow3(shape, 3, 4, seed + 10)
        field = envelope + 0.75 * (detail - 0.55)
        # A soft ramp, not a shell: the edge thins out over a good depth, so
        # back-light glows through it instead of drawing an outline.
        dens = smoothstep(-0.08, 0.5, field) * (0.45 + 0.55 * detail)
        dens += 0.07 * smoothstep(-0.25, 0.0, field) * _billow3(shape, 5, 2, seed + 14)  # wispy fringe
        dens *= smoothstep(-0.62, -0.36, y + 0.15 * wx)  # flat but not ruler-straight base
    else:
        xw, yw, zw = x + 0.5 * wx, y + 0.3 * wy, z + 0.45 * wz
        r = np.sqrt((xw / 0.95) ** 2 + (yw / 0.36) ** 2 + (zw / 0.7) ** 2)
        envelope = 1.0 - r
        # Torn and fibrous: clumps from billow noise, streaks from noise
        # stretched along x, holes where both are low.
        fib = _noise3((depth, n, n // 4), 8, seed + 11)
        fib = np.repeat(fib, 4, axis=2)[:, :, :n]
        detail = 0.5 * _billow3(shape, 4, 3, seed + 12) + 0.5 * fib
        field = envelope + 1.0 * (detail - 0.5)
        dens = 0.5 * smoothstep(-0.05, 0.55, field) * (0.3 + 0.7 * detail)
    dens *= smoothstep(0.98, 0.78, np.sqrt(x * x + y * y))  # round falloff: never touch the cell edge
    return dens


def _render_puff(dens, light, ambient, phase, sigma=16.0, steps=48):
    """Raymarch a density cube seen along +z: alpha = 1 - exp(-tau), colour =
    in-scattered sun (phase * transmittance to the light) plus ambient, per
    unit of opacity. `light` = unit vector towards the light (x, y, z)."""
    depth, n, _ = dens.shape
    dz = 2.0 / depth
    # Transmittance to the light: march towards it, accumulating density.
    lx, ly, lz = light
    step_vox = 1.2
    tau_l = np.zeros_like(dens)
    for k in range(1, steps + 1):
        sx = int(round(k * step_vox * lx))
        sy = int(round(-k * step_vox * ly))      # y index grows downwards
        sz = int(round(k * step_vox * lz * depth / n))
        tau_l += _shift(dens, -sx, -sy, -sz)
    t_light = np.exp(-sigma * tau_l * step_vox * (2.0 / n))
    # Along the view: front (z = 0) to back.
    tau_v = np.cumsum(dens, axis=0) * sigma * dz
    t_view = np.exp(-(tau_v - dens * sigma * dz))
    # Sky light comes from above: the tops see more of it than the bases.
    yy = np.linspace(1, -1, n)[None, :, None]
    sky = ambient * (0.55 + 0.45 * yy)
    scatter = dens * sigma * dz * (sky + phase * t_light)
    radiance = (t_view * scatter).sum(axis=0)
    alpha = 1.0 - np.exp(-tau_v[-1])
    colour = np.where(alpha > 1e-3, radiance / np.maximum(alpha, 1e-3), 0.0)
    return colour, alpha


def cloud():
    """512x512 atlas of pre-rendered volumetric cloud elements
    (docs/decisions/0041), 128x128 cells: rows 0-1 four cumulus elements,
    rows 2-3 four flat wisps; even rows lit from behind the viewer (bright
    tops, self-shadowed undersides), odd rows with the sun behind the element
    (thin edges glow - forward scattering, the "silver lining" - the core goes
    dark). The game cross-fades a row pair by the angle to the sun and tints
    each element by where it sits in its cloud. CLUT = 16 brightness x 16
    alpha levels: smooth alpha edges without k-means."""
    cell = 128
    lum_img = np.zeros((512, 512))
    alpha_img = np.zeros((512, 512))
    front = np.array([0.30, 0.78, -0.55])
    back = np.array([0.12, 0.30, 0.95])
    front /= np.linalg.norm(front)
    back /= np.linalg.norm(back)
    # One exposure for every cell (front-lit white tops ~1.0), so the game's
    # cross-fade between rows doesn't jump in brightness.
    exposure = None
    for kind_row, kind in ((0, "cumulus"), (2, "wisp")):
        for v in range(4):
            dens = _puff_density(101 + 17 * v + 500 * kind_row, kind)
            for k, (light, amb, phase) in enumerate(((front, 0.55, 1.0), (back, 0.34, 2.4))):
                col, a = _render_puff(dens, light, amb, phase)
                if exposure is None:
                    exposure = 1.0 / max(np.percentile(col[a > 0.3], 97.0), 1e-3)
                r = kind_row + k
                lum_img[r * cell:(r + 1) * cell, v * cell:(v + 1) * cell] = np.clip(col * exposure, 0, 1)
                alpha_img[r * cell:(r + 1) * cell, v * cell:(v + 1) * cell] = np.clip(a, 0, 1)

    def index(lum, a):
        lq = np.clip(np.round(lum * 15), 0, 15).astype(np.uint8)
        aq = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
        return lq * 16 + aq

    levels, lum, a = [], lum_img, alpha_img
    for i in range(ftx.mip_levels(512, 512, 4)):
        if i:
            # Average colour weighted by alpha, so dark empty texels don't grey the edges.
            la = ftx.downsample((lum * a)[..., None])[..., 0]
            a = ftx.downsample(a[..., None])[..., 0]
            lum = np.where(a > 1e-3, la / np.maximum(a, 1e-3), 0.0)
        levels.append(index(lum, a))
    shadow, white = np.array([0.42, 0.47, 0.56]), np.array([1.0, 1.0, 1.0])
    clut = np.array([np.append(shadow + (white - shadow) * (l / 15), al / 15)
                     for l in range(16) for al in range(16)])
    ftx.export_indexed(os.path.join(OUT, "cloud.ftx"), levels, clut)
    rgba = clut[levels[0]]
    sky = np.array([0.38, 0.56, 0.80])
    write_png(os.path.join(PREVIEW, "env_cloud.png"), rgba[..., :3] * rgba[..., 3:] + sky * (1 - rgba[..., 3:]))


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
    sea_detail()
    sea_foam()
    deck()
    deck_base()
    deck_storm()
    deck_scud()
    cloud()
    sun()
    for name in ("sea_detail", "sea_foam", "deck", "cloud", "sun"):
        print(f"[env] {name}.ftx {os.path.getsize(os.path.join(OUT, name + '.ftx'))} bytes")
