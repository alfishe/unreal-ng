# Proof of Concept (POC) Directory

Research tools, benchmarks, and feasibility studies for emulator features.

---

## POC Guidelines

Rules for every POC (numbering, the charter with goals / requirements / constraints / acceptance
criteria, reports, analysis, conclusions, proposal, where code and raw data go):
[docs/guidelines/poc-guidelines.md](../../docs/guidelines/poc-guidelines.md). This page is the
registry: a POC gets the next free number here before work starts. Numbers are never reused.

---

## POC Index

| # | Name | Purpose |
|---|------|---------|
| 001 | [flags-widget](001-flags-widget/) | CPU flags debug widget |
| 002 | [stack-widget](002-stack-widget/) | Stack view debug widget |
| 003 | [memory-mapping](003-memory-mapping/) | Memory mapping visualization |
| 004 | [register-dump](004-register-dump/) | Z80 register display widget |
| 005 | [breakpoints](005-breakpoints/) | Breakpoint management UI |
| 006 | [signals-ports](006-signals-ports/) | Signal/port monitoring |
| 007 | [t-states](007-t-states/) | T-state timing analysis |
| 008 | [disassembly](008-disassembly/) | Disassembly view widget |
| 009 | [cmos-editor](009-cmos-editor/) | CMOS configuration editor |
| 010 | [ttd-compression](010-ttd-compression/) | TTD compression algorithms |
| 011 | [ttd-v2-capture-analysis](011-ttd-v2-capture-analysis/) | TTD v2 format: page-granular capture, compression, index overhead |
| 013 | [nvenc-poc](013-nvenc-poc/) | NVIDIA NVENC video encoding |
| 014 | [qt-gui](014-qt-gui/) | Qt GUI framework exploration |
| 015 | [opl4-synthesis](015-opl4-synthesis/) | YMF278B (OPL4/MoonSound) synthesis library: dual-grid chip model, deterministic state, render layer |
| 017 | [z80-standalone-cpu](017-z80-standalone-cpu/) | Z80 CPU extracted into a z80ex-style standalone library (full undocumented support, ZEXALL-verified, benchmarked) |
| 018 | [tui-debuggers](018-tui-debuggers/) | Classic + TSConf debugger TUIs (FTXUI, golden-exact fork band, swappable mock/REST WebAPI backends) |
| 019 | [zxdlss-gigascreen](019-zxdlss-gigascreen/) | ZX DLSS GigaScreen de-flicker: reference recording (Across the Edge), clip extraction/export from TTD, effect map, quality oracle; Python POC → C++ prototype → integration |
| 020 | [nedoos-layer](020-nedoos-layer/) | NedoOS seen from outside: tasks, pages, pipes, sockets, files, kernel-busy report, kernel calls on behalf of a task, ending a stuck call (Python over the WebAPI) |
| 021 | [eve-accel](021-eve-accel/) | FT812 (VDAC2) renderer acceleration: line-parallel CPU, Metal compute (bit-exact), per-line vs per-frame dispatch, 256-subpixel AA vs the distance table, three emulation profiles; summary in `docs/inprogress/2026-10-01-tsconf-vdac2/acceleration-experiments.md` |
| 022 | [breakpoint-matching](022-breakpoint-matching/) | Breakpoint matching on the debug hot path |
| 023 | [steamdeck-client](023-steamdeck-client/README.md) | Steam Deck native client (SDL3 / SDL_GPU): one folder per experiment P-01…P-25 of `docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md`, built in the steamrt4 SDK container, deployed to a Deck over SSH |
| 024 | [dream-banks](024-dream-banks/README.md) | Dream SAM5000 compiled sound banks (.DXB / .B16): reverse-engineered reader, sample extraction, SF2 conversion for libsam2695, render comparison; spec in `docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md` |
| 025 | [next-memory-model](025-next-memory-model/) | ZX Spectrum Next memory model (8 x 8K slot table behind the CPU library's callbacks), page-table and flat buses, mapping-change cost, per-step cost of unreal-next-z80 |
| 026 | [crt-effects-test](026-crt-effects-test/) | CRT shader effects GPU / CPU parity (was 001, renumbered 2026-10-10: 001 belongs to the debugger widget series) |
| 027 | [ttd-gui](027-ttd-gui/) | TTD scrubber/timeline GUI (was 010, renumbered 2026-10-10: 010 is ttd-compression) |

Next free number: **028**. Local folders not in the repository (`012-calltrace-viz`, `015-ios-remote-control`, `016-ios-videocube`) are not registered; give them a free number when they are committed.
---

## Building POCs

Each POC is self-contained with its own build instructions. Most can be built directly:

```bash
cd tools/poc/011-ttd-v2-capture-analysis
c++ -std=c++17 -O2 compression_benchmark.cpp -lzstd -o compression_benchmark
./compression_benchmark
```

POCs that integrate with the emulator build include their own CMakeLists.txt.

---

## Creating a New POC

1. Take the next free number and add the row to the index above
2. Create folder: `mkdir NNN-descriptive-name`
3. Write the charter `README.md` first (goals, requirements, constraints, acceptance criteria /
   expected results) — see [docs/guidelines/poc-guidelines.md](../../docs/guidelines/poc-guidelines.md)
4. Implement measurement tools; raw data and build output in `scratch/YYYY-MM-DD-poc-NNN/`
5. Measurement reports, analysis, conclusions, proposal as separate files
