#!/usr/bin/env python3
"""
Builds everything Levels 1 and 2 need, from the Level 0 pieces:

    python3 Tools/Levels/make_levels_1_2.py        (run from the project folder)

  Level 1, concrete warehouse / parking garage: concrete floor, walls, ceiling and columns (hazard band at the
  foot), wooden crates, long strip lights high up on a 5 m ceiling, concrete footsteps, damp garage room tone.
  Level 2, pipe tunnels: rusty metal walls, grimy floor, dark ceiling, rusted pipes, dim amber caged bulbs,
  metal footsteps, machinery room tone.

Writes textures (Assets/Textures/LevelN), materials (Assets/Materials/LevelN), sounds (Assets/Sounds/LevelN),
piece prefabs (Prefabs/LevelN) and the levels (Levels/LevelN/LevelN.prefab). Prefabs are copies of the Level 0
ones with their materials, lights and sounds swapped; asset IDs are worked out the way the Asset Processor
makes them (a name-based UUID of the file's lowercase project-relative path), so nothing has to be clicked
together in the editor. Rerun after changing anything here.
"""

import copy
import hashlib
import json
import os
import sys
import uuid

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Level0"))
import make_textures as tex  # noqa: E402  (noise helpers)
import make_sounds as snd    # noqa: E402

PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))
tex.RNG = np.random.default_rng(2024)


def asset_guid(relative_path):
    """The Asset Processor's source UUID: SHA-1 name UUID of the lowercase project-relative path."""
    digest = bytearray(hashlib.sha1(relative_path.lower().encode()).digest()[:16])
    digest[6] = (digest[6] & 0x0F) | 0x50
    digest[8] = (digest[8] & 0x3F) | 0x80
    return "{" + str(uuid.UUID(bytes=bytes(digest))).upper() + "}"


def path(*parts):
    full = os.path.join(PROJECT, *parts)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    return full


# ----------------------------------------------------------------------------- textures

def concrete(h, w, base, spread, seed_scale=40.0, stains=0.0):
    n = tex.periodic_noise(h, w, seed_scale, octaves=5)
    fine = tex.periodic_noise(h, w, 4.0, octaves=3)
    shade = base + spread * (n - 0.5) + 0.05 * (fine - 0.5)
    rgb = np.stack([shade, shade * 0.99, shade * 0.97], -1)
    if stains:
        # A few broad, soft damp patches (not many small spots: those read as a pattern once the floor tiles).
        mask = tex.blotches(h, w, 200.0, 0.7, 0.25) * stains
        rgb *= (1.0 - 0.25 * mask)[..., None]
    height = 0.6 * n + 0.4 * fine
    return np.clip(rgb, 0, 1), height


def level1_textures():
    out = "Assets/Textures/Level1"
    s = 512
    # Floor: a 4 m tile of worn, oil-stained concrete with hairline cracks.
    rgb, height = concrete(s, s, 0.42, 0.08, stains=1.0)
    cracks = tex.periodic_noise(s, s, 25.0, octaves=6)
    crack = np.clip(1.0 - np.abs(cracks - 0.5) * 60.0, 0, 1)
    rgb *= (1.0 - 0.5 * crack)[..., None]
    tex.save(rgb, path(out, "level1_floor_basecolor.png"))
    tex.save(tex.normal_from_height(height - crack * 0.5, 2.0), path(out, "level1_floor_normal.png"))
    tex.save(np.clip(0.75 - 0.4 * tex.blotches(s, s, 60.0, 0.6), 0, 1), path(out, "level1_floor_roughness.png"), "L")
    # Walls: board-formed concrete with water streaks running down (one 8 m x 5 m face).
    rgb, height = concrete(s, s, 0.5, 0.08)
    boards = (np.arange(s)[:, None] % 128 < 2).astype(float)
    # Water streaks: broad runs down from the top, from a smooth row of noise (not per-pixel columns).
    runs = tex.periodic_noise(s, s, 90.0, octaves=2)[0]
    streak = np.clip((runs[None, :] - 0.6) * 3.0, 0, 1) * np.linspace(1.0, 0.15, s)[:, None]
    rgb *= (1.0 - 0.1 * boards - 0.25 * streak)[..., None]
    tex.save(rgb, path(out, "level1_wall_basecolor.png"))
    tex.save(tex.normal_from_height(height - boards * 0.6, 2.5), path(out, "level1_wall_normal.png"))
    # Column: concrete with a yellow/black hazard band at the foot (texture runs bottom (v=1) to top (v=0)).
    rgb, height = concrete(s, s // 4, 0.52, 0.12)
    rows = np.arange(s)[:, None]
    band = rows > s * 0.86
    stripe = ((np.arange(s)[:, None] + np.arange(s // 4)[None, :]) // 14) % 2 == 0
    yellow = np.array([0.78, 0.62, 0.08])
    black = np.array([0.06, 0.06, 0.05])
    hazard = np.where(stripe[..., None], yellow, black)
    rgb = np.where(band[..., None], hazard * (0.8 + 0.2 * height[..., None]), rgb)
    tex.save(rgb, path(out, "level1_column_basecolor.png"))
    # Ceiling: darker, water-damaged concrete.
    rgb, height = concrete(s, s, 0.3, 0.12, stains=0.8)
    tex.save(rgb, path(out, "level1_ceiling_basecolor.png"))
    # Crate: wooden planks with dark gaps and a stencil-free scuffed look.
    planks = (np.arange(s)[:, None] % 102 < 4).astype(float)
    grain = tex.periodic_noise(s, s, 3.0, octaves=4)
    wood = np.stack([0.48 + 0.18 * grain, 0.34 + 0.12 * grain, 0.2 + 0.07 * grain], -1)
    frame = ((np.arange(s)[None, :] < 30) | (np.arange(s)[None, :] > s - 30) | (np.arange(s)[:, None] < 30) | (np.arange(s)[:, None] > s - 30))
    wood = wood * (1.0 - 0.6 * planks)[..., None] * np.where(frame[..., None], 0.75, 1.0)
    tex.save(wood, path(out, "level1_crate_basecolor.png"))
    tex.save(tex.normal_from_height(grain * 0.3 - planks, 3.0), path(out, "level1_crate_normal.png"))
    # Strip light lens (lit and dead).
    lens_x = np.abs(np.linspace(-1, 1, s // 4))[None, :]
    lens = np.clip(1.2 - lens_x * 1.3, 0, 1).repeat(s, 0)
    tex.save(np.stack([lens * 0.9 + 0.1] * 3, -1), path(out, "level1_strip_basecolor.png"))
    tex.save(np.stack([lens, lens * 0.97, lens * 0.9], -1), path(out, "level1_strip_emissive.png"))
    tex.save(np.stack([0.2 + 0.1 * lens] * 3, -1), path(out, "level1_strip_dead_basecolor.png"))


def level2_textures():
    out = "Assets/Textures/Level2"
    s = 512
    rust = tex.blotches(s, s, 120.0, 0.62, 0.3) * 0.85  # broad, soft rust (small hard blotches read as spots)
    rust_fine = tex.periodic_noise(s, s, 5.0, octaves=4)
    metal = np.stack([0.32 + 0.05 * rust_fine] * 3, -1) * np.array([1.0, 1.0, 1.04])
    rust_col = np.stack([0.3 + 0.1 * rust_fine, 0.17 + 0.05 * rust_fine, 0.09 + 0.03 * rust_fine], -1)
    # Walls: riveted metal panels over a grimy lower half (one 3 m x 2.6 m face).
    panel = ((np.arange(s)[None, :] % 128 < 3) | (np.arange(s)[:, None] % 170 < 3)).astype(float)
    rivets = (((np.arange(s)[None, :] % 128 - 10) ** 2 + (np.arange(s)[:, None] % 170 - 10) ** 2) < 9).astype(float)
    grime = np.clip((np.arange(s)[:, None] / s - 0.55) * 2.5, 0, 1) * (0.6 + 0.4 * rust_fine)
    rgb = metal * (1 - rust[..., None]) + rust_col * rust[..., None]
    rgb *= (1.0 - 0.45 * panel - 0.4 * grime)[..., None]
    rgb += 0.12 * rivets[..., None]
    tex.save(rgb, path(out, "level2_wall_basecolor.png"))
    tex.save(tex.normal_from_height(rivets * 1.5 - panel + rust_fine * 0.2, 3.0), path(out, "level2_wall_normal.png"))
    tex.save(np.clip(0.45 + 0.45 * rust, 0, 1), path(out, "level2_wall_roughness.png"), "L")
    tex.save(np.clip(0.8 - 0.8 * rust, 0, 1), path(out, "level2_wall_metallic.png"), "L")
    # Floor: grimy concrete with a drain grate pattern now and then (2 m tile).
    rgb, height = concrete(s, s, 0.28, 0.12, stains=1.0)
    tex.save(rgb, path(out, "level2_floor_basecolor.png"))
    tex.save(tex.normal_from_height(height, 1.5), path(out, "level2_floor_normal.png"))
    # Ceiling: dark sooty metal.
    tex.save(metal * 0.45 * (0.8 + 0.2 * rust_fine[..., None]), path(out, "level2_ceiling_basecolor.png"))
    # Pipes: rusted steel.
    pipe_rust = tex.blotches(s, s, 30.0, 0.45, 0.25)
    rgb = np.stack([0.42] * 3, -1) * (1 - pipe_rust[..., None]) + rust_col * pipe_rust[..., None]
    tex.save(rgb, path(out, "level2_pipe_basecolor.png"))
    tex.save(np.clip(0.35 + 0.5 * pipe_rust, 0, 1), path(out, "level2_pipe_roughness.png"), "L")
    # Cage bulb: warm glass behind wire (lit and dead).
    yy, xx = np.mgrid[0:s // 4, 0:s // 4] / (s / 4) - 0.5
    glow = np.clip(1.0 - np.sqrt(xx * xx + yy * yy) * 2.2, 0, 1)
    wire = ((np.abs(xx * 12) % 1 < 0.08) | (np.abs(yy * 12) % 1 < 0.08)).astype(float)
    tex.save(np.stack([glow * 0.9 + 0.1, glow * 0.7 + 0.08, glow * 0.4 + 0.05], -1) * (1 - 0.8 * wire[..., None]),
             path(out, "level2_cage_basecolor.png"))
    tex.save(np.stack([glow, glow * 0.72, glow * 0.4], -1) * (1 - wire[..., None]), path(out, "level2_cage_emissive.png"))
    tex.save(np.stack([0.15 + 0.05 * glow] * 3, -1) * (1 - 0.7 * wire[..., None]), path(out, "level2_cage_dead_basecolor.png"))


# ----------------------------------------------------------------------------- materials

def material(level, name, props):
    values = {"baseColor.color": [1, 1, 1, 1]}
    for key, value in props.items():
        if key.endswith("textureMap"):
            value = f"../../Textures/Level{level}/{value}"
        values[key] = value
    data = {
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": values,
    }
    rel = f"Assets/Materials/Level{level}/{name}.material"
    with open(path(rel), "w") as f:
        json.dump(data, f, indent=4)
    return asset_guid(rel)


def materials():
    m = {}
    m[1] = {
        "wall": material(1, "level1_wall", {"baseColor.textureMap": "level1_wall_basecolor.png",
                                             "normal.textureMap": "level1_wall_normal.png", "normal.factor": 0.7,
                                             "roughness.factor": 0.85, "metallic.factor": 0.0}),
        "pillar": material(1, "level1_column", {"baseColor.textureMap": "level1_column_basecolor.png",
                                                 "roughness.factor": 0.8, "metallic.factor": 0.0}),
        "floor": material(1, "level1_floor", {"baseColor.textureMap": "level1_floor_basecolor.png",
                                               "normal.textureMap": "level1_floor_normal.png", "normal.factor": 0.6,
                                               "roughness.textureMap": "level1_floor_roughness.png",
                                               "uv.tileU": 12.0, "uv.tileV": 12.0}),
        "ceiling": material(1, "level1_ceiling", {"baseColor.textureMap": "level1_ceiling_basecolor.png",
                                                   "roughness.factor": 0.9, "uv.tileU": 8.0, "uv.tileV": 8.0}),
        "lit": material(1, "level1_strip", {"baseColor.textureMap": "level1_strip_basecolor.png", "roughness.factor": 0.4,
                                             "emissive.enable": True, "emissive.color": [1.0, 0.98, 0.95, 1.0],
                                             "emissive.textureMap": "level1_strip_emissive.png", "emissive.intensity": 4.0}),
        "dead": material(1, "level1_strip_dead", {"baseColor.textureMap": "level1_strip_dead_basecolor.png",
                                                   "roughness.factor": 0.6}),
        "prop": material(1, "level1_crate", {"baseColor.textureMap": "level1_crate_basecolor.png",
                                              "normal.textureMap": "level1_crate_normal.png", "normal.factor": 0.8,
                                              "roughness.factor": 0.85}),
    }
    m[2] = {
        "wall": material(2, "level2_wall", {"baseColor.textureMap": "level2_wall_basecolor.png",
                                             "normal.textureMap": "level2_wall_normal.png", "normal.factor": 0.8,
                                             "roughness.textureMap": "level2_wall_roughness.png",
                                             "metallic.textureMap": "level2_wall_metallic.png"}),
        "pillar": None,
        "floor": material(2, "level2_floor", {"baseColor.textureMap": "level2_floor_basecolor.png",
                                               "normal.textureMap": "level2_floor_normal.png", "normal.factor": 0.6,
                                               "roughness.factor": 0.8, "uv.tileU": 12.0, "uv.tileV": 12.0}),
        "ceiling": material(2, "level2_ceiling", {"baseColor.textureMap": "level2_ceiling_basecolor.png",
                                                   "roughness.factor": 0.7, "metallic.factor": 0.6,
                                                   "uv.tileU": 10.0, "uv.tileV": 10.0}),
        "lit": material(2, "level2_cage", {"baseColor.textureMap": "level2_cage_basecolor.png", "roughness.factor": 0.5,
                                            "emissive.enable": True, "emissive.color": [1.0, 0.72, 0.4, 1.0],
                                            "emissive.textureMap": "level2_cage_emissive.png", "emissive.intensity": 3.0}),
        "dead": material(2, "level2_cage_dead", {"baseColor.textureMap": "level2_cage_dead_basecolor.png",
                                                  "roughness.factor": 0.6}),
        "prop": material(2, "level2_pipe", {"baseColor.textureMap": "level2_pipe_basecolor.png",
                                             "roughness.textureMap": "level2_pipe_roughness.png", "metallic.factor": 0.85,
                                             "uv.tileU": 1.0, "uv.tileV": 4.0}),
    }
    return m


# ----------------------------------------------------------------------------- sounds

def sounds():
    rate = snd.RATE
    rng = np.random.default_rng(7)

    def steps(level, kind, make):
        for i in range(1, 5):
            snd.write_wav(path(f"Assets/Sounds/Level{level}/step_{kind}_{i}.wav"), make(i, False))
        snd.write_wav(path(f"Assets/Sounds/Level{level}/step_{kind}_land.wav"), make(9, True), peak=0.9)

    def concrete_step(seed, heavy):
        seconds = 0.4 if heavy else 0.28
        t = np.arange(int(rate * seconds)) / rate
        r = np.random.default_rng(300 + seed)
        click = r.standard_normal(t.size) * np.exp(-t * (60 if heavy else 90))
        thud = np.sin(2 * np.pi * r.uniform(85, 110) * t) * np.exp(-t * 30)
        grit = snd.periodic_noise(seconds, 2500, 9000, seed=seed)[: t.size] * np.exp(-t * 45) * 0.4
        echo = np.roll(click, int(rate * 0.06)) * 0.25  # a hard room
        return (0.7 * click + thud * (1.3 if heavy else 0.8) + grit + echo) * np.clip(t / 0.002, 0, 1)

    def metal_step(seed, heavy):
        seconds = 0.5 if heavy else 0.36
        t = np.arange(int(rate * seconds)) / rate
        r = np.random.default_rng(500 + seed)
        ring = sum(a * np.sin(2 * np.pi * f * r.uniform(0.95, 1.05) * t) for f, a in [(420, 0.5), (890, 0.35), (1730, 0.2)])
        ring *= np.exp(-t * (14 if heavy else 20))
        hit = r.standard_normal(t.size) * np.exp(-t * 80)
        return (hit * 0.8 + ring * (1.2 if heavy else 0.8)) * np.clip(t / 0.002, 0, 1)

    steps(1, "concrete", concrete_step)
    steps(2, "metal", metal_step)

    # Level 1: big damp garage. Low rumble, wide air, distant drips, a faint far-off fluorescent buzz.
    seconds = 12.0
    t = np.arange(int(rate * seconds)) / rate
    tone = snd.periodic_noise(seconds, 20, 300, tilt=1.2, seed=11) + 0.3 * snd.periodic_noise(seconds, 300, 2500, tilt=0.6, seed=12)
    for _ in range(7):
        at = rng.uniform(0, seconds - 0.3)
        f = rng.uniform(900, 1800)
        d = np.clip(t - at, 0, None)
        tone += 0.35 * np.sin(2 * np.pi * f * d * (1 + 0.5 * d)) * np.exp(-d * 35) * (t >= at)
    tone += 0.05 * np.sin(2 * np.pi * 120 * t)
    snd.write_wav(path("Assets/Sounds/Level1/level1_roomtone.wav"), tone, peak=0.6)

    # Level 2: machinery. Deep pumping drone, a pressure hiss, the odd distant clank.
    seconds = 10.0
    t = np.arange(int(rate * seconds)) / rate
    pump = 1.0 + 0.3 * np.sin(2 * np.pi * 0.8 * t)
    tone = pump * (0.5 * np.sin(2 * np.pi * 45 * t) + 0.25 * np.sin(2 * np.pi * 90.4 * t) + 0.12 * np.sin(2 * np.pi * 135 * t))
    tone += 0.35 * snd.periodic_noise(seconds, 3000, 12000, tilt=0.3, seed=21)
    tone += 0.6 * snd.periodic_noise(seconds, 20, 200, tilt=1.0, seed=22)
    for _ in range(4):
        at = rng.uniform(0, seconds - 1.0)
        d = np.clip(t - at, 0, None)
        tone += 0.5 * (np.sin(2 * np.pi * 310 * d) + 0.6 * np.sin(2 * np.pi * 737 * d)) * np.exp(-d * 9) * (t >= at)
    snd.write_wav(path("Assets/Sounds/Level2/level2_roomtone.wav"), tone, peak=0.6)


# ----------------------------------------------------------------------------- prefabs

LEVEL0_MATERIALS = {
    "wall": "assets/materials/level0/level0_wallpaper.material",
    "pillar": "assets/materials/level0/level0_wallpaper_pillar.material",
    "floor": "assets/materials/level0/level0_carpet.material",
    "ceiling": "assets/materials/level0/level0_ceiling.material",
    "lit": "assets/materials/level0/level0_lightpanel.material",
    "dead": "assets/materials/level0/level0_deadpanel.material",
}

# Per level: panel size (strip light or caged bulb) and light changes.
LIGHTS = {
    1: {"panel": [0.22, 2.2, 0.06], "color": [1.0, 0.97, 0.92], "bulb": (2.6, 1.6), "fill": (2.2, 1.6)},
    2: {"panel": [0.28, 0.28, 0.16], "color": [1.0, 0.7, 0.4], "bulb": (0.45, 0.7), "fill": (0.4, 0.75)},
}


def walk(node, fn):
    if isinstance(node, dict):
        fn(node)
        for value in node.values():
            walk(value, fn)
    elif isinstance(node, list):
        for value in node:
            walk(value, fn)


def piece(level, source, target, mats, light=False):
    with open(path("Prefabs/Level0", source)) as f:
        data = json.load(f)
    swap = {asset_guid(rel): mats[key] for key, rel in LEVEL0_MATERIALS.items() if mats.get(key)}
    if source == "L0_Pillar.prefab" and mats.get("prop"):
        swap[asset_guid(LEVEL0_MATERIALS["pillar"])] = mats["prop"]  # props are pillar boxes with their own material

    def fix(node):
        guid = node.get("guid")
        if isinstance(guid, str) and guid.upper() in swap:
            node["guid"] = swap[guid.upper()]
        if light and "Configuration" in node and isinstance(node["Configuration"], dict) and "LightType" in node["Configuration"]:
            cfg = node["Configuration"]
            spec = LIGHTS[level]
            fill = cfg.get("LightType") == 6
            scale_i, scale_r = spec["fill" if fill else "bulb"]
            cfg["Color"] = spec["color"]
            cfg["Intensity"] = round(cfg.get("Intensity", 100.0) * scale_i, 2)
            cfg["AttenuationRadius"] = round(cfg.get("AttenuationRadius", 5.0) * scale_r, 2)

    walk(data, fix)
    for entity in [data.get("ContainerEntity")] + list(data.get("Entities", {}).values()):
        if not entity:
            continue
        if "Name" in entity:
            entity["Name"] = entity["Name"].replace("L0_", f"L{level}_")
        if light and "Panel" in entity.get("Name", ""):
            for comp in entity.get("Components", {}).values():
                if comp.get("$type") == "EditorNonUniformScaleComponent":
                    comp["NonUniformScale"] = LIGHTS[level]["panel"]
    with open(path(f"Prefabs/Level{level}", target), "w") as f:
        json.dump(data, f, indent=4)
    return f"prefabs/level{level}/{target.lower().replace('.prefab', '.spawnable')}"


def pieces(mats):
    spawnables = {}
    for level in (1, 2):
        m = mats[level]
        names = {}
        names["Wall"] = piece(level, "L0_Wall.prefab", f"L{level}_Wall.prefab", m)
        if m["pillar"]:
            names["Pillar"] = piece(level, "L0_Pillar.prefab", f"L{level}_Pillar.prefab", {**m, "prop": None})
        names["Floor"] = piece(level, "L0_Floor.prefab", f"L{level}_Floor.prefab", m)
        names["Ceiling"] = piece(level, "L0_Ceiling.prefab", f"L{level}_Ceiling.prefab", m)
        names["Light"] = piece(level, "L0_Light.prefab", f"L{level}_Light.prefab", m, light=True)
        names["LightFlicker"] = piece(level, "L0_LightFlicker.prefab", f"L{level}_LightFlicker.prefab", m, light=True)
        names["LightDead"] = piece(level, "L0_LightDead.prefab", f"L{level}_LightDead.prefab", m, light=True)
        names["LightDying"] = piece(level, "L0_LightDying.prefab", f"L{level}_LightDying.prefab", m, light=True)
        names["Prop"] = piece(level, "L0_Pillar.prefab", f"L{level}_{'Crate' if level == 1 else 'Pipe'}.prefab", m)
        spawnables[level] = names
    return spawnables


def level_file(level, spawnables):
    with open(path("Levels/Level0/Level0.prefab")) as f:
        data = json.load(f)
    names = spawnables[level]
    kind = "concrete" if level == 1 else "metal"
    sound_swap = {
        asset_guid(f"assets/sounds/level0/step_carpet_{i}.wav"): (asset_guid(f"assets/sounds/level{level}/step_{kind}_{i}.wav"),
                                                                   f"assets/sounds/level{level}/step_{kind}_{i}.wav.miniaudio")
        for i in ("1", "2", "3", "4", "land")
    }
    sound_swap[asset_guid("assets/sounds/level0/level0_roomtone.wav")] = (
        asset_guid(f"assets/sounds/level{level}/level{level}_roomtone.wav"),
        f"assets/sounds/level{level}/level{level}_roomtone.wav.miniaudio")

    def fix(node):
        asset = node.get("assetId")
        if isinstance(asset, dict) and isinstance(asset.get("guid"), str) and asset["guid"].upper() in sound_swap:
            asset["guid"], node["assetHint"] = sound_swap[asset["guid"].upper()]
        if node.get("$type") == "EndlessLevelComponent":
            node["Style"] = level
            node["WallPrefab"] = names["Wall"]
            node["PillarPrefab"] = names.get("Pillar", names["Wall"])
            node["FloorPrefab"] = names["Floor"]
            node["CeilingPrefab"] = names["Ceiling"]
            node["LightPrefab"] = names["Light"]
            node["LightFlickerPrefab"] = names["LightFlicker"]
            node["LightDeadPrefab"] = names["LightDead"]
            node["LightDyingPrefab"] = names["LightDying"]
            node["PropPrefab"] = names["Prop"]
            node["EnablePillars"] = level == 1
            node["DarkZoneShare"] = 0.45 if level == 1 else 0.55

    walk(data, fix)
    for entity in data["Entities"].values():
        if entity.get("Name") == "Level0Generator":
            entity["Name"] = f"Level{level}Generator"
    with open(path(f"Levels/Level{level}/Level{level}.prefab"), "w") as f:
        json.dump(data, f, indent=4)
    print("wrote", f"Levels/Level{level}/Level{level}.prefab")


def noclip_spot():
    """The tear you noclip through: a near-black slab with a faint violet glow, shared by every level."""
    s = 256
    tex.RNG = np.random.default_rng(404)
    n = tex.periodic_noise(s, s, 18.0, octaves=5)
    edge = np.clip(1.0 - np.abs(np.linspace(-1, 1, s))[None, :] * 1.05, 0, 1) ** 0.6
    glow = np.clip((n - 0.35) * 2.0, 0, 1) * edge
    tex.save(np.stack([0.02 + 0.03 * glow] * 3, -1), path("Assets/Textures/Levels/noclip_void_basecolor.png"))
    tex.save(np.stack([0.35 * glow, 0.15 * glow, 0.9 * glow], -1), path("Assets/Textures/Levels/noclip_void_emissive.png"))
    rel = "Assets/Materials/Levels/noclip_void.material"
    data = {
        "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
        "materialTypeVersion": 5,
        "propertyValues": {
            "baseColor.color": [1, 1, 1, 1],
            "baseColor.textureMap": "../../Textures/Levels/noclip_void_basecolor.png",
            "roughness.factor": 0.25,
            "emissive.enable": True,
            "emissive.color": [0.7, 0.5, 1.0, 1.0],
            "emissive.textureMap": "../../Textures/Levels/noclip_void_emissive.png",
            "emissive.intensity": 2.5,
        },
    }
    with open(path(rel), "w") as f:
        json.dump(data, f, indent=4)
    with open(path("Prefabs/Level0/L0_Pillar.prefab")) as f:
        prefab = json.load(f)
    old = asset_guid(LEVEL0_MATERIALS["pillar"])
    new = asset_guid(rel)

    def fix(node):
        if isinstance(node.get("guid"), str) and node["guid"].upper() == old:
            node["guid"] = new
    walk(prefab, fix)
    for entity in [prefab.get("ContainerEntity")] + list(prefab.get("Entities", {}).values()):
        if entity and "Name" in entity:
            entity["Name"] = entity["Name"].replace("L0_Pillar", "NoclipSpot")
    # Walk-through: no collider (a solid tear stopped the player just short of the spot).
    for entity in [prefab.get("ContainerEntity")] + list(prefab.get("Entities", {}).values()):
        comps = entity.get("Components", {}) if entity else {}
        for key in [k for k, c in comps.items() if "Collider" in c.get("$type", "") or "RigidBody" in c.get("$type", "")]:
            del comps[key]
    with open(path("Prefabs/Levels/NoclipSpot.prefab"), "w") as f:
        json.dump(prefab, f, indent=4)
    print("wrote Prefabs/Levels/NoclipSpot.prefab")


def main():
    level1_textures()
    level2_textures()
    mats = materials()
    sounds()
    spawnables = pieces(mats)
    for level in (1, 2):
        level_file(level, spawnables)
    noclip_spot()  # last: it reseeds the noise, so the level textures above stay the same


if __name__ == "__main__":
    main()
