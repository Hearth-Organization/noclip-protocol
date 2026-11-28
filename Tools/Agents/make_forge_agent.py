#!/usr/bin/env python3
"""The agents' own bodies from Hearth Forge, rigged and animated (owner, Nov 28: "do the skeleton and animations").

    python3 Tools/Agents/make_forge_agent.py SOLDIER_GLB BODY_GLB NAME [--preview PNG]

SOLDIER_GLB: Soldier.glb from the three.js examples (Mixamo's "Vanguard" with Mixamo's idle, walk and run, free to use
in games; the same file as Tools/Agents/make_agent.py). BODY_GLB: a Forge agent (hearth-workspace
noclip-protocol/assets/2026-11-28-forge-agent: 1.80 m or 1.71 m, feet on the ground, facing -Y). NAME: AgentMale or
AgentFemale. Writes Assets/Models/Agents/NAME/:
  NAME.fbx                    the actor: the body on a Mixamo skeleton fitted inside it
  NAME_idle/walk/run/death.fbx  the motions (skeleton only)
  NAME_basecolor.png, NAME.material

How: the Soldier's skeleton is scaled to the body's shoulders, its arms are posed down the body's arms, and that pose
becomes the rest pose, with the limbs, neck and head moved into the body's own (found slice by slice from its
surface). Weights come from bone heat on a welded copy of the body (it is split at its UV seams). The body is never
bent: each Mixamo motion is moved across bone by bone as the turn away from the Soldier's rest, put on this rest, so
an arm hanging down in the idle hangs down here too. The death is keyed by hand: a hit, the knees go, back and down.
"""
import json
import math
import sys
from pathlib import Path

import bpy
import bmesh  # noqa: E402  (after bpy)
from mathutils import Matrix, Quaternion, Vector
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parents[2]
P = "mixamorig:"
FPS = 30
MOTIONS = {"idle": "Idle", "walk": "Walk", "run": "Run"}


def soldier(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(path))
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    for o in list(bpy.data.objects):
        if o is not arm and o.type in ("MESH", "EMPTY"):
            bpy.data.objects.remove(o, do_unlink=True)
    # centimetres under a 0.01 scale: into metres (bone heat fails at that scale), the keyed hip moves too
    unit = arm.scale.x
    for act in bpy.data.actions:
        for fc in curves(act):
            if fc.data_path.endswith(".location"):
                for k in fc.keyframe_points:
                    k.co.y *= unit; k.handle_left.y *= unit; k.handle_right.y *= unit
    arm.animation_data.action = None
    rest(arm)
    select(arm)
    # the Soldier faces +Y; the Forge bodies face -Y (the way the game's FBX import wants them): turn the rig
    arm.rotation_euler = (0, 0, math.pi)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    # The glTF import guesses the bones' ends (a hand came out 4.5 m long, a foot 19 m), and bone heat then gave a
    # boot's sole to a hand. Each bone keeps its direction (its frame, which the motions are keyed in) and gets a
    # real length: to its child, or a short stub at the end of a chain.
    bpy.ops.object.mode_set(mode="EDIT")
    for b in arm.data.edit_bones:
        d = (b.tail - b.head).normalized()
        kids = [c for c in b.children]
        length = min((c.head - b.head).length for c in kids) if kids else 0.06
        if length < 0.01:
            length = 0.06
        roll = b.roll
        b.tail = b.head + d * length
        b.roll = roll
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def curves(act):
    return act.fcurves if hasattr(act, "fcurves") else [c for l in act.layers for st in l.strips for b in st.channelbags for c in b.fcurves]


def rest(arm):
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()


def select(obj, *more):
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object and bpy.context.object.mode != "OBJECT" else None
    bpy.ops.object.select_all(action="DESELECT")
    for o in (obj,) + more:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obj


def head_w(arm, n):
    return arm.matrix_world @ arm.data.bones[P + n].head_local


def load_body(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    select(meshes[0], *meshes[1:])
    if len(meshes) > 1:
        bpy.ops.object.join()
    body = bpy.context.object
    body.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in new:
        if o is not body and o.name in bpy.data.objects:
            bpy.data.objects.remove(o, do_unlink=True)
    return body


def widths(pts, z, dz=0.012):
    xs = [p.x for p in pts if abs(p.z - z) < dz]
    return (max(xs) - min(xs)) if xs else 0.0


def shoulder_height(pts):
    """Scanning down from the head: the first height where the body is much wider than its neck (the shoulders' top),
    less a little to the joint."""
    H = max(p.z for p in pts)
    zs = [H * (0.70 + i * 0.005) for i in range(60)]
    neck = min(widths(pts, z) for z in zs if z > H * 0.80)
    for z in sorted(zs, reverse=True):
        if z < H * 0.86 and widths(pts, z) > neck * 1.9:
            return z - 0.05 * H, neck
    return H * 0.80, neck


def neck_height(pts, shoulder):
    H = max(p.z for p in pts)
    best, bz = 9.0, shoulder + 0.08
    z = shoulder + 0.03
    while z < H - 0.12:
        w = widths(pts, z)
        if 0 < w < best:
            best, bz = w, z
        z += 0.01
    return bz


def arm_line(pts, sign, sh_z, floor):
    """Centres of the arm, slice by slice down from the shoulder to the crotch (the outermost 10 cm of each slice past
    the chest; below the crotch the outermost points would be the legs')."""
    out = []
    for i in range(40):
        z = sh_z + 0.02 - i * 0.02
        if z < floor:
            break
        sl = [p for p in pts if abs(p.z - z) < 0.012 and sign * p.x > 0.12]
        if len(sl) < 8:
            continue
        ex = max(sign * p.x for p in sl)
        near = [p for p in sl if sign * p.x > ex - 0.10]
        out.append(sum(near, Vector()) / len(near))
    return out


def crotch(pts):
    for i in range(80):
        z = 0.4 + i * 0.01
        if sum(1 for p in pts if abs(p.z - z) < 0.01 and abs(p.x) < 0.03) > 6:
            return z
    return 0.85


def mean(pts):
    return sum(pts, Vector()) / len(pts)


def build_rest(src, body):
    """The rig for this body: the Soldier's skeleton, arms posed down the body's arms, made the rest pose, limbs moved
    into the body's limbs. Returns the new armature (same bone names)."""
    pts = [v.co.copy() for v in body.data.vertices]
    sh, _ = shoulder_height(pts)
    k = sh / head_w(src, "LeftArm").z
    select(src)
    src.scale = (k, k, k)
    for act in bpy.data.actions:
        for fc in curves(act):
            if fc.data_path.endswith(".location"):
                for kp in fc.keyframe_points:
                    kp.co.y *= k; kp.handle_left.y *= k; kp.handle_right.y *= k
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    hip = head_w(src, "Hips")
    # centre the body on the rig (x and y), feet stay on the ground
    xs = [p.x for p in pts]; ys = [p.y for p in pts if abs(p.z - hip.z) < 0.05]
    d = Vector((-(min(xs) + max(xs)) / 2 + hip.x, hip.y - (min(ys) + max(ys)) / 2, 0))
    for v in body.data.vertices:
        v.co += d
    pts = [v.co.copy() for v in body.data.vertices]
    cz = crotch(pts)
    rig = src.copy(); rig.data = src.data.copy(); rig.animation_data_clear()
    bpy.context.scene.collection.objects.link(rig)
    rig.name = "Agent"
    # arms down along the body's arms (posed, then applied as the rest)
    select(rig)
    bpy.ops.object.mode_set(mode="POSE")
    for side, sign in (("Left", 1), ("Right", -1)):
        line = arm_line(pts, sign, sh, cz - 0.02)
        up = [p for p in line if p.z > sh - 0.22]; lo = [p for p in line if p.z < sh - 0.30]
        want = (mean(lo) - mean(up)).normalized()
        pb = rig.pose.bones[P + side + "Arm"]
        have = (head_w(rig, side + "ForeArm") - head_w(rig, side + "Arm")).normalized()
        q = have.rotation_difference(want)
        M = rig.matrix_world @ pb.bone.matrix_local
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = M.to_quaternion().inverted() @ q @ M.to_quaternion()
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = rig.data.edit_bones
    W = rig.matrix_world; Wi = W.inverted()

    def wh(b):
        return W @ b.head

    def move(b, to):
        dl = Wi.to_3x3() @ (to - wh(b))
        for c in [b] + list(b.children_recursive):
            c.head += dl; c.tail += dl

    def tail_to(b, c):
        b.tail = c.head.copy()

    for side, sign in (("Left", 1), ("Right", -1)):
        line = arm_line(pts, sign, sh, cz - 0.02)
        top = line[2]; low = min(p.z for p in line)
        wrist = next(p for p in line if p.z <= top.z - (top.z - low) * 0.80)
        elbow = next(p for p in line if p.z <= top.z - (top.z - wrist.z) * 0.5)
        a, f, h = eb[P + side + "Arm"], eb[P + side + "ForeArm"], eb[P + side + "Hand"]
        move(a, Vector((sign * max(abs(wh(a).x), abs(top.x) - 0.02), top.y, wh(a).z)))
        move(f, elbow); move(h, wrist); tail_to(a, f); tail_to(f, h)
        print(f"{side} arm: shoulder {top.z:.2f}, elbow {elbow.z:.2f}, wrist {wrist.z:.2f}")
    for side, sign in (("Left", 1), ("Right", -1)):
        u, l, ft = eb[P + side + "UpLeg"], eb[P + side + "Leg"], eb[P + side + "Foot"]
        thigh = mean([p for p in pts if abs(p.z - (cz - 0.05)) < 0.02 and sign * p.x > 0.01])
        ankle = mean([p for p in pts if abs(p.z - 0.12) < 0.015 and sign * p.x > 0.01])
        hipj = Vector((thigh.x, thigh.y, cz + 0.06)); ankle.z = wh(ft).z
        kz = ankle.z + (hipj.z - ankle.z) * 0.53
        knee = mean([p for p in pts if abs(p.z - kz) < 0.015 and sign * p.x > 0.01]); knee.z = kz
        move(u, hipj); move(l, knee); move(ft, ankle); tail_to(u, l); tail_to(l, ft)
    hips = eb[P + "Hips"]
    hips.head = Wi @ Vector((wh(hips).x, wh(hips).y, cz + 0.10))
    # neck and head into the body's own (its helmet sits well above the Soldier's)
    nz = neck_height(pts, sh)
    neck, headb = eb[P + "Neck"], eb[P + "Head"]
    move(neck, Vector((wh(neck).x, wh(neck).y, nz - 0.03)))
    top_head = max(p.z for p in pts)
    move(headb, Vector((wh(headb).x, wh(headb).y, nz + 0.03)))
    headb.tail = Wi @ Vector((wh(headb).x, wh(headb).y, top_head))
    tail_to(eb[P + "Spine2"], neck); tail_to(neck, headb)
    bpy.ops.object.mode_set(mode="OBJECT")
    print(f"rig: scale {k:.3f}, shoulders {sh:.2f}, neck {nz:.2f}, crotch {cz:.2f}")
    return rig


def weights(body, rig):
    proxy = body.copy(); proxy.data = body.data.copy()
    bpy.context.scene.collection.objects.link(proxy)
    bm = bmesh.new(); bm.from_mesh(proxy.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(proxy.data); bm.free()
    select(rig, proxy)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    kd = KDTree(len(proxy.data.vertices))
    for i, v in enumerate(proxy.data.vertices):
        kd.insert(v.co, i)
    kd.balance()
    for g in proxy.vertex_groups:
        body.vertex_groups.new(name=g.name)
    names = [g.name for g in proxy.vertex_groups]
    for v in body.data.vertices:
        _, i, _ = kd.find(v.co)
        for e in proxy.data.vertices[i].groups:
            body.vertex_groups[names[e.group]].add([v.index], e.weight, "REPLACE")
    # Gloved fists and boots: the fingers and toes ride with the hand and foot (the Soldier's spread fingers and toe
    # rolls tore the mesh into spikes), then at most 4 bones a vertex, small weights dropped, normalised.
    def into(n):
        if "Hand" in n and n.split("Hand")[1]:
            return n.split("Hand")[0] + "Hand"
        if n.endswith("ToeBase") or n.endswith("Toe_End"):
            return n.replace("ToeBase", "Foot").replace("Toe_End", "Foot")
        return n
    groups = {g.index: g for g in body.vertex_groups}
    # bone heat now and then hands a far bone a patch (the sole of a boot went to a hand): a weight only counts within
    # reach of its bone
    segs = {}
    for b in rig.data.bones:
        segs[b.name] = (rig.matrix_world @ b.head_local, rig.matrix_world @ b.tail_local)

    def dist(p, n):
        a, b = segs[n]
        ab = b - a
        t = 0.0 if ab.length_squared < 1e-9 else max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
        return (p - (a + ab * t)).length
    reach = lambda n: 0.45 if ("Spine" in n or "Hips" in n) else 0.26
    for v in body.data.vertices:
        acc = {}
        for e in v.groups:
            n = into(groups[e.group].name)
            if dist(v.co, n) > reach(n):
                continue
            acc[n] = acc.get(n, 0.0) + e.weight
        if not acc:
            n = min((into(b) for b in segs), key=lambda b: dist(v.co, b))
            acc[n] = 1.0
        for e in list(v.groups):
            groups[e.group].remove([v.index])
        # where a hanging hand touches the thigh, the surface is one piece: a vertex goes with the arm or the leg,
        # never both (a running stride dragged the hand's skin down the leg)
        armw = sum(w for n, w in acc.items() if any(k in n for k in ("Arm", "Hand", "Shoulder")))
        legw = sum(w for n, w in acc.items() if any(k in n for k in ("UpLeg", "Leg", "Foot")))
        if armw and legw:
            drop = ("UpLeg", "Leg", "Foot") if armw >= legw else ("Arm", "Hand", "Shoulder")
            acc = {n: w for n, w in acc.items() if not any(k in n for k in drop)}
        top = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
        top = [(n, w) for n, w in top if w > 0.04] or top[:1]
        tot = sum(w for _, w in top)
        for n, w in top:
            body.vertex_groups[n].add([v.index], w / tot, "REPLACE")
    # ...and the faces joining a hand to a thigh go: they only stretch when the arm swings (a small hole where the
    # glove touched the trousers, out of sight)
    names_by = {g.index: g.name for g in body.vertex_groups}
    def kind(v):
        best = max(v.groups, key=lambda e: e.weight) if v.groups else None
        n = names_by[best.group] if best else ""
        return "arm" if any(k in n for k in ("Arm", "Hand", "Shoulder")) else "leg" if any(k in n for k in ("UpLeg", "Leg", "Foot")) else "body"
    kinds = [kind(v) for v in body.data.vertices]
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    cut = [f for f in bm.faces if {kinds[v.index] for v in f.verts} >= {"arm", "leg"}]
    bmesh.ops.delete(bm, geom=cut, context="FACES_ONLY")
    bm.to_mesh(body.data); bm.free()
    print(f"cut {len(cut)} faces joining hands to thighs")
    bare = sum(1 for v in body.data.vertices if not v.groups)
    bpy.data.objects.remove(proxy, do_unlink=True)
    m = body.modifiers.new("Armature", "ARMATURE"); m.object = rig
    body.parent = rig
    print(f"weights: {len(names)} bones, {bare} vertices without any")


def order(arm):
    out = []
    def walk(b):
        out.append(b.name)
        for c in b.children:
            walk(c)
    for b in arm.data.bones:
        if b.parent is None:
            walk(b)
    return out


def retarget(src, rig, action_name, ours):
    """The Soldier's motion on this rig: each bone points the way the Soldier's does; the hips move as the
    Soldier's do (scaled to this rig's hip height)."""
    act = bpy.data.actions[action_name]
    src.animation_data_create(); src.animation_data.action = act
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    new = bpy.data.actions.new(ours)
    rig.animation_data_create(); rig.animation_data.action = new
    names = order(rig)
    hk = head_w(rig, "Hips").z / head_w(src, "Hips").z
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"
    start = None
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f)
        posed = {}
        for n in names:
            sb, rb = src.pose.bones[n], rig.pose.bones[n]
            s_rest = src.data.bones[n].matrix_local
            r_rest = rig.data.bones[n].matrix_local
            # This rig's bones keep the Soldier's own bone frames (moved and turned as a whole, never re-rolled),
            # so a bone pointing where the Soldier's points is the Soldier's own turn in the armature.
            rot = sb.matrix.to_quaternion()
            if rb.parent is None:
                at = r_rest.translation + (sb.matrix.translation - s_rest.translation) * hk
            else:
                par = rig.data.bones[n].parent
                at = (posed[par.name] @ (par.matrix_local.inverted() @ r_rest)).translation
            want = Matrix.LocRotScale(at, rot, Vector((1, 1, 1)))
            posed[n] = want
            if rb.parent is None:
                basis = r_rest.inverted() @ want
            else:
                par = rig.data.bones[n].parent
                basis = (posed[par.name] @ par.matrix_local.inverted() @ r_rest).inverted() @ want
            loc, q, _ = basis.decompose()
            if rb.parent is not None:
                loc = Vector()
            rb.location = loc; rb.rotation_quaternion = q
            rb.keyframe_insert("location", frame=f - f0, group=n)
            rb.keyframe_insert("rotation_quaternion", frame=f - f0, group=n)
    src.animation_data.action = None
    print(f"motion {ours}: {f1 - f0 + 1} frames")
    return new


def death(rig):
    """Keyed by hand on this rig: shot (a jolt back), the knees give, the body folds back and drops, head last."""
    act = bpy.data.actions.new("death")
    rig.animation_data.action = act
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.matrix_basis = Matrix()
    hips_h = head_w(rig, "Hips").z

    def key(f, poses, hips=None):
        for pb in rig.pose.bones:
            pb.rotation_quaternion = Quaternion()
            pb.location = Vector()
        for n, (axis, deg) in poses.items():
            pb = rig.pose.bones[P + n]
            # turns about world axes, written into the bone's own space
            M = pb.bone.matrix_local.to_quaternion()
            pb.rotation_quaternion = M.inverted() @ Quaternion(Vector(axis), math.radians(deg)) @ M
        if hips:
            pb = rig.pose.bones[P + "Hips"]
            pb.location = pb.bone.matrix_local.to_3x3().inverted() @ Vector(hips)
        for pb in rig.pose.bones:
            pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
            pb.keyframe_insert("location", frame=f, group=pb.name)
    B = (-1, 0, 0)   # torso: a turn about -X leans it back (the body faces -Y)
    K = (1, 0, 0)    # legs: about +X, minus brings the knee forward, plus folds the shin back
    O = (0, 1, 0)    # arms out to the sides (left minus, right plus)
    key(0, {})
    key(4, {"Spine": (B, 8), "Spine1": (B, 6), "Head": (B, 14), "LeftArm": (O, -10), "RightArm": (O, 10)}, (0, 0.04, 0))
    key(12, {"Spine": (B, -6), "Head": (B, -12), "LeftUpLeg": (K, -35), "RightUpLeg": (K, -28), "LeftLeg": (K, 60), "RightLeg": (K, 50),
             "LeftArm": (O, -20), "RightArm": (O, 22)}, (0, 0.05, -hips_h * 0.25))
    key(22, {"Hips": (B, 35), "Spine": (B, 10), "Head": (B, -15), "LeftUpLeg": (K, -55), "RightUpLeg": (K, -45), "LeftLeg": (K, 70),
             "RightLeg": (K, 62), "LeftArm": (O, -45), "RightArm": (O, 40)}, (0, 0.25, -hips_h * 0.60))
    key(30, {"Hips": (B, 85), "Spine": (B, 5), "Head": (B, 15), "LeftUpLeg": (K, -25), "RightUpLeg": (K, -18), "LeftLeg": (K, 35),
             "RightLeg": (K, 22), "LeftArm": (O, -70), "RightArm": (O, 62)}, (0, 0.55, -hips_h * 0.86))
    key(34, {"Hips": (B, 88), "Spine": (B, 2), "Head": (B, 22), "LeftUpLeg": (K, -18), "RightUpLeg": (K, -12), "LeftLeg": (K, 25),
             "RightLeg": (K, 15), "LeftArm": (O, -76), "RightArm": (O, 66)}, (0, 0.58, -hips_h * 0.88))
    key(45, {"Hips": (B, 90), "Head": (B, 10), "LeftUpLeg": (K, -15), "RightUpLeg": (K, -8), "LeftLeg": (K, 22), "RightLeg": (K, 12),
             "LeftArm": (O, -78), "RightArm": (O, 68)}, (0, 0.58, -hips_h * 0.89))
    print("motion death: 46 frames")
    return act


def write(name, body, rig, motions, albedo, outdir):
    from PIL import Image
    outdir.mkdir(parents=True, exist_ok=True)
    img = None
    for slot in body.material_slots:
        for node in slot.material.node_tree.nodes if slot.material and slot.material.use_nodes else []:
            if node.type == "TEX_IMAGE" and node.image:
                img = node.image
    if albedo:
        Image.open(albedo).convert("RGB").save(outdir / f"{name}_basecolor.png")
    elif img:
        tmp = outdir / "_albedo.png"; img.filepath_raw = str(tmp); img.file_format = "PNG"; img.save()
        Image.open(tmp).convert("RGB").save(outdir / f"{name}_basecolor.png"); tmp.unlink()
    for slot in body.material_slots:
        slot.material.name = name
    mat = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
           "materialTypeVersion": 5,
           "propertyValues": {"baseColor.textureMap": f"{name}_basecolor.png", "baseColor.factor": 1.0,
                              "roughness.factor": 0.78, "metallic.factor": 0.05}}
    (outdir / f"{name}.material").write_text(json.dumps(mat, indent=4) + "\n")
    sc = bpy.context.scene; sc.render.fps = FPS
    rig.animation_data.action = None
    rest(rig)

    def export(out, with_mesh, act=None):
        if act:
            rig.animation_data.action = act
            sc.frame_start, sc.frame_end = int(act.frame_range[0]), int(act.frame_range[1])
        select(rig, body) if with_mesh else select(rig)
        bpy.ops.export_scene.fbx(filepath=str(out), use_selection=True,
                                 object_types={"ARMATURE", "MESH"} if with_mesh else {"ARMATURE"},
                                 add_leaf_bones=False, bake_anim=not with_mesh, bake_anim_use_all_actions=False,
                                 bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True,
                                 path_mode="STRIP", embed_textures=False, axis_forward="-Z", axis_up="Y")
    export(outdir / f"{name}.fbx", True)
    for ours, act in motions.items():
        export(outdir / f"{name}_{ours}.fbx", False, act)
    print(f"{name}: {len(body.data.polygons)} faces, {len(motions)} motions -> {outdir.relative_to(ROOT)}")


def preview(rig, motions, out):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"; sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 500, 760
    cam = bpy.data.cameras.new("c"); cam.type = "ORTHO"; cam.ortho_scale = 2.3
    co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co); sc.camera = co
    shots = [(None, 0, 35), ("idle", 15, 35), ("walk", 0, 90), ("walk", 8, 35), ("walk", 12, 0), ("run", 0, 90), ("run", 6, 35),
             ("death", 12, 90), ("death", 40, 60)]
    from PIL import Image
    sheet = Image.new("RGB", (500 * len(shots), 760))
    for i, (m, f, deg) in enumerate(shots):
        a = math.radians(deg)
        co.location = (2.6 * math.sin(a), -2.6 * math.cos(a), 0.95); co.rotation_euler = (math.radians(90), 0, a)
        rig.animation_data.action = motions[m] if m else None
        if not m:
            rest(rig)
        sc.frame_set(f)
        p = f"/tmp/fa_{i}.png"; sc.render.filepath = p; bpy.ops.render.render(write_still=True)
        sheet.paste(Image.open(p).convert("RGB"), (i * 500, 0))
    sheet.save(out)


def death_strip(rig, act, out):
    from PIL import Image
    sc = bpy.context.scene
    rig.animation_data.action = act
    a = math.radians(80)
    sc.camera.location = (2.8 * math.sin(a), -2.8 * math.cos(a), 0.8); sc.camera.rotation_euler = (math.radians(90), 0, a)
    sc.camera.data.ortho_scale = 2.6
    frames = [0, 4, 8, 12, 17, 22, 27, 32, 45]
    sheet = Image.new("RGB", (500 * len(frames), 760))
    for i, f in enumerate(frames):
        sc.frame_set(f); p = f"/tmp/fd_{i}.png"; sc.render.filepath = p; bpy.ops.render.render(write_still=True)
        sheet.paste(Image.open(p).convert("RGB"), (i * 500, 0))
    sheet.save(out)


def main():
    argv = sys.argv[1:]
    prev = argv[argv.index("--preview") + 1] if "--preview" in argv else None
    albedo = argv[argv.index("--albedo") + 1] if "--albedo" in argv else None
    src_glb, body_glb, name = argv[:3]
    src = soldier(src_glb)
    body = load_body(body_glb)
    rig = build_rest(src, body)
    weights(body, rig)
    motions = {ours: retarget(src, rig, theirs, ours) for ours, theirs in MOTIONS.items()}
    motions["death"] = death(rig)
    if prev:
        preview(rig, motions, prev)
        if "--death-strip" in argv:
            death_strip(rig, motions["death"], argv[argv.index("--death-strip") + 1])
    write(name, body, rig, motions, albedo, ROOT / "Assets" / "Models" / "Agents" / name)
    if "--glb" in argv:
        # the rigged body as a Soldier.glb look-alike, for Tools/Agents/make_arms.py (first-person arms from this body)
        rest(rig); rig.animation_data.action = None
        body.name = "vanguard_Mesh"
        visor = bpy.data.objects.new("vanguard_visor", bpy.data.meshes.new("visor")); bpy.context.scene.collection.objects.link(visor)
        visor.parent = rig
        # make_arms.py works in the Soldier's own facing (+Y): turned back for it
        body.modifiers.clear()
        select(body); bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
        for o in (rig, body):
            o.rotation_euler = (0, 0, math.pi)
        select(rig, body); bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        body.parent = rig
        m = body.modifiers.new("Armature", "ARMATURE"); m.object = rig
        select(rig, body, visor)
        bpy.ops.export_scene.gltf(filepath=argv[argv.index("--glb") + 1], export_format="GLB", use_selection=True, export_animations=False)


if __name__ == "__main__":
    main()
