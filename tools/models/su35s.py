"""Su-35S "Flanker-E" - player-quality model for the modern era.

Real dimensions: length 21.9 m, span 15.3 m (with wingtip rails), height 5.9 m.
No canards (unlike Su-30SM / Su-37), AL-41F1S engines with TVC nozzles.
Stations were measured off planform/profile references at ~23 px/m;
y runs from the nose tip (+11.0) to the tail stinger (-10.9).

Only the right half of paired parts is modelled - the kit mirrors it.
"""

import math

import numpy as np

from fmodel import (MeshBuilder, airfoil, arc_ring, camo, fin_rings, mix, se_ring, smoothstep,
                    spline, star_sdf, surface_coords, wing_rings, fbm)

NACELLE_X = 1.15
NOZZLE_Z = -0.24
RADOME_END = 8.0
FIN_CAP_Z = 3.22
INTAKE_Y = 3.20

_LE = math.tan(math.radians(42))
WING = [dict(x=0.90, y=0.45 + 1.25 * _LE, z=0.10, chord=0.45 + 1.25 * _LE + 4.40, t=0.30),
        dict(x=7.05, y=0.45 - 4.90 * _LE, z=-0.12, chord=1.60, t=0.07)]
LERX = [dict(x=0.70, y=4.70, z=0.16, chord=4.60, t=0.24),
        dict(x=1.10, y=3.85, z=0.145, chord=3.75, t=0.19),
        dict(x=1.45, y=2.95, z=0.13, chord=2.85, t=0.14),
        dict(x=1.80, y=1.80, z=0.115, chord=1.70, t=0.09),
        dict(x=2.15, y=0.48, z=0.10, chord=0.42, t=0.04)]
FIN = [dict(x=2.05, y=-5.60, z=0.15, chord=3.60, t=0.22),
       dict(x=2.05, y=-5.60 - 3.35 * math.tan(math.radians(40)), z=3.50, chord=1.30, t=0.07)]
STAB = [dict(x=2.25, y=-6.40, z=0.00, chord=3.50, t=0.16),
        dict(x=4.94, y=-9.10, z=0.00, chord=1.00, t=0.05)]
VENTRAL = [dict(x=2.05, y=-6.00, z=-0.12, chord=1.50, t=0.08),
           dict(x=2.30, y=-6.70, z=-0.90, chord=0.90, t=0.04)]

THIN_FOIL = airfoil([0.01, 0.05, 0.15, 0.33, 0.58, 0.82], lower=1.0)


def build():
    mb = MeshBuilder()

    # Fuselage: long drooping nose -> lifting-body centre section -> stinger.
    # (y, half-width, top, bottom, z-centre, squareness)
    body = spline([
        (11.00, 0.035, 0.035, 0.035, -0.30, 2.0),
        (10.40, 0.22, 0.22, 0.21, -0.26, 2.0),
        (9.40, 0.40, 0.40, 0.38, -0.18, 2.0),
        (8.00, 0.55, 0.55, 0.50, -0.06, 2.0),
        (7.00, 0.64, 0.66, 0.62, 0.02, 2.1),
        (5.80, 0.74, 0.74, 0.70, 0.08, 2.2),
        (4.60, 0.82, 0.74, 0.70, 0.12, 2.3),
        (3.40, 0.86, 0.70, 0.64, 0.14, 2.3),
        (2.00, 0.98, 0.64, 0.50, 0.13, 2.5),
        (0.30, 1.15, 0.62, 0.42, 0.11, 2.7),
        (-2.20, 1.35, 0.56, 0.36, 0.10, 3.0),
        (-4.50, 1.45, 0.48, 0.32, 0.10, 3.2),
        (-6.50, 1.05, 0.38, 0.28, 0.08, 2.8),
        (-8.50, 0.50, 0.30, 0.24, 0.06, 2.0),
        (-9.80, 0.30, 0.22, 0.18, 0.06, 2.0),
        (-10.60, 0.16, 0.13, 0.12, 0.06, 2.0),
        (-10.90, 0.04, 0.04, 0.04, 0.06, 2.0),
    ], 2)
    mb.loft([se_ring(y, 0, zc, hw, ht, hb, n, 24) for y, hw, ht, hb, zc, n in body],
            "fuselage", smooth=65)

    # Pitot probe and the OLS-35 IRST ball ahead of the windscreen.
    mb.loft([se_ring(y, 0, -0.30, r, r, n=2.0, seg=8)
             for y, r in [(10.90, 0.040), (11.30, 0.028), (11.62, 0.014), (11.70, 0.003)]],
            "pitot", smooth=70)
    irst = spline([(7.98, 0.02, 0.02), (7.88, 0.12, 0.12), (7.74, 0.17, 0.17),
                   (7.60, 0.13, 0.12), (7.50, 0.02, 0.02)], 2)
    mb.loft([arc_ring(y, 0, 0.56, hw, h, seg=10, a0=-10, a1=190) for y, hw, h in irst],
            "irst", smooth=80)

    # Bubble canopy set well forward, then the spine fairing behind it.
    canopy = spline([(7.35, 0.06, 0.03), (7.00, 0.36, 0.26), (6.60, 0.47, 0.42), (6.00, 0.52, 0.50),
                     (5.30, 0.51, 0.48), (4.60, 0.46, 0.38), (4.15, 0.38, 0.26)], 2)
    mb.loft([arc_ring(y, 0, 0.72, hw, h, seg=14) for y, hw, h in canopy], "canopy", smooth=80)
    spine = spline([(4.25, 0.38, 0.27, 0.70), (3.00, 0.44, 0.25, 0.68), (1.00, 0.46, 0.22, 0.66),
                    (-2.00, 0.44, 0.21, 0.60), (-5.00, 0.34, 0.15, 0.52), (-7.50, 0.20, 0.08, 0.44),
                    (-9.20, 0.06, 0.03, 0.36)], 2)
    mb.loft([arc_ring(y, 0, zc, hw, h, seg=12) for y, hw, h, zc in spine], "spine", smooth=65)

    # Engine nacelle, one closed shell: duct floor -> inner lip -> outer lip ->
    # nacelle body. The intake is raked (top lip juts forward).
    nac = [(INTAKE_Y, 0.55, 0.48, 0.52, -0.52, 5.0), (2.20, 0.57, 0.54, 0.56, -0.50, 4.5),
           (0.00, 0.58, 0.58, 0.58, -0.46, 4.0), (-3.00, 0.58, 0.58, 0.58, -0.40, 3.5),
           (-6.00, 0.55, 0.56, 0.55, -0.32, 3.0), (-7.80, 0.50, 0.50, 0.50, -0.26, 2.4),
           (-8.60, 0.47, 0.47, 0.47, -0.24, 2.0)]
    y0, hw0, ht0, hb0, zc0, n0 = nac[0]
    rings = [se_ring(y0 - 0.60, NACELLE_X, zc0, hw0 * 0.80, ht0 * 0.78, hb0 * 0.78, n0, 16),
             se_ring(y0, NACELLE_X, zc0, hw0 * 0.86, ht0 * 0.84, hb0 * 0.84, n0, 16)]
    rings += [se_ring(y, NACELLE_X, zc, hw, ht, hb, n, 16) for y, hw, ht, hb, zc, n in spline(nac, 2)]
    for ring in rings[:3]:
        for v in ring:
            v.y += 0.45 * (v.z - zc0) / ht0
    mb.loft(rings, "nacelle", smooth=55, cap_part="intake")

    # AL-41F1S nozzle: outer petals folding into the exhaust duct.
    noz = [(-8.45, 0.47), (-8.90, 0.47), (-9.30, 0.45), (-9.62, 0.41)]
    rings = [se_ring(y, NACELLE_X, NOZZLE_Z, r, r, seg=20) for y, r in noz]
    rings += [se_ring(-9.62, NACELLE_X, NOZZLE_Z, 0.36, 0.36, seg=20),
              se_ring(-9.05, NACELLE_X, NOZZLE_Z, 0.33, 0.33, seg=20)]
    mb.loft(rings, "nozzle", smooth=55, cap_part="nozzle_in")

    # Lifting surfaces.
    mb.loft(wing_rings(LERX, 1), "lerx", smooth=110)
    mb.loft(wing_rings(WING, 5), "wing", smooth=110)
    mb.loft(fin_rings(FIN, 4, THIN_FOIL), "fin", smooth=110)
    mb.loft(wing_rings(STAB, 3, THIN_FOIL), "stab", smooth=110)
    mb.loft(fin_rings(VENTRAL, 1, THIN_FOIL), "ventral", smooth=110)

    # Wingtip launch rails, protruding ahead of the leading edge.
    rail = spline([(-3.05, 0.015, 0.02), (-3.40, 0.075, 0.095), (-6.10, 0.08, 0.10), (-6.40, 0.02, 0.03)], 1)
    mb.loft([se_ring(y, 7.15, -0.13, hw, h, n=3.0, seg=8) for y, hw, h in rail], "rail", smooth=60)

    # Tail booms outboard of the nacelles carrying fins and stabilators.
    boom = spline([(-3.60, 0.10, 0.05, 0.04), (-4.60, 0.28, 0.17, 0.04), (-7.00, 0.28, 0.18, 0.03),
                   (-9.60, 0.24, 0.15, 0.02), (-10.30, 0.12, 0.08, 0.02), (-10.50, 0.03, 0.03, 0.02)], 2)
    mb.loft([se_ring(y, 2.05, zc, hw, h, n=3.0, seg=12) for y, hw, h, zc in boom], "boom", smooth=60)

    return mb


# ================================================================ paint

FUSELAGE_FRAMES = [RADOME_END, 6.68, 3.95, 1.60, -1.20, -3.90, -6.40, -8.60]
NACELLE_FRAMES = [2.30, -0.80, -4.20, -7.40]
# Access hatches on the fuselage / nacelle sides: (y centre, z centre, half-y, half-z)
HATCHES = [(5.45, -0.18, 0.40, 0.16), (4.20, 0.28, 0.32, 0.14), (-2.60, 0.30, 0.55, 0.12),
           (-5.30, 0.22, 0.35, 0.10)]
NACELLE_HATCHES = [(0.90, -0.42, 0.55, 0.18), (-2.40, -0.40, 0.45, 0.16), (-5.60, -0.30, 0.40, 0.14)]

SKIN_PARTS = ("fuselage", "spine", "nacelle", "lerx", "wing", "boom", "fin", "stab", "ventral")


def _rect_sdf(a, b, ca, cb, ha, hb):
    return np.maximum(np.abs(a - ca) - ha, np.abs(b - cb) - hb)


def _star(m, img, mask, sdf, lv):
    mix(img, lv["star_red"], m.fill(sdf) * mask)          # thin red outline
    mix(img, lv["star_white"], m.fill(sdf + 0.045) * mask)  # white border
    mix(img, lv["star_red"], m.fill(sdf + 0.12) * mask)    # red star


def paint(m, lv):
    x, y, z, nx, nz = m.x, m.y, m.z, m.nx, m.nz
    skin = m.is_part(*SKIN_PARTS)
    img = camo(m, lv, skin.astype(np.float32))

    fus, spine, nac = m.is_part("fuselage"), m.is_part("spine"), m.is_part("nacelle")
    wing, fin = m.is_part("wing"), m.is_part("fin")

    # Dielectric panels: radome, fin tips.
    mix(img, lv["radome"], m.fill(RADOME_END - y) * fus)
    mix(img, lv["dielectric"], m.fill(FIN_CAP_Z - z) * fin)

    # Engine intake duct: everything facing the nacelle axis near the lip.
    radial_n = nx * (x - NACELLE_X) + nz * (z + 0.46)
    duct = nac & (radial_n < 0) & (y > INTAKE_Y - 1.2)
    mix(img, (22, 22, 24), duct.astype(np.float32))

    # Panel lines.
    lines = np.zeros(m.part.shape, np.float32)

    def add(mask, dist, width=0.9):
        np.maximum(lines, m.line(dist, width) * mask, out=lines)

    for s in FUSELAGE_FRAMES:
        add(fus | spine, y - s, 1.2 if s == RADOME_END else 0.9)
    for s in NACELLE_FRAMES:
        add(nac & ~duct, y - s)
    side = np.abs(nx) > 0.45
    for cy, cz, hy, hz in HATCHES:
        add(fus & side, _rect_sdf(y, z, cy, cz, hy, hz))
    for cy, cz, hy, hz in NACELLE_HATCHES:
        add(nac & side & ~duct, _rect_sdf(y, z, cy, cz, hy, hz))

    s, f, chord = surface_coords(m.pos, WING, "x")
    span = WING[1]["x"] - WING[0]["x"]
    add(wing & (s > 0.06) & (s < 0.97), (f - 0.14) * chord)          # leading-edge slats
    add(wing & (s > 0.10) & (s < 0.90), (f - 0.76) * chord)          # flaperons
    for sb in (0.10, 0.50, 0.90):
        add(wing & (f > 0.76), (s - sb) * span)
    for sb in (0.06, 0.52):
        add(wing & (f < 0.14), (s - sb) * span)

    s, f, chord = surface_coords(m.pos, FIN, "z")
    height = FIN[1]["z"] - FIN[0]["z"]
    add(fin & (s > 0.12) & (s < 0.86), (f - 0.70) * chord)           # rudder
    for sb in (0.12, 0.86):
        add(fin & (f > 0.70), (s - sb) * height)

    s, f, chord = surface_coords(m.pos, STAB, "x")
    add(m.is_part("stab") & (s > 0.04), (f - 0.03) * chord, 0.7)     # pivot fairing seam

    img *= (1.0 - 0.42 * lines)[..., None]

    # National markings: wings (both surfaces) and outer/inner fin faces.
    _star(m, img, wing & (np.abs(nz) > 0.3), star_sdf(x, y, 4.30, -3.20, 0.78), lv)
    _star(m, img, fin & (np.abs(nx) > 0.3), star_sdf(y, z, -8.25, 1.90, 0.62), lv)

    # Light weathering on the skin.
    grime = fbm(x * 1.7, y * 1.7, z * 1.7, 99, octaves=4)
    img *= np.where(skin, 0.93 + 0.09 * grime, 1.0)[..., None]

    # Canopy glass: dark tint, sky reflection towards the top, a highlight streak, frames.
    can = m.is_part("canopy").astype(np.float32)
    mix(img, (30, 40, 54), can)
    mix(img, (120, 146, 172), smoothstep(0.1, 0.95, nz) * 0.7 * can)
    streak = np.exp(-((y - 5.35 + 0.9 * (z - 1.0)) / 0.16) ** 2)
    mix(img, (230, 238, 245), streak * smoothstep(0.3, 0.8, nz) * 0.45 * can)
    frame = np.maximum(m.line(y - 6.66, 3.0), m.fill(0.80 - z))
    mix(img, (54, 58, 62), frame * can)

    # Nozzles: titanium petals, heat tint at the exit, dark duct.
    noz = m.is_part("nozzle").astype(np.float32)
    rx, rz = x - NACELLE_X, z - NOZZLE_Z
    petals = 0.5 + 0.5 * np.cos(np.arctan2(rz, rx) * 16)
    outward = (nx * rx + nz * rz) > 0
    metal = np.where(outward[..., None], lerp_c((70, 66, 62), (96, 90, 82), petals),
                     lerp_c((30, 27, 25), (44, 40, 36), petals))
    mix(img, metal, noz)
    mix(img, (118, 96, 66), smoothstep(-9.15, -9.6, y) * outward * 0.45 * noz)
    mix(img, (16, 13, 12), m.is_part("nozzle_in").astype(np.float32))

    # Small parts.
    mix(img, (8, 8, 10), m.is_part("intake").astype(np.float32))
    mix(img, (126, 130, 134), m.is_part("rail").astype(np.float32))
    mix(img, (150, 152, 154), m.is_part("pitot").astype(np.float32))
    irst = m.is_part("irst").astype(np.float32)
    mix(img, (52, 48, 34), irst)
    mix(img, (200, 196, 160), smoothstep(0.6, 0.95, nz) * 0.5 * irst)
    return img


def lerp_c(c0, c1, t):
    c0, c1 = np.array(c0, np.float32) / 255, np.array(c1, np.float32) / 255
    return c0 + (c1 - c0) * t[..., None]


STARS = dict(star_red=(196, 30, 38), star_white=(238, 238, 234))

LIVERIES = {
    # Serial VKS aircraft: two-tone grey-blue, light belly, pale radome.
    "vks": dict(STARS, base=(168, 182, 194), pat1=(124, 140, 158), belly=(198, 206, 212),
                radome=(206, 206, 194), dielectric=(150, 156, 160),
                camo=[("pat1", 3.2, 0.56, 3)]),
    # Demonstrator "901": digital sand / green / dark grey, camo underneath too.
    "901": dict(STARS, base=(198, 182, 120), pat1=(98, 96, 88), pat2=(78, 122, 84),
                pat3=(150, 132, 90), radome=(84, 88, 92), dielectric=(84, 88, 92),
                camo=[("pat3", 3.0, 0.60, 11), ("pat2", 2.6, 0.60, 23), ("pat1", 2.4, 0.64, 5)],
                block=0.25),
}
