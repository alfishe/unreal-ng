# Disassembly virtual window: plan

Design: [README.md](README.md).

| Step | What | State |
|:--|:--|:--|
| 1 | Core: `DisassemblyWindow` (forward rows, local backward sync, anchors, snap), on demand | done: `disasm::Window`, `EmulatorSource` (PC, labels, breakpoints, markup), `BufferSource`; `GET /disasm`, the snapshot and the card CPUs list through it |
| 2 | Core tests: up N / down N round trip on code and data (W3), anchors respected (W4), snap, timing of a screen | done: `DisassemblyWindow_Test` |
| 3 | Planes: `before=` / `snap=` on REST + OpenAPI, WebSocket call, MCP, CLI, Python, Lua; live MCP script | done: module doc `docs/emulator/design/debugger/disassembly-window.md`; `test_debug_code.py` |
| 4 | Qt: painter view on `QAbstractScrollArea`, 64K address scrollbar, row strip, every current feature | done: `DisassemblyView` (painter, fixed columns), `DisassemblyStrip` (reversible rows, dropped on a bytes / marks hash or markup revision change); `DisassemblyStrip_Test` |
| 5 | Qt: label rows, smooth scroll, jump arrows (P2: data rows) | label rows and `db` data rows done; smooth pixel scroll and jump arrows not done |
| 6 | Docs: command-interface, disassembler module doc, plane docs, recipes | todo |
</content>
