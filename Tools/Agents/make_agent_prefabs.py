#!/usr/bin/env python3
"""Puts the agent's temporary body and arms into the game (after Tools/Agents/make_agent.py and make_arms.py, once the
Asset Processor has made them).

    python3 Tools/Agents/make_agent_prefabs.py IDS_JSON

IDS_JSON: Tools/Assets/asset_ids.py's output for Assets/Models (product path -> guid and sub id).
- Prefabs/Agents/Agent_Male.prefab and Agent_Female.prefab: the body walking (the next agent through the breach;
  one body for both until our own models).
- Prefabs/Weapons/View_<Weapon>.prefab: adds (or replaces) a View_<Weapon>_Arms entity, the arms posed on that
  weapon in its own space, with the agent's material. Run again after Tools/Weapons/make_weapons.py rewrites them.
"""
import copy
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools" / "Monsters"))
from make_monster_prefab import _guid  # noqa: E402

AGENT = "assets/models/agents/agent"
WEAPONS = ("Pistol", "Shotgun", "Carbine", "FlareGun", "Knife", "Shield")


def body(ids, name, motion="walk"):
    rnd = random.Random(name + "-agent")
    nid = lambda: rnd.getrandbits(63)
    actor = ids[f"{AGENT}/agent.actor"]
    walk = ids[f"{AGENT}/agent_{motion}.motion"]
    plain = ["EditorEntitySortComponent", "EditorInspectorComponent", "EditorLockComponent", "EditorOnlyEntityComponent",
             "EditorPendingCompositionComponent", "EditorPrefabComponent", "EditorVisibilityComponent",
             "EditorDisabledCompositionComponent", "EditorEntityIconComponent"]
    comps = {k: {"$type": k, "Id": nid()} for k in plain}
    comps["TransformComponent"] = {"$type": "{27F1E1A1-8D9D-4C3B-BD3A-AFB9762449C0} TransformComponent", "Id": nid(),
                                   "Parent Entity": ""}
    comps["EditorActorComponent"] = {"$type": "EditorActorComponent", "Id": nid(),
                                     "ActorAsset": {"assetId": {"guid": actor["guid"], "subId": actor["subId"]},
                                                    "assetHint": f"{AGENT}/agent.actor"},
                                     "UpdateJointTransformsWhenOutOfView": False}
    comps["EditorSimpleMotionComponent"] = {"$type": "EditorSimpleMotionComponent", "Id": nid(), "Configuration": {
        "MotionAsset": {"assetId": {"guid": walk["guid"], "subId": walk["subId"]}, "assetHint": f"{AGENT}/agent_{motion}.motion"},
        "Loop": True, "PlayOnActivation": True, "InPlace": True, "BlendIn": 0.25, "BlendOut": 0.25}}
    comps["EditorMaterialComponent"] = {"$type": "EditorMaterialComponent", "Id": nid(), "Controller": {"Configuration": {
        "materials": {"{}": {"MaterialAsset": {"assetId": {"guid": _guid("Assets/Models/Agents/Agent/Agent.material")},
                                               "assetHint": f"{AGENT}/agent.azmaterial"}}}}}}
    prefab = {"ContainerEntity": {"Id": "ContainerEntity", "Name": name, "Components": comps}, "Entities": {}}
    out = ROOT / "Prefabs" / "Agents" / f"{name}.prefab"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(prefab, indent=4) + "\n")
    print("wrote", out.relative_to(ROOT))


def arms(ids, weapon):
    path = ROOT / "Prefabs" / "Weapons" / f"View_{weapon}.prefab"
    data = json.loads(path.read_text())
    key = f"assets/models/weapons/{weapon.lower()}/{weapon.lower()}_arms.fbx.azmodel"
    if key not in ids:
        print("not processed yet:", key)
        return
    model = next(e for e in data["Entities"].values() if e["Name"] == f"View_{weapon}_Model" or e["Name"].startswith(f"View_{weapon}"))
    for k in [k for k, e in data["Entities"].items() if e["Name"] == f"View_{weapon}_Arms"]:
        del data["Entities"][k]
    order = data["ContainerEntity"]["Components"]["EditorEntitySortComponent"].setdefault("Child Entity Order", [])
    rnd = random.Random(f"View_{weapon}-arms")
    e = copy.deepcopy(model)
    eid = f"Entity_[{rnd.getrandbits(40)}]"
    e["Id"], e["Name"] = eid, f"View_{weapon}_Arms"
    for comp in e["Components"].values():
        comp["Id"] = rnd.getrandbits(63)
    e["Components"]["AZ::Render::EditorMeshComponent"]["Controller"]["Configuration"]["ModelAsset"] = {
        "assetId": {"guid": ids[key]["guid"], "subId": ids[key]["subId"]}, "assetHint": key}
    e["Components"]["EditorMaterialComponent"]["Controller"]["Configuration"]["materials"] = {
        "{}": {"MaterialAsset": {"assetId": {"guid": _guid("Assets/Models/Agents/Agent/Agent.material")},
                                 "assetHint": f"{AGENT}/agent.azmaterial"}}}
    e["Components"]["TransformComponent"]["Transform Data"] = {"Translate": [0.0, 0.0, 0.0]}
    data["Entities"][eid] = e
    order[:] = [o for o in order if o in data["Entities"]] + ([eid] if eid not in order else [])
    path.write_text(json.dumps(data, indent=4) + "\n")
    print("arms on", path.relative_to(ROOT))


def main():
    ids = json.loads(Path(sys.argv[1]).read_text())
    body(ids, "Agent_Male")
    body(ids, "Agent_Female")
    body(ids, "Agent_Run", "run")   # the trailer's agent
    for weapon in WEAPONS:
        arms(ids, weapon)


if __name__ == "__main__":
    main()
