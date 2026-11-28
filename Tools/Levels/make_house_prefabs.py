#!/usr/bin/env python3
"""Writes Prefabs/Suburb/<House>.prefab for Level 9's houses (Tools/Levels/make_houses.py): one child entity per part
(siding, drywall, floors, roof, trim, glass, doors), each a mesh wearing its shared material from
Assets/Materials/Suburb. No colliders: the walls are solid through the layout (SuburbHouses.h), like every level.

  python3 Tools/Levels/make_house_prefabs.py ASSETDB   (Cache/assetdb.sqlite from the PC, after it processed the models)
"""
import json
import random
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "Monsters"))
from make_monster_prefab import _guid  # noqa: E402

SIDING = {"HouseRanchA": "white", "HouseRanchB": "cream", "HouseGarageA": "blue", "HouseGarageB": "sage"}
MATERIAL = {"drywall": "drywall", "floor_wood": "wood_floor", "floor_carpet": "carpet", "floor_tile": "tile",
            "roof": "roof", "concrete": "concrete", "trim": "trim", "glass": "glass", "front_door": "front_door",
            "garage_door": "garage_door"}
BASE = ("EditorEntitySortComponent", "EditorInspectorComponent", "EditorLockComponent", "EditorOnlyEntityComponent",
        "EditorPendingCompositionComponent", "EditorVisibilityComponent", "EditorDisabledCompositionComponent",
        "EditorEntityIconComponent")


def products(db):
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    return [(p.split("/", 1)[-1], s & 0xFFFFFFFF) for (p, s) in con.execute("SELECT ProductName, SubID FROM Products")]


def model(prods, source):
    """The .azmodel made from this .fbx (lower-case path), and its sub id."""
    folder, stem = source.rsplit("/", 1)
    stem = stem[:-4]
    for path, sub in prods:
        if path.startswith(folder + "/") and path.endswith(".fbx.azmodel") and stem in path.rsplit("/", 1)[-1]:
            return path, sub
    raise SystemExit(f"{source}: not processed yet")


def main():
    prods = products(sys.argv[1])
    for name, paint in SIDING.items():
        rnd = random.Random(name + "-house")
        nid = lambda: rnd.getrandbits(63)
        container = {t: {"$type": t, "Id": nid()} for t in BASE + ("EditorPrefabComponent",)}
        container["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                           "Parent Entity": ""}
        entities = {}
        folder = ROOT / "Assets" / "Models" / "Suburb" / name
        for fbx in sorted(folder.glob("*.fbx")):
            part = fbx.stem[len(name) + 1:]
            source = f"assets/models/suburb/{name.lower()}/{fbx.name.lower()}"
            hint, sub = model(prods, source)
            mat = f"house_siding_{paint}" if part == "siding" else f"house_{MATERIAL[part]}"
            eid = f"Entity_[{nid() & 0xFFFFFFFFFFF}]"
            comps = {t: {"$type": t, "Id": nid()} for t in BASE}
            comps["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                           "Parent Entity": "ContainerEntity"}
            comps["AZ::Render::EditorMeshComponent"] = {
                "$type": "AZ::Render::EditorMeshComponent", "Id": nid(),
                "Controller": {"Configuration": {"ModelAsset": {
                    "assetId": {"guid": _guid(source), "subId": sub}, "assetHint": hint}}}}
            comps["EditorMaterialComponent"] = {
                "$type": "EditorMaterialComponent", "Id": nid(),
                "Controller": {"Configuration": {"materials": {"{}": {"MaterialAsset": {
                    "assetId": {"guid": _guid(f"assets/materials/suburb/{mat}.material")},
                    "assetHint": f"assets/materials/suburb/{mat}.azmaterial"}}}}}}
            entities[eid] = {"Id": eid, "Name": part, "Components": comps}
        prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": container}, "Entities": entities}
        out = ROOT / "Prefabs" / "Suburb" / f"{name}.prefab"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(prefab, indent=4) + "\n")
        print(f"wrote {out.relative_to(ROOT)} ({len(entities)} parts)")


if __name__ == "__main__":
    main()
