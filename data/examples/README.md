# unreal-ng examples: driving the emulator from scripts and AI agents

While unreal-qt runs, it serves:

| Surface | Address | For |
|:--|:--|:--|
| WebAPI (HTTP + JSON) | `http://localhost:8090/api/v1` | Scripts in any language, `curl`. The whole API: `GET /api/v1/openapi.json`. |
| MCP | `http://localhost:8092/mcp` | AI agents (Claude Code, Claude Desktop, Cline, ...). |
| WebSocket | `ws://localhost:8090/api/v1/websocket` | Pushes an event for every pause, breakpoint and step. |

Environment variables move the ports: `UNREAL_WEBAPI_PORT`, `UNREAL_MCP_PORT`.

| Example | What it shows |
|:--|:--|
| [01-section-timing](01-section-timing/README.md) | T-states between an entry and an exit marker: min / avg / max per section, interrupts inside, pause when a pass is too slow. A Z80 demo is included and compared on a 48K and a Pentagon. |
| [02-constant-time](02-constant-time/README.md) | Code that must take exactly N T-states on every path: the passes that do not, grouped by the register that chose the path (IY of a jump table), with the data that took them there. A dispatcher with two planted timing bugs and its fixed build. |

`lib/unreal_webapi.py` is the small WebAPI client the Python examples share, and `lib/unreal_tui.py` its terminal
UI. They find a running emulator by themselves (or wait for one), ask which instance to use when several run, and
notice when the emulator goes away. They use the standard library only, so there is nothing to install, and they
run in Windows Terminal, PowerShell, cmd, macOS Terminal and iTerm2.

Each example comes as a Python script and as `prompt.md`, the same task asked of an AI agent over MCP.
