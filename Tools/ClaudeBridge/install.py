#!/usr/bin/env python3
"""
Registers the O3DE bridge MCP server with the Claude desktop app (one time).

Adds an "o3de-bridge" entry to ~/.config/Claude/claude_desktop_config.json,
keeping everything already in that file, and saves a backup first.
Run again any time; it just updates the entry.
"""

import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))
SERVER = os.path.join(HERE, "mcp_server.py")
CONFIG_DIR = os.path.expanduser("~/.config/Claude")
CONFIG = os.path.join(CONFIG_DIR, "claude_desktop_config.json")


def main():
    os.makedirs(CONFIG_DIR, exist_ok=True)
    config = {}
    if os.path.exists(CONFIG):
        with open(CONFIG, "r", encoding="utf-8") as config_file:
            text = config_file.read().strip()
        if text:
            try:
                config = json.loads(text)
            except ValueError:
                print(f"{CONFIG} isn't valid JSON, so I won't touch it. Fix or move it, then run this again.")
                return 1
        backup = f"{CONFIG}.backup-{time.strftime('%Y%m%d-%H%M%S')}"
        shutil.copy2(CONFIG, backup)
        print(f"Backed up the existing config to {backup}")

    servers = config.setdefault("mcpServers", {})
    servers["o3de-bridge"] = {
        "command": sys.executable or "/usr/bin/python3",
        "args": [SERVER, "--project", PROJECT],
    }

    with open(CONFIG, "w", encoding="utf-8") as config_file:
        json.dump(config, config_file, indent=2)
        config_file.write("\n")

    os.chmod(SERVER, 0o755)
    print(f"Added 'o3de-bridge' (project: {PROJECT}) to {CONFIG}")
    print("Now fully quit the Claude desktop app and open it again so it starts the bridge.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
