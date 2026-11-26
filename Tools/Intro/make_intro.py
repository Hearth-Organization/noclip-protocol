#!/usr/bin/env python3
"""
Builds everything the intro needs (Site-[REDACTED], Sublevel 4, where the Foundation punched Breach 4-A into
Level 0's spawn room):

    python3 Tools/Intro/make_intro.py        (run from the project folder; run Tools/Levels/make_levels_1_2.py first)

  Facility pieces: painted block walls with a dark wainscot, sealed concrete floor tiles, acoustic ceiling tiles,
  cool white strip lights, yellow/black hazard columns (also Breach 4-A's frame), the terminal kiosk and its glowing
  screen, a ventilation-and-mains room tone.
  The level: Levels/Intro/Intro.prefab, a copy of Level 0 with the generator set to style 3 (the fixed facility in
  Gem/Source/Level/Level0Layout.cpp) and concrete footsteps (Level 1's).
  Level 0 gets Breach 4-A's frame too (the generator puts it against the spawn room's south wall).
  Levels/MainMenu: Level 0 as the main menu's live backdrop (the generator's Main menu switch).
  Registry/load_level.setreg starts the game at the main menu (NEW EXPEDITION loads the intro).

Prefabs are copies of the Level 0 pieces with materials and lights swapped, the same way as Levels 1 and 2.
"""

import copy
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Levels"))
import make_levels_1_2 as lv  # noqa: E402  (asset IDs, paths, noise, sounds)

tex = lv.tex
snd = lv.snd
path = lv.path
asset_guid = lv.asset_guid

OUT_TEX = "Assets/Textures/Intro"
OUT_MAT = "Assets/Materials/Intro"


# ----------------------------------------------------------------------------- textures

def textures():
    tex.RNG = np.random.default_rng(1312)
    s = 512
    # Walls: painted concrete block, pale institutional green-grey, a dark grey wainscot below 1 m and a thin
    # yellow line on top of it. Only horizontal features: walls are stretched to their length.
    n = tex.periodic_noise(s, s, 30.0, octaves=5)
    fine = tex.periodic_noise(s, s, 3.0, octaves=3)
    rows = np.arange(s)[:, None] / s  # 0 = top (3.2 m), 1 = floor
    paint = np.array([0.56, 0.6, 0.55])
    rgb = paint * (0.92 + 0.08 * n[..., None] + 0.03 * (fine[..., None] - 0.5))
    mortar = (np.arange(s)[:, None] % 32 < 2).astype(float)  # a course every 0.2 m
    rgb = rgb * (1.0 - 0.12 * mortar[..., None])
    wainscot = rows > 1.0 - 1.0 / 3.2
    rgb = np.where(wainscot[..., None], np.array([0.22, 0.23, 0.22]) * (0.9 + 0.2 * n[..., None]), rgb)
    line = (rows > 1.0 - 1.06 / 3.2) & ~wainscot
    rgb = np.where(line[..., None], np.array([0.78, 0.62, 0.1]), rgb)
    grime = np.clip((rows - 0.75) * 4.0, 0, 1) * tex.periodic_noise(s, s, 60.0, octaves=3)
    rgb = rgb * (1.0 - 0.25 * grime[..., None])
    tex.save(np.clip(rgb, 0, 1), path(OUT_TEX, "intro_wall_basecolor.png"))
    tex.save(tex.normal_from_height(0.3 * fine - mortar * 0.5, 1.5), path(OUT_TEX, "intro_wall_normal.png"))

    # Floor: one 2 m sealed concrete tile with a dark joint at its edge (tiled across the slab).
    rgb, height = lv.concrete(s, s, 0.38, 0.06)
    edge = np.zeros((s, s))
    edge[:3, :] = 1
    edge[:, :3] = 1
    rgb = rgb * (1.0 - 0.45 * edge[..., None])
    tex.save(rgb, path(OUT_TEX, "intro_floor_basecolor.png"))
    tex.save(tex.normal_from_height(height - edge, 2.0), path(OUT_TEX, "intro_floor_normal.png"))

    # Ceiling: one 0.6 m acoustic tile, speckled, with its grid.
    speck = tex.periodic_noise(s, s, 2.0, octaves=2)
    rgb = np.stack([0.78 + 0.06 * speck] * 3, -1) * np.array([1.0, 0.99, 0.96])
    grid = np.zeros((s, s))
    grid[:10, :] = 1
    grid[:, :10] = 1
    rgb = np.where(grid[..., None] > 0, np.array([0.55, 0.55, 0.53]), rgb)
    tex.save(rgb, path(OUT_TEX, "intro_ceiling_basecolor.png"))

    # Hazard: yellow and black diagonal stripes, scuffed (Breach 4-A's frame and the chamber's columns).
    stripe = ((np.arange(s)[:, None] + np.arange(s)[None, :]) // 48) % 2 == 0
    wear = tex.periodic_noise(s, s, 20.0, octaves=4)
    rgb = np.where(stripe[..., None], np.array([0.8, 0.63, 0.06]), np.array([0.05, 0.05, 0.045]))
    rgb = rgb * (0.8 + 0.25 * wear[..., None])
    tex.save(np.clip(rgb, 0, 1), path(OUT_TEX, "intro_hazard_basecolor.png"))

    # Kiosk: dark grey powder-coated steel with panel seams.
    n = tex.periodic_noise(s, s, 8.0, octaves=3)
    seams = ((np.arange(s)[:, None] % 170 < 3) | (np.arange(s)[None, :] % 256 < 3)).astype(float)
    rgb = np.stack([0.13 + 0.03 * n] * 3, -1) * (1.0 - 0.5 * seams[..., None])
    tex.save(rgb, path(OUT_TEX, "intro_kiosk_basecolor.png"))

    # Screen: the terminal's amber-cream text on black, as seen from across the room (rows of text, scanlines).
    h, w = 256, 512
    rows = np.arange(h)[:, None]
    cols = np.arange(w)[None, :]
    r = np.random.default_rng(44)
    glow = np.zeros((h, w))
    header = (rows > 10) & (rows < 26) & (cols > 20) & (cols < 200)
    glow[header] = 0.9
    glow[(rows > 32) & (rows < 34) & (cols > 10) & (cols < w - 10)] = 0.5
    for line_y in range(50, h - 50, 14):
        length = r.integers(200, w - 60)
        glow[(rows > line_y) & (rows < line_y + 7) & (cols > 200) & (cols < length)] = 0.75
    for line_y in range(50, 160, 22):
        glow[(rows > line_y) & (rows < line_y + 9) & (cols > 20) & (cols < 170)] = 0.45
    glow[(rows > h - 40) & (rows < h - 18) & (cols > 330) & (cols < 420)] = 1.0  # ACCEPT [Y]
    glow *= np.where(rows % 3 == 0, 0.6, 1.0)
    tex.save(np.stack([0.02 + 0.05 * glow] * 3, -1), path(OUT_TEX, "intro_screen_basecolor.png"))
    tex.save(np.stack([glow, glow * 0.93, glow * 0.72], -1), path(OUT_TEX, "intro_screen_emissive.png"))

    # Strip light lens (lit and dead): cool white.
    lens_x = np.abs(np.linspace(-1, 1, s // 4))[None, :]
    lens = np.clip(1.2 - lens_x * 1.3, 0, 1).repeat(s, 0)
    tex.save(np.stack([lens * 0.9 + 0.1] * 3, -1), path(OUT_TEX, "intro_strip_basecolor.png"))
    tex.save(np.stack([lens * 0.92, lens * 0.96, lens], -1), path(OUT_TEX, "intro_strip_emissive.png"))
    tex.save(np.stack([0.2 + 0.1 * lens] * 3, -1), path(OUT_TEX, "intro_strip_dead_basecolor.png"))


# ----------------------------------------------------------------------------- materials

def material(name, props):
    values = {"baseColor.color": [1, 1, 1, 1]}
    for key, value in props.items():
        if key.endswith("textureMap"):
            value = f"../../Textures/Intro/{value}"
        values[key] = value
    data = {
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": values,
    }
    rel = f"{OUT_MAT}/{name}.material"
    with open(path(rel), "w") as f:
        json.dump(data, f, indent=4)
    return asset_guid(rel)


def materials():
    return {
        "wall": material("intro_wall", {"baseColor.textureMap": "intro_wall_basecolor.png",
                                         "normal.textureMap": "intro_wall_normal.png", "normal.factor": 0.5,
                                         "roughness.factor": 0.75, "metallic.factor": 0.0}),
        "pillar": material("intro_hazard", {"baseColor.textureMap": "intro_hazard_basecolor.png",
                                             "roughness.factor": 0.6, "metallic.factor": 0.0}),
        "floor": material("intro_floor", {"baseColor.textureMap": "intro_floor_basecolor.png",
                                           "normal.textureMap": "intro_floor_normal.png", "normal.factor": 0.5,
                                           "roughness.factor": 0.45, "uv.tileU": 12.0, "uv.tileV": 24.0}),
        "ceiling": material("intro_ceiling", {"baseColor.textureMap": "intro_ceiling_basecolor.png",
                                               "roughness.factor": 0.95, "uv.tileU": 40.0, "uv.tileV": 80.0}),
        "lit": material("intro_strip", {"baseColor.textureMap": "intro_strip_basecolor.png", "roughness.factor": 0.4,
                                         "emissive.enable": True, "emissive.color": [0.92, 0.96, 1.0, 1.0],
                                         "emissive.textureMap": "intro_strip_emissive.png", "emissive.intensity": 4.0}),
        "dead": material("intro_strip_dead", {"baseColor.textureMap": "intro_strip_dead_basecolor.png",
                                               "roughness.factor": 0.6}),
        "prop": material("intro_kiosk", {"baseColor.textureMap": "intro_kiosk_basecolor.png",
                                          "roughness.factor": 0.55, "metallic.factor": 0.6}),
        "screen": material("intro_screen", {"baseColor.textureMap": "intro_screen_basecolor.png", "roughness.factor": 0.15,
                                             "emissive.enable": True, "emissive.color": [1.0, 0.95, 0.8, 1.0],
                                             "emissive.textureMap": "intro_screen_emissive.png", "emissive.intensity": 3.0}),
    }


# ----------------------------------------------------------------------------- sounds

def sounds():
    rate = snd.RATE
    seconds = 12.0
    t = np.arange(int(rate * seconds)) / rate
    # Ventilation: broad soft air; mains hum (60 Hz and its harmonics); a slow, deep throb from the breach chamber.
    tone = 0.7 * snd.periodic_noise(seconds, 60, 900, tilt=1.0, seed=31)
    tone += 0.25 * snd.periodic_noise(seconds, 900, 5000, tilt=0.5, seed=32)
    tone += 0.06 * np.sin(2 * np.pi * 60 * t) + 0.04 * np.sin(2 * np.pi * 120 * t) + 0.02 * np.sin(2 * np.pi * 180 * t)
    throb = 0.5 + 0.5 * np.sin(2 * np.pi * t / 4.0)  # 3 cycles in 12 s: loops cleanly
    tone += 0.25 * throb ** 3 * np.sin(2 * np.pi * 38 * t)
    snd.write_wav(path("Assets/Sounds/Intro/intro_roomtone.wav"), tone, peak=0.55)


# ----------------------------------------------------------------------------- prefabs

LIGHT = {"panel": [0.22, 1.6, 0.05], "color": [0.92, 0.96, 1.0], "bulb": (0.8, 1.0), "fill": (0.7, 1.0)}


def piece(source, target, swap_to, light=False, panel=None):
    """A copy of a Level 0 piece with its materials swapped (swap_to: LEVEL0_MATERIALS key -> new material GUID)."""
    with open(path("Prefabs/Level0", source)) as f:
        data = json.load(f)
    swap = {asset_guid(lv.LEVEL0_MATERIALS[key]): guid for key, guid in swap_to.items()}

    def fix(node):
        guid = node.get("guid")
        if isinstance(guid, str) and guid.upper() in swap:
            node["guid"] = swap[guid.upper()]
        if light and "Configuration" in node and isinstance(node["Configuration"], dict) and "LightType" in node["Configuration"]:
            cfg = node["Configuration"]
            fill = cfg.get("LightType") == 6
            scale_i, scale_r = LIGHT["fill" if fill else "bulb"]
            cfg["Color"] = LIGHT["color"]
            cfg["Intensity"] = round(cfg.get("Intensity", 100.0) * scale_i, 2)
            cfg["AttenuationRadius"] = round(cfg.get("AttenuationRadius", 5.0) * scale_r, 2)

    lv.walk(data, fix)
    for entity in [data.get("ContainerEntity")] + list(data.get("Entities", {}).values()):
        if not entity:
            continue
        if "Name" in entity:
            entity["Name"] = entity["Name"].replace("L0_Pillar", target[:-7]).replace("L0_", "Intro_")
        if light and "Panel" in entity.get("Name", ""):
            for comp in entity.get("Components", {}).values():
                if comp.get("$type") == "EditorNonUniformScaleComponent":
                    comp["NonUniformScale"] = LIGHT["panel"]
    with open(path("Prefabs/Intro", target), "w") as f:
        json.dump(data, f, indent=4)
    return f"prefabs/intro/{target.lower().replace('.prefab', '.spawnable')}"


def pieces(m):
    lights = {"lit": m["lit"], "dead": m["dead"]}
    return {
        "Wall": piece("L0_Wall.prefab", "Intro_Wall.prefab", {"wall": m["wall"]}),
        "Pillar": piece("L0_Pillar.prefab", "Intro_Hazard.prefab", {"pillar": m["pillar"]}),
        "Floor": piece("L0_Floor.prefab", "Intro_Floor.prefab", {"floor": m["floor"]}),
        "Ceiling": piece("L0_Ceiling.prefab", "Intro_Ceiling.prefab", {"ceiling": m["ceiling"]}),
        "Light": piece("L0_Light.prefab", "Intro_Light.prefab", lights, light=True),
        "LightFlicker": piece("L0_LightFlicker.prefab", "Intro_LightFlicker.prefab", lights, light=True),
        "LightDead": piece("L0_LightDead.prefab", "Intro_LightDead.prefab", lights, light=True),
        "LightDying": piece("L0_LightDying.prefab", "Intro_LightDying.prefab", lights, light=True),
        "Prop": piece("L0_Pillar.prefab", "Intro_Kiosk.prefab", {"pillar": m["prop"]}),
        "Screen": piece("L0_Pillar.prefab", "Intro_Screen.prefab", {"pillar": m["screen"]}),
    }


def strip_colliders(prefab_rel):
    """The screen sits on the kiosk's face: no collider of its own (the kiosk's stops you)."""
    full = path(prefab_rel)
    with open(full) as f:
        data = json.load(f)
    removed = 0
    for entity in [data.get("ContainerEntity")] + list(data.get("Entities", {}).values()):
        comps = entity.get("Components", {}) if entity else {}
        for key in [k for k, c in comps.items() if "Collider" in c.get("$type", "") or "StaticRigidBody" in c.get("$type", "")]:
            del comps[key]
            removed += 1
    with open(full, "w") as f:
        json.dump(data, f, indent=4)
    return removed


def level_file(names):
    with open(path("Levels/Level0/Level0.prefab")) as f:
        data = json.load(f)
    sound_swap = {
        asset_guid(f"assets/sounds/level0/step_carpet_{i}.wav"): (asset_guid(f"assets/sounds/level1/step_concrete_{i}.wav"),
                                                                   f"assets/sounds/level1/step_concrete_{i}.wav.miniaudio")
        for i in ("1", "2", "3", "4", "land")
    }
    sound_swap[asset_guid("assets/sounds/level0/level0_roomtone.wav")] = (
        asset_guid("assets/sounds/intro/intro_roomtone.wav"), "assets/sounds/intro/intro_roomtone.wav.miniaudio")

    def fix(node):
        asset = node.get("assetId")
        if isinstance(asset, dict) and isinstance(asset.get("guid"), str) and asset["guid"].upper() in sound_swap:
            asset["guid"], node["assetHint"] = sound_swap[asset["guid"].upper()]
        if node.get("$type") == "EndlessLevelComponent":
            node["Style"] = 3
            node["WallPrefab"] = names["Wall"]
            node["PillarPrefab"] = names["Pillar"]
            node["FloorPrefab"] = names["Floor"]
            node["CeilingPrefab"] = names["Ceiling"]
            node["LightPrefab"] = names["Light"]
            node["LightFlickerPrefab"] = names["LightFlicker"]
            node["LightDeadPrefab"] = names["LightDead"]
            node["LightDyingPrefab"] = names["LightDying"]
            node["PropPrefab"] = names["Prop"]
            node["FramePrefab"] = names["Pillar"]
            node["ScreenPrefab"] = names["Screen"]
            node["EnablePillars"] = True
            node["EnableStains"] = False

    lv.walk(data, fix)
    for entity in data["Entities"].values():
        if entity.get("Name") == "Level0Generator":
            entity["Name"] = "IntroGenerator"
    with open(path("Levels/Intro/Intro.prefab"), "w") as f:
        json.dump(data, f, indent=4)
    print("wrote Levels/Intro/Intro.prefab")


def level0_breach(names):
    """Level 0's end of Breach 4-A: the generator spawns the frame when it has one."""
    full = path("Levels/Level0/Level0.prefab")
    with open(full) as f:
        data = json.load(f)

    def fix(node):
        if node.get("$type") == "EndlessLevelComponent":
            node["FramePrefab"] = names["Pillar"]

    lv.walk(data, fix)
    with open(full, "w") as f:
        json.dump(data, f, indent=4)
    print("Level 0: Breach 4-A frame set")


def main_menu_level():
    """Levels/MainMenu: Level 0 (with Breach 4-A's frame) as the main menu's live backdrop."""
    with open(path("Levels/Level0/Level0.prefab")) as f:
        data = json.load(f)

    def fix(node):
        if node.get("$type") == "EndlessLevelComponent":
            node["MainMenu"] = True

    lv.walk(data, fix)
    for entity in data["Entities"].values():
        if entity.get("Name") == "Level0Generator":
            entity["Name"] = "MainMenuGenerator"
    with open(path("Levels/MainMenu/MainMenu.prefab"), "w") as f:
        json.dump(data, f, indent=4)
    print("wrote Levels/MainMenu/MainMenu.prefab")


def start_in_main_menu():
    full = path("Registry/load_level.setreg")
    data = {"O3DE": {"Autoexec": {"ConsoleCommands": {"LoadLevel": "MainMenu"}}}}
    with open(full, "w") as f:
        json.dump(data, f, indent=4)
        f.write("\n")
    print("the game now starts at the main menu")


def main():
    textures()
    m = materials()
    sounds()
    names = pieces(m)
    print("screen colliders removed:", strip_colliders("Prefabs/Intro/Intro_Screen.prefab"))
    level_file(names)
    level0_breach(names)
    main_menu_level()
    start_in_main_menu()


if __name__ == "__main__":
    main()
