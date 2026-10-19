"""FTR2 (docs/formats/ftr2.md, docs/decisions/0050): rock and island meshes as
the GE reads them fastest - 16 bytes a vertex, no normals, no GE lights.

FTR1 vertices were floats with a normal (36 bytes) lit by the GE's sun light.
Rock is most of the vertices in view and the GE transforms (and lit) every
index it is sent, so the vertex is shrunk to 16-bit texture coordinates and
positions (scaled back by the material's texture scale/offset and the model
matrix) and the light is baked into the vertex colour per placed rock when a
sortie starts (gfx::TerrainMesh::bake). The normal and the albedo stay in the
file, in a separate array the GE never reads, for that bake.

Usage: python3 tools/models/ftr2.py assets/models/rock_*.ftr
Also called by rocks.py after write_ftr. In place; FTR2 files are left alone.
"""

import struct
import sys

import numpy as np

V1 = np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("n", "<f4", 3), ("p", "<f4", 3)])
# GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT: 4 + 4 + 6, padded to 16.
V2 = np.dtype([("u", "<u2"), ("v", "<u2"), ("c", "<u4"), ("p", "<i2", 3), ("pad", "<i2")])
BAKE = np.dtype([("n", "i1", 3), ("pad", "u1"), ("albedo", "<u4")])
assert V1.itemsize == 36 and V2.itemsize == 16 and BAKE.itemsize == 8


def ge_float(x):
    """The value the GE keeps: the top 24 bits of the float (pspsdk sends
    float commands as bits >> 8)."""
    bits = struct.unpack("<I", struct.pack("<f", float(x)))[0] & 0xFFFFFF00
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def convert(path):
    data = open(path, "rb").read()
    if data[:4] == b"FTR2":
        return None
    assert data[:4] == b"FTR1", path
    chunks, nv, ni, roots, mats, radius = struct.unpack_from("<IIIHHf", data, 4)
    names = [data[32 + 16 * i:48 + 16 * i] for i in range(mats)]
    table = 32 + 16 * mats
    records = data[table:table + 64 * chunks]
    v1 = np.frombuffer(data, V1, nv, table + 64 * chunks)
    iat = table + 64 * chunks + 36 * nv
    indices = data[iat:iat + 2 * ni]

    material = np.zeros(nv, np.int32)
    for c in range(chunks):
        first, = struct.unpack_from("<I", records, 64 * c + 24)
        count, = struct.unpack_from("<H", records, 64 * c + 32)
        mat, = struct.unpack_from("<H", records, 64 * c + 38)
        material[first:first + count] = mat

    # Positions: s16 / 32768 * scale per axis (the model matrix carries the scale).
    p = v1["p"].astype(np.float64)
    scale = [float(np.float32(max(np.abs(p[:, k]).max(), 1.0) * 32768.0 / 32760.0)) for k in range(3)]
    q = np.round(p / np.array(scale) * 32768.0)
    assert q.min() >= -32768 and q.max() <= 32767, path

    # Texture coordinates: u16 / 32768 * scale + offset per material
    # (sceGuTexScale / sceGuTexOffset), the values exactly as the GE keeps them.
    uv = np.zeros((nv, 2))
    uvx = []
    for m in range(mats):
        sel = material == m
        rec = []
        for k, key in enumerate(("u", "v")):
            t = v1[key][sel].astype(np.float64)
            lo, hi = (t.min(), t.max()) if len(t) else (0.0, 0.0)
            off = ge_float(lo - 1e-4 * (abs(lo) + 1.0))  # truncation moves it towards zero: start below
            s = ge_float(max(hi - off, 1e-3) / 1.9998)   # top: 1.9998 * 32768 < 65535
            uv[sel, k] = np.round((t - off) / s * 32768.0)
            rec.append((s, off))
        uvx.append(rec)
    assert uv.min() >= 0 and uv.max() <= 65535, path

    v2 = np.zeros(nv, V2)
    v2["u"], v2["v"] = uv[:, 0], uv[:, 1]
    v2["c"] = v1["c"]
    v2["p"] = q
    bake = np.zeros(nv, BAKE)
    bake["n"] = np.clip(np.round(v1["n"] * 127.0), -127, 127)
    bake["albedo"] = v1["c"]

    # Round trip: what the GE will draw against what FTR1 had.
    back = v2["p"].astype(np.float64) / 32768.0 * np.array(scale)
    perr = np.abs(back - p).max()
    uverr = 0.0
    for m in range(mats):
        sel = material == m
        for k, key in enumerate(("u", "v")):
            s, off = uvx[m][k]
            t = v2[key][sel].astype(np.float64) / 32768.0 * s + off
            if len(t):
                uverr = max(uverr, np.abs(t - v1[key][sel]).max())
    assert perr <= max(scale) / 32768.0 and uverr < 0.01, f"{path}: error {perr:.3f} m, uv {uverr:.4f}"

    out = bytearray()
    out += struct.pack("<4sIIIHHf8x", b"FTR2", chunks, nv, ni, roots, mats, radius)
    out += struct.pack("<3f4x", *scale)
    for m in range(mats):
        (su, ou), (sv, ov) = uvx[m]
        out += names[m] + struct.pack("<4f", su, sv, ou, ov)
    out += records
    assert len(out) % 16 == 0
    out += v2.tobytes()
    out += bake.tobytes()
    out += indices
    out += b"\0" * ((-len(out)) % 16)
    open(path, "wb").write(bytes(out))
    print(f"[ftr2] {path}: {nv} vertices, {len(data) // 1024} -> {len(out) // 1024} KB, "
          f"position error {perr * 100:.1f} cm, uv {uverr:.4f}")
    return len(out)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        convert(p)
