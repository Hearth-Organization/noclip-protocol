# Restarts the Claude bridge inside a running editor (also loads any new bridge code).
# Run from the editor Console:
#   pyRunFile /var/home/chris/O3DE/Projects/NoclipProtocol/Tools/ClaudeBridge/restart_bridge.py
import importlib
import claude_bridge
if hasattr(claude_bridge, "stop"):
    claude_bridge.stop()
else:
    old = getattr(claude_bridge, "_state", None)
    if old is not None:
        try:
            old.handler.disconnect()
        except Exception:
            pass
        try:
            old.server.close()
        except Exception:
            pass
claude_bridge = importlib.reload(claude_bridge)
bridge = claude_bridge.start(force=True)
print("[ClaudeBridge] restarted" if bridge else "[ClaudeBridge] failed to start - see user/ClaudeBridge/bridge.log")
