#!/usr/bin/env python3
"""Writes Prefabs/Monsters/<Name>.prefab: one entity with the monster's Actor (its Tripo3D model), a Simple Motion
component (idle to start; the Hearth Monster switches motions as it moves), its material, a capsule collider bullets
hit, and the Hearth Monster itself (the spawner fills in its name and kind).

  python3 Tools/Monsters/make_monster_prefab.py NAME ACTOR_SUBID IDLE_SUBID HEIGHT_M RADIUS_M
  python3 Tools/Monsters/make_monster_prefab.py NAME --mesh MODEL_SUBID HEIGHT_M RADIUS_M     (no skeleton: a plain mesh)

The actor's and the idle motion's product sub ids come from the Asset Processor's database once it has processed
Assets/Models/Monsters/NAME/NAME.fbx and NAME_idle.fbx (the source guid is the name UUID of the source path).
"""
import json
import random
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _guid(rel):
    import hashlib
    # AZ::Uuid::CreateName(path): SHA-1 of the lowercased path, as a version-5 UUID without a namespace.
    h = bytearray(hashlib.sha1(rel.lower().encode()).digest()[:16])
    h[6] = (h[6] & 0x0F) | 0x50
    h[8] = (h[8] & 0x3F) | 0x80
    return "{" + str(uuid.UUID(bytes=bytes(h))).upper() + "}"


def main():
    name = sys.argv[1]
    mesh = sys.argv[2] == "--mesh"
    # IDLE_SUBID may be "walk:SUBID" for a four-legged rig that only has a walk.
    first = "idle"
    if not mesh and ":" in sys.argv[3]:
        first, sys.argv[3] = sys.argv[3].split(":")
    actor_sub, idle_sub = (0, 0) if mesh else (int(sys.argv[2]), int(sys.argv[3]))
    model_sub = int(sys.argv[3]) if mesh else 0
    height, radius = float(sys.argv[4]), float(sys.argv[5])
    folder = f"assets/models/monsters/{name.lower()}"
    rnd = random.Random(name + "-monster")
    nid = lambda: rnd.getrandbits(63)
    comps = {
        "EditorEntitySortComponent": {"$type": "EditorEntitySortComponent", "Id": nid()},
        "EditorInspectorComponent": {"$type": "EditorInspectorComponent", "Id": nid()},
        "EditorLockComponent": {"$type": "EditorLockComponent", "Id": nid()},
        "EditorOnlyEntityComponent": {"$type": "EditorOnlyEntityComponent", "Id": nid()},
        "EditorPendingCompositionComponent": {"$type": "EditorPendingCompositionComponent", "Id": nid()},
        "EditorPrefabComponent": {"$type": "EditorPrefabComponent", "Id": nid()},
        "EditorVisibilityComponent": {"$type": "EditorVisibilityComponent", "Id": nid()},
        "EditorDisabledCompositionComponent": {"$type": "EditorDisabledCompositionComponent", "Id": nid()},
        "EditorEntityIconComponent": {"$type": "EditorEntityIconComponent", "Id": nid()},
        "TransformComponent": {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(), "Parent Entity": ""},
        "EditorActorComponent": {
            "$type": "EditorActorComponent", "Id": nid(),
            "ActorAsset": {"assetId": {"guid": _guid(f"{folder}/{name.lower()}.fbx"), "subId": actor_sub},
                           "assetHint": f"{folder}/{name.lower()}.actor"},
            "UpdateJointTransformsWhenOutOfView": False,
        },
        "EditorSimpleMotionComponent": {
            "$type": "EditorSimpleMotionComponent", "Id": nid(),
            "Configuration": {
                "MotionAsset": {"assetId": {"guid": _guid(f"{folder}/{name.lower()}_{first}.fbx"), "subId": idle_sub},
                                "assetHint": f"{folder}/{name.lower()}_{first}.motion"},
                "Loop": True, "PlayOnActivation": True, "InPlace": True, "BlendIn": 0.25, "BlendOut": 0.25,
            },
        },
        "EditorMaterialComponent": {
            "$type": "EditorMaterialComponent", "Id": nid(),
            "Controller": {"Configuration": {"materials": {"{}": {"MaterialAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.material")},
                "assetHint": f"{folder}/{name.lower()}.azmaterial"}}}}},
        },
        "EditorColliderComponent": {
            "$type": "EditorColliderComponent", "Id": nid(),
            "ColliderConfiguration": {"Position": [0.0, 0.0, height / 2], "MaterialSlots": {"Slots": [{"Name": "Entire object"}]}},
            "ShapeConfiguration": {"ShapeType": 2, "Capsule": {"Height": height, "Radius": radius}},
        },
        "EditorStaticRigidBodyComponent": {"$type": "EditorStaticRigidBodyComponent", "Id": nid()},
        # Its voice (Tools/Monsters/make_sounds.py): heard in 3D from where it is; the game picks the sound.
        "EditorMiniAudioPlaybackComponent": {"$type": "EditorMiniAudioPlaybackComponent", "Id": nid(),
                                             "Controller": {"Config": {"Autoplay": False, "Volume": 100.0, "Loop": False,
                                                                       "Spatialization": True, "Min Distance": 2.0,
                                                                       "Max Distance": 32.0, "Auto-follow": True}}},
        "HearthMonster": {"$type": "GenericComponentWrapper", "Id": nid(),
                          "m_template": {"$type": "HearthMonsterComponent", "Name": name, "Kind": name,
                                         "Motions": f"{folder}/{name.lower()}"}},
    }
    if mesh:
        del comps["EditorActorComponent"]
        del comps["EditorSimpleMotionComponent"]
        comps["AZ::Render::EditorMeshComponent"] = {
            "$type": "AZ::Render::EditorMeshComponent", "Id": nid(),
            "Controller": {"Configuration": {"ModelAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.fbx"), "subId": model_sub},
                "assetHint": f"{folder}/{name.lower()}.fbx.azmodel"}}}}
        comps["HearthMonster"]["m_template"]["Motions"] = ""
    prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": comps}, "Entities": {}}
    out = ROOT / "Prefabs" / "Monsters" / f"{name}.prefab"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(prefab, indent=4) + "\n")
    print(f"wrote {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
