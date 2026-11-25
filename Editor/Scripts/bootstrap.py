# Runs automatically when the O3DE Editor starts for the NoclipProtocol project.
# Starts the Claude bridge (localhost only, per-session token). See claude_bridge.py.
try:
    import claude_bridge
    claude_bridge.start()
except Exception as error:  # noqa: BLE001 - a bridge problem must never stop the editor
    print(f"[ClaudeBridge] not started: {error}")
