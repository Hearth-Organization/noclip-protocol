# Hearth Engine agent

A small background helper that runs outside the O3DE editor, so Claude can build the project,
start, stop and restart the editor, run the standalone game full screen, and read crash reports and logs,
even when the editor is closed or has crashed.

Install once, from a normal terminal (not the editor's console):

    python3 ~/O3DE/Projects/NoclipProtocol/Tools/HearthAgent/install.py

It then starts at every login. Remove it with `install.py --uninstall`.

- `hearth_agent.py`: the agent. Requests are JSON files in `user/HearthAgent/inbox/` carrying the token
  from `user/HearthAgent/token`; answers go to `user/HearthAgent/outbox/`. It runs only a fixed list of
  operations: status, editor_start, editor_stop, build, game_start, game_stop, crash_report, logs.
- `install.py`: sets it up as a systemd user service (or an autostart entry) and starts it.
- The MCP server in `Tools/ClaudeBridge` exposes it as the `hearth_engine` tool.
