#!/usr/bin/env python3
"""The agents' temporary body (owner, Nov 27: "Make temporary body and arms, as realistic as you can", until Tripo
or Hearth Forge can make our own): a rigged soldier in tactical gear with idle, walk and run.

    python3 Tools/Agents/make_agent.py SOLDIER_GLB

SOLDIER_GLB: Soldier.glb from the three.js examples (examples/models/gltf/Soldier.glb), Mixamo's "Vanguard" character
with Mixamo animations (free to use in games). Writes Assets/Models/Agents/Agent/:
  Agent.fbx                  the actor (mesh + skeleton, 1.78 m, facing +Y in the game)
  Agent_idle/walk/run.fbx    the motions (skeleton only)
  Agent_basecolor.png, Agent_normal.png, Agent.material
Uses Tools/Monsters/make_monster.py's fitting and export (needs the bpy module). The first-person arms are
Tools/Agents/make_arms.py.
"""
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "Monsters"))
import bpy  # noqa: E402
import make_monster as mm  # noqa: E402

NAME, HEIGHT, TEX = "Agent", 1.78, 1024
ANIMS = {"idle": "Idle", "walk": "Walk", "run": "Run"}


def load(glb, action=None):
    mm.reset()
    bpy.ops.import_scene.gltf(filepath=str(glb))
    for o in list(bpy.data.objects):
        if o.type == "EMPTY" or (o.type == "MESH" and o.name.startswith("Icosphere")):
            bpy.data.objects.remove(o, do_unlink=True)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    if arm.animation_data is None:
        arm.animation_data_create()
    # This rig is in centimetres under a 0.01 scale: the keyed hip movement is too. Into metres, like the rest pose
    # will be once make_monster's place() applies the scale (left alone, the hips went 100 m up and the body vanished).
    unit = arm.scale.x
    for act in bpy.data.actions:
        curves = act.fcurves if hasattr(act, "fcurves") else [c for l in act.layers for st in l.strips for b in st.channelbags for c in b.fcurves]
        for fc in curves:
            if fc.data_path.endswith(".location"):
                for k in fc.keyframe_points:
                    k.co.y *= unit; k.handle_left.y *= unit; k.handle_right.y *= unit
    arm.animation_data.action = bpy.data.actions[action] if action else bpy.data.actions["TPose"]
    bpy.context.scene.frame_set(0)
    return arm, [o for o in bpy.data.objects if o.type == "MESH"]


def textures(outdir):
    from PIL import Image
    def save(image, out):
        tmp = outdir / f"_{image.name}.png"
        image.filepath_raw = str(tmp); image.file_format = "PNG"; image.save()
        p = Image.open(tmp); p.load(); tmp.unlink()
        p.convert("RGB").resize((TEX, TEX), Image.LANCZOS).save(out)
    save(bpy.data.images["vanguard_vanguard_diffuse_tga"], outdir / f"{NAME}_basecolor.png")
    save(bpy.data.images["file2"], outdir / f"{NAME}_normal.png")
    mat = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
           "materialTypeVersion": 5,
           "propertyValues": {"baseColor.textureMap": f"{NAME}_basecolor.png", "normal.textureMap": f"{NAME}_normal.png",
                              "baseColor.factor": 0.55, "roughness.factor": 0.7, "metallic.factor": 0.1}}
    (outdir / f"{NAME}.material").write_text(json.dumps(mat, indent=4) + "\n")


def main():
    src = Path(sys.argv[1])
    outdir = ROOT / "Assets" / "Models" / "Agents" / NAME
    outdir.mkdir(parents=True, exist_ok=True)
    mm.TURN = math.pi   # this one faces Blender's +Y; turned to face -Y like the Tripo models (the game's +Y)
    arm, meshes = load(src)
    s, offset = mm.fit(arm, meshes, HEIGHT)
    textures(outdir)
    for m in meshes:
        for slot in m.material_slots:
            slot.material.name = NAME
    arm.animation_data.action = None
    mm.place(arm, s, offset)
    mm.export_fbx(outdir / f"{NAME}.fbx", True)
    for ours, theirs in ANIMS.items():
        arm, meshes = load(src, theirs)
        for m in meshes:
            bpy.data.objects.remove(m, do_unlink=True)
        for img in list(bpy.data.images):
            bpy.data.images.remove(img)
        mm.place(arm, s, offset)
        mm.export_fbx(outdir / f"{NAME}_{ours}.fbx", False)
    print(f"{NAME}: scale {s:.4f}, {len(ANIMS)} motions -> {outdir.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
