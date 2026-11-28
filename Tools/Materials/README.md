# Materials tools

- `make_photoreal_level0.py POLYHAVEN_DIR`: Level 0's photoreal carpet, wallpaper and ceiling. Blends the hand-made
  Backrooms textures (kept in `level0_originals/`) with photo-scanned CC0 sets from Poly Haven (dirty_carpet,
  decrepit_wallpaper, ceiling_interior, 2k PNG: diffuse, nor_gl, rough, ao, disp) and updates the .material files.
- `make_photoreal_level1.py POLYHAVEN_DIR`: Level 1's concrete (garage_floor, concrete_wall_008, concrete,
  concrete_wall_006), brought to the level's own grey; the columns keep their hazard band. Originals in `level1_originals/`.

Note: copy new textures in with the game stopped. A material processed before its textures exist bakes an empty
reference (the engine's placeholder pattern), and swapping textures under a running game has lost the Vulkan device.
