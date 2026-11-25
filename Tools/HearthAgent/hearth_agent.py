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

PROJECT = Path(__file__).resolve().parents[2]
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
VERSION = "1.2"
PROJECT_JSON = PROJECT / "project.json"
HTTP_PORT = 47830
HUB_DIR = PROJECT / "Tools" / "HearthEngine" / "hub"

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
    candidates = sorted(glob.glob("/var/opt/O3DE/*/bin/Linux/profile/Default/Editor"), reverse=True)
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
        "editor_launch_known": LAUNCH_FILE.exists(),
    }


def op_editor_start(args):
    if editor_processes():
        return {"ok": True, "note": "editor already running"}
    launch = None
    if LAUNCH_FILE.exists():
        try:
            launch = json.loads(LAUNCH_FILE.read_text())
        except ValueError:
            launch = None
    launch = launch or default_editor_launch()
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
        jobs = str(args.get("jobs") or max(1, (os.cpu_count() or 2) - 1))
        cmd = [cmake, "--build", "build/linux", "--target", *BUILD_TARGETS, "--config", "profile", "-j", jobs]
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


OPS = {"probe": op_probe, "parts": op_parts, "set_parts": op_set_parts, "status": op_status, "editor_start": op_editor_start, "editor_stop": op_editor_stop,
       "build": op_build, "game_start": op_game_start, "game_stop": op_game_stop,
       "crash_report": op_crash_report, "logs": op_logs}


# ---------- request loop ----------

def own_scope(cmd):
    """Run a program in its own systemd scope, so restarting the agent never takes it down."""
    if shutil.which("systemd-run") and os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"):
        return ["systemd-run", "--user", "--scope", "--quiet", "--collect", "--"] + list(cmd)
    return list(cmd)


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
            if path in ("/", "/index.html"):
                path = "/index.html"
            target = (HUB_DIR / path.lstrip("/")).resolve()
            if not str(target).startswith(str(HUB_DIR.resolve())) or not target.is_file():
                return self._send(404, {"error": "not found"})
            body = target.read_bytes()
            if target.name == "index.html":
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
    start_http(token)
    fix_service_unit()
    me = Path(__file__).resolve()
    my_mtime = me.stat().st_mtime
    last_seen = 0.0
    while True:
        now = time.time()
        if now - last_seen > 5:
            last_seen = now
            remember_editor_launch()
            HEARTBEAT.write_text(json.dumps({"time": now, "pid": os.getpid(), "version": VERSION}))
            try:
                changed = me.stat().st_mtime != my_mtime
            except OSError:
                changed = False
            if changed and not _build_state["running"]:
                time.sleep(1.0)  # let the new file finish landing
                try:
                    compile(me.read_text(), str(me), "exec")
                except SyntaxError as error:
                    log(f"new agent file doesn't compile, staying on {VERSION}: {error}")
                    my_mtime = me.stat().st_mtime
                else:
                    log("agent file changed, restarting into the new version")
                    lock.close()
                    os.execv(sys.executable, [sys.executable, str(me)])
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
