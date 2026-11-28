#!/usr/bin/env python3
"""Writes Prefabs/Foliage/GrassPatch{Short,Long,Dry}.prefab: one grass patch (Tools/Levels/make_grass.py) as a mesh and
its material, with no collider, so you walk through it. EndlessLevelComponent spawns them on Level 9's lawns
(Level0Layout BuildSuburb, kinds 5-7).

  python3 Tools/Levels/make_grass_prefabs.py ASSETDB   (Cache/assetdb.sqlite from the PC, after it processed the models)
"""
import json
import random
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "Monsters"))
from make_monster_prefab import _guid  # noqa: E402

NAMES = ("GrassPatchShort", "GrassPatchLong", "GrassPatchDry")


def model_product(db, folder, name):
    """(product path, sub id) of the model: Blender's export names it default_<name>_<uuid>_.fbx.azmodel."""
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    for (product, sub) in con.execute("SELECT ProductName, SubID FROM Products"):
        path = product.split("/", 1)[-1]
        if path.startswith(folder + "/") and path.endswith(".fbx.azmodel"):
            return path, sub & 0xFFFFFFFF
    raise SystemExit(f"{folder}: model not processed yet")


def main():
    db = sys.argv[1]
    for name in NAMES:
        folder = f"assets/models/foliage/{name.lower()}"
        rnd = random.Random(name + "-foliage")
        nid = lambda: rnd.getrandbits(63)
        comps = {t: {"$type": t, "Id": nid()} for t in (
            "EditorEntitySortComponent", "EditorInspectorComponent", "EditorLockComponent", "EditorOnlyEntityComponent",
            "EditorPendingCompositionComponent", "EditorPrefabComponent", "EditorVisibilityComponent",
            "EditorDisabledCompositionComponent", "EditorEntityIconComponent")}
        comps["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                       "Parent Entity": ""}
        hint, sub = model_product(db, folder, name)
        comps["AZ::Render::EditorMeshComponent"] = {
            "$type": "AZ::Render::EditorMeshComponent", "Id": nid(),
            "Controller": {"Configuration": {"IsRayTracingEnabled": False, "ModelAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.fbx"), "subId": sub},
                "assetHint": hint}}}}
        comps["EditorMaterialComponent"] = {
            "$type": "EditorMaterialComponent", "Id": nid(),
            "Controller": {"Configuration": {"materials": {"{}": {"MaterialAsset": {
                "assetId": {"guid": _guid(f"{folder}/{name.lower()}.material")},
                "assetHint": f"{folder}/{name.lower()}.azmaterial"}}}}}}
        prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": comps}, "Entities": {}}
        out = ROOT / "Prefabs" / "Foliage" / f"{name}.prefab"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(prefab, indent=4) + "\n")
        print(f"wrote {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
