#!/usr/bin/env python3
"""Hearth Engine "Package the game": turns an exported game folder into what it's for.

    finish_package.py --export build/export/<exported folder> --target hearthos|steam|test \
        --name NoclipProtocol --display "Noclip Protocol" --version 0.1 --out build/packages [--leave-out-editor]

hearthos: an AppImage plus SHA256SUMS.txt (the Hearth Store installs AppImages from a GitHub release and checks them).
steam:    a folder ready to be a Steam depot (Linux), with a launch script.
test:     a .tar.gz of a folder to try on another device.
Used by the Hearth Engine agent on the laptop and by .github/workflows/hearth-package.yml on GitHub.
"""
import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tarfile
from pathlib import Path

EDITOR_ONLY = ("Editor", "AssetProcessor", "AssetBuilder", "AssetBundler", "SerializeContextTools", "PythonBindingsExample")


def find_launcher(folder, name):
    for candidate in sorted(folder.rglob(f"{name}.GameLauncher")):
        if candidate.is_file():
            return candidate
    for candidate in sorted(folder.rglob("*.GameLauncher")):
        if candidate.is_file():
            return candidate
    return None


def leave_out_editor(folder):
    removed = 0
    for path in list(folder.rglob("*")):
        if not path.is_file():
            continue
        name = path.name
        if name.endswith((".pdb", ".debug", ".dbg")) or any(name.startswith(part) or f".{part}." in name for part in EDITOR_ONLY):
            path.unlink()
            removed += 1
    return removed


def make_executable(path):
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--target", required=True, choices=["hearthos", "steam", "test"])
    parser.add_argument("--name", required=True)
    parser.add_argument("--display", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--icon", type=Path)
    parser.add_argument("--leave-out-editor", action="store_true")
    args = parser.parse_args()

    if not args.version.strip():
        sys.exit("a version is needed (the owner picks it)")
    export = args.export.resolve()
    if not export.is_dir():
        sys.exit(f"no exported game at {export}")
    slug = args.display.lower().replace(" ", "-")
    work = args.out.resolve() / f"{slug}-{args.version}-{args.target}"
    shutil.rmtree(work, ignore_errors=True)
    game = work / (args.display if args.target == "steam" else slug)
    shutil.copytree(export, game, symlinks=True)
    if args.leave_out_editor:
        print(f"left out {leave_out_editor(game)} editor-only files")
    # The game's HUD (Hearth Engine Game UI editor) is read at run time from the products folder.
    hud = Path(__file__).resolve().parents[2] / "UI" / "HUD.uicanvas"
    if hud.is_file():
        products = [p for p in game.rglob("*") if p.is_dir() and p.name == "linux" and p.parent.name.lower() == "cache"] or [game]
        for folder in products:
            (folder / "ui").mkdir(parents=True, exist_ok=True)
            shutil.copy2(hud, folder / "ui" / "hud.uicanvas")
    launcher = find_launcher(game, args.name)
    if not launcher:
        sys.exit("the export has no game launcher")
    make_executable(launcher)
    run = game / "run.sh"
    run.write_text("#!/bin/sh\n# " + args.display + " " + args.version + "\ncd \"$(dirname \"$0\")\"\nexec \"./" +
                   str(launcher.relative_to(game)) + "\" \"$@\"\n")
    make_executable(run)
    results = []

    if args.target == "test":
        archive = args.out.resolve() / f"{slug}-{args.version}-test.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(game, arcname=slug)
        results.append(archive)
    elif args.target == "steam":
        (work / "README-steam.txt").write_text(
            f"{args.display} {args.version} for Steam (Linux).\n\n"
            f"Upload the folder '{args.display}' as the Linux depot. Launch option: run.sh\n")
        results.append(game)
    else:
        appdir = work / f"{args.display}.AppDir"
        shutil.move(str(game), str(appdir / "game"))
        (appdir / "AppRun").write_text("#!/bin/sh\nexec \"$(dirname \"$(readlink -f \"$0\")\")/game/run.sh\" \"$@\"\n")
        make_executable(appdir / "AppRun")
        (appdir / f"{slug}.desktop").write_text(
            f"[Desktop Entry]\nType=Application\nName={args.display}\nExec=AppRun\nIcon={slug}\nCategories=Game;\n")
        icon = appdir / f"{slug}.png"
        if args.icon and args.icon.exists():
            shutil.copy2(args.icon, icon)
        tool = os.environ.get("APPIMAGETOOL") or shutil.which("appimagetool") or shutil.which("appimagetool-x86_64.AppImage")
        if not tool:
            sys.exit("appimagetool isn't on this computer (GitHub packaging has it)")
        image = args.out.resolve() / f"{args.display.replace(' ', '-')}-{args.version}-x86_64.AppImage"
        env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
        subprocess.run([tool, "--no-appstream", str(appdir), str(image)], check=True, env=env)
        sums = args.out.resolve() / "SHA256SUMS.txt"
        sums.write_text(f"{sha256(image)}  {image.name}\n")
        results += [image, sums]
    for result in results:
        print(f"PACKAGE {result}")


if __name__ == "__main__":
    main()
