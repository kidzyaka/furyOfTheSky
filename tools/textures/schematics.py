"""Side-view schematics of the vehicles for the profile and the shop.

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/textures/schematics.py

Writes assets/ui/schematics.ftx - FTX2 T8 256x256, one level, alpha-only
(CLUT index = alpha, colour constant white: tinted by the vertex colour) -
and previews build/previews/ui_schematics.png (+ _2x), white on dark.

Four cells of 256x64, row-major from the top, nose to the right, each
vehicle fitted into 240x56 px and standing on a common ground line (its
lowest pixel row is 59, i.e. y1 = 60 in the cell):
  0  Su-35S, blueprint line art from the real game mesh (tools/models/su35s.py):
     2 px outline, 1 px interior lines (part borders: canopy, intake, LERX,
     fins, rudders, stabilators, nozzles, slats/flaperons; windscreen arch;
     occluding contours and creases), faint fill (alpha 0.18)
  1  Su-57, solid silhouette (polygon outline after published profiles)
  2  Ka-52, solid silhouette, coaxial rotors edge-on as two thin blades
  3  Su-35S, solid silhouette (the same outline as cell 0)
The pixel bounds of every vehicle are printed and listed in
docs/assets/ui-art.md.

Su-35S: the mesh is ray cast orthographically from +X (its right side) on a
4x supersampled grid; only the right half is modelled for paired parts, and
in a side projection the left half lies exactly behind it. Pylons and
launchers are left out (clean airframe).
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "pipeline"))
sys.path.insert(0, os.path.join(HERE, "..", "models"))

import bpy  # noqa: E402,F401  (fmodel needs Blender)
import numpy as np  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import fmodel  # noqa: E402
import ftx  # noqa: E402
from png import write_png  # noqa: E402

OUT = os.path.join(ROOT, "assets", "ui")
PREVIEW = os.path.join(ROOT, "build", "previews")

CELL_W, CELL_H = 256, 64
BOX_W, BOX_H = 240, 56          # the vehicle is fitted into this
GROUND = 60                     # y1 (exclusive) of every vehicle in its cell
SS = 4                          # supersampling of the rasters

FILL_ALPHA = 0.18
LINE_ALPHA = 0.85
OUTLINE_PX = 2
SKIP_PARTS = ("launcher", "pylon")
# Part borders inside one group draw no line: smooth skin joins, and the
# wing's slats / flaperons / tip rail, which a side view sees edge-on (their
# hinge lines would only thicken the wing's sliver into a band).
GROUPS = {"spine": "fuselage", "pitot": "fuselage", "slat": "wing", "flap": "wing", "rail": "wing"}
WINDSCREEN_Y = 6.66             # canopy frame arch, su35s.paint()
RADOME_Y = 8.0                  # radome joint, su35s.RADOME_END
CANOPY_SILL_Z = 0.80            # canopy frame below this, su35s.paint()


# ================================================================ fitting

def fit(x0, x1, z0, z1):
    """Scale (px/m) and the mapping of model (x along the body, nose = +x;
    z up) to SS-pixel coordinates of a cell: fitted into BOX, centred
    horizontally, lowest point on the ground line."""
    s = min(BOX_W / (x1 - x0), BOX_H / (z1 - z0))
    off_x = (CELL_W - (x1 - x0) * s) / 2

    def to_px(x, z):
        return ((x - x0) * s + off_x) * SS, (GROUND - (z - z0) * s) * SS

    def to_model(px, py):
        return px / SS / s + x0 - off_x / s, z0 + (GROUND - py / SS) / s

    return s, to_px, to_model


def downsample(a):
    h, w = a.shape
    return a.reshape(h // SS, SS, w // SS, SS).mean(axis=(1, 3))


def erode(mask, n):
    """n steps of 4/8-neighbour erosion alternated (~round band)."""
    m = mask.copy()
    for i in range(n):
        p = np.pad(m, 1, constant_values=False)
        e = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
        if i % 2:
            e &= p[:-2, :-2] & p[:-2, 2:] & p[2:, :-2] & p[2:, 2:]
        m = e
    return m


def line_coverage(marks):
    """1-SS-pixel line marks -> final alpha: a line crossing a block marks
    ~SS of its pixels, so count / SS gives crisp 1 px lines with AA ends."""
    h, w = marks.shape
    return np.clip(marks.reshape(h // SS, SS, w // SS, SS).sum(axis=(1, 3)) / SS, 0, 1)


# ================================================================ Su-35S

def su35s_rasters():
    """Ray cast the right side of the Su-35S mesh. Returns (mask, part id,
    depth, normal, model-space y/z per SS pixel, part names, fit)."""
    import su35s
    fmodel.TEX = getattr(su35s, "TEX", 512)
    mb = su35s.build()
    bm = mb.bm
    lp = bm.faces.layers.int["part"]
    names = [p.name for p in mb.parts]
    skip = [f for f in bm.faces if names[f[lp]] in SKIP_PARTS]
    import bmesh
    bmesh.ops.delete(bm, geom=skip, context="FACES")
    bm.faces.ensure_lookup_table()
    bm.verts.ensure_lookup_table()
    co = np.array([v.co[:] for v in bm.verts])
    y0, y1 = co[:, 1].min(), co[:, 1].max()       # Blender y: nose = +y
    z0, z1 = co[:, 2].min(), co[:, 2].max()
    s, to_px, to_model = fit(y0, y1, z0, z1)
    print(f"[schem] su35s: length {y1 - y0:.2f} m, height {z1 - z0:.2f} m, {s:.2f} px/m")
    bvh = BVHTree.FromBMesh(bm)
    face_part = np.array([f[lp] for f in bm.faces])
    h, w = CELL_H * SS, CELL_W * SS
    mask = np.zeros((h, w), bool)
    part = np.full((h, w), -1, np.int64)
    depth = np.zeros((h, w))
    normal = np.zeros((h, w, 3))
    my = np.zeros((h, w))
    mz = np.zeros((h, w))
    px0, py1 = to_px(y0, z0)
    px1, py0 = to_px(y1, z1)
    from mathutils import Vector
    down = Vector((-1.0, 0.0, 0.0))
    for py in range(max(int(py0) - 2, 0), min(int(py1) + 3, h)):
        for px in range(max(int(px0) - 2, 0), min(int(px1) + 3, w)):
            yy, zz = to_model(px + 0.5, py + 0.5)
            loc, nrm, idx, _ = bvh.ray_cast(Vector((60.0, yy, zz)), down)
            if loc is None:
                continue
            mask[py, px] = True
            part[py, px] = face_part[idx]
            depth[py, px] = loc.x
            normal[py, px] = nrm[:]
            my[py, px], mz[py, px] = yy, zz
    return mask, part, depth, normal, my, mz, names, s


def su35s_cells():
    mask, part, depth, normal, my, mz, names, scale = su35s_rasters()
    group = np.array([names.index(GROUPS.get(n, n)) for n in names] + [-1])
    g = group[part]                                  # part -1 -> group -1 (last entry)
    marks = np.zeros_like(mask)
    for dy, dx in ((0, 1), (1, 0)):
        a = (slice(0, mask.shape[0] - dy), slice(0, mask.shape[1] - dx))
        b = (slice(dy, None), slice(dx, None))
        both = mask[a] & mask[b]
        crease = (normal[a] * normal[b]).sum(-1) < math.cos(math.radians(60))
        step = np.abs(depth[a] - depth[b]) > 0.5      # occluding contour
        edge = both & ((g[a] != g[b]) | crease | step)
        # Mark the nearer of the two pixels (larger x = closer to the viewer).
        near_a = depth[a] >= depth[b]
        marks[a] |= edge & near_a
        marks[b] |= edge & ~near_a
    canopy = part == names.index("canopy")
    arch = canopy & (np.abs(my - WINDSCREEN_Y) < 0.5 / (scale * SS))
    sill = canopy & (np.abs(mz - CANOPY_SILL_Z) < 0.5 / (scale * SS))
    radome = (part == names.index("fuselage")) & (np.abs(my - RADOME_Y) < 0.5 / (scale * SS))
    marks |= arch | sill | radome
    band = mask & ~erode(mask, OUTLINE_PX * SS)
    cov = downsample(mask.astype(float))
    lines = line_coverage(marks & ~band)
    outline = downsample(band.astype(float))
    blueprint = np.maximum.reduce([cov * FILL_ALPHA, lines * LINE_ALPHA, outline])
    return blueprint, cov


# ================================================================ polygons

def catmull(points, subdiv=6):
    """Closed Catmull-Rom curve through the points (smooth outlines)."""
    p = np.asarray(points, float)
    n = len(p)
    out = []
    for i in range(n):
        p0, p1, p2, p3 = p[i - 1], p[i], p[(i + 1) % n], p[(i + 2) % n]
        for k in range(subdiv):
            t = k / subdiv
            t2, t3 = t * t, t * t * t
            out.append(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (3 * p1 - p0 - 3 * p2 + p3) * t3))
    return np.array(out)


def ellipse(cx, cz, rx, rz, n=24):
    a = np.linspace(0, 2 * math.pi, n, endpoint=False)
    return np.stack([cx + rx * np.cos(a), cz + rz * np.sin(a)], 1)


def blade(x0, x1, z, t_root, t_tip, cone=0.0, n=24):
    """A rotor seen edge-on: a thin lens from tip to tip, thickest at the hub,
    tips raised by `cone` (m)."""
    c = 0.5 * (x0 + x1)
    xs = np.linspace(x0, x1, n)
    u = np.abs(xs - c) / (0.5 * (x1 - x0))
    th = t_root + (t_tip - t_root) * u
    zc = z + cone * u ** 2
    top = np.stack([xs, zc + th / 2], 1)
    bot = np.stack([xs[::-1], (zc - th / 2)[::-1]], 1)
    return np.concatenate([top, bot])


def raster_polys(polys):
    """Union of polygons (model metres, x = nose-wards) -> SS mask, fitted."""
    allp = np.concatenate(polys)
    s, to_px, to_model = fit(allp[:, 0].min(), allp[:, 0].max(), allp[:, 1].min(), allp[:, 1].max())
    h, w = CELL_H * SS, CELL_W * SS
    yy, xx = np.mgrid[0:h, 0:w] + 0.5
    mx, mz = to_model(xx, yy)
    mask = np.zeros((h, w), bool)
    for poly in polys:
        inside = np.zeros((h, w), bool)
        n = len(poly)
        for i in range(n):
            (xa, za), (xb, zb) = poly[i], poly[(i + 1) % n]
            if za == zb:
                continue
            cross = ((za > mz) != (zb > mz)) & (mx < xa + (mz - za) * (xb - xa) / (zb - za))
            inside ^= cross
        mask |= inside
    return mask, s, allp


def su57_polys():
    """Su-57, flight configuration (gear up), right side, metres: x from the
    tail stinger tip (0) to the nose (20.1), z from the intake bottom.
    Built from published side profiles and the commonly quoted length 20.1 m;
    the airframe stands 3.4 m tall without gear (4.6-4.74 m quoted on gear).
    Proportions are approximate (see docs/assets/ui-art.md)."""
    body = catmull([
        (20.10, 1.10), (19.55, 1.30), (18.70, 1.52), (17.70, 1.74), (16.80, 1.90),
        (15.00, 2.04), (12.60, 2.10), (10.40, 2.05), (8.00, 1.98), (6.20, 1.95),
        (4.20, 1.86), (2.80, 1.72), (1.70, 1.52), (1.45, 1.10), (1.45, 0.45),
        (2.20, 0.16), (3.60, 0.03), (5.20, 0.00), (9.00, 0.00), (12.80, 0.00),
        (13.62, 0.04), (13.40, 0.55), (13.25, 0.90), (14.40, 0.64), (16.00, 0.62),
        (17.50, 0.72), (18.80, 0.86), (19.70, 1.00)], 4)
    canopy = catmull([(16.85, 1.88), (16.25, 2.28), (15.45, 2.50), (14.60, 2.57), (13.75, 2.49),
                      (12.95, 2.30), (12.35, 2.08), (13.50, 1.98), (15.50, 1.92)], 6)
    fin = np.array([(6.60, 1.92), (4.70, 3.40), (3.62, 3.40), (3.08, 1.82)])
    stab = np.array([(5.30, 1.34), (1.05, 1.30), (0.90, 1.22), (5.10, 1.17)])
    nozzle = np.array([(2.90, 1.16), (1.05, 1.07), (0.95, 0.98), (0.95, 0.36), (1.05, 0.28), (2.90, 0.14)])
    sting = np.array([(3.20, 1.72), (1.20, 1.55), (0.00, 1.38), (1.20, 1.20), (3.20, 1.05)])
    irst = ellipse(17.10, 1.86, 0.18, 0.13)
    return [body, canopy, fin, stab, nozzle, sting, irst]


def ka52_polys():
    """Ka-52, gear retracted, right side, metres: x from the tail (0) to the
    radar nose (13.5), z from the belly. Coaxial 14.5 m rotors edge-on on the
    mast at x 8.6, so the rotor disc reaches 2.35 m past the nose (overall
    ~15.9 m, as quoted "with rotors turning"). Proportions approximate."""
    body = catmull([
        (13.50, 1.05), (13.35, 1.42), (12.95, 1.70), (12.45, 1.86), (12.10, 2.25),
        (11.60, 2.55), (10.90, 2.66), (10.10, 2.62), (9.60, 2.46), (9.25, 2.56),
        (8.90, 2.86), (8.00, 2.96), (6.80, 2.91), (6.00, 2.72), (5.30, 2.36),
        (4.50, 2.06), (2.50, 1.86), (1.10, 1.76), (0.15, 1.70), (0.00, 1.55),
        (0.20, 1.36), (2.50, 1.34), (4.40, 1.14), (5.20, 0.66), (5.80, 0.25),
        (6.50, 0.05), (7.20, 0.00), (10.80, 0.00), (11.60, 0.14), (12.20, 0.36),
        (12.90, 0.62), (13.35, 0.82)], 4)
    turret = ellipse(12.30, 0.30, 0.31, 0.30)
    fin = np.array([(2.25, 1.82), (0.75, 2.95), (0.15, 2.95), (0.05, 1.62)])
    endplate = np.array([(1.90, 2.22), (1.30, 2.22), (1.05, 1.46), (1.65, 1.46)])
    stab = np.array([(2.40, 1.80), (0.90, 1.78), (0.90, 1.70), (2.40, 1.70)])
    wing = np.array([(8.70, 1.62), (6.60, 1.58), (6.60, 1.40), (8.70, 1.44)])
    pod = catmull([(8.55, 1.55), (8.25, 1.78), (6.90, 1.78), (6.45, 1.55), (6.90, 1.26), (8.25, 1.26)], 3)
    mast_x, low_z, up_z = 8.60, 3.15, 4.30
    mast = np.array([(mast_x + 0.30, 2.90), (mast_x + 0.17, up_z), (mast_x - 0.17, up_z), (mast_x - 0.30, 2.90)])
    low_hub = ellipse(mast_x, low_z, 0.70, 0.17)
    up_hub = ellipse(mast_x, up_z, 0.58, 0.15)
    cap = catmull([(mast_x + 0.35, up_z + 0.05), (mast_x + 0.22, up_z + 0.22), (mast_x, up_z + 0.27),
                   (mast_x - 0.22, up_z + 0.22), (mast_x - 0.35, up_z + 0.05)], 3)
    r = 7.25
    low = blade(mast_x - r, mast_x + r, low_z, 0.13, 0.06, 0.10)
    up = blade(mast_x - r, mast_x + r, up_z, 0.13, 0.06, 0.10)
    return [body, turret, fin, endplate, stab, wing, pod, mast, low_hub, up_hub, cap, low, up]


# ================================================================ output

def bounds(alpha, thresh=8 / 255):
    ys, xs = np.nonzero(alpha >= thresh)
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    blueprint, solid35 = su35s_cells()
    m57, s57, p57 = raster_polys(su57_polys())
    print(f"[schem] su57: length {np.ptp(p57[:, 0]):.2f} m, height {np.ptp(p57[:, 1]):.2f} m, {s57:.2f} px/m")
    m52, s52, p52 = raster_polys(ka52_polys())
    print(f"[schem] ka52: length {np.ptp(p52[:, 0]):.2f} m, height {np.ptp(p52[:, 1]):.2f} m, {s52:.2f} px/m")
    cells = [blueprint, downsample(m57.astype(float)), downsample(m52.astype(float)), solid35]
    atlas = np.concatenate(cells, 0)
    for i, (name, c) in enumerate(zip(("su35s_lines", "su57", "ka52", "su35s"), cells)):
        x0, y0, x1, y1 = bounds(c)
        print(f"[schem] cell {i} {name}: x0 {x0} y0 {y0} x1 {x1} y1 {y1} (x1/y1 exclusive)")
    level = np.clip(np.round(atlas * 255), 0, 255).astype(np.uint8)
    t = np.arange(256) / 255
    clut = np.concatenate([np.ones((256, 3)), t[:, None]], 1)
    path = os.path.join(OUT, "schematics.ftx")
    ftx.export_indexed(path, [level], clut)
    print(f"[schem] assets/ui/schematics.ftx {os.path.getsize(path)} bytes")
    a = (level / 255.0)[..., None]
    bg = np.array([0.07, 0.09, 0.11])
    img = bg * (1 - a) + a
    # Cell borders in the preview only (every 64 rows).
    img[63::64] = img[63::64] * 0.6 + np.array([0.25, 0.3, 0.35]) * 0.4
    write_png(os.path.join(PREVIEW, "ui_schematics.png"), img)
    write_png(os.path.join(PREVIEW, "ui_schematics_2x.png"), np.repeat(np.repeat(img, 2, 0), 2, 1))


for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
main()
