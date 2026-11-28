# Renders each monster actor from +Y, +X, -Y, -X (Workbench) to see which way its face points:
#   python3 Tools/Monsters/front_views.py -- Assets/Models/Monsters OUT_DIR Smiler Wretch ...
# Blender +X here is the game's -X (O3DE imports turned 180 degrees): a face at +X needs turn -90, -Y needs 0, +Y needs 180.
import bpy, sys, math, mathutils
from pathlib import Path
root = Path(sys.argv[sys.argv.index('--')+1]); out = Path(sys.argv[sys.argv.index('--')+2])
kinds = sys.argv[sys.argv.index('--')+3:]
for k in kinds:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(root / k / f'{k}.fbx'))
    bpy.context.view_layer.update()
    ms = [o for o in bpy.data.objects if o.type == 'MESH']
    pts = [m.matrix_world @ mathutils.Vector(c) for m in ms for c in m.bound_box]
    lo = mathutils.Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = mathutils.Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    c = (lo + hi) / 2; size = max(hi - lo)
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'; sc.display.shading.light = 'STUDIO'; sc.display.shading.color_type = 'TEXTURE'
    sc.render.resolution_x = 320; sc.render.resolution_y = 400; sc.render.film_transparent = False
    sc.world = bpy.data.worlds.new('w'); sc.world.color = (0.25, 0.25, 0.3)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = size * 1.15
    for name, d in (('pY', (0, 1)), ('pX', (1, 0)), ('nY', (0, -1)), ('nX', (-1, 0))):
        cam.location = c + mathutils.Vector((d[0], d[1], 0)) * size * 3
        cam.rotation_euler = (mathutils.Vector((-d[0], -d[1], 0))).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = str(out / f'{k}_{name}.png'); bpy.ops.render.render(write_still=True)
