"""Film grain noise for Level 0's Film Grain effect (the engine's own FilmGrain.jpg doesn't process on Linux).

Tileable 512 x 512 grey noise around mid-grey, softened a touch so it reads as film grain rather than pixel static.
Run from the project folder: python3 Tools/Level0/make_film_grain.py
"""
import numpy as np
from PIL import Image

SIZE = 512
rng = np.random.default_rng(1977)
noise = rng.normal(0.0, 1.0, (SIZE, SIZE))
# Soften with a small wrap-around blur (keeps it tileable): grain clumps over 2-3 pixels like real film.
kernel = np.array([0.25, 0.5, 0.25])
for axis in (0, 1):
    noise = sum(w * np.roll(noise, shift, axis=axis) for w, shift in zip(kernel, (-1, 0, 1)))
noise /= noise.std()
grey = np.clip(0.5 + noise * 0.16, 0.0, 1.0)
Image.fromarray((grey * 255).astype(np.uint8), "L").convert("RGB").save("Assets/Textures/Level0/level0_film_grain.png")
print("wrote Assets/Textures/Level0/level0_film_grain.png")
