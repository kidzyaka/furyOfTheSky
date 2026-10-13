"""Aircraft modelling + texture baking kit for Fury of the Sky.

Runs inside Blender (bpy / bmesh / mathutils / numpy). Pipeline:

  1. Geometry: parts are lofted through spline-interpolated cross-sections.
     Paired parts (wings, fins...) are modelled on the right side only.
  2. The mesh is cut at x = 0, the right half is UV-unwrapped, then mirrored,
     so both halves share texels (doubles texture resolution).
  3. Every texel is rasterised back to its 3D position / normal / part, which
     lets the skin be *painted in 3D*: camo noise, panel lines, decals, and
     ray-traced ambient occlusion - no seams in the pattern, no hand painting.
  4. The result is quantised to a 256-colour CLUT, mip-mapped and swizzled
     for the PSP GE, and the mesh exported as an indexed, smooth-shaded list.

Blender space while modelling: X = right wing, Y = nose, Z = up, metres.
Game space on export:          X = right,      Y = up,  -Z = nose.
"""

import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pipeline"))

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

import ftx  # noqa: E402  (tools/pipeline)

TEX = 512
MIP_LEVELS = 4          # 512, 256, 128, 64


def lerp(a, b, t):
    return a + (b - a) * t


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# ================================================================ sections

def _catmull_rom(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                  + (3 * p1 - p0 - 3 * p2 + p3) * t3)


def spline(stations, subdiv):
    """Densify a list of numeric tuples with a Catmull-Rom spline through them."""
    pts = [np.array(s, float) for s in stations]
    n = len(pts)
    out = []
    for i in range(n - 1):
        p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, n - 1)]
        for k in range(subdiv):
            out.append(tuple(_catmull_rom(p0, p1, p2, p3, k / subdiv)))
    out.append(tuple(pts[-1]))
    return out


def _se(c, s, n):
    return math.copysign(abs(c) ** (2.0 / n), c), math.copysign(abs(s) ** (2.0 / n), s)


def se_ring(y, cx, cz, hw, ht, hb=None, n=2.0, seg=24):
    """Closed superellipse cross-section in the XZ plane at station y.
    hw - half width, ht/hb - extent above/below centre, n - squareness."""
    hb = ht if hb is None else hb
    hw, ht, hb = max(hw, 0.004), max(ht, 0.004), max(hb, 0.004)
    pts = []
    for i in range(seg):
        t = 2.0 * math.pi * i / seg
        ex, ez = _se(math.cos(t), math.sin(t), n)
        pts.append(Vector((cx + hw * ex, y, cz + (ht if ez >= 0 else hb) * ez)))
    return pts


def arc_ring(y, cx, cz, hw, h, n=2.0, seg=12, a0=-20.0, a1=200.0):
    """Upper arc only (canopies, fairings); the closing edge runs underneath,
    buried in whatever the part sits on."""
    pts = []
    for i in range(seg + 1):
        t = math.radians(a0 + (a1 - a0) * i / seg)
        ex, ez = _se(math.cos(t), math.sin(t), n)
        pts.append(Vector((cx + max(hw, 0.004) * ex, y, cz + max(h, 0.004) * ez)))
    return pts


def _naca(f):
    """NACA 00xx half-thickness, normalised to 1 at maximum thickness."""
    return (0.2969 * math.sqrt(f) - 0.1260 * f - 0.3516 * f * f
            + 0.2843 * f ** 3 - 0.1036 * f ** 4) / 0.1002


AIRFOIL_STATIONS = [0.006, 0.025, 0.07, 0.15, 0.28, 0.45, 0.63, 0.82]


def airfoil(stations=AIRFOIL_STATIONS, lower=0.85):
    """(chord fraction, thickness fraction) ring: round LE, sharp TE."""
    top = [(f, _naca(f)) for f in stations]
    bottom = [(f, -lower * _naca(f)) for f in reversed(stations)]
    return [(0.0, 0.0)] + top + [(1.0, 0.0)] + bottom


def surface_stations(stations, steps):
    out = [stations[0]]
    for a, b in zip(stations, stations[1:]):
        out += [{k: lerp(a[k], b[k], i / steps) for k in a} for i in range(1, steps + 1)]
    return out


def wing_rings(stations, steps=1, foil=None):
    """Horizontal lifting surface lofted along span.
    stations: dicts(x, y, z, chord, t), root first; y = leading edge."""
    foil = foil or airfoil()
    return [[Vector((p["x"], p["y"] - f * p["chord"], p["z"] + k * p["t"] * 0.5)) for f, k in foil]
            for p in surface_stations(stations, steps)]


def fin_rings(stations, steps=1, foil=None):
    """Vertical surface lofted along height (x may vary for cant)."""
    foil = foil or airfoil(lower=1.0)
    return [[Vector((p["x"] + k * p["t"] * 0.5, p["y"] - f * p["chord"], p["z"])) for f, k in foil]
            for p in surface_stations(stations, steps)]


def surface_coords(pos, stations, span_axis):
    """For texels on a lifting surface: (span fraction s, chord fraction f,
    chord length) relative to its root/tip stations - drives panel lines."""
    root, tip = stations[0], stations[-1]
    axis = {"x": 0, "z": 2}[span_axis]
    s = np.clip((pos[..., axis] - root[span_axis]) / (tip[span_axis] - root[span_axis]), 0, 1)
    le = lerp(root["y"], tip["y"], s)
    chord = lerp(root["chord"], tip["chord"], s)
    return s, (le - pos[..., 1]) / chord, chord


# ================================================================ mesh

class Part:
    def __init__(self, name, smooth):
        self.name, self.smooth = name, smooth


class MeshBuilder:
    """Collects closed lofted shells. Each loft side and each cap is its own
    smoothing group; inside a group, corners are averaged across edges sharper
    than the part's crease angle only."""

    def __init__(self):
        self.bm = bmesh.new()
        self.l_part = self.bm.faces.layers.int.new("part")
        self.l_group = self.bm.faces.layers.int.new("sgroup")
        self.parts = []
        self.group_smooth = []

    def part_id(self, name, smooth):
        for i, p in enumerate(self.parts):
            if p.name == name:
                return i
        self.parts.append(Part(name, smooth))
        return len(self.parts) - 1

    def _group(self, smooth):
        self.group_smooth.append(smooth)
        return len(self.group_smooth) - 1

    def loft(self, rings, part, smooth=60.0, cap_start=True, cap_end=True, cap_part=None):
        pid = self.part_id(part, smooth)
        cap_pid = self.part_id(cap_part, smooth) if cap_part else pid
        verts = [[self.bm.verts.new(v) for v in ring] for ring in rings]
        n = len(rings[0])
        side = self._group(smooth)
        faces = []
        for a, b in zip(verts, verts[1:]):
            for i in range(n):
                j = (i + 1) % n
                f = self.bm.faces.new((a[i], a[j], b[j], b[i]))
                f[self.l_part], f[self.l_group] = pid, side
                faces.append(f)
        for flag, ring, rev in ((cap_start, verts[0], True), (cap_end, verts[-1], False)):
            if flag:
                f = self.bm.faces.new(list(reversed(ring)) if rev else ring)
                f[self.l_part], f[self.l_group] = cap_pid, self._group(0.0)
                faces.append(f)
        bmesh.ops.recalc_face_normals(self.bm, faces=faces)

    # ------------------------------------------------------------ pipeline

    def finalize(self, name):
        """Cut, unwrap, mirror. Leaves self.half (unwrapped right half) and
        self.full (mirrored, welded, triangulated)."""
        bm = self.bm
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-6,
                               plane_co=(0, 0, 0), plane_no=(-1, 0, 0), clear_outer=True)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.normal_update()

        # Blender's UV operators need a real object in edit mode.
        me = bpy.data.meshes.new(name + "_half")
        bm.to_mesh(me)
        obj = bpy.data.objects.new(name + "_half", me)
        bpy.context.scene.collection.objects.link(obj)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(52), island_margin=0.0,
                                 area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
        bpy.ops.uv.pack_islands(rotate=True, margin=0.003, shape_method="CONCAVE")
        bpy.ops.object.mode_set(mode="OBJECT")

        half = bmesh.new()
        half.from_mesh(me)
        bpy.data.objects.remove(obj)
        bpy.data.meshes.remove(me)
        self.half = half

        full = half.copy()
        geom = full.verts[:] + full.edges[:] + full.faces[:]
        dup = bmesh.ops.duplicate(full, geom=geom)["geom"]
        for v in (e for e in dup if isinstance(e, bmesh.types.BMVert)):
            v.co.x = -v.co.x
        bmesh.ops.reverse_faces(full, faces=[e for e in dup if isinstance(e, bmesh.types.BMFace)])
        bmesh.ops.remove_doubles(full, verts=[v for v in full.verts if abs(v.co.x) < 1e-5], dist=1e-5)
        full.normal_update()
        self.full = full

    def corner_normals(self, bm):
        """{face index: [normal per corner]} - angle-weighted, crease-aware."""
        grp = bm.faces.layers.int["sgroup"]
        bm.faces.index_update()
        out = {}
        for f in bm.faces:
            g = f[grp]
            cos_lim = math.cos(math.radians(max(self.group_smooth[g], 0.5)))
            ns = []
            for l in f.loops:
                acc = Vector()
                for l2 in l.vert.link_loops:
                    f2 = l2.face
                    if f2[grp] == g and f2.normal.dot(f.normal) >= cos_lim:
                        acc += f2.normal * l2.calc_angle()
                ns.append(acc.normalized() if acc.length > 1e-9 else f.normal.copy())
            out[f.index] = ns
        return out

    # ------------------------------------------------------------ export

    def export_fsm(self, path):
        """Indexed triangle list, vertex = GU_TEXTURE_32BITF | GU_NORMAL_32BITF |
        GU_VERTEX_32BITF (32 bytes), 16-bit indices."""
        bm = self.full
        uv = bm.loops.layers.uv.active
        normals = self.corner_normals(bm)
        verts, index, lookup = [], [], {}
        radius = 0.0
        for f in bm.faces:
            for l, n in zip(f.loops, normals[f.index]):
                c, t = l.vert.co, l[uv].uv
                key = (round(c.x, 4), round(c.y, 4), round(c.z, 4),
                       round(n.x, 3), round(n.y, 3), round(n.z, 3), round(t.x, 5), round(t.y, 5))
                if key not in lookup:
                    lookup[key] = len(verts)
                    verts.append((t.x, 1.0 - t.y, n.x, n.z, -n.y, c.x, c.z, -c.y))
                    radius = max(radius, c.length)
                index.append(lookup[key])
        assert len(verts) < 65536
        with open(path, "wb") as fh:
            fh.write(struct.pack("<4sIIf", b"FSM3", len(verts), len(index), radius))
            for v in verts:
                fh.write(struct.pack("<8f", *v))
            fh.write(struct.pack(f"<{len(index)}H", *index))
            fh.write(b"\0" * ((-fh.tell()) % 16))
        return len(index) // 3, len(verts), radius

    def to_object(self, name):
        bm = self.full
        normals = self.corner_normals(bm)
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        loop_normals = [None] * len(me.loops)
        for poly in me.polygons:
            for li, n in zip(poly.loop_indices, normals[poly.index]):
                loop_normals[li] = n
        me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        me.normals_split_custom_set(loop_normals)
        obj = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(obj)
        return obj


# ================================================================ texel maps

class TexelMaps:
    """What lives under every texel of the atlas (right half of the model)."""

    def __init__(self, mb):
        self.parts = [p.name for p in mb.parts]
        bm = mb.half
        uv = bm.loops.layers.uv.active
        lpart = bm.faces.layers.int["part"]
        normals = mb.corner_normals(bm)
        self.pos = np.zeros((TEX, TEX, 3), np.float32)
        self.nrm = np.zeros((TEX, TEX, 3), np.float32)
        self.part = np.full((TEX, TEX), -1, np.int16)
        area3d = area_uv = 0.0
        for f in bm.faces:
            uvs = [(l[uv].uv.x * TEX, (1.0 - l[uv].uv.y) * TEX) for l in f.loops]
            ps = [np.array(l.vert.co) for l in f.loops]
            ns = [np.array(n) for n in normals[f.index]]
            area3d += f.calc_area()
            area_uv += abs((uvs[1][0] - uvs[0][0]) * (uvs[2][1] - uvs[0][1])
                           - (uvs[2][0] - uvs[0][0]) * (uvs[1][1] - uvs[0][1])) * 0.5
            self._raster(uvs, ps, ns, f[lpart])
        self.mask = self.part >= 0
        self.texel = math.sqrt(area3d / max(area_uv, 1e-9))     # metres per texel
        n = np.linalg.norm(self.nrm, axis=-1, keepdims=True)
        self.nrm /= np.maximum(n, 1e-6)
        self.x, self.y, self.z = self.pos[..., 0], self.pos[..., 1], self.pos[..., 2]
        self.nx, self.ny, self.nz = self.nrm[..., 0], self.nrm[..., 1], self.nrm[..., 2]

    def _raster(self, uvs, ps, ns, pid):
        (ax, ay), (bx, by), (cx, cy) = uvs
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-10:
            return
        x0, x1 = max(int(min(ax, bx, cx)), 0), min(int(max(ax, bx, cx)) + 1, TEX - 1)
        y0, y1 = max(int(min(ay, by, cy)), 0), min(int(max(ay, by, cy)) + 1, TEX - 1)
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
        self.nrm[rows, cols] = w @ np.array(ns)
        self.part[rows, cols] = pid

    def is_part(self, *names):
        ids = [self.parts.index(n) for n in names if n in self.parts]
        return np.isin(self.part, ids)

    def line(self, dist_m, width=0.9):
        """Anti-aliased line coverage from a signed distance in metres."""
        return np.clip(1.0 - np.abs(dist_m) / (width * self.texel), 0.0, 1.0)

    def fill(self, sdf_m):
        """Anti-aliased coverage of a region given its signed distance (<0 inside)."""
        return np.clip(0.5 - sdf_m / self.texel, 0.0, 1.0)


def ambient_occlusion(mb, maps, rays=20, distance=2.2, step=2):
    """Ray-traced AO against the full (mirrored) model, at 1/step resolution."""
    tree = BVHTree.FromBMesh(mb.full)
    golden = math.pi * (3 - math.sqrt(5))
    dirs = []
    for i in range(rays):                        # cosine-weighted Fibonacci hemisphere
        r = math.sqrt((i + 0.5) / rays)
        a = i * golden
        dirs.append((r * math.cos(a), r * math.sin(a), math.sqrt(1 - r * r)))
    size = TEX // step
    ao = np.ones((size, size), np.float32)
    for r in range(size):
        for c in range(size):
            rr, cc = r * step + step // 2, c * step + step // 2
            if maps.part[rr, cc] < 0:
                continue
            p = Vector(maps.pos[rr, cc])
            n = Vector(maps.nrm[rr, cc])
            t = n.orthogonal().normalized()
            b = n.cross(t)
            origin = p + n * 0.02
            occl = 0.0
            for dx, dy, dz in dirs:
                hit, _, _, dist = tree.ray_cast(origin, t * dx + b * dy + n * dz, distance)
                if hit is not None:
                    occl += 1.0 - (dist / distance) ** 0.5 * 0.6
            ao[r, c] = 1.0 - occl / rays
    big = np.repeat(np.repeat(ao, step, 0), step, 1)
    return _blur(big, 1)


def _blur(img, radius):
    out = img.copy()
    for _ in range(radius):
        p = np.pad(out, 1, mode="edge")
        out = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] + 4 * p[1:-1, 1:-1]) / 8
    return out


# ================================================================ procedural paint helpers

def _hash(ix, iy, iz, seed):
    h = np.sin(ix * 127.1 + iy * 311.7 + iz * 74.7 + seed * 53.3) * 43758.5453
    return h - np.floor(h)


def _value_noise(x, y, z, seed):
    ix, iy, iz = np.floor(x), np.floor(y), np.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    ux, uy, uz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)

    def c(dx, dy, dz):
        return _hash(ix + dx, iy + dy, iz + dz, seed)

    x00 = lerp(c(0, 0, 0), c(1, 0, 0), ux)
    x10 = lerp(c(0, 1, 0), c(1, 1, 0), ux)
    x01 = lerp(c(0, 0, 1), c(1, 0, 1), ux)
    x11 = lerp(c(0, 1, 1), c(1, 1, 1), ux)
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz)


def fbm(x, y, z, seed, octaves=3):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        f = 2.0 ** o
        total = total + amp * _value_noise(x * f, y * f, z * f, seed + o * 17)
        norm += amp
        amp *= 0.5
    return total / norm


def star_sdf(a, b, ca, cb, r, rot=math.pi / 2):
    """Signed distance (metres, <0 inside) to a five-pointed star."""
    pts = []
    for i in range(10):
        ang = rot + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.382
        pts.append((ca + rr * math.cos(ang), cb + rr * math.sin(ang)))
    dist = np.full(a.shape, np.inf, np.float32)
    inside = np.zeros(a.shape, bool)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        ex, ey = x2 - x1, y2 - y1
        t = np.clip(((a - x1) * ex + (b - y1) * ey) / (ex * ex + ey * ey), 0, 1)
        dist = np.minimum(dist, np.hypot(a - x1 - t * ex, b - y1 - t * ey))
        cond = (y1 > b) != (y2 > b)
        xint = ex * (b - y1) / (ey if ey != 0 else 1e-9) + x1
        inside ^= cond & (a < xint)
    return np.where(inside, -dist, dist)


def rgb(c):
    return np.array(c, np.float32) / 255.0


def mix(img, color, amount):
    """In-place blend of a colour (rgb tuple 0..255 or HxWx3 array) by a mask 0..1."""
    col = rgb(color) if isinstance(color, tuple) else color
    a = amount[..., None]
    img *= 1.0 - a
    img += col * a


def camo(maps, livery, where):
    """Base colour + camouflage layers painted in 3D (seamless across UV islands)."""
    img = np.zeros((TEX, TEX, 3), np.float32)
    img[:] = rgb(livery["base"])
    x, y, z = maps.x, maps.y, maps.z
    block = livery.get("block", 0.0)
    if block:                                  # "digital" camo: blocky in 3D
        x, y, z = (np.floor(c / block) * block for c in (x, y, z))
    for role, scale, threshold, seed in livery["camo"]:
        n = fbm(x / scale, y / scale, z / scale, seed)
        cover = (n > threshold).astype(np.float32) if block else \
            smoothstep(threshold - 0.012, threshold + 0.012, n)
        mix(img, livery[role], cover * where)
    if livery.get("belly"):
        mix(img, livery["belly"], smoothstep(-0.30, -0.55, maps.nz) * where)
    return img


# ================================================================ texture export

def dilate(img, mask, iters=12):
    img, m = img.copy(), mask.copy()
    for _ in range(iters):
        acc = np.zeros_like(img)
        cnt = np.zeros(m.shape, np.float32)
        p_img = np.pad(img * m[..., None], ((1, 1), (1, 1), (0, 0)))
        p_m = np.pad(m.astype(np.float32), 1)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    acc += p_img[1 + dy:TEX + 1 + dy, 1 + dx:TEX + 1 + dx]
                    cnt += p_m[1 + dy:TEX + 1 + dy, 1 + dx:TEX + 1 + dx]
        grow = (~m) & (cnt > 0)
        img[grow] = acc[grow] / cnt[grow][:, None]
        m |= grow
    return img


def export_ftx(path, img):
    """T8 atlas with mips (docs/formats/ftx2.md). Returns the quantised
    level-0 RGB - exactly what the PSP will show."""
    return ftx.export_quantized(path, img, max_levels=MIP_LEVELS)


# ================================================================ preview

def atlas_image(name, rgb_img):
    px = np.ones((TEX, TEX, 4), np.float32)
    px[..., :3] = rgb_img
    img = bpy.data.images.new(name, TEX, TEX)
    img.colorspace_settings.name = "sRGB"
    img.pixels.foreach_set(px[::-1].reshape(-1))     # Blender rows go bottom-up
    return img


def apply_texture(obj, img):
    mat = obj.data.materials[0] if obj.data.materials else None
    if mat is None:
        mat = bpy.data.materials.new("skin")
        try:
            mat.use_nodes = True
        except AttributeError:
            pass
        obj.data.materials.append(mat)
    nodes = mat.node_tree.nodes
    tex = next((n for n in nodes if n.type == "TEX_IMAGE"), None)
    if tex is None:
        tex = nodes.new("ShaderNodeTexImage")
        bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    tex.image = img
    tex.interpolation = "Linear"
    nodes.active = tex


def render_previews(obj, out_prefix, radius, views=None):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "TEXTURE"
    sh.show_cavity = False
    sh.show_specular_highlight = True
    sh.show_object_outline = False
    scene.render.resolution_x, scene.render.resolution_y = 960, 600
    scene.render.film_transparent = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.color = (0.32, 0.42, 0.55)

    all_views = {
        "three_quarter": ((radius * 1.7, radius * 1.9, radius * 1.0), False),
        "rear_low":      ((-radius * 1.5, -radius * 2.0, radius * 0.3), False),
        "below":         ((radius * 0.4, radius * 0.6, -radius * 2.6), False),
        "close":         ((radius * 0.55, radius * 1.05, radius * 0.35), False),
        "top":           ((0, 0, radius * 3), True),
        "side":          ((radius * 3, 0, 0), True),
    }
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        scene.collection.objects.link(cam)
        scene.camera = cam
    for name in views or all_views:
        pos, ortho = all_views[name]
        cam.location = pos
        if name == "top":                      # default camera: looks down, nose up
            cam.rotation_euler = (0.0, 0.0, 0.0)
        else:                                  # camera local Y is screen-up
            direction = Vector((0, 0, 0)) - Vector(pos)
            cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        cam.data.type = "ORTHO" if ortho else "PERSP"
        aspect = scene.render.resolution_x / scene.render.resolution_y
        cam.data.ortho_scale = radius * 2.1 * (aspect if name == "top" else 1.0)
        cam.data.lens = 50
        cam.data.clip_start = 0.1
        cam.data.clip_end = radius * 10
        scene.render.filepath = f"{out_prefix}_{name}.png"
        bpy.ops.render.render(write_still=True)
