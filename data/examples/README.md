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

`lib/unreal_webapi.py` is the small WebAPI client the Python examples share. It uses the standard library only, so
there is nothing to install.

Each example comes as a Python script and as `prompt.md`, the same task asked of an AI agent over MCP.
