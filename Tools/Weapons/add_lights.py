#!/usr/bin/env python3
"""Puts the flashlight on every weapon's view (Tools/Weapons/lights.py): its primitive parts go into
Prefabs/Weapons/View_<Weapon>.prefab as View_<Weapon>_Light* entities (replacing any from before), and the lens
material into Assets/Materials/Weapons. Run after Tools/Weapons/make_weapons.py (which rewrites the views).

    python3 Tools/Weapons/add_lights.py
"""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lights  # noqa: E402
import make_weapons as mw  # noqa: E402

mc = mw.mc


def main():
    mw.write_materials()
    with open(mc.path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    tmp = tempfile.mkdtemp(dir=mc.path("Prefabs"))
    rel = os.path.relpath(tmp, mc.path(""))
    try:
        for weapon in lights.LIGHTS:
            parts = lights.parts(weapon, mw.B, mw.C)
            mc.build_prefab(f"Light_{weapon}", {"parts": parts, "size": mc.bounds(parts), "collide": False}, template,
                            folder=rel, mat_guid=mw.mat_guid)
            with open(os.path.join(tmp, f"Light_{weapon}.prefab")) as f:
                light = json.load(f)
            view_path = mc.path("Prefabs/Weapons", f"View_{weapon}.prefab")
            with open(view_path) as f:
                view = json.load(f)
            for k in [k for k, e in view["Entities"].items() if e["Name"].startswith(f"View_{weapon}_Light")]:
                del view["Entities"][k]
            order = view["ContainerEntity"]["Components"]["EditorEntitySortComponent"].setdefault("Child Entity Order", [])
            order[:] = [o for o in order if o in view["Entities"]]
            for i, (eid, e) in enumerate(light["Entities"].items()):
                e["Name"] = f"View_{weapon}_Light{i}"
                tc = e["Components"]["TransformComponent"]
                if tc.get("Parent Entity") in ("ContainerEntity", None, ""):
                    tc["Parent Entity"] = "ContainerEntity"
                for k in [k for k, c in e["Components"].items() if "Collider" in c.get("$type", "") or "RigidBody" in c.get("$type", "")]:
                    del e["Components"][k]
                view["Entities"][eid] = e
                if tc["Parent Entity"] == "ContainerEntity":
                    order.append(eid)
            with open(view_path, "w") as f:
                json.dump(view, f, indent=4)
            print(f"light on View_{weapon}: {len(light['Entities'])} entities")
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    main()
