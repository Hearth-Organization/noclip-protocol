#!/usr/bin/env python3
"""
Builds what the catalog levels (Levels/catalog.json: Levels 3 to 99) need:

    python3 Levels/catalog.py                      (first: the catalog and Gem/Source/Level/LevelCatalog.h)
    python3 Tools/Levels/make_catalog_levels.py    (run from anywhere)

  Assets/Textures/Catalog/<surface>_basecolor.png (+ _normal.png)   the surfaces levels are made of (512 px, seamless)
  Assets/Materials/Catalog/<surface>_<hash>.material                each level's three surfaces, tinted, tiled to its layout
  Gem/Source/Level/LevelCatalogMaterials.h                           which of them each level uses (levels alike share one)
  Levels/Backrooms/Backrooms.prefab                                 the one O3DE level every catalog level is played in

The Backrooms level is Level 1's level file with its generator set to "catalog" (style 4) and Level 0's tile pieces.
When it loads, the generator reads the level number (/NoclipProtocol/Level/Number), builds that level's layout and
puts the level's materials on the pieces and its colour on the lights as they spawn. Only one level is ever loaded.
Rerun after changing the catalog or anything here.
"""

import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Level0"))
sys.path.insert(0, HERE)
import make_textures as tex  # noqa: E402
from make_levels_1_2 import asset_guid, path, walk  # noqa: E402

PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))
S = 512

# Layout sizes: must match Level0::ApplyCatalog (Level0Layout.cpp). cell size, cells per chunk, ceiling height.
LAYOUT = {
    "rooms": (4.0, 6, 3.0), "halls": (8.0, 6, 5.0), "corridors": (3.0, 8, 2.6), "outdoor": (10.0, 6, 14.0),
    "caves": (5.0, 6, 4.5), "water": (6.0, 6, 3.5), "stairs": (3.0, 8, 7.0),
}


def rng(seed):
    tex.RNG = np.random.default_rng(seed)


def noise(scale, octaves=4, persistence=0.5):
    return tex.periodic_noise(S, S, scale, octaves, persistence)


def rgb(shade, color):
    return np.clip(shade[..., None] * np.array(color)[None, None, :], 0, 1)


def grid_lines(count, width_px, offset_rows=False):
    """Mortar/grout mask (1 on a line) for a count x count grid; offset_rows staggers every other row (bricks)."""
    y, x = np.mgrid[0:S, 0:S].astype(float)
    cell = S / count
    row = np.floor(y / cell)
    xs = x + (cell * 0.5 if offset_rows else 0.0) * (row % 2)
    on_x = (xs % cell) < width_px
    on_y = (y % cell) < width_px
    return (on_x | on_y).astype(float)


# ----------------------------------------------------------------------------- the surfaces

def make_surfaces():
    out = {}

    def save(name, base, height=None, strength=4.0):
        tex.save(base, path(f"Assets/Textures/Catalog/{name}_basecolor.png"))
        files = {"base": f"{name}_basecolor.png"}
        if height is not None:
            tex.save(tex.normal_from_height(height, strength), path(f"Assets/Textures/Catalog/{name}_normal.png"))
            files["normal"] = f"{name}_normal.png"
        out[name] = files

    # Reused, made neutral so a level's tint gives it its colour (Level 0 keeps its own yellow ones).
    def reuse(name, src, keep=0.35, bright=1.15, normal=None):
        im = np.asarray(Image.open(os.path.join(PROJECT, src)).convert("RGB").resize((S, S), Image.LANCZOS)) / 255.0
        grey = im.mean(-1, keepdims=True)
        tex.save(np.clip((grey + (im - grey) * keep) * bright, 0, 1), path(f"Assets/Textures/Catalog/{name}_basecolor.png"))
        files = {"base": f"{name}_basecolor.png"}
        if normal:
            n = Image.open(os.path.join(PROJECT, normal)).convert("RGB").resize((S, S), Image.LANCZOS)
            n.save(path(f"Assets/Textures/Catalog/{name}_normal.png"))
            files["normal"] = f"{name}_normal.png"
        out[name] = files

    reuse("wallpaper", "Assets/Textures/Level0/level0_wallpaper_basecolor.png", 0.3, 1.2, "Assets/Textures/Level0/level0_wallpaper_normal.png")
    reuse("carpet", "Assets/Textures/Level0/level0_carpet_basecolor.png", 0.3, 1.2)
    reuse("ceilingtile", "Assets/Textures/Level0/level0_ceiling_basecolor.png", 0.4, 1.05, "Assets/Textures/Level0/level0_ceiling_normal.png")
    reuse("concrete", "Assets/Textures/Level1/level1_wall_basecolor.png", 0.6, 1.0, "Assets/Textures/Level1/level1_wall_normal.png")
    reuse("metal", "Assets/Textures/Level2/level2_wall_basecolor.png", 0.8, 1.0, "Assets/Textures/Level2/level2_wall_normal.png")

    rng(301)
    n = noise(60, 5)
    save("plaster", rgb(0.82 + 0.08 * (n - 0.5), (1.0, 0.99, 0.96)), n, 1.5)

    rng(302)  # small square tiles, 4 x 4 in the texture
    g = grid_lines(4, 6)
    n = noise(30, 3)
    save("tile", rgb((0.92 + 0.04 * (n - 0.5)) * (1 - 0.35 * g), (0.97, 0.99, 1.0)), 1 - g, 3.0)

    rng(303)  # bricks, 8 rows, staggered
    y, x = np.mgrid[0:S, 0:S].astype(float)
    rows, cols = 8, 4
    ch, cw = S / rows, S / cols
    row = np.floor(y / ch)
    xs = x + cw * 0.5 * (row % 2)
    mortar = (((xs % cw) < 5) | ((y % ch) < 5)).astype(float)
    brick_id = (row * 13 + np.floor(xs / cw) * 7) % 5
    n = noise(20, 4)
    shade = (0.55 + 0.06 * brick_id / 5 + 0.12 * (n - 0.5)) * (1 - mortar) + 0.7 * mortar
    save("brick", np.where(mortar[..., None] > 0, rgb(shade, (0.85, 0.83, 0.8)), rgb(shade, (0.75, 0.38, 0.28))), 1 - mortar, 4.0)

    rng(304)
    n = noise(50, 6, 0.6)
    cracks = np.clip(1 - np.abs(noise(25, 3) - 0.5) * 18, 0, 1)
    save("rock", rgb(0.42 + 0.3 * (n - 0.5) - 0.15 * cracks, (0.86, 0.82, 0.76)), n - 0.3 * cracks, 6.0)

    rng(305)  # planks along U
    y, x = np.mgrid[0:S, 0:S].astype(float)
    plank = np.floor(y / (S / 6))
    grain = np.sin(x / 9.0 + noise(40, 3) * 12 + plank * 3.1) * 0.5 + 0.5
    seams = ((y % (S / 6)) < 3).astype(float)
    shade = (0.5 + 0.1 * grain + 0.05 * ((plank * 7) % 3)) * (1 - 0.6 * seams)
    save("wood", rgb(shade, (0.72, 0.5, 0.32)), grain * 0.3 - seams, 3.0)

    rng(306)
    n = noise(8, 4)
    patches = noise(120, 3)
    save("grass", rgb(0.45 + 0.35 * (n - 0.5) + 0.1 * (patches - 0.5), (0.45, 0.62, 0.28)), n, 2.0)

    rng(307)
    n = noise(30, 5)
    save("dirt", rgb(0.4 + 0.25 * (n - 0.5), (0.55, 0.45, 0.35)), n, 3.0)

    rng(308)
    n = noise(12, 4)
    ripples = np.sin(np.mgrid[0:S, 0:S][0] / 7.0 + noise(80, 2) * 20) * 0.5 + 0.5
    save("sand", rgb(0.78 + 0.08 * (n - 0.5), (0.95, 0.86, 0.66)), 0.6 * ripples + 0.4 * n, 1.5)

    rng(309)
    n = noise(40, 5)
    save("snow", rgb(0.93 + 0.05 * (n - 0.5), (0.95, 0.97, 1.0)), n, 1.2)

    rng(310)
    n = noise(35, 5, 0.6)
    veins = np.clip(1 - np.abs(noise(45, 3) - 0.5) * 14, 0, 1)
    save("flesh", np.clip(rgb(0.55 + 0.2 * (n - 0.5), (0.85, 0.42, 0.42)) - veins[..., None] * np.array([0.1, 0.25, 0.2]), 0, 1),
         n + 0.4 * veins, 5.0)

    rng(311)  # glass panes in thin frames
    g = grid_lines(2, 10)
    n = noise(100, 2)
    save("glass", rgb(0.55 + 0.1 * n, (0.75, 0.85, 0.88)) * (1 - g[..., None]) + rgb(np.full((S, S), 0.25), (1, 1, 1)) * g[..., None],
         1 - g, 2.0)

    rng(312)
    n = noise(6, 5, 0.6)
    save("hedge", rgb(0.3 + 0.35 * (n - 0.5), (0.3, 0.5, 0.22)), n, 6.0)

    rng(313)
    n = noise(60, 4)
    save("water", rgb(0.22 + 0.08 * (n - 0.5), (0.35, 0.55, 0.62)), n, 1.0)

    rng(314)  # sky: soft overcast clouds (an emissive ceiling far overhead)
    n = noise(150, 5, 0.55)
    save("sky", rgb(0.75 + 0.2 * (n - 0.5), (0.85, 0.88, 0.95)))
    return out


# ----------------------------------------------------------------------------- per-level materials

METERS = {  # meters one texture repeat covers (walls: 1 = one stretch per wall segment)
    "wallpaper": None, "carpet": 4.0, "ceilingtile": 2.0, "concrete": 4.0, "metal": 3.0, "plaster": 2.0, "tile": 1.2,
    "brick": 1.6, "rock": 3.0, "wood": 2.0, "grass": 3.0, "dirt": 3.0, "sand": 3.0, "snow": 3.0, "flesh": 2.0,
    "glass": 2.0, "hedge": 2.0, "water": 4.0, "sky": None,
}
ROUGH = {"metal": 0.55, "tile": 0.25, "glass": 0.08, "water": 0.05, "flesh": 0.4, "snow": 0.7}
METAL = {"metal": 0.6, "glass": 0.2}


def light_scale(light):
    return {"bright": 1.25, "normal": 1.0, "dim": 0.8, "dark": 0.6, "pitch": 0.5}[light]


def level_material(level, role, surface, files):
    cell, cells, height = LAYOUT[level["layout"]]
    chunk = cell * cells
    tint = [round(v, 3) for v in level["tint"]]
    if surface == "sky":
        tint = [round(v * light_scale(level["light"]), 3) for v in (0.9, 0.92, 1.0)]
    values = {
        "baseColor.color": tint + [1],
        "baseColor.textureMap": f"../../Textures/Catalog/{files['base']}",
        "roughness.factor": ROUGH.get(surface, 0.85),
        "metallic.factor": METAL.get(surface, 0.0),
    }
    if "normal" in files:
        values["normal.textureMap"] = f"../../Textures/Catalog/{files['normal']}"
        values["normal.factor"] = 0.7
    meters = METERS[surface]
    if surface == "sky":
        values["emissive.enable"] = True
        values["emissive.color"] = tint + [1]
        values["emissive.textureMap"] = f"../../Textures/Catalog/{files['base']}"
        values["emissive.intensity"] = 1.5 if level["light"] in ("bright", "normal") else -2.0
    if meters:
        if role == "wall":
            u, v = cell / meters, height / meters
        else:
            u = v = chunk / meters
        values["uv.tileU"] = round(max(1.0, u), 2)
        values["uv.tileV"] = round(max(1.0, v), 2)
    # Levels that come out the same share one material (one copy of everything).
    text = json.dumps({"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
                       "materialTypeVersion": 5, "propertyValues": values}, indent=4)
    import hashlib
    name = f"{surface}_{hashlib.sha1(text.encode()).hexdigest()[:8]}"
    with open(path(f"Assets/Materials/Catalog/{name}.material"), "w") as f:
        f.write(text)
    return f"assets/materials/catalog/{name}.azmaterial"


# ----------------------------------------------------------------------------- the level file

def level_file():
    with open(path("Levels/Level1/Level1.prefab")) as f:
        data = json.load(f)

    def fix(node):
        if node.get("$type") == "EndlessLevelComponent":
            node["Style"] = 4
            for key, piece in (("WallPrefab", "wall"), ("PillarPrefab", "pillar"), ("FloorPrefab", "floor"),
                               ("CeilingPrefab", "ceiling"), ("LightPrefab", "light"), ("LightFlickerPrefab", "lightflicker"),
                               ("LightDeadPrefab", "lightdead"), ("LightDyingPrefab", "lightdying")):
                node[key] = f"prefabs/level0/l0_{piece}.spawnable"
            node["PropPrefab"] = ""
            node["EnablePillars"] = True
            node["NoclipTarget"] = ""

    walk(data, fix)
    for entity in data["Entities"].values():
        if entity.get("Name") == "Level1Generator":
            entity["Name"] = "BackroomsGenerator"
    with open(path("Levels/Backrooms/Backrooms.prefab"), "w") as f:
        json.dump(data, f, indent=4)
    print("wrote Levels/Backrooms/Backrooms.prefab")


def main():
    with open(os.path.join(PROJECT, "Levels", "catalog.json")) as f:
        catalog = json.load(f)
    surfaces = make_surfaces()
    missing = set(catalog["surfaces"]) - set(surfaces)
    assert not missing, missing
    import glob
    for old in glob.glob(path("Assets/Materials/Catalog/x")[:-1] + "*.material"):
        os.remove(old)
    rows = []
    for level in catalog["levels"]:
        if level["number"] < 3:
            rows.append('        { "", "", "" },  // Level %d: its own hand-made level' % level["number"])
            continue
        mats = [level_material(level, role, level["surfaces"][role], surfaces[level["surfaces"][role]])
                for role in ("wall", "floor", "ceiling")]
        rows.append("        { " + ", ".join(f'"{m}"' for m in mats) + f" }},  // Level {level['number']}")
    header = ["// Generated by Tools/Levels/make_catalog_levels.py. Don't edit: rerun it.", "#pragma once", "",
              "namespace NoclipProtocol::Catalog", "{",
              "    //! Each level's wall (and pillar), floor and ceiling material products.",
              f"    inline constexpr const char* Materials[{len(rows)}][3] = {{"] + rows + ["    };", "} // namespace NoclipProtocol::Catalog", ""]
    with open(path("Gem/Source/Level/LevelCatalogMaterials.h"), "w") as f:
        f.write("\n".join(header))
    print(f"wrote {len(glob.glob(path('Assets/Materials/Catalog/x')[:-1] + '*.material'))} distinct level materials")
    level_file()


if __name__ == "__main__":
    main()
