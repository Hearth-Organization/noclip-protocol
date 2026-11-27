#!/usr/bin/env python3
"""Turns the Tripo3D weapon models (.glb) into the held weapons' meshes.

  python3 Tools/Weapons/make_weapon_models.py TRIPO_DIR

For each weapon: Assets/Models/Weapons/<Name>/<Name>.fbx (the mesh, turned to point along +Y, up +Z, and fitted to
the size and place the held primitives had, so the hold offsets in WeaponsComponent still fit), its textures at
1024 px and <Name>.material. make_weapons.py then puts the mesh in Prefabs/Weapons/View_<Name>.prefab.
Needs the bpy module (pip install bpy) and Pillow.
"""
import json
import math
import sys
from pathlib import Path

import bpy
import mathutils

ROOT = Path(__file__).resolve().parents[2]
TEX = 1024

# name: (tripo file, turn about Z in degrees so the muzzle / blade / front points +Y, fit "length" (Y) or "height" (Z),
#        size in metres, where its box centre goes (y, z)): the places the old primitive guns took in their prefabs.
WEAPONS = {
    "Pistol":   ("pistol.glb",   0,   "length", 0.19, (0.055, -0.012)),
    "Shotgun":  ("shotgun.glb",  0,   "length", 0.96, (0.010, -0.012)),
    "Carbine":  ("carbine.glb",  0,   "length", 0.88, (0.010, 0.000)),
    "FlareGun": ("flaregun.glb", 180, "length", 0.22, (0.050, -0.012)),
    "Knife":    ("knife.glb",    180, "length", 0.30, (0.046, 0.010)),
    "Shield":   ("shield2.glb",  -90, "height", 0.90, (0.100, 0.050)),
}


def one(name, glb, turn, fit, size, centre, src):
    from PIL import Image
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(src / glb))
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    outdir = ROOT / "Assets" / "Models" / "Weapons" / name
    outdir.mkdir(parents=True, exist_ok=True)

    images = {}
    for m in meshes:
        for slot in m.material_slots:
            for node in slot.material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    images[node.image.name.split("_")[0].lower()] = node.image
            slot.material.name = name

    def pil(image):
        tmp = outdir / f"_{name}.png"
        image.filepath_raw = str(tmp)
        image.file_format = "PNG"
        image.save()
        p = Image.open(tmp)
        p.load()
        tmp.unlink()
        return p

    pil(images["color"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS).save(outdir / f"{name}_basecolor.png")
    pil(images["normalgl"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS).save(outdir / f"{name}_normal.png")
    for ch, label in zip(pil(images["orm"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS).split(), ("ao", "roughness", "metallic")):
        ch.save(outdir / f"{name}_{label}.png")
    (outdir / f"{name}.material").write_text(json.dumps({
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": {
            "baseColor.textureMap": f"{name}_basecolor.png", "normal.textureMap": f"{name}_normal.png",
            "occlusion.diffuseTextureMap": f"{name}_ao.png", "roughness.textureMap": f"{name}_roughness.png",
            "metallic.textureMap": f"{name}_metallic.png", "metallic.useTexture": True, "roughness.useTexture": True}},
        indent=4) + "\n")
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)

    # Join, turn, fit, place; everything applied into the mesh.
    bpy.ops.object.select_all(action="DESELECT")
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.rotation_mode = "XYZ"   # glTF imports leave it on quaternions, which would ignore rotation_euler
    obj.rotation_euler = (0.0, 0.0, math.radians(turn))
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    lo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)])
    hi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
    s = size / ((hi.y - lo.y) if fit == "length" else (hi.z - lo.z))
    mid = (lo + hi) / 2
    obj.location = (-mid.x * s, centre[0] - mid.y * s, centre[1] - mid.z * s)
    obj.scale = (s, s, s)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Then the whole thing half way round about the origin: O3DE's FBX import turns models half way round
    # (Blender's -Y comes in as the game's +Y), so this lands pointing +Y in the game, at the place given above.
    obj.rotation_euler = (0.0, 0.0, math.radians(180))
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    bpy.ops.export_scene.fbx(filepath=str(outdir / f"{name}.fbx"), use_selection=True, object_types={"MESH"},
                             path_mode="STRIP", embed_textures=False)
    # Blender's own axes (the file says Y up, -Z forward; O3DE turns that back into Blender's Z up): with other
    # axes the guns came into the game standing on end.
    vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
    box = " ".join(f"{min(v[i] for v in vs):+.2f}..{max(v[i] for v in vs):+.2f}" for i in range(3))
    print(f"{name}: x{s:.3f}, {len(obj.data.polygons)} faces, box {box}")


def main():
    src = Path(sys.argv[1])
    for name, spec in WEAPONS.items():
        one(name, *spec, src)


if __name__ == "__main__":
    main()
