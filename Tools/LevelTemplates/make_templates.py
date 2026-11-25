#!/usr/bin/env python3
"""Makes the project's level templates for Hearth Engine's New level dialog (Templates/Levels/):

  backrooms_level.prefab  - Level 0's setup: the endless generator, the player and the room tone.
  foundation_site.prefab  - an indoor facility starter: a concrete room (floor, walls, ceiling with colliders),
                            two ceiling lights, the player.
  templates.json          - titles and one-line descriptions the dialog shows.

Run from the project folder: python3 Tools/LevelTemplates/make_templates.py
"""
import copy
import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEVEL0 = ROOT / "Levels" / "Level0" / "Level0.prefab"
OUT = ROOT / "Templates" / "Levels"
TRANSFORM = "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent"
BASICS = ["EditorDisabledCompositionComponent", "EditorEntityIconComponent", "EditorEntitySortComponent", "EditorInspectorComponent",
          "EditorLockComponent", "EditorOnlyEntityComponent", "EditorPendingCompositionComponent", "EditorVisibilityComponent"]
rng = random.Random(1166)


def new_id():
    return rng.randrange(10**15, 2**53)


def entity_id():
    return f"Entity_[{rng.randrange(10**11, 10**13)}]"


def basics():
    return {name: {"$type": name, "Id": new_id()} for name in BASICS}


def subtree(entities, root_id):
    keep = {root_id}
    changed = True
    while changed:
        changed = False
        for eid, entity in entities.items():
            parent = entity["Components"].get("TransformComponent", {}).get("Parent Entity")
            if parent in keep and eid not in keep:
                keep.add(eid)
                changed = True
    return keep


def box(name, parent, position, size):
    eid = entity_id()
    components = basics()
    components["AZ::Render::EditorMeshComponent"] = {
        "$type": "AZ::Render::EditorMeshComponent", "Id": new_id(),
        "Controller": {"Configuration": {"ModelAsset": {
            "assetId": {"guid": "{00000000-0000-0000-0000-000000000000}", "subId": 0},
            "assetHint": "objects/_primitives/_box_1x1.fbx.azmodel"}}}}
    components["EditorNonUniformScaleComponent"] = {"$type": "EditorNonUniformScaleComponent", "Id": new_id(), "NonUniformScale": list(size)}
    components["EditorColliderComponent"] = {
        "$type": "EditorColliderComponent", "Id": new_id(),
        "ColliderConfiguration": {"MaterialSlots": {"Slots": [{"Name": "Entire object"}]}},
        "ShapeConfiguration": {"ShapeType": 1, "HasNonUniformScale": True}, "HasNonUniformScale": True}
    components["EditorStaticRigidBodyComponent"] = {"$type": "EditorStaticRigidBodyComponent", "Id": new_id()}
    components["TransformComponent"] = {"$type": TRANSFORM, "Id": new_id(), "Parent Entity": parent,
                                        "Transform Data": {"Translate": list(position)}}
    return eid, {"Id": eid, "Name": name, "Components": components}


def light(name, parent, position):
    eid = entity_id()
    components = basics()
    components["AZ::Render::EditorAreaLightComponent"] = {
        "$type": "AZ::Render::EditorAreaLightComponent", "Id": new_id(),
        "Controller": {"Configuration": {"LightType": 6, "Color": [0.92, 0.96, 1.0], "Intensity": 220.0,
                                         "AttenuationRadiusMode": 0, "AttenuationRadius": 9.0}}}
    components["TransformComponent"] = {"$type": TRANSFORM, "Id": new_id(), "Parent Entity": parent,
                                        "Transform Data": {"Translate": list(position)}}
    return eid, {"Id": eid, "Name": name, "Components": components}


def set_children(container_or_entity, children):
    for key, component in container_or_entity["Components"].items():
        if component.get("$type") == "EditorEntitySortComponent":
            component["Child Entity Order"] = children


def write(path, data):
    path.write_text(json.dumps(data, indent=4) + "\n")


def main():
    level = json.loads(LEVEL0.read_text())
    OUT.mkdir(parents=True, exist_ok=True)

    # Backrooms level: Level 0 as it is (generator, player, sound, environment).
    backrooms = copy.deepcopy(level)
    backrooms["ContainerEntity"]["Name"] = "BackroomsLevel"
    write(OUT / "backrooms_level.prefab", backrooms)

    # Foundation site: the environment and the player from Level 0, in a lit concrete room.
    entities = level["Entities"]
    container_id = level["ContainerEntity"]["Id"]
    keep = set()
    for eid, entity in entities.items():
        if entity["Name"] in ("Atom Default Environment", "Player"):
            keep |= subtree(entities, eid)
    site = {"ContainerEntity": copy.deepcopy(level["ContainerEntity"]), "Entities": {eid: copy.deepcopy(entities[eid]) for eid in keep}}
    site["ContainerEntity"]["Name"] = "FoundationSite"
    top = [eid for eid in keep if entities[eid]["Components"].get("TransformComponent", {}).get("Parent Entity") in (container_id, None)]
    room_parts = [("Floor", (0, 0, -0.1), (12, 12, 0.2)), ("Ceiling", (0, 0, 3.6), (12, 12, 0.2)),
                  ("WallNorth", (0, 6.1, 1.75), (12.4, 0.2, 3.5)), ("WallSouth", (0, -6.1, 1.75), (12.4, 0.2, 3.5)),
                  ("WallEast", (6.1, 0, 1.75), (0.2, 12, 3.5)), ("WallWest", (-6.1, 0, 1.75), (0.2, 12, 3.5))]
    for name, position, size in room_parts:
        eid, entity = box(name, container_id, position, size)
        site["Entities"][eid] = entity
        top.append(eid)
    for name, position in (("CeilingLight1", (-3, 0, 3.4)), ("CeilingLight2", (3, 0, 3.4))):
        eid, entity = light(name, container_id, position)
        site["Entities"][eid] = entity
        top.append(eid)
    set_children(site["ContainerEntity"], top)
    write(OUT / "foundation_site.prefab", site)

    write(OUT / "templates.json", {"templates": [
        {"file": "backrooms_level.prefab", "title": "Backrooms level", "about": "Endless generator, player, sound"},
        {"file": "foundation_site.prefab", "title": "Foundation site", "about": "Indoor facility kit"},
    ]})
    print("made", sorted(p.name for p in OUT.iterdir()))


if __name__ == "__main__":
    main()
