"""Campaign location art: one cinematic aerial picture per location card.

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/textures/locations.py [-- [north_sea ...] [--samples N] [--draft] [--gpu]]

Every picture is a procedural Blender scene - seeded numpy heightfields and
colours, cumulus built from soft volumetric puffs, a physical sky - rendered
by Cycles at 512x256 and reduced 2x to 256x128 in numpy (tone curve,
grade, unsharp mask). The haze is a real medium in the scene (stacked
homogeneous slabs), so distant ridges fade with the sun's in-scattering
instead of a flat overlay (north_sea: horizon haze in grade(), see there).
The terrain mesh is a camera-aligned
wedge grid (rows spaced geometrically with distance), so its vertices are
roughly uniform on screen and carry the albedo as a colour attribute.

Writes assets/ui/loc_<id>.ftx (FTX2 T8 256x128, one level) and previews
build/previews/ui_loc_<id>.png (+ _2x) - the quantised image the PSP shows.
The linear render is kept in build/locations/<id>.exr.
--draft: 256x128, 16 spp, for composition checks. --gpu: render on the GPU
(faster, but not bit-identical between runs; the CPU default is).
Docs: docs/assets/ui-art.md.
"""

import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(HERE, "..", "pipeline"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
import OpenImageIO as oiio  # noqa: E402  (bundled with Blender)
from mathutils import Euler  # noqa: E402

import ftx  # noqa: E402
from png import write_png  # noqa: E402

OUT = os.path.join(ROOT, "assets", "ui")
PREVIEW = os.path.join(ROOT, "build", "previews")
WORK = os.path.join(ROOT, "build", "locations")

RENDER_W, RENDER_H = 512, 256
FINAL_W, FINAL_H = 256, 128
LENS = 35.0                     # mm on a 36 mm sensor: 54.4 x 28.8 deg


# ================================================================ noise (numpy)

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def perlin(x, y, seed):
    """2D gradient noise, about -0.7..0.7, lattice period 256."""
    rng = np.random.default_rng(seed)
    perm = np.tile(rng.permutation(256), 2)
    ang = rng.random(256) * 2 * math.pi
    gx, gy = np.cos(ang), np.sin(ang)
    x0, y0 = np.floor(x), np.floor(y)
    fx, fy = x - x0, y - y0
    xi, yi = x0.astype(np.int64) & 255, y0.astype(np.int64) & 255

    def corner(dx, dy):
        h = perm[perm[xi + dx] + yi + dy]
        return gx[h] * (fx - dx) + gy[h] * (fy - dy)

    u, v = _fade(fx), _fade(fy)
    a = corner(0, 0) + u * (corner(1, 0) - corner(0, 0))
    b = corner(0, 1) + u * (corner(1, 1) - corner(0, 1))
    return a + v * (b - a)


def fbm(x, y, seed, octaves=6, lacunarity=2.0, gain=0.5):
    """Fractal sum of perlin(), normalised to about -0.5..0.5."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        f = lacunarity ** o
        total = total + amp * perlin(x * f + 17.3 * o, y * f - 9.1 * o, seed + 101 * o)
        norm += amp
        amp *= gain
    return total / norm


def ridged(x, y, seed, octaves=7, lacunarity=2.0, gain=0.5, sharp=2.0):
    """Musgrave ridged multifractal, 0..~1: sharp crests, smooth valleys.
    Each octave is weighted by the previous one, so detail sits on ridges."""
    total, amp, weight, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        f = lacunarity ** o
        n = 1.0 - np.abs(perlin(x * f + 31.7 * o, y * f + 5.3 * o, seed + 97 * o) * 1.4)
        n = np.clip(n, 0, 1) ** sharp
        total = total + n * amp * weight
        weight = np.clip(n * 1.6, 0, 1)
        norm += amp
        amp *= gain
    return total / norm


def blur(img, radius):
    """Separable box blur x3 (~Gaussian), edges clamped. img: 2D or HxWxC."""
    out = img.astype(np.float64)
    r = max(1, int(radius))
    for axis in (0, 1):
        for _ in range(3):
            pad = [(0, 0)] * out.ndim
            pad[axis] = (r + 1, r)
            c = np.cumsum(np.pad(out, pad, mode="edge"), axis=axis)
            hi = np.take(c, np.arange(2 * r + 1, c.shape[axis]), axis=axis)
            lo = np.take(c, np.arange(0, c.shape[axis] - 2 * r - 1), axis=axis)
            out = (hi - lo) / (2 * r + 1)
    return out


def ocean_heights(n, size_m, seed, wind_dir, wind_speed, swell_dir, swell_len, min_len=None):
    """Periodic sea-surface height on an n x n grid covering size_m, 0..1:
    Phillips wind-sea spectrum (Tessendorf) plus a narrow long swell."""
    rng = np.random.default_rng(seed)
    k1 = 2 * math.pi * np.fft.fftfreq(n, size_m / n)
    kx, ky = np.meshgrid(k1, k1)
    k = np.sqrt(kx * kx + ky * ky)
    k[0, 0] = 1e-9
    big_l = wind_speed ** 2 / 9.81
    cosw = (kx * math.cos(wind_dir) + ky * math.sin(wind_dir)) / k
    phil = np.exp(-1.0 / (k * big_l) ** 2) / k ** 4 * np.abs(cosw) ** 4
    if min_len:
        phil *= np.exp(-(k * min_len / (2 * math.pi)) ** 2)
    ks = 2 * math.pi / swell_len
    kpar = kx * math.cos(swell_dir) + ky * math.sin(swell_dir)
    kperp = -kx * math.sin(swell_dir) + ky * math.cos(swell_dir)
    swell = np.exp(-((kpar - ks) / (0.18 * ks)) ** 2 - (kperp / (0.10 * ks)) ** 2)
    spec = phil / phil.max() * 0.35 + swell
    spec[0, 0] = 0
    amp = np.sqrt(spec) * (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n)))
    h = np.real(np.fft.ifft2(amp))
    return (h - h.min()) / (h.max() - h.min())


# ================================================================ Blender scene

def reset():
    for coll in (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.images,
                 bpy.data.lights, bpy.data.cameras, bpy.data.worlds, bpy.data.node_groups,
                 bpy.data.collections):
        for item in list(coll):
            coll.remove(item)


def setup_render(samples, draft, use_gpu=False):
    """CPU by default: bit-identical output run to run (checked: two CPU renders
    match exactly, two Metal GPU renders do not). --gpu is ~25% faster on the
    cloud scene but its result varies slightly between runs."""
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    gpu = False
    if use_gpu:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for kind in ("METAL", "OPTIX", "CUDA", "HIP"):
            try:
                prefs.compute_device_type = kind
                prefs.get_devices()
                if any(d.type == kind for d in prefs.devices):
                    for d in prefs.devices:
                        d.use = d.type == kind
                    gpu = True
                    break
            except TypeError:
                continue
    sc.cycles.device = "GPU" if gpu else "CPU"
    sc.cycles.denoising_use_gpu = gpu
    sc.cycles.samples = samples
    sc.cycles.seed = 1
    sc.cycles.use_adaptive_sampling = False
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 24
    sc.cycles.diffuse_bounces = 3
    sc.cycles.glossy_bounces = 2
    sc.cycles.transmission_bounces = 2
    # Thick cumulus need many scattering events to look white; 16 is the
    # speed/brightness compromise (the rest is lifted by the grade).
    sc.cycles.volume_bounces = 16
    # Ray-marched ("biased") volumes: an order of magnitude faster than the
    # default null scattering for these procedural puffs, same look.
    sc.cycles.volume_biased = True
    sc.cycles.volume_step_rate = 0.3
    sc.cycles.volume_max_steps = 512
    sc.cycles.sample_clamp_indirect = 10.0
    scale = 0.5 if draft else 1.0
    sc.render.resolution_x, sc.render.resolution_y = int(RENDER_W * scale), int(RENDER_H * scale)
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    sc.render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    sc.render.image_settings.color_depth = "32"
    sc.render.image_settings.exr_codec = "ZIP"
    return "GPU" if gpu else "CPU"


class Nodes:
    """Tiny helper for building shader node trees in code."""

    def __init__(self, tree):
        self.tree = tree
        self.nodes, self.links = tree.nodes, tree.links

    def new(self, kind, inputs=None, **props):
        n = self.nodes.new(kind)
        for k, v in props.items():
            setattr(n, k, v)
        for k, v in (inputs or {}).items():
            self.set(n.inputs[k], v)
        return n

    def set(self, sock, v):
        if hasattr(v, "is_output") or hasattr(v, "links"):
            self.links.new(v, sock)
        elif isinstance(v, bpy.types.Node):
            self.links.new(v.outputs[0], sock)
        else:
            sock.default_value = v

    def math(self, op, a, b=0.0, c=None, clamp=False):
        n = self.new("ShaderNodeMath", operation=op, use_clamp=clamp)
        self.set(n.inputs[0], a)
        self.set(n.inputs[1], b)
        if c is not None:
            self.set(n.inputs[2], c)
        return n.outputs[0]

    def vmath(self, op, a, b=None, scale=None):
        n = self.new("ShaderNodeVectorMath", operation=op)
        self.set(n.inputs[0], a)
        if b is not None:
            self.set(n.inputs[1], b)
        if scale is not None:
            self.set(n.inputs[3], scale)
        out = "Value" if op in ("DOT_PRODUCT", "LENGTH", "DISTANCE") else "Vector"
        return n.outputs[out]

    def sep(self, v):
        n = self.new("ShaderNodeSeparateXYZ")
        self.set(n.inputs[0], v)
        return n.outputs

    def noise(self, vec, scale, detail=2.0, rough=0.5, dims="3D"):
        n = self.new("ShaderNodeTexNoise", noise_dimensions=dims)
        self.set(n.inputs["Vector"], vec)
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        return n.outputs["Fac"]

    def smooth(self, e0, e1, x):
        n = self.new("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
        self.set(n.inputs["Value"], x)
        n.inputs["From Min"].default_value = e0
        n.inputs["From Max"].default_value = e1
        return n.outputs["Result"]


def material(name):
    mat = bpy.data.materials.new(name)
    try:
        mat.use_nodes = True
    except AttributeError:
        pass
    nt = Nodes(mat.node_tree)
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.new("ShaderNodeOutputMaterial")
    return mat, nt, out


def sky_world(sun_elev_deg, sun_azim_deg, strength=1.0, aerosol=1.0, air=1.0, ozone=1.0):
    """Physical sky (multiple scattering model) whose sun disc is the key
    light. Azimuth 0 = +Y (forward in every scene), positive = to the right."""
    world = bpy.data.worlds.new("sky")
    try:
        world.use_nodes = True
    except AttributeError:
        pass
    nt = Nodes(world.node_tree)
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    sky = nt.new("ShaderNodeTexSky", sky_type="MULTIPLE_SCATTERING")
    sky.sun_disc = True
    sky.sun_size = math.radians(0.545)
    sky.sun_elevation = math.radians(sun_elev_deg)
    sky.sun_rotation = math.radians(sun_azim_deg)   # checked: + turns the sun towards +X
    sky.air_density = air
    sky.aerosol_density = aerosol
    sky.ozone_density = ozone
    bg = nt.new("ShaderNodeBackground", {"Strength": strength})
    nt.set(bg.inputs["Color"], sky.outputs["Color"])
    out = nt.new("ShaderNodeOutputWorld")
    nt.set(out.inputs["Surface"], bg.outputs[0])
    bpy.context.scene.world = world


def camera(pos, yaw_deg, pitch_deg, lens=LENS):
    """yaw 0 = looking along +Y, positive = to the right; pitch < 0 = down."""
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    bpy.context.scene.collection.objects.link(cam)
    cam.location = pos
    cam.rotation_euler = Euler((math.radians(90 + pitch_deg), 0.0, math.radians(-yaw_deg)), "XYZ")
    cam.data.lens = lens
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.sensor_width = 36
    cam.data.clip_start = 5.0
    cam.data.clip_end = 600000.0
    bpy.context.scene.camera = cam
    return cam


def mesh_object(name, verts, faces=None, loops=None):
    me = bpy.data.meshes.new(name)
    if loops is None:
        me.from_pydata(verts, [], faces)
    else:
        me.vertices.add(len(verts))
        me.vertices.foreach_set("co", np.asarray(verts, np.float32).reshape(-1))
        nq = len(loops) // 4
        me.loops.add(len(loops))
        me.loops.foreach_set("vertex_index", np.asarray(loops, np.int32))
        me.polygons.add(nq)
        me.polygons.foreach_set("loop_start", (np.arange(nq) * 4).astype(np.int32))
    me.update()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def box_object(name, x0, x1, y0, y1, z0, z1):
    v = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    f = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    return mesh_object(name, v, f)


def plane_object(name, half, z=0.0):
    return mesh_object(name, [(-half, -half, z), (half, -half, z), (half, half, z), (-half, half, z)],
                       [(0, 1, 2, 3)])


def haze(density, color, anisotropy, scale_height, ground=0.0, half=250000.0, layers=6):
    """Aerial perspective as a real medium: stacked slabs of constant density
    approximating density * exp(-(z - ground) / scale_height). A constant
    density keeps each slab homogeneous, which Cycles samples analytically
    instead of ray marching (the haze costs almost nothing)."""
    z0 = ground - 400.0
    for i in range(layers):
        z1 = ground + scale_height * 0.7 * (i + 1)
        mid = 0.5 * (max(z0, ground) + z1) - ground
        obj = box_object(f"haze{i}", -half, half, -half, half, z0, z1)
        mat, nt, out = material(f"haze{i}")
        vol = nt.new("ShaderNodeVolumePrincipled", {"Color": (*color, 1.0), "Anisotropy": anisotropy,
                                                     "Density": density * math.exp(-mid / scale_height)})
        nt.set(out.inputs["Volume"], vol.outputs[0])
        obj.data.materials.append(mat)
        z0 = z1


def image_from_array(name, arr, colorspace="Non-Color"):
    """arr: HxW or HxWx3/4 float (row 0 = top) -> float Blender image."""
    a = np.asarray(arr, np.float32)
    if a.ndim == 2:
        a = np.repeat(a[..., None], 3, axis=2)
    if a.shape[2] == 3:
        a = np.concatenate([a, np.ones(a.shape[:2] + (1,), np.float32)], axis=2)
    h, w = a.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=True)
    img.colorspace_settings.name = colorspace
    img.pixels.foreach_set(np.ascontiguousarray(a[::-1]).reshape(-1))
    img.pack()
    return img


def wedge(cam_xy, yaw_deg, half_deg, near, far, nx, ny):
    """Ground sample points in the camera's view wedge: ny rows spaced
    geometrically from near to far (constant on-screen spacing), nx columns
    across. Returns world (x, y), each ny x nx."""
    t = np.linspace(0.0, 1.0, ny)
    dist = near * (far / near) ** t
    side = np.linspace(-1.0, 1.0, nx) * math.tan(math.radians(half_deg))
    fwd, lat = np.meshgrid(dist, side, indexing="ij")
    lat = lat * fwd
    yaw = math.radians(yaw_deg)
    x = cam_xy[0] + lat * math.cos(yaw) + fwd * math.sin(yaw)
    y = cam_xy[1] - lat * math.sin(yaw) + fwd * math.cos(yaw)
    return x, y


def grid_object(name, x, y, z, color=None):
    """Smooth-shaded quad grid (ny x nx points) with a point colour
    attribute "albedo" (linear RGB)."""
    ny, nx = x.shape
    idx = np.arange(nx * ny).reshape(ny, nx)
    quads = np.stack([idx[:-1, :-1], idx[:-1, 1:], idx[1:, 1:], idx[1:, :-1]], -1).reshape(-1)
    obj = mesh_object(name, np.stack([x, y, z], -1).reshape(-1, 3), loops=quads)
    me = obj.data
    me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), bool))
    if color is not None:
        attr = me.color_attributes.new("albedo", "FLOAT_COLOR", "POINT")
        rgba = np.concatenate([color.reshape(-1, 3), np.ones((nx * ny, 1))], 1)
        attr.data.foreach_set("color", rgba.astype(np.float32).reshape(-1))
    me.update()
    return obj


def grid_normals(x, y, z):
    """Unit normals (ny x nx x 3) of a grid surface from its neighbours."""
    p = np.stack([x, y, z], -1)
    t1 = np.gradient(p, axis=1)
    t2 = np.gradient(p, axis=0)
    n = np.cross(t1, t2)
    n *= np.sign(n[..., 2:3] + 1e-12)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def lerp_c(c0, c1, t):
    """Blend two RGB colours by t (any shape) -> t.shape + (3,)."""
    t = np.asarray(t)[..., None]
    return np.asarray(c0) * (1 - t) + np.asarray(c1) * t


def terrain_material(name, rough=0.85, bump_m=60.0, bump=0.35, color=None):
    """Albedo from the "albedo" point colour (or a constant `color`); a fine
    procedural bump adds relief below the grid spacing."""
    mat, nt, out = material(name)
    pos = nt.new("ShaderNodeNewGeometry").outputs["Position"]
    n = nt.noise(nt.vmath("SCALE", pos, scale=1.0 / bump_m), 1.0, 6.0, 0.6)
    bmp = nt.new("ShaderNodeBump", {"Strength": bump, "Distance": bump_m * 0.4})
    nt.set(bmp.inputs["Height"], n)
    bsdf = nt.new("ShaderNodeBsdfPrincipled", {"Roughness": rough})
    if color is None:
        attr = nt.new("ShaderNodeAttribute", attribute_name="albedo")
        nt.set(bsdf.inputs["Base Color"], attr.outputs["Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    nt.set(bsdf.inputs["Normal"], bmp.outputs["Normal"])
    nt.set(out.inputs["Surface"], bsdf.outputs[0])
    return mat


def sea_material(name, swell_img, tile_m, deep, rough, bump, body_attr=False):
    """Sea surface: Fresnel reflection of the sky over a body colour (constant
    deep colour, or the "albedo" attribute - turquoise shallows); the normal
    comes from a periodic Tessendorf heightfield sampled at two scales and
    rotations so the tile never repeats visibly."""
    mat, nt, out = material(name)
    pos = nt.new("ShaderNodeNewGeometry").outputs["Position"]
    hsum = None
    for scale, ang, w in ((1.0, 0.0, 1.0), (0.37, 0.9, 0.45)):
        rot = nt.new("ShaderNodeVectorRotate", rotation_type="Z_AXIS")
        rot.inputs["Angle"].default_value = ang
        nt.set(rot.inputs["Vector"], pos)
        uv = nt.vmath("SCALE", rot.outputs[0], scale=1.0 / (tile_m * scale))
        tex = nt.new("ShaderNodeTexImage", image=swell_img, interpolation="Cubic", extension="REPEAT")
        nt.set(tex.inputs["Vector"], uv)
        val = nt.math("MULTIPLY", tex.outputs["Color"], w * scale)
        hsum = val if hsum is None else nt.math("ADD", hsum, val)
    bsdf = nt.new("ShaderNodeBsdfPrincipled", {"Base Color": (*deep, 1.0), "Roughness": rough, "IOR": 1.333})
    if body_attr:
        attr = nt.new("ShaderNodeAttribute", attribute_name="albedo")
        nt.set(bsdf.inputs["Base Color"], attr.outputs["Color"])
    bmp = nt.new("ShaderNodeBump", {"Strength": 1.0, "Distance": bump})
    nt.set(bmp.inputs["Height"], hsum)
    nt.set(bsdf.inputs["Normal"], bmp.outputs["Normal"])
    nt.set(out.inputs["Surface"], bsdf.outputs[0])
    return mat


# ---------------------------------------------------------------- clouds

def puff_material(name, base, density, noise_m=160.0, color=(0.995, 0.995, 0.995), amp=0.55, glow=0.0,
                  soft=0.08, aniso=None):
    """Soft cloud puff (unit icosphere scaled; one material for all puffs).
    Density follows a "pyroclastic" sphere: the radius is pushed out by a
    3-octave billow noise sum |2n - 1| in world space, so overlapping puffs
    share one cauliflower surface. Everything below the condensation level
    (`base`, world z, slightly ragged) is cut: the flat cumulus base.
    glow: a little emission per unit density standing in for the light the
    capped number of scattering bounces loses inside thick cloud."""
    mat, nt, out = material(name)
    tc = nt.new("ShaderNodeTexCoord")
    r = nt.vmath("LENGTH", tc.outputs["Object"])
    wpos = nt.new("ShaderNodeNewGeometry").outputs["Position"]
    bill = None
    for k, w in enumerate((1.0, 0.55, 0.3)):
        n = nt.noise(nt.vmath("SCALE", wpos, scale=(2.6 ** k) / noise_m), 1.0, 1.0, 0.5)
        b = nt.math("MULTIPLY", nt.math("ABSOLUTE", nt.math("MULTIPLY_ADD", n, 2.0, -1.0)), w)
        bill = b if bill is None else nt.math("ADD", bill, b)
    body = nt.math("ADD", nt.math("SUBTRACT", 1.0, r), nt.math("MULTIPLY", nt.math("SUBTRACT", bill, 0.45), amp))
    dens = nt.smooth(0.0, soft, body)
    z = nt.sep(wpos)[2]
    n0 = nt.noise(nt.vmath("SCALE", wpos, scale=1.0 / 300.0), 1.0, 2.0, 0.5)
    flat = nt.smooth(base - 10.0, base + 20.0, nt.math("MULTIPLY_ADD", nt.math("SUBTRACT", n0, 0.5), 120.0, z))
    dens = nt.math("MULTIPLY", nt.math("MULTIPLY", dens, flat), density)
    # Mie phase function of ~20 um droplets: the strong forward lobe gives
    # backlit cumulus their silver rims. aniso: a milder Henyey-Greenstein
    # lobe instead, which with capped bounces keeps lit faces brighter.
    if aniso is None:
        vol = nt.new("ShaderNodeVolumeScatter", {"Color": (*color, 1.0), "Diameter": 20.0}, phase="MIE")
    else:
        vol = nt.new("ShaderNodeVolumeScatter", {"Color": (*color, 1.0), "Anisotropy": aniso},
                     phase="HENYEY_GREENSTEIN")
    nt.set(vol.inputs["Density"], dens)
    shader = vol.outputs[0]
    if glow:
        em = nt.new("ShaderNodeEmission", {"Color": (0.80, 0.86, 1.0, 1.0)})
        nt.set(em.inputs["Strength"], nt.math("MULTIPLY", dens, glow))
        add = nt.new("ShaderNodeAddShader")
        nt.set(add.inputs[0], shader)
        nt.set(add.inputs[1], em.outputs[0])
        shader = add.outputs[0]
    nt.set(out.inputs["Volume"], shader)
    return mat


def ico_mesh(name, subdiv=2, radius=1.45):
    """The billows push the surface out past r = 1: the hull is larger."""
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=radius)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return me


def add_puffs(name, mat, puffs):
    """puffs: [((x, y, z), (sx, sy, sz))] - linked duplicates of one hull."""
    me = ico_mesh(name + "_puff")
    me.materials.append(mat)
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    for loc, scale in puffs:
        obj = bpy.data.objects.new("puff", me)
        obj.location = loc
        obj.scale = scale
        coll.objects.link(obj)
    return len(puffs)


def cumulus(rng, centres, base, cam=(0.0, 0.0), detail_dist=16000.0, tall=(0.75, 1.35)):
    """Cumulus clouds as puffs, built like a cauliflower: a flattened row of
    large body puffs, medium turrets heaped on their upper sides, then small
    billows on the turrets. centres: (x, y, R), R = half-width (m). Clouds
    beyond detail_dist skip the smallest level."""
    out = []
    for cx, cy, big_r in centres:
        ang = rng.uniform(0, math.pi)
        ax, ay = math.cos(ang), math.sin(ang)
        elong = rng.uniform(1.0, 1.7)
        tall_k = rng.uniform(*tall)
        level = []
        n_body = int(rng.integers(4, 8))
        for i in range(n_body):
            t = (i / (n_body - 1) - 0.5) * 2
            r = big_r * rng.uniform(0.42, 0.58) * (1 - 0.3 * abs(t))
            p = (cx + ax * t * big_r * 0.6 * elong + rng.normal(0, 0.1) * big_r,
                 cy + ay * t * big_r * 0.6 * elong + rng.normal(0, 0.1) * big_r,
                 base + r * 0.25)
            level.append((p, r))
        allp = list(level)
        far = math.hypot(cx - cam[0], cy - cam[1]) > detail_dist
        for k, (lo, hi, per) in enumerate([(0.40, 0.60, 2.5), (0.36, 0.55, 2.5)]):
            if k == 1 and far:
                break
            nxt = []
            for (px, py, pz), pr in level:
                for _ in range(int(rng.poisson(per)) + (1 if k == 0 else 0)):
                    th = rng.uniform(0, 2 * math.pi)
                    up = rng.uniform(0.25, 1.0) ** (0.6 if k == 0 else 1.0)
                    s = math.sqrt(1 - up * up)
                    r = pr * rng.uniform(lo, hi)
                    q = (px + math.cos(th) * s * pr * 0.72, py + math.sin(th) * s * pr * 0.72,
                         pz + up * pr * 0.72 * tall_k)
                    if q[2] - r < base + 0.15 * big_r:
                        continue
                    nxt.append((q, r))
            allp += nxt
            level = nxt
        out += [(p, (r, r, r * tall_k)) for p, r in allp]
    return out


def scatter(rng, n, x_range, y_range, accept, size, pts=None):
    """Rejection-sampled cloud centres that do not overlap: accept(x, y) ->
    probability 0..1, size = (typical, max) half-width. y is drawn uniformly
    in log distance (as many clouds per screen row near as far).
    pts: centres placed by hand first. Returns [(x, y, R)]."""
    pts = list(pts or [])
    tries = 0
    ly0, ly1 = math.log(y_range[0]), math.log(y_range[1])
    while len(pts) < n and tries < n * 400:
        tries += 1
        y = math.exp(rng.uniform(ly0, ly1))
        x = rng.uniform(-1.0, 1.0) * (0.62 * y + 4000.0)
        if rng.random() > accept(x, y):
            continue
        r = float(np.clip(rng.lognormal(math.log(size[0]), 0.45), size[0] * 0.5, size[1]))
        if all((x - px) ** 2 + (y - py) ** 2 > (1.1 * (r + pr)) ** 2 for px, py, pr in pts):
            pts.append((x, y, r))
    return pts


# ================================================================ output

def render(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    t0 = time.time()
    bpy.ops.render.render(write_still=True)
    print(f"[loc]   render {time.time() - t0:.0f}s")


def read_exr(path):
    """Linear RGB (HxWx3) of the Combined pass of Blender's multilayer EXR."""
    inp = oiio.ImageInput.open(path)
    spec = inp.spec()
    data = inp.read_image(0, 0, 0, spec.nchannels, "float")
    inp.close()
    names = list(spec.channelnames)
    idx = [next(i for i, n in enumerate(names) if n.endswith(f"Combined.{c}")) for c in "RGB"]
    return data[..., idx].astype(np.float64)


def aces(x):
    """Narkowicz's fit of the ACES filmic curve (scene-linear -> 0..1)."""
    return np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)


def srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def luma(img):
    return img[..., 0] * 0.2126 + img[..., 1] * 0.7152 + img[..., 2] * 0.0722


def grade(lin, g):
    """Scene-linear render -> display image at 256x128.
    g: key (target of the key_pct luminance percentile), white_balance,
    sat, lift / gain / gamma (RGB), contrast, vignette, sharpen."""
    f = lin.shape[1] // FINAL_W
    h, w = lin.shape[:2]
    small = lin.reshape(h // f, f, w // f, f, 3).mean(axis=(1, 3)) if f > 1 else lin
    hz = g.get("horizon")
    if hz:
        # Aerial perspective for the flat-sea scene, in post: on an open sea
        # the distance is a function of the angle to the horizon, so the haze
        # is a band around the horizon row, blended to the sky colour there.
        hh = small.shape[0]
        vfov = 2 * math.degrees(math.atan(9.0 / LENS))
        row = (0.5 + math.tan(math.radians(hz["pitch"])) / (2 * math.tan(math.radians(vfov / 2)))) * hh
        ang = (np.arange(hh) + 0.5 - row) / hh * vfov                 # deg, + = below the horizon
        f = np.where(ang > 0, np.exp(-ang / hz["below"]), np.exp(ang / hz["above"])) * hz["amount"]
        sky_row = max(int(row - hz["above"] * hh / vfov), 0)
        col = np.median(small[sky_row], axis=0)
        small = small * (1 - f[:, None, None]) + col * f[:, None, None]
    ref = np.percentile(luma(small), g.get("key_pct", 50))
    x = small * (g.get("key", 0.18) / max(ref, 1e-6)) * np.array(g.get("white_balance", (1, 1, 1)))
    d = srgb(aces(x))
    y = luma(d)[..., None]
    d = y + (d - y) * g.get("sat", 1.0)
    lift = np.array(g.get("lift", (0, 0, 0)))
    gain = np.array(g.get("gain", (1, 1, 1)))
    d = np.clip(d * (gain - lift) + lift, 0, 1) ** (1.0 / np.array(g.get("gamma", (1, 1, 1))))
    c = g.get("contrast", 0.0)
    if c:
        d = np.clip(d + c * (d - 0.5) * (1 - np.abs(2 * d - 1)) * 2, 0, 1)
    hh, ww = d.shape[:2]
    yy, xx = np.mgrid[0:hh, 0:ww]
    r = np.hypot((xx + 0.5) / ww * 2 - 1, ((yy + 0.5) / hh * 2 - 1) * 0.6)
    d = d * (1 - g.get("vignette", 0.18) * smoothstep(0.55, 1.25, r))[..., None]
    # Unsharp mask: keeps edges after the reduction, on a small LCD.
    s = g.get("sharpen", 0.35)
    if s:
        d = d + s * (d - blur(d, 1))
    return np.clip(d, 0, 1)


def finish(loc_id, lin, g):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    # Snap to 8 bits first: the display is 8-bit anyway, and it absorbs the
    # last-bit render differences of the volume scenes between runs before
    # k-means (which would otherwise pick a slightly different palette).
    img = np.round(grade(lin, g) * 255) / 255
    path = os.path.join(OUT, f"loc_{loc_id}.ftx")
    shown = ftx.export_quantized(path, img, max_levels=1)
    write_png(os.path.join(PREVIEW, f"ui_loc_{loc_id}.png"), shown)
    write_png(os.path.join(PREVIEW, f"ui_loc_{loc_id}_2x.png"), np.repeat(np.repeat(shown, 2, 0), 2, 1))
    print(f"[loc]   assets/ui/loc_{loc_id}.ftx {os.path.getsize(path)} bytes")


# ================================================================ scenes

HALF_FOV = math.degrees(math.atan(18.0 / LENS))       # 27.2 deg


def north_sea():
    """Cold open sea from 650 m, just under a broken cumulus deck: flat grey
    cloud bases recede to the horizon, the low sun (19 deg) above the frame
    lays a silver glitter path across the long swell, cloud shadows on it."""
    sky_world(19.0, -14.0, aerosol=0.8)
    cam_xy, yaw, base = (0.0, 0.0), 0.0, 1000.0
    camera((*cam_xy, 650.0), yaw, 2.5)
    swell = ocean_heights(512, 1600.0, seed=11, wind_dir=math.radians(70), wind_speed=14.0,
                          swell_dir=math.radians(100), swell_len=180.0, min_len=6.0)
    sea = plane_object("sea", 300000.0)
    sea.data.materials.append(sea_material("sea", image_from_array("swell", swell), 1600.0,
                                           (0.008, 0.018, 0.026), 0.12, 14.0))
    rng = np.random.default_rng(5)

    def accept(x, y):
        return float(smoothstep(-0.35, 0.15, perlin(np.array(x / 7000.0), np.array(y / 7000.0), 3)))

    # Hand-placed: a big cloud over the left edge, one right of centre.
    hero = [(-3200.0, 5200.0, 1500.0), (2600.0, 8500.0, 1100.0), (-600.0, 3300.0, 900.0),
            (3900.0, 3900.0, 1000.0)]
    cents = scatter(rng, 700, None, (2500, 75000), accept, (600.0, 1500.0), hero)
    n = add_puffs("cumulus", puff_material("puff", base, 0.03, soft=0.18, glow=0.8), cumulus(rng, cents, base))
    print(f"[loc]   {len(cents)} clouds, {n} puffs")
    # No haze medium here: under the broken deck it renders the thin cloud
    # shadows as streaky shafts; the horizon haze is added in grade().
    return dict(key=0.20, sat=0.85, white_balance=(0.94, 0.98, 1.06), gain=(0.96, 1.0, 1.04),
                lift=(0.015, 0.02, 0.03), contrast=0.12, vignette=0.20, sharpen=0.35,
                horizon=dict(pitch=2.5, below=1.6, above=2.2, amount=0.75))


def volcanic_isles():
    """A chain of steep green volcanic islands curving away to the horizon,
    a smoking stratovolcano in the middle distance, turquoise shallows,
    warm afternoon haze. Camera 1.4 km up, sun high on the right."""
    sky_world(24.0, 68.0, aerosol=1.6)
    cam_xy, yaw = (0.0, 0.0), 0.0
    camera((*cam_xy, 1150.0), yaw, -5.5)
    # (x, y, radius, height, kind); the cone is the focal point.
    isles = [(-4800, 7400, 2900, 1250, "ridge"), (-900, 10800, 1300, 650, "ridge"),
             (3300, 15500, 3600, 2100, "cone"), (-6400, 19500, 2400, 1250, "ridge"),
             (8800, 23500, 2300, 1300, "ridge"), (-2500, 29000, 2600, 1450, "ridge"),
             (6000, 36000, 3200, 1500, "ridge"), (-9000, 42000, 3000, 1150, "ridge"),
             (14000, 47000, 4000, 1700, "cone"), (2000, 52000, 2400, 1000, "ridge"),
             (-1900, 6200, 500, 160, "ridge"), (1300, 12600, 420, 110, "ridge")]
    cone = isles[2]

    def height(x, y):
        wx = fbm(x / 2600.0, y / 2600.0, 21, 4)
        wy = fbm(x / 2600.0 + 9.3, y / 2600.0 - 3.1, 22, 4)
        rid = ridged(x / 2300.0, y / 2300.0, 23, 6)
        h = np.full(x.shape, -400.0)
        for cx, cy, rad, top, kind in isles:
            dx = x - cx + wx * rad * 0.55
            dy = y - cy + wy * rad * 0.55
            u = np.sqrt(dx * dx + dy * dy) / rad
            inside = np.clip(1.0 - u, 0.0, 1.0)
            if kind == "cone":
                ang = np.arctan2(dy, dx)
                gully = perlin(ang * 9.0 + 50.0, u * 6.0, 31)
                g = top * inside ** 1.9 * (1.0 - 0.10 * np.abs(gully))
                g -= top * 0.10 * smoothstep(0.07, 0.0, u)          # summit crater
            else:
                # High island: steep coast (dome 1 - u^2.4), fluted by radial
                # amphitheatre valleys, jagged crests on top.
                ang = np.arctan2(dy, dx)
                flute = 1.0 - np.abs(perlin(ang * rad / 650.0 + cx * 0.01, u * 2.5, 32) * 1.6)
                flute = np.clip(flute, 0, 1) ** 2
                dome = np.clip(1.0 - u ** 2.4, 0.0, 1.0)
                g = top * dome * (0.42 + 0.38 * flute + 0.45 * rid * dome)
            # Outside the coast: a shallow shelf (lagoons, reef flats), then the drop-off.
            g = np.where(u < 1.0, g, -(u - 1.0) * 40.0 - smoothstep(1.3, 1.75, u) * 300.0)
            h = np.maximum(h, g - 28.0)
        land = smoothstep(0.0, 60.0, h)
        return h + 22.0 * fbm(x / 420.0, y / 420.0, 24, 4) * land

    x, y = wedge(cam_xy, yaw, HALF_FOV + 4, 400.0, 70000.0, 1000, 800)
    z = height(x, y)
    n = grid_normals(x, y, z)
    slope = 1.0 - n[..., 2]
    # Land colours (linear albedo): rainforest greens, bare scoria up the
    # cone, rock on cliffs, a thin black-sand beach.
    tone = fbm(x / 900.0, y / 900.0, 41, 4)
    green = lerp_c((0.022, 0.065, 0.018), (0.060, 0.125, 0.030), smoothstep(-0.25, 0.25, tone))
    rock = lerp_c((0.075, 0.068, 0.060), (0.12, 0.10, 0.085), smoothstep(-0.3, 0.3, tone))
    col = lerp_c(green, rock, smoothstep(0.62, 0.85, slope))
    dcone = np.hypot(x - cone[0], y - cone[1]) / cone[2]
    ash = lerp_c((0.055, 0.048, 0.045), (0.13, 0.085, 0.06), smoothstep(-0.2, 0.3, fbm(x / 300.0, y / 300.0, 42, 3)))
    col = lerp_c(col, ash, smoothstep(0.45, 0.30, dcone) * smoothstep(500.0, 900.0, z))
    col = lerp_c(col, (0.09, 0.085, 0.08), smoothstep(14.0, 4.0, z))
    terrain = grid_object("terrain", x, y, z, col)
    terrain.data.materials.append(terrain_material("land", bump_m=80.0, bump=0.3))

    # Water: body colour from depth (turquoise over sand shelves, deep blue
    # off the slopes), a foam line at the shore.
    wx_, wy_ = wedge(cam_xy, yaw, HALF_FOV + 4, 400.0, 70000.0, 700, 560)
    depth = -height(wx_, wy_)
    body = lerp_c((0.07, 0.48, 0.40), (0.012, 0.13, 0.16), smoothstep(2.0, 26.0, depth))
    body = lerp_c(body, (0.004, 0.022, 0.045), smoothstep(25.0, 140.0, depth))
    body = lerp_c(body, (0.55, 0.60, 0.60), smoothstep(1.5, 0.0, depth) * 0.6)
    swell = ocean_heights(512, 900.0, seed=12, wind_dir=math.radians(40), wind_speed=9.0,
                          swell_dir=math.radians(60), swell_len=110.0, min_len=4.0)
    sea_mat = sea_material("sea", image_from_array("swell", swell), 900.0, (0.004, 0.022, 0.045), 0.10, 4.0,
                           body_attr=True)
    water = grid_object("water", wx_, wy_, np.zeros_like(wx_), body)
    water.data.materials.append(sea_mat)
    far = plane_object("far_sea", 300000.0, -2.0)
    far.data.materials.append(sea_material("far_sea", image_from_array("swell2", swell), 900.0,
                                           (0.004, 0.022, 0.045), 0.10, 4.0))

    # Steam plume: puffs rising from the crater, swelling and bending downwind.
    rng = np.random.default_rng(9)
    puffs = []
    for i in range(56):
        t = i / 55.0
        r = 150.0 + 650.0 * t ** 1.2
        px = cone[0] - 3200.0 * t ** 1.6 + rng.normal(0, 0.25) * r
        py = cone[1] + 1200.0 * t + rng.normal(0, 0.25) * r
        pz = cone[3] - 180.0 + 2100.0 * t ** 0.8 + rng.normal(0, 0.2) * r
        puffs.append(((px, py, pz), (r, r, r * 0.8)))
    add_puffs("plume", puff_material("steam", -1e5, 0.014, noise_m=120.0, color=(0.93, 0.93, 0.94),
                                     soft=0.45, amp=0.8, glow=0.3), puffs)
    haze(1.0 / 55000.0, (1.0, 0.94, 0.87), 0.55, 1100.0)
    return dict(key=0.17, sat=1.05, white_balance=(1.04, 1.0, 0.94), lift=(0.02, 0.018, 0.012),
                contrast=0.12, vignette=0.22, sharpen=0.35)


def terrace(h, step, sharp=0.8):
    """Horizontal strata: flat benches joined by steep risers (h in m)."""
    t = h / step
    k = np.floor(t)
    return (k + smoothstep(sharp, 1.0, t - k)) * step


def red_canyons():
    """Desert tableland cut by a deep winding canyon with stepped red walls
    and side gullies, mesas and buttes on the right, a thread of river on
    the floor; high sun from the left, hard shadows, dust haze. Camera
    800 m above the rim."""
    sky_world(46.0, -72.0, aerosol=2.6)
    cam_xy, yaw = (0.0, 0.0), 0.0
    camera((*cam_xy, 2150.0), yaw, -10.0)
    rim, deep = 1350.0, 700.0

    def path(y):
        return 1700.0 * np.sin(y / 6200.0 + 0.9) - 1500.0 + 700.0 * perlin(y / 3800.0, 0.37, 51)

    def width(y):
        return 700.0 + 250.0 * perlin(y / 5000.0, 2.7, 53) + 0.010 * y

    def height(x, y):
        plateau = rim + 45.0 * fbm(x / 5000.0, y / 5000.0, 52, 4)
        # Irregular walls: the distance to the canyon axis is warped at two scales.
        xw = x + 520.0 * fbm(x / 1800.0, y / 1800.0, 64, 3) + 160.0 * fbm(x / 450.0, y / 450.0, 65, 3)
        d = np.abs(xw - path(y)) / width(y)
        main = smoothstep(1.0, 0.35, d)
        # Side gullies eating into the rim.
        gv = 1.0 - np.abs(perlin(xw / 480.0, y / 480.0, 66) * 1.5)
        gully = smoothstep(0.82, 0.97, gv) * smoothstep(1.9, 1.0, d) * 0.6
        # Badlands on the right: the tableland broken into mesas and buttes.
        zone = smoothstep(-0.05, 0.25, fbm(x / 14000.0, y / 14000.0, 56, 3) + (x - 2000.0) / 16000.0)
        mesa = smoothstep(0.04, 0.09, fbm(xw / 1500.0, y / 1500.0, 57, 4))
        cut = np.maximum.reduce([main, gully, zone * (1.0 - mesa) * 0.8])
        h = plateau - deep * cut
        h = 0.15 * h + 0.85 * terrace(h + 22.0 * fbm(x / 600.0, y / 600.0, 58, 3), 70.0, 0.72)
        river = smoothstep(0.06, 0.0, d)
        return h - 8.0 * river + 7.0 * fbm(x / 220.0, y / 220.0, 59, 3), d

    x, y = wedge(cam_xy, yaw, HALF_FOV + 4, 300.0, 80000.0, 1100, 900)
    z, d = height(x, y)
    n = grid_normals(x, y, z)
    slope = 1.0 - n[..., 2]
    # Strata colours by absolute height (sedimentary beds are horizontal).
    strata = [(0.24, 0.070, 0.035), (0.40, 0.15, 0.060), (0.50, 0.30, 0.16), (0.30, 0.095, 0.045),
              (0.44, 0.20, 0.085), (0.56, 0.40, 0.25), (0.34, 0.12, 0.05)]
    band = (z + 20.0 * fbm(x / 1500.0, y / 1500.0, 61, 3)) / 62.0
    k = np.floor(band).astype(np.int64) % len(strata)
    pal = np.asarray(strata)
    col = lerp_c(pal[k], pal[(k + 1) % len(strata)], smoothstep(0.75, 1.0, band - np.floor(band)))
    # Flat ground (bench tops, plateau, floor): red sand with desert varnish
    # patches and sparse scrub.
    flat = smoothstep(0.12, 0.04, slope)
    vary = fbm(x / 900.0, y / 900.0, 62, 5) + 0.6 * fbm(x / 4000.0, y / 4000.0, 67, 3)
    sand = lerp_c((0.22, 0.085, 0.04), (0.46, 0.25, 0.12), smoothstep(-0.4, 0.4, vary))
    col = lerp_c(col, sand, flat * 0.8)
    scrub = smoothstep(0.15, 0.32, fbm(x / 140.0, y / 140.0, 63, 2)) * flat
    col = lerp_c(col, (0.16, 0.12, 0.07), scrub * 0.45)
    col = lerp_c(col, (0.035, 0.065, 0.055), smoothstep(0.05, 0.02, d))         # the river
    terrain = grid_object("terrain", x, y, z, col)
    terrain.data.materials.append(terrain_material("rock", rough=0.9, bump_m=45.0, bump=0.45))
    # Beyond the 80 km wedge: flat desert, below everything in view.
    far = plane_object("far_ground", 400000.0, rim - deep - 60.0)
    far.data.materials.append(terrain_material("far", rough=0.9, color=(0.40, 0.20, 0.10)))
    haze(1.0 / 32000.0, (1.0, 0.78, 0.56), 0.5, 1500.0, ground=rim - deep)
    return dict(key=0.17, sat=1.05, white_balance=(1.02, 1.0, 0.96), lift=(0.02, 0.012, 0.008),
                contrast=0.16, vignette=0.22, sharpen=0.35)


def glacier_range():
    """Snow-capped range at dawn: a glacial U-valley with a frozen lake and
    braided ice runs to a pyramidal peak; sunrise from the right lights the
    summits pink-orange while the valley lies in blue shadow."""
    sky_world(3.2, 72.0, aerosol=0.6, air=1.0)
    cam_xy, yaw = (0.0, 0.0), 0.0
    camera((*cam_xy, 2700.0), yaw, -5.0)
    floor = 950.0
    peak = (1200.0, 24000.0, 7000.0, 5000.0)

    def valley_x(y):
        return 1200.0 * np.sin(y / 8500.0 + 0.4) + 500.0 * perlin(y / 5000.0, 0.21, 71)

    def parts(x, y):
        wx = fbm(x / 7000.0, y / 7000.0, 72, 4) * 2500.0
        wy = fbm(x / 7000.0 + 4.1, y / 7000.0 - 2.2, 73, 4) * 2500.0
        rid = ridged(x / 9000.0 + wx / 9000.0, y / 9000.0 + wy / 9000.0, 74, 8, sharp=2.2)
        mount = floor + 3100.0 * rid ** 1.25
        r = np.hypot(x - peak[0], (y - peak[1]) * 0.85) / peak[2]
        pyr = peak[3] * np.clip(1.0 - r, 0, 1) ** 1.35 * (0.82 + 0.3 * rid)
        mount = np.maximum(mount, pyr)
        w = 1900.0 + 0.02 * y
        v = np.abs(x - valley_x(y)) / w
        u = smoothstep(0.0, 1.0, np.clip(v, 0, 1.6) / 1.6) ** 1.3          # U-shaped profile
        u = np.maximum(u, smoothstep(peak[1] - 9000.0, peak[1] - 2000.0, y))  # valley head
        return mount, u, rid

    def height(x, y):
        mount, u, _ = parts(x, y)
        return floor + (mount - floor) * u + 8.0 * fbm(x / 300.0, y / 300.0, 75, 3)

    x, y = wedge(cam_xy, yaw, HALF_FOV + 4, 400.0, 90000.0, 1100, 900)
    mount, u, rid = parts(x, y)
    z = floor + (mount - floor) * u + 8.0 * fbm(x / 300.0, y / 300.0, 75, 3)
    n = grid_normals(x, y, z)
    slope = 1.0 - n[..., 2]
    tone = fbm(x / 800.0, y / 800.0, 76, 4)
    rock = lerp_c((0.045, 0.043, 0.045), (0.09, 0.085, 0.08), smoothstep(-0.3, 0.3, tone))
    snow = lerp_c((0.70, 0.74, 0.80), (0.86, 0.88, 0.90), smoothstep(-0.3, 0.3, tone))
    # Winter: snow lies on everything below ~40 deg, valley floor included.
    snowy = smoothstep(0.34, 0.20, slope + 0.08 * tone)
    col = lerp_c(rock, snow, snowy)
    # Glaciers: the smooth troughs between ridges above ~1.6 km.
    trough = smoothstep(0.42, 0.25, rid) * smoothstep(1500.0, 2100.0, z) * smoothstep(0.6, 0.3, slope)
    col = lerp_c(col, (0.62, 0.72, 0.82), trough)
    # Frozen valley floor: lake ice, snow drifts, darker braided channels.
    low = smoothstep(floor + 60.0, floor + 5.0, z)
    ice = lerp_c((0.30, 0.42, 0.52), (0.75, 0.80, 0.86),
                 smoothstep(-0.1, 0.25, fbm(x / 500.0, y / 900.0, 78, 4)))
    col = lerp_c(col, ice, low)
    terrain = grid_object("terrain", x, y, z, col)
    terrain.data.materials.append(terrain_material("mountain", rough=0.8, bump_m=70.0, bump=0.3))
    far = plane_object("far_ground", 400000.0, floor - 40.0)
    far.data.materials.append(terrain_material("far", rough=0.9, color=(0.5, 0.55, 0.6)))
    haze(1.0 / 60000.0, (0.82, 0.88, 1.0), 0.5, 2000.0, ground=floor)
    return dict(key=0.16, key_pct=55, sat=1.0, white_balance=(0.97, 0.98, 1.05), lift=(0.012, 0.016, 0.03),
                contrast=0.12, vignette=0.24, sharpen=0.35)


SCENES = {
    "north_sea": north_sea,
    "volcanic_isles": volcanic_isles,
    "red_canyons": red_canyons,
    "glacier_range": glacier_range,
}


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ids = [a for a in args if not a.startswith("--") and not a.isdigit()]
    draft = "--draft" in args
    samples = int(args[args.index("--samples") + 1]) if "--samples" in args else (16 if draft else 48)
    os.makedirs(WORK, exist_ok=True)
    t_all = time.time()
    for loc_id in ids or list(SCENES):
        t0 = time.time()
        reset()
        device = setup_render(samples, draft, "--gpu" in args)
        print(f"[loc] {loc_id}: {device}, {samples} spp{' (draft)' if draft else ''}")
        g = SCENES[loc_id]()
        print(f"[loc]   scene built {time.time() - t0:.0f}s")
        path = os.path.join(WORK, f"{loc_id}.exr")
        render(path)
        finish(loc_id, read_exr(path), g)
    print(f"[loc] done in {time.time() - t_all:.0f}s")


main()
