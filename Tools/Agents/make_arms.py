#!/usr/bin/env python3
"""First-person arms for every weapon (owner, Nov 27: "Make temporary body and arms, as realistic as you can").

    python3 Tools/Agents/make_arms.py SOLDIER_GLB [--preview DIR]

Takes the agent's own body (the same Soldier.glb as Tools/Agents/make_agent.py), poses its arms on each weapon with
IK (wrists on the grips, elbows down and out, hands turned onto the grip, fingers closed round it), keeps only the
arms (cut off above the elbow, out of view), and writes them in the weapon's own space (x right, y ahead, z up, the
same space as the View_* models) to Assets/Models/Weapons/<Weapon>/<Weapon>_arms.fbx, with the agent's material.
--preview DIR renders each weapon's arms from the eye (with stand-in boxes for the gun) as DIR/<Weapon>.png.
"""
import math
import sys
from pathlib import Path

import bpy
import mathutils
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
P = "mixamorig:"

# Where each weapon is held, camera space (WeaponsComponent::PlaceView's hold[]), and the hands on it, in the
# weapon's own space: wrist point, the hand's long axis (wrist to knuckles), the palm's facing, finger curl (degrees:
# index, the other three, thumb). None: that arm isn't shown.
HOLD = {"Pistol": (0.17, 0.52, -0.19), "Shotgun": (0.17, 0.50, -0.23), "Carbine": (0.16, 0.50, -0.22),
        "FlareGun": (0.17, 0.50, -0.19), "Knife": (0.20, 0.46, -0.22), "Shield": (-0.26, 0.62, -0.62)}
TWO_HANDED_PISTOL = {
    "right": ((0.01, -0.08, -0.105), (0.0, 0.72, 0.69), (-1.0, 0.0, 0.05), (25, 85, 55)),
    "left": "light",
}
HANDS = {
    "Pistol": TWO_HANDED_PISTOL,
    "FlareGun": {"right": ((0.01, -0.105, -0.095), (0.0, 0.72, 0.69), (-1.0, 0.0, 0.05), (25, 85, 55)),
                 "left": "light"},
    "Shotgun": {"right": ((0.01, -0.17, -0.10), (0.0, 0.72, 0.69), (-1.0, 0.0, 0.05), (30, 85, 55)),
                "left": ((-0.02, 0.16, -0.045), (0.25, 0.95, 0.15), (0.05, 0.0, 1.0), (70, 75, 40))},
    "Carbine": {"right": ((0.01, -0.17, -0.105), (0.0, 0.72, 0.69), (-1.0, 0.0, 0.05), (30, 85, 55)),
                "left": ((-0.02, 0.16, -0.03), (0.25, 0.95, 0.15), (0.05, 0.0, 1.0), (70, 75, 40))},
    "Knife": {"right": ((0.0, -0.06, -0.075), (0.0, 0.25, 0.97), (-1.0, 0.0, 0.0), (85, 90, 50)), "left": "light"},
    "Shield": {"right": None, "left": ((0.0, -0.05, -0.08), (0.0, 0.1, 1.0), (1.0, 0.0, 0.0), (80, 85, 40))},
}
ARM_BONES = ("Arm", "ForeArm", "Hand")
sys.path.insert(0, str(ROOT / "Tools" / "Weapons"))
from lights import LIGHTS  # noqa: E402


def held_light(weapon):
    """The spare hand closed round the flashlight (Tools/Weapons/lights.py): a hammer grip, the light pointing ahead."""
    (x, y, z), _ = LIGHTS[weapon]
    return ((x - 0.028, y - 0.02, z - 0.072), (0.0, 0.25, 0.97), (1.0, 0.0, 0.0), (88, 92, 55))


for _w, _hands in HANDS.items():
    if _hands.get("left") == "light":
        _hands["left"] = held_light(_w)
FINGERS = ("Index", "Middle", "Ring", "Pinky", "Thumb")


def load(glb):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(glb))
    for o in list(bpy.data.objects):
        if o.type == "EMPTY" or (o.type == "MESH" and o.name.startswith("Icosphere")):
            bpy.data.objects.remove(o, do_unlink=True)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    if arm.animation_data:
        arm.animation_data.action = None
    body = bpy.data.objects["vanguard_Mesh"]
    bpy.data.objects.remove(bpy.data.objects["vanguard_visor"], do_unlink=True)
    # The glTF import guesses bone ends; point each arm and finger bone at its child so the IK bends the real
    # elbow and wrist (with the guessed ends the wrists landed 8 cm off and the IK ran away when corrected).
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    for side in ("Right", "Left"):
        chain = [side + "Shoulder", side + "Arm", side + "ForeArm", side + "Hand"]
        for a, b in zip(chain, chain[1:]):
            eb[P + a].tail = eb[P + b].head
        hand = eb[P + side + "Hand"]
        hand.tail = eb[P + side + "HandMiddle1"].head
        for finger in FINGERS:
            for j in (1, 2, 3):
                if P + f"{side}Hand{finger}{j + 1}" in eb:
                    eb[P + f"{side}Hand{finger}{j}"].tail = eb[P + f"{side}Hand{finger}{j + 1}"].head
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = (1, 0, 0, 0)
    bpy.context.view_layer.update()
    return arm, body


def world_rest(arm, name):
    return arm.matrix_world @ arm.data.bones[P + name].matrix_local


def empty(name, loc, rot=None):
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    e.matrix_world = Matrix.Translation(loc) @ (rot.to_4x4() if rot else Matrix())
    return e


def basis(long_axis, palm, rest):
    """The world rotation that puts this bone's own long axis (Y) along long_axis and its palm side onto palm.
    The palm side in the bone's own space: where the rest pose (arms out, palms down) has world -Z."""
    rest3 = rest.to_3x3().normalized()
    palm_local = (rest3.inverted() @ Vector((0, 0, -1))).normalized()
    y = Vector(long_axis).normalized()
    p = Vector(palm)
    p = (p - y * p.dot(y)).normalized()
    # Local frame: Y, the palm direction, and their cross; mapped onto y, p, cross.
    pl = (palm_local - Vector((0, 1, 0)) * palm_local.y).normalized()
    local = Matrix((Vector((0, 1, 0)), pl, Vector((0, 1, 0)).cross(pl))).transposed()
    world = Matrix((y, p, y.cross(p))).transposed()
    return world @ local.inverted()


def curl(arm, side, angles):
    """Close the fingers: each joint turned about its own bend axis (towards the palm)."""
    index, others, thumb = angles
    for finger in FINGERS:
        deg = index if finger == "Index" else thumb if finger == "Thumb" else others
        for j in (1, 2, 3):
            name = f"{P}{side}Hand{finger}{j}"
            if name not in arm.pose.bones:
                continue
            rest = arm.data.bones[name].matrix_local.to_3x3().normalized()
            palm_local = (rest.inverted() @ Vector((0, 0, -1))).normalized()
            axis = Vector((0, 1, 0)).cross(palm_local).normalized()
            if finger == "Thumb":
                axis = axis if side == "Right" else -axis
                a = math.radians(deg * (0.6 if j == 1 else 1.0))
            else:
                a = math.radians(deg * (0.8 if j == 1 else 1.0 if j == 2 else 0.7))
            arm.pose.bones[name].rotation_quaternion = mathutils.Quaternion(axis, a)


def pose(arm, weapon, origin):
    """IK both arms onto the weapon at origin (Blender world, the character facing +Y so camera axes are world
    axes). Returns which sides are shown."""
    shown = []
    # The gun is held further out than an arm reaches from a standing body's shoulders (every shooter does this):
    # each shoulder (the collarbone's end) is brought towards its hand until it reaches with the elbow a little bent.
    for side, spec in (("Right", HANDS[weapon]["right"]), ("Left", HANDS[weapon]["left"])):
        if spec is None:
            continue
        target = origin + Vector(spec[0])
        head = world_rest(arm, side + "Arm").translation
        # Where the shoulder goes: back, down and out from the wrist, so the forearms rise into view from the
        # bottom corners the way they do in a shooter (straight back, they filled the screen side to side).
        sx = 1.0 if side == "Right" else -1.0
        want = target + Vector((0.09 * sx, -0.30, -0.30)).normalized() * 0.42
        far = 1.0
        if far > 0.0:
            move_world = want - head
            pb = arm.pose.bones[P + side + "Shoulder"]
            m = pb.matrix.copy()
            m.translation += arm.matrix_world.to_3x3().inverted() @ move_world
            pb.matrix = m
            bpy.context.view_layer.update()
    for side, spec in (("Right", HANDS[weapon]["right"]), ("Left", HANDS[weapon]["left"])):
        if spec is None:
            continue
        wrist, long_axis, palm, angles = spec
        target = origin + Vector(wrist)
        sx = 1.0 if side == "Right" else -1.0
        shoulder = (arm.matrix_world @ arm.pose.bones[P + side + "Arm"].matrix).translation
        pole = (shoulder + target) / 2 + Vector((0.25 * sx, -0.05, -0.30))
        t = empty(f"{side}Target", target)
        p = empty(f"{side}Pole", pole)
        fore = arm.pose.bones[P + side + "ForeArm"]
        ik = fore.constraints.new("IK")
        ik.target, ik.chain_count = t, 2   # (a pole target made Blender's IK miss the wrist by 10 cm)
        hand = arm.pose.bones[P + side + "Hand"]
        rot = basis(long_axis, palm, world_rest(arm, side + "Hand"))
        h = empty(f"{side}HandAim", target, rot)
        cr = hand.constraints.new("COPY_ROTATION")
        cr.target = h
        curl(arm, side, angles)
        shown.append(side)
    bpy.context.view_layer.update()
    bpy.context.view_layer.update()
    return shown


def eye_of(arm):
    head = world_rest(arm, "Head").translation
    return head + Vector((0.0, 0.10, 0.10))


def bake(arm, body, shown, origin):
    """The posed arms as a plain mesh in the weapon's own space."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    mesh = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    keep_groups = {body.vertex_groups[P + s + b].index for s in shown for b in ARM_BONES if P + s + b in body.vertex_groups}
    for s in shown:
        for finger in FINGERS:
            for j in (1, 2, 3, 4):
                n = f"{P}{s}Hand{finger}{j}"
                if n in body.vertex_groups:
                    keep_groups.add(body.vertex_groups[n].index)
    upper = {body.vertex_groups[P + s + "Arm"].index for s in shown}
    elbows = [(arm.matrix_world @ arm.pose.bones[P + s + "ForeArm"].matrix).translation for s in shown]
    posed = [body.matrix_world @ v.co for v in mesh.vertices]
    keep = []
    for v, at in zip(body.data.vertices, posed):
        w = sum(g.weight for g in v.groups if g.group in keep_groups)
        wu = sum(g.weight for g in v.groups if g.group in upper)
        near_elbow = min((at - e).length for e in elbows) < 0.13
        keep.append(w > 0.6 and (w - wu > 0.3 or near_elbow))  # the arm from just above the elbow down
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.verts[i] for i, k in enumerate(keep) if not k], context="VERTS")
    m = body.matrix_world
    for v in bm.verts:
        v.co = (m @ v.co) - origin
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("Arms", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def gun_boxes(weapon, origin):
    sys.path.insert(0, str(ROOT / "Tools" / "Weapons"))
    import make_weapons as mw
    objs = []
    import lights
    for kind, centre, size, mat, rot in mw.VIEWS[weapon] + lights.parts(weapon, mw.B, mw.C):
        if kind == "box":
            bpy.ops.mesh.primitive_cube_add(size=1.0)
            o = bpy.context.object
            o.scale = size
        else:
            bpy.ops.mesh.primitive_cylinder_add(vertices=16)
            o = bpy.context.object
            o.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
        o.location = origin + Vector(centre)
        if rot:
            o.rotation_euler = [math.radians(a) for a in rot]
        objs.append(o)
    return objs


def preview(weapon, arms, origin, eye, out):
    sc = bpy.context.scene
    arms.location = origin
    gun_boxes(weapon, origin)
    cam = bpy.data.cameras.new("Eye")
    cam.lens_unit = "FOV"
    cam.angle = math.radians(90)
    cam.clip_start = 0.01
    co = bpy.data.objects.new("Eye", cam)
    sc.collection.objects.link(co)
    co.location = eye
    co.rotation_euler = (math.radians(90), 0, 0)  # looking +Y
    sc.camera = co
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 960, 540
    sc.render.filepath = str(out)
    bpy.ops.render.render(write_still=True)


def export(weapon, arms):
    out = ROOT / "Assets" / "Models" / "Weapons" / weapon / f"{weapon}_arms.fbx"
    for slot in arms.material_slots:
        slot.material.name = "Agent"
    bpy.ops.object.select_all(action="DESELECT")
    arms.location = (0, 0, 0)
    arms.select_set(True)
    bpy.context.view_layer.objects.active = arms
    # O3DE's FBX import turns models half way round (Blender's -Y comes in as the game's +Y): turn first.
    arms.rotation_euler = (0.0, 0.0, math.radians(180))
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.fbx(filepath=str(out), use_selection=True, object_types={"MESH"}, path_mode="STRIP",
                             embed_textures=False)
    vs = [v.co for v in arms.data.vertices]
    print(f"{weapon}: {len(arms.data.polygons)} faces -> {out.relative_to(ROOT)}")


def main():
    src = Path(sys.argv[1])
    prev = Path(sys.argv[sys.argv.index("--preview") + 1]) if "--preview" in sys.argv else None
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else list(HANDS)
    for weapon in only:
        arm, body = load(src)
        eye = eye_of(arm)
        origin = eye + Vector(HOLD[weapon])
        shown = pose(arm, weapon, origin)
        arms = bake(arm, body, shown, origin)
        bpy.data.objects.remove(body, do_unlink=True)
        bpy.data.objects.remove(arm, do_unlink=True)
        if prev:
            prev.mkdir(parents=True, exist_ok=True)
            preview(weapon, arms, origin, eye, prev / f"{weapon}.png")
        else:
            export(weapon, arms)


if __name__ == "__main__":
    main()
