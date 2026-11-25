#!/usr/bin/env python3
"""
Claude bridge - MCP server (stdio). Runs on this computer, started by the Claude desktop app.

It connects to the in-editor bridge (Editor/Scripts/claude_bridge.py) over 127.0.0.1
using the port and token the editor writes to <project>/user/ClaudeBridge/session.json.

Tools:
  o3de_status      - is the editor running and reachable; which level is open
  o3de_python      - run Python inside the editor (azlmbr), returns printed output
  o3de_console     - run an editor console command
  o3de_screenshot  - capture the editor viewport as an image
  o3de_log         - read the end of the editor log (works even if the editor crashed)
  hearth_engine    - the Hearth Engine agent (Tools/HearthAgent): build, start/stop the editor and game,
                     crash reports and logs, even while the editor is closed

Standard library only. Usage: mcp_server.py --project /path/to/NoclipProtocol
"""

import argparse
import base64
import contextlib
import json
import os
import socket
import sys
import time

SERVER_NAME = "o3de-bridge"
SERVER_VERSION = "1.0.0"
DEFAULT_PROTOCOL = "2025-06-18"

PROJECT = None


# --------------------------------------------------------------------------- editor link

class BridgeError(Exception):
    pass


def _session():
    path = os.path.join(PROJECT, "user", "ClaudeBridge", "session.json")
    try:
        with open(path, "r", encoding="utf-8") as session_file:
            return json.load(session_file)
    except FileNotFoundError:
        raise BridgeError("The editor hasn't started the bridge yet (no session file). Open the O3DE Editor for this project.")
    except ValueError:
        raise BridgeError("The bridge session file is unreadable; restart the editor.")


def _folder():
    return os.path.join(PROJECT, "user", "ClaudeBridge")


def _heartbeat_age():
    try:
        with open(os.path.join(_folder(), "heartbeat.json"), "r", encoding="utf-8") as beat:
            return time.time() - float(json.load(beat)["time"])
    except (OSError, ValueError, KeyError):
        return None


TRANSPORT = {"mode": "socket"}  # switches to "files" if sockets are blocked


def _socket_request(session, request, timeout):
    with socket.create_connection(("127.0.0.1", int(session["port"])), timeout=2.0) as sock:
        sock.settimeout(timeout)
        sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    if b"\n" not in data:
        raise BridgeError("The editor closed the connection without answering (it may have crashed). Check o3de_log.")
    return json.loads(data.split(b"\n", 1)[0].decode("utf-8"))


def _file_request(request, timeout):
    inbox = os.path.join(_folder(), "inbox")
    outbox = os.path.join(_folder(), "outbox")
    os.makedirs(inbox, exist_ok=True)
    name = f"{request['id']}.json"
    tmp = os.path.join(inbox, name + ".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as out:
        json.dump(request, out)
    os.replace(tmp, os.path.join(inbox, name))
    answer = os.path.join(outbox, name)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(answer):
            try:
                with open(answer, "r", encoding="utf-8") as response_file:
                    response = json.load(response_file)
            except ValueError:
                time.sleep(0.05)
                continue
            with contextlib.suppress(OSError):
                os.remove(answer)
            return response
        time.sleep(0.05)
    with contextlib.suppress(OSError):
        os.remove(os.path.join(inbox, name))
    age = _heartbeat_age()
    if age is None or age > 10:
        raise BridgeError("The editor isn't processing frames (no recent heartbeat). It may be closed, frozen, "
                          "loading, or showing a dialog. If it's open and idle, run restart_bridge.py in its Console.")
    raise BridgeError(f"The editor is running but didn't finish this request within {int(timeout)} s.")


def _editor_request(op, timeout=120.0, **fields):
    session = _session()
    request = {"token": session["token"], "id": int(time.time() * 1000000), "op": op}
    request.update(fields)
    if TRANSPORT["mode"] == "socket":
        try:
            return _socket_request(session, request, timeout)
        except (ConnectionRefusedError, socket.timeout, OSError):
            TRANSPORT["mode"] = "files"  # sockets blocked or not answering: use the file inbox from now on
    return _file_request(request, timeout)


def _format(response):
    parts = []
    if response.get("output"):
        parts.append(response["output"].rstrip())
    if response.get("error"):
        parts.append("ERROR:\n" + response["error"].rstrip())
    return "\n".join(parts) if parts else ("(ok, no output)" if response.get("ok") else "(failed, no details)")


# --------------------------------------------------------------------------- tools

def tool_status(_args):
    try:
        ping = _editor_request("ping", timeout=10.0)
    except BridgeError as error:
        return _text(str(error), is_error=True)
    level = _editor_request("python", timeout=10.0,
                            code="print(general.get_current_level_name() or '(no level open)')\n"
                                 "print('game mode' if general.is_in_game_mode() else 'edit mode')")
    age = _heartbeat_age()
    beat = f"last frame {age:.1f} s ago" if age is not None else "no heartbeat"
    return _text(f"Editor reachable ({ping.get('output')}, via {TRANSPORT['mode']}, {beat}).\nLevel: {_format(level)}")


def tool_python(args):
    code = args.get("code", "")
    if not code.strip():
        return _text("No code given.", is_error=True)
    try:
        response = _editor_request("python", timeout=float(args.get("timeout_seconds", 120)), code=code)
    except BridgeError as error:
        return _text(str(error), is_error=True)
    return _text(_format(response), is_error=not response.get("ok"))


def tool_script(args):
    action = args.get("action", "list")
    try:
        if action == "save":
            response = _editor_request("save_script", timeout=30.0, name=args.get("name", ""), code=args.get("code", ""))
        elif action == "run":
            response = _editor_request("run_script", timeout=float(args.get("timeout_seconds", 120)), name=args.get("name", ""))
        else:
            response = _editor_request("list_scripts", timeout=30.0)
    except BridgeError as error:
        return _text(str(error), is_error=True)
    return _text(_format(response), is_error=not response.get("ok"))


def tool_console(args):
    try:
        response = _editor_request("console", timeout=30.0, command=args.get("command", ""))
    except BridgeError as error:
        return _text(str(error), is_error=True)
    return _text(_format(response), is_error=not response.get("ok"))


def tool_screenshot(_args):
    folder = os.path.join(PROJECT, "user", "ClaudeBridge", "shots")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, f"shot_{int(time.time() * 1000)}.png")
    try:
        response = _editor_request("screenshot", timeout=30.0, path=path)
    except BridgeError as error:
        return _text(str(error), is_error=True)
    if not response.get("ok"):
        return _text(_format(response), is_error=True)

    # The capture finishes a few frames later; wait for the file to appear and stop growing.
    deadline = time.time() + 20.0
    last_size = -1
    while time.time() < deadline:
        if os.path.exists(path):
            size = os.path.getsize(path)
            if size > 0 and size == last_size:
                break
            last_size = size
        time.sleep(0.25)
    else:
        return _text("The screenshot was requested but never written (is the viewport visible?).", is_error=True)

    with open(path, "rb") as image_file:
        data = base64.b64encode(image_file.read()).decode("ascii")
    _prune_shots(folder, keep=20)
    return {"content": [{"type": "image", "data": data, "mimeType": "image/png"},
                        {"type": "text", "text": f"Viewport screenshot saved at {path}"}]}


def _claude_allowed(key):
    """A switch from Hearth Settings › Claude (~/.config/hearth-engine/settings.ini); on unless the owner turned it off."""
    import configparser
    path = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "hearth-engine", "settings.ini")
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read(path)
        value = parser.get("claude", key, fallback="true")
    except configparser.Error:
        return True
    return value.strip().lower() in ("true", "1", "yes", "on")


def tool_log(args):
    if not _claude_allowed("readLogs"):
        return _text("Hearth Settings › Claude: 'Can read logs' is off.", is_error=True)
    lines = max(10, min(int(args.get("lines", 80)), 2000))
    contains = args.get("contains", "")
    path = os.path.join(PROJECT, "user", "log", "Editor.log")
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as log_file:
            all_lines = [line.rstrip("\n") for line in log_file
                         if line.strip() and "unable to obtain symbol name" not in line]
    except FileNotFoundError:
        return _text("No Editor.log yet.", is_error=True)
    if contains:
        all_lines = [line for line in all_lines if contains.lower() in line.lower()]
    return _text("\n".join(all_lines[-lines:]) or "(nothing matched)")


def _prune_shots(folder, keep):
    shots = sorted((os.path.join(folder, name) for name in os.listdir(folder) if name.endswith(".png")),
                   key=os.path.getmtime)
    for old in shots[:-keep]:
        try:
            os.remove(old)
        except OSError:
            pass


def tool_hearth_engine(args):
    folder = os.path.join(PROJECT, "user", "HearthAgent")
    op = args.get("op", "status")
    if op == "result":
        answer = os.path.join(folder, "outbox", str(args.get("request_id", "")) + ".json")
        if not os.path.exists(answer):
            return _text("No answer yet for that request; it may still be running (check op status).")
        with open(answer, "r", encoding="utf-8") as handle:
            text = handle.read()
        os.remove(answer)
        return _text(text)
    try:
        with open(os.path.join(folder, "heartbeat.json"), "r", encoding="utf-8") as beat:
            age = time.time() - float(json.load(beat)["time"])
        with open(os.path.join(folder, "token"), "r", encoding="utf-8") as token_file:
            token = token_file.read().strip()
    except (OSError, ValueError, KeyError):
        return _text("The Hearth Engine agent isn't installed yet. Run in a terminal: "
                     f"python3 {os.path.join(PROJECT, 'Tools', 'HearthAgent', 'install.py')}", is_error=True)
    if age > 30:
        return _text(f"The Hearth Engine agent isn't running (last heartbeat {int(age)} s ago).", is_error=True)
    request_id = "mcp-" + base64.b32encode(os.urandom(5)).decode().lower()
    inbox = os.path.join(folder, "inbox")
    tmp = os.path.join(inbox, request_id + ".tmp")
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump({"id": request_id, "token": token, "op": op, "args": args.get("args") or {}}, handle)
    os.rename(tmp, os.path.join(inbox, request_id + ".json"))
    answer = os.path.join(folder, "outbox", request_id + ".json")
    deadline = time.time() + float(args.get("wait_seconds", 600))
    while time.time() < deadline:
        if os.path.exists(answer):
            time.sleep(0.1)
            with open(answer, "r", encoding="utf-8") as handle:
                text = handle.read()
            os.remove(answer)
            return _text(text)
        time.sleep(0.5)
    return _text(f"Still running. Fetch the answer later with op 'result' and request_id '{request_id}'.")


def _text(text, is_error=False):
    return {"content": [{"type": "text", "text": text}], "isError": is_error}


TOOLS = [
    {
        "name": "o3de_script",
        "description": "Saved editor scripts (the Script console lists them for the owner): action list, save (name, code) "
                       "or run (name). Save the Python you run often, like 'Screenshot the spawn room'.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["list", "save", "run"]},
                "name": {"type": "string"},
                "code": {"type": "string"},
                "timeout_seconds": {"type": "number"},
            },
        },
        "handler": tool_script,
    },
    {
        "name": "o3de_status",
        "description": "Check that the O3DE Editor for the Noclip Protocol project is open and reachable, "
                       "and report the open level and whether it is in game mode.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": tool_status,
    },
    {
        "name": "o3de_python",
        "description": "Run Python inside the O3DE Editor on its main thread and return everything it printed. "
                       "Preloaded names: azlmbr, bus, editor, entity, components, azmath, general "
                       "(azlmbr.legacy.general). Variables persist between calls. Use print() to return values. "
                       "Do not call general.idle_wait_frames or anything that waits for frames: the code runs "
                       "inside a frame, so split waits across separate calls instead.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "Python source to execute."},
                "timeout_seconds": {"type": "number", "description": "How long to wait for the editor (default 120)."},
            },
            "required": ["code"],
        },
        "handler": tool_python,
    },
    {
        "name": "o3de_console",
        "description": "Run an O3DE Editor console command (the same as typing it in the editor's Console panel).",
        "inputSchema": {
            "type": "object",
            "properties": {"command": {"type": "string"}},
            "required": ["command"],
        },
        "handler": tool_console,
    },
    {
        "name": "o3de_screenshot",
        "description": "Capture the O3DE Editor viewport (or the game view while in game mode) and return it as an image.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": tool_screenshot,
    },
    {
        "name": "o3de_log",
        "description": "Read the end of the project's Editor.log. Works even if the editor has crashed or is closed.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "lines": {"type": "integer", "description": "How many lines from the end (default 80, max 2000)."},
                "contains": {"type": "string", "description": "Only lines containing this text (case-insensitive)."},
            },
        },
        "handler": tool_log,
    },
    {
        "name": "hearth_engine",
        "description": "Talk to the Hearth Engine agent, a helper that runs outside the editor. ops: status (editor, "
                       "game, build, memory), editor_start (args: wait_seconds), editor_stop (args: save, timeout), "
                       "build (closes the editor, builds, reopens it; args: stop_editor, reopen_editor, jobs), "
                       "game_start (standalone game; args: fullscreen, width, height, level), game_stop, "
                       "crash_report, logs (args: which = editor|game|build|agent|asset_processor|editor_stdout|"
                       "game_stdout, lines), result (args: request_id, for an answer that took too long).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "op": {"type": "string", "enum": ["status", "editor_start", "editor_stop", "build", "game_start",
                                                  "game_stop", "crash_report", "logs", "result"]},
                "args": {"type": "object", "description": "Arguments for the op."},
                "request_id": {"type": "string", "description": "For op 'result'."},
                "wait_seconds": {"type": "number", "description": "How long to wait for the answer (default 600)."},
            },
            "required": ["op"],
        },
        "handler": tool_hearth_engine,
    },
]


# --------------------------------------------------------------------------- MCP over stdio

def _send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def _result(request_id, result):
    _send({"jsonrpc": "2.0", "id": request_id, "result": result})


def _error(request_id, code, message):
    _send({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})


def _handle(message):
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}

    if request_id is None:
        return  # notifications (initialized, cancelled...) need no answer

    if method == "initialize":
        _result(request_id, {
            "protocolVersion": params.get("protocolVersion", DEFAULT_PROTOCOL),
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "instructions": "Tools for driving the O3DE Editor of the Noclip Protocol project. "
                            "Start with o3de_status; use o3de_screenshot to see the result of changes.",
        })
    elif method == "ping":
        _result(request_id, {})
    elif method == "tools/list":
        _result(request_id, {"tools": [{k: v for k, v in tool.items() if k != "handler"} for tool in TOOLS]})
    elif method == "tools/call":
        name = params.get("name")
        tool = next((t for t in TOOLS if t["name"] == name), None)
        if tool is None:
            _error(request_id, -32602, f"unknown tool {name!r}")
            return
        try:
            _result(request_id, tool["handler"](params.get("arguments") or {}))
        except Exception as error:  # noqa: BLE001
            _result(request_id, _text(f"Bridge error: {error}", is_error=True))
    else:
        _error(request_id, -32601, f"method not found: {method}")


def main():
    global PROJECT
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    PROJECT = os.path.abspath(os.path.expanduser(parser.parse_args().project))

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except ValueError:
            _error(None, -32700, "parse error")
            continue
        if isinstance(message, list):
            for item in message:
                _handle(item)
        else:
            _handle(message)


if __name__ == "__main__":
    main()
