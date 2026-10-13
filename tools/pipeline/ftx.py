"""FTX2 texture writer shared by all asset tools (numpy only, no Blender).

Format spec: docs/formats/ftx2.md. T8 indices + 256-entry ABGR8888 CLUT,
mip chain, each level swizzled for the PSP GE (docs/psp/gotchas.md, #6).
"""

import struct

import numpy as np

PSM_T8 = 5


def nearest(pix, centers):
    """Index of the closest palette entry for every row of `pix` (chunked)."""
    out = np.empty(len(pix), np.int32)
    c2 = (centers ** 2).sum(1)
    for i in range(0, len(pix), 32768):
        p = pix[i:i + 32768]
        out[i:i + 32768] = np.argmin(c2[None, :] - 2 * p @ centers.T, axis=1)
    return out


def kmeans_palette(img, k=256, iters=14, seed=7, samples=80000):
    """k-means palette over the last axis of `img` (RGB or RGBA, 0..1)."""
    pix = img.reshape(-1, img.shape[-1])
    rng = np.random.default_rng(seed)
    sample = pix[rng.choice(len(pix), min(samples, len(pix)), replace=False)]
    centers = sample[rng.choice(len(sample), k, replace=len(sample) < k)].copy()
    for _ in range(iters):
        lab = nearest(sample, centers)
        counts = np.bincount(lab, minlength=k)
        for ch in range(pix.shape[1]):
            sums = np.bincount(lab, weights=sample[:, ch], minlength=k)
            centers[:, ch] = np.where(counts > 0, sums / np.maximum(counts, 1), centers[:, ch])
        empty = counts == 0
        centers[empty] = sample[rng.choice(len(sample), int(empty.sum()))]
    return centers


def downsample(img):
    return 0.25 * (img[0::2, 0::2] + img[1::2, 0::2] + img[0::2, 1::2] + img[1::2, 1::2])


def swizzle(level):
    """16-byte x 8-row blocks stored contiguously (T8: 16x8 texels)."""
    h, w = level.shape
    assert w % 16 == 0 and h % 8 == 0, f"level {w}x{h} too small to swizzle"
    return level.reshape(h // 8, 8, w // 16, 16).transpose(0, 2, 1, 3).reshape(-1)


def mip_levels(width, height, max_levels):
    """Levels down to 16 texels wide (smallest swizzlable T8 width)."""
    n = 1
    while n < max_levels and (width >> n) >= 16 and (height >> n) >= 8:
        n += 1
    return n


def write_ftx(path, levels, clut):
    """levels: list of uint8 index arrays (level 0 first); clut: (256, 4) RGBA 0..1."""
    h, w = levels[0].shape
    rgba = np.clip(np.round(np.asarray(clut, np.float64) * 255), 0, 255).astype(np.uint32)
    if rgba.shape[1] == 3:
        rgba = np.concatenate([rgba, np.full((len(rgba), 1), 255, np.uint32)], 1)
    assert len(rgba) == 256
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sHHBBBBHH", b"FTX2", w, h, PSM_T8, len(levels), 1, 0, 256, 0))
        for r, g, b, a in rgba:
            fh.write(struct.pack("<I", (int(a) << 24) | (int(b) << 16) | (int(g) << 8) | int(r)))
        for lv in levels:
            fh.write(swizzle(lv).tobytes())


def export_quantized(path, img, max_levels=4, k=256):
    """Quantise an RGB(A) float image with k-means and write it with mips.
    Returns the level-0 image as the PSP will show it."""
    centers = kmeans_palette(img, k)
    n = mip_levels(img.shape[1], img.shape[0], max_levels)
    levels, cur = [], img
    for i in range(n):
        if i:
            cur = downsample(cur)
        levels.append(nearest(cur.reshape(-1, img.shape[-1]), centers).reshape(cur.shape[:2]).astype(np.uint8))
    write_ftx(path, levels, centers)
    return centers[levels[0]]


def export_indexed(path, index_levels, clut):
    """Write pre-indexed levels with a hand-built palette (e.g. alpha ramps)."""
    write_ftx(path, index_levels, clut)
