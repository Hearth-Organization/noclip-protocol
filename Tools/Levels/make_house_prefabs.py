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


def props(prods):
    """The street and yard props (Tools/Levels/make_suburb_props.py, props.json): a child entity per part."""
    manifest = json.loads((ROOT / "Assets" / "Models" / "Suburb" / "Props" / "props.json").read_text())
    for name, info in manifest.items():
        rnd = random.Random(name + "-prop")
        nid = lambda: rnd.getrandbits(63)
        container = {t: {"$type": t, "Id": nid()} for t in BASE + ("EditorPrefabComponent",)}
        container["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                           "Parent Entity": ""}
        entities = {}
        for part in info["parts"]:
            source = f"assets/models/suburb/props/{name.lower()}/{name.lower()}_{part}.fbx"
            hint, sub = model(prods, source)
            eid = f"Entity_[{nid() & 0xFFFFFFFFFFF}]"
            comps = {t: {"$type": t, "Id": nid()} for t in BASE}
            comps["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                           "Parent Entity": "ContainerEntity"}
            if info["stretch"]:
                comps["EditorNonUniformScaleComponent"] = {"$type": "EditorNonUniformScaleComponent", "Id": nid()}
            config = {"ModelAsset": {"assetId": {"guid": _guid(source), "subId": sub}, "assetHint": hint}}
            if part.startswith("leaves") or part in ("leaf_litter", "wire", "chainlink"):
                config["IsRayTracingEnabled"] = False   # thin, many and alpha-tested: not worth tracing
            comps["AZ::Render::EditorMeshComponent"] = {"$type": "AZ::Render::EditorMeshComponent", "Id": nid(),
                                                        "Controller": {"Configuration": config}}
            comps["EditorMaterialComponent"] = {
                "$type": "EditorMaterialComponent", "Id": nid(),
                "Controller": {"Configuration": {"materials": {"{}": {"MaterialAsset": {
                    "assetId": {"guid": _guid(f"assets/materials/suburb/prop_{part}.material")},
                    "assetHint": f"assets/materials/suburb/prop_{part}.azmaterial"}}}}}}
            entities[eid] = {"Id": eid, "Name": part, "Components": comps}
        prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": container}, "Entities": entities}
        out = ROOT / "Prefabs" / "Suburb" / "Props" / f"{name}.prefab"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(prefab, indent=4) + "\n")
        print(f"wrote {out.relative_to(ROOT)} ({len(entities)} parts)")


def main():
    prods = products(sys.argv[1])
    props(prods)
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
