"""R-77 (RVV-AE, AA-12 "Adder") medium-range active radar missile.

Real dimensions: length 3.60 m, body diameter 0.200 m, wing span 0.40 m,
grid-fin span 0.70 m (docs/assets/r77.md). Origin = mid-length point on the
axis (the station attach point), nose +Y. Wings and grid fins lie in the same
planes, "x" arrangement as on the AKU-170 launcher.

Grid fins: an open frame plus a coarse diagonal lattice built from zero-thick
double-sided plates (16 + 24 triangles a fin); real cells are finer, but the
see-through frame with crossing bars reads as a grid fin at game distance.

Only the right half is modelled (fins at +45 / -45 deg) - the kit mirrors it.
"""

import math

import numpy as np

from fmodel import DIAMOND, MeshBuilder, mix, radial_fin_rings

from mathutils import Vector

TEX = 128
UV_ROTATE = "AXIS_ALIGNED"
AO_DISTANCE = 0.5          # occlusion scale of a 0.2 m body, not of an airframe

LENGTH = 3.60
RADIUS = 0.100
NOSE = LENGTH / 2


def y_at(d):
    return NOSE - d


RADOME_END = 0.56
# (r, d of the leading edge, chord, thickness): root first.
WING = [(0.093, 2.000, 0.800, 0.012), (0.200, 2.060, 0.680, 0.006)]
# Grid fin frame: radial r0..r1, half-width (tangential), leading/trailing d.
GRID_R0, GRID_R1, GRID_HW = 0.100, 0.345, 0.085
GRID_D0, GRID_D1 = 3.400, 3.450
GRID_BARS = (-0.075, 0.0, 0.075)     # u +- v = c, u radial from the frame centre


def _clip_line(c, sign, hu, hv):
    """Segment of u + sign*v = c inside |u| <= hu, |v| <= hv (or None)."""
    pts = []
    for v in (-hv, hv):
        u = c - sign * v
        if -hu - 1e-9 <= u <= hu + 1e-9:
            pts.append((u, v))
    for u in (-hu, hu):
        v = (c - u) * sign
        if -hv - 1e-9 <= v <= hv + 1e-9:
            pts.append((u, v))
    pts = sorted(set((round(a, 6), round(b, 6)) for a, b in pts))
    return (pts[0], pts[-1]) if len(pts) >= 2 and pts[0] != pts[-1] else None


def grid_fin(mb, angle):
    a = math.radians(angle)
    er = Vector((math.cos(a), 0.0, math.sin(a)))
    et = Vector((-math.sin(a), 0.0, math.cos(a)))
    rc, hu = 0.5 * (GRID_R0 + GRID_R1), 0.5 * (GRID_R1 - GRID_R0)
    y0, y1 = y_at(GRID_D0), y_at(GRID_D1)

    def p(u, v, y):
        return er * (rc + u) + et * v + Vector((0.0, y, 0.0))

    def wall(a_uv, b_uv, part):
        (ua, va), (ub, vb) = a_uv, b_uv
        mb.plate([p(ua, va, y0), p(ub, vb, y0), p(ub, vb, y1), p(ua, va, y1)], part)

    hv = GRID_HW
    corners = [(-hu, -hv), (hu, -hv), (hu, hv), (-hu, hv)]
    for c0, c1 in zip(corners, corners[1:] + corners[:1]):
        wall(c0, c1, "grid_frame")
    for c in GRID_BARS:
        for sign in (1, -1):
            seg = _clip_line(c, sign, hu, hv)
            if seg:
                wall(seg[0], seg[1], "grid_bar")


def build():
    mb = MeshBuilder()
    # Tangent-ogive radome, blunted at the tip; cylinder to a flat tail.
    profile = [(0.0, 0.0), (0.120, 0.041), (0.300, 0.080), (RADOME_END, RADIUS), (NOSE, RADIUS),
               (LENGTH, RADIUS)]
    # Split at mid-length: two 1.8 m UV islands instead of one 3.6 m strip.
    mb.revolve([(y_at(d), r) for d, r in profile], "body", smooth=60, seg=12, cap_part="nozzle",
               breaks=(4,))
    for angle in (45, -45):
        stations = [dict(r=r, y=y_at(d), chord=c, t=t) for r, d, c, t in WING]
        mb.loft(radial_fin_rings(stations, angle, DIAMOND), "wing", smooth=0.0, cap_start=False)
        grid_fin(mb, angle)
    return mb


# ================================================================ paint

JOINTS = [RADOME_END, 0.60, 1.21, 1.30, 1.53, 1.85, 3.12, 3.30]


def paint(m, lv):
    x, y, z, nz = m.x, m.y, m.z, m.nz
    d = NOSE - y
    ang = np.degrees(np.arctan2(z, x))
    body = m.is_part("body")

    img = np.zeros(m.part.shape + (3,), np.float32)
    img[:] = np.array(lv["base"], np.float32) / 255

    # Dielectric radome: slightly warmer, smoother paint; dark rain-erosion tip.
    rad = (body & (d < RADOME_END)).astype(np.float32)
    mix(img, lv["radome"], rad)
    mix(img, (92, 92, 88), m.fill(d - 0.035) * rad)

    lines = np.zeros(m.part.shape, np.float32)
    for j in JOINTS:
        np.maximum(lines, m.line(d - j, 0.8) * body, out=lines)
    img *= (1.0 - 0.36 * lines)[..., None]

    # Cable raceway along the bottom and the suspension lugs on top.
    race = body & (d > 1.10) & (d < 2.05) & (np.abs(ang + 90) < 9)
    mix(img, (176, 178, 178), race.astype(np.float32))
    for lug in (1.25, 2.30):
        mix(img, (96, 100, 104), (body & (np.abs(d - lug) < 0.03) & (np.abs(ang - 90) < 12)).astype(np.float32))

    # Grid-fin actuator hatches at the tail (4, between the fins at 0/90/180/270 deg).
    hatch = body & (d > 3.12) & (d < 3.30)
    for c in (0, 90, 180, -90):
        h = hatch & (np.abs(((ang - c + 180) % 360) - 180) < 12)
        mix(img, (150, 154, 156), h.astype(np.float32) * 0.8)

    # Stencils on the sides of the motor section.
    for a0, half in ((12, 0.11), (5, 0.08), (-2, 0.095)):
        sten = body & (np.abs(d - 2.40) < half) & (np.abs(ang - a0) < 2.0)
        mix(img, (78, 82, 86), sten.astype(np.float32) * 0.7)

    # Grid fins: bare light metal frame, darker lattice.
    mix(img, lv["grid"], m.is_part("grid_frame").astype(np.float32))
    mix(img, lv["lattice"], m.is_part("grid_bar").astype(np.float32))

    # Tail: nozzle exit.
    exit_ = m.is_part("nozzle").astype(np.float32)
    mix(img, (26, 24, 22), exit_)
    mix(img, (104, 102, 98), np.clip((np.hypot(x, z) - 0.070) / 0.01, 0, 1) * exit_)
    mix(img, (120, 118, 114), (body & (d > LENGTH - 0.03)).astype(np.float32))
    return img


LIVERIES = {
    # Serial rounds: white body, off-white radome, natural-metal grid fins.
    "std": dict(base=(228, 230, 230), radome=(222, 220, 208), grid=(196, 198, 200), lattice=(176, 178, 180)),
}
