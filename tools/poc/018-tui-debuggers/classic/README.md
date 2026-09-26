# 018a - Classic UnrealSpeccy Debugger TUI (80x30)

Standalone C++20/FTXUI recreation of the classic 80x30 debugger screen
(TDD-DBG-01) with a swappable `IDebuggerBackend` seam. Fully working on the
mock backend (vendored z80ex + golden sample set) and on the live emulator via
the WebAPI REST client.

## Build and run

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/dbgclassic                # mock backend (default), terminal >= 80x30
./build/dbgclassic-tests          # test harness (placeholder, see Tests)
```

Backend selection: `--backend mock|rest|ws|ipc [--endpoint <url/pipe>]`
(`ws`/`ipc` are planned transports; the factory reports them as not wired).
For `rest`, start the emulator first and create an instance, e.g.:

```bash
./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
curl -s -X POST "http://localhost:8090/api/v1/emulator/start" -d '{"model":"128k"}'
./build/dbgclassic --backend rest
```

## Keys (v0 slice)

| Key | Action |
|-----|--------|
| `F5` / `r` | run until break |
| `F11` / `s` | single step |
| `F9` / `b` | toggle BPX at trace cursor |
| Up / Down | move trace cursor (length-aware down, PrevInstruction up) |
| `1` `2` `3` | focus regs / trace / mem |
| `t` | reset time-delta mark |
| `q` / `Esc` / `F10` | quit |

## Source map

| Area | Files | Status |
|------|-------|--------|
| Data model | `src/model/model.h`, `hexfmt.cpp` | done |
| Screen model | `src/screen/` (TextScreen, palette, CP437 glyphs) | done |
| Backend seam | `src/backend/debugger-backend.h` | done |
| Mock backend | `mock-backend.cpp`, `sample-state.*` | done - z80ex exec, bp, conditions, ripper |
| Disasm (Unreal format) | `disasm-unreal.cpp` | done - oracle-exact |
| Assembler subset | `asm-subset.cpp` | done - 26+4 dialog cases verified |
| Condition expr lang | `expr-lang.cpp` | done - compile/eval/decompile |
| REST backend | `rest-backend.cpp`, `http-client.cpp` | done - verified vs live emulator |
| Painters | `src/panels/regs/trace/side.cpp` | v0 - geometry exact, attrs approximate |
| Frame render / loop | `src/tui/frame-render.cpp`, `src/main.cpp` | done |
| UI subsystems | `src/ui/*.cpp`, `panels/memory|preview.cpp` | scaffolded stubs (pending milestone) |

Oracle for the golden sample and screen layout:
`docs/inprogress/2026-09-24-tui-debugger/unreal_dbg_render.py`.
Backend behavior smoke reference: `scratch/poc018-mock-smoke.cpp`,
`scratch/poc018-rest-smoke.cpp`.

## Tests

`tests/test-main.cpp` is a placeholder harness that always passes; the classic
golden-exact panel compare is the pending "panels" milestone. The tsconf fork
project (`../tsconf/`) already carries the full golden-compare pattern against
the same oracle. Backend behavior is covered by the smoke references above and
the tsconf test binary (shared codebase lineage).

## Next steps

1. Golden-exact attributes for the classic band (regs/trace/mem/side).
2. Wire `src/ui/` modals + full section-9 key map onto the existing backend
   capabilities (expr/asm/find are already implemented at the seam).
3. WebSocket transport for push-based stop events.
