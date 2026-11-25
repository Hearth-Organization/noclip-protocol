#!/usr/bin/env python3
"""Writes l0_plane.gltf: a flat 1 m x 1 m single-sided quad facing up, UVs 0..1.

    python3 make_plane.py <output folder>

Used for floor stains so they lie flush on the carpet (a squashed box shows its sides).
glTF is Y-up; O3DE converts it to Z-up on import, so the quad faces +Z in the engine.
"""
import base64
import json
import struct
import sys
from pathlib import Path

positions = [(-0.5, 0.0, -0.5), (0.5, 0.0, -0.5), (0.5, 0.0, 0.5), (-0.5, 0.0, 0.5)]
normals = [(0.0, 1.0, 0.0)] * 4
uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
indices = [0, 3, 2, 0, 2, 1]  # counter-clockwise seen from +Y

blob = b"".join(struct.pack("<3f", *p) for p in positions)
blob += b"".join(struct.pack("<3f", *n) for n in normals)
blob += b"".join(struct.pack("<2f", *t) for t in uvs)
index_offset = len(blob)
blob += b"".join(struct.pack("<H", i) for i in indices)
blob += b"\0" * (-len(blob) % 4)

gltf = {
    "asset": {"version": "2.0", "generator": "NoclipProtocol make_plane.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "l0_plane", "mesh": 0}],
    "materials": [{"name": "l0_plane_mat"}],
    "meshes": [{"name": "l0_plane", "primitives": [{
        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
        "indices": 3, "material": 0}]}],
    "buffers": [{"byteLength": len(blob),
                 "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962},
        {"buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962},
        {"buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962},
        {"buffer": 0, "byteOffset": index_offset, "byteLength": 12, "target": 34963},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
         "min": [-0.5, 0.0, -0.5], "max": [0.5, 0.0, 0.5]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
    ],
}

out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
out.mkdir(parents=True, exist_ok=True)
(out / "l0_plane.gltf").write_text(json.dumps(gltf, indent=1))
print("wrote", out / "l0_plane.gltf")
