"""Level 1's photoreal materials (the Habitable Zone, a concrete parking garage): photo-scanned CC0 concrete from
Poly Haven, brought to the level's own cold grey so it still reads as Level 1.

usage: python3 Tools/Materials/make_photoreal_level1.py POLYHAVEN_DIR
  POLYHAVEN_DIR holds the 2k PNG sets of garage_floor, concrete_wall_008, concrete, concrete_wall_006
  (diffuse, nor_gl, rough, ao, disp; api.polyhaven.com/files/<name>).
The hand-made originals the colours are matched to are kept in Tools/Materials/level1_originals/.
"""
import json
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join(ROOT, 'Assets', 'Textures', 'Level1')
MAT = os.path.join(ROOT, 'Assets', 'Materials', 'Level1')
ORIG = os.path.join(ROOT, 'Tools', 'Materials', 'level1_originals')
SIZE = (2048, 2048)


def load(path, size=SIZE, mode='RGB'):
    im = Image.open(path).convert(mode)
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def save(arr, name, mode='RGB'):
    out = (np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, mode).save(os.path.join(TEX, name), optimize=True)
    print('wrote', name)


def recolour(scan, target_rgb, saturation):
    """The scan's detail in the level's colour: partly desaturated, its average brought to the original's."""
    grey = scan.mean(axis=-1, keepdims=True)
    s = grey + (scan - grey) * saturation
    return s * (target_rgb / (s.reshape(-1, 3).mean(axis=0) + 1e-4))


def original(name):
    keep = os.path.join(ORIG, f'level1_{name}_basecolor.png')
    if not os.path.exists(keep):
        os.makedirs(ORIG, exist_ok=True)
        os.rename(os.path.join(TEX, f'level1_{name}_basecolor.png'), keep)
    return load(keep)


def maps(ph, scan, surface, rough_lo, rough_hi):
    save(load(os.path.join(ph, f'{scan}_nor_gl_2k.png')), f'level1_{surface}_normal.png')
    rough = load(os.path.join(ph, f'{scan}_rough_2k.png'), mode='L')
    save(rough_lo + rough * (rough_hi - rough_lo), f'level1_{surface}_roughness.png', 'L')
    save(load(os.path.join(ph, f'{scan}_ao_2k.png'), mode='L'), f'level1_{surface}_ao.png', 'L')


def material(surface, extra):
    path = os.path.join(MAT, f'level1_{surface}.material')
    data = json.load(open(path))
    p = data['propertyValues']
    p.pop('roughness.factor', None)
    p.update({
        'baseColor.textureMap': f'../../Textures/Level1/level1_{surface}_basecolor.png',
        'normal.textureMap': f'../../Textures/Level1/level1_{surface}_normal.png',
        'normal.factor': 1.0,
        'roughness.textureMap': f'../../Textures/Level1/level1_{surface}_roughness.png',
        'occlusion.diffuseTextureMap': f'../../Textures/Level1/level1_{surface}_ao.png',
        'occlusion.diffuseFactor': 0.95,
    })
    p.update(extra)
    json.dump(data, open(path, 'w'), indent=4)
    print('updated', path)


def surface(ph, name, scan, saturation, rough_lo, rough_hi, extra, keep_mask=None):
    old = original(name)
    target = old.reshape(-1, 3).mean(axis=0)
    base = recolour(load(os.path.join(ph, f'{scan}_diff_2k.png')), target, saturation)
    if keep_mask is not None:
        m = keep_mask(old)[..., None]
        base = base * (1 - m) + old * m
    save(base, f'level1_{name}_basecolor.png')
    maps(ph, scan, name, rough_lo, rough_hi)
    material(name, extra)


def hazard(old):
    # The yellow-and-black hazard band at the foot of the columns stays: wherever the original is strongly coloured
    # or near black, in the bottom quarter.
    sat = old.max(axis=-1) - old.min(axis=-1)
    dark = old.mean(axis=-1) < 0.15
    mask = ((sat > 0.25) | dark).astype(np.float32)
    rows = np.arange(old.shape[0])[:, None] >= int(old.shape[0] * 0.75)
    return mask * rows


def main():
    ph = sys.argv[1]
    surface(ph, 'floor', 'garage_floor', 0.25, 0.55, 0.95, {'uv.tileU': 12.0, 'uv.tileV': 12.0})
    surface(ph, 'wall', 'concrete_wall_008', 0.3, 0.75, 0.95, {})
    surface(ph, 'ceiling', 'concrete', 0.2, 0.8, 0.98, {'uv.tileU': 8.0, 'uv.tileV': 8.0})
    surface(ph, 'column', 'concrete_wall_006', 0.3, 0.7, 0.95, {}, keep_mask=hazard)


if __name__ == '__main__':
    main()
