"""
Claude bridge - in-editor side.

Runs inside the O3DE Editor (started by bootstrap.py). Listens on 127.0.0.1 only,
requires a per-session secret token, and handles requests on the editor's main
thread during its frame tick, so editor calls are safe.

Requests are one JSON object per line:
  {"token": "...", "id": 1, "op": "python",     "code": "print(1+1)"}
  {"token": "...", "id": 2, "op": "screenshot", "path": "/abs/path.png"}
  {"token": "...", "id": 3, "op": "console",    "command": "r_displayInfo 1"}
  {"token": "...", "id": 4, "op": "ping"}
Each gets one JSON line back: {"id": ..., "ok": true/false, "output": "...", "error": "..."}

The session's port and token are written to <project>/user/ClaudeBridge/session.json,
which only this user can read. The MCP server (Tools/ClaudeBridge/mcp_server.py) reads it.
"""

import contextlib
import io
import json
import os
import secrets
import socket
import time
import traceback

PREFERRED_PORT = 47821
BACKGROUND_UPDATE_MS = 100  # 10 updates a second while the editor is in the background
MAX_REQUEST_BYTES = 8 * 1024 * 1024

_state = None


def _log(message):
    print(f"[ClaudeBridge] {message}")
    # Also keep our own log file: prints made while the editor is still starting don't reach Editor.log.
    try:
        folder = os.path.join(_project_path(), "user", "ClaudeBridge")
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "bridge.log"), "a", encoding="utf-8") as log_file:
            log_file.write(time.strftime("%H:%M:%S ") + str(message) + "\n")
    except Exception:  # noqa: BLE001
        pass


def _running_in_editor():
    """bootstrap.py also runs in asset builders and other tools; only the Editor should host the bridge."""
    try:
        with open("/proc/self/comm", "r", encoding="utf-8") as comm:
            return comm.read().strip().lower().startswith("editor")
    except OSError:
        return True  # not Linux: assume Editor


def _project_path():
    import azlmbr.paths
    return azlmbr.paths.projectroot


class _Client:
    def __init__(self, sock, address):
        self.sock = sock
        self.address = address
        self.inbox = b""
        self.outbox = b""
        self.closed = False
        self.close_after_write = False


class _Bridge:
    def __init__(self):
        import azlmbr.bus as bus

        self.token = secrets.token_urlsafe(32)
        self.clients = []
        self.namespace = self._fresh_namespace()

        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.server.bind(("127.0.0.1", PREFERRED_PORT))
        except OSError:
            self.server.bind(("127.0.0.1", 0))  # preferred port busy: any free one
        self.server.listen(4)
        self.server.setblocking(False)
        self.port = self.server.getsockname()[1]

        try:
            self.handler = bus.NotificationHandler("TickBus")
            self.handler.connect()
            self.handler.add_callback("OnTick", self._on_tick)
        except Exception:
            self.server.close()
            raise
        # The editor stops updating when its window loses focus, which would freeze the bridge
        # whenever the user is looking at Claude. Keep it updating gently in the background.
        try:
            import azlmbr.legacy.general as general
            general.run_console(f"ed_backgroundUpdatePeriod {BACKGROUND_UPDATE_MS}")
            _log(f"background updates on ({BACKGROUND_UPDATE_MS} ms)")
        except Exception:  # noqa: BLE001
            _log("could not turn on background updates:\n" + traceback.format_exc())
        self.ticks = 0
        self.last_heartbeat = 0.0
        self.folder = os.path.join(_project_path(), "user", "ClaudeBridge")
        self.inbox = os.path.join(self.folder, "inbox")
        self.outbox_dir = os.path.join(self.folder, "outbox")
        os.makedirs(self.inbox, exist_ok=True)
        os.makedirs(self.outbox_dir, exist_ok=True)
        self._write_session_file()
        _log(f"listening on 127.0.0.1:{self.port}")

    # ------------------------------------------------------------------ setup

    def _fresh_namespace(self):
        namespace = {"__name__": "__claude__"}
        exec(  # noqa: S102 - this is the point of the bridge
            "import azlmbr\n"
            "import azlmbr.bus as bus\n"
            "import azlmbr.editor as editor\n"
            "import azlmbr.entity as entity\n"
            "import azlmbr.components as components\n"
            "import azlmbr.math as azmath\n"
            "import azlmbr.legacy.general as general\n",
            namespace)
        return namespace

    def _write_session_file(self):
        folder = os.path.join(_project_path(), "user", "ClaudeBridge")
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, "session.json")
        data = {"port": self.port, "token": self.token, "pid": os.getpid(), "started": time.time()}
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as session_file:
            json.dump(data, session_file)
        self.session_path = path

    # ------------------------------------------------------------------ tick

    def _on_tick(self, _args):
        self.ticks += 1
        if self.ticks == 1:
            _log("first frame tick received - bridge is live")
        try:
            now = time.time()
            if now - self.last_heartbeat > 2.0:
                self.last_heartbeat = now
                self._write_json(os.path.join(self.folder, "heartbeat.json"),
                                 {"ticks": self.ticks, "time": now, "port": self.port})
            self._poll_inbox()
            self._accept()
            for client in list(self.clients):
                self._read(client)
                self._handle_lines(client)
                self._write(client)
                if client.closed:
                    with contextlib.suppress(OSError):
                        client.sock.close()
                    self.clients.remove(client)
        except Exception:  # noqa: BLE001 - never let the bridge break the editor tick
            _log("tick error:\n" + traceback.format_exc())

    def _accept(self):
        while True:
            try:
                sock, address = self.server.accept()
            except (BlockingIOError, InterruptedError):
                return
            if address[0] != "127.0.0.1":
                sock.close()
                continue
            sock.setblocking(False)
            self.clients.append(_Client(sock, address))

    def _read(self, client):
        while not client.closed:
            try:
                chunk = client.sock.recv(65536)
            except (BlockingIOError, InterruptedError):
                return
            except OSError:
                client.closed = True
                return
            if not chunk:
                client.closed = True
                return
            client.inbox += chunk
            if len(client.inbox) > MAX_REQUEST_BYTES:
                client.closed = True
                return

    def _write(self, client):
        while client.outbox and not client.closed:
            try:
                sent = client.sock.send(client.outbox)
            except (BlockingIOError, InterruptedError):
                return
            except OSError:
                client.closed = True
                return
            client.outbox = client.outbox[sent:]
        if client.close_after_write and not client.outbox:
            client.closed = True

    def _handle_lines(self, client):
        while b"\n" in client.inbox:
            line, client.inbox = client.inbox.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                request = json.loads(line.decode("utf-8"))
            except ValueError:
                client.closed = True
                return
            if not secrets.compare_digest(str(request.get("token", "")), self.token):
                client.outbox += (json.dumps({"id": request.get("id"), "ok": False, "error": "bad token"}) + "\n").encode()
                client.inbox = b""
                client.close_after_write = True
                return
            response = self._dispatch(request)
            response["id"] = request.get("id")
            client.outbox += (json.dumps(response) + "\n").encode("utf-8")

    # ------------------------------------------------------------------ file transport
    # Same requests as the socket, but as files: inbox/<id>.json -> outbox/<id>.json.
    # Used when a sandbox keeps the connector from opening network connections.

    def _write_json(self, path, data):
        tmp = path + ".tmp"
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            json.dump(data, out)
        os.replace(tmp, path)

    def _poll_inbox(self):
        try:
            names = sorted(n for n in os.listdir(self.inbox) if n.endswith(".json"))
        except OSError:
            return
        for name in names:
            path = os.path.join(self.inbox, name)
            try:
                with open(path, "r", encoding="utf-8") as request_file:
                    request = json.load(request_file)
            except (OSError, ValueError):
                continue  # still being written; next tick
            with contextlib.suppress(OSError):
                os.remove(path)
            if not secrets.compare_digest(str(request.get("token", "")), self.token):
                response = {"ok": False, "error": "bad token"}
            else:
                response = self._dispatch(request)
            response["id"] = request.get("id")
            self._write_json(os.path.join(self.outbox_dir, name), response)

    # ------------------------------------------------------------------ ops

    def _dispatch(self, request):
        op = request.get("op")
        if op == "ping":
            return {"ok": True, "output": "pong"}
        if op == "python":
            return self._run_python(str(request.get("code", "")))
        if op == "console":
            return self._run_console(str(request.get("command", "")))
        if op == "screenshot":
            return self._screenshot(str(request.get("path", "")))
        if op == "reset":
            self.namespace = self._fresh_namespace()
            return {"ok": True, "output": "namespace reset"}
        return {"ok": False, "error": f"unknown op {op!r}"}

    def _run_python(self, code):
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                compiled = compile(code, "<claude>", "exec")
                exec(compiled, self.namespace)  # noqa: S102
            return {"ok": True, "output": output.getvalue()}
        except Exception:  # noqa: BLE001
            return {"ok": False, "output": output.getvalue(), "error": traceback.format_exc()}

    def _run_console(self, command):
        try:
            import azlmbr.legacy.general as general
            general.run_console(command)
            return {"ok": True, "output": f"ran: {command}"}
        except Exception:  # noqa: BLE001
            return {"ok": False, "error": traceback.format_exc()}

    def _screenshot(self, path):
        if not path:
            path = os.path.join(_project_path(), "user", "ClaudeBridge", "shots", f"shot_{int(time.time() * 1000)}.png")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        try:
            import azlmbr.atom
            import azlmbr.bus as bus
            outcome = azlmbr.atom.FrameCaptureRequestBus(bus.Broadcast, "CaptureScreenshot", path)
            if outcome.IsSuccess():
                return {"ok": True, "output": path}
            return {"ok": False, "error": f"capture refused: {outcome.GetError().error_message}"}
        except Exception:  # noqa: BLE001
            return {"ok": False, "error": traceback.format_exc()}


def stop():
    global _state
    if _state is None:
        return
    with contextlib.suppress(Exception):
        _state.handler.disconnect()
    for client in _state.clients:
        with contextlib.suppress(OSError):
            client.sock.close()
    with contextlib.suppress(OSError):
        _state.server.close()
    _state = None


def start(force=False):
    """Called from bootstrap.py (and restart_bridge.py with force=True)."""
    global _state
    if _state is not None and not force:
        return _state
    if force:
        stop()
    if not _running_in_editor():
        return None
    try:
        _state = _Bridge()
    except Exception:  # noqa: BLE001
        _log("could not start:\n" + traceback.format_exc())
        _state = None
    return _state
