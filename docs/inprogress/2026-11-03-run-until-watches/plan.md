# Run until, and frame watches: plan

Design: [README.md](README.md).

| Step | What | State |
|:--|:--|:--|
| 1 | Core: `framewatch::Spec` / `Probe` / `Manager`, `Emulator::RunFramesUntil`, the `MainLoop::CompleteFrame` hook, `DebugStop::Reason::Watch`, `NC_FRAME_WATCH` | done |
| 2 | `RunControl::RunUntil` (session, claim, revoked answer); the gate: `run_until` / `watch` change the machine, `debug` companion scope | done |
| 3 | Planes: WebAPI routes + OpenAPI, WebSocket `watch_*` on `debug.events`, CLI, Lua, Python, MCP actions; the snapshot's `pause` | done |
| 4 | Tests: `FrameWatch_Test` W1-W7, MCP forwarding, the event hub, the companion route list | done |
| 5 | Live measurement over REST and MCP: the old loops against `run_until` and a watch (`scratch/run-until-watches/live_run_until.py`); a live MCP script in `tools/verification/mcp/` | |
| 6 | Docs: command-interface, plane docs, MCP README, recipes (waiting-and-watching, debugger-snapshot, gameplay automation, transports) | done |
