#!/usr/bin/env python3
"""Turns a Tripo3D model with no skeleton (.glb) into a game mesh: monsters that move as a whole (the Clump, the
Deathmoths, SCP-173) and items (SCP-914, SCP-1162, SCP-500, SCP-999).

  python3 Tools/Monsters/make_static.py NAME GLB FOLDER SIZE_M [--length] [--turn DEG]

Writes FOLDER/NAME/ (e.g. Assets/Models/Monsters): NAME.fbx (feet on the ground, centred, facing +Y in the game),
its textures at 1024 px and NAME.material. SIZE_M is the height, or with --length the longest flat side.
--turn DEG turns a model Tripo made facing another way than -Y. Needs bpy and Pillow.
"""
import json
import math
import sys
from pathlib import Path

import bpy
import mathutils

ROOT = Path(__file__).resolve().parents[2]
TEX = 1024


def main():
    from PIL import Image
    args = sys.argv[1:]
    by_length = "--length" in args
    args = [a for a in args if a != "--length"]
    turn = 0.0
    if "--turn" in args:
        i = args.index("--turn")
        turn = float(args[i + 1])
        args = args[:i] + args[i + 2:]
    name, glb, folder, size = args[0], Path(args[1]), args[2], float(args[3])
    outdir = ROOT / folder / name
    outdir.mkdir(parents=True, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(glb))
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
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
            "metallic.textureMap": f"{name}_metallic.png", "metallic.useTexture": True, "roughness.useTexture": True,
            "baseColor.factor": 1.0}}, indent=4) + "\n")
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)

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
    # The turn, and nothing else: O3DE's FBX import turns models half way round (Blender's -Y comes in as the game's
    # +Y), and Tripo models face Blender's -Y already.
    obj.rotation_euler = (0.0, 0.0, math.radians(turn))
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    lo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)])
    hi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
    span = max(hi.x - lo.x, hi.y - lo.y) if by_length else hi.z - lo.z
    s = size / span
    obj.location = (-(lo.x + hi.x) / 2 * s, -(lo.y + hi.y) / 2 * s, -lo.z * s)
    obj.scale = (s, s, s)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.fbx(filepath=str(outdir / f"{name}.fbx"), use_selection=True, object_types={"MESH"},
                             path_mode="STRIP", embed_textures=False)
    vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
    box = " ".join(f"{min(v[i] for v in vs):+.2f}..{max(v[i] for v in vs):+.2f}" for i in range(3))
    print(f"{name}: x{s:.3f}, {len(obj.data.polygons)} faces, box {box}")


if __name__ == "__main__":
    main()
