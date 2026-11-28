#!/usr/bin/env python3
"""
The weapons' assets (approved PlayerWeapons mockup, hearth-workspace noclip-protocol/mockups/ui-and-hearth-engine):

    python3 Tools/Weapons/make_weapons.py        (run from the project folder, after Tools/Base/make_base_pieces.py)

Writes
  Prefabs/Weapons/View_<Name>.prefab   what you hold: the service pistol, pump shotgun, carbine, flare gun, combat knife
                                       and riot shield, built from boxes and cylinders at real size (stand-ins until the
                                       real models). Origin at the grip, the muzzle toward +Y, up +Z.
  Assets/Materials/Weapons/*.material  gunmetal, grip polymer, wood, the flare gun's orange, the shield's polycarbonate
  Assets/Sounds/Weapons/*.wav          shots, dry click, magazine, shell, pump, knife, impact (16-bit mono)
  Prefabs/Weapons/WeaponSounds.prefab  one MiniAudio player per sound (not spatial: they're your own weapon's)

The game spawns the view prefab of the weapon in your hands and the sounds once per level (Player/WeaponsSystemComponent).
"""

import copy
import hashlib
import json
import os
import random
import sys
import uuid
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Clutter"))
import make_clutter as mc  # noqa: E402

path = mc.path
asset_guid = mc.asset_guid
B, C = mc.B, mc.C

MATERIALS = {
    #  name              base color             metal rough  emissive
    "wpn_gunmetal":     ([0.035, 0.036, 0.038], 0.85, 0.38, None),
    "wpn_polymer":      ([0.018, 0.018, 0.017], 0.0, 0.7, None),
    "wpn_wood":         ([0.20, 0.10, 0.045], 0.0, 0.55, None),
    "wpn_olive":        ([0.05, 0.06, 0.03], 0.2, 0.6, None),
    "wpn_orange":       ([0.45, 0.09, 0.008], 0.0, 0.5, None),     # linear: a signal orange under Level 0's lights
    "wpn_flare_glow":   ([0.90, 0.10, 0.04], 0.0, 0.4, ([1.0, 0.15, 0.06], 12.0)),
    "wpn_steel":        ([0.50, 0.50, 0.50], 1.0, 0.25, None),
    "wpn_shield":       ([0.10, 0.11, 0.12], 0.0, 0.15, None),
    "wpn_sight":        ([0.20, 0.90, 0.40], 0.0, 0.3, ([0.3, 1.0, 0.5], 2.0)),
    "wpn_lens":         ([0.90, 0.92, 0.85], 0.0, 0.1, ([1.0, 0.97, 0.88], 4.0)),   # the flashlight's lens (Tools/Weapons/lights.py)
}


def mat_guid(name):
    if name in MATERIALS:
        return asset_guid(f"Assets/Materials/Weapons/{name}.material")
    return mc.material_guid(name)


def write_materials():
    folder = path("Assets/Materials/Weapons")
    os.makedirs(folder, exist_ok=True)
    for name, (color, metal, rough, emissive) in MATERIALS.items():
        props = {
            "baseColor": {"color": color + [1.0]},
            "metallic": {"factor": metal},
            "roughness": {"factor": rough},
        }
        if emissive:
            props["emissive"] = {"enable": True, "color": emissive[0] + [1.0], "intensity": emissive[1]}
        data = {"materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype",
                "materialTypeVersion": 5, "propertyValues": flatten(props)}
        with open(os.path.join(folder, f"{name}.material"), "w") as f:
            json.dump(data, f, indent=4)


def flatten(props):
    out = {}
    for group, values in props.items():
        for key, value in values.items():
            out[f"{group}.{key}"] = value
    return out


# ------------------------------------------------------------------------------------------------ the guns (metres)
# B(x, y, z, sx, sy, sz, mat): a box by its centre and size; C(x, y, z0, d, h, mat, rot): a cylinder ([90, 0, 0] lies along Y).
ALONG_Y = [90, 0, 0]

VIEWS = {
    # A full-size 9 mm service pistol: 19 cm long, 3 cm wide.
    "Pistol": [
        B(0, 0.055, 0.045, 0.030, 0.190, 0.034, "wpn_gunmetal"),          # slide
        B(0, 0.045, 0.019, 0.028, 0.160, 0.020, "wpn_polymer"),           # frame
        B(0, -0.010, -0.040, 0.028, 0.050, 0.110, "wpn_polymer", [-14, 0, 0]),  # grip
        B(0, 0.030, -0.006, 0.010, 0.040, 0.010, "wpn_polymer"),          # trigger guard
        B(0, 0.140, 0.064, 0.012, 0.010, 0.010, "wpn_sight"),             # front sight
        B(0, -0.030, 0.066, 0.022, 0.012, 0.010, "wpn_gunmetal"),          # rear sight
    ],
    # A pump shotgun: about 1 m, held at the grip, the forend under the barrel.
    "Shotgun": [
        C(0, 0.18, 0.040, 0.026, 0.62, "wpn_gunmetal", ALONG_Y),          # barrel (centre at y 0.18 + ...)
        C(0, 0.16, 0.012, 0.030, 0.50, "wpn_gunmetal", ALONG_Y),          # magazine tube
        B(0, 0.20, 0.014, 0.046, 0.200, 0.050, "wpn_wood"),               # forend (the pump)
        B(0, -0.06, 0.026, 0.048, 0.220, 0.070, "wpn_gunmetal"),          # receiver
        B(0, -0.10, -0.040, 0.034, 0.050, 0.100, "wpn_wood", [-18, 0, 0]),  # pistol grip
        B(0, -0.32, 0.000, 0.044, 0.300, 0.080, "wpn_wood", [6, 0, 0]),     # stock
        B(0, 0.48, 0.060, 0.010, 0.010, 0.012, "wpn_steel"),               # bead sight
    ],
    # A short carbine: magazine, rail, red-dot sight.
    "Carbine": [
        C(0, 0.25, 0.040, 0.020, 0.40, "wpn_gunmetal", ALONG_Y),          # barrel
        B(0, 0.20, 0.034, 0.050, 0.260, 0.060, "wpn_olive"),              # handguard
        B(0, -0.04, 0.034, 0.050, 0.240, 0.070, "wpn_gunmetal"),          # receiver
        B(0, 0.02, -0.050, 0.026, 0.050, 0.140, "wpn_polymer", [12, 0, 0]),  # magazine
        B(0, -0.10, -0.045, 0.030, 0.045, 0.100, "wpn_polymer", [-18, 0, 0]),  # pistol grip
        B(0, -0.33, 0.020, 0.040, 0.200, 0.075, "wpn_olive"),             # stock
        B(0, -0.02, 0.092, 0.036, 0.080, 0.040, "wpn_polymer"),           # red dot body
        B(0, -0.02, 0.096, 0.012, 0.004 + 0.010, 0.012, "wpn_sight"),     # the dot
    ],
    # A 12-gauge orange flare gun.
    "FlareGun": [
        C(0, 0.06, 0.040, 0.044, 0.200, "wpn_orange", ALONG_Y),           # barrel
        B(0, -0.03, 0.040, 0.036, 0.060, 0.050, "wpn_orange"),            # breech
        B(0, -0.035, -0.030, 0.030, 0.050, 0.100, "wpn_orange", [-16, 0, 0]),  # grip
        B(0, 0.005, -0.004, 0.010, 0.040, 0.010, "wpn_polymer"),          # trigger guard
    ],
    # A combat knife: 30 cm, held blade forward.
    "Knife": [
        B(0, 0.110, 0.010, 0.004 + 0.010, 0.170, 0.032, "wpn_steel"),     # blade
        B(0, 0.020, 0.010, 0.020, 0.012, 0.050, "wpn_gunmetal"),          # guard
        B(0, -0.045, 0.010, 0.024, 0.115, 0.028, "wpn_polymer"),          # handle
    ],
    # A riot shield: 90 x 50 cm, held at its back handle, the pane in front of you.
    "Shield": [
        B(0, 0.10, 0.05, 0.50, 0.012, 0.90, "wpn_shield"),                # pane (faces +Y)
        B(0, 0.10, 0.36, 0.30, 0.014, 0.10, "wpn_olive"),                 # "POLICE"-less band
        B(0, 0.04, 0.0, 0.03, 0.10, 0.12, "wpn_polymer"),                 # handle
    ],
}


def write_flare():
    """A burning road flare / flare-gun flare: a red glowing stick and a red point light (no sound, no shape)."""
    with open(path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    parts = [C(0, 0, 0.015, 0.03, 0.20, "wpn_flare_glow", ALONG_Y), C(0, 0.11, 0.015, 0.032, 0.03, "wpn_polymer", ALONG_Y)]
    mc.build_prefab("Flare", {"parts": parts, "size": mc.bounds(parts), "collide": False}, template, folder="Prefabs/Weapons", mat_guid=mat_guid)
    with open(path("Prefabs/Weapons", "Flare.prefab")) as f:
        data = json.load(f)
    with open(path("Prefabs/Base", "Build_Flare.prefab")) as f:
        base = json.load(f)
    bulb = copy.deepcopy(next(e for e in base["Entities"].values() if e["Name"].endswith("Bulb")))
    rnd = random.Random("FlareLight")
    eid = f"Entity_[{rnd.getrandbits(40)}]"
    bulb["Id"] = eid
    bulb["Name"] = "Flare_Light"
    comps = bulb["Components"]
    for key in [k for k, c in comps.items() if "MiniAudio" in c.get("$type", "") or "Shape" in c.get("$type", "")]:
        del comps[key]
    for comp in comps.values():
        comp["Id"] = rnd.getrandbits(63)
    light = next(c for c in comps.values() if "AreaLight" in c.get("$type", ""))
    light["Controller"]["Configuration"] = {"LightType": 6, "Color": [1.0, 0.22, 0.08], "Intensity": 420.0,
                                            "AttenuationRadiusMode": 0, "AttenuationRadius": 9.0}
    comps["TransformComponent"]["Parent Entity"] = "ContainerEntity"
    comps["TransformComponent"]["Transform Data"] = {"Translate": [0.0, 0.0, 0.25]}
    data["Entities"][eid] = bulb
    data["ContainerEntity"]["Components"]["EditorEntitySortComponent"]["Child Entity Order"].append(eid)
    with open(path("Prefabs/Weapons", "Flare.prefab"), "w") as f:
        json.dump(data, f, indent=4)


# The real models (Tools/Weapons/make_weapon_models.py, from Tripo3D) replace the primitives once the Asset
# Processor has made them: their ids are in Tools/Weapons/model_ids.json (Tools/Assets/asset_ids.py writes it).
MODEL_IDS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model_ids.json")


def model_view(name, template):
    """View_<name>.prefab holding one mesh: Assets/Models/Weapons/<name>/<name>.fbx with its own material."""
    try:
        with open(MODEL_IDS) as f:
            ids = json.load(f)
    except OSError:
        return False
    key = f"assets/models/weapons/{name.lower()}/{name.lower()}.fbx.azmodel"
    if key not in ids:
        return False
    rnd = random.Random(f"View_{name}-model")
    data = copy.deepcopy(template)
    data["ContainerEntity"]["Name"] = f"View_{name}"
    for comp in data["ContainerEntity"]["Components"].values():
        comp["Id"] = rnd.getrandbits(63)
    e = copy.deepcopy(next(iter(template["Entities"].values())))
    eid = f"Entity_[{rnd.getrandbits(40)}]"
    e["Id"], e["Name"] = eid, f"View_{name}_Model"
    comps = e["Components"]
    for comp in comps.values():
        comp["Id"] = rnd.getrandbits(63)
    for k in [k for k, c in comps.items() if "Collider" in c.get("$type", "") or "RigidBody" in c.get("$type", "")]:
        del comps[k]
    comps["AZ::Render::EditorMeshComponent"]["Controller"]["Configuration"]["ModelAsset"] = {
        "assetId": {"guid": ids[key]["guid"], "subId": ids[key]["subId"]}, "assetHint": key}
    comps["EditorMaterialComponent"]["Controller"]["Configuration"]["materials"]["{}"]["MaterialAsset"]["assetId"]["guid"] = \
        asset_guid(f"Assets/Models/Weapons/{name}/{name}.material")
    comps["EditorNonUniformScaleComponent"]["NonUniformScale"] = [1.0, 1.0, 1.0]
    comps["TransformComponent"]["Transform Data"] = {"Translate": [0.0, 0.0, 0.0]}
    comps["TransformComponent"]["Parent Entity"] = "ContainerEntity"
    data["Entities"] = {eid: e}
    data["ContainerEntity"]["Components"]["EditorEntitySortComponent"]["Child Entity Order"] = [eid]
    with open(path("Prefabs/Weapons", f"View_{name}.prefab"), "w") as f:
        json.dump(data, f, indent=4)
    return True


def write_views():
    with open(path("Prefabs/Level0", "L0_Pillar.prefab")) as f:
        template = json.load(f)
    os.makedirs(path("Prefabs/Weapons"), exist_ok=True)
    for name, parts in VIEWS.items():
        if model_view(name, template):
            continue
        spec = {"parts": parts, "size": mc.bounds(parts), "collide": False}
        mc.build_prefab(f"View_{name}", spec, template, folder="Prefabs/Weapons", mat_guid=mat_guid)


# ------------------------------------------------------------------------------------------------ the sounds
RATE = 44100


def env(n, attack, decay):
    t = np.arange(n) / RATE
    return np.minimum(1.0, t / max(attack, 1e-4)) * np.exp(-t / decay)


def noise(n, seed, lo=None, hi=None):
    rng = np.random.default_rng(seed)
    x = rng.standard_normal(n)
    if lo or hi:
        spec = np.fft.rfft(x)
        f = np.fft.rfftfreq(n, 1.0 / RATE)
        mask = np.ones_like(f)
        if lo:
            mask *= 1.0 / (1.0 + (lo / np.maximum(f, 1.0)) ** 2)
        if hi:
            mask *= 1.0 / (1.0 + (f / hi) ** 2)
        x = np.fft.irfft(spec * mask, n)
    return x / (np.abs(x).max() + 1e-9)


def tail(n, decay, seed):
    """The room answering: a dark, decaying reverb tail (the Backrooms are carpet and ceiling tile)."""
    return noise(n, seed, 120, 1800) * env(n, 0.002, decay)


def shot(seconds, crack_decay, body_hz, body_decay, room, seed, crack_hi=9000):
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    crack = noise(n, seed, 300, crack_hi) * env(n, 0.0004, crack_decay)
    body = np.sin(2 * np.pi * body_hz * t * (1 - 0.35 * t)) * env(n, 0.001, body_decay)
    out = crack + 0.9 * body + room * tail(n, 0.35, seed + 1)
    return np.tanh(out * 2.2)


def click(n, seed, hz=3200, decay=0.004, gain=1.0):
    t = np.arange(n) / RATE
    return gain * (noise(n, seed, 1500, 12000) * 0.6 + np.sin(2 * np.pi * hz * t)) * env(n, 0.0002, decay)


def at(total, part, start):
    out = np.zeros(total)
    s = int(start * RATE)
    m = min(len(part), total - s)
    out[s:s + m] += part[:m]
    return out


def sounds():
    out = {}
    out["pistol_shot"] = shot(0.9, 0.012, 140, 0.05, 0.5, 1)
    out["shotgun_shot"] = shot(1.3, 0.025, 80, 0.10, 0.8, 2, crack_hi=6000)
    out["carbine_shot"] = shot(0.7, 0.010, 170, 0.04, 0.45, 3, crack_hi=11000)
    n = int(RATE * 1.0)
    whoosh = noise(n, 4, 200, 3000) * env(n, 0.01, 0.25)
    out["flare_shot"] = np.tanh((shot(1.0, 0.008, 110, 0.04, 0.3, 4) * 0.7 + whoosh * 0.8) * 1.5)
    out["dry_click"] = click(int(RATE * 0.15), 5, 2600, 0.006)
    n = int(RATE * 1.1)
    out["reload_mag"] = (at(n, click(int(RATE * 0.1), 6, 1800, 0.01), 0.05) + at(n, click(int(RATE * 0.1), 7, 2200, 0.008), 0.55)
                         + at(n, click(int(RATE * 0.12), 8, 1400, 0.015, 1.3), 0.85))
    out["shell_in"] = at(int(RATE * 0.35), click(int(RATE * 0.12), 9, 1200, 0.02) + 0.5 * click(int(RATE * 0.12), 10, 3000, 0.004), 0.02)
    n = int(RATE * 0.6)
    out["pump"] = at(n, click(int(RATE * 0.15), 11, 900, 0.03, 1.2), 0.02) + at(n, click(int(RATE * 0.15), 12, 1300, 0.02, 1.4), 0.28)
    n = int(RATE * 0.35)
    t = np.arange(n) / RATE
    out["knife_swish"] = noise(n, 13, 1500, 7000) * np.sin(np.pi * np.clip(t / 0.25, 0, 1)) ** 2
    out["knife_hit"] = np.tanh((noise(int(RATE * 0.3), 14, 200, 2500) * env(int(RATE * 0.3), 0.001, 0.04)
                                + 0.5 * np.sin(2 * np.pi * 180 * np.arange(int(RATE * 0.3)) / RATE) * env(int(RATE * 0.3), 0.001, 0.05)) * 2)
    out["impact"] = noise(int(RATE * 0.25), 15, 400, 5000) * env(int(RATE * 0.25), 0.0005, 0.03)
    n = int(RATE * 0.4)
    out["flare_strike"] = at(n, click(int(RATE * 0.05), 16, 2000, 0.006), 0.0) + noise(n, 17, 800, 6000) * env(n, 0.02, 0.2) * 0.8
    return out


def write_wav(file, samples, peak):
    samples = samples / (np.abs(samples).max() + 1e-9) * peak
    data = (samples * 32767).astype("<i2")
    with wave.open(file, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


VOLUMES = {"pistol_shot": 85, "shotgun_shot": 95, "carbine_shot": 80, "flare_shot": 70, "dry_click": 50, "reload_mag": 60,
           "shell_in": 55, "pump": 65, "knife_swish": 45, "knife_hit": 60, "impact": 35, "flare_strike": 55}


def source_guid(rel):
    """The Asset Processor's id for a source file: SHA-1 name UUID of its lowercased path (matches the level's own sounds)."""
    h = bytearray(hashlib.sha1(rel.lower().encode()).digest()[:16])
    h[6] = (h[6] & 0x0F) | 0x50
    h[8] = (h[8] & 0x3F) | 0x80
    return "{" + str(uuid.UUID(bytes=bytes(h))).upper() + "}"


def write_sounds():
    folder = path("Assets/Sounds/Weapons")
    os.makedirs(folder, exist_ok=True)
    for name, samples in sounds().items():
        write_wav(os.path.join(folder, f"{name}.wav"), samples, 0.9)
    # The players: copied from Build_Flare's bulb (a MiniAudio Playback on a plain entity), one per sound.
    with open(path("Prefabs/Base", "Build_Flare.prefab")) as f:
        template = json.load(f)
    rnd = random.Random("WeaponSounds")
    data = copy.deepcopy(template)
    data["ContainerEntity"]["Name"] = "WeaponSounds"
    for comp in data["ContainerEntity"]["Components"].values():
        comp["Id"] = rnd.getrandbits(63)
    bulb = next(e for e in template["Entities"].values() if e["Name"].endswith("Bulb"))
    entities, order = {}, []
    for name in VOLUMES:
        eid = f"Entity_[{rnd.getrandbits(40)}]"
        e = copy.deepcopy(bulb)
        e["Id"] = eid
        e["Name"] = f"Sound_{name}"
        comps = e["Components"]
        for key in [k for k, c in comps.items() if "AreaLight" in c.get("$type", "") or "Shape" in c.get("$type", "")]:
            del comps[key]
        for comp in comps.values():
            comp["Id"] = rnd.getrandbits(63)
        comps["TransformComponent"]["Parent Entity"] = "ContainerEntity"
        comps["TransformComponent"]["Transform Data"] = {"Translate": [0.0, 0.0, 0.0]}
        rel = f"Assets/Sounds/Weapons/{name}.wav"
        comps["EditorMiniAudioPlaybackComponent"]["Controller"]["Config"] = {
            "Autoplay": False,
            "Sound": {"assetId": {"guid": source_guid(rel), "subId": 1}, "assetHint": rel.lower() + ".miniaudio"},
            "Volume": float(VOLUMES[name]),
            "Loop": False,
            "Spatialization": False,
            "Auto-follow": False,
        }
        entities[eid] = e
        order.append(eid)
    data["Entities"] = entities
    data["ContainerEntity"]["Components"]["EditorEntitySortComponent"]["Child Entity Order"] = order
    os.makedirs(path("Prefabs/Weapons"), exist_ok=True)
    with open(path("Prefabs/Weapons", "WeaponSounds.prefab"), "w") as f:
        json.dump(data, f, indent=4)


def main():
    write_materials()
    write_views()
    write_flare()
    write_sounds()
    print(f"wrote {len(VIEWS)} weapon views, {len(MATERIALS)} materials, {len(VOLUMES)} sounds and the sound players")


if __name__ == "__main__":
    main()
