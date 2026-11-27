#!/usr/bin/env python3
"""Turns a Tripo3D monster (model + rig + animation .glb files) into game assets.

  python3 Tools/Monsters/make_monster.py NAME TRIPO_DIR HEIGHT_M [--turn DEG] [--length] ANIM [ANIM ...]

--turn DEG: an extra turn about Z for a model Tripo made facing another way than -Y (a side-view prompt).
--length: HEIGHT_M is the body's length instead (four-legged creatures).

TRIPO_DIR holds NAME-rig.glb and NAME-<anim>.glb (from hearth-workspace tools/tripo/tripo.py).
Writes Assets/Models/Monsters/NAME/:
  NAME.fbx                 the actor: mesh + skeleton, scaled to HEIGHT_M, facing +Y (O3DE forward)
  NAME_<anim>.fbx          one motion per animation: skeleton only, no mesh or textures
  NAME_basecolor.png, NAME_normal.png, NAME_ao.png, NAME_roughness.png, NAME_metallic.png   (1024 px)
  NAME.material            StandardPBR using them
Needs the bpy module (pip install bpy).
"""
import json, os, sys
from pathlib import Path

import bpy
import mathutils

ROOT = Path(__file__).resolve().parents[2]
TEX = 1024


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def load(glb):
    reset()
    bpy.ops.import_scene.gltf(filepath=str(glb))
    for o in list(bpy.data.objects):
        if o.type == "MESH" and o.name.startswith("Icosphere"):   # Tripo leaves a stray marker sphere in rigged files
            bpy.data.objects.remove(o, do_unlink=True)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    return arm, meshes


def fit(arm, meshes, height, by_length=False):
    """Scale to height, feet on the ground, centred, turned to face +Y. Tripo models face -Y after glTF import."""
    bpy.context.view_layer.update()
    pts = [m.matrix_world @ mathutils.Vector(c) for m in meshes for c in m.bound_box]
    lo = min(p.z for p in pts); hi = max(p.z for p in pts)
    cx = (min(p.x for p in pts) + max(p.x for p in pts)) / 2; cy = (min(p.y for p in pts) + max(p.y for p in pts)) / 2
    span = max(max(p.x for p in pts) - min(p.x for p in pts), max(p.y for p in pts) - min(p.y for p in pts)) if by_length else hi - lo
    s = height / span
    return s, mathutils.Vector((-cx, -cy, -lo))


TURN = 0.0


def place(arm, s, offset):
    # Applied on the armature object (its mesh children follow); the same numbers go on every motion file.
    arm.location = (arm.location + offset) * s
    arm.scale = arm.scale * s
    arm.rotation_mode = "XYZ"
    # Not turned: O3DE's FBX import turns a model half way round (Blender's -Y comes in as the game's +Y; seen in
    # game, Nov 27), and Tripo models already face Blender's -Y, so they come in facing +Y, the way they walk.
    arm.rotation_euler.z += TURN
    # Applying rescales the rest pose; the keyed bone movement (hips) is scaled to match by hand.
    if arm.animation_data and arm.animation_data.action:
        act = arm.animation_data.action
        sc = bpy.context.scene   # the export bakes the scene's frame range: make it the motion's own
        sc.frame_start, sc.frame_end = int(round(act.frame_range[0])), int(round(act.frame_range[1]))
        sc.render.fps = 30
        curves = act.fcurves if hasattr(act, "fcurves") else [c for l in act.layers for st in l.strips for b in st.channelbags for c in b.fcurves]
        for fc in curves:
            # Walks and runs move the hips forward (root motion); the game moves the monster itself, so take out
            # the drift from the first key to the last and keep the bob and sway.
            pts = fc.keyframe_points
            if fc.data_path.endswith(".location") and len(pts) > 2 and abs(pts[-1].co.y - pts[0].co.y) > 0.2:
                t0, t1, v0, v1 = pts[0].co.x, pts[-1].co.x, pts[0].co.y, pts[-1].co.y
                for k in pts:
                    d = (k.co.x - t0) / (t1 - t0) * (v1 - v0)
                    k.co.y -= d; k.handle_left.y -= d; k.handle_right.y -= d
            if fc.data_path.endswith(".location"):
                for k in fc.keyframe_points:
                    k.co.y *= s; k.handle_left.y *= s; k.handle_right.y *= s
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def export_fbx(out, with_mesh):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.fbx(filepath=str(out), use_selection=True,
                             object_types={"ARMATURE", "MESH"} if with_mesh else {"ARMATURE"},
                             add_leaf_bones=False, bake_anim=not with_mesh, bake_anim_use_all_actions=False,
                             bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True,
                             path_mode="STRIP", embed_textures=False, axis_forward="-Z", axis_up="Y")


def textures(name, meshes, outdir):
    from PIL import Image
    img = {}
    for m in meshes:
        for slot in m.material_slots:
            for node in slot.material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    n = node.image.name.split("_")[0].lower()
                    img[n] = node.image
    def pil(image):
        tmp = outdir / f"_{image.name}.png"
        image.filepath_raw = str(tmp); image.file_format = "PNG"; image.save()
        p = Image.open(tmp); p.load(); tmp.unlink(); return p
    pil(img["color"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS).save(outdir / f"{name}_basecolor.png")
    pil(img["normalgl"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS).save(outdir / f"{name}_normal.png")
    orm = pil(img["orm"]).convert("RGB").resize((TEX, TEX), Image.LANCZOS)
    for ch, label in zip(orm.split(), ("ao", "roughness", "metallic")):
        ch.save(outdir / f"{name}_{label}.png")
    rel = f"Assets/Models/Monsters/{name}"
    mat = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
           "materialTypeVersion": 5,
           "propertyValues": {
               "baseColor.textureMap": f"{name}_basecolor.png",
               "normal.textureMap": f"{name}_normal.png",
               "occlusion.diffuseTextureMap": f"{name}_ao.png",
               "roughness.textureMap": f"{name}_roughness.png",
               "metallic.textureMap": f"{name}_metallic.png",
               "metallic.useTexture": True, "roughness.useTexture": True, "baseColor.factor": 1.0}}
    (outdir / f"{name}.material").write_text(json.dumps(mat, indent=4) + "\n")


def main():
    global TURN
    name, src, height, rest = sys.argv[1], Path(sys.argv[2]), float(sys.argv[3]), sys.argv[4:]
    by_length = "--length" in rest
    rest = [a for a in rest if a != "--length"]
    if rest[:1] == ["--turn"]:
        import math
        TURN = math.radians(float(rest[1]))
        rest = rest[2:]
    anims = rest
    outdir = ROOT / "Assets" / "Models" / "Monsters" / name
    outdir.mkdir(parents=True, exist_ok=True)
    arm, meshes = load(src / f"{name.lower()}-rig.glb")
    s, offset = fit(arm, meshes, height, by_length)
    textures(name, meshes, outdir)
    for m in meshes:   # the actor's own material comes from NAME.material; keep the FBX free of images
        for slot in m.material_slots:
            slot.material.name = name
    place(arm, s, offset)
    export_fbx(outdir / f"{name}.fbx", True)
    for anim in anims:
        arm, meshes = load(src / f"{name.lower()}-{anim}.glb")
        for m in meshes:
            bpy.data.objects.remove(m, do_unlink=True)
        for img in list(bpy.data.images):
            bpy.data.images.remove(img)
        place(arm, s, offset)
        export_fbx(outdir / f"{name}_{anim}.fbx", False)
    print(f"{name}: scale {s:.3f}, {len(anims)} motions -> {outdir.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
