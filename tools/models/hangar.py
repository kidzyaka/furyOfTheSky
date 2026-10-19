"""Hardened aircraft shelter (interior) behind the menus: an arched Soviet /
Russian shelter after the AU-19 (2A19) type built around the Su-27 family,
widened for the menu cameras (docs/assets/hangar.md, ADR 0036).

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/hangar.py [-- --samples N] [-- --no-preview]

Writes assets/models/hangar.fsm + hangar.ftx (arch, walls, props, lamps,
apron and the daylight backdrop outside the door) and hangar_floor.fsm +
hangar_floor.ftx (the floor slabs), previews build/previews/hangar_*.png and
the matching dynamic lights build/hangar_lights.txt.

Coordinates: the aircraft's model space (Blender x right, y nose, z up;
game = (x, z, -y), ADR 0007). Aircraft origin = world origin, floor at
z = GROUND_Y (su35s_gear.py), the open door at +y (game -Z), where the
nose points.

Lighting is baked (Cycles: diffuse direct + indirect, no colour) and
multiplied by the procedural albedo here, so the PSP draws the shelter with
GE lighting off: texture x white. The aircraft is not in the bake (no shadow
of it on the floor: the game draws a blob). GE limits honoured
(docs/psp/gotchas.md #5, #15, #19): faces wind CCW seen from inside; floor
cells <= 2.5 m and wall/arch cells <= 3 m, so no clipped triangle that runs
past a camera projects outside the 4096 px guard band.
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

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import ftx  # noqa: E402
import png  # noqa: E402
from fmodel import fbm, smoothstep  # noqa: E402
from su35s_gear import GROUND_Y, MAIN_X, MAIN_Y, NOSE_Y  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def _arg(name, default):
    return type(default)(ARGS[ARGS.index(name) + 1]) if name in ARGS else default


# ================================================================ dimensions

F = GROUND_Y                 # floor (Blender z = game y)
A, B = 12.0, 11.0            # semi-elliptic arch: half-width at the floor, crown height
Y_BACK, Y_FRONT = -19.0, 21.0  # inner faces of the back wall and of the front (door) wall
WALL_T = 0.8                 # front wall thickness: the door reveal
OPEN_HW, OPEN_H = 9.0, 6.4   # door opening: 18 x 6.4 m (span 15.3, fins 5.9)
ARC_SEG = 16                 # arch polygon, ~2.2 m a segment
ROWS = 16                    # arch rows along y, 2.5 m
LAMPS = [(sx * 4.6, y) for y in (-12.5, -3.5, 5.5, 14.5) for sx in (-1, 1)]
LAMP_Z = F + 7.85            # lens of the pendant reflector lamps
TRAY_H = 3.2                 # cable trays along both walls
SKY_R = 75.0                 # backdrop band radius around the door
TEX = 512

# Daylight: sun ahead of the door, from the right, 48 deg up (its patch on the
# floor ends at y ~ 16, short of the radome at 11.7: the aircraft is lit only
# by sky and bounce, which the game's key light reproduces).
SUN_DIR = Vector((0.384, 0.548, 0.743)).normalized()      # towards the sun
SUN_STRENGTH = 5.5
SUN_COLOR = (1.0, 0.95, 0.87)
SKY_COLOR = (0.50, 0.66, 0.92)
SKY_STRENGTH = 2.6
LAMP_POWER = 420.0           # W per lamp (Cycles), warm incandescent / DRL
LAMP_COLOR = (1.0, 0.80, 0.58)
EXPOSURE = 1.55
CLEAR_COLOR = (178, 204, 232)  # GE clear colour behind the backdrop (sky top)


def tonemap(x):
    """Linear up to 0.6, then a soft shoulder to 1: the interior stays
    linear (the GE lights match it), the daylight outside clips gently."""
    return np.where(x < 0.6, x, 0.6 + 0.4 * (1.0 - np.exp(-(x - 0.6) / 0.4)))


def srgb_encode(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def srgb_decode(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.04045, x / 12.92, np.power((x + 0.055) / 1.055, 2.4))


# ================================================================ geometry

class Geo:
    """Polygons with per-corner UVs (metres x density weight, per island),
    a material per face, wound CCW seen from the visible side."""

    def __init__(self):
        self.verts, self.faces = [], []
        self._key = {}
        self.islands = 0

    def v(self, p, share=None):
        p = Vector(p)
        if share is not None:
            k = (share, round(p.x, 4), round(p.y, 4), round(p.z, 4))
            if k in self._key:
                return self._key[k]
            self._key[k] = len(self.verts)
        self.verts.append(p)
        return len(self.verts) - 1

    def island(self):
        self.islands += 1
        return self.islands

    def face(self, idx, uvs, mat, want, isl):
        keep = [k for k in range(len(idx)) if idx[k] != idx[k - 1]]
        idx, uvs = [idx[k] for k in keep], [uvs[k] for k in keep]
        if len(idx) < 3:
            return
        p = [self.verts[i] for i in idx]
        n = Vector()
        for a, b in zip(p, p[1:] + p[:1]):                       # Newell normal
            n += Vector(((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)))
        if n.length < 1e-9:
            return
        if n.dot(Vector(want)) < 0:
            idx, uvs, n = idx[::-1], uvs[::-1], -n
        self.faces.append(dict(idx=idx, uv=[tuple(u) for u in uvs], mat=mat, isl=isl, n=n.normalized()))

    def grid(self, pos, ni, nj, mat, uv, want, isl=None, share="g"):
        isl = isl or self.island()
        tag = f"{share}{isl}"
        ids = [[self.v(pos(i, j), tag) for j in range(nj + 1)] for i in range(ni + 1)]
        for i in range(ni):
            for j in range(nj):
                c = (pos(i, j) + pos(i + 1, j + 1)) * 0.5
                self.face([ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]],
                          [uv(i, j), uv(i + 1, j), uv(i + 1, j + 1), uv(i, j + 1)], mat, want(c), isl)
        return isl


def planar_face(geo, pts, mat, want, w, isl=None):
    """One flat polygon, its own UV island (metres x w)."""
    pts = [Vector(p) for p in pts]
    e1 = (pts[1] - pts[0]).normalized()
    n = Vector(want).normalized()
    e2 = n.cross(e1)
    isl = isl or geo.island()
    idx = [geo.v(p) for p in pts]
    geo.face(idx, [((p - pts[0]).dot(e1) * w, (p - pts[0]).dot(e2) * w) for p in pts], mat, want, isl)


def box(geo, center, size, mat, w=2.5, rot=None, skip=()):
    """Box with outward faces, each face its own island; skip: face names
    (-z, +z, -x, +x, -y, +y) that are hidden (on the floor, on a wall)."""
    c, (sx, sy, sz) = Vector(center), size
    rot = rot or Matrix.Identity(3)

    def P(x, y, z):
        return c + rot @ Vector((x * sx / 2, y * sy / 2, z * sz / 2))
    faces = {"-z": [(-1, -1, -1), (-1, 1, -1), (1, 1, -1), (1, -1, -1)],
             "+z": [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)],
             "-x": [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)],
             "+x": [(1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)],
             "-y": [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)],
             "+y": [(-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1)]}
    for name, corners in faces.items():
        if name in skip:
            continue
        pts = [P(*k) for k in corners]
        axis = {"x": 0, "y": 1, "z": 2}[name[1]]
        out = Vector([0, 0, 0])
        out[axis] = 1.0 if name[0] == "+" else -1.0
        planar_face(geo, pts, mat, rot @ out, w)


def cylinder(geo, p0, p1, r0, r1, seg, mat, w=2.5, caps=(True, True), cap_mat=None):
    """Truncated cone p0 -> p1 (outward), side one island, caps planar."""
    p0, p1 = Vector(p0), Vector(p1)
    ax = (p1 - p0).normalized()
    u = ax.orthogonal().normalized()
    v = ax.cross(u)
    ring = [[p + (u * math.cos(2 * math.pi * i / seg) + v * math.sin(2 * math.pi * i / seg)) * r
             for i in range(seg + 1)] for p, r in ((p0, r0), (p1, r1))]
    isl = geo.island()
    length = (p1 - p0).length
    for i in range(seg):
        quad = [ring[0][i], ring[0][i + 1], ring[1][i + 1], ring[1][i]]
        s0, s1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        rr = max(r0, r1)
        uvs = [(s0 * rr * w, 0), (s1 * rr * w, 0), (s1 * rr * w, length * w), (s0 * rr * w, length * w)]
        mid = sum(quad, Vector()) / 4
        foot = p0 + ax * (mid - p0).dot(ax)
        geo.face([geo.v(q) for q in quad], uvs, mat, mid - foot, isl)
    for k, (flag, p, r) in enumerate(((caps[0], p0, r0), (caps[1], p1, r1))):
        if flag:
            pts = ring[k][:seg]
            planar_face(geo, pts, cap_mat or mat, -ax if k == 0 else ax, w)


# ---------------------------------------------------------------- the arch

def arch_profile():
    """Polygon of the arch cross-section from x = +A to -A: equal arc length
    points plus the door edges x = +-OPEN_HW (so the front wall needs no
    T-junction). Returns [(x, z)] and arc length s at each point."""
    ts = np.linspace(0.0, math.pi, 4001)
    xs, zs = A * np.cos(ts), B * np.sin(ts)
    cum = np.concatenate([[0.0], np.cumsum(np.hypot(np.diff(xs), np.diff(zs)))])
    t = np.interp(np.linspace(0, cum[-1], ARC_SEG + 1), cum, ts)
    pts = [(A * math.cos(a), B * math.sin(a)) for a in t]
    out = [pts[0]]
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        for xe in (OPEN_HW, -OPEN_HW):
            if (x0 - xe) * (x1 - xe) < 0:
                k = (xe - x0) / (x1 - x0)
                out.append((xe, z0 + (z1 - z0) * k))
        out.append((x1, z1))
    s = [0.0]
    for (x0, z0), (x1, z1) in zip(out, out[1:]):
        s.append(s[-1] + math.hypot(x1 - x0, z1 - z0))
    return [(x, F + z) for x, z in out], s


def arch_top(x):
    return F + B * math.sqrt(max(0.0, 1.0 - (x / A) ** 2))


def build_shell():
    g = Geo()
    prof, s = arch_profile()
    # Down 10 cm past the floor at both ends: the floor overlaps, no crack.
    prof = [(A, F - 0.1)] + prof + [(-A, F - 0.1)]
    s = [-0.1] + s + [s[-1] + 0.1]
    ys = np.linspace(Y_BACK - 0.1, Y_FRONT + 0.1, ROWS + 1)

    def arch_pos(i, j):
        return Vector((prof[i][0], ys[j], prof[i][1]))
    g.grid(arch_pos, len(prof) - 1, ROWS, "arch", lambda i, j: (s[i], ys[j]),
           lambda c: Vector((-c.x, 0.0, -(c.z - F))))

    # Back wall and the front wall's inner face: columns under the arch polygon.
    def wall(y, want_y, bottom, mat_of):
        isl = {}
        for (x0, z0), (x1, z1) in zip(prof, prof[1:]):
            if abs(x1 - x0) < 1e-6:
                continue
            xm = 0.5 * (x0 + x1)
            b = bottom(xm)
            if b is None or max(z0, z1) <= b + 1e-3:
                continue
            mat, key = mat_of(xm)
            if key not in isl:
                isl[key] = g.island()
            k = max(1, math.ceil((max(z0, z1) - b) / 2.6))
            for r in range(k):
                zs0 = [b + (z0 - b) * r / k, b + (z1 - b) * r / k]
                zs1 = [b + (z0 - b) * (r + 1) / k, b + (z1 - b) * (r + 1) / k]
                pts = [(x0, y, zs0[0]), (x1, y, zs0[1]), (x1, y, zs1[1]), (x0, y, zs1[0])]
                ids = [g.v(p, f"w{y}") for p in pts]
                g.face(ids, [(p[0], p[2]) for p in pts], mat, (0, want_y, 0), isl[key])

    wall(Y_BACK, 1.0, lambda x: F - 0.1, lambda x: ("back", 0))
    wall(Y_FRONT, -1.0, lambda x: F + OPEN_H if abs(x) < OPEN_HW else F - 0.1,
         lambda x: ("front", 0 if abs(x) < OPEN_HW else (1 if x > 0 else 2)))

    # Door reveal: jambs and the lintel soffit through the wall thickness.
    for sx in (1, -1):
        g.grid(lambda i, j: Vector((sx * OPEN_HW, Y_FRONT + WALL_T * i, F - 0.1 + (OPEN_H + 0.1) * j / 3)),
               1, 3, "reveal", lambda i, j: (WALL_T * i, (OPEN_H + 0.1) * j / 3), lambda c: Vector((-sx, 0, 0)))
    g.grid(lambda i, j: Vector((-OPEN_HW + 2 * OPEN_HW * i / 6, Y_FRONT + WALL_T * j, F + OPEN_H)),
           6, 1, "reveal", lambda i, j: (2 * OPEN_HW * i / 6, WALL_T * j), lambda c: Vector((0, 0, -1)))

    # Apron and grass outside, warped UVs: full density near the door only.
    xs = [-75, -50, -32, -22, -16, -12, -9, -7, -5, -3, -1, 1, 3, 5, 7, 9, 12, 16, 22, 32, 50, 75]
    ya = [21.8, 24.3, 26.8, 29.5, 33, 38, 45, 55, 70, 97]

    def warp(d):
        return math.copysign(abs(d) if abs(d) < 14 else 14 + (abs(d) - 14) * 0.2, d)
    g.grid(lambda i, j: Vector((xs[i], ya[j], F)), len(xs) - 1, len(ya) - 1, "apron",
           lambda i, j: (warp(xs[i]) * 0.45, warp(ya[j] - 21.8) * 0.45), lambda c: Vector((0, 0, 1)))

    # Backdrop: a band around the door - sky, tree line, distant shelters.
    th = [math.radians(a) for a in range(-75, 76, 10)]
    zb = [F - 4.0, F + 5.0, F + 46.0]
    vb = [0.0, 9.0 * 0.45, 9.0 * 0.45 + 41.0 * 0.07]
    cy = Y_FRONT + WALL_T
    g.grid(lambda i, j: Vector((SKY_R * math.sin(th[i]), cy + SKY_R * math.cos(th[i]), zb[j])),
           len(th) - 1, 2, "sky", lambda i, j: (th[i] * SKY_R * 0.16, vb[j]),
           lambda c: Vector((-c.x, cy - c.y, 0)))

    # Pendant reflector lamps on rods from the arch.
    for x, y in LAMPS:
        cylinder(g, (x, y, LAMP_Z), (x, y, LAMP_Z + 0.45), 0.34, 0.13, 8, "lamp_shade", w=2.0,
                 caps=(True, False), cap_mat="lamp_lens")
        cylinder(g, (x, y, LAMP_Z + 0.45), (x, y, arch_top(x) + 0.15), 0.025, 0.025, 3, "rod", w=1.0,
                 caps=(False, False))

    # Cable trays along both walls.
    for sx in (1, -1):
        xw = A * math.sqrt(1 - (TRAY_H / B) ** 2)
        x_in, x_out = sx * (xw - 0.34), sx * (xw + 0.05)
        z0, z1 = F + TRAY_H, F + TRAY_H + 0.12
        yy = np.linspace(Y_BACK, Y_FRONT, 17)
        for pts, want in ((lambda i, j: Vector((x_in + (x_out - x_in) * i, yy[j], z0)), (0, 0, -1)),
                          (lambda i, j: Vector((x_in, yy[j], z0 + (z1 - z0) * i)), (-sx, 0, 0)),
                          (lambda i, j: Vector((x_in + (x_out - x_in) * i, yy[j], z1)), (0, 0, 1))):
            g.grid(pts, 1, 16, "tray", lambda i, j: (i * 0.4 * 0.8, yy[j] * 0.8), lambda c, w=want: Vector(w))

    props(g)
    return g


def props(g):
    """Ground equipment along the walls (none under the aircraft, so it can be
    swapped): fire board + sand box, a wheeled CO2 extinguisher, tool cart,
    crates, oxygen bottles, step ladder, cable reel, tow bar, chocks, drums,
    electrical cabinet."""
    xw = lambda h: A * math.sqrt(max(0.0, 1 - (h / B) ** 2))   # noqa: E731  wall x at height h
    # Fire board on legs against the right wall, sand box under it.
    box(g, (xw(2.2) - 0.12, 10.0, F + 1.65), (0.06, 1.6, 1.1), "fireboard", skip=("+x",))
    for dy in (-0.7, 0.7):
        box(g, (xw(2.2) - 0.10, 10.0 + dy, F + 0.55), (0.05, 0.05, 1.1), "steel", skip=("-z", "+x"))
    box(g, (xw(0.6) - 0.55, 10.0, F + 0.30), (0.6, 1.1, 0.6), "sandbox", skip=("-z",))
    # OU-80 extinguisher on its cart.
    ex, ey = xw(1.0) - 1.05, 11.65
    cylinder(g, (ex, ey, F + 0.18), (ex, ey, F + 1.15), 0.15, 0.15, 8, "extinguisher", caps=(False, True))
    cylinder(g, (ex, ey, F + 1.15), (ex, ey, F + 1.27), 0.05, 0.05, 6, "steel", caps=(False, True))
    for sy in (-1, 1):
        cylinder(g, (ex + 0.10, ey + sy * 0.20, F + 0.17), (ex + 0.10, ey + sy * 0.27, F + 0.17),
                 0.17, 0.17, 8, "tyre", w=3.0)
    box(g, (ex - 0.05, ey, F + 0.95), (0.04, 0.5, 0.04), "steel")
    # Tool cart with a tool chest on it.
    box(g, (xw(0.9) - 0.75, 6.8, F + 0.55), (0.6, 1.0, 0.5), "cart", skip=("-z",))
    box(g, (xw(0.9) - 0.75, 6.75, F + 0.95), (0.45, 0.7, 0.3), "toolbox", skip=("-z",))
    for dx in (-0.22, 0.22):
        for dy in (-0.4, 0.4):
            box(g, (xw(0.9) - 0.75 + dx, 6.8 + dy, F + 0.15), (0.06, 0.2, 0.3), "tyre", skip=("-z",))
    # Crates (ZIP boxes).
    cx = xw(0.6) - 0.7
    box(g, (cx, 3.0, F + 0.26), (0.62, 1.05, 0.52), "crate", skip=("-z",))
    box(g, (cx, 4.15, F + 0.26), (0.62, 1.05, 0.52), "crate", skip=("-z",))
    box(g, (cx + 0.02, 3.55, F + 0.78), (0.6, 1.05, 0.52), "crate", rot=Matrix.Rotation(0.12, 3, "Z"),
        skip=("-z",))
    # Tow bar lying along the right wall, chocks near the door.
    box(g, (xw(0.2) - 0.9, -9.0, F + 0.07), (0.12, 4.8, 0.12), "towbar", skip=("-z",))
    for sx in (-1, 1):
        tx = xw(0.2) - 0.9
        cylinder(g, (tx + sx * 0.10, -11.3, F + 0.16), (tx + sx * 0.17, -11.3, F + 0.16), 0.16, 0.16, 8, "tyre", w=3.0)
    for k, y in enumerate((15.6, 16.15)):
        box(g, (xw(0.2) - 1.4, y, F + 0.09), (0.55, 0.22, 0.18), "chock", rot=Matrix.Rotation(0.2 * k, 3, "Z"),
            skip=("-z",))
    # Oxygen / nitrogen bottles chained to the left wall, a drum rack at the back.
    for k, y in enumerate((-7.5, -7.15, -6.8, -6.45)):
        x = -(xw(1.6) - 0.22)
        cylinder(g, (x, y, F), (x, y, F + 1.55), 0.11, 0.11, 8, "o2" if k < 3 else "n2", w=3.0, caps=(False, True))
    box(g, (-(xw(1.2) - 0.08), -6.97, F + 1.2), (0.05, 1.3, 0.05), "steel")
    for x, y in ((10.3, -16.2), (9.6, -16.9), (10.4, -17.1)):
        cylinder(g, (x, y, F), (x, y, F + 0.88), 0.29, 0.29, 10, "drum", w=2.0, caps=(False, True))
    # Step ladder / maintenance stand by the left wing root, platform at 2.2 m.
    lx, ly = -9.6, 2.4
    for dy in (-0.45, 0.45):
        box(g, (lx + 0.55, ly + dy, F + 1.1), (0.07, 0.07, 2.35), "ladder",
            rot=Matrix.Rotation(-0.42, 3, "Y"), skip=())
        box(g, (lx - 0.45, ly + dy, F + 1.1), (0.07, 0.07, 2.25), "ladder")
    for k in range(5):
        h = 0.40 * (k + 1)
        box(g, (lx + 1.03 - h * math.tan(0.42), ly, F + h), (0.26, 0.9, 0.04), "step")
    box(g, (lx - 0.2, ly, F + 2.2), (0.7, 1.0, 0.05), "step")
    # Cable reel, back left; electrical cabinet on the back wall.
    cylinder(g, (-10.4, -14.4, F + 0.55), (-9.8, -14.4, F + 0.55), 0.55, 0.55, 12, "reel", w=2.0)
    box(g, (-10.1, -14.4, F + 0.05), (0.9, 0.5, 0.1), "steel", skip=("-z",))
    box(g, (6.5, Y_BACK + 0.16, F + 1.55), (0.9, 0.3, 1.25), "cabinet", skip=("-y",))


def build_floor():
    g = Geo()
    xs = [-12.1, -10.55, -9, -7, -5, -3, -1, 1, 3, 5, 7, 9, 10.55, 12.1]
    ys = np.linspace(Y_BACK - 0.1, Y_FRONT, 17)
    y0, y1 = Y_BACK - 0.1, Y_FRONT + WALL_T
    uv = lambda x, y: ((x + 12.1) / 24.2, (y - y0) / (y1 - y0))   # noqa: E731  fixed, not packed
    isl = g.grid(lambda i, j: Vector((xs[i], ys[j], F)), len(xs) - 1, len(ys) - 1, "floor",
                 lambda i, j: uv(xs[i], ys[j]), lambda c: Vector((0, 0, 1)), share="f")
    xt = [x for x in xs if abs(x) <= OPEN_HW + 1e-6]
    g.grid(lambda i, j: Vector((xt[i], Y_FRONT + WALL_T * j, F)), len(xt) - 1, 1, "floor",
           lambda i, j: uv(xt[i], Y_FRONT + WALL_T * j), lambda c: Vector((0, 0, 1)), isl=isl, share="f")
    return g


# ================================================================ UV packing

def pack(g, size=TEX, margin=3):
    """Skyline-pack the islands (UVs in metres x weight) into one atlas at
    the largest common scale that fits (each island may turn 90 deg). Sets
    face UVs to 0..1, returns texels per unit (= texels/m at weight 1)."""
    isl = {}
    for f in g.faces:
        b = isl.setdefault(f["isl"], [1e9, 1e9, -1e9, -1e9])
        for u, v in f["uv"]:
            b[0], b[1], b[2], b[3] = min(b[0], u), min(b[1], v), max(b[2], u), max(b[3], v)

    def dims(k):
        b = isl[k]
        return b[2] - b[0], b[3] - b[1]
    orders = [sorted(isl, key=lambda k: -max(dims(k)) * min(dims(k)) ** 0.25),
              sorted(isl, key=lambda k: -max(dims(k))),
              sorted(isl, key=lambda k: -min(dims(k))),
              sorted(isl, key=lambda k: -dims(k)[0] * dims(k)[1])]

    def place(scale, order):
        sky = [[0, 0, size]]                                   # skyline segments: x, top, width
        pos = {}
        for k in order:
            b = isl[k]
            best = None
            for turn in (False, True):
                w = math.ceil((b[3] - b[1] if turn else b[2] - b[0]) * scale) + 2 * margin
                h = math.ceil((b[2] - b[0] if turn else b[3] - b[1]) * scale) + 2 * margin
                for i, (x, _, _) in enumerate(sky):
                    if x + w > size:
                        break
                    top, j, cover = 0, i, 0
                    while cover < w:
                        top = max(top, sky[j][1])
                        cover += sky[j][2] - (x - sky[j][0] if j == i else 0)
                        j += 1
                    if top + h <= size and (best is None or (top + h, x) < (best[0], best[1])):
                        best = (top + h, x, top, w, h, turn)
            if best is None:
                return None
            _, x, top, w, h, turn = best
            pos[k] = (x, top, turn)
            new, end = [], x + w
            for sx, sy, sw in sky:
                if sx + sw <= x or sx >= end:
                    new.append([sx, sy, sw])
                else:
                    if sx < x:
                        new.append([sx, sy, x - sx])
                    if sx + sw > end:
                        new.append([end, sy, sx + sw - end])
            new.append([x, top + h, w])
            new.sort()
            merged = [new[0]]
            for seg in new[1:]:
                if seg[1] == merged[-1][1] and merged[-1][0] + merged[-1][2] == seg[0]:
                    merged[-1][2] += seg[2]
                else:
                    merged.append(seg)
            sky = merged
        return pos

    best = (0.0, None)
    for order in orders:
        lo, hi = 1.0, 64.0
        for _ in range(26):
            mid = 0.5 * (lo + hi)
            lo, hi = (mid, hi) if place(mid, order) else (lo, mid)
        if lo > best[0]:
            best = (lo, place(lo, order))
    scale, pos = best
    for f in g.faces:
        b, (px, py, turn) = isl[f["isl"]], pos[f["isl"]]
        out = []
        for u, v in f["uv"]:
            du, dv = ((v - b[1]) * scale, (b[2] - u) * scale) if turn else ((u - b[0]) * scale, (v - b[1]) * scale)
            # Atlas row y grows downwards; Blender v grows upwards.
            out.append(((px + margin + du) / size, 1.0 - (py + margin + dv) / size))
        f["uv"] = out
    return scale


# ================================================================ texel maps

class Maps:
    """3D position, normal, material and texel size under every texel."""

    def __init__(self, g, size=TEX):
        self.size = size
        self.mats = sorted({f["mat"] for f in g.faces})
        self.pos = np.zeros((size, size, 3), np.float32)
        self.nrm = np.zeros((size, size, 3), np.float32)
        self.mat = np.full((size, size), -1, np.int16)
        self.tsize = np.ones((size, size), np.float32)
        for f in g.faces:
            ps = [np.array(g.verts[i]) for i in f["idx"]]
            uv = [(u * size, (1.0 - v) * size) for u, v in f["uv"]]
            for k in range(1, len(ps) - 1):
                self._raster([uv[0], uv[k], uv[k + 1]], [ps[0], ps[k], ps[k + 1]], np.array(f["n"]),
                             self.mats.index(f["mat"]))
        self.mask = self.mat >= 0
        self.x, self.y, self.z = self.pos[..., 0], self.pos[..., 1], self.pos[..., 2]
        self.nx, self.ny, self.nz = self.nrm[..., 0], self.nrm[..., 1], self.nrm[..., 2]

    def _raster(self, uvs, ps, n, mid):
        (ax, ay), (bx, by), (cx, cy) = uvs
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-10:
            return
        area3 = 0.5 * np.linalg.norm(np.cross(ps[1] - ps[0], ps[2] - ps[0]))
        ts = math.sqrt(area3 / (abs(d) * 0.5))
        x0, x1 = max(int(min(ax, bx, cx)) - 1, 0), min(int(max(ax, bx, cx)) + 1, self.size - 1)
        y0, y1 = max(int(min(ay, by, cy)) - 1, 0), min(int(max(ay, by, cy)) + 1, self.size - 1)
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / d
        w1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / d
        w2 = 1.0 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            return
        w = np.stack([w0, w1, w2], -1)[inside]
        rows, cols = Y[inside].astype(int), X[inside].astype(int)
        self.pos[rows, cols] = w @ np.array(ps)
        self.nrm[rows, cols] = n
        self.mat[rows, cols] = mid
        self.tsize[rows, cols] = ts

    def m(self, *names):
        return np.isin(self.mat, [self.mats.index(n) for n in names if n in self.mats])

    def line(self, dist, width=1.0):
        return np.clip(1.0 - np.abs(dist) / (width * self.tsize), 0.0, 1.0)

    def fill(self, sdf):
        return np.clip(0.5 - sdf / self.tsize, 0.0, 1.0)


def dilate(img, mask, iters=8):
    img, m = img.copy(), mask.copy()
    h, w = mask.shape
    for _ in range(iters):
        acc = np.zeros_like(img)
        cnt = np.zeros(m.shape, np.float32)
        pi = np.pad(img * m[..., None], ((1, 1), (1, 1), (0, 0)))
        pm = np.pad(m.astype(np.float32), 1)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    acc += pi[1 + dy:h + 1 + dy, 1 + dx:w + 1 + dx]
                    cnt += pm[1 + dy:h + 1 + dy, 1 + dx:w + 1 + dx]
        grow = (~m) & (cnt > 0)
        img[grow] = acc[grow] / cnt[grow][:, None]
        m |= grow
    return img


def blur_masked(img, mask, passes=1):
    """Small blur inside the atlas islands only (bake noise)."""
    out, mk = img.copy(), mask.astype(np.float32)[..., None]
    for _ in range(passes):
        pi, pm = np.pad(out * mk, ((1, 1), (1, 1), (0, 0)), mode="edge"), np.pad(mk, ((1, 1), (1, 1), (0, 0)),
                                                                                  mode="edge")
        acc = pi[:-2, 1:-1] + pi[2:, 1:-1] + pi[1:-1, :-2] + pi[1:-1, 2:] + 4 * pi[1:-1, 1:-1]
        cnt = pm[:-2, 1:-1] + pm[2:, 1:-1] + pm[1:-1, :-2] + pm[1:-1, 2:] + 4 * pm[1:-1, 1:-1]
        out = np.where(mask[..., None], acc / np.maximum(cnt, 1e-6), out)
    return out


# ================================================================ paint (albedo, sRGB 0..1)

def rgb(c):
    return np.array(c, np.float32) / 255.0


def mix(img, color, amount):
    col = rgb(color) if isinstance(color, tuple) else color
    a = amount[..., None]
    img *= 1.0 - a
    img += col * a


def _hash2(i, j, seed):
    h = np.sin(i * 127.1 + j * 311.7 + seed * 74.7) * 43758.5453
    return h - np.floor(h)


def grain(m, seed, amp):
    """Per-texel concrete grain (stable for a given atlas)."""
    r, c = np.mgrid[0:m.size, 0:m.size]
    return 1.0 + amp * (_hash2(r, c, seed) - 0.5)


def paint_shell(m):
    x, y, z = m.x, m.y, m.z
    img = np.zeros((m.size, m.size, 3), np.float32)
    h = z - F
    big = fbm(x * 0.12, y * 0.12, z * 0.12, 11, octaves=4)
    mid = fbm(x * 0.6, y * 0.6, z * 0.6, 23, octaves=3)

    # Arch: 5 mm steel liner plates, painted pale grey-green above a darker
    # dado; weld seams, rust runs, soot towards the back (engine runs).
    arch = m.m("arch")
    if arch.any():
        # arc length from the right foot of the arch (plate grid along it)
        ts = np.linspace(0.0, np.pi, 801)
        cum = np.concatenate([[0.0], np.cumsum(np.hypot(np.diff(A * np.cos(ts)), np.diff(B * np.sin(ts))))])
        sarc = np.interp(np.arctan2(np.maximum(z - F, 0.0) / B, x / A), ts, cum)
        col = np.zeros_like(img)
        col[:] = rgb((150, 156, 146))
        # sheets: a slightly different tone each (1.5 m wide rings of liner)
        ring = np.floor((y - Y_BACK) / 1.5)
        col *= (0.95 + 0.07 * _hash2(ring, np.floor(sarc / 6.0), 41))[..., None]
        dado = smoothstep(1.85, 1.80, h)
        mix(col, (76, 88, 78), dado)
        mix(col, (44, 50, 46), m.line(h - 1.82, 1.2))
        crown = np.abs(x) < 0.6
        seams = np.maximum(m.line(np.mod(y - Y_BACK + 0.75, 1.5) - 0.75, 1.0), m.line(x, 1.0) * crown)
        col *= (1.0 - 0.26 * seams)[..., None]
        # rust runs down from some of the transverse welds, damp at the crown joint
        streak = fbm(y * 2.2, sarc * 0.08, 0.0, 43, octaves=3)
        weld = smoothstep(0.30, 0.0, np.abs(np.mod(y - Y_BACK + 0.75, 1.5) - 0.75))
        runs = smoothstep(0.64, 0.80, streak) * weld * smoothstep(1.5, 4.0, h)
        mix(col, (118, 92, 66), runs * 0.35)
        mix(col, (104, 100, 86), smoothstep(1.6, 0.0, np.abs(x)) * smoothstep(0.45, 0.7, mid) * 0.45)
        # water stains near the crown, soot at the back, splash dirt low on the walls
        mix(col, (120, 116, 100), smoothstep(0.55, 0.75, big) * smoothstep(6.0, 9.0, h) * 0.5)
        mix(col, (46, 44, 42), smoothstep(-8.0, -18.5, y) * smoothstep(0.75, 0.1, np.abs(x) / A) * 0.45
            * (0.6 + 0.4 * mid))
        mix(col, (70, 68, 60), smoothstep(0.8, 0.0, h) * 0.5)
        col *= (0.90 + 0.14 * mid)[..., None] * grain(m, 3, 0.05)[..., None]
        img = np.where(arch[..., None], col, img)

    # Back wall: FBS blocks 2.4 x 0.6 m, painted concrete, exhaust duct door,
    # personnel door, soot fan around the duct, a red/white sign.
    back = m.m("back")
    if back.any():
        col = np.zeros_like(img)
        col[:] = rgb((150, 148, 140))
        course = np.floor(h / 0.6)
        off = np.where(np.mod(course, 2) == 0, 0.0, 1.2)
        joints = np.maximum(m.line(np.mod(h + 0.3, 0.6) - 0.3, 0.9), m.line(np.mod(x + off + 1.2, 2.4) - 1.2, 0.9))
        col *= (1.0 - 0.22 * joints)[..., None]
        blk = _hash2(course, np.floor((x + off) / 2.4), 5)
        col *= (0.93 + 0.12 * blk)[..., None]
        # exhaust duct: 3 x 3 m steel door, ribbed, soot around it
        duct = (np.abs(x) < 1.5) & (h > 0.45) & (h < 3.45)
        mix(col, (40, 38, 36), smoothstep(5.5, 1.2, np.hypot(x * 0.8, h - 2.2)) * 0.75 * (0.7 + 0.3 * mid))
        dcol = np.zeros_like(img)
        dcol[:] = rgb((86, 80, 72))
        ribs = m.line(np.mod(h, 0.5) - 0.25, 1.0)
        dcol *= (1.0 - 0.25 * ribs)[..., None]
        mix(dcol, (110, 70, 42), smoothstep(0.55, 0.8, mid) * 0.6)
        mix(dcol, (30, 28, 26), smoothstep(1.6, 3.4, h) * 0.5)
        col = np.where(duct[..., None], dcol, col)
        col *= (1.0 - 0.6 * np.maximum(m.line(np.abs(x) - 1.5, 1.2) * (h > 0.4) * (h < 3.5),
                                       m.line(h - 3.45, 1.2) * (np.abs(x) < 1.5)))[..., None]
        # personnel door + sign
        door = (np.abs(x + 8.2) < 0.48) & (h < 2.05)
        mix(col, (98, 110, 104), door.astype(np.float32))
        col *= (1.0 - 0.5 * np.maximum(m.line(np.abs(x + 8.2) - 0.48, 1.2) * (h < 2.05),
                                       m.line(h - 2.05, 1.2) * (np.abs(x + 8.2) < 0.48)))[..., None]
        sign = (np.abs(x + 8.2) < 0.45) & (np.abs(h - 2.55) < 0.2)
        mix(col, (224, 222, 210), sign.astype(np.float32))
        mix(col, (176, 36, 32), (sign & (np.abs(h - 2.55) < 0.07)).astype(np.float32))
        mix(col, (60, 58, 52), smoothstep(0.6, 0.0, h) * 0.45)
        col *= grain(m, 7, 0.06)[..., None]
        img = np.where(back[..., None], col, img)

    # Front wall inside and the door reveal: bare concrete, rougher, stained.
    front = m.m("front", "reveal")
    if front.any():
        col = np.zeros_like(img)
        col[:] = rgb((136, 134, 126))
        col *= (0.85 + 0.2 * mid)[..., None]
        lift = np.floor(h / 1.2)
        col *= (1.0 - 0.18 * m.line(np.mod(h, 1.2), 0.9))[..., None]
        col *= (0.95 + 0.06 * _hash2(lift, np.floor(x / 3.0), 9))[..., None]
        mix(col, (84, 78, 66), smoothstep(0.6, 0.85, fbm(x * 1.5, y, h * 0.3, 77, 3)) * smoothstep(OPEN_H + 0.1, 3.0, h)
            * 0.35)
        mix(col, (176, 150, 40), (m.m("reveal") & (h < 1.0) & (np.mod(h, 0.5) < 0.25)).astype(np.float32)
            * 0.85)  # yellow/black hazard band at the jamb foot
        mix(col, (30, 30, 28), (m.m("reveal") & (h < 1.0) & (np.mod(h, 0.5) >= 0.25)).astype(np.float32) * 0.8)
        col *= grain(m, 13, 0.07)[..., None]
        img = np.where(front[..., None], col, img)

    # Apron (PAG slabs) and grass beyond it, sliding door rails at the door.
    ap = m.m("apron")
    if ap.any():
        col = np.zeros_like(img)
        col[:] = rgb((146, 142, 134))
        slab = _hash2(np.floor(x / 2.0), np.floor((y - 21.8) / 6.0), 17)
        col *= (0.9 + 0.12 * slab)[..., None]
        col *= (1.0 - 0.22 * np.maximum(m.line(np.mod(x + 1.0, 2.0) - 1.0, 0.7),
                                        m.line(np.mod(y - 21.8 + 3.0, 6.0) - 3.0, 0.7)))[..., None]
        col *= (0.88 + 0.16 * mid)[..., None]
        mix(col, (196, 160, 52), m.fill(np.abs(x) - 0.075) * (y < 60))
        rails = (np.abs(y - 22.35) < 0.05) | (np.abs(y - 23.05) < 0.05)
        mix(col, (82, 80, 76), (rails & (np.abs(x) < 20)).astype(np.float32))
        grass = smoothstep(42.0, 52.0, y) + smoothstep(26.0, 34.0, np.abs(x))
        g_col = np.zeros_like(img)
        g_col[:] = rgb((88, 96, 62))
        g_col *= (0.75 + 0.4 * fbm(x * 0.3, y * 0.3, 0, 31, 3))[..., None]
        mix(g_col, (128, 120, 82), smoothstep(0.5, 0.75, fbm(x * 0.12, y * 0.12, 1, 32, 3)) * 0.6)
        col = col * (1 - np.clip(grass, 0, 1))[..., None] + g_col * np.clip(grass, 0, 1)[..., None]
        img = np.where(ap[..., None], col, img)

    # Lamps, rods, trays and props.
    flat = {
        "lamp_shade": (64, 82, 66), "rod": (60, 62, 60), "tray": (138, 142, 140),
        "fireboard": (176, 38, 34), "sandbox": (168, 40, 34), "extinguisher": (182, 34, 30),
        "steel": (84, 86, 84), "tyre": (34, 34, 34), "cart": (82, 96, 112), "toolbox": (150, 46, 40),
        "crate": (92, 102, 64), "towbar": (196, 170, 52), "chock": (176, 46, 36), "o2": (64, 104, 168),
        "n2": (40, 40, 42), "drum": (78, 98, 72), "ladder": (206, 166, 44), "step": (120, 122, 118),
        "reel": (118, 92, 60), "cabinet": (110, 118, 112), "lamp_lens": (255, 246, 226),
    }
    for name, c in flat.items():
        msk = m.m(name)
        if msk.any():
            img[msk] = rgb(c)
    # wear and detail on props
    props_m = m.m(*[k for k in flat if k not in ("lamp_lens",)])
    img = np.where(props_m[..., None], img * (0.86 + 0.18 * mid)[..., None], img)
    fb = m.m("fireboard")
    if fb.any():   # tools on the fire board: axe, hook, two cone buckets, shovel (dark silhouettes)
        u = y - 10.0
        tools = ((np.abs(u + 0.55) < 0.05) & (np.abs(h - 1.65) < 0.45)) | \
                ((np.abs(u + 0.2) < 0.03) & (np.abs(h - 1.6) < 0.5)) | \
                ((np.abs(u - 0.62) < 0.04) & (np.abs(h - 1.62) < 0.45)) | \
                ((np.abs(u - 0.2) < 0.14 - (h - 1.25) * 0.3) & (h > 1.25) & (h < 1.6)) | \
                ((np.abs(u - 0.2) < 0.14 - (h - 1.7) * 0.3) & (h > 1.7) & (h < 2.05))
        mix(img, (34, 30, 28), (fb & tools).astype(np.float32))
        mix(img, (238, 236, 228), (fb & (np.abs(h - 2.12) < 0.05)).astype(np.float32))
    for name, c in (("o2", (230, 232, 236)), ("n2", (226, 220, 60))):
        msk = m.m(name)
        mix(img, c, (msk & (h > 1.25) & (h < 1.4)).astype(np.float32))
    mix(img, (238, 236, 230), (m.m("crate") & (np.abs(h - 0.26) < 0.05) & (np.abs(m.nx) > 0.5)).astype(np.float32)
        * 0.8)   # stencil band on the crate sides
    mix(img, (30, 30, 30), (m.m("ladder") & (np.mod(z * 3.0, 1.0) < 0.5)).astype(np.float32) * 0.85)
    mix(img, (232, 228, 40), (m.m("cabinet") & (np.abs(h - 1.9) < 0.12) & (np.abs(x - 6.5) < 0.12)).astype(np.float32))
    mix(img, (238, 240, 236), (m.m("lamp_shade") & (m.nz < -0.1)).astype(np.float32))   # enamel inside
    return img


def paint_sky(m, img):
    """Backdrop band (unlit, painted in display values): sky gradient with a
    hazy horizon, tree line, distant shelter mounds, grass."""
    sky = m.m("sky")
    if not sky.any():
        return img
    cy = Y_FRONT + WALL_T
    th = np.arctan2(m.x, m.y - cy)
    h = m.z - F
    col = np.zeros_like(img)
    t = np.clip((h - 2.0) / 40.0, 0, 1) ** 0.6
    top, hor = rgb((150, 186, 228)), rgb((236, 240, 242))
    col[:] = hor
    col = hor + (top - hor) * t[..., None]
    cloud = smoothstep(0.58, 0.78, fbm(th * 6.0, h * 0.06, 0.0, 51, 4)) * smoothstep(8.0, 16.0, h)
    mix(col, (248, 250, 252), cloud * 0.6)
    # tree line: noisy top edge 3..6 m, darker under; shelter mounds every ~25 deg
    edge = 3.0 + 2.6 * fbm(th * 18.0, 0.0, 0.0, 61, 4) + 1.4 * fbm(th * 60.0, 0.0, 0.0, 62, 2)
    trees = smoothstep(0.15, -0.15, h - edge)
    tcol = np.zeros_like(img)
    tcol[:] = rgb((92, 108, 96))
    tcol *= (0.85 + 0.3 * fbm(th * 90.0, h * 2.0, 0.0, 63, 3))[..., None]
    col = col * (1 - trees)[..., None] + tcol * trees[..., None]
    mound_c = np.mod(th + 0.22, 0.44) - 0.22
    mound = smoothstep(0.03, -0.03, h - (2.6 - (mound_c / 0.17) ** 2 * 2.6)) * (np.abs(mound_c) < 0.17)
    mix(col, (128, 136, 100), mound * 0.95)
    portal = (np.abs(mound_c) < 0.035) & (h < 1.6) & (np.abs(th) > 0.3)
    mix(col, (150, 150, 144), portal.astype(np.float32) * 0.9)
    ground = smoothstep(0.4, -0.2, h)
    mix(col, (138, 140, 104), ground)
    # aerial haze: everything at the horizon pulled towards the sky colour
    mix(col, (226, 232, 238), smoothstep(1.5, 4.0, h) * (1 - smoothstep(4.0, 9.0, h)) * 0.25 * trees)
    return np.where(sky[..., None], col, img)


def paint_floor(m):
    x, y = m.x, m.y
    img = np.zeros((m.size, m.size, 3), np.float32)
    img[:] = rgb((138, 136, 130))
    mid = fbm(x * 0.5, y * 0.5, 0.0, 71, octaves=4)
    fine = fbm(x * 3.0, y * 3.0, 0.0, 72, octaves=2)
    # PAG-14 slabs 2 x 6 m, each a slightly different pour; dark sealant joints.
    si, sj = np.floor((x + 12.0) / 2.0), np.floor((y - Y_BACK) / 6.0)
    img *= (0.90 + 0.12 * _hash2(si, sj, 3))[..., None]
    img *= (0.88 + 0.16 * mid + 0.06 * fine)[..., None]
    joints = np.maximum(m.line(np.mod(x + 12.0 + 1.0, 2.0) - 1.0, 0.7), m.line(np.mod(y - Y_BACK + 3.0, 6.0) - 3.0, 0.7))
    mix(img, (62, 60, 56), joints * 0.7)
    # hairline cracks and patched spalls
    crack = m.line(fbm(x * 0.9, y * 0.9, 5.0, 73, 3) - 0.5, 0.5) * smoothstep(0.66, 0.74, fbm(x * 0.2, y * 0.2, 9.0, 74, 2))
    mix(img, (86, 84, 78), crack * 0.35)
    patch = smoothstep(0.73, 0.76, fbm(x * 0.35, y * 0.35, 3.0, 75, 3))
    mix(img, (128, 128, 124), patch * 0.6)
    # tyre tracks from the door to the parking spot, oil drips under the
    # engines and the gearbox, fuel stain, damp near the door
    for tx in (-MAIN_X, MAIN_X, 0.0):
        y_stop = MAIN_Y if tx else NOSE_Y
        track = smoothstep(0.42, 0.0, np.abs(x - tx)) * smoothstep(y_stop - 1.0, y_stop + 1.5, y)
        mix(img, (70, 68, 64), track * (0.25 + 0.2 * fine))
    for ox, oy, r in ((1.15, -5.0, 1.3), (-1.15, -4.2, 1.1), (0.2, -1.0, 0.8), (1.3, -8.4, 0.9),
                      (-1.2, -8.8, 1.0), (6.0, -12.0, 1.1)):
        d = np.hypot((x - ox) / r, (y - oy) / (r * 1.6)) + 0.9 * (fbm(x * 1.6, y * 1.6, ox, 76, 3) - 0.5)
        mix(img, (70, 66, 58), smoothstep(1.0, 0.3, d) * 0.4)
    mix(img, (104, 100, 92), smoothstep(0.58, 0.72, fbm(x * 0.15, y * 0.15, 4.0, 77, 3)) * 0.35)   # grime
    mix(img, (98, 98, 94), smoothstep(16.0, 20.5, y) * smoothstep(0.55, 0.7, mid) * 0.5)
    # markings: yellow taxi centreline to the nose wheel stop, stop bar,
    # red safety line 1.2 m from the walls, wheel boxes, tie-down rings,
    # earthing point; worn by traffic (fine noise)
    wear = smoothstep(0.25, 0.6, fine + 0.25 * mid)
    yellow, red, white = (206, 166, 46), (170, 44, 38), (226, 226, 218)
    mix(img, yellow, m.fill(np.abs(x) - 0.075) * (y > NOSE_Y + 0.4) * wear)
    mix(img, yellow, m.fill(np.abs(y - (NOSE_Y + 0.25)) - 0.075) * (np.abs(x) < 1.2) * wear)
    mix(img, red, m.fill(np.abs(np.abs(x) - (A - 1.25)) - 0.05) * (y > Y_BACK + 1.0) * (y < Y_FRONT - 0.2) * wear)
    mix(img, white, m.fill(np.abs(np.abs(y - MAIN_Y) - 0.9) - 0.04) * (np.abs(np.abs(x) - MAIN_X) < 0.6) * wear)
    mix(img, white, m.fill(np.abs(np.abs(np.abs(x) - MAIN_X) - 0.6) - 0.04) * (np.abs(y - MAIN_Y) < 0.9) * wear)
    for rx, ry in ((3.6, MAIN_Y - 0.6), (-3.6, MAIN_Y - 0.6), (3.6, MAIN_Y + 2.6), (-3.6, MAIN_Y + 2.6),
                   (0.0, NOSE_Y + 2.2)):
        d = np.hypot(x - rx, y - ry)
        mix(img, (40, 40, 38), m.fill(d - 0.13))
        mix(img, (118, 118, 112), m.line(d - 0.09, 1.2))
    d = np.hypot(x + 1.6, y - (NOSE_Y + 1.4))
    mix(img, (40, 40, 38), m.fill(d - 0.16))
    mix(img, (196, 176, 50), m.fill(np.abs(d - 0.24) - 0.035))
    # door threshold: steel drain grating across the opening
    grate = (np.abs(y - (Y_FRONT + 0.4)) < 0.22) & (np.abs(x) < OPEN_HW)
    mix(img, (66, 66, 62), grate.astype(np.float32))
    mix(img, (30, 30, 28), (grate & (np.mod(x, 0.12) < 0.05)).astype(np.float32))
    img *= grain(m, 21, 0.07)[..., None]
    return img


# ================================================================ Blender objects

def to_object(g, name, faces=None):
    faces = g.faces if faces is None else faces
    used = sorted({i for f in faces for i in f["idx"]})
    remap = {old: new for new, old in enumerate(used)}
    me = bpy.data.meshes.new(name)
    me.from_pydata([g.verts[i] for i in used], [], [[remap[i] for i in f["idx"]] for f in faces])
    uvl = me.uv_layers.new(name="UV")
    k = 0
    for poly, f in zip(me.polygons, faces):
        for li, uv in zip(poly.loop_indices, f["uv"]):
            uvl.data[li].uv = uv
        k += 1
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def float_image(name, arr):
    """arr: HxWx3 (row 0 = top) linear floats."""
    h, w = arr.shape[:2]
    img = bpy.data.images.new(name, w, h, float_buffer=True)
    px = np.ones((h, w, 4), np.float32)
    px[..., :3] = arr
    img.pixels.foreach_set(px[::-1].reshape(-1))
    return img


def read_image(img):
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(h, w, 4)[::-1, :, :3].copy()


def bake_material(obj, albedo_lin, target):
    mat = bpy.data.materials.new(obj.name + "_bake")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfDiffuse")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = albedo_lin
    tex.interpolation = "Closest"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Color"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    tgt = nt.nodes.new("ShaderNodeTexImage")
    tgt.image = target
    nt.nodes.active = tgt
    obj.data.materials.clear()
    obj.data.materials.append(mat)


def setup_cycles(samples):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = "GPU"
    except Exception as e:  # noqa: BLE001
        print("[hangar] GPU unavailable, CPU bake:", e)
        sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.max_bounces = 4
    sc.cycles.diffuse_bounces = 3
    sc.cycles.glossy_bounces = 0
    sc.cycles.transmission_bounces = 0
    sc.cycles.transparent_max_bounces = 2
    sc.render.bake.margin = 3
    sc.render.bake.margin_type = "EXTEND"
    world = sc.world or bpy.data.worlds.new("World")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (*SKY_COLOR, 1.0)
    bg.inputs["Strength"].default_value = SKY_STRENGTH


def add_lights():
    sc = bpy.context.scene
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = SUN_STRENGTH
    sun.color = SUN_COLOR
    sun.angle = math.radians(0.6)
    so = bpy.data.objects.new("sun", sun)
    so.rotation_euler = (-SUN_DIR).to_track_quat("-Z", "Y").to_euler()
    sc.collection.objects.link(so)
    for k, (x, y) in enumerate(LAMPS):
        li = bpy.data.lights.new(f"lamp{k}", "SPOT")
        li.energy = LAMP_POWER
        li.color = LAMP_COLOR
        li.spot_size = math.radians(118)
        li.spot_blend = 0.45
        li.shadow_soft_size = 0.22
        lo = bpy.data.objects.new(f"lamp{k}", li)
        lo.location = (x, y, LAMP_Z - 0.06)
        lo.rotation_euler = (0, 0, 0)          # spot points down its -Z
        sc.collection.objects.link(lo)
        # Light leaking up through the reflector's vent: a glow on the vault.
        up = bpy.data.lights.new(f"lamp_up{k}", "POINT")
        up.energy = LAMP_POWER * 0.18
        up.color = LAMP_COLOR
        up.shadow_soft_size = 0.3
        uo = bpy.data.objects.new(f"lamp_up{k}", up)
        uo.location = (x, y, LAMP_Z + 0.75)
        sc.collection.objects.link(uo)


def bake(obj):
    for o in bpy.context.scene.objects:
        if o is not None:
            o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="DIFFUSE", pass_filter={"DIRECT", "INDIRECT"}, margin=3, use_clear=True)


# ================================================================ export

def export_fsm(g, path):
    """FSM4 (docs/formats/fsm4.md), one static part, game axes."""
    verts, index, lookup = [], [], {}
    radius = 0.0
    for f in g.faces:
        corners = []
        for i, (u, v) in zip(f["idx"], f["uv"]):
            p, n = g.verts[i], f["n"]
            key = (round(p.x, 4), round(p.y, 4), round(p.z, 4), round(u, 6), round(v, 6), round(n.x, 3), round(n.y, 3),
                   round(n.z, 3))
            if key not in lookup:
                lookup[key] = len(verts)
                verts.append((u, 1.0 - v, n.x, n.z, -n.y, p.x, p.z, -p.y))
                radius = max(radius, p.length)
            corners.append(lookup[key])
        for k in range(1, len(corners) - 1):
            index += [corners[0], corners[k], corners[k + 1]]
    assert len(verts) < 65536
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sIIfI12x", b"FSM4", len(verts), len(index), radius, 1))
        fh.write(struct.pack("<IIHH4x3f4x3f4x", 0, len(index), 0, 0, 0, 0, 0, 1, 0, 0))
        for v in verts:
            fh.write(struct.pack("<8f", *v))
        fh.write(struct.pack(f"<{len(index)}H", *index))
        fh.write(b"\0" * ((-fh.tell()) % 16))
    return len(index) // 3, len(verts), radius, os.path.getsize(path)


def check_guard_band(geos, eyes, foc=261.2, half=(240, 136), band=2048):
    """GE guard band (gotchas #5, #19): a triangle that crosses the near plane
    (1 m) is clipped there; if a clipped vertex lands more than 2048 px off
    the centre, the GE drops the whole triangle. Counts, per camera, the
    dropped triangles whose remaining part would have been on screen."""
    report = {}
    for name, (eye, target) in eyes.items():
        e = Vector(eye)
        f = (Vector(target) - e).normalized()
        r = f.cross(Vector((0, 0, 1))).normalized()
        u = r.cross(f)
        dropped = visible = 0
        for g in geos:
            for fc in g.faces:
                idx = fc["idx"]
                for k in range(1, len(idx) - 1):
                    tri = [g.verts[i] - e for i in (idx[0], idx[k], idx[k + 1])]
                    cam = [(p.dot(r), p.dot(u), p.dot(f)) for p in tri]
                    if not (min(c[2] for c in cam) < 1.0 < max(c[2] for c in cam)):
                        continue
                    poly = []                                   # clip to depth >= 1
                    for a, b in zip(cam, cam[1:] + cam[:1]):
                        if a[2] >= 1.0:
                            poly.append(a)
                        if (a[2] - 1.0) * (b[2] - 1.0) < 0:
                            t = (1.0 - a[2]) / (b[2] - a[2])
                            poly.append(tuple(a[i] + (b[i] - a[i]) * t for i in range(3)))
                    scr = [(foc * x / z, foc * y / z) for x, y, z in poly]
                    if max(max(abs(x), abs(y)) for x, y in scr) <= band:
                        continue
                    dropped += 1
                    # on screen? clip the projected polygon to the screen rect
                    pts = scr
                    for axis, lim, sgn in ((0, half[0], 1), (0, half[0], -1), (1, half[1], 1), (1, half[1], -1)):
                        nxt = []
                        for a, b in zip(pts, pts[1:] + pts[:1]):
                            ina, inb = sgn * a[axis] <= lim, sgn * b[axis] <= lim
                            if ina:
                                nxt.append(a)
                            if ina != inb:
                                t = (lim * sgn - a[axis]) / (b[axis] - a[axis])
                                nxt.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
                        pts = nxt
                        if not pts:
                            break
                    if len(pts) >= 3:
                        area = 0.5 * abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1])))
                        visible += area > 0.5
        report[name] = (dropped, visible)
    return report


# ================================================================ previews

def read_fsm(path):
    with open(path, "rb") as fh:
        data = fh.read()
    _, nv, ni, _, nparts = struct.unpack_from("<4sIIfI", data, 0)
    off = 32 + 48 * nparts
    v = np.frombuffer(data, np.float32, nv * 8, off).reshape(nv, 8)
    idx = np.frombuffer(data, np.uint16, ni, off + nv * 32).reshape(-1, 3)
    return v, idx


def read_ftx(path):
    with open(path, "rb") as fh:
        data = fh.read()
    w, h = struct.unpack_from("<HH", data, 4)
    clut = np.frombuffer(data, np.uint32, 256, 16)
    rgba = np.stack([(clut >> s) & 255 for s in (0, 8, 16)], -1).astype(np.float32) / 255
    lv = np.frombuffer(data, np.uint8, w * h, 1040).reshape(h // 8, w // 16, 8, 16).transpose(0, 2, 1, 3).reshape(h, w)
    return rgba[lv]


def mesh_from_fsm(path, name):
    v, idx = read_fsm(path)
    me = bpy.data.meshes.new(name)
    pos = [(x, -z, y) for x, y, z in v[:, 5:8]]                   # game -> Blender
    me.from_pydata(pos, [], idx.tolist())
    uvl = me.uv_layers.new(name="UV")
    loop_v = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", loop_v)
    uvs = np.stack([v[loop_v, 0], 1.0 - v[loop_v, 1]], -1)
    uvl.data.foreach_set("uv", uvs.reshape(-1))
    nrm = [(nx, -nz, ny) for nx, ny, nz in v[loop_v, 2:5]]
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    me.normals_split_custom_set(nrm)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def display_image(name, arr):
    """Texture as stored (display values), read raw by the shaders."""
    h, w = arr.shape[:2]
    img = bpy.data.images.new(name, w, h)
    img.colorspace_settings.name = "Non-Color"
    px = np.ones((h, w, 4), np.float32)
    px[..., :3] = arr
    img.pixels.foreach_set(px[::-1].reshape(-1))
    return img


def ge_material(name, img, lights=None):
    """Emulates the GE: colour = texture x (lights or white), in display
    values, emitted as is (Standard view transform: display = emitted)."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = "Linear"
    col = tex.outputs["Color"]
    if lights:
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        acc = None
        for d, c in lights["dir"]:
            dot = nt.nodes.new("ShaderNodeVectorMath")
            dot.operation = "DOT_PRODUCT"
            dot.inputs[1].default_value = d
            nt.links.new(geo.outputs["Normal"], dot.inputs[0])
            mx = nt.nodes.new("ShaderNodeMath")
            mx.operation = "MAXIMUM"
            mx.inputs[1].default_value = 0.0
            nt.links.new(dot.outputs["Value"], mx.inputs[0])
            sc = nt.nodes.new("ShaderNodeVectorMath")
            sc.operation = "SCALE"
            sc.inputs[0].default_value = c
            nt.links.new(mx.outputs["Value"], sc.inputs["Scale"])
            if acc is None:
                add = nt.nodes.new("ShaderNodeVectorMath")
                add.operation = "ADD"
                add.inputs[1].default_value = lights["ambient"]
                nt.links.new(sc.outputs["Vector"], add.inputs[0])
            else:
                add = nt.nodes.new("ShaderNodeVectorMath")
                add.operation = "ADD"
                nt.links.new(acc.outputs["Vector"], add.inputs[0])
                nt.links.new(sc.outputs["Vector"], add.inputs[1])
            acc = add
        clamp = nt.nodes.new("ShaderNodeVectorMath")
        clamp.operation = "MINIMUM"
        clamp.inputs[1].default_value = (1.0, 1.0, 1.0)
        nt.links.new(acc.outputs["Vector"], clamp.inputs[0])
        mul = nt.nodes.new("ShaderNodeVectorMath")
        mul.operation = "MULTIPLY"
        nt.links.new(col, mul.inputs[0])
        nt.links.new(clamp.outputs["Vector"], mul.inputs[1])
        col = mul.outputs["Vector"]
    gam = nt.nodes.new("ShaderNodeGamma")                          # display -> linear for the emission
    gam.inputs["Gamma"].default_value = 2.2
    nt.links.new(col, gam.inputs["Color"])
    nt.links.new(gam.outputs["Color"], em.inputs["Color"])
    # GU_CULL_FACE with GU_CCW: back faces are not drawn (see-through here),
    # so a wrongly wound face shows up in the preview as a hole.
    cull = nt.nodes.new("ShaderNodeMixShader")
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    bf = nt.nodes.new("ShaderNodeNewGeometry")
    nt.links.new(bf.outputs["Backfacing"], cull.inputs["Fac"])
    nt.links.new(em.outputs["Emission"], cull.inputs[1])
    nt.links.new(tr.outputs["BSDF"], cull.inputs[2])
    nt.links.new(cull.outputs["Shader"], out.inputs["Surface"])
    return mat


def blob_shadow():
    """Soft shadow the game draws under the aircraft (approximation): a
    multiplying plane 2 cm above the floor."""
    n = 128
    yy, xx = np.mgrid[0:n, 0:n]
    u, v = (xx + 0.5) / n * 2 - 1, (yy + 0.5) / n * 2 - 1        # -1..1 over 18 x 24 m
    planform = np.exp(-((u / 0.62) ** 2 + (v / 0.80) ** 2) * 2.2) * 0.38
    core = np.exp(-((u / 0.11) ** 2 + (v / 0.82) ** 2) * 2.0) * 0.25
    k = 1.0 - np.clip(planform + core, 0, 0.6)
    img = bpy.data.images.new("blob", n, n)
    img.colorspace_settings.name = "Non-Color"
    px = np.ones((n, n, 4), np.float32)
    px[..., :3] = k[..., None]
    img.pixels.foreach_set(px[::-1].reshape(-1))
    me = bpy.data.meshes.new("blob")
    me.from_pydata([(-9, -12.5, F + 0.02), (9, -12.5, F + 0.02), (9, 11.5, F + 0.02), (-9, 11.5, F + 0.02)], [],
                   [(0, 1, 2, 3)])
    uvl = me.uv_layers.new(name="UV")
    for li, uv in zip(range(4), ((0, 0), (1, 0), (1, 1), (0, 1))):
        uvl.data[li].uv = uv
    mat = bpy.data.materials.new("blob")
    mat.use_nodes = True
    nt = mat.node_tree
    for nn in list(nt.nodes):
        nt.nodes.remove(nn)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    gam = nt.nodes.new("ShaderNodeGamma")
    gam.inputs["Gamma"].default_value = 2.2
    nt.links.new(tex.outputs["Color"], gam.inputs["Color"])
    nt.links.new(gam.outputs["Color"], tr.inputs["Color"])
    nt.links.new(tr.outputs["BSDF"], out.inputs["Surface"])
    me.materials.append(mat)
    obj = bpy.data.objects.new("blob", me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# Cameras, Blender coordinates (eye, target). Vertical FOV 55 deg, near 1 m.
CAMERAS = {
    "menu": ((-8.4, 17.9, F + 2.2), (6.3, 3.0, -0.8)),
    "profile": ((-5.2, 14.4, F + 2.1), (0.2, 7.6, -0.35)),
    "wide": ((2.6, 18.0, F + 6.6), (-0.6, -1.5, -1.2)),
}
# Hangar-screen orbit samples: (yaw from the nose, deg; pitch; radius) about
# the aircraft origin, clamped to the shelter (orbit_limit).
ORBIT = {"orbit_front": (30.0, 10.0, 16.0), "orbit_rear": (215.0, 8.0, 14.0), "orbit_high": (300.0, 24.0, 14.0)}
EYE_CLEARANCE = 3.0


def inside(p, clear=EYE_CLEARANCE):
    """Eye at least `clear` from the arch, the walls and the floor plane."""
    x, y, z = p
    a, b = A - clear, B - clear
    h = z - F
    if h < 0.5 or y < Y_BACK + clear or y > Y_FRONT - clear:
        return False
    return (x / a) ** 2 + (h / b) ** 2 <= 1.0


def orbit_eye(yaw, pitch, radius, target=(0.0, 0.0, 0.0)):
    """Eye on the orbit, pulled in along the view ray until it is inside
    (game: yaw 0 = in front of the nose, i.e. towards the door)."""
    yw, pt = math.radians(yaw), math.radians(pitch)
    d = Vector((math.sin(yw) * math.cos(pt), math.cos(yw) * math.cos(pt), math.sin(pt)))
    r = radius
    while r > 3.0 and not inside(Vector(target) + d * r):
        r -= 0.05
    return Vector(target) + d * r, r


def render_previews(shell_shown, floor_shown, lights, size_list=((960, 544), (480, 272))):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 16
    sc.cycles.max_bounces = 1
    sc.cycles.transparent_max_bounces = 4
    sc.cycles.use_denoising = False
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    bg = sc.world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = tuple(srgb_decode(np.array(CLEAR_COLOR) / 255.0)) + (1.0,)
    bg.inputs["Strength"].default_value = 1.0

    models = os.path.join(ROOT, "assets", "models")
    for name, tex in (("hangar", shell_shown), ("hangar_floor", floor_shown)):
        o = mesh_from_fsm(os.path.join(models, f"{name}.fsm"), name)
        o.data.materials.append(ge_material(name, display_image(name, tex)))
    aircraft = [("su35s", "su35s_vks"), ("su35s_gear", "su35s_gear_std")]
    for mesh, tex in aircraft:
        fp, tp = os.path.join(models, f"{mesh}.fsm"), os.path.join(models, f"{tex}.ftx")
        if os.path.exists(fp) and os.path.exists(tp):
            o = mesh_from_fsm(fp, mesh)
            o.data.materials.append(ge_material(mesh, display_image(tex, read_ftx(tp)), lights))
    blob_shadow()

    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(55.0)
    cam.data.clip_start, cam.data.clip_end = 1.0, 150.0
    views = dict(CAMERAS)
    for name, (yaw, pitch, radius) in ORBIT.items():
        eye, r = orbit_eye(yaw, pitch, radius)
        views[name] = (tuple(eye), (0.0, 0.0, 0.0))
        print(f"[hangar] {name}: yaw {yaw} pitch {pitch} radius {radius} -> clamped {r:.1f} m")
    prev = os.path.join(ROOT, "build", "previews")
    for name, (eye, target) in views.items():
        cam.location = eye
        cam.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()
        for w, h in size_list:
            sc.render.resolution_x, sc.render.resolution_y = w, h
            sc.render.pixel_aspect_x = sc.render.pixel_aspect_y = 1.0
            sc.render.filter_size = 1.2 if w > 480 else 0.6
            sc.render.filepath = os.path.join(prev, f"hangar_{name}{'' if w > 480 else '_psp'}.png")
            bpy.ops.render.render(write_still=True)
    return views


# ================================================================ light probe fit

def fit_lights():
    """White probe spheres at the aircraft, baked like the shelter; fit the
    GE model ambient + key max(0, n.k) + fill max(0, n.f) in display values.
    Returns {probe: (ambient, key dir, key rgb, fill dir, fill rgb, rms)}."""
    probes = {"centre": (0.0, 0.0, 0.0), "nose": (0.0, 7.5, -0.2), "tail": (0.0, -7.5, 0.4)}
    out = {}
    white = float_image("white", np.ones((4, 4, 3), np.float32))
    for name, p in probes.items():
        bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.5, location=p)
        o = bpy.context.active_object
        o.name = "probe_" + name
        tgt = bpy.data.images.new("probe_" + name, 128, 64, float_buffer=True)
        bake_material(o, white, tgt)
        bake(o)
        L = read_image(tgt)
        me = o.data
        ns, vals = [], []
        uvd = me.uv_layers.active.data
        for poly in me.polygons:
            u = np.mean([uvd[li].uv[0] for li in poly.loop_indices])
            v = np.mean([uvd[li].uv[1] for li in poly.loop_indices])
            r, c = int((1 - v) * 63.999), int(u * 127.999)
            ns.append(tuple(poly.normal))
            vals.append(L[r, c])
        bpy.data.objects.remove(o)
        ns = np.array(ns)
        disp = srgb_encode(tonemap(EXPOSURE * np.array(vals)))     # white surface as the PSP shows it
        best = None
        for kaz in range(-40, 41, 5):
            for kel in range(-10, 46, 5):
                kd = np.array([math.sin(math.radians(kaz)) * math.cos(math.radians(kel)),
                               math.cos(math.radians(kaz)) * math.cos(math.radians(kel)), math.sin(math.radians(kel))])
                for fel in (90, 75, 60):
                    for faz in (0, 180):
                        fd = np.array([0.0, math.cos(math.radians(fel)) * (1 if faz == 0 else -1),
                                       math.sin(math.radians(fel))])
                        X = np.stack([np.ones(len(ns)), np.maximum(ns @ kd, 0), np.maximum(ns @ fd, 0)], -1)
                        coef = np.clip(np.linalg.lstsq(X, disp, rcond=None)[0], 0, None)
                        err = np.sqrt(np.mean((X @ coef - disp) ** 2))
                        if best is None or err < best[0]:
                            best = (err, kd, fd, coef)
        err, kd, fd, coef = best
        out[name] = dict(ambient=coef[0], key_dir=kd, key=coef[1], fill_dir=fd, fill=coef[2], rms=err,
                         min=disp.min(0), max=disp.max(0))
    return out


def game(v):
    return (v[0], v[2], -v[1])


# ================================================================ main

def main():
    t0 = time.time()
    samples = _arg("--samples", 384)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    out_dir = os.path.join(ROOT, "assets", "models")
    prev_dir = os.path.join(ROOT, "build", "previews")
    os.makedirs(prev_dir, exist_ok=True)

    shell, floor = build_shell(), build_floor()
    scale = pack(shell)
    print(f"[hangar] shell: {len(shell.faces)} polygons, {shell.islands} islands, arch {scale:.1f} texels/m")
    groups = {"arch": ("arch",), "walls + reveal": ("back", "front", "reveal"), "apron": ("apron",), "backdrop": ("sky",),
              "lamps": ("lamp_shade", "lamp_lens", "rod"), "trays": ("tray",)}
    tri = {k: sum(len(f["idx"]) - 2 for f in shell.faces if f["mat"] in v) for k, v in groups.items()}
    tri["props"] = sum(len(f["idx"]) - 2 for f in shell.faces) - sum(tri.values())
    print("[hangar] triangles: " + ", ".join(f"{k} {v}" for k, v in tri.items()))
    eyes = dict(CAMERAS)
    for yaw in range(0, 360, 15):
        for pitch in (5, 15, 25, 35):
            for radius in (14.0, 30.0):
                eyes[f"orbit {yaw}/{pitch}/{radius:.0f}"] = (tuple(orbit_eye(yaw, pitch, radius)[0]), (0.0, 0.0, 0.0))
    band = check_guard_band([shell, floor], eyes)
    worst = max(band.items(), key=lambda kv: kv[1][1])
    print(f"[hangar] GE guard band: {len(eyes)} camera positions, dropped triangles max "
          f"{max(d for d, _ in band.values())}, of them on screen max {worst[1][1]} ({worst[0]})")
    ms, mf = Maps(shell), Maps(floor)
    alb_s = paint_sky(ms, paint_shell(ms))
    alb_f = paint_floor(mf)
    print(f"[hangar] albedo painted ({time.time() - t0:.0f}s), shell atlas coverage {ms.mask.mean() * 100:.0f}%")

    setup_cycles(samples)
    add_lights()
    # The backdrop is painted, not baked, and must not shade the sky light.
    o_shell = to_object(shell, "shell", [f for f in shell.faces if f["mat"] != "sky"])
    o_floor = to_object(floor, "floor")
    lit_s = bpy.data.images.new("lit_shell", TEX, TEX, float_buffer=True)
    lit_f = bpy.data.images.new("lit_floor", TEX, TEX, float_buffer=True)
    bake_material(o_shell, float_image("alb_shell", srgb_decode(alb_s)), lit_s)
    bake_material(o_floor, float_image("alb_floor", srgb_decode(alb_f)), lit_f)
    bake(o_shell)
    bake(o_floor)
    print(f"[hangar] light baked, {samples} samples ({time.time() - t0:.0f}s)")
    probes = fit_lights()

    shown = {}
    for name, g, maps, alb, lit in (("hangar", shell, ms, alb_s, lit_s), ("hangar_floor", floor, mf, alb_f, lit_f)):
        light = read_image(lit)
        # Texels of the arch / walls / floor outside the room (overlaps past
        # the seams) saw the sky in the bake: refill them from the inside,
        # or filtering and mips bleed a bright line along the seams.
        x, y, z = maps.x, maps.y, maps.z
        room = maps.m("arch", "back", "front", "floor")
        valid = ~room | ((z >= F - 1e-3) & (y >= Y_BACK - 1e-3) & (np.abs(x) <= A + 1e-3)
                         & ((y <= Y_FRONT + 1e-3) | (np.abs(x) <= OPEN_HW + 1e-3)))
        light = dilate(light, maps.mask & valid, 6)
        light = blur_masked(light, maps.mask, 1)
        rad = srgb_decode(alb) * light * EXPOSURE
        disp = srgb_encode(tonemap(rad))
        if name == "hangar":
            lens = maps.m("lamp_lens")
            disp[lens] = rgb((255, 248, 232))
            sky = maps.m("sky")
            disp[sky] = alb[sky]
        disp = dilate(disp, maps.mask, 10)
        tris, verts, radius, nbytes = export_fsm(g, os.path.join(out_dir, f"{name}.fsm"))
        shown[name] = ftx.export_quantized(os.path.join(out_dir, f"{name}.ftx"), disp, max_levels=4)
        png.write_png(os.path.join(prev_dir, f"{name}_atlas.png"), shown[name])
        v, idx = read_fsm(os.path.join(out_dir, f"{name}.fsm"))
        a, b, c = v[idx[:, 0], 5:8], v[idx[:, 1], 5:8], v[idx[:, 2], 5:8]
        wrong = int(((np.cross(b - a, c - a) * v[idx[:, 0], 2:5]).sum(1) <= 0).sum())
        print(f"[hangar] {name}.fsm winding: {wrong} of {len(idx)} triangles not CCW from their visible side")
        print(f"[hangar] {name}.fsm: {tris} triangles, {verts} vertices, radius {radius:.1f} m, {nbytes} bytes; "
              f"{name}.ftx {TEX}x{TEX} T8, {os.path.getsize(os.path.join(out_dir, name + '.ftx'))} bytes")

    lines = ["# Dynamic lights matching the baked shelter (game axes: x right, y up, z aft; "
             "direction = unit vector TO the light; colours 0..255 display values)"]
    for name, pr in probes.items():
        kd, fd = game(pr["key_dir"]), game(pr["fill_dir"])
        c = lambda v: tuple(int(round(min(1.0, max(0.0, x)) * 255)) for x in v)   # noqa: E731
        lines.append(f"{name}: ambient {c(pr['ambient'])}  key dir ({kd[0]:+.3f}, {kd[1]:+.3f}, {kd[2]:+.3f}) "
                     f"rgb {c(pr['key'])}  fill dir ({fd[0]:+.3f}, {fd[1]:+.3f}, {fd[2]:+.3f}) rgb {c(pr['fill'])}  "
                     f"rms {pr['rms'] * 255:.1f}  white probe min {c(pr['min'])} max {c(pr['max'])}")
    with open(os.path.join(ROOT, "build", "hangar_lights.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\n".join("[hangar] " + s for s in lines))

    print("[hangar] orbit about the aircraft origin, largest radius with the eye 3 m inside (yaw 0 = nose/door):")
    print("[hangar]   yaw " + " ".join(f"{p:>5d}" for p in (5, 10, 15, 20, 25, 30, 35)) + "  (pitch, deg)")
    for yaw in range(0, 181, 15):
        row = [orbit_eye(yaw, p, 30.0)[1] for p in (5, 10, 15, 20, 25, 30, 35)]
        print(f"[hangar]   {yaw:3d} " + " ".join(f"{r:5.1f}" for r in row))
    if "--no-preview" not in ARGS:
        c = probes["centre"]
        lights = dict(ambient=tuple(float(v) for v in c["ambient"]),
                      dir=[(tuple(c["key_dir"]), tuple(float(v) for v in c["key"])),
                           (tuple(c["fill_dir"]), tuple(float(v) for v in c["fill"]))])
        views = render_previews(shown["hangar"], shown["hangar_floor"], lights)
        for name, (eye, target) in views.items():
            ge, gt = game(eye), game(target)
            print(f"[hangar] camera {name}: eye ({ge[0]:+.2f}, {ge[1]:+.2f}, {ge[2]:+.2f}) "
                  f"target ({gt[0]:+.2f}, {gt[1]:+.2f}, {gt[2]:+.2f})")
    print(f"[hangar] done ({time.time() - t0:.0f}s)")


main()
