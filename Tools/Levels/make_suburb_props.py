"""Level 9's street and yard dressing, the things that make it a suburb (the owner, Nov 28: "Don't forget trees ect,
things that make it a suburban"), modelled here at real size:

  from the wiki (backrooms-wiki.wikidot.com/level-9): power poles and their lines, arrow signs pointing to Level 11,
  leaves on the wet road, furnished back yards (patio set, grill, swing set, paddling pool), and a playground with tube
  structures whose round windows glow white;
  the rest of a street: hedges, bushes, picket and chain-link fences, mailboxes, wheelie bins, fire hydrants, stop signs.

  python3 Tools/Levels/make_suburb_props.py     (Blender as a module, bpy; Pillow, numpy)

Writes Assets/Models/Suburb/Props/<Prop>/<Prop>_<part>.fbx, their textures and materials (Assets/Textures/Suburb,
Assets/Materials/Suburb/prop_*.material), and Gem/Source/Level/SuburbProps.h (each prop's solid boxes; which parts
glow). Tools/Levels/make_house_prefabs.py ASSETDB then writes their prefabs too.
Local frame like the houses: x across, y the prop's front, z up; metres; the origin on the ground.
"""
import json
import math
import random
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_houses as mh  # noqa: E402
from make_houses import House, write_part  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Assets" / "Models" / "Suburb" / "Props"
TEX = ROOT / "Assets" / "Textures" / "Suburb"
MAT = ROOT / "Assets" / "Materials" / "Suburb"
HEADER = ROOT / "Gem" / "Source" / "Level" / "SuburbProps.h"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
PH = Path("/tmp/ph")

# Texture repeat per part (metres); parts not listed map 0..1 per metre.
mh.TILE.update({"wood_pole": 1.5, "white_paint": 1.0, "metal_dark": 1.0, "galvanized": 1.0, "red_paint": 1.0,
                "plastic_green": 1.0, "plastic_grey": 1.0, "plastic_white": 1.0, "plastic_red": 1.0, "plastic_yellow": 1.0,
                "plastic_blue": 1.0, "leaves": 1.0, "leaves_dry": 1.0, "chainlink": 1.0, "water": 1.0, "rubber": 1.0,
                "glow": 1.0, "sign_level11": 1.0, "sign_stop": 1.0, "sign_back": 1.0, "leaf_litter": 1.0, "wire": 1.0,
                "ceramic": 1.0})


# --- textures ------------------------------------------------------------------------------------------------------
def leaf_card(path, base, var, seed, dry=False):
    """A cluster of leaves on transparent ground, for hedges, bushes and the leaves on the road."""
    rng = random.Random(seed)
    size = 512
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for _ in range(420):
        x, y = rng.uniform(0, size), rng.uniform(0, size)
        L, W = rng.uniform(18, 34), rng.uniform(8, 14)
        a = rng.uniform(0, math.tau)
        k = rng.uniform(-var, var)
        c = tuple(int(max(0, min(255, (base[i] + k + rng.uniform(-0.03, 0.03)) * 255))) for i in range(3))
        pts = []
        for t in range(12):
            u = t / 11.0
            w = W * math.sin(math.pi * u) * 0.5
            pts.append((u * L - L / 2, w))
        pts += [(p[0], -p[1]) for p in reversed(pts)]
        pts = [(x + px * math.cos(a) - py * math.sin(a), y + px * math.sin(a) + py * math.cos(a)) for px, py in pts]
        d.polygon(pts, fill=c + (255,))
    im = im.filter(ImageFilter.SMOOTH)
    im.save(path)


def chainlink(path):
    size = 256
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    step = 32
    for i in range(-size, size * 2, step):
        d.line([(i, 0), (i + size, size)], fill=(150, 152, 150, 255), width=4)
        d.line([(i, size), (i + size, 0)], fill=(150, 152, 150, 255), width=4)
    im.save(path)


def sign(path, kind):
    w, h = 512, 256 if kind != "stop" else 512
    if kind == "stop":
        im = Image.new("RGB", (w, h), (176, 20, 24))
        d = ImageDraw.Draw(im)
        f = ImageFont.truetype(FONT, 150)
        d.text((w / 2, h / 2), "STOP", fill=(240, 240, 236), font=f, anchor="mm")
        d.rectangle([12, 12, w - 12, h - 12], outline=(240, 240, 236), width=14)
    else:
        # Street-sign green, a white arrow and the way it points: Level 11 (the wiki's arrow signs).
        im = Image.new("RGB", (w, h), (18, 96, 52))
        d = ImageDraw.Draw(im)
        d.rectangle([8, 8, w - 8, h - 8], outline=(236, 236, 232), width=8)
        f = ImageFont.truetype(FONT, 72)
        d.text((40, h / 2), "LEVEL 11", fill=(236, 236, 232), font=f, anchor="lm")
        d.polygon([(380, 78), (450, 128), (380, 178), (380, 150), (330, 150), (330, 106), (380, 106)], fill=(236, 236, 232))
    # A little weathering.
    arr = np.asarray(im).astype(np.float32) / 255.0
    rng = np.random.default_rng(7)
    noise = rng.normal(0, 0.03, arr.shape[:2])[..., None]
    Image.fromarray((np.clip(arr * (0.92 + noise), 0, 1) * 255).astype(np.uint8)).save(path)


def material(name, props):
    data = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
            "materialTypeVersion": 5, "propertyValues": dict({"metallic.factor": 0.0}, **props)}
    json.dump(data, open(MAT / f"prop_{name}.material", "w"), indent=4)


def flat(color, rough, metal=0.0, extra=None):
    p = {"baseColor.color": [color[0], color[1], color[2], 1.0], "roughness.factor": rough, "metallic.factor": metal}
    p.update(extra or {})
    return p


def textures():
    TEX.mkdir(parents=True, exist_ok=True)
    MAT.mkdir(parents=True, exist_ok=True)
    leaf_card(TEX / "prop_leaves.png", (0.13, 0.22, 0.08), 0.05, "green")
    leaf_card(TEX / "prop_leaves_dry.png", (0.32, 0.25, 0.12), 0.07, "dry")
    leaf_card(TEX / "prop_leaf_litter.png", (0.30, 0.20, 0.09), 0.10, "litter")
    chainlink(TEX / "prop_chainlink.png")
    sign(TEX / "prop_sign_level11.png", "arrow")
    sign(TEX / "prop_sign_stop.png", "stop")
    cut = {"opacity.mode": "Cutout", "opacity.factor": 0.5, "general.doubleSided": True}
    material("leaves", dict({"baseColor.textureMap": "../../Textures/Suburb/prop_leaves.png", "roughness.factor": 0.7,
                             "opacity.alphaSource": "Packed"}, **cut))
    material("leaves_dry", dict({"baseColor.textureMap": "../../Textures/Suburb/prop_leaves_dry.png", "roughness.factor": 0.8,
                                 "opacity.alphaSource": "Packed"}, **cut))
    material("leaf_litter", dict({"baseColor.textureMap": "../../Textures/Suburb/prop_leaf_litter.png", "roughness.factor": 0.45,
                                  "opacity.alphaSource": "Packed"}, **cut))   # wet leaves: a little shiny
    material("chainlink", dict({"baseColor.textureMap": "../../Textures/Suburb/prop_chainlink.png", "roughness.factor": 0.4,
                                "metallic.factor": 0.8, "opacity.alphaSource": "Packed"}, **cut))
    material("sign_level11", {"baseColor.textureMap": "../../Textures/Suburb/prop_sign_level11.png", "roughness.factor": 0.35,
                              "specularF0.factor": 0.6})
    material("sign_stop", {"baseColor.textureMap": "../../Textures/Suburb/prop_sign_stop.png", "roughness.factor": 0.35,
                           "specularF0.factor": 0.6})
    # The pole: the scanned weathered wood, darkened (creosote).
    pole = Image.open(PH / "weathered_planks_diff_2k.png").convert("RGB").resize((512, 512)) if (PH / "weathered_planks_diff_2k.png").exists() else None
    if pole is not None:
        arr = np.asarray(pole).astype(np.float32) / 255.0 * np.array([0.55, 0.48, 0.42])
        Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8)).save(TEX / "prop_wood_pole.png")
        material("wood_pole", {"baseColor.textureMap": "../../Textures/Suburb/prop_wood_pole.png", "roughness.factor": 0.85})
    else:
        material("wood_pole", flat((0.22, 0.17, 0.12), 0.85))
    material("white_paint", flat((0.80, 0.79, 0.75), 0.55))
    material("metal_dark", flat((0.05, 0.05, 0.05), 0.45, 0.6))
    material("galvanized", flat((0.55, 0.56, 0.56), 0.35, 1.0))
    material("red_paint", flat((0.55, 0.06, 0.04), 0.4))
    material("plastic_green", flat((0.10, 0.25, 0.14), 0.5))
    material("plastic_grey", flat((0.22, 0.23, 0.24), 0.5))
    material("plastic_white", flat((0.78, 0.78, 0.75), 0.45))
    material("plastic_red", flat((0.60, 0.08, 0.06), 0.4))
    material("plastic_yellow", flat((0.75, 0.58, 0.08), 0.4))
    material("plastic_blue", flat((0.10, 0.28, 0.62), 0.35))
    material("water", flat((0.03, 0.06, 0.07), 0.02, 0.0, {"specularF0.factor": 0.6}))
    material("rubber", flat((0.03, 0.03, 0.03), 0.8))
    material("ceramic", flat((0.50, 0.45, 0.36), 0.3))
    material("wire", flat((0.02, 0.02, 0.02), 0.5, 0.3))
    material("sign_back", flat((0.55, 0.56, 0.56), 0.4, 1.0))
    # The playground's windows: white light, the only thing glowing on the street (the wiki).
    material("glow", flat((0.9, 0.92, 0.95), 0.5, 0.0, {"emissive.enable": True, "emissive.color": [0.92, 0.95, 1.0, 1.0],
                                                        "emissive.intensity": 6.0, "emissive.unit": "Ev100"}))


# --- shape helpers -----------------------------------------------------------------------------------------------
def cyl(k, part, x, y, z0, z1, r, n=12, axis="z", solid=False):
    """A cylinder; axis 'z' (upright from z0 to z1 at x, y), 'x' or 'y' (lying, centred at x/y/z0, length z1)."""
    pts0, pts1 = [], []
    for i in range(n):
        a = math.tau * i / n
        c, s = math.cos(a) * r, math.sin(a) * r
        if axis == "z":
            pts0.append((x + c, y + s, z0)); pts1.append((x + c, y + s, z1))
        elif axis == "x":
            pts0.append((x - z1 / 2, y + c, z0 + s)); pts1.append((x + z1 / 2, y + c, z0 + s))
        else:
            pts0.append((x + c, y - z1 / 2, z0 + s)); pts1.append((x + c, y + z1 / 2, z0 + s))
    verts = pts0 + pts1
    faces = []
    for i in range(n):
        j = (i + 1) % n
        u0, u1 = i / n, (i + 1) / n
        faces.append(((i, j, n + j, n + i), [(u0 * 2, 0), (u1 * 2, 0), (u1 * 2, 1), (u0 * 2, 1)]))
    faces.append((tuple(range(n - 1, -1, -1)), [(0.5 + math.cos(math.tau * i / n) / 2, 0.5 + math.sin(math.tau * i / n) / 2) for i in range(n - 1, -1, -1)]))
    faces.append((tuple(range(n, 2 * n)), [(0.5 + math.cos(math.tau * i / n) / 2, 0.5 + math.sin(math.tau * i / n) / 2) for i in range(n)]))
    if axis != "z":
        # Lying cylinders: wind outward-facing for their orientation.
        faces = [(idx[::-1], uv[::-1]) for idx, uv in faces]
    k.parts.setdefault(part, []).append((verts, faces))
    if solid:
        if axis == "z":
            k.solids.append((x, y, (z0 + z1) / 2, 2 * r, 2 * r, z1 - z0))
        elif axis == "x":
            k.solids.append((x, y, z0, z1, 2 * r, 2 * r))
        else:
            k.solids.append((x, y, z0, 2 * r, z1, 2 * r))


def card(k, part, cx, cy, cz, w, h, yaw, tilt=0.0):
    """A leaf card: a w x h quad standing up (tilt 0) or lying flat (tilt 90 degrees), turned by yaw."""
    c, s = math.cos(yaw), math.sin(yaw)
    ct, st = math.cos(tilt), math.sin(tilt)
    pts = []
    for (u, v) in ((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)):
        lx, ly, lz = u * w, -v * h * st, v * h * ct
        pts.append((cx + lx * c - ly * s, cy + lx * s + ly * c, cz + lz))
    k.parts.setdefault(part, []).append((pts, [((0, 1, 2, 3), [(0, 0), (1, 0), (1, 1), (0, 1)])]))


def foliage(k, part, x0, x1, y0, y1, z0, z1, density, seed):
    """Leafy mass filling a box: crossed leaf cards at random, so it reads as a hedge or bush from every side."""
    rng = random.Random(seed)
    vol = (x1 - x0) * (y1 - y0) * (z1 - z0)
    for _ in range(int(vol * density)):
        x, y, z = rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(z0, z1)
        size = rng.uniform(0.45, 0.75)
        yaw = rng.uniform(0, math.pi)
        card(k, part, x, y, z, size, size, yaw, rng.uniform(-0.6, 0.6))
        card(k, part, x, y, z, size, size, yaw + math.pi / 2, rng.uniform(-0.6, 0.6))


# --- the props ---------------------------------------------------------------------------------------------------
def power_pole():
    k = House()
    cyl(k, "wood_pole", 0, 0, 0, 10.0, 0.15, solid=True)
    k.box("wood_pole", -1.2, -0.06, 9.1, 1.2, 0.06, 9.24)            # cross-arm, across the street's line (x)
    for x in (-1.05, 0.0, 1.05):
        cyl(k, "ceramic", x, 0, 9.24, 9.42, 0.05, n=8)
    k.box("metal_dark", -0.03, -0.03, 8.6, 0.03, 0.4, 8.66)          # a brace
    return k


def power_pole_transformer():
    k = power_pole()
    cyl(k, "galvanized", 0, 0.42, 7.4, 8.5, 0.26, n=14)              # the can on the pole
    k.box("metal_dark", -0.05, 0.12, 7.9, 0.05, 0.2, 8.0)
    return k


def wire():
    """One metre of power line along y, stretched by the game between two poles (NonUniformScale)."""
    k = House()
    cyl(k, "wire", 0, 0, 0, 1.0, 0.012, n=6, axis="y")
    return k


def arrow_sign():
    k = House()
    cyl(k, "galvanized", 0, 0, 0, 2.9, 0.035, n=8, solid=True)
    k.box("sign_level11", -0.06, -0.012, 2.4, 0.86, 0.0, 2.8, fit=True)
    k.box("sign_back", -0.06, 0.0, 2.4, 0.86, 0.012, 2.8)
    return k


def stop_sign():
    k = House()
    cyl(k, "galvanized", 0, 0, 0, 2.6, 0.035, n=8, solid=True)
    # An octagon face, the stop texture across it.
    r = 0.38
    pts = [(math.cos(math.tau * i / 8 + math.pi / 8) * r, -0.015, 2.3 + math.sin(math.tau * i / 8 + math.pi / 8) * r) for i in range(8)]
    uv = [(0.5 + (p[0]) / (2 * r), 0.5 + (p[2] - 2.3) / (2 * r)) for p in pts]
    k.parts.setdefault("sign_stop", []).append((pts, [(tuple(range(8))[::-1], uv[::-1])]))
    back = [(p[0], -0.005, p[2]) for p in pts]
    k.parts.setdefault("sign_back", []).append((back, [(tuple(range(8)), [(0, 0)] * 8)]))
    return k


def mailbox():
    k = House()
    k.box("wood_pole", -0.05, -0.05, 0.0, 0.05, 0.05, 1.0, solid=True)
    k.box("metal_dark", -0.12, -0.26, 1.0, 0.12, 0.26, 1.16, solid=True)
    cyl(k, "metal_dark", 0, 0, 1.16, 0.52, 0.12, n=12, axis="y")
    k.box("red_paint", 0.12, -0.05, 1.15, 0.135, -0.03, 1.38)        # the flag, up
    return k


def hydrant():
    k = House()
    cyl(k, "red_paint", 0, 0, 0.0, 0.08, 0.16, n=14)
    cyl(k, "red_paint", 0, 0, 0.08, 0.62, 0.11, n=14, solid=True)
    cyl(k, "red_paint", 0, 0, 0.62, 0.70, 0.13, n=14)
    cyl(k, "red_paint", 0, 0, 0.70, 0.80, 0.07, n=10)
    cyl(k, "red_paint", 0, 0, 0.45, 0.42, 0.05, n=8, axis="x")
    cyl(k, "red_paint", 0, 0.12, 0.42, 0.12, 0.06, n=8, axis="y")
    return k


def wheelie_bin(colour):
    k = House()
    k.box(colour, -0.29, -0.36, 0.06, 0.29, 0.36, 1.0, solid=True)
    k.box(colour, -0.31, -0.40, 1.0, 0.31, 0.40, 1.06)               # lid
    k.box(colour, -0.25, -0.43, 0.9, 0.25, -0.36, 0.97)              # handle
    cyl(k, "rubber", 0.0, -0.3, 0.1, 0.62, 0.1, n=10, axis="x")       # wheels
    return k


def picket_fence():
    """2.5 m of white picket fence along x."""
    k = House()
    for i in range(18):
        x = -1.25 + 0.07 + i * (2.5 - 0.14) / 17
        k.box("white_paint", x - 0.04, -0.012, 0.0, x + 0.04, 0.012, 0.95)
        # A point on each picket.
        k.parts["white_paint"].append(([(x - 0.04, -0.012, 0.95), (x + 0.04, -0.012, 0.95), (x, -0.012, 1.03)],
                                       [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
        k.parts["white_paint"].append(([(x + 0.04, 0.012, 0.95), (x - 0.04, 0.012, 0.95), (x, 0.012, 1.03)],
                                       [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
    for z in (0.25, 0.75):
        k.box("white_paint", -1.25, 0.012, z - 0.04, 1.25, 0.04, z + 0.04)
    k.box("white_paint", -1.25, 0.012, 0.0, -1.18, 0.08, 1.05)
    k.solids.append((0.0, 0.0, 0.5, 2.5, 0.12, 1.0))
    return k


def chain_fence():
    k = House()
    for x in (-1.25, 1.25):
        cyl(k, "galvanized", x, 0, 0, 1.25, 0.03, n=8)
    cyl(k, "galvanized", 0, 0, 1.22, 2.5, 0.02, n=8, axis="x")
    k.box("chainlink", -1.25, -0.005, 0.03, 1.25, 0.005, 1.22, fit=True)
    k.solids.append((0.0, 0.0, 0.62, 2.5, 0.1, 1.25))
    # The link pattern repeats every 0.2 m: scale the UVs from the 0..1 fit.
    verts, faces = k.parts["chainlink"][-1]
    k.parts["chainlink"][-1] = (verts, [(idx, [(u * 12.5, v * 6.0) for (u, v) in uv]) for idx, uv in faces])
    return k


def hedge(part, seed):
    """2.5 m of hedge along x, 0.9 m deep, 1.2 m high, the leaves round a dark core."""
    k = House()
    k.box("plastic_green" if part == "leaves" else "rubber", -1.15, -0.32, 0.0, 1.15, 0.32, 1.05)
    foliage(k, part, -1.25, 1.25, -0.45, 0.45, 0.05, 1.2, 26, seed)
    k.solids.append((0.0, 0.0, 0.6, 2.5, 0.9, 1.2))
    return k


def bush(part, seed):
    k = House()
    k.box("plastic_green" if part == "leaves" else "rubber", -0.35, -0.35, 0.0, 0.35, 0.35, 0.7)
    foliage(k, part, -0.6, 0.6, -0.6, 0.6, 0.05, 1.0, 30, seed)
    k.solids.append((0.0, 0.0, 0.45, 1.0, 1.0, 0.9))
    return k


def leaf_litter(seed):
    """Wet leaves lying in a 2 x 2 m drift (the wiki: the road partly covered with leaves)."""
    k = House()
    rng = random.Random(seed)
    for _ in range(14):
        card(k, "leaf_litter", rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), 0.012 + rng.uniform(0, 0.006), rng.uniform(0.5, 0.9),
             rng.uniform(0.5, 0.9), rng.uniform(0, math.tau), math.pi / 2)
    return k


def patio_set():
    k = House()
    cyl(k, "plastic_white", 0, 0, 0.70, 0.74, 0.5, n=20, solid=True)   # round table top
    cyl(k, "plastic_white", 0, 0, 0.0, 0.70, 0.04, n=8)
    cyl(k, "plastic_white", 0, 0, 0.0, 0.03, 0.3, n=12)
    for i in range(4):
        a = math.tau * i / 4 + 0.3
        cx, cy = math.cos(a) * 0.9, math.sin(a) * 0.9
        k.box("plastic_white", cx - 0.22, cy - 0.22, 0.42, cx + 0.22, cy + 0.22, 0.46, solid=True)
        for lx, ly in ((-0.18, -0.18), (0.18, -0.18), (0.18, 0.18), (-0.18, 0.18)):
            k.box("plastic_white", cx + lx - 0.02, cy + ly - 0.02, 0.0, cx + lx + 0.02, cy + ly + 0.02, 0.42)
        bx, by = cx + math.cos(a) * 0.22, cy + math.sin(a) * 0.22
        k.box("plastic_white", bx - 0.2, by - 0.2, 0.46, bx + 0.2, by + 0.2, 0.9) if False else None
        # chair back: a thin board on the far side
        ox, oy = math.cos(a), math.sin(a)
        if abs(ox) > abs(oy):
            k.box("plastic_white", bx - 0.02, cy - 0.21, 0.46, bx + 0.02, cy + 0.21, 0.9)
        else:
            k.box("plastic_white", cx - 0.21, by - 0.02, 0.46, cx + 0.21, by + 0.02, 0.9)
    return k


def grill():
    k = House()
    for (x, y) in ((-0.2, -0.15), (0.2, -0.15), (0.0, 0.22)):
        k.box("metal_dark", x - 0.015, y - 0.015, 0.0, x + 0.015, y + 0.015, 0.68)
    # The kettle: a bowl and its lid, stacked cylinders narrowing.
    for i, (z0, z1, r) in enumerate(((0.62, 0.70, 0.22), (0.70, 0.82, 0.29), (0.82, 0.92, 0.29), (0.92, 1.0, 0.24), (1.0, 1.04, 0.14))):
        cyl(k, "metal_dark", 0, 0, z0, z1, r, n=16)
    cyl(k, "metal_dark", 0, 0, 1.04, 1.1, 0.03, n=6)
    k.solids.append((0.0, 0.0, 0.5, 0.6, 0.6, 1.0))
    return k


def swing_set():
    k = House()
    for x in (-1.6, 1.6):
        for y in (-0.8, 0.8):
            # Legs leaning in to the top bar (as boxes stepped up, a lean of 0.8 m over 2.2 m).
            for j in range(8):
                t0, t1 = j / 8, (j + 1) / 8
                yy = y * (1 - (t0 + t1) / 2)
                k.box("galvanized", x - 0.03, yy - 0.03, 2.2 * t0, x + 0.03, yy + 0.03, 2.2 * t1)
        k.solids.append((x, 0.0, 1.1, 0.15, 1.7, 2.2))
    cyl(k, "galvanized", 0, 0, 2.2, 3.4, 0.04, n=10, axis="x")
    for sx in (-0.7, 0.7):
        for cx in (sx - 0.22, sx + 0.22):
            k.box("galvanized", cx - 0.006, -0.006, 0.45, cx + 0.006, 0.006, 2.18)   # chains
        k.box("rubber", sx - 0.25, -0.1, 0.42, sx + 0.25, 0.1, 0.46)                  # seats
    return k


def paddling_pool():
    k = House()
    n = 24
    for i in range(n):
        a0, a1 = math.tau * i / n, math.tau * (i + 1) / n
        x0, y0, x1, y1 = math.cos(a0) * 1.0, math.sin(a0) * 1.0, math.cos(a1) * 1.0, math.sin(a1) * 1.0
        k.parts.setdefault("plastic_blue", []).append(([(x0, y0, 0), (x1, y1, 0), (x1, y1, 0.3), (x0, y0, 0.3)],
                                                        [((0, 1, 2, 3), [(0, 0), (1, 0), (1, 1), (0, 1)])]))
        k.parts["plastic_blue"].append(([(x1 * 0.92, y1 * 0.92, 0), (x0 * 0.92, y0 * 0.92, 0), (x0 * 0.92, y0 * 0.92, 0.3), (x1 * 0.92, y1 * 0.92, 0.3)],
                                        [((0, 1, 2, 3), [(0, 0), (1, 0), (1, 1), (0, 1)])]))
        k.parts["plastic_blue"].append(([(x0, y0, 0.3), (x1, y1, 0.3), (x1 * 0.92, y1 * 0.92, 0.3), (x0 * 0.92, y0 * 0.92, 0.3)],
                                        [((0, 1, 2, 3), [(0, 0), (1, 0), (1, 1), (0, 1)])]))
    cyl(k, "water", 0, 0, 0.0, 0.18, 0.93, n=24)
    k.solids.append((0.0, 0.0, 0.15, 1.8, 1.8, 0.3))
    return k


def playground():
    """The wiki's playground: a tower and long tube structures, their round windows glowing white."""
    k = House()
    # Tower: four posts, a deck at 1.5 m, a roof at 3.2 m, a ladder, and a slide off the front.
    for x in (-0.9, 0.9):
        for y in (-0.9, 0.9):
            k.box("plastic_yellow", x - 0.07, y - 0.07, 0.0, x + 0.07, y + 0.07, 3.2, solid=True)
    k.box("plastic_grey", -1.0, -1.0, 1.45, 1.0, 1.0, 1.55)
    k.box("plastic_red", -1.15, -1.15, 3.2, 1.15, 1.15, 3.32)
    k.parts["plastic_red"].append(([(-1.15, -1.15, 3.32), (1.15, -1.15, 3.32), (0, 0, 4.0)], [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
    k.parts["plastic_red"].append(([(1.15, -1.15, 3.32), (1.15, 1.15, 3.32), (0, 0, 4.0)], [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
    k.parts["plastic_red"].append(([(1.15, 1.15, 3.32), (-1.15, 1.15, 3.32), (0, 0, 4.0)], [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
    k.parts["plastic_red"].append(([(-1.15, 1.15, 3.32), (-1.15, -1.15, 3.32), (0, 0, 4.0)], [((0, 1, 2), [(0, 0), (1, 0), (0.5, 1)])]))
    for i in range(5):
        k.box("galvanized", -0.35, -1.0 - 0.03, 0.3 * (i + 1) - 0.02, 0.35, -1.0 + 0.03, 0.3 * (i + 1) + 0.02)
    for x in (-0.38, 0.38):
        k.box("galvanized", x - 0.025, -1.03, 0.0, x + 0.025, -0.97, 1.6)
    for j in range(10):
        t0, t1 = j / 10, (j + 1) / 10
        y0, y1 = 1.0 + t0 * 3.0, 1.0 + t1 * 3.0
        z = 1.5 * (1 - (t0 + t1) / 2)
        k.box("plastic_red", -0.35, y0, z - 0.03, 0.35, y1, z + 0.03)
        k.box("plastic_red", -0.38, y0, z, -0.33, y1, z + 0.25)
        k.box("plastic_red", 0.33, y0, z, 0.38, y1, z + 0.25)
    k.solids.append((0.0, 2.5, 0.5, 0.8, 3.0, 1.0))
    # Two tubes off the deck, one level and one climbing to a second tower; round windows along them glow white.
    def tube(x0, x1, z, r=0.5):
        n = 16
        length = x1 - x0
        cyl(k, "plastic_blue", (x0 + x1) / 2, 0.0, z, length, r, n=n, axis="x", solid=True)
        steps = int(length / 1.0)
        for i in range(steps):
            x = x0 + 0.5 + i * (length - 1.0) / max(1, steps - 1)
            for side in (-1, 1):
                pts = []
                for t in range(12):
                    a = math.tau * t / 12
                    pts.append((x + math.cos(a) * 0.16, side * (r + 0.012), z + math.sin(a) * 0.16))
                uv = [(0.5, 0.5)] * 12
                k.parts.setdefault("glow", []).append((pts if side > 0 else pts[::-1], [(tuple(range(12)), uv)]))
    tube(1.0, 7.0, 2.0)
    for x in (6.5, 8.5):
        for y in (-0.9, 0.9):
            k.box("plastic_yellow", x - 0.07, y - 0.07, 0.0, x + 0.07, y + 0.07, 3.2, solid=True)
    k.box("plastic_grey", 6.4, -1.0, 1.45, 8.6, 1.0, 1.55)
    tube(-7.0, -1.0, 1.3)
    for x in (-7.5, -6.6):
        k.box("plastic_yellow", x - 0.07, -0.07, 0.0, x + 0.07, 0.07, 1.3, solid=True)
    return k


PROPS = {
    "PowerPole": power_pole, "PowerPoleTransformer": power_pole_transformer, "PowerWire": wire,
    "ArrowSign": arrow_sign, "StopSign": stop_sign, "Mailbox": mailbox, "Hydrant": hydrant,
    "BinGreen": lambda: wheelie_bin("plastic_green"), "BinGrey": lambda: wheelie_bin("plastic_grey"),
    "PicketFence": picket_fence, "ChainFence": chain_fence,
    "Hedge": lambda: hedge("leaves", "hedge"), "HedgeDry": lambda: hedge("leaves_dry", "hedge-dry"),
    "Bush": lambda: bush("leaves", "bush"), "BushDry": lambda: bush("leaves_dry", "bush-dry"),
    "LeafLitter": lambda: leaf_litter("litter-a"), "LeafLitterB": lambda: leaf_litter("litter-b"),
    "PatioSet": patio_set, "Grill": grill, "SwingSet": swing_set, "PaddlingPool": paddling_pool, "Playground": playground,
}
STRETCH = {"PowerWire"}  # scaled by the game (NonUniformScale)


def main():
    textures()
    lines = []
    manifest = {}
    mh.OUT = OUT
    for name, make in PROPS.items():
        k = make()
        for part, items in k.parts.items():
            write_part(name, part, items)
        manifest[name] = {"parts": sorted(k.parts), "stretch": name in STRETCH}
        solids = ", ".join(f"{{ {cx:.3f}f, {cy:.3f}f, {cz:.3f}f, {sx:.3f}f, {sy:.3f}f, {sz:.3f}f }}" for (cx, cy, cz, sx, sy, sz) in k.solids)
        lines.append((name, solids, len(k.solids)))
        print(name, {p: len(i) for p, i in k.parts.items()}, len(k.solids), "solids")
    (OUT / "props.json").write_text(json.dumps(manifest, indent=2) + "\n")
    with open(HEADER, "w") as f:
        f.write("#pragma once\n\n// Generated by Tools/Levels/make_suburb_props.py: Level 9's street and yard props, their solid boxes.\n")
        f.write('#include "SuburbHouses.h"\n\nnamespace NoclipProtocol::Suburb\n{\n    enum Prop : int\n    {\n')
        for i, (name, _, _) in enumerate(lines):
            f.write(f"        Prop{name} = {i},\n")
        f.write(f"        PropCount = {len(lines)}\n    }};\n\n")
        for (name, solids, n) in lines:
            if n:
                f.write(f"    constexpr LocalBox {name}PropSolids[{n}] = {{ {solids} }};\n")
        f.write("\n    struct PropInfo { const char* m_prefab; const LocalBox* m_solids; int m_solidCount; };\n")
        f.write("    constexpr PropInfo Props[PropCount] = {\n")
        for (name, solids, n) in lines:
            f.write(f'        {{ "prefabs/suburb/props/{name.lower()}.spawnable", {name + "PropSolids" if n else "nullptr"}, {n} }},\n')
        f.write("    };\n} // namespace NoclipProtocol::Suburb\n")
    print("wrote", HEADER.relative_to(ROOT))


if __name__ == "__main__":
    main()
