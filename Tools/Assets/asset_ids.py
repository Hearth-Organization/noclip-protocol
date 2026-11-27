#!/usr/bin/env python3
"""Asset ids of processed products, read from the Asset Processor's database (Cache/assetdb.sqlite).

  python3 Tools/Assets/asset_ids.py ASSETDB SOURCE_PREFIX [OUT.json]

Prints (and writes) {product path: {"guid": "{...}", "subId": n}} for every product of the sources under
SOURCE_PREFIX (e.g. Assets/Models/Monsters). Prefabs need these for actors, motions and models, whose sub ids
the Asset Processor picks itself.
"""
import json
import sqlite3
import sys
import uuid


def main():
    db, prefix = sys.argv[1], sys.argv[2].lower()
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    rows = con.execute(
        "SELECT s.SourceName, s.SourceGuid, p.ProductName, p.SubID FROM Sources s "
        "JOIN Jobs j ON j.SourcePK = s.SourceID JOIN Products p ON p.JobPK = j.JobID").fetchall()
    out = {}
    for source, guid, product, sub in rows:
        if not source.lower().startswith(prefix):
            continue
        g = guid if isinstance(guid, str) else "{" + str(uuid.UUID(bytes=bytes(guid))).upper() + "}"
        # ProductName is "<platform>/<path>"
        path = product.split("/", 1)[1] if "/" in product else product
        out[path] = {"guid": g, "subId": sub & 0xFFFFFFFF, "source": source}  # stored signed; prefabs want it unsigned
    text = json.dumps(dict(sorted(out.items())), indent=2)
    print(text)
    if len(sys.argv) > 3:
        with open(sys.argv[3], "w") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
