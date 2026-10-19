"""R-73 (RVV-MD, AA-11 "Archer") short-range IR missile.

Real dimensions: length 2.90 m, body diameter 0.170 m (US drawing: 165 mm),
spans: destabilizers 0.25 m, canards 0.39 m, wings 0.51 m (docs/assets/r73.md).
Origin = mid-length point on the axis (the station attach point), nose +Y.
Fins in the "x" arrangement, as the missile hangs on the P-72-1 / wingtip rail:
the launcher sits between the two upper planes.

Only the right half is modelled (fins at +45 / -45 deg) - the kit mirrors it.
"""

import numpy as np

from fmodel import DIAMOND, MeshBuilder, mix, radial_fin_rings, smoothstep

TEX = 128
UV_ROTATE = "AXIS_ALIGNED"
AO_DISTANCE = 0.5          # occlusion scale of a 0.2 m body, not of an airframe

LENGTH = 2.90
RADIUS = 0.085
NOSE = LENGTH / 2


def y_at(d):
    """Blender y of a point d metres behind the nose tip."""
    return NOSE - d


DOME_END = 0.062          # IR seeker dome (hemisphere r 0.06)
BAND_END = 0.14           # destabilizer ring
CONE_END = 0.26           # flare to the full diameter
TAIL = 2.80               # nozzle section start

# (r, d of the leading edge, chord, thickness): root first.
DESTAB = [(0.058, 0.074, 0.066, 0.008), (0.125, 0.128, 0.012, 0.003)]
CANARD = [(0.078, 0.280, 0.262, 0.012), (0.195, 0.452, 0.088, 0.005)]
WING = [(0.078, 2.200, 0.585, 0.014), (0.255, 2.430, 0.335, 0.006)]
AILERON = 0.30            # roll tabs: aft 30 % of the outer half of each wing


def fin_stations(spec):
    return [dict(r=r, y=y_at(d), chord=c, t=t) for r, d, c, t in spec]


def build():
    mb = MeshBuilder()
    profile = [(0.0, 0.0), (0.013, 0.037), (0.032, 0.052), (DOME_END, 0.060), (BAND_END, 0.063),
               (CONE_END, RADIUS), (NOSE, RADIUS), (TAIL, RADIUS), (LENGTH, 0.071)]
    # Split at mid-length: two 1.45 m UV islands instead of one 2.9 m strip.
    mb.revolve([(y_at(d), r) for d, r in profile], "body", smooth=60, seg=12, cap_part="nozzle",
               breaks=(6,))
    for spec, part in ((DESTAB, "destab"), (CANARD, "canard"), (WING, "wing")):
        for angle in (45, -45):
            # Root ring buried in the body: no cap there.
            mb.loft(radial_fin_rings(fin_stations(spec), angle, DIAMOND), part, smooth=0.0,
                    cap_start=False)
    return mb


# ================================================================ paint

# Section joints (d from the nose): seeker, canard/actuator, fuze, warhead, motor.
JOINTS = [DOME_END, BAND_END, CONE_END, 0.58, 0.83, 1.10, 1.33, 2.12, TAIL]
LUGS = [0.72, 1.55, 2.38]           # launch rail guides on top (3, US drawing)


def paint(m, lv):
    x, y, z, nz = m.x, m.y, m.z, m.nz
    d = NOSE - y
    ang = np.degrees(np.arctan2(z, x))          # 90 = top, 0 = right side
    body = m.is_part("body")
    fins = m.is_part("destab", "canard", "wing")

    img = np.zeros(m.part.shape + (3,), np.float32)
    img[:] = np.array(lv["base"], np.float32) / 255

    # Seeker dome: dark glass with a sky reflection on top.
    dome = (body & (d < DOME_END)).astype(np.float32)
    mix(img, lv["dome"], dome)
    mix(img, (128, 146, 166), smoothstep(0.2, 0.9, nz) * 0.45 * dome)
    mix(img, (232, 238, 244), np.exp(-((nz - 0.75) / 0.12) ** 2) * 0.5 * dome)
    mix(img, lv["ring"], (body & (d >= DOME_END) & (d < BAND_END)).astype(np.float32))

    # Laser proximity fuze windows (1 pair, 180 deg apart) behind the canards.
    win = body & (np.abs(d - 0.66) < 0.016) & (np.abs(ang) < 12)
    mix(img, (52, 58, 66), win.astype(np.float32) * 0.85)

    # Panel lines at section joints; lugs on top.
    lines = np.zeros(m.part.shape, np.float32)
    for j in JOINTS:
        np.maximum(lines, m.line(d - j, 0.8) * body, out=lines)
    img *= (1.0 - 0.38 * lines)[..., None]
    for lug in LUGS:
        mix(img, (96, 100, 104), (body & (np.abs(d - lug) < 0.03) & (np.abs(ang - 90) < 14)).astype(np.float32))

    # Stencils: short dark strips on both sides of the motor section.
    for row, (a0, half) in enumerate(((14, 0.10), (6, 0.07), (-2, 0.085))):
        sten = body & (np.abs(d - 1.70) < half) & (np.abs(ang - a0) < 2.2)
        mix(img, (78, 82, 86), sten.astype(np.float32) * 0.7)

    # Wings: roll-tab hinge lines.
    r = np.hypot(x, z)
    wing = m.is_part("wing")
    w_root, w_tip = WING[0], WING[-1]
    s = np.clip((r - w_root[0]) / (w_tip[0] - w_root[0]), 0, 1)
    te = w_root[1] + w_root[2] + (w_tip[1] + w_tip[2] - w_root[1] - w_root[2]) * s
    chord = w_root[2] + (w_tip[2] - w_root[2]) * s
    hinge = te - AILERON * chord
    tab = np.maximum(m.line(d - hinge, 0.8) * (s > 0.45),
                     m.line((s - 0.45) * (w_tip[0] - w_root[0]), 0.8) * (d > hinge))
    img *= (1.0 - 0.35 * tab * wing)[..., None]

    # Nozzle section: darker metal; exit with the gas vanes in the dark.
    noz = (body & (d > TAIL)).astype(np.float32)
    mix(img, lv["nozzle"], noz)
    exit_ = m.is_part("nozzle").astype(np.float32)
    rr = np.hypot(x, z)
    mix(img, (24, 22, 20), exit_)
    mix(img, (92, 90, 86), smoothstep(0.050, 0.060, rr) * exit_)

    # Slight dirt on the fins' roots and the rear.
    img *= np.where(fins | body, 0.97 + 0.03 * np.cos(d * 9.0), 1.0)[..., None]
    return img


LIVERIES = {
    # Operational VKS rounds: light grey body, dark seeker dome.
    "std": dict(base=(216, 220, 220), dome=(46, 52, 60), ring=(176, 180, 182), nozzle=(118, 116, 112)),
}
