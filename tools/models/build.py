"""Build an aircraft model: indexed .fsm mesh + one baked .ftx texture per
livery, plus preview renders of exactly what the PSP will receive.

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/build.py -- su35s [--no-preview | --loadout]

--loadout: also builds the missiles of model.STATIONS and renders the aircraft
with all of them hung (build/previews/<name>_loadout_*.png).

A model with HOST (e.g. su35s_gear: HOST = "su35s", GROUND_Y) is also rendered
mounted on that aircraft, standing on a ground plane: build/previews/<name>_*.png.
"""

import importlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import math  # noqa: E402

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import fmodel  # noqa: E402

AO_STRENGTH = 0.85

# Preview pose for the articulated parts: pull + right roll + airbrake + TVC up.
POSE = {fmodel.CH_STABILATOR: (-14, -14), fmodel.CH_FLAPERON: (-18, 18), fmodel.CH_SLAT: (22, 22),
        fmodel.CH_RUDDER: (25, 25), fmodel.CH_NOZZLE: (-12, -12)}


def pose_angle(channel, side):
    return math.radians(POSE.get(channel, (0, 0))[side - 1])


def build_model(name, preview, write=True, out_dir=None):
    """Build one model and write its assets: mesh, texel maps, AO, liveries.
    preview: make a textured Blender object (first livery); write: also render
    the standard preview images; out_dir: where the assets go (default
    assets/models). Returns (model module, MeshBuilder, object or None,
    bounding radius)."""
    t0 = time.time()
    model = importlib.import_module(name)
    # Per-model atlas size: TexelMaps, AO, dilate, camo and atlas_image all
    # read fmodel.TEX at call time.
    fmodel.TEX = getattr(model, "TEX", 512)
    mb = model.build()
    mb.finalize(name, getattr(model, "UV_ROTATE", None))

    out_dir = out_dir or os.path.join(ROOT, "assets", "models")
    prev_dir = os.path.join(ROOT, "build", "previews")
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(prev_dir, exist_ok=True)

    tris, verts, radius = mb.export_fsm(os.path.join(out_dir, f"{name}.fsm"))
    print(f"[model] {name}: {tris} triangles, {verts} vertices, radius {radius:.2f} m, "
          f"{len(mb.part_table)} parts")
    for first, count, channel, side, pivot, axis in mb.part_table:
        print(f"[model]   part ch{channel} side{side}: {count // 3} tris, pivot "
              f"({pivot[0]:.2f}, {pivot[1]:.2f}, {pivot[2]:.2f}) axis ({axis[0]:.2f}, {axis[1]:.2f}, {axis[2]:.2f})")

    lod_tris = getattr(model, "LOD_TRIS", None)
    lod_obj = None
    if lod_tris:
        lod_n, lod_verts, lod_obj = mb.export_lod(name + "_lod", os.path.join(out_dir, f"{name}_lod.fsm"),
                                                     lod_tris)
        print(f"[model] {name}_lod: {lod_n} triangles, {lod_verts} vertices (target {lod_tris})")
        lod_obj.hide_render = True

    maps = fmodel.TexelMaps(mb)
    print(f"[model] texture {fmodel.TEX}x{fmodel.TEX}, texel density {1.0 / maps.texel:.1f} texels/m, "
          f"atlas coverage {maps.mask.mean() * 100:.0f}%")
    ao = fmodel.ambient_occlusion(mb, maps, distance=getattr(model, "AO_DISTANCE", 2.2))
    print(f"[model] ambient occlusion baked ({time.time() - t0:.0f}s)")
    shade = 1.0 - AO_STRENGTH * (1.0 - ao)

    obj = mb.to_object(name) if preview else None
    posed = mb.to_object(name + "_posed", mb.posed(pose_angle)) if preview and len(mb.joints) > 1 else None
    if posed:
        posed.hide_render = True
    first_atlas = None
    for livery, spec in model.LIVERIES.items():
        img = model.paint(maps, spec) * shade[..., None]
        img = fmodel.dilate(np.clip(img, 0, 1), maps.mask)
        shown = fmodel.export_ftx(os.path.join(out_dir, f"{name}_{livery}.ftx"), img)
        print(f"[model] livery {livery} -> {name}_{livery}.ftx ({time.time() - t0:.0f}s)")
        if preview:
            atlas = fmodel.atlas_image(f"{name}_{livery}", shown)
            atlas.filepath_raw = os.path.join(prev_dir, f"{name}_{livery}_atlas.png")
            atlas.file_format = "PNG"
            atlas.save()
            fmodel.apply_texture(obj, atlas)
            if write:
                fmodel.render_previews(obj, os.path.join(prev_dir, f"{name}_{livery}"), radius)
            if livery == next(iter(model.LIVERIES)):
                first_atlas = atlas
                if posed and write:
                    # Same atlas on the deflected copy: checks hinge lines and axes.
                    fmodel.apply_texture(posed, atlas)
                    obj.hide_render, posed.hide_render = True, False
                    fmodel.render_previews(posed, os.path.join(prev_dir, f"{name}_posed"), radius,
                                           ["rear_low", "three_quarter", "close"])
                    obj.hide_render, posed.hide_render = False, True
    if lod_obj and first_atlas and write:
        # The distant copy with the same atlas: checks the decimation kept the shape and UVs.
        fmodel.apply_texture(lod_obj, first_atlas)
        obj.hide_render, lod_obj.hide_render = True, False
        fmodel.render_previews(lod_obj, os.path.join(prev_dir, f"{name}_lod"), radius,
                               ["three_quarter", "below", "rear_low"])
        obj.hide_render, lod_obj.hide_render = False, True
    if obj and first_atlas:
        fmodel.apply_texture(obj, first_atlas)
    return model, mb, obj, radius


def loadout(name):
    """Aircraft with every missile hung at its station (model.STATIONS, game
    axes: x right, y up, z aft): build/previews/<name>_loadout_*.png."""
    model, mb, plane, radius = build_model(name, True, write=False)
    plane.hide_render = False
    meshes = {}
    for station in model.STATIONS:
        weapon = station["missile"]
        if weapon not in meshes:
            _, _, wobj, _ = build_model(weapon, True, write=False)
            wobj.hide_render = True                    # the template; copies are shown
            meshes[weapon] = wobj
        gx, gy, gz = station["pos"]
        for sx in (1, -1):
            copy = meshes[weapon].copy()
            copy.hide_render = False
            copy.location = (sx * gx, -gz, gy)         # game (x, y, z) -> Blender (x, -z, y)
            bpy.context.scene.collection.objects.link(copy)
    prev_dir = os.path.join(ROOT, "build", "previews")
    prefix = os.path.join(prev_dir, f"{name}_loadout")
    fmodel.render_previews(plane, prefix, radius, ["three_quarter", "below", "rear_low", "close", "side"])
    # Close-ups of the right wing stations (Blender space): contact and clearance.
    for view, pos, target, lens in (
            ("stations_front", (7.5, 7.5, -2.2), (4.2, -2.5, -0.5), 50),
            ("stations_rear", (6.0, -9.5, -1.6), (4.0, -2.5, -0.5), 50),
            ("stations_head_on", (4.0, 14.0, -0.55), (4.0, 0.0, -0.55), 90),
            ("wingtip", (9.6, -1.2, -0.9), (7.1, -4.2, -0.25), 60),
            ("intake", (3.2, 3.5, -2.4), (1.15, 0.6, -1.15), 45)):
        render_view(pos, target, lens, f"{prefix}_{view}.png")


def render_view(pos, target, lens, path, ortho_scale=None):
    """One perspective render from pos towards target (after render_previews
    has set up the scene and camera); orthographic if ortho_scale is given."""
    from mathutils import Vector
    scene = bpy.context.scene
    cam = scene.camera
    cam.location = pos
    cam.rotation_euler = (Vector(target) - Vector(pos)).to_track_quat("-Z", "Y").to_euler()
    cam.data.type = "ORTHO" if ortho_scale else "PERSP"
    if ortho_scale:
        cam.data.ortho_scale = ortho_scale
    cam.data.lens = lens
    cam.data.clip_start = 0.05
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def mounted(model, part):
    """A part drawn with its host aircraft's model transform (model.HOST),
    on a ground plane at model.GROUND_Y: build/previews/<name>_*.png. The host
    is rebuilt into build/mounted - its files in assets/models are not touched."""
    _, _, host, radius = build_model(model.HOST, True, write=False,
                                     out_dir=os.path.join(ROOT, "build", "mounted"))
    host.hide_render = part.hide_render = False
    gz, s = model.GROUND_Y, radius * 4
    me = bpy.data.meshes.new("ground")
    me.from_pydata([(-s, -s, gz), (s, -s, gz), (s, s, gz), (-s, s, gz)], [], [(0, 1, 2, 3)])
    ground = bpy.data.objects.new("ground", me)
    mat = bpy.data.materials.new("ground")
    mat.diffuse_color = (0.30, 0.30, 0.29, 1.0)
    me.materials.append(mat)
    bpy.context.scene.collection.objects.link(ground)
    prefix = os.path.join(ROOT, "build", "previews", model.__name__)
    fmodel.render_previews(host, prefix, radius, ["side"])
    sh = bpy.context.scene.display.shading
    sh.show_shadows, sh.shadow_intensity = True, 0.35
    for view, pos, target, lens, ortho in (
            ("front", (0.0, 40.0, gz + 2.95), (0.0, 0.0, gz + 2.95), 50, 17.0),
            ("three_quarter", (-13.0, 16.0, gz + 2.6), (0.0, 0.5, -0.9), 40, None),
            ("rear_three_quarter", (17.0, -21.0, gz + 3.4), (0.0, -0.5, -0.9), 35, None),
            ("nose", (1.9, 7.6, gz + 0.9), (0.0, 4.7, gz + 1.1), 32, None),
            ("nose_side", (3.2, 4.6, gz + 0.8), (0.0, 4.75, gz + 1.0), 35, None),
            ("main", (4.8, 2.4, gz + 0.9), (2.0, -1.2, gz + 1.3), 32, None),
            ("main_head_on", (2.0, 12.0, gz + 1.2), (2.0, -1.2, gz + 1.2), 85, None),
            ("main_rear", (3.6, -5.2, gz + 0.5), (2.0, -1.0, gz + 1.4), 32, None)):
        render_view(pos, target, lens, f"{prefix}_{view}.png", ortho)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        sys.exit("model name required, e.g. su35s")
    name = args[0]
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    if "--loadout" in args:
        loadout(name)
    else:
        model, _, obj, _ = build_model(name, "--no-preview" not in args)
        if obj is not None and getattr(model, "HOST", None):
            mounted(model, obj)


main()
