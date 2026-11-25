#!/usr/bin/env python3
"""
Generates the Level 0 textures (seamless where they tile) with numpy + Pillow.

    python3 make_textures.py <output folder>

Writes, for O3DE's image presets (the suffix picks the preset):
  level0_wallpaper_basecolor.png / _normal.png / _roughness.png   one 4.2 m x 3 m wall face, with baseboard
  level0_carpet_basecolor.png / _roughness.png                    a 4 m x 4 m seamless carpet tile
  level0_ceiling_basecolor.png / _normal.png                      a 2.4 m x 2.4 m seamless block of 4 x 4 drop-ceiling tiles
  level0_fixture_basecolor.png / _emissive.png                    a two-tube fluorescent fixture
"""

import os
import sys

import numpy as np
from PIL import Image

RNG = np.random.default_rng(1993)


# ----------------------------------------------------------------------------- noise helpers

def periodic_noise(h, w, scale, octaves=4, persistence=0.5):
    """Seamless fractal noise in [0, 1]: band-limited random fields built in frequency space."""
    total = np.zeros((h, w))
    amplitude = 1.0
    norm = 0.0
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    radius = np.sqrt(fx * fx + fy * fy)
    for octave in range(octaves):
        frequency = (2 ** octave) / scale
        spectrum = (RNG.standard_normal((h, w)) + 1j * RNG.standard_normal((h, w)))
        band = np.exp(-((radius - frequency) ** 2) / (2 * (frequency * 0.5) ** 2))
        field = np.real(np.fft.ifft2(spectrum * band))
        field /= (np.abs(field).max() + 1e-9)
        total += amplitude * field
        norm += amplitude
        amplitude *= persistence
    total /= norm
    return (total - total.min()) / (total.max() - total.min() + 1e-9)


def blotches(h, w, scale, threshold, softness=0.08):
    """Irregular stain mask in [0, 1] (seamless)."""
    n = periodic_noise(h, w, scale, octaves=5, persistence=0.55)
    return np.clip((n - threshold) / softness, 0.0, 1.0)


def normal_from_height(height, strength):
    gy, gx = np.gradient(height)
    nx, ny, nz = -gx * strength, -gy * strength, np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack([nx / length, ny / length, nz / length], axis=-1) * 0.5 + 0.5
    return rgb


def save(array, path, mode="RGB"):
    data = np.clip(array * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(data, mode).save(path)
    print("wrote", os.path.basename(path), data.shape)


# ----------------------------------------------------------------------------- wallpaper

def wallpaper(out):
    # One wall face: 4.2 m wide, 3.0 m tall, 400 px per meter.
    w, h = 1680, 1200
    ppm = w / 4.2
    y, x = np.mgrid[0:h, 0:w].astype(float)
    height_m = (h - y) / ppm  # 0 at the floor

    # Classic pattern: soft vertical stripes with a small repeating diamond motif.
    cell = 0.14 * ppm
    u = (x % cell) / cell - 0.5
    v = ((y + (np.floor(x / cell) % 2) * cell * 0.7) % (cell * 1.4)) / (cell * 1.4) - 0.5  # offset columns
    ring = np.abs(np.abs(u) * 2.0 + np.abs(v) * 2.0 - 0.62)
    diamond_outline = np.clip(1.0 - ring * 9.0, 0, 1)
    dot = np.clip(1.0 - np.sqrt(u * u + v * v) * 16.0, 0, 1)
    column_line = np.clip(1.0 - np.abs(u) * 40.0, 0, 1) * 0.0
    motif = np.maximum(diamond_outline, dot * 0.9)
    stripe = np.clip(1.0 - np.abs(np.abs(u) - 0.5) * 30.0, 0, 1)  # thin line between columns

    base = np.array([0.86, 0.77, 0.36])
    ink = np.array([0.72, 0.63, 0.25])
    pattern = np.clip(0.5 * stripe + motif, 0, 1)[..., None]
    color = base * (1 - 0.55 * pattern) + ink * (0.55 * pattern)

    # Age: overall mottling, darker toward the floor, water streaks running down from the ceiling.
    mottle = periodic_noise(h, w, 60, octaves=5)
    color *= (0.93 + 0.09 * mottle)[..., None]
    floor_grime = np.clip(1.0 - height_m / 0.9, 0, 1) ** 2
    color *= (1.0 - 0.25 * floor_grime)[..., None]
    streak_cols = periodic_noise(1, w, 40, octaves=3)[0]
    streaks = np.clip((streak_cols - 0.62) / 0.15, 0, 1)[None, :] * np.clip(1.0 - (3.0 - height_m) / 1.6, 0, 1)
    stains = blotches(h, w, 260, 0.7, 0.25) * 0.35 + streaks * 0.4
    stain_color = np.array([0.52, 0.44, 0.24])
    color = color * (1 - 0.45 * stains[..., None]) + stain_color * (0.45 * stains[..., None])

    # Baseboard: 10 cm painted strip with a top lip.
    board = height_m < 0.10
    color[board] = np.array([0.55, 0.50, 0.33]) * (0.85 + 0.15 * mottle[board][..., None])
    lip = (height_m >= 0.10) & (height_m < 0.112)
    color[lip] *= 0.55

    save(color, os.path.join(out, "level0_wallpaper_basecolor.png"))

    height = 0.25 * motif + 0.08 * mottle
    height[board] = 0.6
    save(normal_from_height(height, 3.0), os.path.join(out, "level0_wallpaper_normal.png"))
    rough = 0.78 + 0.15 * mottle - 0.15 * stains
    rough[board] = 0.45
    save(np.clip(rough, 0, 1), os.path.join(out, "level0_wallpaper_roughness.png"), "L")


# ----------------------------------------------------------------------------- carpet

def carpet(out):
    n = 1024  # 4 m tile, seamless
    fibers = periodic_noise(n, n, 3, octaves=3, persistence=0.6)
    tufts = periodic_noise(n, n, 18, octaves=4)
    damp = blotches(n, n, 400, 0.62, 0.4) * 0.25
    base = np.array([0.74, 0.64, 0.30])
    color = base * (0.9 + 0.08 * tufts + 0.08 * fibers)[..., None]
    color = color * (1 - 0.35 * damp[..., None]) + np.array([0.38, 0.33, 0.21]) * (0.35 * damp[..., None])
    save(color, os.path.join(out, "level0_carpet_basecolor.png"))
    save(np.clip(0.95 - 0.35 * damp, 0, 1), os.path.join(out, "level0_carpet_roughness.png"), "L")


# ----------------------------------------------------------------------------- ceiling tiles

def ceiling(out):
    n = 1024  # 2.4 m block of 4 x 4 tiles (0.6 m each), seamless
    tile = n // 4
    y, x = np.mgrid[0:n, 0:n]
    tx, ty = x % tile, y % tile
    grid = (tx < 6) | (ty < 6)  # metal T-bar grid
    speckle = periodic_noise(n, n, 2, octaves=2)
    color = np.ones((n, n, 3)) * np.array([0.86, 0.82, 0.64])
    color *= (0.9 + 0.12 * speckle)[..., None]

    # Each tile is a little different; a couple are stained.
    stains = blotches(n, n, 320, 0.74, 0.18) * 0.7
    for ix in range(4):
        for iy in range(4):
            shade = 0.94 + 0.08 * RNG.random()
            region = (slice(iy * tile, (iy + 1) * tile), slice(ix * tile, (ix + 1) * tile))
            color[region] *= shade
    color = color * (1 - 0.5 * stains[..., None]) + np.array([0.6, 0.52, 0.3]) * (0.5 * stains[..., None])
    color[grid] = np.array([0.72, 0.70, 0.62])
    save(color, os.path.join(out, "level0_ceiling_basecolor.png"))

    height = np.where(grid, 1.0, 0.0) + 0.03 * speckle
    save(normal_from_height(height, 1.5), os.path.join(out, "level0_ceiling_normal.png"))


# ----------------------------------------------------------------------------- light fixture

def fixture(out):
    w, h = 1024, 256  # 1.2 m x 0.3 m two-tube troffer
    y, x = np.mgrid[0:h, 0:w].astype(float)
    color = np.ones((h, w, 3)) * np.array([0.82, 0.82, 0.78])  # white frame
    emissive = np.zeros((h, w, 3))
    lens = (x > 24) & (x < w - 24) & (y > 24) & (y < h - 24)
    color[lens] = np.array([0.95, 0.95, 0.9])
    tube_glow = np.zeros((h, w))
    for center in (h * 0.36, h * 0.64):
        tube_glow += np.exp(-((y - center) ** 2) / (2 * 14.0 ** 2))
    tube_glow = np.clip(tube_glow, 0, 1) * lens
    diffuse = 0.55 * lens
    emissive_level = np.clip(diffuse + tube_glow, 0, 1)
    emissive = emissive_level[..., None] * np.array([1.0, 0.98, 0.88])
    save(color, os.path.join(out, "level0_fixture_basecolor.png"))
    save(emissive, os.path.join(out, "level0_fixture_emissive.png"))


def square_panel(out):
    n = 512  # one 0.6 m ceiling tile replaced by a light panel
    y, x = np.mgrid[0:n, 0:n].astype(float)
    frame = (x < 18) | (x > n - 18) | (y < 18) | (y > n - 18)
    color = np.ones((n, n, 3)) * np.array([0.95, 0.94, 0.88])
    color[frame] = np.array([0.80, 0.78, 0.70])
    # diffuser: bright and even, a touch brighter in the middle
    cx, cy = (x - n / 2) / (n / 2), (y - n / 2) / (n / 2)
    glow = np.clip(1.0 - 0.18 * (cx * cx + cy * cy), 0, 1) * (~frame)
    emissive = glow[..., None] * np.array([1.0, 0.97, 0.82])
    save(color, os.path.join(out, "level0_panel_basecolor.png"))
    save(emissive, os.path.join(out, "level0_panel_emissive.png"))


def dead_panel(out):
    """A broken light panel: dirty grey diffuser with a crack and dead bugs, no glow."""
    n = 512
    y, x = np.mgrid[0:n, 0:n].astype(float)
    frame = (x < 18) | (x > n - 18) | (y < 18) | (y > n - 18)
    grime = periodic_noise(n, n, 60, octaves=4)
    color = np.ones((n, n, 3)) * np.array([0.55, 0.54, 0.5]) * (0.75 + 0.35 * grime)[..., None]
    color[frame] = np.array([0.6, 0.58, 0.52])
    crack = np.abs((y - n * 0.3) - 0.45 * (x - n * 0.2) - 18 * np.sin(x / 23.0)) < 2.2
    crack &= (x > n * 0.2) & (x < n * 0.85)
    color[crack] = np.array([0.18, 0.17, 0.15])
    bugs = periodic_noise(n, n, 6, octaves=2) > 0.86
    corner = ((x - n * 0.7) ** 2 + (y - n * 0.75) ** 2) < (n * 0.12) ** 2
    color[bugs & corner] = np.array([0.12, 0.11, 0.09])
    save(color, os.path.join(out, "level0_deadpanel_basecolor.png"))


def decal(out, name, n, color_fn):
    """RGBA floor stain: color_fn returns (rgb, alpha) for an n x n image.

    Stains are drawn on a flat quad, so the shape itself must carry the edge: an irregular,
    noise-warped outline (never a circle) that reaches zero well before the image border.
    """
    y, x = np.mgrid[0:n, 0:n].astype(float)
    r = np.sqrt(((x - n / 2) / (n / 2)) ** 2 + ((y - n / 2) / (n / 2)) ** 2)
    safety = np.clip((0.97 - r) / 0.12, 0, 1)
    rgb, alpha = color_fn(n, r)
    alpha = np.clip(alpha * safety, 0, 1)
    # Color and opacity as separate images: O3DE's _basecolor preset drops alpha, _opacity keeps it.
    save(rgb, os.path.join(out, f"level0_{name}_basecolor.png"))
    save(alpha, os.path.join(out, f"level0_{name}_opacity.png"), "L")


CARPET = np.array([0.74, 0.64, 0.30])


def organic(n, r, size, softness, warp=0.5):
    """Blob mask with a ragged, lobed outline: radial falloff pushed around by two noise layers."""
    big = periodic_noise(n, n, 140, octaves=3) - 0.5
    small = periodic_noise(n, n, 35, octaves=3) - 0.5
    d = r + warp * big + 0.18 * small
    return np.clip((size - d) / softness, 0, 1)


def wet_spot(n, r):
    """Soaked carpet: darker toward the middle where it is wettest, a thin darker tide line where
    it is drying at the edge, and a couple of small splashes."""
    body = organic(n, r, 0.55, 0.22, warp=0.6)        # 0 outside .. 1 deep inside, soft
    edge = organic(n, r, 0.55, 0.03, warp=0.6)         # same outline, crisp
    y, x = np.mgrid[0:n, 0:n].astype(float)
    for cx, cy, s in [(0.80, 0.28, 0.06), (0.20, 0.78, 0.045)]:
        dr = np.sqrt(((x / n - cx) ** 2 + (y / n - cy) ** 2)) + 0.03 * (periodic_noise(n, n, 10, 2) - 0.5)
        splash = np.clip((s - dr) / 0.025, 0, 1)
        body = np.maximum(body, splash * 0.6)
        edge = np.maximum(edge, np.clip((s - dr) / 0.006, 0, 1))
    tide = np.clip(edge - body * 1.6, 0, 1)            # band just inside the crisp outline
    depth = periodic_noise(n, n, 70, octaves=3)
    rgb = np.ones((n, n, 3)) * CARPET * 0.45 * np.array([1.0, 0.94, 0.82])
    rgb *= (0.92 + 0.16 * depth)[..., None]
    rgb = rgb * (1 - tide[..., None]) + np.array([0.27, 0.21, 0.09]) * tide[..., None]
    alpha = body * (0.35 + 0.3 * depth) + tide * 0.45 + edge * 0.12
    return rgb, alpha


def mold(n, r):
    """Mold: round colonies of mixed sizes, each a near-black green core fading through olive to a
    faint brown stain; dense in the middle, thinning out toward the edge."""
    rng = np.random.default_rng(77)
    y, x = np.mgrid[0:n, 0:n].astype(float) / n
    wobble = periodic_noise(n, n, 18, octaves=2) - 0.5
    core = np.zeros((n, n))
    halo = np.zeros((n, n))
    for _ in range(70):
        ang, dist = rng.uniform(0, 2 * np.pi), 0.36 * np.sqrt(rng.random())
        cx, cy = 0.5 + dist * np.cos(ang), 0.5 + dist * np.sin(ang)
        size = rng.uniform(0.008, 0.035) * (1.3 - dist * 1.6)
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) + 0.012 * wobble
        core = np.maximum(core, np.clip((size - d) / (size * 0.35), 0, 1))
        halo = np.maximum(halo, np.clip((size * 2.6 - d) / (size * 1.8), 0, 1))
    stain = organic(n, r, 0.5, 0.3, warp=0.6)
    rgb = np.ones((n, n, 3)) * CARPET * 0.62 * np.array([0.95, 0.9, 0.7])
    rgb = rgb * (1 - halo[..., None] * 0.8) + np.array([0.28, 0.29, 0.12]) * (halo[..., None] * 0.8)
    rgb = rgb * (1 - core[..., None]) + np.array([0.06, 0.08, 0.035]) * core[..., None]
    alpha = np.maximum(np.maximum(core * 0.95, halo * 0.5), stain * 0.22)
    return rgb, alpha


def worn(n, r):
    """Foot-traffic grime: a broad, very soft darkening, streaky in one direction."""
    area = organic(n, r, 0.55, 0.35, warp=0.6)
    y, x = np.mgrid[0:n, 0:n].astype(float)
    streak = periodic_noise(n, n, 50, octaves=3)
    rgb = np.ones((n, n, 3)) * CARPET * 0.55 * np.array([0.95, 0.88, 0.75])
    rgb = rgb * (0.85 + 0.3 * streak)[..., None]
    alpha = area * (0.25 + 0.3 * streak)
    return rgb, alpha


if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(folder, exist_ok=True)
    wallpaper(folder)
    carpet(folder)
    ceiling(folder)
    fixture(folder)
    square_panel(folder)
    dead_panel(folder)
    decal(folder, "decal_wet", 512, wet_spot)
    decal(folder, "decal_mold", 512, mold)
    decal(folder, "decal_worn", 512, worn)
