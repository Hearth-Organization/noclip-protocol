#!/usr/bin/env python3
"""Hearth Engine agent: a small background helper for the Noclip Protocol project.

It runs outside the editor (started at login), so it keeps working when the editor is closed,
frozen or has crashed. It lets Claude build the project, start and stop the editor and the
standalone game, and read crash reports and logs.

How it is driven: request files. Each request is a JSON file dropped into
<project>/user/HearthAgent/inbox/ with the token from <project>/user/HearthAgent/token;
the answer is written to <project>/user/HearthAgent/outbox/<id>.json.

    {"id": "abc", "token": "...", "op": "status", "args": {}}

What it will do is a fixed list of operations (see OPS below). It never runs arbitrary commands,
and it only touches this project's folder, the engine's own programs, and the system's crash list.
"""
import fcntl
import glob
import json
import os
import secrets
import shutil
import signal
import subprocess
import sys
import threading
import time
import traceback
from pathlib import Path

def _project_from_args():
    """The project this agent works on: --project PATH, else HEARTH_PROJECT, else the project this file sits in.
    (The Hearth Engine ships a copy of this agent in scripts/hearth/agent/ and starts it with --project.)"""
    if "--project" in sys.argv[1:-1]:
        return Path(sys.argv[sys.argv.index("--project") + 1]).expanduser().resolve()
    if os.environ.get("HEARTH_PROJECT"):
        return Path(os.environ["HEARTH_PROJECT"]).expanduser().resolve()
    return Path(__file__).resolve().parents[2]


PROJECT = _project_from_args()
HOME = PROJECT / "user" / "HearthAgent"
INBOX = HOME / "inbox"
OUTBOX = HOME / "outbox"
TOKEN_FILE = HOME / "token"
HEARTBEAT = HOME / "heartbeat.json"
LAUNCH_FILE = HOME / "editor_launch.json"
BUILD_LOG = HOME / "build.log"
AGENT_LOG = HOME / "agent.log"
LOCK_FILE = HOME / "agent.lock"
BRIDGE = PROJECT / "user" / "ClaudeBridge"
LOG_DIR = PROJECT / "user" / "log"
LAUNCHER = PROJECT / "build" / "linux" / "bin" / "profile" / "NoclipProtocol.GameLauncher"
BUILD_TARGETS = ["NoclipProtocol.GameLauncher", "NoclipProtocol.HeadlessServerLauncher",
                 "NoclipProtocol.ServerLauncher", "NoclipProtocol.UnifiedLauncher", "Editor"]
VERSION = "1.8"
ENGINE_DIR = Path.home() / "O3DE" / "Engines" / "hearth-engine"
ENGINE_REPO = "https://github.com/Dragongamer9/hearth-engine"
ENGINE_BRANCH = "hearth/main"
ENGINE_LOG = HOME / "engine_build.log"
STATS_FILE = HOME / "stats.json"
PROJECTS_ROOT = PROJECT.parent
SHOTS = BRIDGE / "shots"
PROJECT_JSON = PROJECT / "project.json"
HTTP_PORT = 47830
HUB_DIR = PROJECT / "Tools" / "HearthEngine" / "hub"
LEVEL0_PREFAB = PROJECT / "Levels" / "Level0" / "Level0.prefab"
LEVELGEN_TOOL = HOME / "level0gen"
LEVELGEN_SOURCES = [PROJECT / "Tools" / "Level0" / "level0gen.cpp",
                    PROJECT / "Gem" / "Source" / "Level" / "Level0Json.cpp",
                    PROJECT / "Gem" / "Source" / "Level" / "Level0Json.h",
                    PROJECT / "Gem" / "Source" / "Level" / "Level0Layout.cpp",
                    PROJECT / "Gem" / "Source" / "Level" / "Level0Layout.h"]
# Level Generator settings: Hub name -> (field in the level file, label in the Editor, default, generator key)
LEVELGEN_FIELDS = {
    "seed": ("Seed", "Seed", 1, "seed"),
    "newSeedEachRun": ("RandomSeedEachRun", "Random seed each run", True, None),
    "openness": ("OpenFraction", "Openness", 0.2, "openFraction"),
    "roomChance": ("RoomChance", "Open room chance", 0.35, "roomChance"),
    "pillarChance": ("PillarChance", "Pillar chance", 0.6, "pillarChance"),
    "baseSize": ("SpawnRoomRadius", "Forward base size", 3, "spawnRoomRadius"),
    "darkness": ("DarkZoneShare", "Darkness", 0.4, "darkZoneShare"),
    "darkZoneSize": ("DarkZoneScale", "Dark zone size", 3.5, "darkZoneScale"),
    "stains": ("DecalChance", "Stains", 0.3, "decalChance"),
    "flicker": ("EnableFlicker", "Flickering lights", True, None),
    "dying": ("EnableDyingLights", "Dying lights", True, None),
    "roomShifting": ("EnableRoomShifting", "Room shifting (soon)", False, None),
}

_build_lock = threading.Lock()
_build_state = {"running": False, "started": None, "finished": None, "exit_code": None}
_game_proc = None


def log(message):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + message
    with open(AGENT_LOG, "a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def tail(path, lines=60):
    try:
        with open(path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            size = handle.tell()
            handle.seek(max(0, size - 256 * 1024))
            text = handle.read().decode("utf-8", "replace")
        return "\n".join(text.splitlines()[-lines:])
    except OSError as error:
        return f"(can't read {path}: {error})"


# ---------- finding processes ----------

def find_processes(name_fragment):
    """[(pid, cmdline list)] of this user's processes whose program name contains name_fragment."""
    found = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            raw = Path(f"/proc/{entry}/cmdline").read_bytes()
            if not raw:
                continue
            args = [a.decode("utf-8", "replace") for a in raw.split(b"\0") if a]
            program = os.path.basename(args[0])
            if name_fragment in program and os.stat(f"/proc/{entry}").st_uid == os.getuid():
                found.append((int(entry), args))
        except (OSError, IndexError):
            continue
    return found


def editor_processes():
    return [(pid, args) for pid, args in find_processes("Editor")
            if os.path.basename(args[0]) == "Editor"]


def game_processes():
    return find_processes("NoclipProtocol.GameLauncher")


def remember_editor_launch():
    """When the editor is running, store exactly how it was started, so we can start it the same way."""
    for pid, args in editor_processes():
        try:
            raw_env = Path(f"/proc/{pid}/environ").read_bytes()
            env = dict(item.decode("utf-8", "replace").split("=", 1)
                       for item in raw_env.split(b"\0") if b"=" in item)
            keep = {k: v for k, v in env.items() if k in (
                "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "QT_QPA_PLATFORM",
                "XDG_SESSION_TYPE", "DBUS_SESSION_BUS_ADDRESS", "HOME", "PATH", "LD_LIBRARY_PATH", "LANG")}
            data = {"args": args, "cwd": os.readlink(f"/proc/{pid}/cwd"), "env": keep, "seen": time.time()}
            LAUNCH_FILE.write_text(json.dumps(data, indent=2))
        except OSError:
            pass
        return


def default_editor_launch():
    # The installed Hearth Engine first, then other O3DE installs.
    candidates = [c for c in ("/opt/hearth-engine/bin/Linux/profile/Default/Editor",
                              "/var/opt/hearth-engine/bin/Linux/profile/Default/Editor") if os.path.exists(c)][:1]
    candidates += sorted(glob.glob("/var/opt/O3DE/*/bin/Linux/profile/Default/Editor"), reverse=True)
    candidates += sorted(glob.glob(os.path.expanduser("~/O3DE/Engines/*/bin/Linux/profile/Default/Editor")), reverse=True)
    if not candidates:
        return None
    return {"args": [candidates[0], f"--project-path={PROJECT}"], "cwd": str(PROJECT), "env": {}}


def bridge_alive(max_age=10.0):
    try:
        return time.time() - (BRIDGE / "heartbeat.json").stat().st_mtime < max_age
    except OSError:
        return False


def bridge_python(code, timeout=60.0):
    """Run code inside the editor through the Claude bridge's file inbox. Returns its output or None."""
    try:
        session = json.loads((BRIDGE / "session.json").read_text())
        token = session.get("token")
    except (OSError, ValueError):
        return None
    request_id = "agent-" + secrets.token_hex(6)
    inbox = BRIDGE / "inbox"
    outbox = BRIDGE / "outbox"
    inbox.mkdir(exist_ok=True)
    tmp = inbox / (request_id + ".tmp")
    tmp.write_text(json.dumps({"id": request_id, "token": token, "op": "python", "code": code}))
    tmp.rename(inbox / (request_id + ".json"))
    deadline = time.time() + timeout
    answer = outbox / (request_id + ".json")
    while time.time() < deadline:
        if answer.exists():
            try:
                data = json.loads(answer.read_text())
                answer.unlink()
                return data
            except (OSError, ValueError):
                pass
        time.sleep(0.25)
    return None


# ---------- operations ----------

def op_status(args):
    try:
        meminfo = {l.split(":")[0]: int(l.split()[1]) // 1024 for l in open("/proc/meminfo")}
    except OSError:
        meminfo = {}

    def rss(pid):
        try:
            for line in open(f"/proc/{pid}/status"):
                if line.startswith("VmRSS"):
                    return int(line.split()[1]) // 1024
        except OSError:
            return None

    return {
        "agent_version": VERSION,
        "editor": [{"pid": p, "memory_mb": rss(p)} for p, _ in editor_processes()],
        "editor_bridge_alive": bridge_alive(),
        "game": [{"pid": p, "memory_mb": rss(p)} for p, _ in game_processes()],
        "asset_processor": [p for p, _ in find_processes("AssetProcessor")],
        "build": dict(_build_state),
        "memory_free_mb": meminfo.get("MemAvailable"),
        "memory_total_mb": meminfo.get("MemTotal"),
        "memory_detail": {k: meminfo.get(k) for k in ("MemFree", "MemAvailable", "Cached", "Buffers", "Shmem",
                                                       "SwapTotal", "SwapFree", "Committed_AS")},
        "top_memory": top_memory(),
        "compilers_running": len(compilers_running()),
        "editor_launch_known": LAUNCH_FILE.exists(),
    }


def op_editor_start(args):
    if editor_processes():
        return {"ok": True, "note": "editor already running"}
    if not args.get("force"):
        if not wait_for_compilers(int(args.get("compiler_wait", 180))):
            return {"ok": False, "error": "a build is still compiling; not opening the editor on top of it"}
        free = meminfo().get("MemAvailable", 0)
        if free < int(args.get("min_free_mb", 1800)):
            return {"ok": False, "error": f"only {free} MB free; the editor needs about 1.8 GB",
                    "top_memory": top_memory()}
    launch = None
    if LAUNCH_FILE.exists():
        try:
            launch = json.loads(LAUNCH_FILE.read_text())
        except ValueError:
            launch = None
    launch = launch or default_editor_launch()
    other = args.get("project_path")
    if other and launch:
        other_path = Path(other).resolve()
        if other_path.parent != PROJECTS_ROOT.resolve() or not (other_path / "project.json").is_file():
            return {"ok": False, "error": "not a project in the projects folder"}
        launch = dict(launch)
        launch["args"] = [a for a in launch["args"] if not a.startswith("--project-path")] + [f"--project-path={other_path}"]
        launch["cwd"] = str(other_path)
    if not launch:
        return {"ok": False, "error": "don't know where the editor is yet: open it once from the Project Manager"}
    env = session_env(launch.get("env", {}))
    out = open(HOME / "editor_stdout.log", "ab")
    proc = subprocess.Popen(own_scope(launch["args"]), cwd=launch.get("cwd") or str(PROJECT), env=env,
                            stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    log(f"editor started, pid {proc.pid}")
    wait = float(args.get("wait_seconds", 0))
    deadline = time.time() + wait
    while wait and time.time() < deadline:
        if bridge_alive(5):
            return {"ok": True, "pid": proc.pid, "bridge": "ready"}
        if proc.poll() is not None:
            return {"ok": False, "pid": proc.pid, "error": f"editor exited with code {proc.returncode}",
                    "log": tail(LOG_DIR / "Editor.log", 40)}
        time.sleep(1)
    return {"ok": True, "pid": proc.pid, "bridge": "ready" if bridge_alive(5) else "starting"}


def op_editor_stop(args):
    procs = editor_processes()
    if not procs:
        return {"ok": True, "note": "editor not running"}
    saved = None
    if args.get("save", True) and bridge_alive():
        result = bridge_python(
            "import azlmbr.legacy.general as g\n"
            "if g.is_in_game_mode(): g.exit_game_mode()\n", 30)
        time.sleep(1.0)
        result = bridge_python("import azlmbr.legacy.general as g\ng.save_level()\nprint('saved')", 60)
        saved = bool(result and "saved" in json.dumps(result))
    for pid, _ in procs:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    deadline = time.time() + float(args.get("timeout", 30))
    while time.time() < deadline and editor_processes():
        time.sleep(0.5)
    forced = False
    for pid, _ in editor_processes():
        forced = True
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    log(f"editor stopped (saved={saved}, forced={forced})")
    return {"ok": True, "saved": saved, "forced": forced}


def find_cmake():
    found = shutil.which("cmake")
    if found:
        return found
    for pattern in ("~/.o3de/3rdParty/cmake-*/bin/cmake", "/var/opt/O3DE/*/cmake/runtime/bin/cmake"):
        hits = sorted(glob.glob(os.path.expanduser(pattern)), reverse=True)
        if hits:
            return hits[0]
    return None


def op_build(args):
    if not _build_lock.acquire(blocking=False):
        return {"ok": False, "error": "a build is already running", "build": dict(_build_state)}
    try:
        reopen = bool(editor_processes()) and args.get("reopen_editor", True)
        if editor_processes():
            if not args.get("stop_editor", True):
                return {"ok": False, "error": "the editor is open; close it first or pass stop_editor"}
            op_editor_stop({"save": True})
        for pid, _ in game_processes():
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass
        cmake = find_cmake()
        if not cmake:
            return {"ok": False, "error": "cmake not found"}
        if _engine_state.get("running"):
            return {"ok": False, "error": "the engine is building; game builds wait until it's done or stopped"}
        wait_for_compilers(240)
        jobs = str(safe_jobs(args.get("jobs") or hearth_setting("laptop", "buildJobs", 2, int)))
        high, cap = build_caps()
        cmd = limited([cmake, "--build", "build/linux", "--target", *BUILD_TARGETS, "--config", "profile", "-j", jobs],
                      high, cap)
        _build_state.update(running=True, started=time.time(), finished=None, exit_code=None)
        with open(BUILD_LOG, "wb") as out:
            code = 0
            if args.get("reconfigure"):
                configure = [cmake, "-S", ".", "-B", "build/linux"]
                log("configure: " + " ".join(configure))
                out.write(b"== configure ==\n"); out.flush()
                code = subprocess.call(configure, cwd=str(PROJECT), stdout=out, stderr=subprocess.STDOUT)
            if code == 0:
                log("build started: " + " ".join(cmd))
                out.write(b"== build ==\n"); out.flush()
                code = subprocess.call(cmd, cwd=str(PROJECT), stdout=out, stderr=subprocess.STDOUT)
        _build_state.update(running=False, finished=time.time(), exit_code=code)
        log(f"build finished with code {code}")
        text = tail(BUILD_LOG, 400)
        errors = [l for l in text.splitlines() if "error" in l.lower() and "werror" not in l.lower()][:40]
        result = {"ok": code == 0, "exit_code": code, "seconds": round(_build_state["finished"] - _build_state["started"]),
                  "errors": errors, "log_tail": "\n".join(text.splitlines()[-25:])}
        steps = [l for l in text.splitlines() if l.startswith("[") and "/" in l.split("]")[0]]
        result["steps"] = steps[-1].split("]")[0].strip("[") if steps else None
        if code == 0 and args.get("reconfigure"):
            save_stats(pending_rebuild=False)
        save_stats(last_build={"ok": code == 0, "finished": _build_state["finished"], "steps": result["steps"],
                               "seconds": result["seconds"], "errors": (result.get("errors") or [])[:5],
                               "log": str(BUILD_LOG)})
        if reopen and code == 0:
            result["editor"] = op_editor_start({"wait_seconds": args.get("wait_seconds", 240)})
        return result
    finally:
        _build_state["running"] = False
        _build_lock.release()


def op_game_start(args):
    global _game_proc
    if game_processes():
        return {"ok": True, "note": "game already running"}
    if not LAUNCHER.exists():
        return {"ok": False, "error": f"no game launcher at {LAUNCHER}; build first"}
    level = args.get("level", "levels/level0/level0.spawnable")
    cmd = [str(LAUNCHER), f"--project-path={PROJECT}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/LoadLevel={level}"]
    if args.get("fullscreen", True):
        cmd.append("--regset=/O3DE/Autoexec/ConsoleCommands/r_fullscreen=true")
    else:
        cmd.append(f"--regset=/O3DE/Autoexec/ConsoleCommands/r_width={int(args.get('width', 1600))}")
        cmd.append(f"--regset=/O3DE/Autoexec/ConsoleCommands/r_height={int(args.get('height', 900))}")
    for item in args.get("extra") or []:
        if isinstance(item, str) and item.startswith("--regset=/NoclipProtocol/"):
            cmd.append(item)
    launch = {}
    if LAUNCH_FILE.exists():
        try:
            launch = json.loads(LAUNCH_FILE.read_text())
        except ValueError:
            launch = {}
    env = session_env(launch.get("env", {}))
    out = open(HOME / "game_stdout.log", "ab")
    _game_proc = subprocess.Popen(own_scope(cmd), cwd=str(PROJECT), env=env, stdout=out, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, start_new_session=True)
    log(f"game started, pid {_game_proc.pid}")
    time.sleep(float(args.get("wait_seconds", 3)))
    if _game_proc.poll() is not None:
        return {"ok": False, "error": f"game exited with code {_game_proc.returncode}", "log": tail(HOME / "game_stdout.log", 40)}
    return {"ok": True, "pid": _game_proc.pid, "command": cmd}


_coop = {"host": None, "window2": None, "port": 33450, "started": None, "settings": {}}
COOP_LOG = HOME / "coop_host.log"
COOP_LOG2 = HOME / "coop_window2.log"


def lan_address():
    """This computer's address on the local network (for the co-op invite)."""
    import socket
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("192.168.1.1", 9))  # no packet is sent; this only picks the outgoing interface
        address = probe.getsockname()[0]
        probe.close()
        return address
    except OSError:
        return "127.0.0.1"


def _coop_cmd(role, args, port):
    level = args.get("level", "levels/level0/level0.spawnable")
    cmd = [str(LAUNCHER), f"--project-path={PROJECT}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/LoadLevel={level}" if role == "host" else f"--regset=/O3DE/Autoexec/ConsoleCommands/connect=127.0.0.1:{port}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/r_width={int(args.get('width', 1280))}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/r_height={int(args.get('height', 720))}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/net_FakeLatencyMs={int(args.get('lag_ms', 0))}",
           f"--regset=/O3DE/Autoexec/ConsoleCommands/net_FakeLossPercent={int(args.get('loss_percent', 0))}"]
    if role == "host":
        cmd += [f"--regset=/O3DE/Autoexec/ConsoleCommands/sv_port={port}", "--regset=/O3DE/Autoexec/ConsoleCommands/host="]
    seed = str(args.get("seed", "")).strip()
    if seed.isdigit():
        cmd.append(f"--regset=/NoclipProtocol/Level0/Override/seed={seed}")
        cmd.append("--regset=/NoclipProtocol/Level0/Override/newSeedEachRun=false")
    if args.get("monsters") is False:
        cmd.append("--regset=/NoclipProtocol/Monsters/Enabled=false")
    return cmd


def op_coop_start(args):
    """Co-op test: this computer hosts; optionally a second game window on this computer joins it."""
    if not LAUNCHER.exists():
        return {"ok": False, "error": f"no game launcher at {LAUNCHER}; build first"}
    gems = set(json.loads(PROJECT_JSON.read_text()).get("gem_names", []))
    if "Multiplayer" not in gems:
        return {"ok": False, "error": "the Multiplayer engine part is off: turn it on in the Hub and rebuild"}
    op_coop_stop({})
    port = int(args.get("port", 33450))
    launch = {}
    if LAUNCH_FILE.exists():
        try:
            launch = json.loads(LAUNCH_FILE.read_text())
        except ValueError:
            launch = {}
    env = session_env(launch.get("env", {}))
    with open(COOP_LOG, "wb"):
        pass
    _coop["host"] = subprocess.Popen(own_scope(_coop_cmd("host", args, port)), cwd=str(PROJECT), env=env,
                                     stdout=open(COOP_LOG, "ab"), stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                     start_new_session=True)
    _coop.update(port=port, started=time.time(), settings=dict(args))
    log(f"co-op host started, pid {_coop['host'].pid}, port {port}")
    if args.get("second_window", True):
        time.sleep(float(args.get("join_delay", 8)))
        with open(COOP_LOG2, "wb"):
            pass
        _coop["window2"] = subprocess.Popen(own_scope(_coop_cmd("client", args, port)), cwd=str(PROJECT), env=env,
                                            stdout=open(COOP_LOG2, "ab"), stderr=subprocess.STDOUT,
                                            stdin=subprocess.DEVNULL, start_new_session=True)
        log(f"co-op second window started, pid {_coop['window2'].pid}")
    return {"ok": True, "invite": f"connect {lan_address()}:{port}", "port": port}


def op_coop_status(args):
    """Who is in the co-op test, and the join log."""
    import re
    def alive(proc):
        return proc is not None and proc.poll() is None
    players = []
    if alive(_coop["host"]):
        players.append({"name": "You", "where": "This computer · host", "state": "ready", "pid": _coop["host"].pid,
                        "mb": rss_mb(_coop["host"].pid)})
    if alive(_coop["window2"]):
        players.append({"name": "Window 2", "where": "This computer · second game window", "state": "ready",
                        "pid": _coop["window2"].pid, "mb": rss_mb(_coop["window2"].pid)})
    lines = []
    remote = {}
    text = tail(COOP_LOG, 400) if COOP_LOG.exists() else ""
    for line in text.splitlines():
        low = line.lower()
        if any(word in low for word in ("connect", "join", "accept", "host", "disconnect", "loading", "level")):
            lines.append(line.strip()[-160:])
            for address in re.findall(r"(\d+\.\d+\.\d+\.\d+)", line):
                if not address.startswith("127.") and not address.startswith("0."):
                    remote[address] = "left" if "disconnect" in low else "ready"
    for address, state in remote.items():
        players.append({"name": address, "where": "Another computer", "state": state})
    return {"ok": True, "running": alive(_coop["host"]), "players": players, "log": lines[-30:],
            "invite": f"connect {lan_address()}:{_coop['port']}", "started": _coop["started"],
            "settings": _coop["settings"], "logs": [str(COOP_LOG), str(COOP_LOG2)]}


def op_coop_stop(args):
    stopped = []
    for key in ("window2", "host"):
        proc = _coop.get(key)
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            stopped.append(key)
        _coop[key] = None
    return {"ok": True, "stopped": stopped}


def op_game_stop(args):
    procs = game_processes()
    for pid, _ in procs:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    time.sleep(2)
    for pid, _ in game_processes():
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    return {"ok": True, "stopped": [p for p, _ in procs]}


def op_crash_report(args):
    report = {}
    if shutil.which("coredumpctl"):
        listing = subprocess.run(["coredumpctl", "--no-pager", "list"], capture_output=True, text=True, timeout=30)
        lines = [l for l in listing.stdout.splitlines() if "Editor" in l or "GameLauncher" in l or "AssetProc" in l]
        report["coredumps"] = lines[-8:]
        pids = [tok for tok in (lines[-1].split() if lines else []) if tok.isdigit()]
        if pids:
            info = subprocess.run(["coredumpctl", "--no-pager", "info", pids[0]],
                                  capture_output=True, text=True, timeout=60)
            report["latest_info"] = "\n".join(info.stdout.splitlines()[:120])
    else:
        report["coredumps"] = "coredumpctl not available"
    backups = sorted(glob.glob(str(LOG_DIR / "LogBackups" / "*")), key=os.path.getmtime)
    if backups:
        latest = backups[-1]
        if os.path.isdir(latest):
            logs = sorted(glob.glob(os.path.join(latest, "*Editor*.log")) or glob.glob(os.path.join(latest, "*.log")))
            latest = logs[-1] if logs else latest
        report["previous_editor_log"] = {"file": latest, "tail": tail(latest, int(args.get("lines", 80)))}
    report["editor_stdout_tail"] = tail(HOME / "editor_stdout.log", 40)
    return report


def op_logs(args):
    which = args.get("which", "editor")
    files = {"editor": LOG_DIR / "Editor.log", "game": LOG_DIR / "Game.log", "build": BUILD_LOG,
             "agent": AGENT_LOG, "asset_processor": LOG_DIR / "AP_GUI.log",
             "editor_stdout": HOME / "editor_stdout.log", "game_stdout": HOME / "game_stdout.log"}
    if which not in files:
        return {"ok": False, "error": f"unknown log; choose from {sorted(files)}"}
    return {"file": str(files[which]), "tail": tail(files[which], int(args.get("lines", 80)))}


def op_probe(args):
    """What this machine has: CPU, memory, disk, Python and Qt, browsers."""
    disk = shutil.disk_usage(str(PROJECT))
    found = {name: shutil.which(name) for name in
             ("firefox", "chromium", "google-chrome", "brave-browser", "microsoft-edge", "xdg-open",
              "cmake", "ninja", "ccache", "clang", "gcc", "coredumpctl", "nvidia-smi")}
    qt = subprocess.run([sys.executable, "-c", "import PySide6; print(PySide6.__version__)"],
                        capture_output=True, text=True)
    try:
        model = [l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")][0]
    except (OSError, IndexError):
        model = None
    gpu = subprocess.run(["lspci"], capture_output=True, text=True).stdout if shutil.which("lspci") else ""
    return {"cpu": model, "cpu_count": os.cpu_count(), "disk_free_gb": round(disk.free / 2**30, 1),
            "python": sys.version.split()[0], "system_pyside6": qt.stdout.strip() or None, "tools": found,
            "gpu": [l for l in gpu.splitlines() if "VGA" in l or "3D" in l],
            "swap": [l for l in open("/proc/meminfo") if l.startswith(("SwapTotal", "SwapFree"))]}


def op_parts(args):
    """Engine parts (gems) the project turns on."""
    data = json.loads(PROJECT_JSON.read_text())
    return {"enabled": data.get("gem_names", [])}


def op_set_parts(args):
    """Turn engine parts on or off: args enable [...] and disable [...]. Keeps a backup of project.json."""
    data = json.loads(PROJECT_JSON.read_text())
    gems = list(data.get("gem_names", []))
    protected = {"NoclipProtocol", "Atom", "EditorPythonBindings", "PrefabBuilder", "PrimitiveAssets"}
    for name in args.get("disable", []):
        if name in protected:
            return {"ok": False, "error": f"{name} can't be turned off: the project needs it"}
        if name in gems:
            gems.remove(name)
    for name in args.get("enable", []):
        if name not in gems:
            gems.append(name)
    backup = HOME / f"project.json.{time.strftime('%Y%m%d-%H%M%S')}.bak"
    shutil.copy2(PROJECT_JSON, backup)
    data["gem_names"] = sorted(gems, key=str.lower)
    PROJECT_JSON.write_text(json.dumps(data, indent=4) + "\n")
    log(f"parts changed: +{args.get('enable', [])} -{args.get('disable', [])}")
    return {"ok": True, "enabled": data["gem_names"], "backup": str(backup),
            "next": "run build with reconfigure true"}


def load_stats():
    try:
        return json.loads(STATS_FILE.read_text())
    except (OSError, ValueError):
        return {}


def save_stats(**values):
    stats = load_stats()
    stats.update(values)
    STATS_FILE.write_text(json.dumps(stats, indent=2))


def rss_mb(pid):
    try:
        for line in open(f"/proc/{pid}/status"):
            if line.startswith("VmRSS"):
                return int(line.split()[1]) // 1024
    except OSError:
        return None


_seen_editor = {"pid": None}
CRASH_HINTS = ("Segmentation fault", "SIGSEGV", "SIGABRT", "Assert", "assert", "Exception", "Fatal", "fatal error",
               "terminate called", "core dumped")


def check_editor_exit():
    """When the editor we saw goes away, decide whether it crashed and remember what we know for the Hub."""
    editors = editor_processes()
    pid = editors[0][0] if editors else None
    old = _seen_editor["pid"]
    _seen_editor["pid"] = pid
    if not old or pid == old or pid:
        return
    crashed = False
    dump = None
    if shutil.which("coredumpctl"):
        try:
            listing = subprocess.run(["coredumpctl", "--no-pager", "list", str(old)], capture_output=True, text=True,
                                     timeout=20)
            if listing.returncode == 0 and str(old) in listing.stdout:
                crashed, dump = True, listing.stdout.splitlines()[-1].strip()
        except (OSError, subprocess.SubprocessError):
            pass
    text = tail(LOG_DIR / "Editor.log", 60)
    lines = text.splitlines()
    hints = [l.strip() for l in lines if any(h in l for h in CRASH_HINTS)]
    if not shutil.which("coredumpctl") and hints and not any("shutdown" in l.lower() for l in lines[-5:]):
        crashed = True  # no crash list on this system: go by the end of the editor's log
    if not crashed:
        return
    during = None
    for line in reversed(lines):
        low = line.lower()
        if "game mode" in low or "entering game" in low:
            during = "while entering Play"
            break
        if "saving" in low:
            during = "while saving"
            break
    level = last_level()
    backup = None
    if level:
        folders = sorted(glob.glob(str(PROJECT / "Levels" / level / "_autobackup" / "*")), key=os.path.getmtime)
        if folders:
            backup = {"path": folders[-1], "when": os.path.getmtime(folders[-1])}
    save_stats(last_crash={"when": time.time(), "pid": old, "during": during, "level": level, "dump": dump,
                           "lines": (hints or lines[-6:])[-6:], "log": str(LOG_DIR / "Editor.log"), "backup": backup})
    log(f"editor {old} closed unexpectedly ({during or 'no hint'})")


def track_memory():
    """Remember the highest memory the editor and the game reach (per run), for the Hub's cards."""
    stats = load_stats()
    changed = False
    for key, procs in (("editor_memory", editor_processes()), ("game_memory", game_processes())):
        for pid, _ in procs[:1]:
            mb = rss_mb(pid)
            if not mb:
                continue
            old = stats.get(key) or {}
            peak = max(mb, old.get("peak_mb", 0)) if old.get("pid") == pid else mb
            if peak != old.get("peak_mb") or old.get("pid") != pid:
                stats[key] = {"peak_mb": peak, "pid": pid, "when": time.time()}
                changed = True
    if changed:
        STATS_FILE.write_text(json.dumps(stats, indent=2))


# The engine parts the Hub lists, in the Hub's order. gems: what each switch turns on or off.
PART_CATALOGUE = [
    {"name": "Atom renderer", "about": "Lighting, fog, bloom, color grading", "gems": ["Atom"], "locked": True},
    {"name": "PhysX", "about": "Player movement and collisions", "gems": ["PhysX5"]},
    {"name": "MiniAudio", "about": "Hum, crackles, footsteps", "gems": ["MiniAudio"]},
    {"name": "Prefabs and spawning", "about": "The endless level's tile pieces", "gems": ["PrefabBuilder"], "locked": True},
    {"name": "Multiplayer", "about": "Co-op squads", "gems": ["Multiplayer"], "note": "Turn on for co-op"},
    {"name": "UI (LyShine)", "about": "Built-in menu system", "gems": ["LyShine"], "note": "We draw our own UI"},
    {"name": "Terrain", "about": "Outdoor landscapes", "gems": ["Terrain"], "note": "Not needed yet"},
    {"name": "Vegetation", "about": "Grass and trees", "gems": ["Vegetation"], "note": "Not needed yet"},
    {"name": "Script Canvas", "about": "Visual scripting", "gems": ["ScriptCanvas"], "note": "Code lives in C++"},
    {"name": "Cloth", "about": "Cloth simulation", "gems": ["NvCloth"], "note": "Not needed yet"},
    {"name": "Profiler", "about": "Frame timings for the Profiler window", "gems": ["Profiler"], "note": "Turn on to profile"},
]


def engine_info():
    launch = {}
    try:
        launch = json.loads(LAUNCH_FILE.read_text())
    except (OSError, ValueError):
        pass
    editor = (launch.get("args") or [None])[0]
    if editor:
        root = Path(editor).resolve().parents[4]
        for name in ("engine.json",):
            try:
                data = json.loads((root / name).read_text())
                return {"o3de_version": data.get("display_version") or data.get("O3DEVersion") or data.get("version"),
                        "engine_root": str(root)}
            except (OSError, ValueError):
                pass
    return {"o3de_version": None, "engine_root": None}


def last_level():
    text = tail(LOG_DIR / "Editor.log", 4000)
    for line in reversed(text.splitlines()):
        if "Successfully opened document" in line:
            return Path(line.split("document", 1)[1].strip()).stem
    return None


def op_hub_info(args):
    data = json.loads(PROJECT_JSON.read_text())
    gems = set(data.get("gem_names", []))
    parts = [{**p, "on": all(g in gems for g in p["gems"])} for p in PART_CATALOGUE]
    stats = load_stats()
    try:
        launch = json.loads(LAUNCH_FILE.read_text())
    except (OSError, ValueError):
        launch = {}
    editors = editor_processes()
    games = game_processes()
    return {
        "agent_version": VERSION,
        "engine": engine_info(),
        "project": {"name": data.get("display_name") or data.get("project_name"), "path": str(PROJECT),
                    "level": last_level(), "last_opened": launch.get("seen")},
        "running": {"editor": bool(editors), "game": bool(games)},
        "build": {**_build_state, "last": stats.get("last_build")},
        "memory": {"editor_now_mb": rss_mb(editors[0][0]) if editors else None,
                   "game_now_mb": rss_mb(games[0][0]) if games else None,
                   "editor_peak": stats.get("editor_memory"), "game_peak": stats.get("game_memory")},
        "parts": parts, "parts_on": sum(p["on"] for p in parts), "parts_total": len(parts),
        "pending_rebuild": stats.get("pending_rebuild", False),
        "last_crash": stats.get("last_crash"),
        "free_mb": meminfo().get("MemAvailable"),
    }


def op_set_part(args):
    """Hub switch: turn one catalogue part on or off (needs a rebuild afterwards)."""
    part = next((p for p in PART_CATALOGUE if p["name"] == args.get("name")), None)
    if not part:
        return {"ok": False, "error": "unknown part"}
    if part.get("locked"):
        return {"ok": False, "error": f"{part['name']} can't be turned off: the project needs it"}
    key = "enable" if args.get("on") else "disable"
    result = op_set_parts({key: part["gems"]})
    if result.get("ok"):
        save_stats(pending_rebuild=True)
    return result


def op_new_project(args):
    """Create a new O3DE project next to this one, from the engine's default template."""
    import re
    name = str(args.get("name", ""))
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{1,40}", name):
        return {"ok": False, "error": "use letters, digits and _ (start with a letter)"}
    target = PROJECTS_ROOT / name
    if target.exists():
        return {"ok": False, "error": "a project with that name already exists"}
    root = engine_info().get("engine_root")
    script = Path(root) / "scripts" / "o3de.sh" if root else None
    if not script or not script.exists():
        return {"ok": False, "error": "can't find the engine's o3de.sh"}
    run = subprocess.run([str(script), "create-project", "--project-path", str(target)],
                         capture_output=True, text=True, timeout=600)
    log(f"new project {name}: exit {run.returncode}")
    return {"ok": run.returncode == 0, "path": str(target), "output": (run.stdout + run.stderr)[-2000:]}


def op_switch_project(args):
    """Hub "Open project": restart this agent on another project in the projects folder (answers first)."""
    target = Path(str(args.get("project_path", ""))).expanduser().resolve()
    if not (target / "project.json").is_file():
        return {"ok": False, "error": "that folder isn't a project"}
    if target == PROJECT.resolve():
        return {"ok": True, "note": "already on this project"}
    if _build_state["running"]:
        return {"ok": False, "error": "a build is running; switch when it's done"}

    def restart():
        time.sleep(0.5)
        log(f"switching to project {target}")
        me = Path(__file__).resolve()
        argv = [a for i, a in enumerate(sys.argv[1:]) if a != "--project" and (i == 0 or sys.argv[i] != "--project")]
        os.execv(sys.executable, [sys.executable, str(me), "--project", str(target)] + argv)

    threading.Thread(target=restart, daemon=True).start()
    return {"ok": True, "project": str(target)}


def op_dismiss_crash(args):
    """Hub: the crash card was dealt with (reopened or handed to Claude)."""
    stats = load_stats()
    if stats.get("last_crash"):
        stats["last_crash"]["dismissed"] = time.time()
        STATS_FILE.write_text(json.dumps(stats, indent=2))
    return {"ok": True}


def op_list_projects(args):
    projects = []
    for folder in sorted(PROJECTS_ROOT.iterdir()):
        project_file = folder / "project.json"
        if project_file.is_file():
            try:
                data = json.loads(project_file.read_text())
            except ValueError:
                continue
            projects.append({"name": data.get("display_name") or data.get("project_name"), "path": str(folder),
                             "current": folder.resolve() == PROJECT.resolve()})
    return {"projects": projects}


_engine_lock = threading.Lock()
_engine_state = {"running": False, "step": None, "started": None, "finished": None, "exit_code": None}


def _engine_run(steps):
    """Run engine setup steps one after another in the background, logging to engine_build.log."""
    try:
        with open(ENGINE_LOG, "ab") as out:
            for name, cmd, cwd, env in steps:
                _engine_state["step"] = name
                out.write(f"\n== {name}: {' '.join(cmd)} ==\n".encode()); out.flush()
                log(f"engine {name}: {' '.join(cmd)}")
                code = subprocess.call(cmd, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
                _engine_state["exit_code"] = code
                if code != 0:
                    log(f"engine {name} failed with code {code}")
                    return
            log("engine steps finished")
    finally:
        _engine_state.update(running=False, finished=time.time())
        _engine_lock.release()


def op_engine(args):
    """Hearth Engine on this machine. args action: status | setup (download) | update (pull) | build."""
    action = args.get("action", "status")
    if action == "status":
        progress = None
        text = tail(ENGINE_LOG, 40)
        for line in reversed(text.splitlines()):
            if line.startswith("[") and "/" in line.split("]")[0]:
                progress = line.split("]")[0].strip("[")
                break
        git_lfs = subprocess.run(["git", "lfs", "version"], capture_output=True, text=True).returncode == 0
        disk = shutil.disk_usage(str(Path.home()))
        return {"state": dict(_engine_state), "progress": progress, "engine_dir": str(ENGINE_DIR),
                "downloaded": (ENGINE_DIR / "engine.json").exists(), "git_lfs": git_lfs,
                "disk_free_gb": round(disk.free / 2**30, 1), "log_tail": "\n".join(text.splitlines()[-12:])}
    if action == "stop":
        stopped = []
        for name in ("ninja", "cmake", "clang", "ccache", "mold"):
            for pid, cmd in find_processes(name):
                if str(ENGINE_DIR) in " ".join(cmd) or os.path.realpath(f"/proc/{pid}/cwd").startswith(str(ENGINE_DIR)):
                    try:
                        os.kill(pid, signal.SIGTERM)
                        stopped.append(pid)
                    except OSError:
                        pass
        log(f"engine build stopped: {stopped}")
        return {"ok": True, "stopped": stopped}
    if not _engine_lock.acquire(blocking=False):
        return {"ok": False, "error": "engine work already running", "state": dict(_engine_state)}
    env = dict(os.environ)
    env["GIT_TERMINAL_PROMPT"] = "0"
    steps = []
    if action == "setup":
        if (ENGINE_DIR / "engine.json").exists():
            _engine_lock.release()
            return {"ok": True, "note": "already downloaded; use update"}
        ENGINE_DIR.parent.mkdir(parents=True, exist_ok=True)
        steps.append(("download", ["git", "clone", "--depth", "1", "--branch", ENGINE_BRANCH, ENGINE_REPO,
                                   str(ENGINE_DIR)], str(ENGINE_DIR.parent), env))
        steps.append(("lfs files", ["git", "lfs", "pull"], str(ENGINE_DIR), env))
    elif action == "update":
        steps.append(("update", ["git", "pull", "--ff-only"], str(ENGINE_DIR), env))
        steps.append(("lfs files", ["git", "lfs", "pull"], str(ENGINE_DIR), env))
    elif action == "build":
        if editor_processes():
            if not args.get("stop_editor", True):
                _engine_lock.release()
                return {"ok": False, "error": "close the editor before an engine build"}
            op_editor_stop({"save": True})
        wait_for_compilers(240)
        build_env = dict(env)
        build_env["HEARTH_JOBS"] = str(safe_jobs(args.get("jobs", 4)))
        high, cap = build_caps()
        steps.append(("build", limited(["bash", str(ENGINE_DIR / "scripts" / "hearth" / "build-laptop.sh"), str(PROJECT)],
                                       high, cap), str(ENGINE_DIR), build_env))
    else:
        _engine_lock.release()
        return {"ok": False, "error": "action must be status, setup, update, build or stop"}
    if args.get("then_build") and action in ("setup", "update"):
        if editor_processes():
            op_editor_stop({"save": True})
        build_env = dict(env)
        build_env["HEARTH_JOBS"] = str(safe_jobs(args.get("jobs", 4)))
        high, cap = build_caps()
        steps.append(("build", limited(["bash", str(ENGINE_DIR / "scripts" / "hearth" / "build-laptop.sh"), str(PROJECT)],
                                       high, cap), str(ENGINE_DIR), build_env))
    _engine_state.update(running=True, step=None, started=time.time(), finished=None, exit_code=None)
    threading.Thread(target=_engine_run, args=(steps,), daemon=True).start()
    return {"ok": True, "started": [name for name, *_ in steps], "log": str(ENGINE_LOG)}


# ---------- Level Generator (Hearth Engine Hub) ----------

def _levelgen_clean(values):
    """Only known settings, each the right type and in range."""
    out = {}
    for name, (_, _, default, _) in LEVELGEN_FIELDS.items():
        value = values.get(name, default)
        try:
            if isinstance(default, bool):
                value = bool(value)
            elif name == "seed":
                value = int(str(value).strip() or "1") % (1 << 64)
            elif isinstance(default, int):
                value = int(round(float(value)))
            else:
                value = float(value)
        except (TypeError, ValueError):
            value = default
        out[name] = value
    out["openness"] = min(1.0, max(0.0, out["openness"]))
    out["roomChance"] = min(1.0, max(0.0, out["roomChance"]))
    out["pillarChance"] = min(1.0, max(0.0, out["pillarChance"]))
    out["darkness"] = min(1.0, max(0.0, out["darkness"]))
    out["stains"] = min(1.0, max(0.0, out["stains"]))
    out["darkZoneSize"] = min(8.0, max(1.0, out["darkZoneSize"]))
    out["baseSize"] = min(5, max(0, out["baseSize"]))
    out["roomShifting"] = False  # not built yet
    return out


def _levelgen_text(values):
    v = _levelgen_clean(values)
    parts = [f"{key}={v[name]}" for name, (_, _, _, key) in LEVELGEN_FIELDS.items() if key]
    parts.append(f"enableFlicker={1 if v['flicker'] else 0}")
    parts.append(f"enableDying={1 if v['dying'] else 0}")
    return ";".join(parts)


def _level0_component(data):
    for entity in (data.get("Entities") or {}).values():
        for component in (entity.get("Components") or {}).values():
            template = component.get("m_template") if isinstance(component, dict) else None
            if isinstance(template, dict) and template.get("$type") == "EndlessLevelComponent":
                return template
    return None


def _seed_text(values):
    """Seeds are 64-bit: send them to the Hub as text (JavaScript numbers would round them)."""
    out = dict(values)
    out["seed"] = str(out["seed"])
    return out


def op_levelgen_get(args):
    """The Level 0 settings saved in the level."""
    try:
        data = json.loads(LEVEL0_PREFAB.read_text())
    except (OSError, ValueError) as error:
        return {"ok": False, "error": f"can't read the level: {error}"}
    component = _level0_component(data) or {}
    values = {name: component.get(field, default) for name, (field, _, default, _) in LEVELGEN_FIELDS.items()}
    return {"ok": True, "values": _seed_text(_levelgen_clean(values)), "level": "Level 0", "name": "The Lobby"}


def _levelgen_tool():
    """Builds the small level0gen tool from the game's own generator code when it is missing or out of date."""
    newest = max(p.stat().st_mtime for p in LEVELGEN_SOURCES)
    if LEVELGEN_TOOL.exists() and LEVELGEN_TOOL.stat().st_mtime >= newest:
        return None
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        return "no C++ compiler (clang++ or g++) found"
    sources = [str(p) for p in LEVELGEN_SOURCES if p.suffix == ".cpp"]
    tmp = LEVELGEN_TOOL.with_suffix(".new")
    run = subprocess.run(["nice", "-n", "10", compiler, "-std=c++17", "-O2", "-o", str(tmp)] + sources,
                         capture_output=True, text=True, timeout=180)
    if run.returncode != 0:
        return "level0gen didn't compile: " + run.stderr[-1500:]
    tmp.replace(LEVELGEN_TOOL)
    return None


def _levelgen_run(argv, timeout=60):
    problem = _levelgen_tool()
    if problem:
        return {"ok": False, "error": problem}
    run = subprocess.run([str(LEVELGEN_TOOL)] + argv, capture_output=True, text=True, timeout=timeout)
    if run.returncode != 0:
        return {"ok": False, "error": run.stderr[-800:] or f"exit code {run.returncode}"}
    try:
        return {"ok": True, "data": json.loads(run.stdout)}
    except ValueError:
        return {"ok": False, "error": "level0gen gave bad output"}


def op_levelgen_area(args):
    """Walls, pillars, lights and stains for a block of chunks (default 4 x 4 around spawn), from the real generator."""
    values = _levelgen_clean(args.get("values") or {})
    area = [int(args.get(k, d)) for k, d in (("minX", -2), ("minY", -2), ("maxX", 1), ("maxY", 1))]
    result = _levelgen_run(["area", _levelgen_text(values)] + [str(n) for n in area])
    if result.get("ok"):
        result["values"] = _seed_text(values)
    return result


def op_levelgen_tests(args):
    values = _levelgen_clean(args.get("values") or {})
    seeds = max(1, min(200, int(args.get("seeds", 50))))
    return _levelgen_run(["tests", _levelgen_text(values), str(seeds)], timeout=120)


def op_levelgen_apply(args):
    """Saves the settings into Level 0: through the open editor when it has the level, otherwise in the level file."""
    values = _levelgen_clean(args.get("values") or {})
    if editor_processes() and bridge_alive():
        pairs = {LEVELGEN_FIELDS[name][1]: values[name] for name in LEVELGEN_FIELDS if name != "roomShifting"}
        code = f"""
import azlmbr.bus as b, azlmbr.editor as ed, azlmbr.entity as ent, azlmbr.legacy.general as g
pairs = {json.dumps(pairs)}
if g.get_current_level_name().lower() != 'level0':
    print('NOT_LEVEL0')
else:
    tid = ed.EditorComponentAPIBus(b.Broadcast, 'FindComponentTypeIdsByEntityType', ['Endless Level'], ent.EntityType().Game)
    done = []
    for e in ed.ToolsApplicationRequestBus(b.Broadcast, 'GetAllEntities') or []:
        got = ed.EditorComponentAPIBus(b.Broadcast, 'GetComponentOfType', e, tid[0]) if tid else None
        if got and got.IsSuccess():
            comp = got.GetValue()
            paths = ed.EditorComponentAPIBus(b.Broadcast, 'BuildComponentPropertyList', comp)
            for label, value in pairs.items():
                for p in paths:
                    if p == label or p.endswith('|' + label):
                        ok = ed.EditorComponentAPIBus(b.Broadcast, 'SetComponentProperty', comp, p, value).IsSuccess()
                        done.append((label, ok))
                        break
    print('SET', done)
    print('SAVED', g.save_level())
"""
        answer = bridge_python(code, timeout=60)
        output = (answer or {}).get("output", "") if isinstance(answer, dict) else ""
        if "NOT_LEVEL0" not in output and "SAVED True" in output:
            return {"ok": True, "via": "editor", "values": _seed_text(values), "detail": output[-600:]}
        if editor_processes():
            return {"ok": False, "error": "the editor is open but couldn't apply the settings (is Level 0 open?)",
                    "detail": output[-600:]}
    try:
        data = json.loads(LEVEL0_PREFAB.read_text())
    except (OSError, ValueError) as error:
        return {"ok": False, "error": f"can't read the level: {error}"}
    component = _level0_component(data)
    if component is None:
        return {"ok": False, "error": "Level 0 has no Endless Level component"}
    for name, (field, _, default, _) in LEVELGEN_FIELDS.items():
        if values[name] == default:
            component.pop(field, None)
        else:
            component[field] = values[name]
    backup = LEVEL0_PREFAB.with_suffix(".prefab.bak")
    shutil.copy2(LEVEL0_PREFAB, backup)
    tmp = LEVEL0_PREFAB.with_suffix(".prefab.tmp")
    tmp.write_text(json.dumps(data, indent=4) + "\n")
    tmp.replace(LEVEL0_PREFAB)
    return {"ok": True, "via": "file", "values": _seed_text(values)}


def op_levelgen_play(args):
    """Starts the game on Level 0 with exactly the settings on screen (not saved to the level)."""
    values = _levelgen_clean(args.get("values") or {})
    extra = [f"--regset=/NoclipProtocol/Level0/Override={_levelgen_text(values)}"]
    return op_game_start({"fullscreen": bool(args.get("fullscreen", True)), "level": "Level0",
                          "width": args.get("width", 1600), "height": args.get("height", 900), "extra": extra})


OPS = {"engine": op_engine, "new_project": op_new_project, "hub_info": op_hub_info, "set_part": op_set_part, "list_projects": op_list_projects, "switch_project": op_switch_project, "dismiss_crash": op_dismiss_crash, "coop_start": op_coop_start, "coop_status": op_coop_status, "coop_stop": op_coop_stop,
       "probe": op_probe, "parts": op_parts, "set_parts": op_set_parts, "status": op_status, "editor_start": op_editor_start, "editor_stop": op_editor_stop,
       "build": op_build, "game_start": op_game_start, "game_stop": op_game_stop,
       "crash_report": op_crash_report, "logs": op_logs,
       "levelgen_get": op_levelgen_get, "levelgen_area": op_levelgen_area, "levelgen_tests": op_levelgen_tests,
       "levelgen_apply": op_levelgen_apply, "levelgen_play": op_levelgen_play}


# ---------- request loop ----------

def meminfo():
    try:
        return {l.split(":")[0]: int(l.split()[1]) // 1024 for l in open("/proc/meminfo")}
    except OSError:
        return {}


def top_memory(count=6):
    """The programs using the most memory right now (name and MB), to see what fills the RAM."""
    users = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            name = Path(f"/proc/{entry}/comm").read_text().strip()
            for line in open(f"/proc/{entry}/status"):
                if line.startswith("VmRSS"):
                    users.append((int(line.split()[1]) // 1024, name))
                    break
        except OSError:
            continue
    totals = {}
    for mb, name in users:
        totals[name] = totals.get(name, 0) + mb
    return sorted(((mb, name) for name, mb in totals.items()), reverse=True)[:count]


def compilers_running():
    return [pid for pid, _ in find_processes("clang") + find_processes("cc1plus") + find_processes("ninja")
            + find_processes("ld.mold") + find_processes("mold")]


def wait_for_compilers(timeout=180):
    """Wait until no compiler or linker is running (a stopped build's last jobs can keep going for a minute)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not compilers_running():
            return True
        time.sleep(2)
    return False


def safe_jobs(requested, per_job_mb=900, keep_free_mb=1800):
    """How many compile jobs fit in the memory that's free right now, keeping room for the desktop."""
    available = meminfo().get("MemAvailable", 0)
    fit = max(1, (available - keep_free_mb) // per_job_mb)
    return int(max(1, min(int(requested), fit)))


def hearth_setting(section, key, default, kind=str):
    """A value from the Hearth Engine settings (~/.config/hearth-engine/settings.ini, written by the Hub's
    first-run setup and Settings page). Never written here: the agent only reads what the owner chose."""
    import configparser
    path = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "hearth-engine" / "settings.ini"
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read(path)
        value = parser.get(section, key)
    except (configparser.Error, OSError):
        return default
    if kind is bool:
        return value.strip().lower() in ("true", "1", "yes", "on")
    try:
        return kind(value)
    except ValueError:
        return default


_paused_assets = set()


def pause_assets_for_game():
    """Laptop setting "Pause asset processing while a game runs": stop the asset processor while the standalone
    game runs, and let it carry on when the game closes."""
    want_pause = bool(game_processes()) and hearth_setting("laptop", "pauseAssetsWhileGaming", False, bool)
    names = ("AssetProcessor", "AssetBuilder")
    if want_pause:
        for fragment in names:
            for pid, _ in find_processes(fragment):
                if pid not in _paused_assets:
                    try:
                        os.kill(pid, signal.SIGSTOP)
                        _paused_assets.add(pid)
                        log(f"paused {fragment} {pid} while the game runs")
                    except OSError:
                        pass
    elif _paused_assets:
        for pid in list(_paused_assets):
            try:
                os.kill(pid, signal.SIGCONT)
            except OSError:
                pass
            _paused_assets.discard(pid)
        log("asset processing carries on (game closed)")


def limited(cmd, memory_high_mb, memory_max_mb):
    """Run a heavy job in its own systemd scope with a memory cap: past the cap it slows down or that one
    job fails, instead of the whole computer freezing."""
    if shutil.which("systemd-run"):
        return ["systemd-run", "--user", "--scope", "--quiet", "--collect",
                "-p", f"MemoryHigh={memory_high_mb}M", "-p", f"MemoryMax={memory_max_mb}M",
                "-p", "MemorySwapMax=1G", "--", "nice", "-n", "15"] + list(cmd)
    return ["nice", "-n", "15"] + list(cmd)


def build_caps():
    total = meminfo().get("MemTotal", 8000)
    return int(total * 0.45), int(total * 0.6)   # MemoryHigh, MemoryMax


def own_scope(cmd):
    """Run a program in its own systemd scope, so restarting the agent never takes it down."""
    if shutil.which("systemd-run") and os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"):
        return ["systemd-run", "--user", "--scope", "--quiet", "--collect", "--"] + list(cmd)
    return list(cmd)


def announce(token):
    """Tell the Hearth Engine Hub where this agent is: ~/.config/hearth-engine/agent.json (only this user can read it)."""
    config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "hearth-engine"
    try:
        config.mkdir(parents=True, exist_ok=True)
        path = config / "agent.json"
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps({"port": HTTP_PORT, "token": token, "project": str(PROJECT), "pid": os.getpid(),
                                   "version": VERSION, "started": time.time()}))
        os.chmod(tmp, 0o600)
        tmp.replace(path)
    except OSError as error:
        log(f"agent.json not written: {error}")


def install_desktop_entry():
    """Put Hearth Engine in the app menu: it opens the Hub."""
    apps = Path.home() / ".local" / "share" / "applications"
    entry = apps / "hearth-engine.desktop"
    if shutil.which("hearth-engine"):
        # The Hearth Engine .deb is installed: its own app menu entry opens the built-in Hub, so drop the old
        # web-page entry (a user entry would hide the installed one).
        try:
            if entry.exists() and "xdg-open http://127.0.0.1" in entry.read_text():
                entry.unlink()
                log("app menu entry: removed the old web Hub entry (the engine's Hub is installed)")
        except OSError as error:
            log(f"app menu entry not removed: {error}")
        return
    icon = HUB_DIR / "icon.svg"
    text = ("[Desktop Entry]\nType=Application\nName=Hearth Engine\nComment=Build, play and edit Noclip Protocol\n"
            f"Exec=xdg-open http://127.0.0.1:{HTTP_PORT}/\nIcon={icon}\nCategories=Development;Game;\nTerminal=false\n")
    try:
        if not entry.exists() or entry.read_text() != text:
            apps.mkdir(parents=True, exist_ok=True)
            entry.write_text(text)
            log("app menu entry: Hearth Engine")
    except OSError as error:
        log(f"app menu entry not written: {error}")


def fix_service_unit():
    """Make sure stopping or restarting the agent's service only stops the agent itself."""
    unit = Path.home() / ".config" / "systemd" / "user" / "hearth-engine-agent.service"
    try:
        text = unit.read_text()
    except OSError:
        return
    if "KillMode=process" in text:
        return
    unit.write_text(text.replace("RestartSec=3\n", "RestartSec=3\nKillMode=process\n"))
    subprocess.run(["systemctl", "--user", "daemon-reload"], capture_output=True)
    log("service unit: KillMode=process (the editor and game survive agent restarts)")


def session_env(extra):
    """Environment for starting graphical programs: what the editor last ran with, else sane defaults."""
    env = dict(os.environ)
    env.update(extra or {})
    uid = os.getuid()
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
    if "WAYLAND_DISPLAY" not in env:
        sockets = sorted(glob.glob(os.path.join(env["XDG_RUNTIME_DIR"], "wayland-[0-9]")))
        if sockets:
            env["WAYLAND_DISPLAY"] = os.path.basename(sockets[0])
    env.setdefault("DISPLAY", ":0")
    env["QT_QPA_PLATFORM"] = "xcb"  # Vulkan needs X11/XWayland on this machine
    return env


def handle(path, token):
    try:
        request = json.loads(path.read_text())
    except (OSError, ValueError):
        return
    finally:
        try:
            path.unlink()
        except OSError:
            pass
    request_id = str(request.get("id") or path.stem)
    if not secrets.compare_digest(str(request.get("token", "")), token):
        result = {"ok": False, "error": "bad token"}
    elif request.get("op") not in OPS:
        result = {"ok": False, "error": f"unknown op; choose from {sorted(OPS)}"}
    else:
        try:
            result = OPS[request["op"]](request.get("args") or {})
        except Exception:  # report, never die
            result = {"ok": False, "error": traceback.format_exc()}
    tmp = OUTBOX / (request_id + ".tmp")
    tmp.write_text(json.dumps({"id": request_id, "op": request.get("op"), "result": result,
                               "answered": time.time()}, indent=2, default=str))
    tmp.rename(OUTBOX / (request_id + ".json"))


class _Api:
    token = ""


def start_http(token):
    """Localhost-only API for the Hearth Engine Hub. Every call needs the token; Host must be local."""
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    _Api.token = token
    allowed_hosts = {f"127.0.0.1:{HTTP_PORT}", f"localhost:{HTTP_PORT}"}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, body, kind="application/json"):
            data = body if isinstance(body, bytes) else json.dumps(body, default=str).encode()
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.headers.get("Host") not in allowed_hosts:
                return self._send(403, {"error": "bad host"})
            path = self.path.split("?")[0]
            if path == "/cover":
                shots = sorted(SHOTS.glob("*.png"), key=lambda p: p.stat().st_mtime) if SHOTS.is_dir() else []
                if not shots:
                    return self._send(404, {"error": "no screenshots yet"})
                return self._send(200, shots[-1].read_bytes(), "image/png")
            if path in ("/", "/index.html"):
                path = "/index.html"
            target = (HUB_DIR / path.lstrip("/")).resolve()
            if not str(target).startswith(str(HUB_DIR.resolve())) or not target.is_file():
                return self._send(404, {"error": "not found"})
            body = target.read_bytes()
            if target.suffix == ".html":
                body = body.replace(b"__HEARTH_TOKEN__", _Api.token.encode())
            kinds = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css",
                     ".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml", ".woff2": "font/woff2"}
            self._send(200, body, kinds.get(target.suffix, "application/octet-stream"))

        def do_POST(self):
            if self.headers.get("Host") not in allowed_hosts or not secrets.compare_digest(
                    self.headers.get("X-Hearth-Token", ""), _Api.token):
                return self._send(403, {"error": "forbidden"})
            op = self.path.strip("/").split("/")[-1]
            if op not in OPS:
                return self._send(404, {"error": "unknown op"})
            length = int(self.headers.get("Content-Length") or 0)
            try:
                args = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                args = {}
            try:
                result = OPS[op](args)
            except Exception:
                result = {"ok": False, "error": traceback.format_exc()}
            self._send(200, result)

    try:
        server = ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), Handler)
    except OSError as error:
        log(f"hub API not started: {error}")
        return
    threading.Thread(target=server.serve_forever, daemon=True).start()
    log(f"hub API on http://127.0.0.1:{HTTP_PORT}/")


def main():
    for folder in (HOME, INBOX, OUTBOX):
        folder.mkdir(parents=True, exist_ok=True)
    lock = open(LOCK_FILE, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("Hearth Engine agent is already running.")
        return
    if not TOKEN_FILE.exists():
        TOKEN_FILE.write_text(secrets.token_hex(24))
    os.chmod(TOKEN_FILE, 0o600)
    token = TOKEN_FILE.read_text().strip()
    log(f"agent {VERSION} started, pid {os.getpid()}, project {PROJECT}")
    # If an earlier agent paused asset processing for a game and then stopped, let it carry on.
    for fragment in ("AssetProcessor", "AssetBuilder"):
        for pid, _ in find_processes(fragment):
            try:
                if open(f"/proc/{pid}/stat").read().split(")")[-1].split()[0] == "T":
                    os.kill(pid, signal.SIGCONT)
            except (OSError, IndexError):
                pass
    start_http(token)
    announce(token)
    fix_service_unit()
    install_desktop_entry()
    me = Path(__file__).resolve()
    my_mtime = me.stat().st_mtime
    last_seen = 0.0
    while True:
        now = time.time()
        if now - last_seen > 5:
            last_seen = now
            remember_editor_launch()
            try:
                track_memory()
            except Exception as error:
                log(f"memory tracking: {error}")
            try:
                check_editor_exit()
            except Exception as error:
                log(f"crash check: {error}")
            try:
                pause_assets_for_game()
            except Exception as error:
                log(f"asset pause: {error}")
            HEARTBEAT.write_text(json.dumps({"time": now, "pid": os.getpid(), "version": VERSION}))
            try:
                changed = me.stat().st_mtime != my_mtime
            except OSError:
                changed = False
            if changed and not _build_state["running"] and not _engine_state["running"]:
                time.sleep(1.0)  # let the new file finish landing
                try:
                    compile(me.read_text(), str(me), "exec")
                except SyntaxError as error:
                    log(f"new agent file doesn't compile, staying on {VERSION}: {error}")
                    my_mtime = me.stat().st_mtime
                else:
                    log("agent file changed, restarting into the new version")
                    lock.close()
                    os.execv(sys.executable, [sys.executable, str(me)] + sys.argv[1:])
        for path in sorted(INBOX.glob("*.json")):
            claimed = path.with_suffix(".working")
            try:
                path.rename(claimed)  # claim it so the next pass doesn't pick it up again
            except OSError:
                continue
            threading.Thread(target=handle, args=(claimed, token), daemon=True).start()
        time.sleep(0.5)


if __name__ == "__main__":
    main()
