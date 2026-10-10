# TODO: unreal-ng Developer Toolchain Implementation

## Status
Work in progress — Architecture proposal and prioritized implementation roadmap completed.

## Design References
- [unreal-ng-developer-toolchain-design.md](unreal-ng-developer-toolchain-design.md)
- [prioritized-roadmap.md](prioritized-roadmap.md)

## Implementation Roadmap & Progress

- [ ] **Phase 0 — Core Automation API & Deterministic Mutation Foundations**
  - [ ] R1: Owner-scoped declarative object sets (`devd:proj`, `mcp:sess`)
  - [ ] R2: Event stream (WebSocket/SSE lifecycle & debug notifications)
  - [ ] R3: Lifecycle semantics & object persistence rules
  - [ ] R4: Safe-point journaled mutations (`TTDExternalEvent`)
  - [ ] R8: Version negotiation & API schema handshake

- [ ] **Phase 1 — Normalized Debug-Info Engine (`libunreal-debuginfo`)**
  - [x] Reconciled with unreal-asm (2026-10-09, [debuginfo-reconciliation.md](debuginfo-reconciliation.md)): `.sym`, `.map`, `.lst` labels, SLD labels and the containers exist; build on the library, not beside it
  - [ ] Standalone `libunreal-debuginfo` parser library (SLD v1, `.lst`, `.sym`, `.cdb`/`.adb`) → an unreal-asm `debuginfo/` module: SLD `T` / `K` / `Z` in full, `.lst` lines, SDCC `.cdb` / `.adb`
  - [ ] Core normalized debug-info model (R5)
  - [ ] (Physical page, offset) → line lookup engine & identity binding
  - [ ] Module load binding & byte-hash staleness detection
  - [ ] `ListingParser` (WebAPI `/listing/*`, source stepping) as a facade over the model; the Qt disassembler's source column (the GDB stub uses neither)

- [ ] **Phase 2 — `unreal-devd` Daemon MVP, DAP Adapter & VS Code VSIX**
  - [ ] Standalone `unreal-devd` executable core & IPC client
  - [ ] Project model & zero-config parser (`DEVICE`, `SAVETRD`, `SAVESNA`, `SAVENEX`)
  - [ ] DAP server (launch, attach, breakpoints, stepping, TTD reverse step, memory inspector)
  - [ ] Headless emulator runner orchestration
  - [ ] Platform-specific VSIX bundling & extension MVP

- [ ] **Phase 3 — Source-Declared Trigger Engine & Action Classes**
  - [ ] Registry-driven trigger engine generalization
  - [ ] sjasmplus SLD `K` record parser & trigger compiler (`@break`, `@log`, `@watch`, `@assert`, `@budget`, `@trace`)
  - [ ] Action classification & determinism matrix (observe, annotate, mutate, control, delegate)
  - [ ] Retroactive query backend over TTD timeline

- [ ] **Phase 4 — Z80 / sjasmplus Language Server Protocol (LSP)**
  - [ ] LSP server implementation in `unreal-devd`
  - [ ] AST parsing & structured build diagnostics
  - [ ] Go-to-definition, references, symbol rename, completion
  - [ ] Model-aware hover stats (T-states, flags, contention, struct layouts)
  - [ ] Real-time coverage & T-state gutter overlays

- [ ] **Phase 5 — Advanced Developer Workflows & Quality Assurance Tools**
  - [ ] Edit-and-replay pipeline & first-divergence detector
  - [ ] Host-directory virtual FAT/TRD volume with TTD COW overlay
  - [ ] Asset pipeline & `INCBIN` safe-point hot-swap
  - [ ] Z80 unit test runner & pytest plugin
  - [ ] `unreal-dev bisect` regression tool

- [ ] **Phase 6 — Declarative OS Awareness & Multi-Task Debugging**
  - [ ] Declarative OS descriptor specification & parser (NedoOS descriptor)
  - [ ] Module-load binding & loader traps
  - [ ] Task-as-thread mapping (`qXfer:threads`, `qXfer:osdata`)
  - [ ] Page-ownership violation triggers & argument-decoded syscall event stream

- [ ] **Phase 7 — Visual Tooling, Webviews & Custom Editors**
  - [ ] Low-latency shared memory 50fps screen/audio stream (`R6`)
  - [ ] VS Code webviews: live screen, raster timeline (code vs beam), TTD timeline
  - [ ] Custom editors for TRD, SCL, TAP, and SCR image formats
  - [ ] Notebook API integration for interactive playbooks
