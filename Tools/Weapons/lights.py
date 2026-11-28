"""Where the flashlight is on each weapon (owner, Nov 27: "on most of the weapons we can have the flash light mounted
to the side of it and for things like pistols we can have them holding the light beside the pistol with the spare
hand"). Weapon space (x right, y ahead, z up), the middle of the light's body; the light points +Y, its lens at
y + LENS. HELD: in the left fist (Tools/Agents/make_arms.py closes the hand round it); else on a rail mount.
Gem/Source/Player/WeaponsComponent.h has the same table (Lens()) for where the beam starts: keep them in step.
"""
LENS = 0.085
LIGHTS = {
    "Pistol":   ((-0.060, 0.015, -0.080), True),
    "FlareGun": ((-0.065, -0.005, -0.080), True),
    "Knife":    ((-0.200, 0.030, -0.050), True),
    "Shotgun":  ((0.042, 0.240, 0.014), False),
    "Carbine":  ((0.042, 0.240, 0.034), False),
    "Shield":   ((0.180, 0.100, 0.520), False),
}


def parts(weapon, B, C):
    """The light as primitive parts in weapon space: body, head, glowing lens, and a mount for a rail light."""
    (x, y, z), held = LIGHTS[weapon]
    along = [90, 0, 0]
    p = [C(x, y, z, 0.026, 0.14, "wpn_gunmetal", along),
         C(x, y + 0.072, z, 0.034, 0.03, "wpn_gunmetal", along),
         C(x, y + LENS, z, 0.029, 0.004, "wpn_lens", along),
         C(x, y - 0.03, z, 0.028, 0.012, "wpn_polymer", along)]          # grip ring / tail cap
    if not held:
        side = -1.0 if x > 0 else 1.0
        p.append(B(x + side * 0.018, y, z, 0.014, 0.05, 0.016, "wpn_polymer"))   # the rail mount
    return p
