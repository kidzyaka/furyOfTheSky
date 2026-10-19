"""Su-35S landing gear, extended - drawn with the airframe's own model
transform, only while the aircraft stands on the ground (hangar, runway).

Su-27 family numbers (docs/assets/su35s.md, "Шасси"): track 4.34 m,
wheelbase 5.8-5.88 m, main wheels 1030 x 350 on telescopic legs raked
2 deg 43', twin-wheel semi-levered nose leg with a mudguard (Su-35 family),
wheels 620 x 180 (not verified for the Su-35S).

The gear is modelled in the airframe's Blender space (x right wing, y nose,
z up), right half only - the kit mirrors it. The airframe mesh has no bays:
dark bay plates 15 mm under its skin and the open doors along their edges
read as the opening. Fit is checked against the real su35s mesh on every
build (ray casts / BVHTree, printed as [gear] lines).
"""

import math

import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

from fmodel import MeshBuilder, mix, smoothstep

TEX = 256
AO_DISTANCE = 0.8           # occlusion scale of legs and wheels, not of an airframe
HOST = "su35s"              # build.py renders the gear mounted on this aircraft

# Ground plane in the airframe's model coordinates (game y = Blender z):
# fin tips at 3.50 and the real overall height 5.9 m.
GROUND_Y = -2.40
# Static attitude on the ground. The Su-27 parking angle is 0 deg 16'
# (2.7 cm over the wheelbase), so the model stands level.
GROUND_PITCH_DEG = 0.0

TRACK = 4.34
WHEELBASE = 5.88
MAIN_X = TRACK / 2
MAIN_Y = -1.33              # main axle station (Blender y; game z = -MAIN_Y)
NOSE_Y = MAIN_Y + WHEELBASE  # 4.55, under the aft end of the canopy

MAIN_R, MAIN_W, MAIN_SEG = 0.515, 0.35, 12
NOSE_R, NOSE_W, NOSE_SEG = 0.31, 0.18, 10
# Polygonal tyres stand on a flat edge (r cos(pi/seg) below the axle), sunk
# 1 cm into the ground: the contact patch of a loaded tyre, no gap on the floor.
SINK = 0.010
MAIN_AXLE_Z = GROUND_Y + MAIN_R * math.cos(math.pi / MAIN_SEG) - SINK
NOSE_AXLE_Z = GROUND_Y + NOSE_R * math.cos(math.pi / NOSE_SEG) - SINK
NOSE_WHEEL_X = 0.14         # twin wheels either side of the lever

# Main leg: on the inboard side of the wheel, between it and the nacelle wall
# (x 1.73); raked 2 deg 43' (top forward). Trunnion buried in the wing root.
LEG_X = 1.90
LEG_RAKE = math.radians(2 + 43 / 60)
LEG_TOP_Z = 0.06            # inside the wing (its underside is at -0.02..-0.05)

# Nose leg: strut ahead of the axle, trailing lever to the wheels.
NOSE_STRUT_Y = NOSE_Y + 0.30
NOSE_TOP_Z = -0.40          # inside the belly (underside -0.56..-0.60)

# Doors and bay openings (right half). Main bay: forward of the trunnion,
# the wheel turns flat into the wing root. Nose bay: under the cockpit.
MAIN_BAY = dict(x0=1.76, x1=2.52, y0=0.02, y1=-1.15)       # aft of the wing LE (y 0.1 at x 2.5)
MAIN_DOOR = dict(x=2.52, y0=-0.02, y1=-1.10, h=0.62, splay=math.radians(12))
NOSE_BAY = dict(x0=0.0, x1=0.29, y0=6.60, y1=4.62)
NOSE_DOOR = dict(x=0.29, y0=6.55, y1=4.70, h=0.32, splay=math.radians(8))
DOOR_T = 0.016


# ================================================================ geometry helpers

def _basis(axis):
    a = Vector(axis).normalized()
    u = a.orthogonal().normalized()
    return a, u, a.cross(u)


def ring(center, axis, radius, seg, phase=0.0):
    a, u, v = _basis(axis)
    c = Vector(center)
    return [c + (u * math.cos(phase + 2 * math.pi * i / seg) + v * math.sin(phase + 2 * math.pi * i / seg)) * radius
            for i in range(seg)]


def tube(mb, p0, p1, r0, r1, part, seg=8, smooth=60, caps=(True, True)):
    p0, p1 = Vector(p0), Vector(p1)
    axis = p1 - p0
    mb.loft([ring(p0, axis, r0, seg), ring(p1, axis, r1, seg)], part, smooth,
            cap_start=caps[0], cap_end=caps[1])


def box(mb, center, size, part, rot=None):
    """Axis box (size = full extents x, y, z), optionally rotated about its centre."""
    c, (sx, sy, sz) = Vector(center), size
    rot = rot or Matrix.Identity(3)
    rings = []
    for y in (-sy / 2, sy / 2):
        rings.append([c + rot @ Vector((x, y, z)) for x, z in
                      ((-sx / 2, -sz / 2), (sx / 2, -sz / 2), (sx / 2, sz / 2), (-sx / 2, sz / 2))])
    mb.loft(rings, part, 0.0)


def wheel(mb, cx, cy, cz, profile, seg):
    """Tyre + hub as one closed shell of revolution about an x-parallel axle.
    profile: (dx from the wheel's mid-plane, radius), inboard hub face first.
    Vertices sit at +-pi/seg either side of straight down: a flat bottom edge."""
    angles = [math.pi / seg + 2 * math.pi * i / seg for i in range(seg)]
    rings = [[Vector((cx + dx, cy + r * math.sin(t), cz - r * math.cos(t))) for t in angles] for dx, r in profile]
    mb.loft(rings, "tyre", 50, cap_part="hub")


def door(mb, d, hinge_z):
    """Flat door hanging from a hinge line along y at x = d["x"], splayed
    outboard by d["splay"]; hinge_z(y) gives the skin height (top edge 1 cm in)."""
    t, h, s = DOOR_T, d["h"], d["splay"]
    out = Vector((math.sin(s), 0.0, -math.cos(s)))       # down the door, outboard
    nrm = Vector((math.cos(s), 0.0, math.sin(s)))        # door normal, outboard
    rings = []
    for y in (d["y0"], d["y1"]):
        top = Vector((d["x"], y, hinge_z(y) + 0.010))
        bot = top + out * h
        rings.append([top - nrm * t / 2, bot - nrm * t / 2, bot + nrm * t / 2, top + nrm * t / 2])
    mb.loft(rings, "door", 0.0)


def bay_plate(mb, bay, skin_z, cols=2, rows=2):
    """Dark plate 15 mm under the skin over a bay opening, facing down."""
    xs = [bay["x0"] + (bay["x1"] - bay["x0"]) * i / cols for i in range(cols + 1)]
    ys = [bay["y0"] + (bay["y1"] - bay["y0"]) * j / rows for j in range(rows + 1)]
    for j in range(rows):
        for i in range(cols):
            quad = [(xs[i], ys[j]), (xs[i + 1], ys[j]), (xs[i + 1], ys[j + 1]), (xs[i], ys[j + 1])]
            pts = [Vector((x, y, skin_z(x, y) - 0.015)) for x, y in quad]
            if (pts[1] - pts[0]).cross(pts[2] - pts[0]).z > 0:
                pts.reverse()                              # normal down: seen from below
            mb.plate(pts, "bay", double=False)


# ================================================================ airframe

_AIRFRAME = None


def airframe_tree():
    """BVH of the full (mirrored) su35s mesh - fit checks and occlusion."""
    global _AIRFRAME
    if _AIRFRAME is None:
        import bmesh
        import su35s
        bm = su35s.build().bm
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        dup = bmesh.ops.duplicate(bm, geom=geom)["geom"]
        for v in (e for e in dup if isinstance(e, bmesh.types.BMVert)):
            v.co.x = -v.co.x
        bmesh.ops.reverse_faces(bm, faces=[e for e in dup if isinstance(e, bmesh.types.BMFace)])
        bm.normal_update()
        _AIRFRAME = (BVHTree.FromBMesh(bm), bm)
    return _AIRFRAME[0]


def skin_below(x, y, start=0.6):
    """Lowest airframe skin at (x, y): ray cast up from below."""
    tree = airframe_tree()
    hit = tree.ray_cast(Vector((x, y, -1.8)), Vector((0, 0, 1)), 4.0)
    return hit[0].z if hit[0] is not None else start


# ================================================================ build

def build():
    mb = MeshBuilder()

    # ---- main leg (right) -------------------------------------------------------
    lean = Vector((0.0, -math.sin(LEG_RAKE), -math.cos(LEG_RAKE)))   # down and slightly aft
    axle = Vector((LEG_X, MAIN_Y, MAIN_AXLE_Z))
    top = axle - lean * ((LEG_TOP_Z - MAIN_AXLE_Z) / math.cos(LEG_RAKE))

    def along(z):                                                    # point on the leg at height z
        return axle - lean * ((z - MAIN_AXLE_Z) / math.cos(LEG_RAKE))

    # Ends buried in the wing, the cylinder or the knuckle have no caps.
    tube(mb, top, along(-1.12), 0.088, 0.096, "leg", seg=8, caps=(False, True))   # oleo cylinder
    tube(mb, along(-1.10), along(-1.84), 0.058, 0.058, "piston", seg=8, caps=(False, False))
    box(mb, along(-1.86) + Vector((0.03, 0, 0)), (0.20, 0.17, 0.16), "leg")   # axle knuckle
    tube(mb, (LEG_X, MAIN_Y, MAIN_AXLE_Z), (MAIN_X - 0.05, MAIN_Y, MAIN_AXLE_Z),
         0.05, 0.05, "leg", seg=6, caps=(False, False))               # stub axle into the hub
    # Torque links on the front of the strut: upper and lower arm meeting forward.
    knee = along(-1.47) + Vector((0, 0.20, 0))
    for a, b in ((along(-1.14), knee), (knee, along(-1.80))):
        mid, d = (a + b) / 2, b - a
        rot = d.to_track_quat("Z", "Y").to_matrix()
        box(mb, mid + Vector((0, 0.07, 0)), (0.045, 0.06, d.length), "link", rot)
    # Drag brace (gear retracts forward): leg -> wing root ahead of the trunnion.
    tube(mb, along(-0.62), Vector((LEG_X + 0.04, MAIN_Y + 0.95, 0.03)), 0.042, 0.042, "link", seg=6,
         caps=(False, False))
    # Brake line down the back of the leg.
    tube(mb, along(-0.05) + Vector((0.05, -0.09, 0)), along(-1.70) + Vector((0.07, -0.07, 0)),
         0.012, 0.012, "link", seg=4, caps=(False, False))

    wheel(mb, MAIN_X, MAIN_Y, MAIN_AXLE_Z,
          [(-0.11, 0.27), (-0.172, 0.37), (-0.125, MAIN_R), (0.125, MAIN_R), (0.172, 0.37), (0.11, 0.27)],
          seg=MAIN_SEG)

    door(mb, MAIN_DOOR, lambda y: skin_below(MAIN_DOOR["x"] - 0.03, y))
    bay_plate(mb, MAIN_BAY, skin_below, cols=2, rows=2)

    # ---- nose leg ----------------------------------------------------------------
    sx = NOSE_STRUT_Y
    tube(mb, (0, sx, NOSE_TOP_Z), (0, sx, -1.28), 0.066, 0.066, "leg", seg=8, caps=(False, True))
    tube(mb, (0, sx, -1.20), (0, sx, -1.27), 0.086, 0.086, "leg", seg=8)        # steering collar
    tube(mb, (0, sx, -1.28), (0, sx, -1.80), 0.046, 0.046, "piston", seg=8, caps=(False, False))
    box(mb, (0, sx, -1.84), (0.14, 0.16, 0.14), "leg")                         # fork head
    # Trailing lever: fork head -> axle; shock link above it.
    lever = Vector((0, NOSE_Y, NOSE_AXLE_Z)) - Vector((0, sx, -1.86))
    rot = lever.to_track_quat("Z", "X").to_matrix()
    box(mb, (Vector((0, sx, -1.86)) + Vector((0, NOSE_Y, NOSE_AXLE_Z))) / 2, (0.06, 0.07, lever.length + 0.08),
        "leg", rot)
    tube(mb, (-0.05, NOSE_Y, NOSE_AXLE_Z), (NOSE_WHEEL_X - 0.02, NOSE_Y, NOSE_AXLE_Z),
         0.035, 0.035, "leg", seg=6, caps=(False, False))                   # axle (cut at x = 0)
    tube(mb, (0, sx - 0.05, -1.55), (0, NOSE_Y + 0.08, NOSE_AXLE_Z + 0.07), 0.026, 0.026, "link", seg=6)
    tube(mb, (0, sx, -1.00), (0, sx + 0.85, NOSE_TOP_Z + 0.05), 0.034, 0.034, "link", seg=6,
         caps=(False, False))                                                 # drag brace
    # Taxi / landing lights on the front of the strut.
    for z in (-0.80, -0.98):
        box(mb, (0, sx + 0.10, z), (0.15, 0.08, 0.11), "lamp")

    wheel(mb, NOSE_WHEEL_X, NOSE_Y, NOSE_AXLE_Z,
          [(-0.07, 0.16), (-0.09, 0.24), (-0.05, NOSE_R), (0.05, NOSE_R), (0.09, 0.24), (0.07, 0.16)],
          seg=NOSE_SEG)

    # Mudguard: a curved shield over and behind the twin wheels, on the lever.
    arc = [math.radians(a) for a in (60, 88, 116, 144, 172)]          # from forward, over the top, aft
    rr, hw, t = NOSE_R + 0.06, 0.30, 0.022
    rings = []
    for side in (-1.0, 1.0):
        x = side * hw
        outer = [Vector((x, NOSE_Y + rr * math.cos(a), NOSE_AXLE_Z + rr * math.sin(a))) for a in arc]
        inner = [Vector((x, NOSE_Y + (rr - t) * math.cos(a), NOSE_AXLE_Z + (rr - t) * math.sin(a))) for a in arc]
        rings.append(outer + inner[::-1])
    mb.loft(rings, "guard", 40)
    box(mb, (0, NOSE_Y + 0.09, NOSE_AXLE_Z + 0.215), (0.05, 0.05, 0.30), "leg")      # guard bracket on the lever

    door(mb, NOSE_DOOR, lambda y: skin_below(NOSE_DOOR["x"] - 0.02, y))
    bay_plate(mb, NOSE_BAY, skin_below, cols=3, rows=3)

    check_fit(mb)
    return mb


# ================================================================ fit check

def check_fit(mb):
    """Print, per part, how it meets the airframe: intersections (wanted only
    where a part is buried in the skin) and the smallest clearance."""
    import bmesh
    tree = airframe_tree()
    lp = mb.bm.faces.layers.int["part"]
    names = [p.name for p in mb.parts]
    for pid, name in enumerate(names):
        faces = [f for f in mb.bm.faces if f[lp] == pid]
        sub = bmesh.new()
        vmap = {}
        for f in faces:
            vs = []
            for v in f.verts:
                if v not in vmap:
                    vmap[v] = sub.verts.new(v.co)
                vs.append(vmap[v])
            sub.faces.new(vs)
        hits = len(BVHTree.FromBMesh(sub).overlap(tree))
        gap = min(tree.find_nearest(v.co)[3] for v in vmap)
        lowest = min(v.co.z for v in vmap)
        print(f"[gear] fit {name:7s}: {hits:3d} face pairs intersect the airframe, "
              f"min clearance {gap * 100:5.1f} cm, lowest z {lowest:+.3f}")
        sub.free()
    # Wheel clearance to the nacelle wall and the intake pylon / R-77 (x 1.15).
    inner_face = MAIN_X - MAIN_W / 2
    hit = tree.ray_cast(Vector((inner_face, MAIN_Y, MAIN_AXLE_Z + MAIN_R)), Vector((-1, 0, 0)), 3.0)
    print(f"[gear] main tyre inner face x {inner_face:.3f}; nearest skin inboard at tyre-top height: "
          f"{'none' if hit[0] is None else f'{hit[3]:.2f} m'}")
    print(f"[gear] ground y {GROUND_Y:+.2f}, pitch {GROUND_PITCH_DEG:.1f} deg, main axle {MAIN_AXLE_Z:+.3f}, "
          f"nose axle {NOSE_AXLE_Z:+.3f}, track {TRACK} m, wheelbase {WHEELBASE} m")


# ================================================================ paint

DOOR_RECTS = [MAIN_DOOR, NOSE_DOOR]


def _airframe_occlusion(m, rays=24, distance=3.0):
    """Occlusion of every texel by the airframe above it (bays, legs under
    the wing). fmodel's AO only sees the gear itself."""
    tree = airframe_tree()
    golden = math.pi * (3 - math.sqrt(5))
    dirs = []
    for i in range(rays):
        r = math.sqrt((i + 0.5) / rays)
        a = i * golden
        dirs.append((r * math.cos(a), r * math.sin(a), math.sqrt(1 - r * r)))
    occ = np.ones(m.part.shape, np.float32)
    rows, cols = np.nonzero(m.mask)
    for r, c in zip(rows, cols):
        n = Vector(m.nrm[r, c])
        t = n.orthogonal().normalized()
        b = n.cross(t)
        o = Vector(m.pos[r, c]) + n * 0.01
        hit = 0.0
        for dx, dy, dz in dirs:
            h = tree.ray_cast(o, t * dx + b * dy + n * dz, distance)
            if h[0] is not None:
                hit += 1.0 - 0.5 * (h[3] / distance)
        occ[r, c] = 1.0 - hit / rays
    return occ


def paint(m, lv):
    x, y, z, nx, ny, nz = m.x, m.y, m.z, m.nx, m.ny, m.nz
    img = np.zeros(m.part.shape + (3,), np.float32)
    img[:] = np.array(lv["leg"], np.float32) / 255

    tyre, hub = m.is_part("tyre"), m.is_part("hub")
    main = (x > 1.0)
    cy = np.where(main, MAIN_Y, NOSE_Y)
    cz = np.where(main, MAIN_AXLE_Z, NOSE_AXLE_Z)
    cx = np.where(main, MAIN_X, NOSE_WHEEL_X)
    rr = np.hypot(y - cy, z - cz) / np.where(main, MAIN_R, NOSE_R)       # 1 = tread
    dx = (x - cx) / np.where(main, MAIN_W, NOSE_W)                       # -0.5..0.5 across

    # Wheels: green hub inside the rim, rubber outside; tread grooves, brake dust.
    wheel = (tyre | hub).astype(np.float32)
    rubber = smoothstep(0.60, 0.64, rr) * wheel
    mix(img, lv["hub"], wheel)
    mix(img, lv["tyre"], rubber)
    tread = smoothstep(0.93, 0.97, rr) * (np.abs(dx) < 0.36) * wheel
    mix(img, lv["tread"], tread * 0.7)
    for g in (-0.17, 0.0, 0.17):
        mix(img, (18, 18, 19), m.line((dx - g) * np.where(main, MAIN_W, NOSE_W), 0.8) * tread)
    hubs = wheel * (1 - rubber)
    mix(img, (54, 96, 66), smoothstep(0.50, 0.58, rr) * hubs * 0.8)        # rim flange, darker
    ang = np.arctan2(z - cz, y - cy)
    bolts = (np.abs(rr - 0.30) < 0.03) * (np.cos(ang * 8) > 0.90) * wheel
    mix(img, (40, 58, 46), bolts.astype(np.float32))
    mix(img, (120, 124, 120), smoothstep(0.16, 0.10, rr) * hubs)          # axle nut / cap

    # Struts: painted cylinders, bright chrome pistons with a streak, hydraulic lines.
    piston = m.is_part("piston").astype(np.float32)
    mix(img, lv["chrome"], piston)
    mix(img, (238, 242, 246), np.exp(-((nx + 0.55 * ny - 0.6) / 0.25) ** 2) * piston * 0.7)
    mix(img, lv["link"], m.is_part("link").astype(np.float32))
    lamp = m.is_part("lamp").astype(np.float32)
    mix(img, (70, 74, 78), lamp)
    mix(img, (226, 232, 236), lamp * (ny > 0.7))                         # lens facing forward
    mix(img, lv["guard"], m.is_part("guard").astype(np.float32))

    # Doors: livery grey outside, lighter inside with red edges; dark bays.
    drs = m.is_part("door")
    outer = drs & (nx > 0.3)
    inner = drs & (nx < -0.3)
    mix(img, lv["door_out"], outer.astype(np.float32))
    mix(img, lv["door_in"], inner.astype(np.float32))
    for d in DOOR_RECTS:
        on = inner & (x > d["x"] - 0.05) & (x < d["x"] + d["h"]) & (y <= d["y0"] + 0.01) & (y >= d["y1"] - 0.01)
        if not on.any():
            continue
        bottom = z[on].max() - d["h"] * math.cos(d["splay"])
        edge = np.minimum(np.minimum(y - d["y1"], d["y0"] - y), z - bottom)   # free edges, not the hinge
        mix(img, lv["door_edge"], m.fill(edge - 0.035) * on)
    mix(img, lv["door_in"], (drs & ~outer & ~inner).astype(np.float32))   # thin edges
    bay = m.is_part("bay").astype(np.float32)
    mix(img, lv["bay"], bay)
    # Bay ribs: faint lighter frames across the opening.
    mix(img, (72, 76, 78), bay * (np.cos(y * 2 * math.pi / 0.32) > 0.85) * 0.6)

    # Grime and the airframe's shade over everything.
    img *= (0.94 + 0.06 * np.cos(z * 7.0 + x * 3.0))[..., None]
    img *= (0.35 + 0.65 * _airframe_occlusion(m))[..., None]
    return img


LIVERIES = {
    # One texture for both liveries: neutral greys that sit under either belly.
    "std": dict(leg=(176, 182, 186), chrome=(206, 212, 216), link=(150, 156, 160), hub=(76, 128, 88),
                tyre=(40, 40, 42), tread=(58, 58, 58), guard=(168, 174, 178), door_out=(186, 194, 200),
                door_in=(170, 176, 178), door_edge=(178, 44, 42), bay=(48, 52, 54)),
}
