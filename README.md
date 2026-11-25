# Noclip Protocol

A co-op first-person horror shooter with base building: the Foundation invades the Backrooms. Built in **O3DE 26.10**.

Design doc: https://claude.ai/artifact/YTNiz8wGTzr5ceY7oDRFXG

## What's in here

| Path | What it is |
| --- | --- |
| `Gem/Source/Player/` | First-person controller (WASD, mouse look, sprint, jump) and battery flashlight (F) |
| `Gem/Source/Level/` | Endless Level 0: deterministic chunked maze layout (`Level0Layout`) and the streaming component that spawns chunks around the player (`EndlessLevelComponent`) |
| `Prefabs/Level0/` | Tile pieces the generator spawns: wall, floor, ceiling, ceiling light |
| `Assets/Materials/Level0/` | Placeholder Level 0 materials (wallpaper, carpet, ceiling tiles, light panels) |
| `Levels/Level0/` | The Level 0 level: player, generator, fog, endless floor collision |
| `Editor/Scripts/` | Editor startup scripts: the Claude bridge and level-building helpers |
| `Tools/ClaudeBridge/` | MCP connector that lets Claude drive the editor (status, Python, console, screenshots, log) |

## Running it

1. Build the project in the O3DE Project Manager.
2. Open the editor (on Wayland desktops, in X11 mode):
   `QT_QPA_PLATFORM=xcb /var/opt/O3DE/26.10/bin/Linux/profile/Default/Editor --project-path <this folder>`
3. Open **Level0** and press **Ctrl+G**. WASD to move, mouse to look, Shift to sprint, Space to jump, F for the flashlight, Esc to stop.

## Linux build notes

- Debian trixie needs `libwayland-dev` from `trixie-backports` (matching the installed Wayland 1.26) for O3DE's Qt to configure.
