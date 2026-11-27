#!/usr/bin/env python3
"""
Builds every base-building piece from the approved Build mockups (hearth-workspace
noclip-protocol/mockups/ui-and-hearth-engine: Build, BuildDefenses, BuildPower, BuildStorage, BuildResearch,
BuildContainment), six tabs of six tiles:

    python3 Tools/Base/make_base_pieces.py        (run from the project folder, after Tools/Clutter/make_clutter.py)

Writes
  Prefabs/Base/Piece_<Name>.prefab     each new piece at its real size, built from boxes and cylinders like the clutter
                                       (the Lighting tab's pieces keep their own prefabs, from make_base.py)
  Assets/Materials/Base/piece_*.material  the few colors the clutter's materials don't have
  Gem/Source/Base/BaseCatalog.h        every tile: name, cost, power, footprint, lock, what it does, and its parts
                                       (the HUD draws the see-through placement preview from the parts)
  Gem/Source/Hud/BuildIcons.h          the tiles' icons, traced from the mockups' SVG icons

Every piece stands on the floor at its origin with its front facing +Y. Pieces you would bump into get one invisible
box collider over their footprint; low or thin ones (a tripwire, a cable, an open shutter) get none.
"""

import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Clutter"))
import make_clutter as mc  # noqa: E402

path = mc.path
asset_guid = mc.asset_guid
B, F, C, S, legs4 = mc.B, mc.F, mc.C, mc.S, mc.legs4

# Colors the clutter doesn't have.
BASE_MATERIALS = {
    #  name               base color             metal rough  emissive (color, intensity)
    "piece_steel":       ([0.10, 0.10, 0.095], 0.4, 0.55, None),
    "piece_hazard":      ([0.85, 0.62, 0.05], 0.2, 0.5, None),
    "piece_uv":          ([0.50, 0.30, 0.90], 0.0, 0.3, ([0.6, 0.35, 1.0], 4.0)),
    "piece_red_led":     ([0.90, 0.10, 0.05], 0.0, 0.3, ([1.0, 0.15, 0.1], 4.0)),
    "piece_green_led":   ([0.20, 0.90, 0.40], 0.0, 0.3, ([0.3, 1.0, 0.5], 3.0)),
    "piece_warm_lamp":   ([1.00, 0.95, 0.85], 0.0, 0.3, ([1.0, 0.93, 0.8], 5.0)),
    "piece_copper":      ([0.72, 0.42, 0.22], 1.0, 0.3, None),
    "piece_almond":      ([0.90, 0.86, 0.72], 0.0, 0.1, ([1.0, 0.95, 0.8], 0.6)),
    "piece_canvas":      ([0.24, 0.27, 0.17], 0.0, 0.95, None),
    "piece_concrete":    ([0.42, 0.42, 0.40], 0.0, 0.9, None),
    "piece_foundation":  ([0.14, 0.14, 0.13], 0.5, 0.45, None),
    "piece_999":         ([0.95, 0.55, 0.10], 0.0, 0.2, ([1.0, 0.6, 0.15], 0.8)),
    "piece_flare":       ([0.85, 0.15, 0.08], 0.0, 0.4, None),
    "piece_sandbag":     ([0.25, 0.19, 0.09], 0.0, 0.95, None),   # linear: a khaki canvas under Level 0's lights
    "piece_sandbag_dark": ([0.16, 0.12, 0.06], 0.0, 0.95, None),
    "piece_olive":       ([0.06, 0.07, 0.03], 0.4, 0.55, None),
    "piece_seam":        ([0.55, 0.50, 1.00], 0.0, 0.2, ([0.6, 0.55, 1.0], 10.0)),
    "piece_anchor":      ([0.50, 0.55, 1.00], 0.0, 0.2, ([0.55, 0.6, 1.0], 6.0)),
    # The ways on (Tools/Levels/make_exits.py): pool water and tiles, the stairwell's concrete.
    "piece_water":       ([0.03, 0.16, 0.20], 0.0, 0.04, ([0.10, 0.45, 0.55], 1.2)),
    "piece_tile":        ([0.80, 0.84, 0.82], 0.0, 0.25, None),
    "piece_door":        ([0.30, 0.31, 0.30], 0.6, 0.45, None),
    "piece_void":        ([0.0, 0.0, 0.0], 0.0, 1.0, None),
    "piece_exit_sign":   ([0.02, 0.55, 0.12], 0.0, 0.3, ([0.05, 0.9, 0.2], 1.4)),
}


def mat_guid(name):
    if name in BASE_MATERIALS:
        return asset_guid(f"Assets/Materials/Base/{name}.material")
    assert name in mc.MATERIALS, f"unknown material {name}"
    return mc.material_guid(name)


def write_materials():
    for name, (color, metal, rough, emissive) in BASE_MATERIALS.items():
        props = {"baseColor.color": color + [1.0], "metallic.factor": metal, "roughness.factor": rough}
        if emissive:
            props.update({"emissive.enable": True, "emissive.color": emissive[0] + [1.0], "emissive.intensity": emissive[1]})
        data = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
                "materialTypeVersion": 5, "propertyValues": props}
        with open(path(f"Assets/Materials/Base/{name}.material"), "w") as f:
            json.dump(data, f, indent=4)


def life_size(parts, sx, sy=None, sz=None):
    """Scales a piece about its origin (centres and sizes), so it comes out at its real-world size."""
    sy = sx if sy is None else sy
    sz = sx if sz is None else sz
    out = []
    for shape, (cx, cy, cz), (a, b, c), mat, rot in parts:
        out.append((shape, (cx * sx, cy * sy, cz * sz), (a * sx, b * sy, c * sz), mat, rot))
    return out


# ----------------------------------------------------------------------------- the pieces' parts

def floodlight():  # the Lighting tab's floodlight (its own prefabs; these parts are only for the preview)
    return [F(0, 0, 0, 0.5, 0.5, 0.05, "metal_dark"), C(0, 0, 0.05, 0.07, 2.1, "metal_dark"),
            F(0, 0.02, 2.05, 0.5, 0.32, 0.28, "metal_dark"), F(0, -0.14, 2.09, 0.42, 0.02, 0.2, "lamp_glow")]


def ceiling_panel():
    return [F(0, 0, 2.94, 1.2, 0.6, 0.06, "lamp_glow")]


def flare_stand():
    return [F(0, 0, 0, 0.35, 0.35, 0.04, "metal_dark"), C(0, 0, 0.04, 0.06, 0.66, "metal_dark"), C(0, 0, 0.7, 0.07, 0.2, "piece_flare")]


def beacon():
    return [F(0, 0, 0, 0.8, 0.8, 0.08, "metal_dark"), F(0, 0, 0.08, 0.4, 0.4, 1.32, "brass"), C(0, 0, 1.4, 0.3, 0.3, "yellow")]


def reality_anchor():
    """The Foundation's reality anchor in the spawn room: its field keeps the Backrooms' entities out of the base."""
    p = [F(0, 0, 0, 1.1, 1.1, 0.08, "piece_foundation"), F(0, 0, 0.08, 0.9, 0.9, 0.06, "piece_hazard"),
         C(0, 0, 0.14, 0.2, 1.5, "piece_steel")]
    for z in (0.45, 0.85, 1.25):
        p.append(C(0, 0, z, 0.3, 0.06, "piece_anchor"))
    for x, y in ((0.24, 0), (-0.24, 0), (0, 0.24), (0, -0.24)):
        p.append(F(x, y, 0.14, 0.05, 0.05, 1.5, "piece_copper"))
    p += [C(0, 0, 1.64, 0.32, 0.08, "piece_foundation"), C(0, 0, 1.72, 0.12, 0.26, "piece_anchor"),
          F(0, -0.5, 0.14, 0.36, 0.14, 0.5, "piece_foundation"), F(0.1, -0.575, 0.52, 0.06, 0.01, 0.04, "piece_green_led"),
          F(-0.08, -0.575, 0.52, 0.12, 0.01, 0.05, "piece_hazard")]
    return p


def uv_lamp():
    return [F(0, 0, 0, 0.5, 0.4, 0.04, "metal_dark"), C(0, 0, 0.04, 0.06, 1.16, "metal_dark"), F(0, 0, 1.22, 0.5, 0.18, 0.1, "piece_uv")]


def light_tower():
    p = [F(0, 0, 0, 1.2, 1.2, 0.06, "piece_hazard")]
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        p.append(F(sx * 0.45, sy * 0.45, 0.06, 0.06, 0.06, 2.3, "metal_dark"))
    p += [F(0, 0, 2.36, 1.0, 1.0, 0.06, "metal_dark")]
    for sx in (-1, 1):
        for sy in (-1, 1):
            p.append(F(sx * 0.28, sy * 0.28, 2.42, 0.34, 0.24, 0.24, "metal_dark"))
            p.append(F(sx * 0.28, sy * 0.28 + sy * 0.125, 2.44, 0.28, 0.02, 0.2, "lamp_glow"))
    return p


def barricade():
    """A military barricade: a sandbag wall on a steel frame, concertina wire along the top."""
    p = [F(0, 0, 0, 2.2, 0.5, 0.04, "piece_olive"),                     # the frame's foot
         F(-1.1, -0.12, 0, 0.1, 0.1, 1.25, "piece_olive"), F(1.1, -0.12, 0, 0.1, 0.1, 1.25, "piece_olive"),
         F(0, -0.12, 1.15, 2.3, 0.08, 0.08, "piece_olive"),              # top rail
         F(-1.1, -0.18, 0.5, 0.11, 0.02, 0.25, "piece_hazard"), F(1.1, -0.18, 0.5, 0.11, 0.02, 0.25, "piece_hazard")]
    # Five courses of sandbags, staggered like a real wall, two tones of canvas.
    for row in range(5):
        z = 0.04 + row * 0.152
        offset = 0.25 if row % 2 else 0.0
        n = 4 if row % 2 else 5
        width = 2.0 / 4.2
        for k in range(n):
            x = -0.95 + offset + k * width + (0.0 if row % 2 else -0.03)
            if abs(x) > 1.0:
                continue
            tone = "piece_sandbag" if (row + k) % 3 else "piece_sandbag_dark"
            turn = ((row * 7 + k * 3) % 5 - 2) * 2.0
            p.append(F(x, 0.05, z, width * 0.96, 0.34, 0.15, tone, [0, 0, turn]))
            p.append(F(x, 0.05, z + 0.13, width * 0.8, 0.28, 0.03, tone, [0, 0, turn]))   # the bag's rounded top
    # Concertina wire: coils of razor wire along the top rail.
    r = 0.17
    for ring in range(10):
        x = -1.05 + ring * 0.233
        for side in range(8):
            m = side * 45 + 22.5 + ring * 9          # the middle of this side of the ring
            a = math.radians(m)
            cy, cz = r * math.cos(math.radians(22.5)) * math.cos(a), 1.4 + r * math.cos(math.radians(22.5)) * math.sin(a)
            p.append(B(x + 0.04 * math.sin(a), -0.12 + cy, cz, 0.012, 0.012, 2 * r * math.sin(math.radians(22.5)) + 0.01, "metal", [m, 0, 0]))
    # A steel plate over the top course, with a firing slit.
    p += [F(-0.6, -0.1, 0.8, 1.0, 0.05, 0.32, "piece_olive"), F(0.6, -0.1, 0.8, 1.0, 0.05, 0.32, "piece_olive"),
          F(0, -0.1, 0.8, 0.2, 0.05, 0.12, "piece_olive"), F(0, -0.1, 1.04, 0.2, 0.05, 0.08, "piece_olive")]
    return p


def shutter():
    # Rolled up (you walk under it); it rolls down when something comes (the monsters).
    return [F(-1.15, 0, 0, 0.12, 0.16, 2.55, "piece_steel"), F(1.15, 0, 0, 0.12, 0.16, 2.55, "piece_steel"),
            C(0, 0, 2.45, 0.34, 2.42, "metal", [0, 90, 0]), F(0, 0, 2.05, 2.18, 0.04, 0.2, "metal"),
            F(0, 0.1, 0.9, 0.12, 0.08, 0.14, "piece_hazard"), F(1.0, 0.1, 1.0, 0.08, 0.04, 0.12, "piece_green_led")]


def tripwire():
    return [F(-0.9, 0, 0, 0.04, 0.04, 0.3, "wood_dark"), F(0.9, 0, 0, 0.04, 0.04, 0.3, "wood_dark"),
            F(0, 0, 0.17, 1.8, 0.01, 0.01, "piece_red_led"), F(0.9, 0.05, 0.3, 0.12, 0.1, 0.1, "brass"),
            C(0.9, 0.05, 0.4, 0.09, 0.06, "brass")]


def flare_trap():
    p = [F(0, 0, 0, 0.34, 0.34, 0.1, "metal_dark"), F(0, 0, 0.1, 0.3, 0.3, 0.02, "piece_hazard")]
    for x, y in ((-0.08, -0.07), (0.08, -0.07), (0, 0.08)):
        p.append(C(x, y, 0.12, 0.05, 0.24, "piece_flare"))
    p.append(F(0.15, 0, 0.02, 0.4, 0.01, 0.01, "piece_red_led"))
    return p


def uv_trap():
    return [F(0, 0, 0, 0.6, 0.5, 0.05, "metal_dark"), C(-0.2, 0, 0.05, 0.05, 0.9, "metal_dark"), C(0.2, 0, 0.05, 0.05, 0.9, "metal_dark"),
            F(0, 0, 0.95, 0.8, 0.16, 0.12, "piece_steel"), F(0, 0.085, 0.96, 0.72, 0.02, 0.08, "piece_uv"),
            F(0, -0.1, 0.4, 0.3, 0.12, 0.2, "piece_steel")]


def turret():
    p = []
    for a in (0, 120, 240):
        r = math.radians(a)
        p.append(F(0.32 * math.sin(r), 0.32 * math.cos(r), 0, 0.06, 0.06, 0.06, "rubber"))
        p.append(F(0.3 * math.sin(r), 0.3 * math.cos(r), 0.06, 0.05, 0.05, 0.54, "metal_dark"))
        p.append(B(0.15 * math.sin(r), 0.15 * math.cos(r), 0.58, 0.05, 0.32, 0.05, "metal_dark", [0, 0, -a]))
    p += [C(0, 0, 0.6, 0.2, 0.12, "metal_dark"), F(0, 0, 0.72, 0.4, 0.5, 0.3, "piece_foundation"),
          C(0, 0.45, 0.87, 0.06, 0.6, "metal_dark", [90, 0, 0]), F(0.16, -0.05, 0.75, 0.12, 0.25, 0.2, "green_paint"),
          F(0, 0.26, 0.92, 0.14, 0.02, 0.06, "piece_red_led")]
    return p


def generator():
    return [F(0, 0, 0, 1.0, 0.65, 0.65, "red_paint"), F(0, 0, 0.65, 0.7, 0.45, 0.12, "red_paint"),
            F(-0.5, 0, 0, 0.04, 0.68, 0.85, "metal_dark"), F(0.5, 0, 0, 0.04, 0.68, 0.85, "metal_dark"),
            F(0, -0.32, 0.81, 1.0, 0.04, 0.04, "metal_dark"), F(0, 0.32, 0.81, 1.0, 0.04, 0.04, "metal_dark"),
            C(0.3, -0.2, 0.77, 0.12, 0.25, "metal_dark"), C(-0.25, 0.05, 0.77, 0.14, 0.05, "black_plastic"),
            F(0.2, 0.33, 0.3, 0.3, 0.02, 0.2, "black_plastic"), F(0.26, 0.345, 0.38, 0.05, 0.01, 0.03, "piece_green_led")]


def wiring_tap():
    return [F(0, 0, 0, 0.55, 0.3, 1.25, "piece_steel"), F(0, 0.155, 0.2, 0.5, 0.01, 0.95, "metal"),
            F(0, 0.165, 1.02, 0.12, 0.02, 0.12, "piece_hazard"), C(0.12, 0, 1.25, 0.06, 1.75, "metal"),
            C(-0.12, 0, 1.25, 0.06, 1.75, "rubber"), F(0.15, 0.17, 0.6, 0.06, 0.02, 0.06, "piece_green_led")]


def battery_bank():
    p = [F(0, 0, 0, 1.0, 0.5, 0.06, "metal_dark"), F(0, 0, 0.56, 1.0, 0.5, 0.04, "metal_dark")]
    for sx in (-1, 1):
        for sy in (-1, 1):
            p.append(F(sx * 0.47, sy * 0.22, 0.06, 0.04, 0.04, 0.5, "metal_dark"))
    for i in range(4):
        x = -0.36 + i * 0.24
        p += [F(x, 0, 0.06, 0.2, 0.4, 0.42, "black_plastic"), F(x - 0.05, 0.1, 0.48, 0.04, 0.04, 0.04, "red_paint"),
              F(x + 0.05, 0.1, 0.48, 0.04, 0.04, 0.04, "metal")]
    p.append(F(0.35, 0.255, 0.3, 0.16, 0.01, 0.06, "piece_green_led"))
    return p


def cable_run():
    p = [B(-1.75, 0, 0.25, 0.5, 0.18, 0.5, "wood_light"),  # a cable reel stood on end
         C(-1.75, 0, 0.25, 0.3, 0.2, "rubber", [90, 0, 0]), F(0.2, 0, 0, 3.6, 0.03, 0.02, "rubber"),
          F(1.95, 0, 0, 0.12, 0.08, 0.06, "piece_hazard")]
    return p


def breaker_box():
    return [F(0, 0, 0, 0.4, 0.3, 0.04, "metal_dark"), F(0, 0, 0.04, 0.08, 0.08, 1.0, "metal_dark"),
            F(0, 0.02, 1.0, 0.5, 0.18, 0.7, "piece_steel"), F(0, 0.115, 1.06, 0.44, 0.01, 0.58, "metal"),
            F(-0.12, 0.125, 1.5, 0.06, 0.02, 0.1, "black_plastic"), F(0, 0.125, 1.5, 0.06, 0.02, 0.1, "black_plastic"),
            F(0.12, 0.125, 1.5, 0.06, 0.02, 0.1, "black_plastic"), F(0, 0.125, 1.2, 0.3, 0.01, 0.08, "piece_hazard"),
            C(0, 0.02, 1.7, 0.05, 1.3, "rubber")]


def heavy_generator():
    return [F(0, 0, 0, 1.9, 1.0, 0.12, "metal_dark"), F(0, 0, 0.12, 1.8, 0.9, 1.0, "yellow"),
            F(0, 0, 1.12, 1.6, 0.8, 0.1, "metal_dark"), C(0.6, 0.1, 1.22, 0.16, 0.45, "metal_dark"),
            F(-0.5, 0.455, 0.4, 0.5, 0.01, 0.4, "black_plastic"), F(-0.4, 0.465, 0.65, 0.08, 0.01, 0.05, "piece_green_led"),
            F(0.3, 0.455, 0.25, 0.7, 0.01, 0.6, "metal_dark")]


def supply_crate():
    return [F(0, 0, 0, 1.0, 0.6, 0.62, "green_paint"), F(0, 0, 0.62, 1.04, 0.64, 0.06, "green_paint"),
            F(0, 0.305, 0.3, 0.5, 0.01, 0.18, "mtf_patch"), F(-0.52, 0, 0.35, 0.03, 0.2, 0.06, "metal_dark"),
            F(0.52, 0, 0.35, 0.03, 0.2, 0.06, "metal_dark")]


def workbench():
    p = [F(0, 0, 0.86, 1.6, 0.7, 0.06, "wood_light")] + legs4(1.6, 0.7, 0.86, 0.07, "metal_dark")
    p += [F(0, 0, 0.25, 1.5, 0.6, 0.03, "wood_dark"), F(0, -0.33, 0.92, 1.6, 0.04, 0.9, "wood_dark"),
          F(-0.55, 0.1, 0.92, 0.18, 0.16, 0.14, "blue_paint"), F(0.25, 0.05, 0.92, 0.5, 0.22, 0.12, "red_paint"),
          F(0.6, -0.3, 1.3, 0.04, 0.02, 0.3, "metal"), F(0.4, -0.3, 1.25, 0.03, 0.02, 0.25, "metal"),
          F(-0.2, -0.3, 1.35, 0.25, 0.02, 0.04, "metal")]
    return p


def still():
    return [C(-0.15, 0, 0, 0.6, 0.85, "piece_copper"), C(-0.15, 0, 0.85, 0.3, 0.2, "piece_copper"),
            C(0.05, 0, 1.0, 0.06, 0.45, "piece_copper", [0, 90, 0]), C(0.35, 0, 0, 0.3, 0.75, "metal"),
            C(0.35, 0, 0.75, 0.06, 0.25, "piece_copper"), F(0.35, 0.2, 0.0, 0.3, 0.16, 0.02, "metal_dark"),
            C(0.3, 0.22, 0.02, 0.07, 0.24, "piece_almond"), C(0.42, 0.22, 0.02, 0.07, 0.24, "piece_almond"),
            F(-0.15, 0.31, 0.3, 0.12, 0.02, 0.08, "piece_green_led")]


def gear_locker():
    return [F(0, 0, 0, 0.6, 0.5, 1.9, "green_paint"), F(0, 0.255, 0.05, 0.01, 0.01, 1.8, "metal_dark"),
            F(-0.15, 0.255, 1.4, 0.24, 0.01, 0.2, "metal_dark"), F(0.15, 0.255, 1.4, 0.24, 0.01, 0.2, "metal_dark"),
            F(-0.04, 0.265, 0.95, 0.02, 0.02, 0.12, "metal"), F(0.04, 0.265, 0.95, 0.02, 0.02, 0.12, "metal"),
            F(0, 0.26, 1.7, 0.3, 0.01, 0.06, "mtf_patch")]


def army_cot():
    p = [F(0, 0, 0.36, 1.9, 0.7, 0.05, "piece_canvas"), F(0, -0.36, 0.34, 1.9, 0.04, 0.04, "metal_dark"),
         F(0, 0.36, 0.34, 1.9, 0.04, 0.04, "metal_dark")]
    for x in (-0.85, 0, 0.85):
        p.append(B(x, 0, 0.18, 0.04, 0.75, 0.04, "metal_dark", [45, 0, 0]))
        p.append(B(x, 0, 0.18, 0.04, 0.75, 0.04, "metal_dark", [-45, 0, 0]))
    p += [F(-0.72, 0, 0.41, 0.4, 0.55, 0.1, "white_plastic"), F(0.25, 0, 0.41, 1.1, 0.66, 0.04, "fabric_green")]
    return p


def storage_rack():
    p = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            p.append(F(sx * 0.87, sy * 0.22, 0, 0.05, 0.05, 2.0, "piece_hazard"))
    for z in (0.15, 0.75, 1.35, 1.95):
        p.append(F(0, 0, z, 1.8, 0.5, 0.04, "metal_dark"))
    p += [F(-0.5, 0, 0.19, 0.6, 0.45, 0.4, "cardboard"), F(0.3, 0, 0.19, 0.5, 0.4, 0.3, "green_paint"),
          F(-0.3, 0, 0.79, 0.7, 0.45, 0.35, "cardboard"), F(0.5, 0, 0.79, 0.4, 0.4, 0.45, "cardboard"),
          F(0, 0, 1.39, 0.9, 0.45, 0.3, "green_paint")]
    return p


def research_desk():
    p = [F(0, 0, 0.72, 1.4, 0.7, 0.05, "wood_dark"), F(-0.65, 0, 0, 0.05, 0.65, 0.72, "wood_dark"),
         F(0.65, 0, 0, 0.05, 0.65, 0.72, "wood_dark"), F(-0.2, 0.1, 0.77, 0.45, 0.35, 0.38, "beige_plastic"),
         F(-0.2, 0.28, 0.82, 0.38, 0.01, 0.28, "screen_glow"), F(0.35, -0.05, 0.77, 0.3, 0.22, 0.04, "paper"),
         F(0.38, 0.02, 0.81, 0.26, 0.2, 0.02, "mtf_patch"), C(0.55, -0.2, 0.77, 0.1, 0.35, "metal_dark"),
         F(0.5, -0.12, 1.08, 0.16, 0.14, 0.08, "lamp_glow")]
    return p


def sample_scanner():
    return [F(0, 0, 0, 0.6, 0.6, 0.9, "white_plastic"), F(0, 0.305, 0.55, 0.4, 0.01, 0.25, "screen_glow"),
            C(0, -0.05, 0.9, 0.42, 0.06, "metal"), C(0, -0.05, 0.96, 0.36, 0.3, "glass_dark"),
            C(0, -0.05, 0.96, 0.08, 0.18, "piece_green_led"), F(0.2, 0.305, 0.25, 0.08, 0.01, 0.08, "piece_red_led")]


def field_radio():
    p = [F(0, 0, 0.72, 0.7, 0.45, 0.04, "wood_light")] + legs4(0.7, 0.45, 0.72, 0.04, "metal_dark")
    p += [F(0, 0, 0.76, 0.45, 0.26, 0.24, "green_paint"), F(0, 0.135, 0.8, 0.4, 0.01, 0.16, "black_plastic"),
          C(0.08, 0.14, 0.88, 0.05, 0.02, "metal", [90, 0, 0]), C(-0.08, 0.14, 0.88, 0.05, 0.02, "metal", [90, 0, 0]),
          B(0.18, -0.05, 1.3, 0.01, 0.01, 0.85, "metal", [12, 0, 0]), F(-0.25, 0.05, 0.76, 0.12, 0.1, 0.04, "black_plastic")]
    return p


def containment_cell():
    p = [F(0, 0, 0, 2.08, 2.08, 0.05, "piece_concrete")]
    for sx in (-1, 1):
        for sy in (-1, 1):
            p.append(F(sx * 1.0, sy * 1.0, 0.05, 0.08, 0.08, 2.35, "piece_foundation"))
    for x in (-0.6, -0.2, 0.2, 0.6):
        p.append(F(x, 1.0, 0.05, 0.03, 0.03, 2.35, "metal"))
    for y in (-0.5, 0.0, 0.5):
        p.append(F(-1.0, y, 0.05, 0.03, 0.03, 2.35, "metal"))
        p.append(F(1.0, y, 0.05, 0.03, 0.03, 2.35, "metal"))
    p += [F(0, -1.0, 0.05, 2.0, 0.04, 2.35, "piece_steel"), F(0, 0, 2.4, 2.08, 2.08, 0.08, "piece_foundation"),
          F(0, 1.0, 1.2, 2.0, 0.04, 0.06, "piece_hazard"), F(0.85, 1.05, 1.1, 0.1, 0.04, 0.16, "piece_red_led")]
    return p


def light_cage():
    p = [F(0, 0, 0, 1.6, 1.6, 0.05, "piece_hazard")]
    for sx in (-1, 1):
        for sy in (-1, 1):
            p.append(F(sx * 0.77, sy * 0.77, 0.05, 0.06, 0.06, 2.1, "metal_dark"))
    for i in range(5):
        t = -0.6 + i * 0.3
        p += [F(t, 0.77, 0.05, 0.02, 0.02, 2.1, "metal"), F(t, -0.77, 0.05, 0.02, 0.02, 2.1, "metal"),
              F(0.77, t, 0.05, 0.02, 0.02, 2.1, "metal"), F(-0.77, t, 0.05, 0.02, 0.02, 2.1, "metal")]
    p += [F(0, 0, 2.15, 1.6, 1.6, 0.05, "metal_dark"), F(0, 0, 2.06, 0.5, 0.5, 0.09, "piece_warm_lamp")]
    return p


def camera():
    return [F(0, 0, 0, 0.36, 0.36, 0.04, "metal_dark"), C(0, 0, 0.04, 0.06, 2.1, "metal_dark"),
            F(0, 0.08, 2.1, 0.14, 0.32, 0.14, "white_plastic"), F(0, 0.245, 2.13, 0.08, 0.01, 0.08, "glass_dark"),
            F(0.05, 0.245, 2.2, 0.02, 0.01, 0.02, "piece_red_led"), F(0, 0.02, 2.24, 0.2, 0.42, 0.02, "white_plastic")]


def blast_door():
    # Open (slid aside): it closes when something comes.
    return [F(-1.35, 0, 0, 0.3, 0.5, 2.7, "piece_foundation"), F(1.35, 0, 0, 0.3, 0.5, 2.7, "piece_foundation"),
            F(0, 0, 2.5, 3.0, 0.5, 0.2, "piece_foundation"), F(-1.35, 0.255, 0.1, 0.2, 0.01, 2.3, "piece_hazard"),
            F(1.35, 0.255, 0.1, 0.2, 0.01, 2.3, "piece_hazard"), F(-1.95, -0.1, 0, 1.2, 0.2, 2.45, "piece_steel"),
            F(1.2, 0.26, 1.4, 0.1, 0.02, 0.1, "piece_green_led")]


def pen_999():
    p = [F(0, 0, 0, 2.0, 2.0, 0.04, "piece_concrete")]
    for i in range(5):
        t = -0.8 + i * 0.4
        p += [F(t, 0.98, 0.04, 0.03, 0.03, 0.8, "metal"), F(t, -0.98, 0.04, 0.03, 0.03, 0.8, "metal"),
              F(0.98, t, 0.04, 0.03, 0.03, 0.8, "metal"), F(-0.98, t, 0.04, 0.03, 0.03, 0.8, "metal")]
    p += [F(0, 0.98, 0.8, 2.0, 0.04, 0.04, "metal"), F(0, -0.98, 0.8, 2.0, 0.04, 0.04, "metal"),
          F(0.98, 0, 0.8, 0.04, 2.0, 0.04, "metal"), F(-0.98, 0, 0.8, 0.04, 2.0, 0.04, "metal"),
          F(0.5, 0.5, 0.04, 0.5, 0.5, 0.06, "rubber"), F(-0.4, -0.3, 0.04, 0.3, 0.3, 0.12, "red_paint")]
    return p


def scp_914():
    p = [F(0, 0, 0, 2.6, 1.2, 0.1, "piece_concrete"), F(0, 0, 0.1, 0.9, 1.0, 2.0, "brass"),
         F(0, 0.51, 1.3, 0.5, 0.01, 0.3, "wood_light"), C(0, 0.53, 1.45, 0.16, 0.02, "metal", [90, 0, 0])]
    for sx in (-1, 1):
        p += [F(sx * 0.95, 0, 0.1, 0.7, 0.9, 1.9, "metal"), F(sx * 0.95, 0.46, 0.2, 0.5, 0.02, 1.6, "metal_dark"),
              F(sx * 0.95, 0.47, 1.85, 0.3, 0.01, 0.1, "brass")]
    p += [F(0, 0, 2.1, 0.5, 0.5, 0.4, "brass"), C(0.0, 0.52, 0.8, 0.3, 0.03, "metal", [90, 0, 0])]
    return p


# ----------------------------------------------------------------------------- the tiles

# Unlocks: what opens a locked tile (Base::Unlock).
NONE, POWER2, STORAGE2, WEAPONS1, TIER3, CAPTURE999 = range(6)
# Lights a piece puts up (the Lighting tab's light prefabs): 0 none, 1 warm floodlight, 2 UV, 3 red flare, 4 gold.
L_NONE, L_WARM, L_UV, L_RED, L_GOLD = range(5)
# Things you can [E] at.
USE_NONE, USE_REST, USE_CRAFT, USE_STUDY, USE_RADIO, USE_STORE = range(6)

TABS = ["LIGHTING", "DEFENSES", "POWER", "STORAGE", "RESEARCH", "CONTAINMENT"]

# One entry per tile, tab by tab, in the mockups' order. Fields:
#   id, tile name, label name, cost text (None: "Scrap 4 · Wire 2" from the costs), scrap, wire, fuel, icon,
#   parts, prefab (None: the Lighting tab's own pieces, or a research project), and options:
#   power (kW drawn), supply (kW made), battery (kWh), holds (storage), unlock, lock (locked tile's note),
#   project (docs, samples): a research project, not a piece; light, light_z; use; node (carries power);
#   cover (covers a doorway); comfort; note (shown on the placement label when it fits); collide; fuel_min (minutes a fuel can lasts).
TILES = [
    # LIGHTING (approved before, Build.dc.html; their own prefabs from make_base.py)
    ("Floodlight", "Floodlight", "FLOODLIGHT", None, 4, 2, 0, "", floodlight(), None, dict(power=1.0, light=L_WARM, comfort=1)),
    ("CeilingPanel", "Ceiling panel fix", "CEILING PANEL FIX", None, 0, 1, 0, "", ceiling_panel(), None, dict(power=0.5)),
    ("FlareStand", "Flare stand", "FLARE STAND", None, 2, 0, 1, "", flare_stand(), None, dict(light=L_RED)),
    ("Beacon", "Foundation beacon", "FOUNDATION BEACON", None, 10, 6, 0, "", beacon(), None, dict(power=2.0, comfort=1)),
    ("UvLamp", "UV lamp", "UV LAMP", None, 6, 4, 0, "", uv_lamp(), None, dict(power=1.5)),
    ("LightTower", "Light tower", "LIGHT TOWER", "Scrap 14 \u00b7 Wire 8 \u00b7 3 kW", 14, 8, 0, "", light_tower(), "LightTower",
     dict(power=3.0, unlock=POWER2, lock="Research: Power II", light=L_WARM, light_z=2.5, comfort=1)),
    # DEFENSES
    ("Barricade", "Barricade", "BARRICADE", "Scrap 6", 6, 0, 0, "barricade", barricade(), "Barricade", dict(cover=True)),
    ("Shutter", "Roll-down shutter", "ROLL-DOWN SHUTTER", "Scrap 8 · Wire 2 · 1 kW", 8, 2, 0, "shutter", shutter(), "Shutter",
     dict(power=1.0, cover=True, collide=False)),
    ("Tripwire", "Tripwire alarm", "TRIPWIRE ALARM", "Wire 3", 0, 3, 0, "tripwire", tripwire(), "Tripwire", dict(collide=False)),
    ("FlareTrap", "Flare trap", "FLARE TRAP", "Scrap 2 · Fuel 1", 2, 0, 1, "flaretrap", flare_trap(), "FlareTrap", dict(collide=False)),
    ("UvTrap", "UV light trap", "UV LIGHT TRAP", "Scrap 6 · Wire 4 · 2 kW", 6, 4, 0, "uvtrap", uv_trap(), "UvTrap",
     dict(power=2.0, light=L_UV, light_z=1.0)),
    ("Turret", "Sentry turret", "SENTRY TURRET", "Scrap 14 \u00b7 Wire 10 \u00b7 2 kW", 14, 10, 0, "turret", life_size(turret(), 1.35), "Turret",
     dict(power=2.0, unlock=WEAPONS1, lock="Research: Weapons I")),
    # POWER
    ("Generator", "Fuel generator", "FUEL GENERATOR", "Scrap 10 · Wire 4 → +6 kW", 10, 4, 0, "generator", generator(), "Generator",
     dict(supply=6.0, node=True, note="needs a clear 2 × 2 m · LOUD", clear=2.0, fuel_min=14.0)),
    ("WiringTap", "Wiring tap", "WIRING TAP", "Wire 6 → +2 kW", 0, 6, 0, "tap", wiring_tap(), "WiringTap", dict(supply=2.0, node=True)),
    ("Battery", "Battery bank", "BATTERY BANK", "Scrap 6 · Wire 6 · stores 20 kWh", 6, 6, 0, "battery", battery_bank(), "Battery",
     dict(battery=20.0, node=True)),
    ("CableRun", "Cable run", "CABLE RUN", "Wire 1 per 4 m", 0, 1, 0, "cable", cable_run(), "CableRun", dict(node=True, collide=False)),
    ("Breaker", "Breaker box", "BREAKER BOX", "Scrap 4 · Wire 3", 4, 3, 0, "breaker", breaker_box(), "Breaker", dict(node=True)),
    ("HeavyGenerator", "Power II", "HEAVY GENERATOR", "Scrap 16 \u00b7 Wire 8 \u2192 +10 kW", 16, 8, 0, "solar", life_size(heavy_generator(), 1.3, 1.1, 1.0), "HeavyGenerator",
     dict(supply=10.0, node=True, unlock=POWER2, lock="Research: Power II", note="LOUD", clear=2.6, fuel_min=10.0)),
    # STORAGE
    ("SupplyCrate", "Supply crate", "SUPPLY CRATE", "Scrap 6 · holds 40", 6, 0, 0, "crate", life_size(supply_crate(), 1.12), "SupplyCrate",
     dict(holds=40, use=USE_STORE)),
    ("Workbench", "Workbench", "WORKBENCH", "Scrap 12 · Wire 4", 12, 4, 0, "workbench", workbench(), "Workbench", dict(use=USE_CRAFT, comfort=1)),
    ("Still", "Almond Water still", "ALMOND WATER STILL", "Scrap 8 · Wire 6 · 1 kW", 8, 6, 0, "still", life_size(still(), 1.4), "Still",
     dict(power=1.0, comfort=1)),
    ("GearLocker", "Gear locker", "GEAR LOCKER", "Scrap 8", 8, 0, 0, "locker", gear_locker(), "GearLocker", dict(comfort=1)),
    ("ArmyCot", "Army cot", "ARMY COT", "Scrap 4 · rest here", 4, 0, 0, "cot", army_cot(), "ArmyCot", dict(use=USE_REST, comfort=2)),
    ("StorageRack", "Storage rack", "STORAGE RACK", "Scrap 10 \u00b7 Wire 2 \u00b7 holds 80", 10, 2, 0, "rack", storage_rack(), "StorageRack",
     dict(holds=80, unlock=STORAGE2, lock="Research: Storage II", use=USE_STORE)),
    # RESEARCH
    ("ResearchDesk", "Research desk", "RESEARCH DESK", "Scrap 10 · Wire 6", 10, 6, 0, "desk", research_desk(), "ResearchDesk",
     dict(use=USE_STUDY, comfort=1, note="needs light to read by")),
    ("SampleScanner", "Sample scanner", "SAMPLE SCANNER", "Scrap 8 · Wire 8 · 2 kW", 8, 8, 0, "scanner", sample_scanner(), "SampleScanner",
     dict(power=2.0)),
    ("FieldRadio", "Field radio", "FIELD RADIO", "Scrap 4 · Wire 4", 4, 4, 0, "radio", field_radio(), "FieldRadio", dict(use=USE_RADIO, comfort=1)),
    ("StudyPowerII", "Power II", "POWER II", "12 documents", 0, 0, 0, "solar", [], None, dict(project=(12, 0))),
    ("StudyStorageII", "Storage II", "STORAGE II", "8 documents · 2 samples", 0, 0, 0, "rack", [], None, dict(project=(8, 2))),
    ("WeaponsI", "Weapons I", "WEAPONS I", None, 0, 0, 0, "turret", [], None, dict(project=(0, 0), unlock=WEAPONS1, lock="Order from the main base")),
    # CONTAINMENT
    ("ContainmentCell", "Containment cell", "CONTAINMENT CELL", "Scrap 20 · Wire 10 · 3 kW", 20, 10, 0, "cell", life_size(containment_cell(), 1.44, 1.4, 1.08), "ContainmentCell",
     dict(power=3.0)),
    ("LightCage", "Light cage", "LIGHT CAGE", "Scrap 12 · Wire 8 · 2 kW", 12, 8, 0, "lightcage", life_size(light_cage(), 1.25, 1.25, 1.13), "LightCage",
     dict(power=2.0, light=L_WARM, light_z=2.32)),
    ("Camera", "Observation camera", "OBSERVATION CAMERA", "Wire 4 · 1 kW", 0, 4, 0, "camera", camera(), "Camera", dict(power=1.0, collide=False)),
    ("BlastDoor", "Blast door", "BLAST DOOR", "Scrap 16 · Wire 6", 16, 6, 0, "door", blast_door(), "BlastDoor", dict(cover=True, collide=False)),
    ("Pen999", "SCP-999 pen", "SCP-999 PEN", "Scrap 10 \u00b7 Wire 4", 10, 4, 0, "cell", life_size(pen_999(), 1.5, 1.5, 1.2), "Pen999", dict(unlock=CAPTURE999, lock="Capture SCP-999")),
    ("Scp914", "SCP-914", "SCP-914", "Scrap 30 \u00b7 Wire 20 \u00b7 3 kW", 30, 20, 0, "workbench", life_size(scp_914(), 1.6, 1.5, 1.1), "Scp914", dict(power=3.0, unlock=TIER3, lock="Research: Tier III")),
]
assert len(TILES) == 36


# ----------------------------------------------------------------------------- the icons (from the mockups' SVG)

ICONS = {
    "barricade": '<path d="M4 20 H24 M6 20 V10 M22 20 V10 M4 10 H24 M4 15 H24 M8 10 L12 15 M16 10 L20 15" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "shutter": '<rect x="5" y="4" width="18" height="20" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M5 9 H23 M5 13 H23 M5 17 H23" stroke="{c}" stroke-width="1.4"/>',
    "tripwire": '<path d="M4 20 H24" stroke="{c}" stroke-width="1.6" stroke-dasharray="2 2"/><circle cx="5" cy="20" r="2" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M22 20 V10 M19 10 H25 M20 7 Q22 4 24 7" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "flaretrap": '<path d="M14 24 V12 M10 12 H18 L16 7 H12 Z" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M8 5 L10 7 M20 5 L18 7 M14 2 V4" stroke="{c}" stroke-width="1.6"/>',
    "uvtrap": '<rect x="6" y="8" width="16" height="8" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M8 16 L5 24 M20 16 L23 24 M14 16 V24" stroke="{c}" stroke-width="1.4" stroke-dasharray="2 2"/>',
    "turret": '<path d="M8 24 H20 L18 18 H10 Z M12 18 V13 H16 V18 M16 15 H25" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "generator": '<rect x="4" y="9" width="20" height="13" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M15 11 L11 16 H15 L13 20" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M7 9 V6 H11 V9" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "tap": '<path d="M4 6 H24 M4 22 H24" stroke="{c}" stroke-width="1.6"/><path d="M10 6 V12 H18 V6 M14 12 V22" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M11 17 L14 14 L17 17" fill="none" stroke="{c}" stroke-width="1.4"/>',
    "battery": '<rect x="5" y="9" width="17" height="11" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M22 12 H24 V17 H22 M8 12 V17 M11 12 V17 M14 12 V17" stroke="{c}" stroke-width="1.6"/>',
    "cable": '<path d="M4 20 C9 20 9 8 14 8 S19 20 24 20" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="4" cy="20" r="2" fill="{c}"/><circle cx="24" cy="20" r="2" fill="{c}"/>',
    "breaker": '<rect x="7" y="4" width="14" height="20" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M11 9 H17 M11 14 H17 M11 19 H17" stroke="{c}" stroke-width="1.6"/>',
    "solar": '<path d="M5 22 L9 8 H23 L19 22 Z M7 15 H21 M14 8 L12 22" fill="none" stroke="{c}" stroke-width="1.4"/>',
    "crate": '<rect x="5" y="9" width="18" height="14" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M5 13 H23 M12 13 V17 H16 V13" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "workbench": '<path d="M4 12 H24 M6 12 V23 M22 12 V23 M6 18 H22" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M10 12 V7 M8 7 H12 M18 12 L20 6" stroke="{c}" stroke-width="1.6"/>',
    "still": '<path d="M9 23 H19 L18 12 H10 Z M12 12 V7 H16 V12 M16 8 H21 V16" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M21 18 V20" stroke="{c}" stroke-width="1.6"/>',
    "locker": '<rect x="7" y="4" width="14" height="20" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M14 4 V24 M11 11 V13 M17 11 V13" stroke="{c}" stroke-width="1.6"/>',
    "cot": '<path d="M4 15 H24 M6 15 L5 22 M22 15 L23 22 M4 12 H10" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "rack": '<path d="M5 4 V24 M23 4 V24 M5 9 H23 M5 15 H23 M5 21 H23" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "desk": '<path d="M4 13 H24 M6 13 V23 M22 13 V23" fill="none" stroke="{c}" stroke-width="1.6"/><rect x="9" y="5" width="10" height="7" fill="none" stroke="{c}" stroke-width="1.4"/>',
    "scanner": '<circle cx="14" cy="14" r="9" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M14 14 L20 8" stroke="{c}" stroke-width="1.6"/><circle cx="14" cy="14" r="2" fill="{c}"/>',
    "radio": '<rect x="5" y="12" width="18" height="11" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M9 12 L20 4" stroke="{c}" stroke-width="1.6"/><circle cx="11" cy="17" r="2.5" fill="none" stroke="{c}" stroke-width="1.4"/>',
    "cell": '<rect x="5" y="5" width="18" height="18" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M10 5 V23 M14 5 V23 M18 5 V23" stroke="{c}" stroke-width="1.4"/>',
    "lightcage": '<path d="M5 23 H23 M7 23 V8 H21 V23" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="14" cy="8" r="2" fill="{c}"/><path d="M14 10 L10 18 M14 10 L18 18" stroke="{c}" stroke-width="1.2" stroke-dasharray="2 2"/>',
    "lock": '<rect x="7" y="12" width="14" height="11" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M10 12 V9 A4 4 0 0 1 18 9 V12" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "camera": '<rect x="5" y="9" width="13" height="9" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M18 11 L23 8 V19 L18 16 M8 18 V23" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "door": '<rect x="7" y="4" width="14" height="20" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M11 4 V24 M17 4 V24" stroke="{c}" stroke-width="1.2"/><circle cx="14" cy="14" r="1.5" fill="{c}"/>',
}
ICON_NAMES = list(ICONS)


def attrs(tag):
    return dict(re.findall(r'([\w-]+)="([^"]*)"', tag))


def path_polylines(d):
    """SVG path (absolute M L H V Q C S A Z) to polylines."""
    tokens = re.findall(r"[MLHVQCSAZ]|-?\d*\.?\d+", d)
    out, cur, start, last_ctrl = [], None, None, None
    i, cmd = 0, None

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    while i < len(tokens):
        if re.match(r"[A-Z]", tokens[i]):
            cmd = tokens[i]
            i += 1
        if cmd == "M":
            cur = (num(), num())
            start = cur
            out.append([cur])
            cmd = "L"
            last_ctrl = None
        elif cmd == "L":
            cur = (num(), num())
            out[-1].append(cur)
            last_ctrl = None
        elif cmd == "H":
            cur = (num(), cur[1])
            out[-1].append(cur)
        elif cmd == "V":
            cur = (cur[0], num())
            out[-1].append(cur)
        elif cmd == "Z":
            out[-1].append(start)
            cur = start
            cmd = None
        elif cmd == "Q":
            c1, end = (num(), num()), (num(), num())
            for k in range(1, 9):
                t = k / 8
                out[-1].append(((1 - t) ** 2 * cur[0] + 2 * (1 - t) * t * c1[0] + t * t * end[0],
                                (1 - t) ** 2 * cur[1] + 2 * (1 - t) * t * c1[1] + t * t * end[1]))
            cur = end
        elif cmd in "CS":
            if cmd == "C":
                c1 = (num(), num())
            else:
                c1 = (2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1]) if last_ctrl else cur
            c2, end = (num(), num()), (num(), num())
            for k in range(1, 13):
                t = k / 12
                a, b, c, e = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
                out[-1].append((a * cur[0] + b * c1[0] + c * c2[0] + e * end[0], a * cur[1] + b * c1[1] + c * c2[1] + e * end[1]))
            last_ctrl = c2
            cur = end
            continue
        elif cmd == "A":
            rx, _ry, _rot, large, sweep, x, y = num(), num(), num(), num(), num(), num(), num()
            # Only the lock's shackle: a half circle of radius rx between cur and (x, y), bulging up.
            cx, cy = (cur[0] + x) / 2, (cur[1] + y) / 2
            a0 = math.atan2(cur[1] - cy, cur[0] - cx)
            a1 = math.atan2(y - cy, x - cx)
            if sweep and a1 < a0:
                a1 += 2 * math.pi
            for k in range(1, 13):
                a = a0 + (a1 - a0) * k / 12
                out[-1].append((cx + rx * math.cos(a), cy + rx * math.sin(a)))
            cur = (x, y)
        if cmd not in ("C", "S"):
            last_ctrl = None
    return out


def icon_data(svg):
    strokes, fills = [], []
    for tag in re.findall(r"<(?:path|rect|circle)[^>]*>", svg):
        a = attrs(tag)
        width = float(a.get("stroke-width", "1.6"))
        dashed = "stroke-dasharray" in a
        if tag.startswith("<path"):
            for line in path_polylines(a["d"]):
                strokes.append((width, dashed, line))
        elif tag.startswith("<rect"):
            x, y, w, h = (float(a[k]) for k in ("x", "y", "width", "height"))
            strokes.append((width, dashed, [(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]))
        else:
            cx, cy, r = float(a["cx"]), float(a["cy"]), float(a["r"])
            if a.get("fill", "none") != "none":
                fills.append((cx, cy, r))
            else:
                strokes.append((width, dashed, [(cx + r * math.cos(t * math.pi / 12), cy + r * math.sin(t * math.pi / 12)) for t in range(25)]))
    return strokes, fills


def write_icons():
    lines = ["#pragma once", "", "// Generated by Tools/Base/make_base_pieces.py: the base-building tiles' icons, traced from the approved",
             "// mockups' 28 x 28 SVG icons (1.6 px strokes). Do not edit by hand.", "", "namespace NoclipProtocol::BuildIcons", "{",
             "    struct Stroke { float m_width; bool m_dashed; int m_first; int m_count; }; // points m_first.. in Points",
             "    struct Fill { float m_x, m_y, m_r; };",
             "    struct Icon { int m_firstStroke; int m_strokes; int m_firstFill; int m_fills; };", ""]
    points, strokes, fills, icons = [], [], [], []
    for name in ICON_NAMES:
        s, f = icon_data(ICONS[name])
        icons.append((len(strokes), len(s), len(fills), len(f)))
        for width, dashed, line in s:
            strokes.append((width, dashed, len(points), len(line)))
            points.extend(line)
        fills.extend(f)
    lines.append(f"    constexpr int IconCount = {len(ICON_NAMES)};")
    for k, name in enumerate(ICON_NAMES):
        lines.append(f"    constexpr int Icon_{name} = {k};")
    lines.append(f"    constexpr float Points[{len(points)}][2] = {{")
    for k in range(0, len(points), 6):
        lines.append("        " + " ".join(f"{{ {x:.2f}f, {y:.2f}f }}," for x, y in points[k:k + 6]))
    lines.append("    };")
    lines.append(f"    constexpr Stroke Strokes[{len(strokes)}] = {{")
    for width, dashed, first, count in strokes:
        lines.append(f"        {{ {width:.1f}f, {'true' if dashed else 'false'}, {first}, {count} }},")
    lines.append("    };")
    lines.append(f"    constexpr Fill Fills[{max(1, len(fills))}] = {{")
    for x, y, r in fills or [(0, 0, 0)]:
        lines.append(f"        {{ {x:.2f}f, {y:.2f}f, {r:.2f}f }},")
    lines.append("    };")
    lines.append("    constexpr Icon Icons[IconCount] = {")
    for (a, b, c, d), name in zip(icons, ICON_NAMES):
        lines.append(f"        {{ {a}, {b}, {c}, {d} }}, // {name}")
    lines += ["    };", "} // namespace NoclipProtocol::BuildIcons", ""]
    with open(path("Gem/Source/Hud/BuildIcons.h"), "w") as f:
        f.write("\n".join(lines))


# ----------------------------------------------------------------------------- the catalog

def utf8_c(s):
    """A C string literal for s with its non-ASCII bytes escaped (and cut so a hex escape never eats a letter)."""
    if s is None:
        return "nullptr"
    out = '"'
    pending = False
    for b in s.encode("utf-8"):
        ch = chr(b)
        if b >= 0x80:
            out += f"\\x{b:02x}"
            pending = True
        else:
            if pending and ch in "0123456789abcdefABCDEF":
                out += '" "'
            pending = False
            out += '\\"' if ch == '"' else ch
    return out + '"'


def write_catalog():
    names = [t[0] for t in TILES]
    lines = ["#pragma once", "", "// Generated by Tools/Base/make_base_pieces.py: do not edit by hand, edit the tool and rerun it.",
             "// Every base-building tile from the approved Build mockups (six tabs of six), what each piece costs and does,",
             "// and its parts (boxes and cylinders, meters, front facing +Y) for the see-through placement preview.", "",
             "namespace NoclipProtocol::Base", "{",
             "    enum class Kind : int", "    {"]
    for k, n in enumerate(names):
        lines.append(f"        {n}{' = 0' if k == 0 else ''},")
    lines += ["        Count", "    };", "",
              "    enum class Unlock : int { None = 0, PowerII, StorageII, WeaponsI, TierIII, Capture999 };",
              "    enum class Use : int { None = 0, Rest, Craft, Study, Radio, Store };",
              "    enum class Light : int { None = 0, Warm, Uv, Red, Gold };", "",
              "    constexpr int TabCount = 6;",
              "    constexpr const char* TabNames[TabCount] = { " + ", ".join(f'"{t}"' for t in TABS) + " };", "",
              "    struct Part { bool m_cylinder; float m_x, m_y, m_z; float m_sx, m_sy, m_sz; float m_rx, m_ry, m_rz; };", "",
              "    struct ItemInfo",
              "    {",
              "        const char* m_name;     // as on the tile",
              "        const char* m_upper;    // as on the placement label",
              "        const char* m_costText; // the tile's second line as in the mockup (nullptr: from the costs)",
              "        int m_scrap, m_wire, m_fuel;",
              "        float m_power;          // kW drawn while it stands and has power",
              "        float m_supply;         // kW it makes (generators, wiring taps)",
              "        float m_battery;        // kWh it stores",
              "        int m_holds;            // storage it adds",
              "        float m_sizeX, m_sizeY, m_height; // footprint (m), before rotating, and height",
              "        int m_tab;",
              "        int m_icon;             // BuildIcons index (-1: the Lighting tab's own icons)",
              "        Unlock m_unlock;        // what opens it (None: open from the start)",
              "        const char* m_lockNote; // the locked tile's second line",
              "        bool m_project;         // a research project, not a piece",
              "        int m_docs, m_samples;  // a project's cost",
              "        const char* m_prefab;   // prefabs/base/piece_<lower>.spawnable (nullptr: the Lighting tab's own, or a project)",
              "        Light m_light;          // a light it puts up",
              "        float m_lightZ;",
              "        Use m_use;              // [E] at it",
              "        bool m_node;            // carries power (generators, taps, batteries, breakers, cables)",
              "        bool m_cover;           // covers a doorway",
              "        int m_comfort;",
              "        const char* m_note;     // on the placement label when it fits",
              "        float m_clear;          // needs this much clear floor (m, square) around it",
              "        float m_fuelMinutes;    // a fuel can lasts this long in it",
              "        int m_firstPart, m_parts;",
              "    };", ""]
    parts = []
    lines.append(f"    constexpr int KindCount = {len(TILES)};")
    lines.append("    inline const ItemInfo& Info(Kind kind)")
    lines.append("    {")
    lines.append("        static const ItemInfo infos[KindCount] = {")
    unlock_names = ["None", "PowerII", "StorageII", "WeaponsI", "TierIII", "Capture999"]
    light_names = ["None", "Warm", "Uv", "Red", "Gold"]
    use_names = ["None", "Rest", "Craft", "Study", "Radio", "Store"]
    for k, (kid, name, upper, cost, scrap, wire, fuel, icon, pp, prefab, o) in enumerate(TILES):
        tab = k // 6
        if pp:
            w, d, h = mc.bounds(pp)
        else:
            w, d, h = 1.0, 1.0, 1.0
        # The Lighting tab keeps its approved footprints.
        lighting = {"Floodlight": (1.0, 0.7), "CeilingPanel": (1.2, 0.6), "FlareStand": (0.6, 0.6), "Beacon": (0.8, 0.8),
                    "UvLamp": (0.8, 0.6), "LightTower": (1.2, 1.2)}
        if kid in lighting:
            w, d = lighting[kid]
        first = len(parts)
        for shape, center, size, _mat, rot in pp:
            parts.append((shape == "cyl", center, size, rot or (0, 0, 0)))
        docs, samples = o.get("project", (0, 0))
        lines.append(
            f"            {{ {utf8_c(name)}, {utf8_c(upper)}, {utf8_c(cost)}, {scrap}, {wire}, {fuel}, {o.get('power', 0.0):.1f}f, "
            f"{o.get('supply', 0.0):.1f}f, {o.get('battery', 0.0):.1f}f, {o.get('holds', 0)}, {w:.2f}f, {d:.2f}f, {h:.2f}f, {tab}, "
            f"{ICON_NAMES.index(icon) if icon else -1}, Unlock::{unlock_names[o.get('unlock', NONE)]}, {utf8_c(o.get('lock'))}, "
            f"{'true' if 'project' in o else 'false'}, {docs}, {samples}, "
            f"{utf8_c(('prefabs/base/piece_' + prefab.lower() + '.spawnable') if prefab else None)}, Light::{light_names[o.get('light', L_NONE)]}, "
            f"{o.get('light_z', 0.0):.2f}f, Use::{use_names[o.get('use', USE_NONE)]}, {'true' if o.get('node') else 'false'}, "
            f"{'true' if o.get('cover') else 'false'}, {o.get('comfort', 0)}, {utf8_c(o.get('note'))}, {o.get('clear', 0.0):.1f}f, "
            f"{o.get('fuel_min', 0.0):.1f}f, {first}, {len(pp)} }}, // {kid}")
    lines += ["        };", "        return infos[static_cast<int>(kind) < 0 || static_cast<int>(kind) >= KindCount ? 0 : static_cast<int>(kind)];",
              "    }", ""]
    lines.append(f"    constexpr int PartCount = {len(parts)};")
    lines.append("    constexpr Part Parts[PartCount] = {")
    for cyl, c, s, r in parts:
        lines.append(f"        {{ {'true' if cyl else 'false'}, {c[0]:.3f}f, {c[1]:.3f}f, {c[2]:.3f}f, {s[0]:.3f}f, {s[1]:.3f}f, {s[2]:.3f}f, "
                     f"{float(r[0]):.1f}f, {float(r[1]):.1f}f, {float(r[2]):.1f}f }},")
    lines += ["    };", "} // namespace NoclipProtocol::Base", ""]
    with open(path("Gem/Source/Base/BaseCatalog.h"), "w") as f:
        f.write("\n".join(lines))


def main():
    mc.write_materials()  # the clutter's materials (shared), in case they aren't there yet
    write_materials()
    with open(path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    built = 0
    for kid, _n, _u, _c, _s, _w, _f, _i, pp, prefab, o in TILES:
        if not prefab:
            continue
        spec = {"parts": pp, "size": mc.bounds(pp), "collide": o.get("collide", True)}
        mc.build_prefab(f"Piece_{prefab}", spec, template, folder="Prefabs/Base", mat_guid=mat_guid)
        built += 1
    # Breach 4-A sealed: its glowing seam (the level stretches it, wider while it opens for a delivery).
    mc.build_prefab("Piece_BreachSeam", {"parts": [B(0, 0, 0, 1.0, 1.0, 1.0, "piece_seam")], "size": (1, 1, 1), "collide": False},
                    template, folder="Prefabs/Base", mat_guid=mat_guid)
    # The spawn room's reality anchor (owner, Nov 27: monsters can't enter the spawn, "like a device").
    mc.build_prefab("Piece_RealityAnchor", {"parts": reality_anchor(), "size": mc.bounds(reality_anchor()), "collide": True},
                    template, folder="Prefabs/Base", mat_guid=mat_guid)
    write_catalog()
    write_icons()
    print(f"wrote {built} piece prefabs, {len(BASE_MATERIALS)} materials, the catalog ({len(TILES)} tiles) and {len(ICONS)} icons")


if __name__ == "__main__":
    main()
