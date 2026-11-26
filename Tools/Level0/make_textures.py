#!/usr/bin/env python3
"""
Generates the Level 0 textures (seamless where they tile) with numpy + Pillow.

    python3 make_textures.py <output folder>

Writes, for O3DE's image presets (the suffix picks the preset):
  level0_wallpaper_basecolor.png / _normal.png / _roughness.png   one 4.2 m x 3 m wall face, with baseboard
  level0_carpet_basecolor.png / _roughness.png                    a 4 m x 4 m seamless carpet tile
  level0_ceiling_basecolor.png / _normal.png                      a 2 m x 2 m seamless block of 4 x 4 drop-ceiling tiles
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
    n = 1024  # 2.0 m block of 4 x 4 tiles (0.5 m each, lined up with the 4 m cells), seamless
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


def troffer_mask(w, h):
    """Shared shape of a 0.5 m x 1.0 m two-tube troffer (512 x 1024 px, ~1 mm per px): the steel frame
    and the recessed diffuser inside it."""
    y, x = np.mgrid[0:h, 0:w].astype(float)
    frame = (x < 26) | (x > w - 27) | (y < 26) | (y > h - 27)
    lip = ((x < 34) | (x > w - 35) | (y < 34) | (y > h - 35)) & ~frame   # shadowed inner edge
    return x, y, frame, lip


def square_panel(out):
    """A working ceiling light: a 2 x 4 ft (0.5 m x 1.0 m here, two ceiling tiles) fluorescent troffer.
    Ribbed prismatic diffuser over two tubes that run its length, a painted steel frame, a little grime
    and a few dead bugs at one end. The emissive map is the light coming through the diffuser, brightest
    over the tubes, so the panel reads as a fixture instead of a white square."""
    w, h = 512, 1024
    x, y, frame, lip = troffer_mask(w, h)
    lens = ~frame & ~lip
    ribs = 0.5 + 0.5 * np.cos(2 * np.pi * x / 9.0) * np.cos(2 * np.pi * y / 9.0)  # prismatic pyramids
    grime = periodic_noise(h, w, 90, octaves=3)
    end_dirt = np.clip((y - h * 0.78) / (h * 0.22), 0, 1) * (0.5 + 0.5 * grime)
    color = np.ones((h, w, 3)) * np.array([0.93, 0.92, 0.86])
    color *= (0.95 + 0.05 * ribs)[..., None]
    color = color * (1 - 0.25 * end_dirt[..., None]) + np.array([0.55, 0.5, 0.36]) * (0.25 * end_dirt[..., None])
    bugs = (periodic_noise(h, w, 5, octaves=2) > 0.87) & (end_dirt > 0.35)
    color[bugs] = np.array([0.14, 0.12, 0.09])
    color[frame] = np.array([0.80, 0.79, 0.74])
    color[lip] = np.array([0.52, 0.51, 0.47])

    tubes = np.zeros((h, w))
    for cx in (w * 0.33, w * 0.67):
        tubes += np.exp(-((x - cx) ** 2) / (2 * 34.0 ** 2))
    ends = np.clip(np.minimum(y - 40, h - 40 - y) / 90.0, 0, 1)          # tubes dim toward their ends
    light = (0.42 + 0.58 * np.clip(tubes, 0, 1) * ends) * (0.9 + 0.1 * ribs)
    light *= 1 - 0.55 * end_dirt
    light[bugs] = 0.05
    emissive = (light * lens)[..., None] * np.array([1.0, 0.97, 0.84])
    save(color, os.path.join(out, "level0_panel_basecolor.png"))
    save(emissive, os.path.join(out, "level0_panel_emissive.png"))


def dead_panel(out):
    """A dead troffer, same size as the working one: dirty grey diffuser, a crack, dead bugs piled
    at the low end, a dark scorch where a tube burned out. No glow."""
    w, h = 512, 1024
    x, y, frame, lip = troffer_mask(w, h)
    grime = periodic_noise(h, w, 60, octaves=4)
    ribs = 0.5 + 0.5 * np.cos(2 * np.pi * x / 9.0) * np.cos(2 * np.pi * y / 9.0)
    color = np.ones((h, w, 3)) * np.array([0.52, 0.51, 0.47]) * (0.72 + 0.34 * grime)[..., None]
    color *= (0.94 + 0.06 * ribs)[..., None]
    scorch = np.exp(-(((x - w * 0.33) / 40.0) ** 2 + ((y - h * 0.12) / 70.0) ** 2))
    color *= (1 - 0.6 * scorch)[..., None]
    crack = np.abs((x - w * 0.62) - 0.28 * (y - h * 0.35) - 14 * np.sin(y / 31.0)) < 2.4
    crack &= (y > h * 0.35) & (y < h * 0.8)
    color[crack] = np.array([0.16, 0.15, 0.13])
    pile = np.clip((y - h * 0.82) / (h * 0.18), 0, 1)
    bugs = periodic_noise(h, w, 6, octaves=2) > (0.9 - 0.12 * pile)
    color[bugs & (pile > 0)] = np.array([0.1, 0.09, 0.07])
    color[frame] = np.array([0.6, 0.58, 0.52])
    color[lip] = np.array([0.38, 0.37, 0.34])
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
    """Mold on damp carpet: irregular fuzzy patches (not dots) that grow out of a brown damp stain,
    darkest and greenest where they are oldest, with a few small satellite spores around them."""
    damp = organic(n, r, 0.6, 0.28, warp=0.7)                       # the wet stain it grows in
    growth_noise = periodic_noise(n, n, 55, octaves=5, persistence=0.6)
    fuzz = periodic_noise(n, n, 9, octaves=3)
    patch = np.clip((growth_noise - 0.56) / 0.12, 0, 1) * np.clip(damp * 1.6, 0, 1)
    patch = np.clip(patch * (0.75 + 0.5 * fuzz), 0, 1)              # fuzzy edges
    old = np.clip((growth_noise - 0.66) / 0.08, 0, 1) * patch        # dense, darkest centres
    spores = np.clip((periodic_noise(n, n, 4, octaves=2) - 0.83) / 0.05, 0, 1) * np.clip(damp * 2 - 0.4, 0, 1)
    rgb = np.ones((n, n, 3)) * CARPET * 0.6 * np.array([0.95, 0.88, 0.7])
    rgb = rgb * (1 - patch[..., None] * 0.85) + np.array([0.24, 0.26, 0.14]) * (patch[..., None] * 0.85)
    rgb = rgb * (1 - old[..., None] * 0.9) + np.array([0.07, 0.09, 0.05]) * (old[..., None] * 0.9)
    rgb = rgb * (1 - spores[..., None] * 0.7) + np.array([0.12, 0.13, 0.07]) * (spores[..., None] * 0.7)
    alpha = np.maximum.reduce([damp * 0.28, patch * 0.8, old * 0.95, spores * 0.6])
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
