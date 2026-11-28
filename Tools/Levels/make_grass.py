"""Real grass for the outdoor levels: clumps of individual blades (Assets/Models/Foliage/GrassClump*/), made here, not
scanned, so thousands of them can stand in a lawn. Each blade is a tapered, curved strip with a dark base and a lighter
tip; a clump is about 1 m across. Three clumps (short lawn, long unmown, dry) so a field never repeats.

The owner, Nov 28: "Real grass ect" (for Level 9's lawns and fields, and the other outdoor levels).

usage: python3 Tools/Levels/make_grass.py
Needs bpy (Blender as a module) and Pillow.
"""
import json
import math
import random
from pathlib import Path

import bpy
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Assets" / "Models" / "Foliage"

# name: (blades, min height, max height, base colour, tip colour, lean)
CLUMPS = {
    "GrassClumpShort": (220, 0.12, 0.26, (0.10, 0.16, 0.05), (0.33, 0.42, 0.16), 0.25),
    "GrassClumpLong": (160, 0.30, 0.62, (0.09, 0.14, 0.05), (0.38, 0.44, 0.18), 0.45),
    "GrassClumpDry": (180, 0.18, 0.45, (0.18, 0.16, 0.08), (0.52, 0.46, 0.27), 0.40),
}


def gradient(path, base, tip):
    """A 16x256 strip: v=0 the root (dark, a little dirt), v=1 the tip."""
    im = Image.new("RGB", (16, 256))
    px = im.load()
    for y in range(256):
        t = 1.0 - y / 255.0          # image row 0 is v = 1
        t = t ** 0.8
        c = [base[i] + (tip[i] - base[i]) * t for i in range(3)]
        for x in range(16):
            px[x, y] = tuple(int(max(0, min(1, v)) * 255) for v in c)
    im.save(path)


def blade(rng, height, lean, verts, faces, uvs, at=None, wide=1.0):
    """One blade: 4 segments, 2 vertices wide, narrowing to a point and bending over."""
    if at is None:
        r = 0.5 * math.sqrt(rng.random())
        a = rng.random() * math.tau
        x0, y0 = math.cos(a) * r, math.sin(a) * r
    else:
        x0, y0 = at
    face_dir = rng.random() * math.tau
    bend = rng.random() * math.tau
    width = rng.uniform(0.006, 0.012) * wide
    segs = 4
    start = len(verts)
    for s in range(segs + 1):
        t = s / segs
        w = width * (1.0 - t) ** 0.7
        off = lean * height * t * t
        cx, cy, cz = x0 + math.cos(bend) * off, y0 + math.sin(bend) * off, height * t * (1.0 - 0.25 * lean * t)
        px, py = math.cos(face_dir) * w, math.sin(face_dir) * w
        verts.append((cx - px, cy - py, cz))
        verts.append((cx + px, cy + py, cz))
        uvs.append((0.0, t))
        uvs.append((1.0, t))
    for s in range(segs):
        i = start + s * 2
        faces.append((i, i + 1, i + 3, i + 2))


# Patches: a 5 m square of blades for a whole lawn, so a yard is a couple of dozen entities, not hundreds
# (2.5 m patches, 16 to a lot, froze the game loading Level 9: Nov 28). Wider blades, fewer of them.
# name: (blades per square metre, min height, max height, base, tip, lean)
PATCH = 5.0
PATCHES = {
    "GrassPatchShort": (110, 0.08, 0.22, (0.09, 0.15, 0.05), (0.30, 0.40, 0.15), 0.25),
    "GrassPatchLong": (80, 0.25, 0.65, (0.09, 0.14, 0.05), (0.36, 0.42, 0.17), 0.45),
    "GrassPatchDry": (80, 0.15, 0.45, (0.17, 0.15, 0.08), (0.50, 0.44, 0.26), 0.40),
}
WIDE = {"GrassPatchShort": 1.8, "GrassPatchLong": 1.6, "GrassPatchDry": 1.6}  # blades this much wider


def tufts(rng, x, y):
    """How tall the grass grows here: lumpy, so a lawn has thick tufts and thin spots (tiles across patches)."""
    v = 0.0
    for k, (fx, fy, ph) in enumerate(((2.513, 1.7, 0.3), (5.03, 3.9, 1.7), (7.54, 6.2, 4.1))):
        v += math.sin(x * fx + ph) * math.cos(y * fy - ph) / (k + 1)
    return 0.5 + 0.35 * v


def make(name, spec):
    count, lo, hi, base, tip, lean = spec
    rng = random.Random(name)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    verts, faces, uvs = [], [], []
    if name.startswith("GrassPatch"):
        for _ in range(int(count * PATCH * PATCH)):
            x, y = (rng.random() - 0.5) * PATCH, (rng.random() - 0.5) * PATCH
            t = max(0.0, min(1.0, tufts(rng, x, y) + rng.uniform(-0.2, 0.2)))
            blade(rng, lo + (hi - lo) * t, lean * rng.uniform(0.6, 1.3), verts, faces, uvs, (x, y), WIDE[name])
    else:
        for _ in range(count):
            blade(rng, rng.uniform(lo, hi), lean * rng.uniform(0.6, 1.3), verts, faces, uvs)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    layer = mesh.uv_layers.new(name="UVMap")
    for poly in mesh.polygons:
        for li in poly.loop_indices:
            layer.data[li].uv = uvs[mesh.loops[li].vertex_index]
    # Blades lit like a soft canopy: normals bent towards up, so the clump doesn't flash as you walk round it.
    mesh.shade_smooth()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    mat = bpy.data.materials.new(name)
    obj.data.materials.append(mat)
    folder = OUT / name
    folder.mkdir(parents=True, exist_ok=True)
    gradient(folder / f"{name}_basecolor.png", base, tip)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(folder / f"{name}.fbx"), use_selection=True, object_types={"MESH"},
                             path_mode="STRIP", axis_forward="-Y", axis_up="Z", apply_scale_options="FBX_SCALE_ALL")
    material = {
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": {
            "baseColor.textureMap": f"{name}_basecolor.png",
            "roughness.factor": 0.75,
            "metallic.factor": 0.0,
            "specularF0.factor": 0.35,
            "general.doubleSided": True,
        },
    }
    json.dump(material, open(folder / f"{name}.material", "w"), indent=4)
    print(name, len(verts), "vertices", len(faces), "quads")


for n, s in {**CLUMPS, **PATCHES}.items():
    make(n, s)
