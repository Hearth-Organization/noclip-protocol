"""Level 0's photoreal materials: the Backrooms' own colours and patterns, with the real-world detail of photo-scanned
surfaces from Poly Haven (CC0): fibres and stains in the carpet, peeling, scuffs and damp in the wallpaper, the rough
paint of the ceiling tiles.

The owner, Nov 28 2026: "we need to work on making the game look photo realistic now".

usage: python3 Tools/Materials/make_photoreal_level0.py POLYHAVEN_DIR
  POLYHAVEN_DIR holds the 2k PNG sets of dirty_carpet, decrepit_wallpaper and ceiling_interior
  (https://polyhaven.com, downloaded with the api: api.polyhaven.com/files/<name>, the "2k" "png" urls of
  Diffuse, nor_gl, Rough, AO, Displacement).
Writes Assets/Textures/Level0/level0_<surface>_<map>.png and updates the three .material files.
"""
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEX = os.path.join(ROOT, 'Assets', 'Textures', 'Level0')
MAT = os.path.join(ROOT, 'Assets', 'Materials', 'Level0')
ORIG = os.path.join(ROOT, 'Tools', 'Materials', 'level0_originals')  # the hand-made textures the blends start from


def load(path, size=None, mode='RGB'):
    im = Image.open(path).convert(mode)
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def save(arr, name, mode='RGB'):
    arr = np.clip(arr, 0.0, 1.0)
    out = (arr * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, mode).save(os.path.join(TEX, name), optimize=True)
    print('wrote', name, out.shape)


def lum(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def detail(scan_rgb, sigma_fine, sigma_coarse, keep_coarse):
    """The scan's brightness as a multiplier round 1: fine detail kept whole, the broad pattern only in part."""
    l = lum(scan_rgb) + 1e-3
    coarse = gaussian_filter(l, sigma_coarse, mode='wrap')
    fine = l / coarse                                   # fibres, scratches, grain
    broad = coarse / coarse.mean()                      # stains, damp, fading
    return fine * (broad ** keep_coarse)


def normal_blend(a, b, strength_b):
    """Whiteout blend of two tangent-space normal maps (0-1 encoded, OpenGL green)."""
    na = a * 2.0 - 1.0
    nb = b * 2.0 - 1.0
    nb[..., :2] *= strength_b
    n = np.stack([na[..., 0] + nb[..., 0], na[..., 1] + nb[..., 1], na[..., 2] * nb[..., 2]], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-6
    return n * 0.5 + 0.5


def flat_normal(shape):
    n = np.zeros(shape[:2] + (3,), np.float32)
    n[..., 0] = 0.5
    n[..., 1] = 0.5
    n[..., 2] = 1.0
    return n


def update_material(name, values, remove=()):
    path = os.path.join(MAT, name)
    data = json.load(open(path))
    props = data['propertyValues']
    for key in remove:
        props.pop(key, None)
    props.update(values)
    json.dump(data, open(path, 'w'), indent=4)
    print('updated', name)


def carpet(ph):
    # The Backrooms' damp, mustard office carpet: its own colour and stains, the scan's fibres and pile.
    size = (2048, 2048)
    old = load(os.path.join(ORIG, 'level0_carpet_basecolor.png'), size)
    scan = load(os.path.join(ph, 'dirty_carpet_diff_2k.png'), size)
    fibres = detail(scan, 0, 10.0, 0.0)               # no stripes from the scanned rug's pattern
    fibres = gaussian_filter(fibres, 0.8, mode='wrap')
    fibres = 1.0 + (fibres - 1.0) * 0.45
    base = old * fibres[..., None]
    save(base, 'level0_carpet_basecolor.png')
    save(load(os.path.join(ph, 'dirty_carpet_nor_gl_2k.png'), size), 'level0_carpet_normal.png')
    rough = load(os.path.join(ph, 'dirty_carpet_rough_2k.png'), size, 'L')
    save(np.clip(rough * 0.15 + 0.82, 0, 1), 'level0_carpet_roughness.png', 'L')   # carpet: rough, a little less where worn
    save(load(os.path.join(ph, 'dirty_carpet_ao_2k.png'), size, 'L'), 'level0_carpet_ao.png', 'L')
    save(load(os.path.join(ph, 'dirty_carpet_disp_2k.png'), size, 'L'), 'level0_carpet_height.png', 'L')
    update_material('level0_carpet.material', {
        'normal.textureMap': '../../Textures/Level0/level0_carpet_normal.png',
        'normal.factor': 0.6,
        'occlusion.diffuseTextureMap': '../../Textures/Level0/level0_carpet_ao.png',
        'occlusion.diffuseFactor': 1.0,
        'parallax.textureMap': '../../Textures/Level0/level0_carpet_height.png',
        'parallax.factor': 0.008,
        'uv.tileU': 12.0,
        'uv.tileV': 12.0,
    })


def wallpaper(ph):
    # The diamond-pattern yellow wallpaper, aged by the scan: peeling, scuffs, damp patches, fading.
    old_rgb = load(os.path.join(ORIG, 'level0_wallpaper_basecolor.png'))
    h, w = old_rgb.shape[:2]
    size = (w * 2, h * 2)                              # twice the detail of the old one
    old = load(os.path.join(ORIG, 'level0_wallpaper_basecolor.png'), size)
    scan = load(os.path.join(ph, 'decrepit_wallpaper_diff_2k.png'), size)
    grime = detail(scan, 0, 40.0, 0.35)
    grime = 1.0 + (grime - 1.0) * 0.4
    # Where the scan's paper has peeled to the backing, a paler, browner colour shows through.
    l = lum(scan)
    peel = np.clip((l - np.percentile(l, 92)) / 0.08, 0, 1)
    backing = np.array([0.62, 0.55, 0.40], np.float32)
    base = old * grime[..., None]
    base = base * (1 - peel[..., None] * 0.35) + backing * peel[..., None] * 0.35
    save(base, 'level0_wallpaper_basecolor.png')
    old_n = load(os.path.join(ORIG, 'level0_wallpaper_normal.png'), size)
    scan_n = load(os.path.join(ph, 'decrepit_wallpaper_nor_gl_2k.png'), size)
    save(normal_blend(old_n, scan_n, 0.5), 'level0_wallpaper_normal.png')
    rough = load(os.path.join(ph, 'decrepit_wallpaper_rough_2k.png'), size, 'L')
    save(np.clip(rough * 0.35 + 0.55, 0, 1), 'level0_wallpaper_roughness.png', 'L')  # satin paper, rougher where torn
    save(load(os.path.join(ph, 'decrepit_wallpaper_ao_2k.png'), size, 'L'), 'level0_wallpaper_ao.png', 'L')
    for name in ('level0_wallpaper.material', 'level0_wallpaper_pillar.material'):
        update_material(name, {
            'normal.factor': 1.0,
            'occlusion.diffuseTextureMap': '../../Textures/Level0/level0_wallpaper_ao.png',
            'occlusion.diffuseFactor': 0.8,
        })


def ceiling(ph):
    # The drop-ceiling tiles keep their grid and water stains; the scan gives them real, rough painted fibreboard.
    size = (2048, 2048)
    old = load(os.path.join(ORIG, 'level0_ceiling_basecolor.png'), size)
    scan = load(os.path.join(ph, 'ceiling_interior_diff_2k.png'), size)
    grain = detail(scan, 0, 30.0, 0.3)
    grain = 1.0 + (grain - 1.0) * 0.35
    save(old * grain[..., None], 'level0_ceiling_basecolor.png')
    old_n = load(os.path.join(ORIG, 'level0_ceiling_normal.png'), size)
    scan_n = load(os.path.join(ph, 'ceiling_interior_nor_gl_2k.png'), size)
    save(normal_blend(old_n, scan_n, 0.35), 'level0_ceiling_normal.png')
    rough = load(os.path.join(ph, 'ceiling_interior_rough_2k.png'), size, 'L')
    save(np.clip(rough * 0.2 + 0.78, 0, 1), 'level0_ceiling_roughness.png', 'L')
    save(load(os.path.join(ph, 'ceiling_interior_ao_2k.png'), size, 'L'), 'level0_ceiling_ao.png', 'L')
    update_material('level0_ceiling.material', {
        'roughness.textureMap': '../../Textures/Level0/level0_ceiling_roughness.png',
        'occlusion.diffuseTextureMap': '../../Textures/Level0/level0_ceiling_ao.png',
        'occlusion.diffuseFactor': 0.8,
    })


def main():
    ph = sys.argv[1]
    # Keep the hand-made originals once (the blends start from them every time, so a rerun gives the same result).
    for name in ('carpet_basecolor', 'wallpaper_basecolor', 'wallpaper_normal', 'ceiling_basecolor', 'ceiling_normal'):
        src = os.path.join(TEX, f'level0_{name}.png')
        keep = os.path.join(ORIG, f'level0_{name}.png')
        if not os.path.exists(keep):
            os.makedirs(ORIG, exist_ok=True)
            os.rename(src, keep)
    carpet(ph)
    wallpaper(ph)
    ceiling(ph)


if __name__ == '__main__':
    main()
