#!/usr/bin/env python3
"""Paints the face under the agent's visor out (owner: visor closed, no face showing).

    python3 Tools/Agents/close_visor.py Assets/Models/Agents/AgentMale/AgentMale

Skin-coloured texels in NAME_basecolor.png become the visor's glossy black (NAME_roughness.png goes low there).
Run after Tools/Monsters/make_monster.py writes the textures.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

base = sys.argv[1]
b = np.asarray(Image.open(base + "_basecolor.png").convert("RGB")).astype(float) / 255
r, g, bl = b[..., 0], b[..., 1], b[..., 2]
mx, mn = b.max(-1), b.min(-1)
skin = (r > g) & (g > bl) & ((mx - mn) / (mx + 1e-6) > 0.22) & (mx > 0.30) & ((r - bl) > 0.12)
k = Image.fromarray((skin * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.5))
k = np.asarray(k).astype(float)[..., None] / 255
Image.fromarray(((b * (1 - k) + np.array([0.05, 0.055, 0.065]) * k) * 255).astype(np.uint8)).save(base + "_basecolor.png")
ro = np.asarray(Image.open(base + "_roughness.png")).astype(float)
Image.fromarray((ro * (1 - k[..., 0]) + 40 * k[..., 0]).astype(np.uint8)).save(base + "_roughness.png")
print(f"visor: {skin.mean() * 100:.2f}% of the texture painted")
