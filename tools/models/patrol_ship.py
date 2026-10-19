"""Enemy patrol ship: a ~56 m missile corvette of the Tarantul / Molniya
class (docs/assets/patrol_ship.md, ADR 0045).

Real dimensions (Project 12411 "Molniya"): length 56.1 m, beam 10.2 m,
draught 2.5 m; a 76 mm AK-176 on the forecastle, launch containers amidships,
the superstructure with the bridge and a lattice mast forward of them, an
AK-630M aft, a short exhaust casing. Modelled at ~1500 triangles: details a
chase camera sees from hundreds of metres - no railings, no lattice.

Blender axes: X starboard, Y bow, Z up, the waterline at Z = 0 (the game draws
the mesh at the ship's position on the water). Only the starboard half is
modelled; the kit mirrors it.
"""

import math

import numpy as np

from fmodel import MeshBuilder, fbm, mix

from mathutils import Vector

TEX = 512
UV_ROTATE = "AXIS_ALIGNED"
AO_DISTANCE = 4.0
LOD_TRIS = 420

L = 56.0
STERN, BOW = -L / 2, L / 2


def deck_at(y):
    """Deck height (sheer rises to the bow), m above the waterline."""
    t = (y - STERN) / L
    return 2.9 + 0.5 * t + 1.6 * max(t - 0.6, 0.0) ** 1.5


def hull_ring(y, hw, deck, keel, flare):
    """Closed hull section at station y: flat deck, flared topsides, a round
    bilge into a shallow V bottom. Starboard points mirrored to port."""
    side = [
        (0.0, deck),
        (hw * 0.55, deck),
        (hw, deck - 0.08),
        (hw * (1.0 - 0.06 * flare), deck * 0.55),
        (hw * (1.0 - 0.10 * flare), 0.0),
        (hw * 0.86, keel * 0.45),
        (hw * 0.55, keel * 0.85),
        (hw * 0.18, keel * 0.98),
        (0.0, keel),
    ]
    pts = side + [(-x, z) for x, z in reversed(side[1:-1])]
    return [Vector((x, y, z)) for x, z in pts]


# Stations: (y, half beam, keel), bow rake handled by the last stations.
HULL = [
    (STERN, 4.4, -1.4),
    (STERN + 4.0, 4.9, -2.1),
    (-14.0, 5.1, -2.5),
    (-2.0, 5.1, -2.6),
    (10.0, 4.7, -2.5),
    (18.0, 3.6, -2.1),
    (23.0, 2.2, -1.5),
    (26.5, 0.9, -0.6),
    (BOW, 0.15, 1.2),
]


def box_ring_xy(z, cx, cy, hx, hy, bevel=0.15):
    """Horizontal rectangle (bevelled corners) at height z, for vertical lofts."""
    b = min(bevel, hx * 0.4, hy * 0.4)
    pts = [(cx + hx, cy - hy + b), (cx + hx, cy + hy - b), (cx + hx - b, cy + hy), (cx - hx + b, cy + hy),
           (cx - hx, cy + hy - b), (cx - hx, cy - hy + b), (cx - hx + b, cy - hy), (cx + hx - b, cy - hy)]
    return [Vector((x, y, z)) for x, y in pts]


def circle_xy(z, cx, cy, r, seg=12):
    return [Vector((cx + r * math.cos(2 * math.pi * i / seg), cy + r * math.sin(2 * math.pi * i / seg), z))
            for i in range(seg)]


def block(mb, part, y0, y1, z0, z1, hx0, hx1=None, cx=0.0, taper_y=0.0):
    """A vertical block (superstructure, funnel): bottom ring at z0, top at z1."""
    hx1 = hx0 if hx1 is None else hx1
    cy, hy = 0.5 * (y0 + y1), 0.5 * (y1 - y0)
    mb.loft([box_ring_xy(z0, cx, cy, hx0, hy), box_ring_xy(z1, cx, cy - taper_y, hx1, max(hy - taper_y, 0.3))],
            part, smooth=0.0)


def build():
    mb = MeshBuilder()
    # Hull, sheer and flare; closed at the transom.
    rings = []
    for y, hw, keel in HULL:
        flare = 1.0 if y > 0 else 0.4
        rings.append(hull_ring(y, hw, deck_at(y), keel, flare))
    mb.loft(rings, "hull", smooth=40.0, cap_start=True, cap_end=False, cap_part="transom")

    d = deck_at
    # Superstructure: deckhouse, the bridge on it, the exhaust casing aft.
    block(mb, "house", -6.0, 12.0, d(3.0) - 0.2, d(3.0) + 2.6, 3.6, 3.4)
    block(mb, "bridge", 5.0, 11.5, d(8.0) + 2.6, d(8.0) + 5.0, 2.8, 2.5, taper_y=0.4)
    block(mb, "house", -12.0, -6.0, d(-9.0) - 0.2, d(-9.0) + 2.0, 3.0, 2.8)
    block(mb, "funnel", -10.5, -7.0, d(-9.0) + 2.0, d(-9.0) + 4.6, 1.5, 1.2, taper_y=0.3)
    # Mast: a tapered post, the radar platform, the search-radar dish (flat).
    zb = d(6.0) + 5.0
    mb.loft([box_ring_xy(zb, 0.0, 7.0, 0.7, 0.7), box_ring_xy(zb + 9.0, 0.0, 6.8, 0.35, 0.35)], "mast", smooth=0.0)
    mb.loft([box_ring_xy(zb + 9.0, 0.0, 6.8, 1.6, 1.2), box_ring_xy(zb + 9.4, 0.0, 6.8, 1.6, 1.2)], "mast", smooth=0.0)
    mb.loft([circle_xy(zb + 9.4, 0.0, 6.8, 0.25, 8), circle_xy(zb + 10.6, 0.0, 6.8, 0.2, 8)], "mast", smooth=60.0)
    mb.plate([Vector((x, 6.6, zb + 10.4 + dz)) for x, dz in ((2.4, 0.0), (2.4, 0.9), (-2.4, 0.9), (-2.4, 0.0))],
             "radar")
    # Fire-control radar dome on the bridge roof.
    zr = d(8.0) + 5.0
    mb.loft([circle_xy(zr, 0.0, 9.0, 0.9), circle_xy(zr + 0.9, 0.0, 9.0, 0.85), circle_xy(zr + 1.4, 0.0, 9.0, 0.45),
             circle_xy(zr + 1.55, 0.0, 9.0, 0.05)], "dome", smooth=60.0, cap_end=False)
    # 76 mm AK-176 on the forecastle: a faceted turret, the barrel forward.
    zf = d(16.0)
    mb.loft([circle_xy(zf, 0.0, 16.0, 1.9, 10), circle_xy(zf + 1.4, 0.0, 15.8, 1.7, 10),
             circle_xy(zf + 2.0, 0.0, 15.6, 1.1, 10)], "turret", smooth=30.0)
    mb.revolve([(17.3, 0.0), (17.4, 0.13), (21.8, 0.11), (22.0, 0.0)], "barrel", seg=8, cz=zf + 1.2)
    # AK-630M aft: a small dome and the six-barrel cluster.
    za = d(-21.0)
    mb.loft([circle_xy(za, 0.0, -21.0, 1.0, 10), circle_xy(za + 1.0, 0.0, -21.0, 0.85, 10),
             circle_xy(za + 1.4, 0.0, -21.0, 0.3, 10)], "turret", smooth=40.0)
    mb.revolve([(-22.0, 0.0), (-22.05, 0.16), (-24.2, 0.14), (-24.3, 0.0)], "barrel", seg=8, cz=za + 0.95)
    # Launch containers, two each side, angled up towards the bow.
    for k, (x, z0) in enumerate(((3.2, 0.9), (3.6, 2.1))):
        zc = d(-8.0) + z0
        y0, y1 = -14.5, -3.5
        prof = [(y0, 0.0), (y0 + 0.05, 0.62), (y1 - 0.05, 0.62), (y1, 0.0)]
        rings = []
        for y, r in prof:
            dz = (y - y0) * 0.14
            if r <= 0.0:
                r = 0.05
            rings.append([Vector((x + r * math.cos(2 * math.pi * i / 10), y, zc + dz + r * math.sin(2 * math.pi * i / 10)))
                          for i in range(10)])
        mb.loft(rings, "container", smooth=50.0)
    # The Igla launcher post abaft the house (port and starboard).
    mb.loft([box_ring_xy(d(-12.5), 1.6, -12.5, 0.25, 0.25), box_ring_xy(d(-12.5) + 1.6, 1.6, -12.5, 0.25, 0.25)],
            "mast", smooth=0.0)
    return mb


# ================================================================ paint

def _digit_segments(ch):
    seg = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
           "7": "abc", "8": "abcdefg", "9": "abcdfg"}
    return seg.get(ch, "")


def number(maps, text, y_right, z0, h, w):
    """Hull number in seven-segment blocks on the starboard bow (mirrored on
    port by the kit - read from the bow on both sides is fine at a distance)."""
    y, z = maps.y, maps.z
    cover = np.zeros(y.shape, np.float32)
    t = 0.14 * h
    for k, ch in enumerate(reversed(text)):
        yr = y_right - k * (w + 0.35 * w)
        x0, x1 = yr - w, yr
        z1 = z0 + h
        zm = z0 + h / 2
        boxes = {"a": (x0, x1, z1 - t, z1), "d": (x0, x1, z0, z0 + t), "g": (x0, x1, zm - t / 2, zm + t / 2),
                 "f": (x0, x0 + t, zm, z1), "e": (x0, x0 + t, z0, zm), "b": (x1 - t, x1, zm, z1),
                 "c": (x1 - t, x1, z0, zm)}
        for sname in _digit_segments(ch):
            a0, a1, b0, b1 = boxes[sname]
            # y runs bow-wards: the digits read from the bow; flip so they read left to right on starboard.
            cover = np.maximum(cover, ((y > a0) & (y < a1) & (z > b0) & (z < b1)).astype(np.float32))
    return cover


def paint(m, lv):
    x, y, z, nz = m.x, m.y, m.z, m.nz
    img = np.zeros(m.part.shape + (3,), np.float32)
    img[:] = np.array(lv["hull"], np.float32) / 255
    hull = m.is_part("hull", "transom")
    up = nz > 0.75
    grain = fbm(x * 1.5, y * 1.5, z * 1.5, 11, 3)
    big = fbm(x * 0.15, y * 0.15, z * 0.15, 12, 3)

    # Superstructure a shade lighter, the funnel top sooty.
    mix(img, lv["house"], m.is_part("house", "bridge", "funnel", "container", "turret", "mast", "dome").astype(np.float32))
    mix(img, (52, 52, 54), (m.is_part("funnel") & (z > deck_at(-9.0) + 4.0)).astype(np.float32))
    # Decks: dark non-skid.
    mix(img, lv["deck"], (up & (hull | m.is_part("house"))).astype(np.float32) * (0.85 + 0.15 * grain))
    # Below the waterline: antifouling red; the black boot topping band.
    mix(img, lv["bottom"], (hull & (z < 0.15)).astype(np.float32))
    mix(img, (38, 38, 40), (hull & (z >= 0.15) & (z < 0.55)).astype(np.float32))
    # Bridge windows: a dark band round the front and sides.
    zb = deck_at(8.0)
    win = m.is_part("bridge") & (z > zb + 3.7) & (z < zb + 4.4) & (np.abs(nz) < 0.3)
    mix(img, (30, 36, 44), win.astype(np.float32))
    # Hull number on the bow.
    num = number(m, lv["number"], 21.0, deck_at(20.0) - 2.2, 1.4, 0.8) * (hull & (x > 0.5) & (np.abs(nz) < 0.5))
    mix(img, (232, 232, 228), num)
    # Rust: streaks below scuppers and the hawse, heavier near the waterline.
    streak = fbm(x * 0.2, y * 3.0, z * 0.08, 13, 3)
    rust = hull & ~up & (z > 0.55) & (streak > 0.62)
    mix(img, (110, 74, 54), rust.astype(np.float32) * np.clip((streak - 0.62) * 4.0, 0, 0.6))
    hawse = hull & (np.abs(y - 24.0) < 0.6) & (z > deck_at(24.0) - 2.6) & (z < deck_at(24.0) - 0.8)
    mix(img, (96, 66, 50), hawse.astype(np.float32) * 0.8)
    # Weathering: blotchy paint, salt-bleached tops.
    img *= (0.9 + 0.12 * big)[..., None]
    mix(img, (196, 198, 196), (up & ~hull).astype(np.float32) * 0.15)
    # Black barrels, the radar dish dark.
    mix(img, (40, 40, 42), m.is_part("barrel").astype(np.float32))
    mix(img, (70, 74, 78), m.is_part("radar").astype(np.float32))
    return img


LIVERIES = {
    # Haze grey of a northern fleet; dark grey decks; red antifouling.
    "std": dict(hull=(140, 146, 150), house=(160, 165, 168), deck=(86, 90, 92), bottom=(112, 50, 42), number="714"),
}
