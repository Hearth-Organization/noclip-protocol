#!/usr/bin/env python3
"""The ways into other levels that aren't tears (owner, Nov 27: "entrances to other levels like a doorway or stairs or
a drop off into water"): Prefabs/Levels/Exit_Door, Exit_Stairs and Exit_Water. Level0Layout places them (step 12) and
cuts the floor round the stairs and the pool; EndlessLevelComponent::CheckExits takes you through.

    python3 Tools/Levels/make_exits.py      (after Tools/Base/make_base_pieces.py, for the materials)

Each faces +Y (the door's front, the way into the room) / runs along Y (the stairs go down toward -Y). Origin on the floor.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Clutter"))
sys.path.insert(0, os.path.join(HERE, "..", "Base"))
import make_clutter as mc  # noqa: E402
import make_base_pieces as bp  # noqa: E402

F, B, C = mc.F, mc.B, mc.C


def door():
    """A grey steel fire door standing open on a black doorway in the wall, a green EXIT sign over it."""
    p = [B(0, 0.03, 1.06, 1.0, 0.04, 2.12, "piece_void"),                                   # the dark beyond
         F(-0.56, 0.06, 0, 0.1, 0.12, 2.2, "metal_dark"), F(0.56, 0.06, 0, 0.1, 0.12, 2.2, "metal_dark"),
         F(0, 0.06, 2.12, 1.22, 0.12, 0.1, "metal_dark"),                                    # frame
         F(0, 0.06, 0, 1.0, 0.12, 0.02, "metal_dark")]                                       # threshold
    # The door leaf, open about 100 degrees on its left hinge, into the room.
    hx, hy, a = -0.5, 0.12, math.radians(100)
    cx, cy = hx + math.cos(a) * 0.48, hy + math.sin(a) * 0.48
    p += [B(cx, cy, 1.05, 0.96, 0.05, 2.06, "piece_door", [0, 0, 100]),
          B(cx + math.cos(a) * 0.36 - math.sin(a) * 0.05, cy + math.sin(a) * 0.36 + math.cos(a) * 0.05, 1.0, 0.03, 0.06, 0.16, "metal",
            [0, 0, 100]),                                                                    # push bar handle
          B(cx - math.sin(a) * 0.03, cy + math.cos(a) * 0.03, 1.55, 0.5, 0.01, 0.3, "piece_hazard", [0, 0, 100])]  # warning plate
    p += [F(0, 0.1, 2.3, 0.46, 0.08, 0.17, "metal_dark"), F(0, 0.145, 2.32, 0.38, 0.01, 0.12, "piece_exit_sign")]  # EXIT sign
    blockers = [((cx, cy, 1.05), (0.96, 0.05, 2.06), [0, 0, 100])]
    return p, blockers


def stairs():
    """Concrete stairs going down through a hole in the floor (1.7 x 3.3 m): ten steps, a landing, a dark doorway with a
    red emergency light, a yellow-and-black railing round the three sides you can fall from."""
    w, top, rise, tread, n = 1.6, 1.6, 0.24, 0.24, 10
    bottom = -rise * n
    p, blockers = [], []
    for i in range(n):
        z = -rise * (i + 1)
        y = top - tread * (i + 0.5)
        p.append(B(0, y, (z + bottom - 0.2) / 2, w, tread, z - (bottom - 0.2), "concrete"))
        p.append(B(0, y + tread * 0.5 - 0.02, z - 0.005, w, 0.04, 0.012, "piece_hazard"))   # the step's edge
        blockers.append(((0, y, (z + bottom - 0.2) / 2), (w, tread, z - (bottom - 0.2)), None))
    landing = top - tread * n
    ly = (landing + -1.65) / 2
    p.append(B(0, ly, bottom - 0.1, w, landing + 1.65, 0.2, "concrete"))
    blockers.append(((0, ly, bottom - 0.1), (w, landing + 1.65, 0.2), None))
    # The pit's walls.
    depth = -bottom + 0.2
    for sx in (-1, 1):
        p.append(B(sx * (w / 2 + 0.06), 0, -depth / 2, 0.12, 3.3, depth, "concrete"))
        blockers.append(((sx * (w / 2 + 0.06), 0, -depth / 2), (0.12, 3.3, depth), None))
    p.append(B(0, -1.7, -depth / 2, w + 0.24, 0.1, depth, "concrete"))
    p.append(B(0, -1.64, bottom + 1.0, 0.9, 0.02, 2.0, "piece_void"))                       # doorway at the bottom
    p.append(F(0, -1.62, bottom + 2.05, 0.3, 0.04, 0.1, "piece_red_led"))                  # emergency light
    p.append(B(0, 1.7, -depth / 2, w + 0.24, 0.1, depth, "concrete"))
    # Railing: posts and two rails along both long sides and across the far end (the way in, +Y, is open).
    for sx in (-1, 1):
        x = sx * (w / 2 + 0.06)
        for y in (-1.6, 0.0, 1.6):
            p.append(C(x, y, 0, 0.05, 1.0, "piece_hazard"))
        for z in (0.5, 0.98):
            p.append(C(x, 0, z, 0.04, 3.2, "metal_dark", [90, 0, 0]))
        blockers.append(((x, 0, 0.5), (0.08, 3.2, 1.0), None))
    for z in (0.5, 0.98):
        p.append(C(0, -1.62, z, 0.04, w + 0.12, "metal_dark", [0, 90, 0]))
    blockers.append(((0, -1.62, 0.5), (w + 0.12, 0.08, 1.0), None))
    return p, blockers


def water():
    """A square drop (2.2 m) through the floor into still, faintly lit water: tiled sides, broken tile edges."""
    s, depth, level = 2.2, 3.0, -1.3
    p, blockers = [], []
    for sx in (-1, 1):
        p.append(B(sx * (s / 2 + 0.05), 0, -depth / 2, 0.1, s + 0.2, depth, "piece_tile"))
        p.append(B(0, sx * (s / 2 + 0.05), -depth / 2, s + 0.2, 0.1, depth, "piece_tile"))
        blockers.append(((sx * (s / 2 + 0.05), 0, -depth / 2), (0.1, s + 0.2, depth), None))
        blockers.append(((0, sx * (s / 2 + 0.05), -depth / 2), (s + 0.2, 0.1, depth), None))
    p.append(B(0, 0, level, s, s, 0.02, "piece_water"))
    p.append(B(0, 0, -depth, s, s, 0.1, "piece_tile"))
    blockers.append(((0, 0, -depth), (s, s, 0.1), None))
    # The edge: chipped floor tiles hanging over, a few bits lying round it.
    for x, y, r in ((-0.7, 1.14, 8), (0.4, -1.16, -5), (1.15, 0.3, 80), (-1.13, -0.5, 95)):
        p.append(B(x, y, 0.005, 0.32, 0.12, 0.02, "concrete", [0, 0, r]))
    for x, y in ((0.9, 1.45), (-1.4, -0.9), (1.5, -1.3)):
        p.append(F(x, y, 0, 0.12, 0.09, 0.04, "concrete"))
    return p, blockers


def main():
    with open(mc.path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    for name, make in (("Exit_Door", door), ("Exit_Stairs", stairs), ("Exit_Water", water)):
        parts, blockers = make()
        spec = {"parts": parts, "size": mc.bounds(parts), "collide": False, "blockers": blockers}
        mc.build_prefab(name, spec, template, folder="Prefabs/Levels", mat_guid=bp.mat_guid)
        print("wrote", name, len(parts), "parts,", len(blockers), "colliders")


if __name__ == "__main__":
    main()
