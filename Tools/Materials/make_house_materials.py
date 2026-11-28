"""Level 9's houses: their surfaces from photo-scanned CC0 sets (Poly Haven), for Tools/Levels/make_houses.py.

Painted lap siding in four old paint colours (weathered_plank_siding, repainted), drywall (beige_wall_001), an old
wood floor (old_wood_floor), carpet (dirty_carpet), kitchen and bathroom tile (floor_tiles_02), asphalt shingles
(grey_roof_01), concrete (concrete_floor), the front door (rough_pine_door), the garage door (wooden_garage_door),
plus white trim and window glass.

The models carry their UVs in metres (one texture repeat = TILE metres, set in make_houses.py), so every material
here tiles once per UV unit and nothing stretches.

usage: python3 Tools/Materials/make_house_materials.py POLYHAVEN_DIR
Writes Assets/Textures/Suburb/house_*.png and Assets/Materials/Suburb/house_*.material.
"""
import json
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join(ROOT, 'Assets', 'Textures', 'Suburb')
MAT = os.path.join(ROOT, 'Assets', 'Materials', 'Suburb')
SIZE = (1024, 1024)

# The siding's old paint: off-white, cream, faded blue, sage.
PAINTS = {
    'white': (0.80, 0.79, 0.75),
    'cream': (0.78, 0.71, 0.56),
    'blue': (0.52, 0.60, 0.66),
    'sage': (0.58, 0.62, 0.53),
}


def load(path, mode='RGB'):
    im = Image.open(path).convert(mode)
    if im.size != SIZE:
        im = im.resize(SIZE, Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def save(arr, name, mode='RGB'):
    out = (np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, mode).save(os.path.join(TEX, name), optimize=True)


def material(name, maps, extra=None):
    props = {'metallic.factor': 0.0}
    if 'basecolor' in maps:
        props['baseColor.textureMap'] = f'../../Textures/Suburb/{name}_basecolor.png'
    if 'normal' in maps:
        props['normal.textureMap'] = f'../../Textures/Suburb/{name}_normal.png'
        props['normal.factor'] = 1.0
    if 'roughness' in maps:
        props['roughness.textureMap'] = f'../../Textures/Suburb/{name}_roughness.png'
    if 'ao' in maps:
        props['occlusion.diffuseTextureMap'] = f'../../Textures/Suburb/{name}_ao.png'
        props['occlusion.diffuseFactor'] = 1.0
    props.update(extra or {})
    data = {
        'materialType': '@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype',
        'materialTypeVersion': 5,
        'propertyValues': props,
    }
    json.dump(data, open(os.path.join(MAT, f'{name}.material'), 'w'), indent=4)
    print('wrote', f'{name}.material')


def scan(ph, src, name, tint=None, rough_scale=1.0, rough_add=0.0, base_scale=1.0):
    """One scanned set as it is (or tinted), at 1024."""
    base = load(os.path.join(ph, f'{src}_diff_2k.png')) * base_scale
    if tint is not None:
        lum = base @ np.array([0.299, 0.587, 0.114], np.float32)
        lum = lum / max(1e-4, float(lum.mean()))
        base = np.array(tint, np.float32)[None, None, :] * np.clip(0.62 + 0.38 * lum, 0, 1.4)[..., None]
    save(base, f'{name}_basecolor.png')
    save(load(os.path.join(ph, f'{src}_nor_gl_2k.png')), f'{name}_normal.png')
    rough = load(os.path.join(ph, f'{src}_rough_2k.png'), 'L')
    save(np.clip(rough * rough_scale + rough_add, 0.04, 1), f'{name}_roughness.png', 'L')
    maps = ['basecolor', 'normal', 'roughness']
    ao = os.path.join(ph, f'{src}_ao_2k.png')
    if os.path.exists(ao):
        save(load(ao, 'L'), f'{name}_ao.png', 'L')
        maps.append('ao')
    return maps


def main():
    ph = sys.argv[1]
    os.makedirs(TEX, exist_ok=True)
    os.makedirs(MAT, exist_ok=True)

    # Lap siding: the scan's planks and grain, repainted; the paint a little glossier than raw wood.
    for paint, colour in PAINTS.items():
        maps = scan(ph, 'weathered_plank_siding', f'house_siding_{paint}', tint=colour, rough_scale=0.8, rough_add=0.1)
        material(f'house_siding_{paint}', maps)

    material('house_drywall', scan(ph, 'beige_wall_001', 'house_drywall', base_scale=1.05, rough_scale=0.9, rough_add=0.05))
    material('house_wood_floor', scan(ph, 'old_wood_floor', 'house_wood_floor', rough_scale=0.85))
    material('house_carpet', scan(ph, 'dirty_carpet', 'house_carpet', rough_add=0.1))
    material('house_tile', scan(ph, 'floor_tiles_02', 'house_tile', base_scale=0.95, rough_scale=0.7))
    material('house_roof', scan(ph, 'grey_roof_01', 'house_roof', base_scale=0.8))
    material('house_concrete', scan(ph, 'concrete_floor', 'house_concrete', base_scale=0.8))
    material('house_front_door', scan(ph, 'rough_pine_door', 'house_front_door', base_scale=0.85))
    material('house_garage_door', scan(ph, 'wooden_garage_door', 'house_garage_door', tint=(0.74, 0.73, 0.69),
                                       rough_scale=0.8, rough_add=0.1))
    # Trim (window and door casings, fascia, soffits): flat white paint, a little dirty.
    material('house_trim', [], {'baseColor.color': [0.78, 0.77, 0.73, 1.0], 'baseColor.factor': 1.0,
                                'roughness.factor': 0.55, 'specularF0.factor': 0.5})
    # Glass: dark, glossy, mostly see-through, so a dark room shows behind it.
    material('house_glass', [], {'baseColor.color': [0.05, 0.06, 0.07, 1.0], 'roughness.factor': 0.04,
                                 'specularF0.factor': 0.6, 'opacity.mode': 'Blended', 'opacity.factor': 0.35,
                                 'general.doubleSided': True})


if __name__ == '__main__':
    main()
