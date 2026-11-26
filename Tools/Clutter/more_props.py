"""
More clutter for make_clutter.py: everything else that has fallen into the Backrooms.

add(prop, F, B, C, S, legs4) registers the props. Sizes are worked out from the parts by make_clutter.py, so
the size given here is only a note. Weights: (Level 0 lobby, Level 1 garage, Level 2 tunnels).
Parts: F(x, y, bottom z, sx, sy, sz, material, rot) box standing on z; C(x, y, bottom z, diameter, height,
material, rot) cylinder; S(x, y, center z, diameter, material) a round lump. Front faces +Y; wall props stand
with their back (-Y) to the wall.
"""


def add(prop, F, B, C, S, legs4):
    W = None  # size placeholder: computed from the parts

    def table(w, d, h, top, leg, t=0.04):
        return [F(0, 0, h - 0.04, w, d, 0.04, top)] + legs4(w, d, h - 0.04, t, leg)

    def chair(seat, leg, back=True, h=0.45):
        p = [F(0, 0, h - 0.04, 0.44, 0.42, 0.04, seat)] + legs4(0.42, 0.4, h - 0.04, 0.03, leg)
        if back:
            p.append(F(0, -0.19, h, 0.42, 0.03, 0.42, seat))
        return p

    def cabinet(w, d, h, body, front, handles="metal", rows=2, cols=1):
        p = [F(0, 0, 0, w, d, h, body)]
        cw = w / cols
        rh = h / rows
        for c in range(cols):
            for r in range(rows):
                x = -w / 2 + cw * (c + 0.5)
                p.append(F(x, d / 2 + 0.005, r * rh + 0.03, cw - 0.04, 0.01, rh - 0.06, front))
                p.append(F(x + cw * 0.3, d / 2 + 0.015, r * rh + rh * 0.5, 0.03, 0.02, 0.1, handles))
        return p

    def boxpile(n, mat, seed=1, s=0.4):
        import random
        r = random.Random(seed)
        p = []
        for i in range(n):
            w = s * r.uniform(0.7, 1.2)
            p.append(F(r.uniform(-0.3, 0.3), r.uniform(-0.2, 0.2), 0, w, w * r.uniform(0.6, 1.0), w * r.uniform(0.4, 0.8),
                       mat, [0, 0, r.uniform(-40, 40)]))
        return p

    # ------------------------------------------------------------------ office
    prop("SwivelStool", "free", W, (5, 1, 1), [C(0, 0, 0, 0.5, 0.04, "black_plastic"), C(0, 0, 0.04, 0.05, 0.5, "metal"),
                                               C(0, 0, 0.54, 0.38, 0.08, "fabric_red")])
    prop("ConferenceTable", "free", W, (3, 0, 0), table(1.7, 0.85, 0.75, "wood_dark", "metal_dark", 0.06))
    prop("Whiteboard", "wall", W, (5, 1, 0), [F(0, -0.1, 0.8, 1.4, 0.04, 0.9, "white_plastic"), F(0, -0.07, 0.78, 1.44, 0.08, 0.04, "metal"),
                                              F(-0.68, -0.05, 0, 0.04, 0.3, 1.75, "metal"), F(0.68, -0.05, 0, 0.04, 0.3, 1.75, "metal"),
                                              F(-0.2, -0.07, 1.2, 0.5, 0.005, 0.05, "fabric_blue"), F(0.2, -0.07, 1.05, 0.4, 0.005, 0.04, "fabric_red")])
    prop("Photocopier", "wall", W, (5, 1, 0), [F(0, 0, 0, 0.9, 0.65, 0.85, "beige_plastic"), F(0, 0, 0.85, 0.9, 0.62, 0.15, "white_plastic"),
                                               F(0.32, 0.25, 1.0, 0.2, 0.12, 0.02, "screen_glow"), F(-0.42, 0.1, 0.55, 0.12, 0.4, 0.04, "white_plastic")])
    prop("PrinterCart", "free", W, (4, 0, 0), table(0.6, 0.5, 0.7, "metal_dark", "metal") + [F(0, 0, 0.7, 0.45, 0.38, 0.22, "beige_plastic"),
                                                                                         F(0, 0, 0.15, 0.55, 0.45, 0.02, "metal_dark")])
    prop("PaperStack", "small", W, (6, 1, 1), [F(0, 0, 0, 0.3, 0.22, 0.25, "paper"), F(0.05, 0.03, 0.25, 0.3, 0.22, 0.12, "paper", [0, 0, 14])],
         collide=False)
    prop("BinderPile", "small", W, (5, 0, 0), [F(0, 0, 0, 0.32, 0.28, 0.06, "fabric_blue", [0, 0, 8]), F(0.02, 0, 0.06, 0.32, 0.28, 0.06, "fabric_red", [0, 0, -12]),
                                               F(-0.02, 0.01, 0.12, 0.32, 0.28, 0.06, "black_plastic", [0, 0, 25])], collide=False)
    prop("DeskLamp", "small", W, (4, 0, 1), [C(0, 0, 0, 0.16, 0.03, "black_plastic"), F(0, 0, 0.03, 0.02, 0.02, 0.35, "metal", [0, 20, 0]),
                                             C(0.08, 0, 0.32, 0.12, 0.1, "lamp_glow")], collide=False)
    prop("Wastebasket", "small", W, (6, 1, 1), [C(0, 0, 0, 0.28, 0.34, "metal_dark"), F(0.05, 0.02, 0.3, 0.14, 0.1, 0.08, "paper", [20, 0, 10])], collide=False)
    prop("CubicleWall", "wall", W, (5, 0, 0), [F(0, -0.25, 0, 1.6, 0.06, 1.4, "fabric_blue"), F(0, -0.25, 1.4, 1.6, 0.07, 0.03, "metal"),
                                               F(-0.4, -0.2, 1.05, 0.21, 0.01, 0.3, "paper"), F(0.3, -0.2, 0.9, 0.15, 0.01, 0.2, "paper")], collide=True)
    prop("CoatRack", "free", W, (3, 0, 1), [C(0, 0, 0, 0.4, 0.04, "wood_dark"), C(0, 0, 0.04, 0.05, 1.7, "wood_dark"),
                                            F(0.12, 0, 1.6, 0.25, 0.03, 0.03, "wood_dark", [0, -30, 0]), F(-0.12, 0, 1.6, 0.25, 0.03, 0.03, "wood_dark", [0, 30, 0]),
                                            F(0.1, 0.05, 0.9, 0.36, 0.12, 0.7, "fabric_brown", [0, 4, 0])])
    prop("WallClock", "wall", W, (4, 0, 1), [C(0, -0.1, 2.0, 0.34, 0.05, "white_plastic", [90, 0, 0]), F(0, -0.06, 2.0, 0.02, 0.01, 0.13, "black_plastic"),
                                             F(0.04, -0.06, 2.0, 0.09, 0.01, 0.02, "black_plastic"), F(0, -0.1, 0, 0.3, 0.1, 0.02, "metal_dark")], collide=False)
    prop("Shredder", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.4, 0.32, 0.6, "black_plastic"), F(0, 0, 0.6, 0.42, 0.34, 0.12, "metal_dark"),
                                            F(0, 0.1, 0.4, 0.3, 0.2, 0.15, "paper", [0, 0, 10])])
    prop("ServerRack", "wall", W, (3, 2, 4), [F(0, 0, 0, 0.6, 0.8, 1.9, "black_plastic")] + [
        F(0, 0.405, 0.15 + 0.2 * i, 0.5, 0.01, 0.14, "metal_dark") for i in range(8)] + [
        F(0.18, 0.412, 0.2 + 0.2 * i, 0.04, 0.01, 0.02, "screen_glow") for i in range(0, 8, 2)])
    prop("KeyboardPile", "small", W, (4, 0, 0), [F(0, 0, 0, 0.45, 0.15, 0.03, "beige_plastic", [0, 0, 10]), F(0.03, 0.05, 0.03, 0.45, 0.15, 0.03, "black_plastic", [0, 0, -20]),
                                                 F(-0.05, -0.04, 0.06, 0.45, 0.15, 0.03, "beige_plastic", [0, 0, 35])], collide=False)
    prop("FlatMonitor", "small", W, (4, 0, 1), [F(0, 0, 0, 0.25, 0.18, 0.02, "black_plastic"), F(0, 0, 0.02, 0.05, 0.05, 0.15, "black_plastic"),
                                                F(0, 0.0, 0.17, 0.55, 0.03, 0.33, "black_plastic"), F(0, 0.016, 0.19, 0.51, 0.01, 0.29, "screen")], collide=False)
    prop("TrashBags", "small", W, (5, 3, 3), [S(0, 0, 0.22, 0.5, "black_plastic", 0.45), S(0.35, 0.1, 0.18, 0.4, "black_plastic", 0.36),
                                              S(-0.2, 0.3, 0.16, 0.36, "black_plastic", 0.3)], collide=False)
    prop("ReceptionDesk", "wall", W, (2, 0, 0), [F(0, 0.1, 0, 1.7, 0.25, 1.1, "wood_light"), F(0, -0.15, 0.7, 1.7, 0.5, 0.04, "wood_light"),
                                                 F(0, 0.1, 1.1, 1.75, 0.35, 0.04, "wood_dark"), F(0.4, -0.15, 0.74, 0.4, 0.3, 0.3, "beige_plastic"),
                                                 F(-0.5, -0.2, 0.74, 0.2, 0.15, 0.08, "black_plastic")])
    prop("MailSorter", "wall", W, (3, 0, 0), [F(0, 0, 0, 1.0, 0.35, 0.8, "wood_light")] + [
        F(-0.4 + 0.2 * c, 0.17, 0.82 + 0.17 * r, 0.18, 0.02, 0.15, "wood_dark") for c in range(5) for r in range(4)] + [
        F(0, -0.1, 0.8, 1.0, 0.15, 0.7, "wood_light")])
    prop("FileBoxes", "wall", W, (6, 2, 1), boxpile(5, "cardboard", 3, 0.38) + [F(0.2, 0.0, 0.25, 0.38, 0.3, 0.28, "cardboard", [0, 0, 5])])
    prop("FireExtinguisher", "wall", W, (5, 3, 3), [C(0, -0.05, 0, 0.16, 0.5, "red_paint"), C(0, -0.05, 0.5, 0.06, 0.08, "black_plastic"),
                                                    F(0.06, -0.02, 0.5, 0.1, 0.03, 0.03, "black_plastic")], collide=False)
    prop("ProjectorScreen", "wall", W, (2, 0, 0), [F(0, -0.12, 0.9, 1.6, 0.02, 1.2, "white_plastic"), C(0, -0.12, 2.1, 0.08, 1.7, "metal_dark", [0, 90, 0]),
                                                   C(0, 0, 0, 0.4, 0.03, "metal_dark"), C(0, 0, 0, 0.04, 2.1, "metal")])
    prop("WallTv", "wall", W, (3, 0, 0), [F(0, -0.12, 1.2, 1.1, 0.06, 0.65, "black_plastic"), F(0, -0.085, 1.23, 1.04, 0.01, 0.59, "screen_glow"),
                                          F(0, -0.15, 0, 0.3, 0.06, 1.2, "metal_dark")])

    # ------------------------------------------------------------------ household
    prop("KitchenTable", "free", W, (5, 0, 0), table(1.1, 0.75, 0.76, "wood_light", "wood_light"))
    prop("KitchenChair", "free", W, (6, 0, 0), chair("wood_light", "wood_light"))
    prop("Stove", "wall", W, (4, 0, 0), [F(0, 0, 0, 0.76, 0.65, 0.9, "appliance"), F(0, 0.33, 0.12, 0.6, 0.01, 0.5, "glass_dark"),
                                         F(0, -0.3, 0.9, 0.76, 0.05, 0.2, "appliance")] + [C(x, y, 0.9, 0.2, 0.015, "metal_dark") for x in (-0.18, 0.18) for y in (-0.12, 0.15)])
    prop("MicrowaveCart", "free", W, (3, 0, 0), table(0.6, 0.45, 0.8, "metal", "metal") + [F(0, 0, 0.8, 0.5, 0.38, 0.3, "black_plastic"),
                                                                                       F(-0.05, 0.195, 0.84, 0.32, 0.01, 0.22, "glass_dark")])
    prop("Dishwasher", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.6, 0.6, 0.85, "appliance"), F(0, 0.305, 0.7, 0.5, 0.02, 0.04, "metal")])
    prop("KitchenCounter", "wall", W, (4, 0, 0), cabinet(1.2, 0.6, 0.86, "wood_light", "wood_dark", "metal", 1, 2) + [
        F(0, 0.02, 0.86, 1.22, 0.64, 0.04, "concrete"), C(-0.3, -0.1, 0.9, 0.2, 0.2, "red_paint"), F(0.3, 0.0, 0.9, 0.25, 0.18, 0.2, "white_plastic")])
    prop("SinkCabinet", "wall", W, (4, 0, 1), cabinet(0.8, 0.55, 0.85, "white_plastic", "white_plastic", "metal", 1, 2) + [
        F(0, 0, 0.85, 0.82, 0.57, 0.04, "porcelain"), F(0, -0.2, 0.89, 0.04, 0.04, 0.25, "metal"), F(0, -0.12, 1.12, 0.03, 0.18, 0.03, "metal")])
    prop("Toaster", "small", W, (3, 0, 0), [F(0, 0, 0, 0.28, 0.16, 0.18, "metal"), F(-0.05, 0, 0.18, 0.04, 0.12, 0.005, "black_plastic"),
                                            F(0.05, 0, 0.18, 0.04, 0.12, 0.005, "black_plastic")], collide=False)
    prop("Kettle", "small", W, (3, 0, 0), [C(0, 0, 0, 0.18, 0.2, "metal"), F(0.1, 0, 0.12, 0.08, 0.03, 0.03, "black_plastic"),
                                           F(0, 0, 0.2, 0.12, 0.03, 0.06, "black_plastic")], collide=False)
    prop("DirtyDishes", "small", W, (4, 0, 0), [C(0, 0, 0, 0.26, 0.02, "porcelain"), C(0.02, 0.01, 0.02, 0.24, 0.02, "porcelain"),
                                                C(0.25, 0.05, 0, 0.09, 0.1, "porcelain"), C(-0.22, 0.1, 0, 0.2, 0.06, "porcelain")], collide=False)
    prop("Dresser", "wall", W, (5, 0, 0), cabinet(1.0, 0.5, 0.85, "wood_dark", "wood_light", "brass", 3, 2) + [F(0, 0, 0.85, 1.04, 0.52, 0.03, "wood_dark")])
    prop("Nightstand", "wall", W, (4, 0, 0), cabinet(0.45, 0.4, 0.55, "wood_dark", "wood_light", "brass", 2, 1) + [C(0.08, 0, 0.55, 0.14, 0.03, "metal"),
                                                                                                                C(0.08, 0, 0.58, 0.03, 0.25, "metal"), C(0.08, 0, 0.78, 0.22, 0.18, "lamp_glow")])
    prop("Wardrobe", "wall", W, (4, 0, 0), cabinet(1.0, 0.58, 1.95, "wood_dark", "wood_dark", "brass", 1, 2))
    prop("BunkBed", "big", W, (3, 0, 1), [F(0, 0, 0.3, 0.95, 1.95, 0.18, "mattress"), F(0, 0, 1.35, 0.95, 1.95, 0.18, "mattress"),
                                          F(0, 0, 0.22, 0.98, 2.0, 0.08, "metal"), F(0, 0, 1.27, 0.98, 2.0, 0.08, "metal")] +
         legs4(1.0, 2.02, 1.8, 0.05, "metal") + [F(0.5, 0.6, 0.6 + 0.25 * i, 0.03, 0.45, 0.03, "metal") for i in range(3)])
    prop("SingleBed", "big", W, (4, 0, 0), [F(0, 0, 0.25, 0.95, 1.95, 0.2, "mattress"), F(0, -0.98, 0, 1.0, 0.05, 0.9, "wood_dark"),
                                            F(0, 0.98, 0, 1.0, 0.05, 0.55, "wood_dark"), F(0, -0.8, 0.45, 0.6, 0.35, 0.12, "porcelain"),
                                            F(0, 0.3, 0.45, 0.97, 1.1, 0.04, "fabric_blue")] + legs4(0.95, 1.9, 0.25, 0.05, "wood_dark"))
    prop("Crib", "free", W, (2, 0, 0), [F(0, 0, 0.35, 0.65, 1.15, 0.12, "mattress")] + legs4(0.7, 1.2, 0.95, 0.04, "white_plastic") + [
        F(sx * 0.33, 0, 0.55, 0.02, 1.15, 0.03, "white_plastic") for sx in (-1, 1)] + [
        F(0, sy * 0.58, 0.55, 0.65, 0.02, 0.03, "white_plastic") for sy in (-1, 1)] + [
        F(sx * 0.33, -0.5 + 0.1 * i, 0.47, 0.015, 0.015, 0.45, "white_plastic") for sx in (-1, 1) for i in range(11)])
    prop("RockingChair", "free", W, (3, 0, 0), chair("wood_dark", "wood_dark") + [
        F(sx * 0.2, 0, 0.0, 0.04, 0.75, 0.05, "wood_dark", [8, 0, 0]) for sx in (-1, 1)] + [F(0, -0.2, 0.85, 0.42, 0.03, 0.12, "wood_dark")])
    prop("Recliner", "free", W, (3, 0, 0), [F(0, 0, 0.05, 0.85, 0.9, 0.4, "fabric_brown"), F(0, -0.35, 0.45, 0.85, 0.25, 0.6, "fabric_brown", [-15, 0, 0]),
                                            F(-0.37, 0.05, 0.45, 0.14, 0.8, 0.22, "fabric_brown"), F(0.37, 0.05, 0.45, 0.14, 0.8, 0.22, "fabric_brown"),
                                            F(0, 0.5, 0.15, 0.7, 0.35, 0.12, "fabric_brown", [20, 0, 0])])
    prop("Loveseat", "wall", W, (4, 0, 0), [F(0, 0, 0.05, 1.5, 0.85, 0.35, "fabric_red"), F(0, -0.32, 0.4, 1.5, 0.2, 0.45, "fabric_red"),
                                            F(-0.68, 0.05, 0.4, 0.14, 0.75, 0.22, "fabric_red"), F(0.68, 0.05, 0.4, 0.14, 0.75, 0.22, "fabric_red"),
                                            F(0, 0.08, 0.4, 1.2, 0.6, 0.1, "fabric_red")] + legs4(1.4, 0.75, 0.05, 0.05, "wood_dark"))
    prop("CoffeeTable", "free", W, (5, 0, 0), table(1.1, 0.6, 0.45, "wood_dark", "wood_dark", 0.05) + [F(0.2, 0.05, 0.45, 0.25, 0.2, 0.02, "paper", [0, 0, 20]),
                                                                                                   C(-0.3, -0.1, 0.45, 0.08, 0.1, "porcelain")])
    prop("BeanBag", "free", W, (3, 0, 0), [S(0, 0, 0.25, 0.85, "fabric_blue", 0.5), S(0, -0.15, 0.45, 0.6, "fabric_blue", 0.3)])
    prop("FlatTvStand", "wall", W, (4, 0, 0), [F(0, 0, 0, 1.2, 0.4, 0.5, "wood_dark"), F(0, -0.05, 0.5, 0.3, 0.2, 0.03, "black_plastic"),
                                               F(0, -0.05, 0.53, 1.1, 0.05, 0.65, "black_plastic"), F(0, -0.02, 0.56, 1.04, 0.01, 0.59, "screen")])
    prop("LowBookcase", "wall", W, (5, 0, 0), [F(0, 0, 0, 0.8, 0.3, 0.9, "wood_light")] + [
        F(-0.3 + 0.12 * i, 0.03, 0.05 + 0.44 * r, 0.08, 0.22, 0.3, ["fabric_red", "fabric_blue", "paper", "fabric_green", "cardboard"][(i + r) % 5])
        for i in range(6) for r in range(2)])
    prop("GrandfatherClock", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.5, 0.3, 2.0, "wood_dark"), C(0, 0.155, 1.65, 0.32, 0.01, "porcelain", [90, 0, 0]),
                                                    F(0, 0.155, 0.4, 0.3, 0.01, 0.9, "glass_dark"), C(0, 0.16, 0.6, 0.15, 0.01, "brass", [90, 0, 0])])
    prop("StandingMirror", "free", W, (3, 0, 1), [F(0, 0, 0, 0.5, 0.35, 0.03, "wood_dark"), F(0, 0, 0.1, 0.55, 0.04, 1.6, "wood_dark", [-6, 0, 0]),
                                                  F(0, 0.025, 0.15, 0.48, 0.01, 1.5, "metal", [-6, 0, 0])])
    prop("Toilet", "wall", W, (4, 0, 1), [F(0, -0.25, 0.4, 0.42, 0.18, 0.4, "porcelain"), C(0, 0.05, 0, 0.32, 0.4, "porcelain"),
                                          C(0, 0.05, 0.4, 0.38, 0.04, "porcelain"), F(0, -0.25, 0.8, 0.44, 0.2, 0.03, "porcelain")])
    prop("PedestalSink", "wall", W, (3, 0, 1), [C(0, 0, 0, 0.18, 0.8, "porcelain"), F(0, 0.0, 0.8, 0.55, 0.42, 0.15, "porcelain"),
                                                F(0, -0.16, 0.95, 0.04, 0.04, 0.15, "metal"), F(0, -0.09, 1.08, 0.03, 0.15, 0.03, "metal")])
    prop("ShowerStall", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.9, 0.9, 0.08, "porcelain"), F(-0.44, 0, 0.08, 0.02, 0.9, 1.9, "glass_dark"),
                                               F(0.44, 0, 0.08, 0.02, 0.9, 1.9, "glass_dark"), F(0, -0.44, 0.08, 0.9, 0.02, 1.9, "porcelain"),
                                               F(0, -0.36, 1.9, 0.1, 0.15, 0.04, "metal"), F(0, 0.44, 0.08, 0.04, 0.02, 1.9, "metal")])
    prop("LaundryBasket", "small", W, (4, 0, 0), [F(0, 0, 0, 0.55, 0.4, 0.35, "white_plastic"), F(0, 0, 0.3, 0.5, 0.35, 0.12, "fabric_blue", [5, 0, 0]),
                                                  F(0.1, 0.05, 0.35, 0.3, 0.2, 0.08, "fabric_red", [0, 10, 20])], collide=False)
    prop("IroningBoard", "free", W, (3, 0, 0), [F(0, 0, 0.85, 0.35, 1.25, 0.03, "fabric_blue"), F(0, -0.2, 0, 0.03, 0.03, 0.9, "metal", [25, 0, 0]),
                                                F(0, 0.2, 0, 0.03, 0.03, 0.9, "metal", [-25, 0, 0]), F(0, 0.45, 0.88, 0.12, 0.22, 0.12, "white_plastic")])
    prop("Vacuum", "small", W, (3, 0, 0), [F(0, 0, 0, 0.3, 0.35, 0.25, "red_paint"), F(0, -0.15, 0.2, 0.04, 0.04, 0.9, "metal", [-15, 0, 0]),
                                           F(0, 0.2, 0, 0.32, 0.12, 0.06, "black_plastic")], collide=False)
    prop("StandingFan", "free", W, (3, 0, 0), [C(0, 0, 0, 0.4, 0.04, "white_plastic"), C(0, 0, 0.04, 0.04, 1.0, "white_plastic"),
                                               C(0, 0.05, 1.12, 0.45, 0.12, "white_plastic", [90, 0, 0])])
    prop("SpaceHeater", "small", W, (3, 0, 1), [F(0, 0, 0, 0.4, 0.22, 0.55, "metal_dark"), F(0, 0.112, 0.08, 0.32, 0.01, 0.4, "lamp_glow")], collide=False)
    prop("ChristmasTree", "free", W, (1, 0, 0), [C(0, 0, 0, 0.35, 0.25, "red_paint"), C(0, 0, 0.25, 0.06, 0.2, "wood_dark"),
                                                 C(0, 0, 0.45, 0.95, 0.45, "fabric_green"), C(0, 0, 0.85, 0.7, 0.45, "fabric_green"),
                                                 C(0, 0, 1.25, 0.45, 0.4, "fabric_green"), C(0, 0, 1.6, 0.2, 0.25, "fabric_green"),
                                                 S(0.3, 0.2, 0.7, 0.07, "red_paint"), S(-0.2, 0.25, 1.05, 0.07, "brass"), S(0.1, -0.2, 1.4, 0.06, "blue_paint"),
                                                 C(0, 0, 1.85, 0.1, 0.1, "brass")])
    prop("PottedFern", "wall", W, (4, 0, 0), [C(0, 0, 0, 0.42, 0.4, "rust"), S(0, 0, 0.6, 0.7, "fabric_green", 0.5), S(0.15, 0.1, 0.75, 0.4, "fabric_green", 0.4)])
    prop("Aquarium", "wall", W, (2, 0, 0), [F(0, 0, 0, 1.0, 0.4, 0.75, "wood_dark"), F(0, 0, 0.75, 1.0, 0.4, 0.5, "water_blue"),
                                            F(0, 0, 1.25, 1.02, 0.42, 0.06, "black_plastic"), F(0.2, 0.0, 0.78, 0.3, 0.2, 0.1, "concrete")])
    prop("Birdcage", "small", W, (2, 0, 0), [C(0, 0, 0, 0.35, 0.03, "brass"), C(0, 0, 0.03, 0.34, 0.45, "glass_dark"), C(0, 0, 0.48, 0.2, 0.08, "brass"),
                                             F(0, 0, 0.56, 0.1, 0.02, 0.1, "brass")], collide=False)
    prop("DogBed", "small", W, (3, 0, 0), [C(0, 0, 0, 0.75, 0.15, "fabric_brown"), C(0, 0, 0.1, 0.55, 0.06, "fabric_red"), F(0.15, 0.1, 0.1, 0.2, 0.06, 0.05, "rubber")],
         collide=False)
    prop("Suitcase", "small", W, (5, 2, 1), [F(0, 0, 0, 0.7, 0.45, 0.25, "black_plastic", [0, 0, 15]), F(0.1, 0.1, 0.25, 0.25, 0.04, 0.03, "metal", [0, 0, 15])],
         collide=False)
    prop("LuggagePile", "wall", W, (3, 1, 0), [F(-0.25, 0, 0, 0.5, 0.3, 0.7, "fabric_brown"), F(0.25, 0.05, 0, 0.45, 0.28, 0.6, "fabric_blue"),
                                               F(0.0, 0.0, 0.7, 0.7, 0.45, 0.22, "black_plastic", [0, 0, 6])])
    prop("PairOfShoes", "small", W, (5, 1, 1), [F(-0.08, 0, 0, 0.1, 0.27, 0.1, "black_plastic"), F(0.08, 0.04, 0, 0.1, 0.27, 0.1, "black_plastic", [0, 0, 15])],
         collide=False)
    prop("Umbrella", "small", W, (3, 1, 1), [F(0, 0, 0.03, 0.9, 0.06, 0.06, "black_plastic", [0, 0, 30]), C(0.4, 0.2, 0, 0.1, 0.06, "black_plastic")], collide=False)
    prop("Guitar", "wall", W, (3, 0, 0), [F(0, 0.05, 0.3, 0.38, 0.1, 0.5, "wood_light", [-12, 0, 0]), F(0, -0.05, 0.75, 0.06, 0.04, 0.6, "wood_dark", [-12, 0, 0]),
                                          C(0, 0.11, 0.55, 0.1, 0.01, "black_plastic", [78, 0, 0])], collide=False)
    prop("KeyboardStand", "free", W, (2, 0, 0), [F(0, 0, 0.85, 1.2, 0.32, 0.1, "black_plastic"), F(0, 0.08, 0.95, 1.1, 0.12, 0.01, "porcelain"),
                                                 F(0, 0, 0, 0.04, 0.04, 0.9, "metal", [0, 35, 0]), F(0, 0, 0, 0.04, 0.04, 0.9, "metal", [0, -35, 0])])
    prop("DrumKit", "free", W, (2, 0, 0), [C(0, 0.1, 0, 0.55, 0.4, "red_paint", [90, 0, 0]), C(-0.4, -0.1, 0.45, 0.35, 0.25, "red_paint"),
                                           C(0.4, -0.1, 0.45, 0.35, 0.25, "red_paint"), C(0.6, 0.2, 0, 0.03, 1.1, "metal"), C(0.6, 0.2, 1.1, 0.4, 0.01, "brass"),
                                           C(-0.25, 0.45, 0.4, 0.32, 0.15, "red_paint"), C(-0.25, 0.45, 0, 0.03, 0.4, "metal")])
    prop("RecordPlayer", "small", W, (3, 0, 0), [F(0, 0, 0, 0.45, 0.35, 0.12, "wood_dark"), C(-0.05, 0, 0.12, 0.3, 0.01, "black_plastic"),
                                                 F(0.16, -0.05, 0.12, 0.03, 0.2, 0.02, "metal", [0, 0, 20])], collide=False)
    prop("Gramophone", "small", W, (2, 0, 0), [F(0, 0, 0, 0.35, 0.35, 0.15, "wood_dark"), C(0, 0, 0.15, 0.28, 0.01, "black_plastic"),
                                               C(0.1, 0.0, 0.2, 0.05, 0.25, "brass", [0, 30, 0]), C(0.22, 0.0, 0.42, 0.4, 0.3, "brass", [0, 60, 0])], collide=False)

    # ------------------------------------------------------------------ kids, school, party
    prop("RockingHorse", "free", W, (2, 0, 0), [F(0, 0, 0.35, 0.25, 0.7, 0.3, "wood_light"), F(0, 0.35, 0.55, 0.18, 0.3, 0.3, "wood_light", [30, 0, 0]),
                                                F(-0.18, 0, 0, 0.04, 0.95, 0.06, "red_paint", [8, 0, 0]), F(0.18, 0, 0, 0.04, 0.95, 0.06, "red_paint", [8, 0, 0])] +
         [F(sx * 0.1, sy * 0.25, 0.05, 0.04, 0.04, 0.32, "wood_light") for sx in (-1, 1) for sy in (-1, 1)])
    prop("ToyBlocks", "small", W, (3, 0, 0), [F(0, 0, 0, 0.08, 0.08, 0.08, "red_paint"), F(0.1, 0.02, 0, 0.08, 0.08, 0.08, "blue_paint", [0, 0, 20]),
                                              F(0.05, 0.01, 0.08, 0.08, 0.08, 0.08, "yellow"), F(-0.15, 0.12, 0, 0.08, 0.08, 0.08, "fabric_green", [0, 0, 40]),
                                              F(0.2, -0.15, 0, 0.08, 0.08, 0.08, "yellow", [0, 0, 70])], collide=False)
    prop("RedBall", "small", W, (3, 1, 1), [S(0, 0, 0.11, 0.22, "red_paint")], collide=False)
    prop("Tricycle", "small", W, (2, 0, 0), [C(0, 0.25, 0.2, 0.4, 0.04, "rubber", [0, 90, 0]), C(-0.2, -0.2, 0.1, 0.2, 0.03, "rubber", [0, 90, 0]),
                                             C(0.2, -0.2, 0.1, 0.2, 0.03, "rubber", [0, 90, 0]), F(0, 0, 0.18, 0.05, 0.5, 0.05, "red_paint", [-10, 0, 0]),
                                             F(0, -0.1, 0.4, 0.2, 0.2, 0.04, "black_plastic"), F(0, 0.25, 0.55, 0.4, 0.04, 0.04, "metal")], collide=False)
    prop("DollHouse", "wall", W, (2, 0, 0), [F(0, 0, 0, 0.8, 0.4, 0.75, "white_plastic"), F(0, 0, 0.75, 0.82, 0.42, 0.25, "fabric_red", [0, 0, 0]),
                                             F(-0.2, 0.205, 0.15, 0.15, 0.01, 0.2, "glass_dark"), F(0.2, 0.205, 0.15, 0.15, 0.01, 0.2, "glass_dark"),
                                             F(-0.2, 0.205, 0.45, 0.15, 0.01, 0.15, "glass_dark"), F(0.2, 0.205, 0.45, 0.15, 0.01, 0.15, "glass_dark")])
    prop("Stroller", "free", W, (2, 0, 0), [F(0, 0, 0.4, 0.45, 0.7, 0.35, "fabric_blue"), F(0, -0.25, 0.75, 0.45, 0.3, 0.25, "fabric_blue", [30, 0, 0])] +
         [C(sx * 0.22, sy * 0.3, 0.08, 0.16, 0.03, "rubber", [0, 90, 0]) for sx in (-1, 1) for sy in (-1, 1)] +
         [F(0, 0.4, 0.4, 0.03, 0.03, 0.65, "metal", [-25, 0, 0]), F(0, 0.55, 1.0, 0.45, 0.03, 0.03, "metal")])
    prop("ToyChest", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.8, 0.45, 0.45, "wood_light"), F(0, -0.05, 0.45, 0.82, 0.47, 0.05, "red_paint", [-20, 0, 0]),
                                            S(0.1, 0.1, 0.5, 0.15, "teddy")])
    prop("SchoolDesk", "free", W, (3, 0, 0), table(0.6, 0.45, 0.72, "wood_light", "metal") +
         [F(0, -0.45, 0.38, 0.4, 0.36, 0.04, "blue_paint"), F(0, -0.62, 0.42, 0.4, 0.03, 0.35, "blue_paint")] +
         [F(sx * 0.17, -0.45, 0, 0.03, 0.03, 0.38, "metal") for sx in (-1, 1)])
    prop("Chalkboard", "wall", W, (2, 0, 0), [F(0, -0.1, 0.9, 1.6, 0.04, 1.0, "fabric_green"), F(0, -0.07, 0.88, 1.65, 0.08, 0.04, "wood_light"),
                                              F(-0.3, -0.08, 1.4, 0.5, 0.005, 0.03, "paper"), F(0.2, -0.08, 1.2, 0.35, 0.005, 0.03, "paper"),
                                              F(-0.78, -0.05, 0, 0.04, 0.3, 1.9, "wood_light"), F(0.78, -0.05, 0, 0.04, 0.3, 1.9, "wood_light")])
    prop("Globe", "small", W, (2, 0, 0), [C(0, 0, 0, 0.2, 0.03, "wood_dark"), C(0, 0, 0.03, 0.03, 0.15, "brass"), S(0, 0, 0.32, 0.3, "blue_paint")], collide=False)
    prop("Balloons", "free", W, (2, 0, 0), [C(0, 0, 0, 0.08, 0.04, "metal_dark")] + [
        F(dx * 0.5, dy * 0.5, 0.02, 0.005, 0.005, 1.6 + dz, "white_plastic") for dx, dy, dz in ((0.2, 0, 0), (-0.15, 0.1, 0.2), (0, -0.2, 0.1), (0.1, 0.2, -0.1))] + [
        S(dx, dy, 1.75 + dz, 0.3, m) for (dx, dy, dz), m in zip(((0.1, 0, 0), (-0.08, 0.05, 0.2), (0, -0.1, 0.1), (0.05, 0.1, -0.1)),
                                                              ("red_paint", "yellow", "blue_paint", "fabric_green"))], collide=False)
    prop("PartyTable", "free", W, (2, 0, 0), table(1.2, 0.7, 0.75, "white_plastic", "metal") + [
        C(0, 0, 0.75, 0.3, 0.15, "porcelain"), C(0, 0, 0.9, 0.02, 0.06, "yellow"), C(0.35, 0.1, 0.75, 0.25, 0.02, "fabric_red"),
        C(-0.35, -0.1, 0.75, 0.08, 0.12, "red_paint"), C(-0.25, 0.15, 0.75, 0.08, 0.12, "blue_paint")])
    prop("GiftBoxes", "small", W, (2, 0, 0), [F(0, 0, 0, 0.35, 0.35, 0.3, "red_paint"), F(0, 0, 0.0, 0.36, 0.06, 0.31, "yellow"),
                                              F(0.35, 0.1, 0, 0.25, 0.25, 0.2, "blue_paint", [0, 0, 20]), F(0.35, 0.1, 0, 0.26, 0.05, 0.21, "porcelain", [0, 0, 20])],
         collide=False)

    # ------------------------------------------------------------------ shops, streets, public places
    prop("Atm", "wall", W, (3, 1, 0), [F(0, 0, 0, 0.7, 0.6, 1.6, "metal"), F(0, 0.305, 1.1, 0.35, 0.01, 0.25, "screen_glow"),
                                       F(0, 0.32, 0.85, 0.35, 0.06, 0.12, "black_plastic"), F(0, 0.2, 1.55, 0.72, 0.4, 0.1, "blue_paint")])
    prop("GumballMachine", "free", W, (3, 0, 0), [C(0, 0, 0, 0.3, 0.05, "red_paint"), C(0, 0, 0.05, 0.06, 0.65, "red_paint"),
                                                  F(0, 0, 0.7, 0.25, 0.25, 0.25, "red_paint"), S(0, 0, 1.08, 0.38, "water_blue"), C(0, 0, 1.25, 0.12, 0.06, "red_paint")])
    prop("NewspaperBox", "wall", W, (2, 2, 0), [F(0, 0, 0, 0.45, 0.4, 0.95, "blue_paint"), F(0, 0.205, 0.55, 0.35, 0.01, 0.3, "glass_dark"),
                                                F(0, 0.21, 0.4, 0.2, 0.02, 0.04, "metal")])
    prop("Mailbox", "free", W, (2, 1, 0), [F(0, 0, 0, 0.05, 0.05, 1.0, "wood_dark"), F(0, 0, 1.0, 0.2, 0.45, 0.22, "metal_dark"),
                                           F(0.11, -0.1, 1.15, 0.01, 0.04, 0.2, "red_paint")])
    prop("FireHydrant", "free", W, (2, 3, 1), [C(0, 0, 0, 0.3, 0.06, "red_paint"), C(0, 0, 0.06, 0.22, 0.5, "red_paint"), C(0, 0, 0.56, 0.26, 0.08, "red_paint"),
                                               C(0, 0, 0.64, 0.12, 0.08, "red_paint"), C(0, 0.12, 0.35, 0.1, 0.1, "red_paint", [90, 0, 0]),
                                               C(0.12, 0, 0.35, 0.08, 0.08, "red_paint", [0, 90, 0])])
    prop("ParkBench", "wall", W, (3, 2, 0), [F(0, 0.05, 0.42, 1.6, 0.45, 0.05, "wood_light"), F(0, -0.2, 0.5, 1.6, 0.05, 0.4, "wood_light", [-10, 0, 0]),
                                             F(-0.7, 0, 0, 0.06, 0.5, 0.45, "metal_dark"), F(0.7, 0, 0, 0.06, 0.5, 0.45, "metal_dark")])
    prop("BusStopSign", "free", W, (2, 1, 0), [C(0, 0, 0, 0.35, 0.05, "concrete"), C(0, 0, 0.05, 0.06, 2.3, "metal"),
                                               F(0, 0, 2.0, 0.5, 0.03, 0.4, "blue_paint"), F(0, 0.02, 2.1, 0.3, 0.01, 0.15, "porcelain")])
    prop("StreetLamp", "free", W, (2, 2, 0), [C(0, 0, 0, 0.3, 0.3, "metal_dark"), C(0, 0, 0.3, 0.1, 2.3, "metal_dark"),
                                              F(0.25, 0, 2.55, 0.6, 0.08, 0.06, "metal_dark"), F(0.5, 0, 2.43, 0.3, 0.2, 0.12, "lamp_glow")])
    prop("PublicBin", "free", W, (4, 3, 1), [C(0, 0, 0, 0.5, 0.85, "green_paint"), C(0, 0, 0.85, 0.55, 0.08, "metal_dark"),
                                             F(0.1, 0.05, 0.86, 0.2, 0.15, 0.08, "paper", [20, 0, 30])])
    prop("RecyclingBins", "wall", W, (3, 3, 2), [F(-0.42, 0, 0, 0.4, 0.45, 0.8, "blue_paint"), F(0, 0, 0, 0.4, 0.45, 0.8, "fabric_green"),
                                                 F(0.42, 0, 0, 0.4, 0.45, 0.8, "yellow")] + [F(x, 0, 0.8, 0.42, 0.47, 0.04, "black_plastic") for x in (-0.42, 0, 0.42)])
    prop("InfoKiosk", "free", W, (2, 1, 0), [F(0, 0, 0, 0.5, 0.4, 1.1, "metal_dark"), F(0, 0.05, 1.1, 0.5, 0.35, 0.4, "metal_dark", [-25, 0, 0]),
                                             F(0, 0.12, 1.15, 0.42, 0.01, 0.3, "screen_glow", [-25, 0, 0])])
    prop("WetFloorSign", "small", W, (6, 3, 2), [F(0, 0.08, 0, 0.3, 0.02, 0.62, "yellow", [12, 0, 0]), F(0, -0.08, 0, 0.3, 0.02, 0.62, "yellow", [-12, 0, 0]),
                                                 F(0, 0.12, 0.3, 0.12, 0.005, 0.12, "black_plastic", [12, 0, 0])], collide=False)
    prop("Stanchions", "free", W, (3, 1, 0), [C(x, 0, 0, 0.3, 0.03, "brass") for x in (-0.6, 0.6)] + [C(x, 0, 0.03, 0.05, 0.95, "brass") for x in (-0.6, 0.6)] +
         [F(0, 0, 0.85, 1.15, 0.03, 0.03, "fabric_red")])
    prop("ShoppingBasket", "small", W, (3, 1, 0), [F(0, 0, 0, 0.45, 0.3, 0.22, "red_paint"), F(0, 0, 0.22, 0.4, 0.02, 0.15, "black_plastic"),
                                                   F(0.05, 0.0, 0.12, 0.15, 0.1, 0.15, "cardboard")], collide=False)
    prop("CheckoutCounter", "wall", W, (2, 0, 0), [F(0, 0, 0, 1.7, 0.6, 0.9, "white_plastic"), F(0, 0, 0.9, 1.7, 0.6, 0.03, "black_plastic"),
                                                   F(0.55, -0.1, 0.93, 0.4, 0.35, 0.18, "beige_plastic"), F(-0.3, 0, 0.93, 0.3, 0.25, 0.04, "glass_dark"),
                                                   F(0.6, 0.1, 1.11, 0.25, 0.05, 0.18, "screen_glow")])
    prop("StoreShelf", "wall", W, (3, 1, 0), [F(0, -0.25, 0, 1.2, 0.05, 1.8, "white_plastic")] + [F(0, 0, 0.1 + 0.42 * r, 1.2, 0.45, 0.03, "metal") for r in range(4)] + [
        F(-0.5 + 0.13 * i, 0.0, 0.13 + 0.42 * r, 0.1, 0.18, 0.2 + 0.05 * ((i + r) % 3), ["red_paint", "yellow", "blue_paint", "cardboard", "fabric_green"][(i * 2 + r) % 5])
        for i in range(8) for r in range(4) if (i + r * 3) % 5 != 0])
    prop("ClothesRack", "free", W, (3, 0, 0), [F(-0.55, 0, 0, 0.04, 0.4, 0.03, "metal"), F(0.55, 0, 0, 0.04, 0.4, 0.03, "metal"),
                                               F(-0.55, 0, 0, 0.03, 0.03, 1.5, "metal"), F(0.55, 0, 0, 0.03, 0.03, 1.5, "metal"),
                                               F(0, 0, 1.5, 1.15, 0.03, 0.03, "metal")] + [
        F(-0.45 + 0.15 * i, 0, 0.75, 0.04, 0.4, 0.72, ["fabric_red", "fabric_blue", "fabric_green", "fabric_brown", "mtf_uniform"][i % 5]) for i in range(7)])
    prop("MannequinTorso", "small", W, (2, 0, 0), [C(0, 0, 0, 0.2, 0.03, "metal_dark"), C(0, 0, 0.03, 0.03, 0.3, "metal"), F(0, 0, 0.33, 0.36, 0.2, 0.45, "mannequin")],
         collide=False)
    prop("SnackShelf", "wall", W, (3, 1, 0), [F(0, -0.12, 0, 0.9, 0.06, 1.5, "metal")] + [F(0, 0, 0.1 + 0.35 * r, 0.9, 0.3, 0.02, "metal") for r in range(4)] + [
        F(-0.35 + 0.1 * i, 0.02, 0.12 + 0.35 * r, 0.08, 0.06, 0.2, ["red_paint", "yellow", "orange", "blue_paint"][(i + r) % 4]) for i in range(8) for r in range(4) if (i * r) % 3 != 1])
    prop("IceMachine", "wall", W, (2, 1, 0), [F(0, 0, 0, 0.8, 0.65, 1.6, "appliance"), F(0, 0.33, 1.2, 0.6, 0.01, 0.25, "blue_paint"),
                                              F(0, 0.33, 0.5, 0.4, 0.01, 0.4, "metal")])
    prop("ClawMachine", "wall", W, (3, 0, 0), [F(0, 0, 0, 0.8, 0.8, 0.9, "red_paint"), F(0, 0, 0.9, 0.78, 0.78, 0.9, "glass_dark"),
                                               F(0, 0, 1.8, 0.8, 0.8, 0.2, "vending_glow"), S(-0.15, 0.1, 1.0, 0.2, "teddy"), S(0.2, -0.1, 1.0, 0.18, "fabric_green"),
                                               F(0, 0.41, 0.8, 0.3, 0.02, 0.08, "black_plastic")])
    prop("PinballMachine", "free", W, (2, 0, 0), [F(0, 0, 0.75, 0.7, 1.3, 0.2, "black_plastic", [-5, 0, 0]), F(0, 0.05, 0.96, 0.62, 1.15, 0.01, "screen_glow", [-5, 0, 0]),
                                                  F(0, -0.62, 0.85, 0.7, 0.12, 0.7, "black_plastic"), F(0, -0.56, 1.05, 0.6, 0.01, 0.4, "vending_glow")] +
         legs4(0.68, 1.25, 0.8, 0.05, "metal"))
    prop("Jukebox", "wall", W, (2, 0, 0), [F(0, 0, 0, 0.9, 0.6, 1.1, "wood_dark"), C(0, 0, 1.1, 0.9, 0.6, "wood_dark", [90, 0, 0]),
                                           F(0, 0.305, 0.6, 0.6, 0.01, 0.4, "vending_glow"), F(0, 0.305, 0.15, 0.6, 0.01, 0.3, "brass")])
    prop("PhotoBooth", "wall", W, (2, 0, 0), [F(0, 0, 0, 1.0, 0.9, 2.1, "metal"), F(0.25, 0.455, 0.1, 0.45, 0.01, 1.9, "fabric_red"),
                                              F(-0.3, 0.455, 1.3, 0.3, 0.01, 0.5, "vending_glow")])
    prop("Turnstile", "free", W, (1, 2, 0), [F(-0.45, 0, 0, 0.15, 0.8, 1.0, "metal"), F(0.45, 0, 0, 0.15, 0.8, 1.0, "metal"),
                                             C(0, 0, 0.85, 0.08, 0.3, "metal"), F(0.2, 0, 0.9, 0.4, 0.04, 0.04, "metal")])
    prop("StorageBins", "small", W, (3, 2, 2), [F(0, 0, 0, 0.55, 0.4, 0.3, "water_blue"), F(0, 0, 0.3, 0.55, 0.4, 0.3, "water_blue"),
                                                F(0, 0, 0.6, 0.57, 0.42, 0.03, "black_plastic")], collide=False)
    prop("CoolerBox", "small", W, (3, 2, 2), [F(0, 0, 0, 0.6, 0.38, 0.38, "blue_paint"), F(0, 0, 0.38, 0.62, 0.4, 0.05, "white_plastic"),
                                              F(0, 0, 0.43, 0.3, 0.04, 0.03, "white_plastic")], collide=False)

    # ------------------------------------------------------------------ hospital and institutions
    prop("Wheelchair", "free", W, (2, 0, 2), [F(0, 0, 0.45, 0.45, 0.45, 0.05, "fabric_blue"), F(0, -0.22, 0.5, 0.45, 0.04, 0.45, "fabric_blue"),
                                              C(-0.28, -0.05, 0.3, 0.6, 0.03, "metal", [0, 90, 0]), C(0.28, -0.05, 0.3, 0.6, 0.03, "metal", [0, 90, 0]),
                                              C(-0.18, 0.3, 0.08, 0.15, 0.03, "rubber", [0, 90, 0]), C(0.18, 0.3, 0.08, 0.15, 0.03, "rubber", [0, 90, 0]),
                                              F(0, -0.25, 0.9, 0.5, 0.03, 0.03, "metal")])
    prop("IvStand", "free", W, (2, 0, 2), [F(0, 0, 0.02, 0.5, 0.04, 0.04, "metal"), F(0, 0, 0.02, 0.04, 0.5, 0.04, "metal"), C(0, 0, 0.04, 0.03, 1.8, "metal"),
                                           F(0, 0, 1.82, 0.3, 0.02, 0.02, "metal"), F(0.12, 0, 1.55, 0.1, 0.04, 0.2, "water_blue")])
    prop("Gurney", "big", W, (2, 0, 2), [F(0, 0, 0.75, 0.65, 1.9, 0.08, "metal"), F(0, 0, 0.83, 0.6, 1.85, 0.08, "mattress"),
                                         F(0, 0.1, 0.91, 0.62, 1.2, 0.03, "porcelain")] + legs4(0.6, 1.8, 0.75, 0.04, "metal"))
    prop("MedicalCabinet", "wall", W, (2, 0, 2), cabinet(0.8, 0.4, 1.8, "white_plastic", "glass_dark", "metal", 2, 2) + [F(0, 0.21, 1.5, 0.15, 0.01, 0.15, "red_paint")])
    prop("WaitingChairs", "wall", W, (3, 1, 0), [F(0, 0, 0, 1.6, 0.06, 0.06, "metal")] + [F(x, 0.0, 0.42, 0.48, 0.45, 0.06, "blue_paint") for x in (-0.55, 0, 0.55)] +
         [F(x, -0.2, 0.48, 0.48, 0.05, 0.42, "blue_paint") for x in (-0.55, 0, 0.55)] + [F(x, 0, 0, 0.04, 0.4, 0.42, "metal") for x in (-0.78, 0.78)])
    prop("FoldingScreen", "free", W, (2, 0, 1), [F(-0.45, 0, 0, 0.45, 0.03, 1.7, "white_plastic", [0, 0, 20]), F(0, 0.08, 0, 0.45, 0.03, 1.7, "white_plastic", [0, 0, -15]),
                                                 F(0.45, 0, 0, 0.45, 0.03, 1.7, "white_plastic", [0, 0, 20])])
    prop("OxygenTank", "small", W, (2, 0, 3), [C(0, 0, 0, 0.18, 0.9, "fabric_green"), C(0, 0, 0.9, 0.08, 0.1, "metal"), F(0, 0.05, 0.95, 0.06, 0.08, 0.04, "metal")],
         collide=False)
    prop("FirstAidBox", "wall", W, (3, 2, 3), [F(0, -0.12, 1.3, 0.4, 0.15, 0.3, "white_plastic"), F(0, -0.04, 1.4, 0.12, 0.01, 0.04, "red_paint"),
                                               F(0, -0.04, 1.37, 0.04, 0.01, 0.1, "red_paint")], collide=False)

    # ------------------------------------------------------------------ garage and warehouse (Level 1)
    prop("Forklift", "big", W, (0, 4, 0), [F(0, -0.3, 0.25, 1.1, 1.6, 0.7, "yellow"), F(0, -0.85, 0.25, 1.1, 0.5, 0.8, "metal_dark"),
                                           F(0, -0.2, 0.95, 0.6, 0.6, 0.1, "black_plastic"), F(-0.5, -0.3, 0.95, 0.06, 0.06, 1.2, "metal_dark"),
                                           F(0.5, -0.3, 0.95, 0.06, 0.06, 1.2, "metal_dark"), F(0, -0.3, 2.15, 1.1, 1.2, 0.06, "metal_dark"),
                                           F(-0.35, 0.65, 0, 0.1, 0.1, 2.2, "metal_dark"), F(0.35, 0.65, 0, 0.1, 0.1, 2.2, "metal_dark"),
                                           F(-0.25, 1.1, 0.05, 0.12, 0.9, 0.05, "metal_dark"), F(0.25, 1.1, 0.05, 0.12, 0.9, 0.05, "metal_dark")] +
         [C(sx * 0.55, y, 0.27, 0.54, 0.2, "rubber", [0, 90, 0]) for sx in (-1, 1) for y in (-0.8, 0.35)])
    prop("PalletJack", "free", W, (0, 5, 1), [F(-0.2, 0.3, 0, 0.16, 1.15, 0.08, "red_paint"), F(0.2, 0.3, 0, 0.16, 1.15, 0.08, "red_paint"),
                                              F(0, -0.3, 0, 0.55, 0.25, 0.25, "red_paint"), F(0, -0.38, 0.25, 0.05, 0.05, 1.0, "metal_dark", [-20, 0, 0]),
                                              F(0, -0.55, 1.2, 0.3, 0.04, 0.04, "black_plastic")])
    prop("EmptyPallet", "free", W, (1, 6, 2), [F(0, y, 0, 1.2, 0.1, 0.1, "wood_light") for y in (-0.45, 0, 0.45)] + [
        F(-0.55 + 0.157 * i, 0, 0.1, 0.12, 1.0, 0.025, "wood_light") for i in range(8)])
    prop("PalletPile", "free", W, (0, 5, 1), [F(0, 0, 0.14 * i, 1.2, 1.0, 0.13, "wood_light", [0, 0, (i * 7) % 11 - 5]) for i in range(7)])
    prop("WrappedCrates", "free", W, (0, 6, 1), [F(0, 0, 0, 1.2, 1.0, 0.14, "wood_light"), F(0, 0, 0.14, 1.15, 0.95, 1.1, "glass_dark"),
                                                 F(0, 0, 0.14, 1.1, 0.9, 1.08, "cardboard")])
    prop("DrumCluster", "free", W, (0, 5, 4), [C(-0.3, -0.3, 0, 0.58, 0.9, "blue_paint"), C(0.3, -0.3, 0, 0.58, 0.9, "rust"),
                                               C(0, 0.25, 0, 0.58, 0.9, "yellow"), C(0.0, 0.25, 0.9, 0.6, 0.03, "metal_dark")])
    prop("IbcTank", "free", W, (0, 4, 3), [F(0, 0, 0, 1.2, 1.0, 0.15, "metal_dark"), F(0, 0, 0.15, 1.1, 0.95, 0.95, "white_plastic"),
                                           F(0, 0.48, 0.15, 1.15, 0.03, 1.0, "metal"),
                                           F(0, -0.48, 0.15, 1.15, 0.03, 1.0, "metal"), C(0, 0, 1.1, 0.2, 0.08, "black_plastic")])
    prop("Workbench", "wall", W, (1, 5, 4), table(1.6, 0.7, 0.9, "wood_light", "metal_dark", 0.06) + [
        F(0, -0.33, 0.9, 1.6, 0.04, 0.8, "metal_dark"), F(0.5, 0.0, 0.9, 0.3, 0.2, 0.15, "red_paint"), F(-0.4, 0.1, 0.9, 0.3, 0.06, 0.04, "metal"),
        F(-0.2, -0.3, 1.3, 0.05, 0.03, 0.25, "metal"), F(0.1, -0.3, 1.3, 0.05, 0.03, 0.2, "metal"), F(0, 0, 0.2, 1.5, 0.6, 0.03, "wood_light")])
    prop("RollingToolChest", "wall", W, (0, 5, 4), cabinet(0.7, 0.45, 1.0, "red_paint", "red_paint", "metal", 6, 1) + [
        C(sx * 0.3, sy * 0.18, 0, 0.08, 0.05, "rubber") for sx in (-1, 1) for sy in (-1, 1)])
    prop("AirCompressor", "free", W, (0, 4, 4), [C(0, 0, 0.1, 0.4, 0.9, "red_paint", [0, 90, 0]), F(0, 0, 0.35, 0.4, 0.3, 0.25, "black_plastic"),
                                                 C(0.35, 0, 0, 0.12, 0.05, "rubber"), C(-0.35, 0, 0, 0.12, 0.05, "rubber")])
    prop("PortableGenerator", "free", W, (0, 4, 4), [F(0, 0, 0.05, 0.65, 0.5, 0.45, "yellow"), F(0, 0, 0.5, 0.5, 0.4, 0.12, "black_plastic")] +
         [F(sx * 0.33, 0, 0, 0.04, 0.52, 0.55, "metal_dark") for sx in (-1, 1)])
    prop("Welder", "small", W, (0, 3, 3), [F(0, 0, 0, 0.45, 0.3, 0.35, "blue_paint"), F(0, 0.155, 0.15, 0.3, 0.01, 0.12, "black_plastic"),
                                           C(0.3, 0.1, 0, 0.25, 0.05, "black_plastic")], collide=False)
    prop("JerryCans", "small", W, (0, 4, 3), [F(0, 0, 0, 0.18, 0.36, 0.46, "red_paint"), F(0.22, 0.03, 0, 0.18, 0.36, 0.46, "fabric_green", [0, 0, 8]),
                                              F(0, 0, 0.46, 0.04, 0.2, 0.04, "red_paint")], collide=False)
    prop("TireRack", "wall", W, (0, 5, 0), [F(-0.75, 0, 0, 0.05, 0.6, 1.6, "metal_dark"), F(0.75, 0, 0, 0.05, 0.6, 1.6, "metal_dark"),
                                            F(0, 0, 0.6, 1.5, 0.05, 0.05, "metal_dark"), F(0, 0, 1.4, 1.5, 0.05, 0.05, "metal_dark")] + [
        C(-0.5 + 0.33 * i, 0, z, 0.62, 0.2, "rubber", [0, 90, 0]) for i in range(4) for z in (0.0, 0.8)])
    prop("EngineBlock", "free", W, (0, 3, 2), [F(0, 0, 0, 0.8, 0.6, 0.15, "wood_light"), F(0, 0, 0.15, 0.6, 0.5, 0.45, "metal_dark"),
                                               F(0, 0, 0.6, 0.5, 0.35, 0.12, "metal"), C(0.32, 0, 0.3, 0.25, 0.08, "metal", [0, 90, 0])])
    prop("Bicycle", "free", W, (1, 4, 1), [C(0, 0.5, 0.33, 0.66, 0.04, "rubber", [0, 90, 0]), C(0, -0.5, 0.33, 0.66, 0.04, "rubber", [0, 90, 0]),
                                           F(0, 0, 0.5, 0.04, 0.8, 0.04, "red_paint", [-20, 0, 0]), F(0, -0.25, 0.5, 0.04, 0.04, 0.4, "red_paint"),
                                           F(0, -0.25, 0.9, 0.12, 0.25, 0.05, "black_plastic"), F(0, 0.45, 0.95, 0.5, 0.03, 0.03, "metal")])
    prop("ScissorLift", "big", W, (0, 3, 0), [F(0, 0, 0.1, 1.0, 2.2, 0.35, "orange")] + [C(sx * 0.4, sy * 0.85, 0.12, 0.25, 0.12, "rubber", [0, 90, 0]) for sx in (-1, 1) for sy in (-1, 1)] +
         [F(0, 0, 0.45 + 0.4 * i, 0.06, 2.0, 0.06, "metal_dark", [20 if i % 2 else -20, 0, 0]) for i in range(4)] + [F(0, 0, 2.05, 1.0, 2.2, 0.06, "orange"),
                                                                                                                F(0, 0, 2.1, 1.0, 0.04, 1.0, "orange")])
    prop("TrafficBarrels", "free", W, (1, 5, 1), [C(0, 0, 0, 0.55, 0.95, "orange"), C(0, 0, 0.25, 0.57, 0.1, "white_plastic"), C(0, 0, 0.6, 0.57, 0.1, "white_plastic"),
                                                  C(0.6, 0.2, 0, 0.55, 0.95, "orange"), C(0.6, 0.2, 0.4, 0.57, 0.1, "white_plastic")])
    prop("RoadSign", "small", W, (1, 4, 1), [F(0, 0, 0, 0.5, 0.4, 0.05, "black_plastic"), F(0, 0, 0.05, 0.6, 0.03, 0.6, "orange", [-8, 0, 0]),
                                             F(0, 0.02, 0.25, 0.2, 0.01, 0.2, "black_plastic", [-8, 0, 0])], collide=False)
    prop("ParkingMeter", "free", W, (1, 4, 0), [C(0, 0, 0, 0.08, 1.15, "metal_dark"), F(0, 0, 1.15, 0.22, 0.18, 0.35, "metal"),
                                                F(0, 0.092, 1.3, 0.15, 0.01, 0.1, "glass_dark")])
    prop("TicketMachine", "wall", W, (0, 4, 0), [F(0, 0, 0, 0.6, 0.45, 1.5, "yellow"), F(0, 0.23, 1.1, 0.35, 0.01, 0.2, "screen_glow"),
                                                 F(0, 0.24, 0.8, 0.2, 0.03, 0.06, "black_plastic")])
    prop("ShippingContainer", "big", W, (0, 2, 0), [F(0, 0, 0, 2.4, 6.0, 2.5, "rust"), F(0, 3.005, 0.05, 2.3, 0.01, 2.4, "red_paint")] + [
        F(-1.205, -2.7 + 0.6 * i, 0.05, 0.01, 0.08, 2.4, "red_paint") for i in range(10)])
    prop("WhiteVan", "big", W, (0, 5, 0), [F(0, 0, 0.3, 1.95, 4.9, 1.6, "appliance"), F(0, 2.05, 0.95, 1.8, 0.6, 0.6, "glass_dark", [-25, 0, 0]),
                                           F(-0.98, 1.6, 1.15, 0.02, 0.6, 0.45, "glass_dark"), F(0.98, 1.6, 1.15, 0.02, 0.6, 0.45, "glass_dark"),
                                           F(-0.6, 2.455, 0.55, 0.3, 0.01, 0.12, "lamp_glow"), F(0.6, 2.455, 0.55, 0.3, 0.01, 0.12, "lamp_glow")] +
         [C(sx * 0.9, y, 0.35, 0.7, 0.25, "rubber", [0, 90, 0]) for sx in (-1, 1) for y in (1.6, -1.5)])
    prop("PickupTruck", "big", W, (0, 4, 0), [F(0, 0, 0.45, 1.9, 5.2, 0.6, "car_red"), F(0, 0.6, 1.05, 1.8, 1.8, 0.65, "car_red"),
                                              F(0, 1.52, 1.1, 1.7, 0.02, 0.5, "glass_dark", [-25, 0, 0]), F(0, -1.5, 1.05, 1.9, 2.0, 0.35, "car_red"),
                                              F(0, -1.5, 1.2, 1.7, 1.9, 0.25, "black_plastic")] +
         [C(sx * 0.85, y, 0.38, 0.76, 0.28, "rubber", [0, 90, 0]) for sx in (-1, 1) for y in (1.7, -1.7)])
    prop("WreckedCar", "big", W, (0, 3, 0), [F(0, 0, 0.1, 1.8, 4.3, 0.55, "rust", [3, -6, 4]), F(0.1, -0.3, 0.6, 1.5, 2.0, 0.4, "rust", [-8, 4, 2]),
                                             F(0, 1.8, 0.4, 1.7, 0.6, 0.3, "metal_dark", [20, 0, 0])] +
         [C(sx * 0.8, y, 0.25, 0.6, 0.22, "rubber", [0, 90, 0]) for sx, y in ((-1, 1.3), (1, -1.3), (-1, -1.3))])
    prop("TaxiCab", "big", W, (0, 4, 0), [F(0, 0, 0.28, 1.8, 4.6, 0.55, "yellow"), F(0, -0.2, 0.83, 1.6, 2.4, 0.5, "yellow"),
                                          F(0, -0.2, 1.33, 0.5, 0.25, 0.15, "vending_glow"), F(-0.805, -0.2, 0.9, 0.02, 2.1, 0.36, "glass_dark"),
                                          F(0.805, -0.2, 0.9, 0.02, 2.1, 0.36, "glass_dark")] +
         [C(sx * 0.8, y, 0.33, 0.66, 0.24, "rubber", [0, 90, 0]) for sx in (-1, 1) for y in (1.4, -1.4)])
    prop("CementBags", "small", W, (0, 4, 2), [F(0, 0, 0, 0.6, 0.4, 0.15, "paper"), F(0.05, 0, 0.15, 0.6, 0.4, 0.15, "paper", [0, 0, 8]),
                                               F(-0.05, 0.02, 0.3, 0.6, 0.4, 0.15, "paper", [0, 0, -6])], collide=False)
    prop("RebarBundle", "small", W, (0, 3, 2), [C(-0.05 + 0.03 * i, 0, 0.05 + 0.02 * (i % 2), 0.03, 1.8, "rust", [90, 0, 3 * i]) for i in range(6)], collide=False)
    prop("Scaffolding", "wall", W, (0, 4, 1), [F(sx * 0.9, sy * 0.35, 0, 0.05, 0.05, 2.4, "metal") for sx in (-1, 1) for sy in (-1, 1)] +
         [F(0, sy * 0.35, z, 1.85, 0.05, 0.05, "metal") for sy in (-1, 1) for z in (0.8, 1.6, 2.35)] + [F(0, 0, 1.6, 1.85, 0.75, 0.04, "wood_light")])
    prop("StepLadder", "free", W, (2, 4, 3), [F(0, 0.25, 0, 0.45, 0.05, 1.7, "orange", [-12, 0, 0]), F(0, -0.25, 0, 0.45, 0.05, 1.7, "orange", [12, 0, 0])] +
         [F(0, 0.18 - 0.04 * i, 0.3 + 0.35 * i, 0.42, 0.12, 0.03, "metal") for i in range(4)] + [F(0, 0, 1.65, 0.45, 0.25, 0.05, "orange")])
    prop("PaintCans", "small", W, (2, 3, 2), [C(0, 0, 0, 0.18, 0.2, "metal"), C(0.2, 0.05, 0, 0.18, 0.2, "metal"), C(0.1, 0.02, 0.2, 0.18, 0.2, "metal"),
                                              C(0, 0, 0.2, 0.18, 0.005, "blue_paint"), C(0.2, 0.05, 0.2, 0.18, 0.005, "red_paint")], collide=False)
    prop("CableSpool", "free", W, (0, 4, 5), [C(-0.3, 0, 0.5, 1.0, 0.08, "wood_light", [0, 90, 0]),
                                              C(0.3, 0, 0.5, 1.0, 0.08, "wood_light", [0, 90, 0]), C(0, 0, 0.5, 0.7, 0.55, "black_plastic", [0, 90, 0])])
    prop("CinderBlocks", "small", W, (1, 4, 3), [F(0, 0, 0, 0.4, 0.2, 0.2, "concrete"), F(0.42, 0, 0, 0.4, 0.2, 0.2, "concrete"),
                                                 F(0.2, 0, 0.2, 0.4, 0.2, 0.2, "concrete", [0, 0, 5]), F(-0.1, 0.3, 0, 0.4, 0.2, 0.2, "concrete", [0, 0, 70])], collide=False)
    prop("HoseReel", "wall", W, (0, 3, 4), [F(0, -0.1, 0.5, 0.4, 0.06, 0.5, "red_paint"), C(0, 0.0, 0.75, 0.45, 0.15, "fabric_green", [90, 0, 0])], collide=False)

    # ------------------------------------------------------------------ tunnels (Level 2)
    prop("ValveManifold", "wall", W, (0, 1, 7), [C(0, -0.1, 0.2, 0.15, 1.6, "rust", [0, 90, 0]), C(-0.5, -0.1, 0, 0.12, 0.2, "rust"),
                                                 C(0.5, -0.1, 0, 0.12, 0.2, "rust")] + [C(x, -0.02, 0.2, 0.25, 0.03, "red_paint", [90, 0, 0]) for x in (-0.3, 0.1, 0.45)] +
         [C(0, -0.1, 0.2, 0.12, 1.4, "rust")], collide=False)
    prop("ElectricalCabinet", "wall", W, (0, 2, 7), cabinet(0.9, 0.4, 1.9, "metal", "metal", "black_plastic", 1, 2) + [F(0, 0.21, 1.6, 0.25, 0.01, 0.12, "yellow")])
    prop("PipeJunction", "wall", W, (0, 0, 6), [C(0, -0.1, 0, 0.3, 2.4, "rust"), C(0, 0.05, 1.1, 0.25, 0.4, "rust", [90, 0, 0]),
                                                C(0, -0.1, 1.0, 0.4, 0.08, "metal_dark"), C(0, 0.2, 1.1, 0.35, 0.06, "metal_dark", [90, 0, 0])], collide=False)
    prop("PumpUnit", "free", W, (0, 1, 6), [F(0, 0, 0, 1.0, 0.6, 0.12, "metal_dark"), C(-0.15, 0, 0.12, 0.4, 0.55, "blue_paint", [0, 90, 0]),
                                            C(0.3, 0, 0.12, 0.3, 0.4, "metal"), C(0.3, 0, 0.52, 0.1, 0.3, "rust")])
    prop("SmallCableSpool", "small", W, (0, 2, 4), [C(0, 0, 0.25, 0.5, 0.04, "wood_light", [90, 0, 0]), C(0, 0.3, 0.25, 0.5, 0.04, "wood_light", [90, 0, 0]),
                                                    C(0, 0.15, 0.25, 0.35, 0.3, "orange", [90, 0, 0])], collide=False)
    prop("FloorGrate", "small", W, (0, 1, 5), [F(0, 0, 0, 1.0, 1.0, 0.02, "metal_dark")] + [F(-0.4 + 0.1 * i, 0, 0.02, 0.02, 1.0, 0.01, "rust") for i in range(9)],
         collide=False)
    prop("MaintenanceCart", "free", W, (1, 2, 5), table(0.9, 0.5, 0.9, "metal", "metal", 0.03) + [F(0, 0, 0.3, 0.85, 0.45, 0.03, "metal"),
                                                                                              F(0.2, 0, 0.9, 0.35, 0.2, 0.15, "red_paint"), C(-0.25, 0, 0.33, 0.25, 0.3, "metal")])
    prop("BreakerPanel", "wall", W, (0, 2, 6), [F(0, -0.1, 0.9, 0.55, 0.12, 0.8, "metal"), F(-0.33, -0.04, 0.9, 0.02, 0.4, 0.8, "metal", [0, 0, -60])] + [
        F(-0.15 + 0.1 * c, -0.035, 1.0 + 0.12 * r, 0.05, 0.02, 0.08, "black_plastic") for c in range(4) for r in range(4)], collide=False)
    prop("JunctionBox", "wall", W, (1, 2, 6), [F(0, -0.12, 1.6, 0.3, 0.1, 0.3, "metal_dark"), C(0, -0.12, 1.9, 0.05, 0.7, "metal_dark"),
                                               C(0.1, -0.12, 0.2, 0.05, 1.4, "metal_dark")], collide=False)
    prop("WaterHeater", "wall", W, (1, 2, 5), [C(0, 0, 0, 0.55, 1.5, "appliance"), C(0, 0, 1.5, 0.1, 0.4, "metal"), F(0, 0.28, 0.25, 0.15, 0.02, 0.1, "metal_dark")])
    prop("GaugePanel", "wall", W, (0, 1, 6), [F(0, -0.1, 0.8, 0.9, 0.08, 0.7, "green_paint")] + [
        C(-0.25 + 0.25 * i, -0.05, 1.1, 0.15, 0.02, "porcelain", [90, 0, 0]) for i in range(3)] + [
        F(-0.25 + 0.25 * i, -0.04, 1.1, 0.01, 0.01, 0.06, "red_paint", [0, 25 * i - 20, 0]) for i in range(3)] + [
        F(0, -0.04, 0.9, 0.5, 0.02, 0.06, "black_plastic")], collide=False)
    prop("ChemicalDrums", "free", W, (0, 2, 5), [C(-0.3, 0, 0, 0.58, 0.9, "yellow"), C(0.3, 0.05, 0, 0.58, 0.9, "yellow"),
                                                 C(-0.3, 0.3, 0.4, 0.2, 0.01, "black_plastic", [90, 0, 0]), C(0.3, 0.35, 0.4, 0.2, 0.01, "black_plastic", [90, 0, 0])])
    prop("RagPile", "small", W, (1, 2, 4), [S(0, 0, 0.05, 0.4, "mattress", 0.1), S(0.15, 0.1, 0.07, 0.3, "fabric_brown", 0.12), S(-0.1, 0.12, 0.04, 0.3, "rust", 0.08)],
         collide=False)
    prop("Wrenches", "small", W, (0, 2, 4), [F(0, 0, 0, 0.4, 0.04, 0.02, "metal", [0, 0, 20]), F(0.05, 0.1, 0, 0.3, 0.035, 0.02, "metal", [0, 0, -30]),
                                             F(-0.1, -0.1, 0, 0.25, 0.03, 0.02, "metal_dark", [0, 0, 80])], collide=False)
    prop("Hardhat", "small", W, (1, 3, 4), [S(0, 0, 0.08, 0.3, "yellow", 0.16), C(0, 0, 0, 0.34, 0.02, "yellow")], collide=False)
    prop("MopBucket", "small", W, (3, 2, 4), [F(0, 0, 0, 0.45, 0.35, 0.35, "yellow"), C(0.1, 0, 0.3, 0.03, 1.2, "wood_light", [0, 15, 0]),
                                              F(0, 0, 0.32, 0.4, 0.3, 0.04, "water_blue")], collide=False)
    prop("ValveWheel", "small", W, (0, 1, 4), [C(0, 0, 0, 0.12, 0.7, "rust"), C(0, 0, 0.7, 0.4, 0.04, "red_paint"), C(0, 0, 0.7, 0.08, 0.08, "metal_dark")],
         collide=False)
    prop("RadioTransmitter", "wall", W, (1, 1, 4), [F(0, 0, 0, 0.7, 0.45, 0.8, "green_paint"), F(0, 0.23, 0.45, 0.55, 0.01, 0.25, "black_plastic")] + [
        C(-0.2 + 0.13 * i, 0.235, 0.35, 0.06, 0.03, "metal", [90, 0, 0]) for i in range(4)] + [F(0.1, 0.23, 0.6, 0.15, 0.01, 0.08, "screen_glow"),
                                                                                                 C(0.25, -0.1, 0.8, 0.02, 0.9, "metal")])
    prop("ArmyCot", "free", W, (2, 2, 3), [F(0, 0, 0.4, 0.7, 1.9, 0.04, "mtf_vest")] + [F(0, sy * 0.85, 0, 0.7, 0.03, 0.4, "metal_dark", [0, 0, 0]) for sy in (-1, 1)] +
         [F(sx * 0.33, 0, 0.38, 0.03, 1.9, 0.03, "metal_dark") for sx in (-1, 1)] + [F(0, -0.6, 0.44, 0.5, 0.3, 0.1, "mattress")], collide=False)
    prop("FoundationCrate", "free", W, (3, 3, 3), [F(0, 0, 0, 1.0, 0.7, 0.7, "metal_dark"), F(0, 0.355, 0.2, 0.3, 0.01, 0.3, "porcelain"),
                                                   C(0, 0.36, 0.35, 0.18, 0.01, "metal_dark", [90, 0, 0]), F(0, 0, 0.7, 1.02, 0.72, 0.05, "mtf_patch")])
    prop("Sandbags", "wall", W, (2, 3, 2), [F(-0.55 + 0.55 * i, 0, 0.18 * r, 0.55, 0.4, 0.18, "cardboard", [0, 0, (i * 5 + r * 7) % 9 - 4])
                                            for r in range(3) for i in range(3 - (r == 2))])
    prop("AmmoCrate", "small", W, (2, 2, 3), [F(0, 0, 0, 0.5, 0.25, 0.25, "fabric_green"), F(0, 0, 0.25, 0.52, 0.27, 0.03, "fabric_green"),
                                              F(0, 0.13, 0.15, 0.15, 0.01, 0.05, "yellow")], collide=False)
    prop("FieldRadio", "small", W, (2, 2, 3), [F(0, 0, 0, 0.35, 0.2, 0.3, "fabric_green"), C(0.12, 0, 0.3, 0.015, 0.8, "metal", [0, 10, 0]),
                                               F(0, 0.101, 0.15, 0.25, 0.01, 0.1, "black_plastic")], collide=False)
    prop("CampTent", "big", W, (2, 1, 1), [F(0, 0, 0, 1.8, 2.1, 0.03, "fabric_green"), F(-0.45, 0, 0.0, 0.03, 2.1, 1.2, "fabric_green", [0, 40, 0]),
                                           F(0.45, 0, 0.0, 0.03, 2.1, 1.2, "fabric_green", [0, -40, 0]), F(0, 1.05, 0.0, 0.05, 0.05, 1.0, "wood_dark")],
         collide=True)
    prop("CampfireRing", "small", W, (3, 1, 1), [F(0.3 * x, 0.3 * y, 0, 0.15, 0.12, 0.1, "concrete", [0, 0, 40 * i]) for i, (x, y) in
                                                 enumerate(((1, 0), (0.7, 0.7), (0, 1), (-0.7, 0.7), (-1, 0), (-0.7, -0.7), (0, -1), (0.7, -0.7)))] + [
        F(0, 0, 0, 0.35, 0.05, 0.05, "wood_dark", [0, 0, 30]), F(0, 0, 0.04, 0.35, 0.05, 0.05, "wood_dark", [0, 0, -40]), F(0, 0, 0, 0.2, 0.2, 0.02, "rubber")],
         collide=False)
    prop("DoNotEnterBarrier", "free", W, (2, 2, 2), [F(-0.6, 0, 0, 0.05, 0.4, 0.05, "metal"), F(0.6, 0, 0, 0.05, 0.4, 0.05, "metal"),
                                                     F(-0.6, 0, 0, 0.05, 0.05, 1.0, "metal"), F(0.6, 0, 0, 0.05, 0.05, 1.0, "metal"),
                                                     F(0, 0, 0.7, 1.25, 0.03, 0.25, "yellow")] + [
        F(-0.5 + 0.2 * i, 0.017, 0.7, 0.08, 0.005, 0.25, "black_plastic", [0, 30, 0]) for i in range(6)])
    prop("HazmatSuit", "wall", W, (1, 2, 3), [F(0, -0.12, 0.5, 0.45, 0.2, 0.8, "yellow"), F(0, -0.12, 1.3, 0.3, 0.2, 0.3, "yellow"),
                                              F(0, -0.02, 1.35, 0.2, 0.01, 0.12, "glass_dark"), F(-0.1, -0.12, 0.0, 0.14, 0.18, 0.5, "yellow"),
                                              F(0.1, -0.12, 0.0, 0.14, 0.18, 0.5, "yellow")], collide=False)
    prop("BodyBag", "small", W, (1, 1, 2), [S(0, 0, 0.1, 0.5, "black_plastic", 0.2), F(0, 0, 0, 0.5, 1.7, 0.2, "black_plastic"), S(0, 0.8, 0.1, 0.4, "black_plastic", 0.2),
                                            F(0, 0, 0.2, 0.02, 1.5, 0.005, "metal")], collide=False)

    # ------------------------------------------------------------------ wrong things (the Backrooms' own)
    prop("StairsToNowhere", "wall", W, (2, 1, 0), [F(0, 0.33 - 0.22 * i, 0, 1.0, 0.22, 0.2 * (i + 1), "ceiling_tile") for i in range(4)])
    prop("DoorInWall", "wall", W, (4, 1, 1), [F(-0.5, -0.07, 0, 0.08, 0.1, 2.1, "wood_light"), F(0.5, -0.07, 0, 0.08, 0.1, 2.1, "wood_light"),
                                              F(0, -0.07, 2.06, 1.08, 0.1, 0.08, "wood_light"), F(0, -0.1, 0, 0.9, 0.04, 2.06, "wood_dark"),
                                              C(0.35, -0.06, 1.0, 0.06, 0.06, "brass", [90, 0, 0])], collide=False)
    prop("WindowToNothing", "wall", W, (3, 1, 1), [F(0, -0.08, 0.9, 1.2, 0.06, 1.1, "wood_light"), F(0, -0.05, 0.95, 1.1, 0.01, 1.0, "glass_dark"),
                                                   F(0, -0.04, 0.95, 0.04, 0.01, 1.0, "wood_light"), F(0, -0.04, 1.45, 1.1, 0.01, 0.04, "wood_light"),
                                                   F(0, -0.02, 0.86, 1.3, 0.12, 0.04, "wood_light")], collide=False)
    prop("ChairTower", "free", W, (2, 1, 0), [p for i in range(5) for p in [F(0, 0, 0.45 * i + 0.41, 0.44, 0.42, 0.04, "white_plastic", [0, 0, 15 * i]),
                                                                             F(0.18, 0.18, 0.45 * i, 0.03, 0.03, 0.41, "white_plastic"),
                                                                             F(-0.18, -0.18, 0.45 * i, 0.03, 0.03, 0.41, "white_plastic")]])
    prop("UpsideDownTable", "free", W, (3, 0, 0), [F(0, 0, 0, 1.1, 0.75, 0.04, "wood_light")] + legs4(1.1, 0.75, 0.7, 0.05, "wood_light", 0.03, 0.04))
    prop("FilingTower", "wall", W, (2, 0, 1), [F(0, 0, 0, 0.47, 0.62, 2.9, "metal")] + [F(0, 0.315, 0.06 + 0.32 * i, 0.43, 0.02, 0.28, "metal_dark") for i in range(9)] +
         [F(0, 0.33, 0.2 + 0.32 * i, 0.12, 0.03, 0.03, "brass") for i in range(9)])
    prop("TvStack", "wall", W, (2, 1, 1), [F(-0.25, 0, 0, 0.5, 0.45, 0.42, "black_plastic"), F(0.28, 0.02, 0, 0.5, 0.45, 0.42, "wood_dark"),
                                           F(0.0, 0, 0.42, 0.55, 0.45, 0.45, "beige_plastic", [0, 0, 5]), F(0.05, 0.01, 0.87, 0.45, 0.4, 0.38, "black_plastic", [0, 0, -7]),
                                           F(-0.25, 0.226, 0.06, 0.4, 0.01, 0.3, "screen_glow"), F(0.28, 0.246, 0.06, 0.4, 0.01, 0.3, "screen"),
                                           F(0.0, 0.226, 0.48, 0.45, 0.01, 0.32, "screen_glow"), F(0.05, 0.211, 0.92, 0.35, 0.01, 0.27, "screen_glow")])
    prop("PhoneOffHook", "small", W, (4, 1, 1), [F(0, 0, 0, 0.2, 0.22, 0.08, "beige_plastic"), F(0.25, 0.1, 0, 0.22, 0.06, 0.06, "beige_plastic", [0, 0, 40]),
                                                 F(0.12, 0.05, 0.02, 0.2, 0.01, 0.01, "black_plastic", [0, 0, 20])], collide=False)
    prop("PileOfClocks", "small", W, (3, 0, 0), [C(0.2 * x, 0.2 * y, 0.02 * i, 0.22, 0.05, "porcelain") for i, (x, y) in
                                                 enumerate(((0, 0), (1, 0.4), (-0.8, 0.6), (0.4, -0.9), (-0.5, -0.4), (0.2, 0.3)))], collide=False)
    prop("FloatingChair", "free", W, (2, 0, 0), (
        [F(0, 0, 1.25, 0.44, 0.42, 0.04, "wood_light", [0, 8, 0]), F(0, -0.19, 1.29, 0.42, 0.03, 0.42, "wood_light", [0, 8, 0])] +
        [F(sx * 0.18, sy * 0.17, 0.84, 0.03, 0.03, 0.41, "wood_light", [0, 8, 0]) for sx in (-1, 1) for sy in (-1, 1)]), collide=False)
    prop("ShoePile", "small", W, (3, 1, 1), [F(0.25 * ((i * 7) % 5 - 2) / 2, 0.25 * ((i * 3) % 5 - 2) / 2, 0.04 * (i % 3), 0.1, 0.27, 0.1,
                                               ["black_plastic", "fabric_brown", "white_plastic", "fabric_red"][i % 4], [0, 0, i * 37]) for i in range(12)],
         collide=False)
    prop("ToiletInTheOpen", "free", W, (2, 0, 1), [F(0, -0.25, 0.4, 0.42, 0.18, 0.4, "porcelain"), C(0, 0.05, 0, 0.32, 0.4, "porcelain"),
                                                   C(0, 0.05, 0.4, 0.38, 0.04, "porcelain"), F(0, -0.25, 0.8, 0.44, 0.2, 0.03, "porcelain")])
    prop("FallenExitSign", "small", W, (4, 2, 2), [F(0, 0, 0, 0.4, 0.2, 0.06, "black_plastic", [0, 0, 20]), F(0, 0, 0.06, 0.34, 0.15, 0.005, "fabric_green", [0, 0, 20])],
         collide=False)
    prop("PayphoneRow", "wall", W, (2, 2, 0), [p for x in (-0.5, 0, 0.5) for p in [F(x, -0.05, 0.95, 0.36, 0.2, 0.6, "metal"),
                                                                                  F(x - 0.12, 0.08, 1.05, 0.06, 0.06, 0.25, "black_plastic")]])
    prop("ElevatorDoors", "wall", W, (3, 1, 1), [F(-0.55, -0.07, 0, 0.12, 0.1, 2.2, "metal"), F(0.55, -0.07, 0, 0.12, 0.1, 2.2, "metal"),
                                                 F(0, -0.07, 2.15, 1.22, 0.1, 0.15, "metal"), F(-0.24, -0.09, 0, 0.48, 0.03, 2.15, "metal"),
                                                 F(0.24, -0.09, 0, 0.48, 0.03, 2.15, "metal"), F(0.75, -0.08, 1.0, 0.1, 0.03, 0.2, "metal_dark"),
                                                 C(0.75, -0.06, 1.1, 0.04, 0.01, "lamp_glow", [90, 0, 0])], collide=False)
    prop("FallenVendingMachine", "big", W, (2, 2, 0), [F(0, 0, 0, 0.9, 1.85, 0.8, "vending_red"), F(-0.1, 0.0, 0.8, 0.58, 1.15, 0.02, "vending_glow"),
                                                       F(0.3, 0.6, 0, 0.3, 0.2, 0.08, "red_paint", [0, 0, 20]), F(-0.4, 1.1, 0, 0.08, 0.08, 0.15, "blue_paint")])
    prop("TablePile", "free", W, (2, 0, 0), table(1.0, 0.7, 0.75, "wood_light", "wood_light") + [
        F(0, 0, 0.75, 1.0, 0.7, 0.04, "wood_dark", [0, 0, 25]), F(0.1, 0, 0.79, 0.04, 0.04, 0.7, "wood_dark", [80, 0, 25])])
    prop("SwingSet", "big", W, (1, 0, 0), [F(0, 0, 2.2, 2.4, 0.08, 0.08, "red_paint")] + [F(sx * 1.15, sy * 0.5, 0, 0.06, 0.06, 2.3, "red_paint", [sy * 12, 0, 0])
                                                                                       for sx in (-1, 1) for sy in (-1, 1)] +
         [p for x in (-0.5, 0.5) for p in [F(x - 0.2, 0, 0.45, 0.01, 0.01, 1.75, "metal"), F(x + 0.2, 0, 0.45, 0.01, 0.01, 1.75, "metal"),
                                           F(x, 0, 0.42, 0.45, 0.18, 0.04, "black_plastic")]])
    prop("PlaygroundSlide", "big", W, (1, 0, 0), [F(0, -1.0, 1.5, 0.7, 0.7, 0.06, "red_paint"),
                                                  F(0, 0.3, 0.6, 0.55, 2.0, 0.04, "yellow", [35, 0, 0])] +
         [F(sx * 0.32, -1.0 + sy * 0.32, 0, 0.06, 0.06, 1.5, "red_paint") for sx in (-1, 1) for sy in (-1, 1)] +
         [F(0, -1.35 - 0.0, 0.25 * i, 0.6, 0.05, 0.04, "metal") for i in range(1, 6)])
