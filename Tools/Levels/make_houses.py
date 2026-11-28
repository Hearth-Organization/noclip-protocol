"""Level 9's houses (the wiki's Suburbs): full-size single-storey ranch houses you can walk into, built here from
measured parts, not scanned, so every wall is where the game's collision says it is.

Each house is an exterior of painted lap siding on a concrete foundation, white trim round every window and door, a
pitched asphalt-shingle roof with eaves, and an interior of drywall rooms: living room, kitchen, hall, two bedrooms,
a bathroom and a closet (and, in the garage versions, a garage through a side door). Floors are wood, carpet or tile
by room. The front door hangs open.

  python3 Tools/Levels/make_houses.py        (Blender as a module, bpy)

Writes, per house:
  Assets/Models/Suburb/<House>/<House>_<part>.fbx   one model per material (siding, drywall, floor ...)
  Gem/Source/Level/SuburbHouses.h                   the solid boxes (walls, counters) and furniture spots, for
                                                     Level0Layout and EndlessLevelComponent
Then Tools/Levels/make_house_prefabs.py ASSETDB writes Prefabs/Suburb/<House>.prefab once the PC has processed them.

The owner, Nov 28: "bigger suburban type houses all need to be able to be entered and they need to be realistic".
Local frame: x across the front, y towards the street (the front door is on +y), z up; metres.
"""
import math
import re
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Assets" / "Models" / "Suburb"
HEADER = ROOT / "Gem" / "Source" / "Level" / "SuburbHouses.h"

H = 2.7            # wall height (ceiling)
EXT = 0.22         # exterior wall thickness
INT = 0.12         # interior wall thickness
FOUND = 0.35       # foundation showing below the siding
FLOOR = 0.02       # floor top
DOOR_H = 2.1
PITCH = math.radians(26.0)
EAVE = 0.45

# One texture repeat, metres, per material (the UVs are in these units).
TILE = {"siding": 2.0, "drywall": 2.0, "floor_wood": 2.0, "floor_carpet": 1.6, "floor_tile": 1.8, "roof": 2.5,
        "concrete": 2.5, "trim": 1.0, "glass": 1.0, "front_door": 1.0, "garage_door": 1.0}
# Which shared material each part wears (Assets/Materials/Suburb/house_<name>.material).
MATERIAL = {"drywall": "drywall", "floor_wood": "wood_floor", "floor_carpet": "carpet", "floor_tile": "tile",
            "roof": "roof", "concrete": "concrete", "trim": "trim", "glass": "glass", "front_door": "front_door",
            "garage_door": "garage_door"}


class House:
    def __init__(self):
        self.parts = {}      # part -> list of (verts, faces, uvs)
        self.solids = []     # (cx, cy, cz, sx, sy, sz)
        self.furniture = []  # (kind name, x, y, yaw)

    # --- geometry -------------------------------------------------------------------------------------------
    def box(self, part, x0, y0, z0, x1, y1, z1, solid=False, fit=False):
        """An axis-aligned box. UVs: metres / TILE, projected per face (fit: 0..1 across each face, for doors)."""
        if x1 - x0 < 1e-4 or y1 - y0 < 1e-4 or z1 - z0 < 1e-4:
            return
        t = TILE.get(part, 1.0)
        v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        faces = [((0, 3, 2, 1), "z"), ((4, 5, 6, 7), "z"), ((0, 1, 5, 4), "y"), ((2, 3, 7, 6), "y"),
                 ((1, 2, 6, 5), "x"), ((3, 0, 4, 7), "x")]
        lo, hi = (x0, y0, z0), (x1, y1, z1)
        out = []
        for idx, axis in faces:
            uv = []
            for i in idx:
                p = v[i]
                a, b = {"z": (0, 1), "y": (0, 2), "x": (1, 2)}[axis]
                if fit:
                    uv.append(((p[a] - lo[a]) / (hi[a] - lo[a]), (p[b] - lo[b]) / (hi[b] - lo[b])))
                else:
                    uv.append((p[a] / t, p[b] / t))
            out.append((idx, uv))
        self.parts.setdefault(part, []).append((v, out))
        if solid:
            self.solids.append(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2, x1 - x0, y1 - y0, z1 - z0))

    def poly(self, part, pts, uv_axes):
        """A flat polygon (roof planes, gables), UVs from two of its axes in metres."""
        t = TILE.get(part, 1.0)
        a, b = uv_axes
        uv = [(p[a] / t, p[b] / t) for p in pts]
        self.parts.setdefault(part, []).append((pts, [(tuple(range(len(pts))), uv)]))

    # --- walls ----------------------------------------------------------------------------------------------
    def wall(self, axis, at, lo, hi, openings, kind, outside=0):
        """A wall along x (axis 'x', at = its y) or y (axis 'y', at = its x), from lo to hi.
        openings: (centre, width, sill, head, what) with what 'door', 'window', 'arch' or 'garage'.
        kind: 'ext' (siding outside, drywall inside; outside = +1/-1, the side the siding faces) or 'int'.
        Each piece between openings is one solid box the full thickness."""
        th = EXT if kind == "ext" else INT
        ops = sorted(openings)

        def piece(a0, a1, z0, z1, solid=True):
            if a1 - a0 < 1e-3 or z1 - z0 < 1e-3:
                return
            if kind == "int":
                self._slab("drywall", axis, at - th / 2, at + th / 2, a0, a1, z0, z1, solid)
                return
            # Exterior: the outer half siding (concrete below FOUND), the inner half drywall; one solid box.
            outer = (at, at + outside * th / 2) if outside > 0 else (at + outside * th / 2, at)
            inner = (at - outside * th / 2, at) if outside > 0 else (at, at - outside * th / 2)
            o0, o1 = sorted(outer)
            i0, i1 = sorted(inner)
            if z0 < FOUND:
                self._slab("concrete", axis, o0, o1, a0, a1, z0, min(z1, FOUND), False)
            if z1 > FOUND:
                self._slab("siding", axis, o0, o1, a0, a1, max(z0, FOUND), z1, False)
            self._slab("drywall", axis, i0, i1, a0, a1, z0, z1, False)
            if solid:
                self._solid(axis, at - th / 2, at + th / 2, a0, a1, z0, z1)

        cur = lo
        for (c, w, sill, head, what) in ops:
            a0, a1 = c - w / 2, c + w / 2
            piece(cur, a0, 0.0, H)
            piece(a0, a1, head, H)           # over it
            if sill > 0:
                piece(a0, a1, 0.0, sill)     # under a window
            self._casing(axis, at, th, a0, a1, sill, head, what)
            cur = a1
        piece(cur, hi, 0.0, H)

    def _slab(self, part, axis, t0, t1, a0, a1, z0, z1, solid):
        if axis == "x":
            self.box(part, a0, t0, z0, a1, t1, z1, solid)
        else:
            self.box(part, t0, a0, z0, t1, a1, z1, solid)

    def _solid(self, axis, t0, t1, a0, a1, z0, z1):
        if axis == "x":
            self.solids.append(((a0 + a1) / 2, (t0 + t1) / 2, (z0 + z1) / 2, a1 - a0, t1 - t0, z1 - z0))
        else:
            self.solids.append(((t0 + t1) / 2, (a0 + a1) / 2, (z0 + z1) / 2, t1 - t0, a1 - a0, z1 - z0))

    def _casing(self, axis, at, th, a0, a1, sill, head, what):
        """White trim round the opening, both faces; glass in a window."""
        if what == "garage":
            return
        tw, td = 0.08, 0.025   # trim width, how far it stands out
        for side in (-1, 1):
            f0 = at + side * th / 2
            f1 = f0 + side * td
            t0, t1 = sorted((f0, f1))
            self._slab("trim", axis, t0, t1, a0 - tw, a0, max(0.0, sill - (tw if sill > 0 else 0)), head + tw, False)
            self._slab("trim", axis, t0, t1, a1, a1 + tw, max(0.0, sill - (tw if sill > 0 else 0)), head + tw, False)
            self._slab("trim", axis, t0, t1, a0 - tw, a1 + tw, head, head + tw, False)
            if sill > 0:
                self._slab("trim", axis, t0, t1, a0 - tw, a1 + tw, sill - tw, sill, False)
        if what == "window":
            # Sash: a glass pane in the middle of the wall and a muntin bar across.
            self._slab("glass", axis, at - 0.006, at + 0.006, a0, a1, sill, head, False)
            mid = (sill + head) / 2
            self._slab("trim", axis, at - 0.02, at + 0.02, a0, a1, mid - 0.025, mid + 0.025, False)
            # Windows are solid (glass): you can't walk out through them.
            self._solid(axis, at - th / 2, at + th / 2, a0, a1, sill, head)

    # --- roof -----------------------------------------------------------------------------------------------
    def roof(self, x0, x1, y0, y1):
        """A gable roof, ridge along x, eaves past the walls; gable ends in siding; soffits and fascia in trim."""
        top = H + 0.08
        half = (y1 - y0) / 2 + EAVE
        rise = half * math.tan(PITCH)
        yc = (y0 + y1) / 2
        ridge = top + rise
        ex0, ex1 = x0 - EAVE, x1 + EAVE
        drop = EAVE * math.tan(PITCH)
        e = top + (EAVE - EAVE) - 0.0
        low = top - drop * 0 + 0.0
        # Eave line height (at the outer edge of the overhang): the slope carried past the wall.
        eave_z = top - drop
        th = 0.12
        for s in (-1, 1):
            edge = yc + s * half
            # Upper surface (shingles) and underside (soffit, trim): two planes of the slab.
            self.poly("roof", [(ex0, edge, eave_z + th), (ex1, edge, eave_z + th), (ex1, yc, ridge + th), (ex0, yc, ridge + th)][::(-s)],
                      (0, 1))
            self.poly("trim", [(ex0, edge, eave_z), (ex0, yc, ridge), (ex1, yc, ridge), (ex1, edge, eave_z)][::(-s)], (0, 1))
            # Fascia board along the eave.
            self.poly("trim", [(ex0, edge, eave_z), (ex1, edge, eave_z), (ex1, edge, eave_z + th), (ex0, edge, eave_z + th)][::s],
                      (0, 2))
        for x, s in ((x0, -1), (x1, 1)):
            # Gable: the triangle of wall over each end, siding outside.
            wy0, wy1 = y0, y1
            zr = top + ((y1 - y0) / 2) * math.tan(PITCH)
            pts = [(x, wy0, top), (x, wy1, top), (x, yc, zr)]
            self.poly("siding", pts if s > 0 else pts[::-1], (1, 2))
            # Rake trim at the roof's end (both sides of the slab edge).
            for side in (-1, 1):
                edge = yc + side * half
                pts = [(x + s * EAVE, edge, eave_z), (x + s * EAVE, yc, ridge), (x + s * EAVE, yc, ridge + th), (x + s * EAVE, edge, eave_z + th)]
                self.poly("trim", pts if (s * side) > 0 else pts[::-1], (1, 2))
        # The attic floor seen from below: the ceiling (drywall) over the whole house, just under the plates.
        self.box("drywall", x0 + EXT, y0 + EXT, H - 0.04, x1 - EXT, y1 - EXT, H, False)

    # --- export ---------------------------------------------------------------------------------------------
    def mirrored(self):
        m = House()
        for part, items in self.parts.items():
            for verts, faces in items:
                nv = [(-p[0], p[1], p[2]) for p in verts]
                nf = [(idx[::-1], uv[::-1]) for idx, uv in faces]
                m.parts.setdefault(part, []).append((nv, nf))
        m.solids = [(-cx, cy, cz, sx, sy, sz) for (cx, cy, cz, sx, sy, sz) in self.solids]
        m.furniture = [(k, -x, y, -yaw) for (k, x, y, yaw) in self.furniture]
        return m


def ranch(garage):
    """The ranch house: 13 x 9 m, living room and kitchen on one side, hall, two bedrooms, bath and closet on the
    other; with garage, a 6 m garage on the living-room end through a door off the kitchen."""
    h = House()
    W, D = 13.0, 9.0
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    X0 = x0 - 6.0 if garage else x0
    # Outside walls (their outer faces on the footprint; the siding faces out).
    h.wall("x", y1 - EXT / 2, x0, x1, [(-2.4, 0.95, 0, DOOR_H, "door"), (-4.8, 1.6, 0.9, DOOR_H, "window"),
                                      (3.6, 1.3, 0.9, DOOR_H, "window")], "ext", +1)
    h.wall("x", y0 + EXT / 2, x0, x1, [(-4.6, 1.2, 1.2, DOOR_H, "window"), (-2.0, 0.9, 0, DOOR_H, "door"),
                                      (3.6, 1.3, 0.9, DOOR_H, "window")], "ext", -1)
    if garage:
        # The old end wall is now inside: drywall both sides, a door through to the garage.
        h.wall("y", x0 + EXT / 2, y0 + EXT, y1 - EXT, [(-2.4, 0.9, 0, DOOR_H, "door")], "int")
    else:
        h.wall("y", x0 + EXT / 2, y0 + EXT, y1 - EXT, [(2.2, 1.3, 0.9, DOOR_H, "window"), (-1.2, 1.0, 1.1, DOOR_H, "window")],
               "ext", -1)
    h.wall("y", x1 - EXT / 2, y0 + EXT, y1 - EXT, [(2.7, 1.2, 0.9, DOOR_H, "window"), (-3.0, 1.2, 0.9, DOOR_H, "window")],
           "ext", +1)
    # Inside walls.
    yi0, yi1 = y0 + EXT, y1 - EXT
    h.wall("y", -0.5, yi0, yi1, [(1.8, 1.0, 0, DOOR_H, "arch"), (-1.4, 0.9, 0, DOOR_H, "arch")], "int")
    h.wall("y", 0.7, yi0, yi1, [(2.4, 0.85, 0, DOOR_H, "door"), (-0.2, 0.75, 0, DOOR_H, "door"), (-3.0, 0.85, 0, DOOR_H, "door")],
           "int")
    h.wall("x", 1.0, 0.7 + INT / 2, x1 - EXT, [(5.6, 0.8, 0, DOOR_H, "door")], "int")
    h.wall("x", -1.5, 0.7 + INT / 2, x1 - EXT, [], "int")
    h.wall("y", 3.5, -1.5 + INT / 2, 1.0 - INT / 2, [], "int")
    h.wall("x", 0.0, x0 + EXT, -3.8, [], "int")   # the half wall between living room and kitchen
    # Floors by room.
    h.box("floor_wood", x0 + EXT, 0.0, 0.0, -0.5, yi1, FLOOR)           # living room
    h.box("floor_tile", x0 + EXT, yi0, 0.0, -0.5, 0.0, FLOOR)           # kitchen
    h.box("floor_wood", -0.5, yi0, 0.0, 0.7, yi1, FLOOR)                # hall
    h.box("floor_carpet", 0.7, 1.0, 0.0, x1 - EXT, yi1, FLOOR)          # front bedroom
    h.box("floor_carpet", 0.7, yi0, 0.0, x1 - EXT, -1.5, FLOOR)         # back bedroom
    h.box("floor_tile", 0.7, -1.5, 0.0, 3.5, 1.0, FLOOR)                # bathroom
    h.box("floor_carpet", 3.5, -1.5, 0.0, x1 - EXT, 1.0, FLOOR)         # closet
    # Steps at the doors: a concrete stoop at the front, a step at the back.
    h.box("concrete", -3.3, y1, 0.0, -1.5, y1 + 1.2, 0.15)
    h.box("concrete", -2.6, y0 - 0.9, 0.0, -1.4, y0, 0.12)
    # The front door, hung open into the living room.
    hinge_x, hinge_y = -2.4 + 0.475, y1 - EXT
    leaf = House()
    leaf.box("front_door", -0.9, -0.045, 0.0, 0.0, 0.0, 2.05, fit=True)
    ang = math.radians(-100.0)
    for verts, faces in leaf.parts["front_door"]:
        nv = [(hinge_x + p[0] * math.cos(ang) - p[1] * math.sin(ang), hinge_y + p[0] * math.sin(ang) + p[1] * math.cos(ang), p[2] + FLOOR)
              for p in verts]
        h.parts.setdefault("front_door", []).append((nv, faces))
    # Furniture (ClutterCatalog kinds), front facing +y at yaw 0, placed where it would really stand.
    q = math.pi / 2
    h.furniture += [
        ("Couch", x0 + EXT + 0.45, 2.1, -q), ("CoffeeTable", -4.4, 2.1, q), ("Rug", -4.4, 2.1, q),
        ("Armchair", -3.0, 3.6, math.pi), ("CrtTv", -0.5 - INT / 2 - 0.27, 3.3, q), ("FloorLamp", -6.0, 3.9, 0.0),
        ("Stove", -5.6, yi0 + 0.34, 0.0), ("KitchenCounter", -4.4, yi0 + 0.34, 0.0), ("SinkCabinet", -3.4, yi0 + 0.30, 0.0),
        ("Fridge", -6.0 if not garage else -5.9, -2.9 if not garage else -3.6, -q),
        ("KitchenTable", -3.2, -1.6, 0.0), ("KitchenChair", -3.2, -0.95, math.pi), ("KitchenChair", -3.2, -2.25, 0.0),
        ("SingleBed", x1 - EXT - 1.0, 2.9, q), ("Nightstand", x1 - EXT - 0.23, 1.6, q), ("Dresser", 0.7 + INT / 2 + 0.28, 3.6, -q),
        ("SingleBed", x1 - EXT - 1.0, -3.0, q), ("Wardrobe", 2.6, -1.5 - INT / 2 - 0.32, math.pi),
        ("ToyChest", 2.0, yi0 + 0.28, 0.0), ("TeddyBear", 4.0, -2.4, 0.6),
        ("Bathtub", 1.75, 1.0 - INT / 2 - 0.41, q), ("Toilet", 3.5 - INT / 2 - 0.35, -0.45, q),
        ("PedestalSink", 2.2, -1.5 + INT / 2 + 0.21, 0.0),
    ]
    if garage:
        gx0, gx1 = X0, x0
        h.wall("x", y1 - EXT / 2, gx0, gx1, [(gx0 + 3.0, 5.0, 0, 2.3, "garage")], "ext", +1)
        h.wall("x", y0 + EXT / 2, gx0, gx1, [(gx0 + 3.0, 1.0, 1.1, DOOR_H, "window")], "ext", -1)
        h.wall("y", gx0 + EXT / 2, y0 + EXT, y1 - EXT, [(0.0, 1.0, 1.1, DOOR_H, "window")], "ext", -1)
        h.box("concrete", gx0 + EXT, y0 + EXT, 0.0, gx1, y1 - EXT, FLOOR)
        # The garage door, rolled up under the ceiling.
        h.box("garage_door", gx0 + 0.5, y1 - 2.6, 2.32, gx0 + 5.5, y1 - 0.3, 2.37, fit=True)
        h.furniture += [("RollingToolChest", gx0 + EXT + 0.25, -2.5, -q), ("CardboardBoxes", gx0 + 3.0, yi0 + 0.31, 0.0),
                        ("TireStack", gx1 - 0.6, yi0 + 0.5, 0.0), ("Ladder", gx0 + EXT + 0.38, 1.5, -q)]
    h.roof(X0, x1, y0, y1)
    return h, (X0, x1, y0, y1)


def write_part(name, part, items):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    verts, polys, uvs = [], [], []
    for vs, faces in items:
        base = len(verts)
        verts.extend(vs)
        for idx, uv in faces:
            polys.append([base + i for i in idx])
            uvs.append(uv)
    mesh = bpy.data.meshes.new(f"{name}_{part}")
    mesh.from_pydata(verts, [], polys)
    mesh.update()
    layer = mesh.uv_layers.new(name="UVMap")
    for poly, uv in zip(mesh.polygons, uvs):
        for li, (u, v) in zip(poly.loop_indices, uv):
            layer.data[li].uv = (u, v)
    obj = bpy.data.objects.new(f"{name}_{part}", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(bpy.data.materials.new(f"{name}_{part}"))
    # O3DE's FBX import turns models half way round (Blender's -Y comes in as the game's +Y): build it turned.
    obj.rotation_euler = (0.0, 0.0, math.pi)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    folder = OUT / name
    folder.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=str(folder / f"{name}_{part}.fbx"), use_selection=True, object_types={"MESH"},
                             path_mode="STRIP", embed_textures=False)


def clutter_index():
    text = (ROOT / "Gem" / "Source" / "Level" / "ClutterCatalog.h").read_text()
    names = re.findall(r'\{ "([A-Za-z0-9]+)", Place::', text)
    return {n: i for i, n in enumerate(names)}


SIDING = {"HouseRanchA": "white", "HouseRanchB": "cream", "HouseGarageA": "blue", "HouseGarageB": "sage"}


def main():
    index = clutter_index()
    a, ext_a = ranch(False)
    g, ext_g = ranch(True)
    houses = {"HouseRanchA": (a, ext_a), "HouseRanchB": (a.mirrored(), (-ext_a[1], -ext_a[0], ext_a[2], ext_a[3])),
              "HouseGarageA": (g, ext_g), "HouseGarageB": (g.mirrored(), (-ext_g[1], -ext_g[0], ext_g[2], ext_g[3]))}
    lines = []
    for name, (house, (hx0, hx1, hy0, hy1)) in houses.items():
        for part, items in house.parts.items():
            write_part(name, part, items)
        garage = "Garage" in name
        gx = (hx0 + 3.0) if (garage and name.endswith("A")) else ((hx1 - 3.0) if garage else 0.0)
        solids = ",\n        ".join(f"{{ {cx:.3f}f, {cy:.3f}f, {cz:.3f}f, {sx:.3f}f, {sy:.3f}f, {sz:.3f}f }}"
                                    for (cx, cy, cz, sx, sy, sz) in house.solids)
        furn = ",\n        ".join(f"{{ {index[k]}, {x:.3f}f, {y:.3f}f, {yaw:.4f}f }} /* {k} */" for (k, x, y, yaw) in house.furniture)
        door = -2.4 if name.endswith("A") else 2.4
        lines.append((name, hx0, hx1, hy0, hy1, garage, gx, solids, furn, len(house.solids), len(house.furniture), door))
        print(name, {p: len(i) for p, i in house.parts.items()}, len(house.solids), "solids")
    with open(HEADER, "w") as f:
        f.write("#pragma once\n\n// Generated by Tools/Levels/make_houses.py: Level 9's houses, their solid boxes and furniture spots.\n")
        f.write("// Local frame: x across the front, y towards the street (front door on +y), z up; metres.\n\n")
        f.write("namespace NoclipProtocol::Suburb\n{\n")
        f.write("    struct LocalBox { float m_cx, m_cy, m_cz, m_sx, m_sy, m_sz; };\n")
        f.write("    struct Furniture { int m_kind; float m_x, m_y, m_yaw; }; // ClutterCatalog kind; front +y at yaw 0\n\n")
        for (name, hx0, hx1, hy0, hy1, garage, gx, solids, furn, ns, nf, door) in lines:
            f.write(f"    constexpr LocalBox {name}Solids[{ns}] = {{\n        {solids}\n    }};\n")
            f.write(f"    constexpr Furniture {name}Furniture[{nf}] = {{\n        {furn}\n    }};\n")
        f.write("\n    struct House\n    {\n        const char* m_prefab;\n        float m_minX, m_maxX, m_minY, m_maxY; // the walls' footprint\n")
        f.write("        bool m_garage;\n        float m_garageX;  // the garage door's middle (the driveway), when it has one\n")
        f.write("        float m_doorX;    // the front door's middle (the front walk)\n")
        f.write("        const LocalBox* m_solids;\n        int m_solidCount;\n        const Furniture* m_furniture;\n        int m_furnitureCount;\n    };\n\n")
        f.write(f"    constexpr int HouseCount = {len(lines)};\n    constexpr House Houses[HouseCount] = {{\n")
        for (name, hx0, hx1, hy0, hy1, garage, gx, solids, furn, ns, nf, door) in lines:
            f.write(f'        {{ "prefabs/suburb/{name.lower()}.spawnable", {hx0:.2f}f, {hx1:.2f}f, {hy0:.2f}f, {hy1:.2f}f, '
                    f'{"true" if garage else "false"}, {gx:.2f}f, {door:.2f}f, {name}Solids, {ns}, {name}Furniture, {nf} }},\n')
        f.write("    };\n} // namespace NoclipProtocol::Suburb\n")
    print("wrote", HEADER.relative_to(ROOT))


if __name__ == "__main__":
    main()
