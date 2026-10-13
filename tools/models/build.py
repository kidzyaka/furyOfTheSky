"""Build an aircraft model: indexed .fsm mesh + one baked .ftx texture per
livery, plus preview renders of exactly what the PSP will receive.

Usage (from the project root):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/build.py -- su35s [--no-preview]
"""

import importlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import fmodel  # noqa: E402

AO_STRENGTH = 0.85


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        sys.exit("model name required, e.g. su35s")
    name = args[0]
    preview = "--no-preview" not in args
    t0 = time.time()

    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)

    model = importlib.import_module(name)
    mb = model.build()
    mb.finalize(name)

    out_dir = os.path.join(ROOT, "assets", "models")
    prev_dir = os.path.join(ROOT, "build", "previews")
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(prev_dir, exist_ok=True)

    tris, verts, radius = mb.export_fsm(os.path.join(out_dir, f"{name}.fsm"))
    print(f"[model] {name}: {tris} triangles, {verts} vertices, radius {radius:.2f} m")

    maps = fmodel.TexelMaps(mb)
    print(f"[model] texel density {1.0 / maps.texel:.1f} texels/m, "
          f"atlas coverage {maps.mask.mean() * 100:.0f}%")
    ao = fmodel.ambient_occlusion(mb, maps)
    print(f"[model] ambient occlusion baked ({time.time() - t0:.0f}s)")
    shade = 1.0 - AO_STRENGTH * (1.0 - ao)

    obj = mb.to_object(name) if preview else None
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
            fmodel.render_previews(obj, os.path.join(prev_dir, f"{name}_{livery}"), radius)


main()
