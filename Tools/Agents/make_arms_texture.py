#!/usr/bin/env python3
"""The first-person arms dressed like the agent: black sleeves and gloves (owner, Nov 28).

    python3 Tools/Agents/make_arms_texture.py

Reads Assets/Models/Agents/Agent/Agent_basecolor.png (the arms' own UVs), keeps its folds and seams, makes it the
agent's cold black; writes Assets/Models/Agents/Arms/Arms_basecolor.png, Arms_normal.png and Arms.material.
"""
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

A = Path(__file__).resolve().parents[2] / "Assets" / "Models" / "Agents"
src = Image.open(A / "Agent" / "Agent_basecolor.png").convert("RGB")
L = np.asarray(src.convert("L")).astype(float) / 255
blur = np.asarray(src.convert("L").filter(ImageFilter.GaussianBlur(12))).astype(float) / 255
lum = np.clip(0.07 + 0.04 * (blur - 0.4) + 0.35 * (L - blur), 0.015, 0.16)
(A / "Arms").mkdir(exist_ok=True)
Image.fromarray((np.clip(lum[..., None] * np.array([0.96, 1.0, 1.08]), 0, 1) * 255).astype(np.uint8)).save(A / "Arms" / "Arms_basecolor.png")
Image.open(A / "Agent" / "Agent_normal.png").save(A / "Arms" / "Arms_normal.png")
(A / "Arms" / "Arms.material").write_text(json.dumps({
    "materialType": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/StandardPBR.materialtype", "materialTypeVersion": 5,
    "propertyValues": {"baseColor.textureMap": "Arms_basecolor.png", "normal.textureMap": "Arms_normal.png",
                       "baseColor.factor": 0.7, "roughness.factor": 0.9, "metallic.factor": 0.0}}, indent=4) + "\n")
print("arms texture written")
