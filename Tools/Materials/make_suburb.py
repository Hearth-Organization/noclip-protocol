"""Level 9's street materials from photo-scanned CC0 sets (Poly Haven): wet dark asphalt with no lane paint
(asphalt_02, darkened, and glossy where the rain lies), sidewalk slabs (concrete_pavement_02) and lawn (leafy_grass).
The wiki: "wet, dark asphalt", puddles, ordinary stone sidewalks, grass past the walkways.

usage: python3 Tools/Materials/make_suburb.py POLYHAVEN_DIR
Writes Assets/Textures/Outdoor/suburb_*.png and Assets/Materials/Outdoor/suburb_{road,sidewalk,lawn}.material.
"""
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join(ROOT, 'Assets', 'Textures', 'Outdoor')
MAT = os.path.join(ROOT, 'Assets', 'Materials', 'Outdoor')
SIZE = (2048, 2048)


def load(path, mode='RGB'):
    im = Image.open(path).convert(mode)
    if im.size != SIZE:
        im = im.resize(SIZE, Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def save(arr, name, mode='RGB'):
    out = (np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, mode).save(os.path.join(TEX, name), optimize=True)
    print('wrote', name)


def material(name, tile, extra=None):
    props = {
        'baseColor.textureMap': f'../../Textures/Outdoor/{name}_basecolor.png',
        'normal.textureMap': f'../../Textures/Outdoor/{name}_normal.png',
        'normal.factor': 1.0,
        'roughness.textureMap': f'../../Textures/Outdoor/{name}_roughness.png',
        'occlusion.diffuseTextureMap': f'../../Textures/Outdoor/{name}_ao.png',
        'occlusion.diffuseFactor': 1.0,
        'metallic.factor': 0.0,
        'uv.tileU': tile,
        'uv.tileV': tile,
    }
    props.update(extra or {})
    data = {
        'materialType': '@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype',
        'materialTypeVersion': 5,
        'propertyValues': props,
    }
    json.dump(data, open(os.path.join(MAT, f'{name}.material'), 'w'), indent=4)
    print('wrote', f'{name}.material')


def main():
    ph = sys.argv[1]
    os.makedirs(TEX, exist_ok=True)
    os.makedirs(MAT, exist_ok=True)

    # Road: the scan darkened to wet asphalt; water lies in the low spots (from the height map), mirror-smooth there.
    base = load(os.path.join(ph, 'asphalt_02_diff_2k.png'))
    height = load(os.path.join(ph, 'asphalt_02_disp_2k.png'), 'L')
    rough = load(os.path.join(ph, 'asphalt_02_rough_2k.png'), 'L')
    low = gaussian_filter(height, 24, mode='wrap')
    wet = np.clip((np.percentile(low, 35) - low) / 0.04, 0, 1)                   # puddles where the road dips
    damp = 0.55                                                                    # the rest still wet from the rain
    save(base * (0.42 - 0.12 * wet[..., None]), 'suburb_road_basecolor.png')
    save(np.clip(rough * (1 - damp) * 0.9 + 0.12, 0.05, 1) * (1 - wet) + 0.04 * wet, 'suburb_road_roughness.png', 'L')
    nor = load(os.path.join(ph, 'asphalt_02_nor_gl_2k.png'))
    flat = np.zeros_like(nor); flat[..., 0] = 0.5; flat[..., 1] = 0.5; flat[..., 2] = 1.0
    save(nor * (1 - wet[..., None]) + flat * wet[..., None], 'suburb_road_normal.png')            # still water is flat
    save(load(os.path.join(ph, 'asphalt_02_ao_2k.png'), 'L'), 'suburb_road_ao.png', 'L')
    material('suburb_road', 4.0)

    # Sidewalk: one slab per box (1.7 m), a little darker from the damp.
    save(load(os.path.join(ph, 'concrete_pavement_02_diff_2k.png')) * 0.75, 'suburb_sidewalk_basecolor.png')
    save(load(os.path.join(ph, 'concrete_pavement_02_nor_gl_2k.png')), 'suburb_sidewalk_normal.png')
    save(np.clip(load(os.path.join(ph, 'concrete_pavement_02_rough_2k.png'), 'L') * 0.8 + 0.1, 0, 1), 'suburb_sidewalk_roughness.png', 'L')
    save(load(os.path.join(ph, 'concrete_pavement_02_ao_2k.png'), 'L'), 'suburb_sidewalk_ao.png', 'L')
    material('suburb_sidewalk', 1.0)

    # Lawn: the scan's leafy grass, a touch less saturated (it's night and the grass is half dead).
    g = load(os.path.join(ph, 'leafy_grass_diff_2k.png'))
    grey = g.mean(axis=-1, keepdims=True)
    save((grey + (g - grey) * 0.7) * 0.8, 'suburb_lawn_basecolor.png')
    save(load(os.path.join(ph, 'leafy_grass_nor_gl_2k.png')), 'suburb_lawn_normal.png')
    save(load(os.path.join(ph, 'leafy_grass_rough_2k.png'), 'L'), 'suburb_lawn_roughness.png', 'L')
    save(load(os.path.join(ph, 'leafy_grass_ao_2k.png'), 'L'), 'suburb_lawn_ao.png', 'L')
    material('suburb_lawn', 30.0)


if __name__ == '__main__':
    main()
