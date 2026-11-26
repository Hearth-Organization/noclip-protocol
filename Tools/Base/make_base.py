#!/usr/bin/env python3
"""
Builds the base-building pieces (the approved Build mockup's LIGHTING tab):

    python3 Tools/Base/make_base.py        (run from the project folder, after Tools/Intro/make_intro.py)

  Prefabs/Base/Build_Pole       a dark steel pole (stretched by the game to each piece's height)
  Prefabs/Base/Build_Floodlight a warm white lamp head (on a 2.2 m pole)
  Prefabs/Base/Build_Flare      a red flare (on a short stand)
  Prefabs/Base/Build_Beacon     the Foundation beacon's glowing gold body
  Prefabs/Base/Build_BeaconLight its gold light
  Prefabs/Base/Build_UvLamp     a violet UV lamp (on a 1.2 m stand)
  (Ceiling panel fix puts up Level 0's own ceiling light.)

Copies of the Level 0 pieces with materials and lights swapped, like the level tools.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Levels"))
import make_levels_1_2 as lv  # noqa: E402

path = lv.path
asset_guid = lv.asset_guid


def material(name, props):
    rel = f"Assets/Materials/Base/{name}.material"
    data = {
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": props,
    }
    with open(path(rel), "w") as f:
        json.dump(data, f, indent=4)
    return asset_guid(rel)


def copy_piece(source, target, swap, light=None):
    with open(path("Prefabs/Level0", source)) as f:
        data = json.load(f)

    def fix(node):
        guid = node.get("guid")
        if isinstance(guid, str) and guid.upper() in swap:
            node["guid"] = swap[guid.upper()]
        if light and "Configuration" in node and isinstance(node["Configuration"], dict) and "LightType" in node["Configuration"]:
            cfg = node["Configuration"]
            fill = cfg.get("LightType") == 6
            scale_i, scale_r = light["fill" if fill else "bulb"]
            cfg["Color"] = light["color"]
            cfg["Intensity"] = round(cfg.get("Intensity", 100.0) * scale_i, 2)
            cfg["AttenuationRadius"] = round(cfg.get("AttenuationRadius", 5.0) * scale_r, 2)

    lv.walk(data, fix)
    stem = target[:-7]
    for entity in [data.get("ContainerEntity")] + list(data.get("Entities", {}).values()):
        if not entity:
            continue
        if "Name" in entity:
            entity["Name"] = entity["Name"].replace("L0_Pillar", stem).replace("L0_Light", stem)
        if light and "Panel" in entity.get("Name", ""):
            for comp in entity.get("Components", {}).values():
                if comp.get("$type") == "EditorNonUniformScaleComponent":
                    comp["NonUniformScale"] = light["panel"]
    with open(path("Prefabs/Base", target), "w") as f:
        json.dump(data, f, indent=4)
    print("wrote", f"Prefabs/Base/{target}")


def main():
    pillar = asset_guid(lv.LEVEL0_MATERIALS["pillar"])
    lit = asset_guid(lv.LEVEL0_MATERIALS["lit"])
    steel = material("build_steel", {"baseColor.color": [0.06, 0.06, 0.055, 1.0], "metallic.factor": 0.3, "roughness.factor": 0.6})
    gold = material("build_beacon", {"baseColor.color": [0.55, 0.4, 0.08, 1.0], "metallic.factor": 0.5, "roughness.factor": 0.4,
                                     "emissive.enable": True, "emissive.color": [1.0, 0.7, 0.2, 1.0], "emissive.intensity": 0.6})
    warm = material("build_lamp_warm", {"baseColor.color": [1.0, 0.95, 0.85, 1.0], "emissive.enable": True,
                                         "emissive.color": [1.0, 0.93, 0.8, 1.0], "emissive.intensity": 5.0})
    red = material("build_lamp_red", {"baseColor.color": [0.9, 0.2, 0.1, 1.0], "emissive.enable": True,
                                       "emissive.color": [1.0, 0.25, 0.12, 1.0], "emissive.intensity": 5.0})
    violet = material("build_lamp_uv", {"baseColor.color": [0.5, 0.3, 0.9, 1.0], "emissive.enable": True,
                                         "emissive.color": [0.6, 0.35, 1.0, 1.0], "emissive.intensity": 5.0})
    beacon_lamp = material("build_lamp_gold", {"baseColor.color": [1.0, 0.8, 0.4, 1.0], "emissive.enable": True,
                                                "emissive.color": [1.0, 0.8, 0.35, 1.0], "emissive.intensity": 4.0})
    copy_piece("L0_Pillar.prefab", "Build_Pole.prefab", {pillar: steel})
    copy_piece("L0_Pillar.prefab", "Build_Beacon.prefab", {pillar: gold})
    copy_piece("L0_Light.prefab", "Build_Floodlight.prefab", {lit: warm},
               {"panel": [0.45, 0.3, 0.25], "color": [1.0, 0.9, 0.75], "bulb": (1.3, 1.3), "fill": (1.0, 1.2)})
    copy_piece("L0_Light.prefab", "Build_Flare.prefab", {lit: red},
               {"panel": [0.12, 0.12, 0.14], "color": [1.0, 0.3, 0.15], "bulb": (0.6, 0.9), "fill": (0.5, 0.9)})
    copy_piece("L0_Light.prefab", "Build_BeaconLight.prefab", {lit: beacon_lamp},
               {"panel": [0.2, 0.2, 0.2], "color": [1.0, 0.8, 0.4], "bulb": (0.8, 1.0), "fill": (0.6, 1.0)})
    copy_piece("L0_Light.prefab", "Build_UvLamp.prefab", {lit: violet},
               {"panel": [0.18, 0.5, 0.08], "color": [0.6, 0.35, 1.0], "bulb": (0.7, 1.0), "fill": (0.5, 1.0)})


if __name__ == "__main__":
    main()
