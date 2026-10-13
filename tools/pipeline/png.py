"""Minimal PNG writer (zlib + struct) for previews without PIL."""

import struct
import zlib

import numpy as np


def _chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, img):
    """img: HxWx3 or HxWx4 float 0..1 (row 0 = top)."""
    arr = np.clip(np.round(np.asarray(img) * 255), 0, 255).astype(np.uint8)
    h, w, c = arr.shape
    color_type = {3: 2, 4: 6}[c]
    raw = b"".join(b"\0" + arr[y].tobytes() for y in range(h))
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0)))
        fh.write(_chunk(b"IDAT", zlib.compress(raw, 9)))
        fh.write(_chunk(b"IEND", b""))
