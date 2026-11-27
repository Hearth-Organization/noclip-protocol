#!/usr/bin/env python3
"""Writes Prefabs/Landmarks/<Name>.prefab for the outdoor levels' landmarks (Tripo3D models made with
Tools/Monsters/make_static.py into Assets/Models/Landmarks): a mesh, its material and a capsule collider.

  python3 Tools/Levels/make_landmark_prefabs.py ASSETDB   (Cache/assetdb.sqlite from the PC, after processing)
"""
import json
import random
import sqlite3
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "Monsters"))
from make_monster_prefab import _guid  # noqa: E402

# name: (collider height, radius): a trunk, the house's body, a lamp post
LANDMARKS = {"Tree": (9.0, 0.45), "House": (5.5, 2.8), "Streetlamp": (5.0, 0.15)}


def model_sub(db, name):
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    want = f"assets/models/landmarks/{name.lower()}/{name.lower()}.fbx.azmodel"
    for (product, sub) in con.execute("SELECT ProductName, SubID FROM Products"):
        if product.split("/", 1)[-1] == want:
            return sub & 0xFFFFFFFF
    raise SystemExit(f"{want} not processed yet")


def main():
    db = sys.argv[1]
    for name, (height, radius) in LANDMARKS.items():
        folder = f"assets/models/landmarks/{name.lower()}"
        rnd = random.Random(name + "-landmark")
        nid = lambda: rnd.getrandbits(63)
        comps = {t: {"$type": t, "Id": nid()} for t in (
            "EditorEntitySortComponent", "EditorInspectorComponent", "EditorLockComponent", "EditorOnlyEntityComponent",
            "EditorPendingCompositionComponent", "EditorPrefabComponent", "EditorVisibilityComponent",
            "EditorDisabledCompositionComponent", "EditorEntityIconComponent", "EditorStaticRigidBodyComponent")}
        comps["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(), "Parent Entity": ""}
        comps["AZ::Render::EditorMeshComponent"] = {
            "$type": "AZ::Render::EditorMeshComponent", "Id": nid(),
            "Controller": {"Configuration": {"ModelAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.fbx"), "subId": model_sub(db, name)},
                "assetHint": f"{folder}/{name.lower()}.fbx.azmodel"}}}}
        comps["EditorMaterialComponent"] = {
            "$type": "EditorMaterialComponent", "Id": nid(),
            "Controller": {"Configuration": {"materials": {"{}": {"MaterialAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.material")}, "assetHint": f"{folder}/{name.lower()}.azmaterial"}}}}}}
        comps["EditorColliderComponent"] = {
            "$type": "EditorColliderComponent", "Id": nid(),
            "ColliderConfiguration": {"Position": [0.0, 0.0, height / 2], "MaterialSlots": {"Slots": [{"Name": "Entire object"}]}},
            "ShapeConfiguration": {"ShapeType": 2, "Capsule": {"Height": height, "Radius": radius}}}
        prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": comps}, "Entities": {}}
        out = ROOT / "Prefabs" / "Landmarks" / f"{name}.prefab"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(prefab, indent=4) + "\n")
        print(f"wrote {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
