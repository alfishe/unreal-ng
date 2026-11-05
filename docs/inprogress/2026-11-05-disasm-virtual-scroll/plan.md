# Disassembly virtual window: plan

Design: [README.md](README.md).

| Step | What | State |
|:--|:--|:--|
| 1 | Core: `DisassemblyWindow` (forward rows, local backward sync, anchors, snap), on demand | todo |
| 2 | Core tests: up N / down N round trip on code and data (W3), anchors respected (W4), snap, timing of a screen | todo |
| 3 | Planes: `before=` / `snap=` on REST + OpenAPI, WebSocket call, MCP, CLI, Python, Lua; live MCP script | todo |
| 4 | Qt: painter view on `QAbstractScrollArea`, 64K address scrollbar, row strip, every current feature | todo |
| 5 | Qt: label rows, smooth scroll, jump arrows (P2: data rows) | todo |
| 6 | Docs: command-interface, disassembler module doc, plane docs, recipes | todo |
</content>
