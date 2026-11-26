#!/usr/bin/env python3
"""
Builds the clutter: everything that has ended up in the Backrooms.

    python3 Tools/Clutter/make_clutter.py        (run from the project folder)

Writes
  Assets/Materials/Clutter/*.material     plain colored materials (Tripo3D models replace these props later)
  Prefabs/Clutter/<Name>.prefab           one prefab per prop, built from boxes, cylinders and spheres
  Gem/Source/Level/ClutterCatalog.h       the list the level code places from (sizes, where they stand, how common)

Every prop is authored at its real size, standing on the floor at its origin, its front facing +Y. Wall props
stand with their back to a wall. Props that block the player get one invisible box collider over their footprint
(cheaper than one per part); small things get none, so you never snag on a tin can.

Where a prop can go:
  wall   back against a wall (shelves, desks, vending machines)
  free   anywhere in the open part of a cell (chairs, lamps, a door standing on its own)
  big    large things, only in open cells with no walls (cars, beds, a bathtub)
  small  little things on the floor, no collider (cans, papers, a teddy bear)
Weights are per level: (Level 0 lobby, Level 1 garage, Level 2 tunnels). 0 = never there.
"""

import copy
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Levels"))
import make_levels_1_2 as lv  # noqa: E402

path = lv.path
asset_guid = lv.asset_guid

# The engine's primitive models (guid from the source path, sub IDs read from the asset catalog).
SHAPES = {
    "box": ({"guid": "{C15CD465-9589-56ED-945F-8416FA4798A3}", "subId": 284301017}, "objects/_primitives/_box_1x1.fbx.azmodel", 1.0),
    "cyl": ({"guid": "{F322592F-43BC-50E7-903C-CC231846093F}", "subId": 276443623}, "objects/_primitives/_cylinder_1x1.fbx.azmodel", 1.0),
    "sph": ({"guid": "{1EE46427-D1C2-5698-B30D-B4CCF46B15CD}", "subId": 274435692}, "objects/_primitives/_sphere_1x1.fbx.azmodel", 1.0),
}

MATERIALS = {
    #  name            base color              metal rough  emissive (color, intensity)
    "wood_light":     ([0.55, 0.40, 0.25], 0.0, 0.6, None),
    "wood_dark":      ([0.22, 0.13, 0.07], 0.0, 0.55, None),
    "metal":          ([0.45, 0.46, 0.47], 0.8, 0.4, None),
    "metal_dark":     ([0.12, 0.12, 0.13], 0.6, 0.5, None),
    "rust":           ([0.33, 0.17, 0.08], 0.5, 0.85, None),
    "beige_plastic":  ([0.72, 0.68, 0.58], 0.0, 0.5, None),
    "black_plastic":  ([0.04, 0.04, 0.04], 0.0, 0.4, None),
    "white_plastic":  ([0.82, 0.82, 0.8], 0.0, 0.45, None),
    "fabric_blue":    ([0.12, 0.16, 0.30], 0.0, 0.95, None),
    "fabric_brown":   ([0.30, 0.20, 0.12], 0.0, 0.95, None),
    "fabric_green":   ([0.20, 0.28, 0.16], 0.0, 0.95, None),
    "fabric_red":     ([0.40, 0.08, 0.07], 0.0, 0.95, None),
    "cardboard":      ([0.50, 0.36, 0.20], 0.0, 0.9, None),
    "paper":          ([0.85, 0.83, 0.76], 0.0, 0.9, None),
    "mattress":       ([0.68, 0.64, 0.52], 0.0, 0.95, None),
    "screen":         ([0.02, 0.03, 0.03], 0.0, 0.1, None),
    "screen_glow":    ([0.10, 0.25, 0.20], 0.0, 0.2, ([0.35, 0.85, 0.65], 1.2)),
    "vending_glow":   ([0.80, 0.80, 0.70], 0.0, 0.2, ([1.0, 0.92, 0.75], 1.5)),
    "lamp_glow":      ([1.00, 0.85, 0.60], 0.0, 0.5, ([1.0, 0.8, 0.5], 2.0)),
    "vending_red":    ([0.55, 0.06, 0.05], 0.2, 0.4, None),
    "appliance":      ([0.80, 0.80, 0.78], 0.1, 0.3, None),
    "porcelain":      ([0.90, 0.90, 0.88], 0.0, 0.15, None),
    "orange":         ([0.90, 0.35, 0.05], 0.0, 0.5, None),
    "yellow":         ([0.80, 0.62, 0.08], 0.2, 0.5, None),
    "green_paint":    ([0.20, 0.30, 0.22], 0.4, 0.5, None),
    "red_paint":      ([0.60, 0.08, 0.06], 0.3, 0.45, None),
    "blue_paint":     ([0.08, 0.20, 0.45], 0.3, 0.45, None),
    "rubber":         ([0.03, 0.03, 0.03], 0.0, 0.9, None),
    "concrete":       ([0.50, 0.50, 0.48], 0.0, 0.9, None),
    "water_blue":     ([0.30, 0.50, 0.80], 0.0, 0.05, None),
    "plant_dead":     ([0.30, 0.27, 0.12], 0.0, 0.9, None),
    "mannequin":      ([0.78, 0.70, 0.60], 0.0, 0.4, None),
    "teddy":          ([0.40, 0.25, 0.12], 0.0, 1.0, None),
    "car_blue":       ([0.10, 0.18, 0.38], 0.6, 0.35, None),
    "car_red":        ([0.45, 0.05, 0.04], 0.6, 0.35, None),
    "car_grey":       ([0.35, 0.36, 0.37], 0.6, 0.35, None),
    "glass_dark":     ([0.05, 0.07, 0.08], 0.2, 0.05, None),
    "brass":          ([0.60, 0.45, 0.18], 1.0, 0.35, None),
    "ceiling_tile":   ([0.76, 0.72, 0.58], 0.0, 0.9, None),
    "mtf_uniform":    ([0.08, 0.09, 0.08], 0.0, 0.9, None),
    "mtf_vest":       ([0.16, 0.17, 0.13], 0.0, 0.8, None),
    "mtf_patch":      ([0.75, 0.62, 0.18], 0.3, 0.5, None),
}


def material_guid(name):
    return asset_guid(f"Assets/Materials/Clutter/{name}.material")


def write_materials():
    for name, (color, metal, rough, emissive) in MATERIALS.items():
        props = {"baseColor.color": color + [1.0], "metallic.factor": metal, "roughness.factor": rough}
        if emissive:
            props.update({"emissive.enable": True, "emissive.color": emissive[0] + [1.0], "emissive.intensity": emissive[1]})
        data = {
            "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
            "materialTypeVersion": 5,
            "propertyValues": props,
        }
        with open(path(f"Assets/Materials/Clutter/{name}.material"), "w") as f:
            json.dump(data, f, indent=4)


# ----------------------------------------------------------------------------- parts

def B(x, y, z, sx, sy, sz, mat, rot=None):
    """A box by its center and size."""
    return ("box", (x, y, z), (sx, sy, sz), mat, rot)


def F(x, y, z0, sx, sy, sz, mat, rot=None):
    """A box standing on height z0 (its bottom), by its xy center and size."""
    return ("box", (x, y, z0 + sz / 2), (sx, sy, sz), mat, rot)


def C(x, y, z0, d, h, mat, rot=None):
    """An upright cylinder: bottom at z0, diameter d, height h (rot turns it, e.g. [90, 0, 0] lies it along Y)."""
    if rot:
        return ("cyl", (x, y, z0), (d, d, h), mat, rot)
    return ("cyl", (x, y, z0 + h / 2), (d, d, h), mat, None)


def S(x, y, z, d, mat, sz=None):
    """A round lump centered at z (heads, knobs, wheels): a short cylinder. The engine's sphere primitive isn't 1 m
    across like the box and cylinder (its size data says 100), and a 6 cm knob came out tens of meters wide."""
    h = sz or d
    return ("cyl", (x, y, z), (d, d, h * 0.9), mat, None)


def legs4(w, d, h, t, mat, inset=0.03, z0=0.0):
    out = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            out.append(F(sx * (w / 2 - t / 2 - inset), sy * (d / 2 - t / 2 - inset), z0, t, t, h, mat))
    return out


# ----------------------------------------------------------------------------- the props

PROPS = {}


def bounds(parts):
    """Width (x), depth (y) and height of a prop from its parts, centered on the origin (turned parts included)."""
    import math
    mx = my = mz = 0.0
    for _shape, center, size, _mat, rot in parts:
        rx, ry, rz = [math.radians(a) for a in (rot or (0, 0, 0))]
        cx, sx_, cy, sy_, cz, sz_ = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
        for dx in (-0.5, 0.5):
            for dy in (-0.5, 0.5):
                for dz in (-0.5, 0.5):
                    x, y, z = dx * size[0], dy * size[1], dz * size[2]
                    y, z = y * cx - z * sx_, y * sx_ + z * cx      # about X
                    x, z = x * cy + z * sy_, -x * sy_ + z * cy     # about Y
                    x, y = x * cz - y * sz_, x * sz_ + y * cz      # about Z
                    mx = max(mx, abs(center[0] + x))
                    my = max(my, abs(center[1] + y))
                    mz = max(mz, center[2] + z)
    return (round(2 * mx, 3), round(2 * my, 3), round(mz, 3))


def prop(name, place, size, weights, parts, collide=True):
    """weights = (Level 0, Level 1, Level 2). The size is worked out from the parts (the given one is only a note)."""
    assert name not in PROPS, f"duplicate prop {name}"
    PROPS[name] = {"place": place, "size": bounds(parts), "weights": weights, "parts": parts, "collide": collide}


def office_chair():
    p = [F(0, 0, 0.02, 0.6, 0.06, 0.04, "black_plastic"), F(0, 0, 0.02, 0.06, 0.6, 0.04, "black_plastic"),
         C(0, 0, 0.06, 0.06, 0.38, "metal"),
         F(0, 0, 0.44, 0.5, 0.48, 0.08, "fabric_blue"),
         F(0, -0.24, 0.56, 0.46, 0.06, 0.5, "fabric_blue")]
    for (x, y) in ((0.28, 0), (-0.28, 0), (0, 0.28), (0, -0.28)):
        p.append(S(x, y, 0.03, 0.06, "rubber"))
    return p


prop("OfficeChair", "free", (0.6, 0.6, 1.06), (8, 1, 1), office_chair())

prop("OfficeDesk", "wall", (1.4, 0.7, 1.15), (7, 1, 0), [
    F(0, 0, 0.72, 1.4, 0.7, 0.04, "wood_light"),
    F(-0.68, 0, 0, 0.03, 0.66, 0.72, "metal_dark"), F(0.68, 0, 0, 0.03, 0.66, 0.72, "metal_dark"),
    F(0, -0.3, 0.3, 1.32, 0.02, 0.42, "metal_dark"),
    F(0.45, -0.1, 0.0, 0.4, 0.5, 0.65, "metal_dark"),          # drawer pedestal
    F(-0.15, -0.1, 0.76, 0.42, 0.34, 0.36, "beige_plastic"),   # CRT monitor
    F(-0.15, 0.075, 0.82, 0.32, 0.02, 0.25, "screen"),
    F(-0.15, 0.18, 0.76, 0.45, 0.15, 0.03, "beige_plastic"),   # keyboard
    F(0.4, 0.1, 0.76, 0.22, 0.3, 0.02, "paper"),
])

prop("FilingCabinet", "wall", (0.47, 0.62, 1.32), (7, 2, 2), [F(0, 0, 0, 0.47, 0.62, 1.32, "metal")] + [
    F(0, 0.315, 0.08 + i * 0.32, 0.43, 0.02, 0.28, "metal_dark") for i in range(4)] + [
    F(0, 0.33, 0.25 + i * 0.32, 0.12, 0.03, 0.03, "brass") for i in range(4)])

prop("WaterCooler", "wall", (0.34, 0.34, 1.45), (5, 0, 0), [
    F(0, 0, 0, 0.32, 0.32, 1.0, "white_plastic"), C(0, 0, 1.0, 0.28, 0.42, "water_blue"),
    F(0, 0.165, 0.7, 0.08, 0.04, 0.06, "black_plastic"), F(0, 0.12, 0.3, 0.2, 0.1, 0.02, "metal_dark")])

prop("VendingMachine", "wall", (0.9, 0.8, 1.85), (4, 2, 0), [
    F(0, 0, 0, 0.9, 0.8, 1.85, "vending_red"),
    F(-0.1, 0.405, 0.55, 0.58, 0.02, 1.15, "vending_glow"),
    F(0.32, 0.405, 1.0, 0.16, 0.02, 0.4, "black_plastic"),
    F(-0.1, 0.405, 0.12, 0.58, 0.02, 0.25, "black_plastic")])


def bookshelf():
    p = [F(-0.44, 0, 0, 0.02, 0.32, 1.8, "wood_dark"), F(0.44, 0, 0, 0.02, 0.32, 1.8, "wood_dark"),
         F(0, -0.15, 0, 0.9, 0.02, 1.8, "wood_dark")]
    rnd = random.Random(7)
    for i in range(5):
        z = i * 0.44
        p.append(F(0, 0, z, 0.86, 0.3, 0.02, "wood_dark"))
        if i < 4:
            x = -0.4
            while x < 0.35:
                w = rnd.uniform(0.04, 0.09)
                hgt = rnd.uniform(0.22, 0.34)
                if rnd.random() < 0.8:
                    p.append(F(x + w / 2, 0.0, z + 0.02, w, 0.22, hgt, rnd.choice(["fabric_red", "fabric_blue", "fabric_green", "paper", "cardboard"])))
                x += w + 0.005
    return p


prop("Bookshelf", "wall", (0.9, 0.32, 1.8), (6, 0, 0), bookshelf())

prop("Couch", "wall", (2.0, 0.9, 0.85), (5, 0, 0), [
    F(0, 0, 0.05, 2.0, 0.9, 0.35, "fabric_brown"), F(0, -0.34, 0.4, 2.0, 0.22, 0.45, "fabric_brown"),
    F(-0.92, 0.05, 0.4, 0.16, 0.8, 0.22, "fabric_brown"), F(0.92, 0.05, 0.4, 0.16, 0.8, 0.22, "fabric_brown"),
    F(-0.42, 0.1, 0.4, 0.8, 0.62, 0.12, "fabric_brown"), F(0.42, 0.1, 0.4, 0.8, 0.62, 0.12, "fabric_brown")]
    + legs4(1.9, 0.8, 0.05, 0.06, "wood_dark"))

prop("Armchair", "free", (0.9, 0.85, 0.9), (4, 0, 0), [
    F(0, 0, 0.08, 0.9, 0.85, 0.34, "fabric_green"), F(0, -0.33, 0.42, 0.9, 0.2, 0.48, "fabric_green"),
    F(-0.38, 0.05, 0.42, 0.14, 0.75, 0.2, "fabric_green"), F(0.38, 0.05, 0.42, 0.14, 0.75, 0.2, "fabric_green")]
    + legs4(0.8, 0.75, 0.08, 0.05, "wood_dark"))

prop("CrtTv", "wall", (0.9, 0.5, 1.15), (4, 0, 0), [
    F(0, 0, 0.5, 0.9, 0.5, 0.03, "wood_dark")] + legs4(0.9, 0.5, 0.5, 0.04, "wood_dark") + [
    F(0, -0.02, 0.53, 0.6, 0.48, 0.5, "black_plastic"),
    F(0, 0.225, 0.59, 0.46, 0.02, 0.36, "screen_glow"),
    F(0, -0.02, 1.03, 0.03, 0.03, 0.12, "metal")])

prop("FloorLamp", "free", (0.4, 0.4, 1.7), (5, 0, 0), [
    C(0, 0, 0, 0.32, 0.03, "metal_dark"), C(0, 0, 0.03, 0.03, 1.4, "brass"),
    C(0, 0, 1.38, 0.4, 0.3, "lamp_glow")])

prop("Mattress", "free", (0.95, 1.95, 0.2), (4, 1, 1), [F(0, 0, 0, 0.95, 1.95, 0.2, "mattress")], collide=False)

prop("CardboardBoxes", "wall", (1.1, 0.7, 1.0), (8, 5, 3), [
    F(-0.25, 0, 0, 0.55, 0.55, 0.45, "cardboard"), F(0.3, 0.05, 0, 0.5, 0.5, 0.4, "cardboard"),
    F(-0.2, -0.02, 0.45, 0.48, 0.48, 0.4, "cardboard", [0, 0, 12]), F(0.3, 0.05, 0.4, 0.36, 0.3, 0.25, "cardboard", [0, 0, -8])])

prop("PlasticChair", "free", (0.5, 0.52, 0.82), (6, 1, 0), [
    F(0, 0, 0.42, 0.46, 0.46, 0.04, "white_plastic"), F(0, -0.22, 0.46, 0.44, 0.04, 0.36, "white_plastic", [-8, 0, 0])]
    + legs4(0.46, 0.46, 0.42, 0.04, "white_plastic"))

prop("Payphone", "wall", (0.4, 0.3, 1.7), (3, 2, 0), [
    F(0, -0.05, 0.95, 0.36, 0.2, 0.6, "metal"), F(0, 0.06, 1.2, 0.2, 0.02, 0.2, "metal_dark"),
    F(-0.12, 0.08, 1.05, 0.06, 0.06, 0.25, "black_plastic"), F(0, 0.07, 0.95, 0.12, 0.02, 0.15, "metal_dark")])

prop("PhoneBooth", "wall", (1.0, 0.9, 2.4), (2, 1, 0), [
    F(-0.47, -0.42, 0, 0.06, 0.06, 2.3, "red_paint"), F(0.47, -0.42, 0, 0.06, 0.06, 2.3, "red_paint"),
    F(-0.47, 0.42, 0, 0.06, 0.06, 2.3, "red_paint"), F(0.47, 0.42, 0, 0.06, 0.06, 2.3, "red_paint"),
    F(0, 0, 2.3, 1.0, 0.9, 0.1, "red_paint"), F(0, 0, 0, 1.0, 0.9, 0.05, "red_paint"),
    F(0, -0.42, 0.05, 0.88, 0.04, 2.25, "red_paint"),
    F(-0.47, 0, 1.0, 0.03, 0.78, 1.1, "glass_dark"), F(0.47, 0, 1.0, 0.03, 0.78, 1.1, "glass_dark"),
    F(0, -0.37, 1.2, 0.3, 0.06, 0.4, "metal"), F(0, 0, 2.15, 0.6, 0.86, 0.12, "lamp_glow")])

prop("LoneDoor", "free", (1.1, 0.2, 2.15), (3, 0, 1), [
    F(-0.5, 0, 0, 0.08, 0.14, 2.12, "wood_light"), F(0.5, 0, 0, 0.08, 0.14, 2.12, "wood_light"),
    F(0, 0, 2.08, 1.08, 0.14, 0.08, "wood_light"),
    F(0.02, 0.18, 0, 0.88, 0.04, 2.06, "wood_dark", [0, 0, -25]),
    S(0.32, 0.38, 1.0, 0.06, "brass")], collide=True)

prop("ArcadeCabinet", "wall", (0.7, 0.8, 1.8), (3, 1, 0), [
    F(0, -0.1, 0, 0.7, 0.6, 1.8, "black_plastic"),
    F(0, 0.12, 0.85, 0.66, 0.3, 0.12, "black_plastic", [20, 0, 0]),
    F(0, 0.19, 1.05, 0.5, 0.02, 0.4, "screen_glow", [-15, 0, 0]),
    F(0, 0.2, 1.58, 0.64, 0.02, 0.16, "vending_glow"),
    S(-0.12, 0.2, 0.96, 0.05, "red_paint"), S(0.1, 0.2, 0.96, 0.04, "yellow")])

prop("Fridge", "wall", (0.72, 0.7, 1.8), (3, 0, 0), [
    F(0, 0, 0, 0.72, 0.7, 1.8, "appliance"), F(0, 0.355, 1.18, 0.7, 0.01, 0.01, "metal_dark"),
    F(-0.3, 0.37, 1.3, 0.03, 0.03, 0.4, "metal"), F(-0.3, 0.37, 0.7, 0.03, 0.03, 0.35, "metal")])

prop("WashingMachine", "wall", (0.6, 0.62, 0.86), (3, 0, 0), [
    F(0, 0, 0, 0.6, 0.62, 0.86, "appliance"), C(0, 0.31, 0.42, 0.36, 0.03, "glass_dark", [90, 0, 0]),
    F(0, 0.315, 0.76, 0.5, 0.01, 0.08, "metal_dark")])

prop("Bathtub", "big", (0.8, 1.65, 0.65), (2, 0, 0), [
    F(0, 0, 0.12, 0.8, 1.65, 0.06, "porcelain"),
    F(-0.37, 0, 0.12, 0.06, 1.65, 0.5, "porcelain"), F(0.37, 0, 0.12, 0.06, 1.65, 0.5, "porcelain"),
    F(0, -0.8, 0.12, 0.8, 0.06, 0.5, "porcelain"), F(0, 0.8, 0.12, 0.8, 0.06, 0.5, "porcelain"),
    F(0, 0.05, 0.16, 0.68, 1.5, 0.12, "water_blue")] + [
    S(sx * 0.3, sy * 0.7, 0.06, 0.12, "brass") for sx in (-1, 1) for sy in (-1, 1)])

prop("HospitalBed", "big", (1.0, 2.1, 1.0), (2, 0, 1), [
    F(0, 0, 0.5, 0.95, 2.0, 0.08, "metal"), F(0, 0.05, 0.58, 0.9, 1.9, 0.14, "mattress"),
    F(0, -1.0, 0.3, 0.98, 0.05, 0.7, "metal"), F(0, 1.0, 0.3, 0.98, 0.05, 0.45, "metal"),
    F(-0.48, 0.2, 0.72, 0.03, 1.0, 0.03, "metal"), F(0.48, 0.2, 0.72, 0.03, 1.0, 0.03, "metal")]
    + legs4(0.9, 1.95, 0.5, 0.04, "metal"))


def cart():
    p = [F(0, 0, 0.45, 0.55, 0.85, 0.02, "metal")]
    for x in (-0.27, 0.27):
        p.append(F(x, 0, 0.45, 0.02, 0.85, 0.5, "metal"))
    for y in (-0.42, 0.42):
        p.append(F(0, y, 0.45, 0.55, 0.02, 0.5, "metal"))
    p += legs4(0.5, 0.75, 0.45, 0.03, "metal")
    p.append(F(0, -0.5, 0.95, 0.5, 0.04, 0.04, "red_paint"))
    return p


prop("ShoppingCart", "free", (0.55, 0.95, 1.0), (3, 3, 0), cart())

prop("TrafficCone", "small", (0.36, 0.36, 0.7), (3, 5, 2), [
    F(0, 0, 0, 0.36, 0.36, 0.03, "orange"), C(0, 0, 0.03, 0.26, 0.2, "orange"), C(0, 0, 0.23, 0.18, 0.15, "white_plastic"),
    C(0, 0, 0.38, 0.13, 0.22, "orange"), C(0, 0, 0.6, 0.07, 0.08, "orange")], collide=False)

prop("Mannequin", "free", (0.45, 0.35, 1.8), (2, 0, 1), [
    C(0, 0, 0, 0.3, 0.03, "metal_dark"), C(0, 0, 0.03, 0.03, 0.8, "metal"),
    F(0, 0, 0.82, 0.38, 0.22, 0.5, "mannequin"), S(0, 0, 1.32, 0.24, "mannequin", 0.12),
    F(0, 0, 1.35, 0.1, 0.1, 0.12, "mannequin"), S(0, 0.01, 1.6, 0.22, "mannequin", 0.27),
    F(-0.23, 0, 0.88, 0.07, 0.08, 0.48, "mannequin", [0, 6, 0]), F(0.23, 0, 0.88, 0.07, 0.08, 0.48, "mannequin", [0, -6, 0])])

prop("UprightPiano", "wall", (1.5, 0.62, 1.25), (2, 0, 0), [
    F(0, -0.12, 0, 1.5, 0.38, 1.25, "wood_dark"), F(0, 0.12, 0.65, 1.5, 0.3, 0.1, "wood_dark"),
    F(0, 0.16, 0.75, 1.3, 0.16, 0.02, "porcelain"),
    F(-0.72, 0.15, 0, 0.06, 0.06, 0.65, "wood_dark"), F(0.72, 0.15, 0, 0.06, 0.06, 0.65, "wood_dark")])


def car(color):
    p = [F(0, 0, 0.28, 1.8, 4.4, 0.55, color), F(0, -0.25, 0.83, 1.6, 2.3, 0.5, color),
         F(0, 0.92, 0.88, 1.5, 0.02, 0.4, "glass_dark", [-30, 0, 0]), F(0, -1.42, 0.88, 1.5, 0.02, 0.4, "glass_dark", [30, 0, 0]),
         F(-0.805, -0.25, 0.9, 0.02, 2.0, 0.36, "glass_dark"), F(0.805, -0.25, 0.9, 0.02, 2.0, 0.36, "glass_dark"),
         F(0, 2.2, 0.45, 1.6, 0.02, 0.12, "metal"), F(0, -2.2, 0.45, 1.6, 0.02, 0.12, "metal"),
         F(-0.6, 2.205, 0.6, 0.3, 0.01, 0.1, "lamp_glow"), F(0.6, 2.205, 0.6, 0.3, 0.01, 0.1, "lamp_glow")]
    for sx in (-1, 1):
        for y in (1.35, -1.35):
            p.append(C(sx * 0.8, y, 0.33, 0.66, 0.24, "rubber", [0, 90, 0]))
    return p


prop("CarBlue", "big", (1.9, 4.4, 1.35), (1, 8, 0), car("car_blue"))
prop("CarRed", "big", (1.9, 4.4, 1.35), (0, 6, 0), car("car_red"))
prop("CarGrey", "big", (1.9, 4.4, 1.35), (0, 8, 0), car("car_grey"))

prop("PalletStack", "free", (1.2, 1.0, 1.4), (1, 8, 0), [
    F(0, 0, 0, 1.2, 1.0, 0.14, "wood_light"), F(0, 0, 0.14, 1.1, 0.95, 1.2, "cardboard"),
    F(0, 0, 0.14, 1.12, 0.97, 1.2, "glass_dark") if False else F(0, 0, 1.34, 1.1, 0.95, 0.02, "paper")])

prop("OilBarrel", "free", (0.6, 0.6, 0.9), (1, 6, 6), [
    C(0, 0, 0, 0.58, 0.9, "blue_paint"), C(0, 0, 0.3, 0.6, 0.03, "metal_dark"), C(0, 0, 0.6, 0.6, 0.03, "metal_dark")])
prop("RustyBarrel", "free", (0.6, 0.6, 0.9), (1, 4, 8), [
    C(0, 0, 0, 0.58, 0.9, "rust"), C(0, 0, 0.3, 0.6, 0.03, "metal_dark"), C(0, 0, 0.6, 0.6, 0.03, "metal_dark")])


def rack():
    p = []
    for x in (-1.18, 1.18):
        for y in (-0.42, 0.42):
            p.append(F(x, y, 0, 0.06, 0.06, 2.6, "orange"))
    for z in (0.1, 1.0, 1.9):
        p.append(F(0, 0, z, 2.4, 0.9, 0.05, "metal_dark"))
        p.append(F(-0.6, 0, z + 0.05, 0.9, 0.75, 0.65, "cardboard"))
        if z < 1.5:
            p.append(F(0.6, 0, z + 0.05, 0.9, 0.75, 0.5, "wood_light"))
    return p


prop("ShelvingRack", "wall", (2.4, 0.9, 2.6), (1, 7, 0), rack())

prop("Toolbox", "small", (0.5, 0.25, 0.3), (1, 3, 6), [
    F(0, 0, 0, 0.5, 0.25, 0.22, "red_paint"), F(0, 0, 0.22, 0.48, 0.23, 0.04, "red_paint"),
    F(0, 0, 0.26, 0.2, 0.03, 0.04, "black_plastic")], collide=False)

prop("Bucket", "small", (0.3, 0.3, 0.32), (2, 3, 5), [C(0, 0, 0, 0.3, 0.32, "metal"), C(0, 0, 0.25, 0.27, 0.02, "rust")], collide=False)

prop("Backpack", "small", (0.35, 0.25, 0.48), (4, 2, 3), [
    F(0, 0, 0, 0.34, 0.22, 0.46, "fabric_green", [-10, 0, 0]), F(0, 0.13, 0.08, 0.26, 0.08, 0.22, "fabric_green"),
    F(0, -0.05, 0.44, 0.24, 0.16, 0.08, "fabric_green")], collide=False)

prop("SleepingBag", "small", (0.8, 1.9, 0.12), (3, 2, 2), [
    F(0, 0, 0, 0.78, 1.8, 0.1, "fabric_red"), F(0, -0.9, 0.0, 0.6, 0.25, 0.14, "fabric_red")], collide=False)

prop("Lantern", "small", (0.18, 0.18, 0.3), (3, 2, 3), [
    C(0, 0, 0, 0.16, 0.04, "metal_dark"), C(0, 0, 0.04, 0.12, 0.17, "lamp_glow"), C(0, 0, 0.21, 0.16, 0.05, "metal_dark"),
    F(0, 0, 0.26, 0.1, 0.02, 0.06, "metal")], collide=False)

prop("TinCans", "small", (0.4, 0.3, 0.12), (4, 3, 3), [
    C(-0.1, 0, 0, 0.075, 0.11, "metal"), C(0.06, 0.05, 0, 0.075, 0.11, "metal"),
    C(0.12, -0.08, 0.037, 0.075, 0.11, "metal", [90, 0, 30]), C(-0.05, 0.1, 0.0, 0.075, 0.11, "red_paint")], collide=False)

prop("Papers", "small", (0.9, 0.7, 0.02), (8, 2, 3), [
    F(0, 0, 0, 0.21, 0.297, 0.002, "paper", [0, 0, 12]), F(0.25, 0.12, 0.002, 0.21, 0.297, 0.002, "paper", [0, 0, -30]),
    F(-0.22, 0.18, 0.004, 0.21, 0.297, 0.002, "paper", [0, 0, 70]), F(0.1, -0.22, 0.006, 0.21, 0.297, 0.002, "paper", [0, 0, 5]),
    F(-0.3, -0.15, 0.002, 0.3, 0.22, 0.015, "cardboard", [0, 0, -15])], collide=False)

prop("OfficePlant", "wall", (0.5, 0.5, 1.2), (5, 0, 0), [
    C(0, 0, 0, 0.36, 0.38, "white_plastic"), C(0, 0, 0.36, 0.33, 0.02, "wood_dark")] + [
    F(0, 0, 0.38, 0.05, 0.4 + 0.1 * i, 0.02, "plant_dead", [40 + 12 * i, 0, i * 60]) for i in range(6)])

prop("Lockers", "wall", (1.2, 0.5, 1.85), (1, 4, 4), [F(0, 0, 0, 1.2, 0.5, 1.85, "green_paint")] + [
    F(-0.4 + 0.4 * i, 0.255, 0.05, 0.36, 0.01, 1.75, "metal_dark") for i in range(3)] + [
    F(-0.4 + 0.4 * i + 0.12, 0.265, 1.0, 0.03, 0.02, 0.12, "metal") for i in range(3)])

prop("FuseBox", "wall", (0.6, 0.25, 2.0), (0, 1, 7), [
    F(0, -0.02, 1.0, 0.6, 0.2, 0.8, "metal"), F(0, 0.085, 1.4, 0.5, 0.01, 0.06, "yellow"),
    F(0.2, 0.1, 1.1, 0.04, 0.04, 0.12, "black_plastic"), C(0, -0.05, 1.8, 0.06, 0.2, "metal_dark")])

prop("GasCylinders", "wall", (0.9, 0.35, 1.5), (0, 2, 6), [
    C(-0.3, 0, 0, 0.26, 1.35, "green_paint"), C(0, 0, 0, 0.26, 1.35, "yellow"), C(0.3, 0, 0, 0.26, 1.35, "green_paint"),
    S(-0.3, 0, 1.38, 0.24, "green_paint", 0.16), S(0, 0, 1.38, 0.24, "yellow", 0.16), S(0.3, 0, 1.38, 0.24, "green_paint", 0.16),
    F(0, -0.12, 0.9, 0.9, 0.03, 0.04, "metal_dark")])

prop("Ladder", "wall", (0.5, 0.6, 2.4), (2, 2, 3), [
    F(-0.22, 0.1, 0, 0.05, 0.06, 2.4, "metal", [-12, 0, 0]), F(0.22, 0.1, 0, 0.05, 0.06, 2.4, "metal", [-12, 0, 0])] + [
    F(0, 0.27 - 0.06 * i, 0.3 + 0.28 * i, 0.44, 0.04, 0.03, "metal") for i in range(7)], collide=False)

prop("TireStack", "free", (0.7, 0.7, 0.8), (0, 6, 1), [C(0, 0, i * 0.2, 0.66, 0.2, "rubber") for i in range(4)])

prop("JerseyBarrier", "free", (0.6, 2.0, 0.8), (0, 6, 0), [
    F(0, 0, 0, 0.6, 2.0, 0.25, "concrete"), F(0, 0, 0.25, 0.3, 2.0, 0.55, "concrete")])

prop("TeddyBear", "small", (0.3, 0.3, 0.35), (2, 0, 1), [
    S(0, 0, 0.12, 0.24, "teddy"), S(0, 0, 0.3, 0.16, "teddy"), S(-0.07, 0, 0.38, 0.06, "teddy"), S(0.07, 0, 0.38, 0.06, "teddy"),
    S(-0.1, 0.06, 0.03, 0.08, "teddy"), S(0.1, 0.06, 0.03, 0.08, "teddy")], collide=False)

prop("Radio", "small", (0.4, 0.15, 0.25), (3, 1, 1), [
    F(0, 0, 0, 0.4, 0.14, 0.22, "black_plastic"), C(-0.08, 0.071, 0.1, 0.12, 0.01, "metal", [90, 0, 0]),
    F(0.1, 0.071, 0.12, 0.12, 0.01, 0.06, "screen_glow"), F(0.15, 0, 0.22, 0.01, 0.01, 0.35, "metal", [0, 25, 0])], collide=False)

prop("FallenCeilingTiles", "small", (1.4, 1.2, 0.2), (7, 0, 0), [
    F(0, 0, 0, 0.6, 0.6, 0.015, "ceiling_tile", [0, 0, 20]), F(0.45, 0.3, 0.0, 0.6, 0.6, 0.015, "ceiling_tile", [6, 0, -10]),
    F(-0.4, 0.25, 0.04, 0.6, 0.6, 0.015, "ceiling_tile", [12, 3, 50]), F(0.2, -0.4, 0.0, 0.3, 0.45, 0.015, "ceiling_tile", [0, 0, 33])],
    collide=False)

prop("Rug", "small", (2.2, 1.5, 0.01), (0, 0, 0), [F(0, 0, 0, 2.2, 1.5, 0.008, "fabric_red"), F(0, 0, 0.008, 1.8, 1.1, 0.002, "fabric_brown")],
     collide=False)

prop("RoundTable", "free", (0.9, 0.9, 0.75), (2, 0, 0), [
    C(0, 0, 0.72, 0.9, 0.03, "wood_light"), C(0, 0, 0.05, 0.06, 0.67, "metal_dark"), C(0, 0, 0, 0.5, 0.05, "metal_dark")])

prop("Crt Monitor Pile".replace(" ", ""), "wall", (1.0, 0.6, 0.9), (3, 1, 1), [
    F(-0.22, 0, 0, 0.42, 0.4, 0.36, "beige_plastic"), F(0.24, 0.05, 0, 0.42, 0.4, 0.36, "beige_plastic", [0, 0, -10]),
    F(0.0, 0.0, 0.36, 0.42, 0.4, 0.36, "beige_plastic", [0, 0, 8]),
    F(-0.22, 0.201, 0.05, 0.32, 0.01, 0.25, "screen"), F(0.0, 0.201, 0.41, 0.32, 0.01, 0.25, "screen")])


# A fallen MTF soldier, lying on his back with his gear (later: [E] search for his weapon, ammo, notes).
prop("FallenSoldier", "small", (0.9, 1.9, 0.3), (2, 2, 2), [
    F(0, 0.15, 0, 0.42, 0.62, 0.22, "mtf_uniform"), F(0, 0.15, 0.2, 0.44, 0.5, 0.08, "mtf_vest"),
    F(-0.16, 0.38, 0.28, 0.06, 0.06, 0.01, "mtf_patch"),
    S(0, 0.62, 0.11, 0.22, "mannequin", 0.2), S(0, 0.63, 0.15, 0.27, "mtf_vest", 0.18),
    F(-0.11, -0.45, 0, 0.15, 0.62, 0.14, "mtf_uniform", [0, 0, 4]), F(0.12, -0.44, 0, 0.15, 0.62, 0.14, "mtf_uniform", [0, 0, -9]),
    F(-0.11, -0.8, 0, 0.12, 0.2, 0.12, "rubber"), F(0.18, -0.79, 0, 0.12, 0.2, 0.12, "rubber"),
    F(-0.3, 0.2, 0, 0.11, 0.55, 0.11, "mtf_uniform", [0, 0, 18]), F(0.33, 0.35, 0, 0.11, 0.5, 0.11, "mtf_uniform", [0, 0, -40]),
    F(0.55, 0.0, 0, 0.08, 0.85, 0.09, "metal_dark", [0, 0, 25]), F(0.53, 0.05, 0.02, 0.06, 0.2, 0.14, "metal_dark", [0, 0, 25])],
    collide=False)

import more_props  # noqa: E402  (everything else that has fallen in)
more_props.add(prop, F, B, C, S, legs4)


# Little scenes, laid out around a cell's middle: (prop, x, y, yaw degrees). Weights as for props.
VIGNETTES = {
    "AbandonedCamp": ((6, 3, 4), [("SleepingBag", -0.6, 0.2, 10), ("Backpack", 0.2, 0.9, -30), ("Lantern", 0.3, 0.1, 0),
                                  ("TinCans", 0.7, -0.3, 40), ("Papers", -0.3, -0.9, 0), ("CardboardBoxes", 1.0, 0.8, -100)]),
    "LivingRoom": ((4, 0, 0), [("Rug", 0, 0, 0), ("Couch", 0, -1.1, 0), ("Armchair", -1.2, 0.3, 70), ("CrtTv", 0.2, 1.3, 180),
                               ("FloorLamp", 1.25, -1.2, 0)]),
    "OfficeIsland": ((5, 0, 0), [("OfficeDesk", 0, -0.4, 0), ("OfficeDesk", 0, 0.4, 180), ("OfficeChair", 0.3, -1.1, 15),
                                 ("OfficeChair", -0.4, 1.15, 200), ("Papers", 1.1, 0.9, 0)]),
    "Dining": ((3, 0, 0), [("RoundTable", 0, 0, 0), ("PlasticChair", 0, -0.75, 0), ("PlasticChair", 0.7, 0.3, 115),
                           ("PlasticChair", -0.75, 0.35, -110), ("TinCans", 0.15, 0.1, 0)]),
    "Bedroom": ((3, 0, 1), [("Mattress", -0.3, 0, 5), ("FloorLamp", 0.75, -0.8, 0), ("TeddyBear", -0.2, 0.7, 160), ("Radio", 0.6, 0.5, -80)]),
    "FallenPatrol": ((2, 2, 2), [("FallenSoldier", 0, 0, 0), ("Backpack", 0.7, -0.6, 60), ("Papers", -0.5, -0.8, 0),
                                 ("Lantern", -0.6, 0.5, 0), ("TinCans", 0.6, 0.8, 0)]),
    "Kitchenette": ((4, 0, 0), [("KitchenTable", 0, 0.2, 0), ("KitchenChair", -0.75, 0.2, 90), ("KitchenChair", 0.75, 0.2, -90),
                                ("DirtyDishes", 0.1, 0.25, 0), ("Kettle", 0, -1.1, 0), ("TrashBags", 1.2, -1.1, 0)]),
    "WaitingRoom": ((3, 1, 0), [("WaitingChairs", 0, -0.9, 0), ("CoffeeTable", 0, 0.1, 0), ("PottedFern", 1.15, -1.1, 0),
                                ("Papers", -0.6, 0.6, 0), ("WetFloorSign", 0.9, 0.9, 30)]),
    "Classroom": ((3, 0, 0), [("SchoolDesk", -0.6, -0.5, 0), ("SchoolDesk", 0.6, -0.5, 0), ("SchoolDesk", -0.6, 0.7, 0),
                              ("SchoolDesk", 0.6, 0.7, 10), ("Globe", 1.25, 1.2, 0), ("PaperStack", -1.2, 1.2, 0)]),
    "Party": ((2, 0, 0), [("PartyTable", 0, 0, 0), ("Balloons", 1.1, 0.8, 0), ("GiftBoxes", -1.0, -0.8, 30),
                          ("PlasticChair", 0, 0.75, 180), ("PlasticChair", 0.9, -0.6, 60)]),
    "Laundry": ((3, 0, 1), [("WashingMachine", -0.4, -1.2, 0), ("WashingMachine", 0.3, -1.2, 0), ("LaundryBasket", 0.6, 0.2, 20),
                            ("IroningBoard", -0.4, 0.6, 80), ("PairOfShoes", 0.9, 1.0, 0)]),
    "Hospital": ((2, 0, 1), [("Gurney", -0.3, 0, 0), ("IvStand", 0.5, 0.6, 0), ("Wheelchair", 0.8, -0.9, 200), ("OxygenTank", -1.2, 0.9, 0)]),
    "Playroom": ((2, 0, 0), [("RockingHorse", -0.6, 0.3, 40), ("ToyBlocks", 0.3, 0.4, 0), ("RedBall", 0.9, -0.5, 0), ("Tricycle", -0.2, -0.8, 120),
                             ("TeddyBear", 0.6, 0.9, 200), ("Crib", 0.0, 1.0, 90)]),
    "Garage": ((0, 4, 1), [("Workbench", 0, -1.6, 0), ("RollingToolChest", 1.6, -1.6, 0), ("Bicycle", -1.2, 0.5, 70),
                           ("JerryCans", 0.8, 0.6, 0), ("TireStack", -1.6, -1.2, 0), ("PaintCans", 0.5, -0.6, 0)]),
    "MtfOutpost": ((1, 2, 3), [("ArmyCot", -0.7, 0, 0), ("FoundationCrate", 0.8, -0.8, 0), ("AmmoCrate", 0.9, 0.3, 30),
                               ("FieldRadio", 0.2, 0.9, 0), ("Lantern", -0.1, -0.2, 0), ("Sandbags", 0.5, 1.4, 180)]),
    "Workshop": ((0, 3, 5), [("Toolbox", 0, 0, 20), ("RustyBarrel", -0.8, 0.5, 0), ("Bucket", 0.5, 0.6, 0), ("Ladder", 0.9, -0.6, 90),
                             ("TrafficCone", -0.5, -0.6, 0)]),
}


# ----------------------------------------------------------------------------- prefab writing

def new_id(rnd):
    return rnd.getrandbits(63)


def build_prefab(name, spec, template):
    rnd = random.Random(name)
    data = copy.deepcopy(template)
    data["ContainerEntity"]["Name"] = name
    for comp in data["ContainerEntity"]["Components"].values():
        comp["Id"] = new_id(rnd)
    child_template = next(iter(template["Entities"].values()))
    entities = {}
    order = []

    def add_entity(label, shape, center, size, mat, rot, mesh=True, collider=False):
        eid = f"Entity_[{rnd.getrandbits(40)}]"
        e = copy.deepcopy(child_template)
        e["Id"] = eid
        e["Name"] = f"{name}_{label}"
        comps = e["Components"]
        for comp in comps.values():
            comp["Id"] = new_id(rnd)
        if mesh:
            model, hint, unit = SHAPES[shape]
            comps["AZ::Render::EditorMeshComponent"]["Controller"]["Configuration"]["ModelAsset"] = {"assetId": dict(model), "assetHint": hint}
            comps["EditorMaterialComponent"]["Controller"]["Configuration"]["materials"]["{}"]["MaterialAsset"]["assetId"]["guid"] = material_guid(mat)
            scale = [round(s / unit, 5) for s in size]
        else:
            del comps["AZ::Render::EditorMeshComponent"]
            del comps["EditorMaterialComponent"]
            scale = [round(s, 5) for s in size]
        if not collider:
            for key in [k for k, c in comps.items() if "Collider" in c.get("$type", "") or "RigidBody" in c.get("$type", "")]:
                del comps[key]
        comps["EditorNonUniformScaleComponent"]["NonUniformScale"] = scale
        td = {"Translate": [round(v, 5) for v in center]}
        if rot:
            td["Rotate"] = [float(r) for r in rot]
        comps["TransformComponent"]["Transform Data"] = td
        comps["TransformComponent"]["Parent Entity"] = "ContainerEntity"
        entities[eid] = e
        order.append(eid)

    for i, (shape, center, size, mat, rot) in enumerate(spec["parts"]):
        # Nothing thinner than 1 cm: paper-thin (2 mm) boxes whited out the whole screen around them in game
        # (Level 0's floor stains are 1 cm and fine). Lift thin parts so they still sit on what's under them.
        if min(size) < 0.01:
            lift = (0.01 - min(size)) / 2
            size = tuple(max(0.01, s) for s in size)
            center = (center[0], center[1], center[2] + lift)
        add_entity(f"Part{i}", shape, center, size, mat, rot)
    if spec["collide"]:
        w, d, h = spec["size"]
        add_entity("Blocker", "box", (0, 0, h / 2), (w, d, h), None, None, mesh=False, collider=True)
    data["Entities"] = entities
    data["ContainerEntity"]["Components"]["EditorEntitySortComponent"]["Child Entity Order"] = order
    with open(path("Prefabs/Clutter", f"{name}.prefab"), "w") as f:
        json.dump(data, f, indent=4)


def write_catalog():
    names = list(PROPS)
    place_code = {"wall": "Wall", "free": "Free", "big": "Big", "small": "Small"}
    lines = [
        "#pragma once",
        "",
        "// Generated by Tools/Clutter/make_clutter.py: do not edit by hand, edit the tool and rerun it.",
        "// The clutter props (everything that has ended up in the Backrooms) and the little scenes made from them.",
        "",
        "namespace NoclipProtocol::Clutter",
        "{",
        "    enum class Place : int { Wall, Free, Big, Small };",
        "",
        "    struct Kind",
        "    {",
        "        const char* m_name;       // prefab: prefabs/clutter/<lowercase name>.spawnable",
        "        Place m_place;",
        "        float m_width, m_depth, m_height; // meters, front facing +Y",
        "        int m_weight[3];          // how common in Level 0, 1, 2 (0 = never)",
        "        bool m_blocks;            // has a collider",
        "    };",
        "",
        f"    constexpr int KindCount = {len(names)};",
        "    constexpr Kind Kinds[KindCount] = {",
    ]
    for n in names:
        s = PROPS[n]
        w, d, h = s["size"]
        a, b, c = s["weights"]
        lines.append(f"        {{ \"{n}\", Place::{place_code[s['place']]}, {w:.2f}f, {d:.2f}f, {h:.2f}f, {{ {a}, {b}, {c} }}, {'true' if s['collide'] else 'false'} }},")
    lines += ["    };", "", "    struct ScenePiece { int m_kind; float m_x, m_y, m_yawDegrees; };",
              "    struct Scene { const char* m_name; int m_weight[3]; int m_count; ScenePiece m_pieces[8]; };", "",
              f"    constexpr int SceneCount = {len(VIGNETTES)};", "    constexpr Scene Scenes[SceneCount] = {"]
    for vn, (weights, pieces) in VIGNETTES.items():
        body = ", ".join(f"{{ {names.index(p)}, {x:.2f}f, {y:.2f}f, {yaw:.1f}f }}" for p, x, y, yaw in pieces)
        lines.append(f"        {{ \"{vn}\", {{ {weights[0]}, {weights[1]}, {weights[2]} }}, {len(pieces)}, {{ {body} }} }},")
    lines += ["    };", "} // namespace NoclipProtocol::Clutter", ""]
    with open(path("Gem/Source/Level/ClutterCatalog.h"), "w") as f:
        f.write("\n".join(lines))


def check_fits():
    """Warn about props that can never be placed in a level they are weighted for (same limits as Level0Layout)."""
    import math
    levels = [(4.0, 0.2), (8.0, 0.3), (3.0, 0.25)]  # cell size, wall thickness per level
    bad = 0
    for name, s in PROPS.items():
        w, d, _h = s["size"]
        r = 0.5 * math.hypot(w, d)
        for lvl, (cell, thick) in enumerate(levels):
            if s["weights"][lvl] <= 0:
                continue
            inner = cell / 2 - thick / 2
            band = min(0.95, cell * 0.25)
            span = inner - band - 0.05
            open_ = cell / 2 - 0.25
            ok = {"wall": d <= band and w / 2 <= span, "free": r <= open_ - 0.6 or r <= span,
                  "big": r <= open_, "small": r <= span + 0.3}[s["place"]]
            if not ok:
                # Too big for that level's cells on its own: left out there (scenes check their own fit).
                bad += 1
                weights = list(s["weights"])
                weights[lvl] = 0
                s["weights"] = tuple(weights)
    for name, s in PROPS.items():
        for part in s["parts"]:
            assert part[3] in MATERIALS, f"{name}: unknown material {part[3]}"
    if bad:
        print(f"{bad} prop/level pairs left out (too big for those cells)")
    return bad


def main():
    check_fits()
    write_materials()
    with open(path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    for name, spec in PROPS.items():
        build_prefab(name, spec, template)
    write_catalog()
    print(f"wrote {len(PROPS)} props, {len(VIGNETTES)} scenes, {len(MATERIALS)} materials")


if __name__ == "__main__":
    main()
