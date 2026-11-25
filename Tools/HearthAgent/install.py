#!/usr/bin/env python3
"""Installs the Hearth Engine agent so it starts at login and keeps running.

Run once, in a normal terminal (not the O3DE editor console):

    python3 ~/O3DE/Projects/NoclipProtocol/Tools/HearthAgent/install.py

Uses a systemd user service when available (restarts the agent if it ever stops);
otherwise an autostart entry, and starts the agent right away either way.
Run with --uninstall to remove it.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

AGENT = Path(__file__).resolve().parent / "hearth_agent.py"
PYTHON = sys.executable or shutil.which("python3") or "/usr/bin/python3"
UNIT_DIR = Path.home() / ".config" / "systemd" / "user"
UNIT = UNIT_DIR / "hearth-engine-agent.service"
AUTOSTART = Path.home() / ".config" / "autostart" / "hearth-engine-agent.desktop"

UNIT_TEXT = f"""[Unit]
Description=Hearth Engine agent (Noclip Protocol build and editor helper)
After=default.target

[Service]
ExecStart={PYTHON} {AGENT}
Restart=always
RestartSec=3
KillMode=process

[Install]
WantedBy=default.target
"""

DESKTOP_TEXT = f"""[Desktop Entry]
Type=Application
Name=Hearth Engine agent
Exec={PYTHON} {AGENT}
NoDisplay=true
X-GNOME-Autostart-enabled=true
"""


def systemd_available():
    if not shutil.which("systemctl"):
        return False
    return subprocess.run(["systemctl", "--user", "is-system-running"], capture_output=True).returncode in (0, 1)


def install():
    if systemd_available():
        UNIT_DIR.mkdir(parents=True, exist_ok=True)
        UNIT.write_text(UNIT_TEXT)
        subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
        subprocess.run(["systemctl", "--user", "enable", "--now", "hearth-engine-agent.service"], check=False)
        state = subprocess.run(["systemctl", "--user", "is-active", "hearth-engine-agent.service"],
                               capture_output=True, text=True).stdout.strip()
        if state == "active":
            print("Hearth Engine agent installed and running (systemd user service).")
            print("It starts every time you log in. Nothing else to do.")
            return
        print(f"systemd didn't start it ({state}); using an autostart entry instead.")
    AUTOSTART.parent.mkdir(parents=True, exist_ok=True)
    AUTOSTART.write_text(DESKTOP_TEXT)
    subprocess.Popen([PYTHON, str(AGENT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     stdin=subprocess.DEVNULL, start_new_session=True)
    print("Hearth Engine agent installed (autostart entry) and started.")
    print("It starts every time you log in. Nothing else to do.")


def uninstall():
    if UNIT.exists():
        subprocess.run(["systemctl", "--user", "disable", "--now", "hearth-engine-agent.service"], check=False)
        UNIT.unlink()
        subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
    if AUTOSTART.exists():
        AUTOSTART.unlink()
    subprocess.run(["pkill", "-f", str(AGENT)], check=False)
    print("Hearth Engine agent removed.")


if __name__ == "__main__":
    uninstall() if "--uninstall" in sys.argv else install()
