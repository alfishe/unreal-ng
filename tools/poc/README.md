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

| # | Name | Purpose | Status | Outcome |
|---|------|---------|--------|---------|
| 001 | [flags-widget](001-flags-widget/) | CPU flags debug widget | abandoned (pre-guideline) | Five flag-display layouts on demo data; no choice recorded; not used in production |
| 002 | [stack-widget](002-stack-widget/) | Stack view debug widget | abandoned (pre-guideline) | Nine stack-view layouts on sample data; no choice recorded; not used (stack widget work still open in `2026-08-26-debugger-enhancements`) |
| 003 | [memory-mapping](003-memory-mapping/) | Memory mapping visualization | abandoned (pre-guideline) | Nine bank-mapping layouts on static models; no choice recorded; production memory-page widgets built separately |
| 004 | [register-dump](004-register-dump/) | Z80 register display widget | abandoned (pre-guideline) | Nine register-dump layouts on sample state; no choice recorded; `registerswidget` evolved separately |
| 005 | [breakpoints](005-breakpoints/) | Breakpoint management UI | abandoned (pre-guideline) | Nine breakpoint-list layouts + condition editor mock-up; no choice recorded; not used |
| 006 | [signals-ports](006-signals-ports/) | Signal/port monitoring | abandoned (pre-guideline) | Nine signal / port panel layouts; no choice recorded; not used (port watch still open) |
| 007 | [t-states](007-t-states/) | T-state timing analysis | abandoned (pre-guideline) | Nine T-state counter layouts (a display widget, not timing analysis); no choice recorded; not used |
| 008 | [disassembly](008-disassembly/) | Disassembly view widget | abandoned (pre-guideline) | Nine disassembly-view layouts on sample code; no choice recorded; not used |
| 009 | [cmos-editor](009-cmos-editor/) | CMOS configuration editor | abandoned (pre-guideline) | Nine CMOS / RTC editor layouts; no choice recorded; the Qt CMOS panel is still a TODO item |
| 010 | [ttd-compression](010-ttd-compression/) | TTD compression algorithms | concluded (pre-guideline) | zstd level 1 is Pareto-optimal for TTD buffers (re-measured on real data 2026-08-20); production uses zstd-1 |
| 011 | [ttd-v2-capture-analysis](011-ttd-v2-capture-analysis/) | TTD v2 format: page-granular capture, compression, index overhead | concluded (pre-guideline) | TTD v2: page-granular, zstd-1, 4 KB pages; experiments E1–E8 fixed the v2 parameters (chain limit 50, 8-page blocks, journal on demand) |
| 013 | [nvenc-poc](013-nvenc-poc/) | NVIDIA NVENC video encoding | concluded (pre-guideline) | Five NVENC quality levels monotonic in size, detail kept, 3.3–4.7x Fastest→Best; results in `docs/emulator/design/recording/nvenc-integration.md` |
| 014 | [qt-gui](014-qt-gui/) | Qt GUI framework exploration | concluded (pre-guideline) | Only the icon generator is on master (the GUI POC is on branch `new-gui`); icon design adopted via `tools/icons/` (2026-09-07) |
| 015 | [opl4-synthesis](015-opl4-synthesis/) | YMF278B (OPL4/MoonSound) synthesis library: dual-grid chip model, deterministic state, render layer | concluded (pre-guideline) | Adopted: vendored into the core as `core/src/3rdparty/opl4` (2026-09-18), drives MoonSound |
| 017 | [z80-standalone-cpu](017-z80-standalone-cpu/) | Z80 CPU extracted into a z80ex-style standalone library (full undocumented support, ZEXALL-verified, benchmarked) | concluded (pre-guideline) | Extraction works: ZEXALL family passes, 1.00–1.14x z80ex; the same `Z80Cpu*` API ships as the vendored `unreal-z80` library |
| 018 | [tui-debuggers](018-tui-debuggers/) | Classic + TSConf debugger TUIs (FTXUI, golden-exact fork band, swappable mock/REST WebAPI backends) | concluded (pre-guideline) | Both TUIs work on mock and live WebAPI, TSConf band cell-exact; PLAN #49 "Spec + POC done", real front-end pending |
| 019 | [zxdlss-gigascreen](019-zxdlss-gigascreen/) | ZX DLSS GigaScreen de-flicker: reference recording (Across the Edge), clip extraction/export from TTD, effect map, quality oracle; Python POC → C++ prototype → integration | concluded (pre-guideline) | Phase 1 (Python) done: algorithm `mod-tpgw` specified; the C++ phase continues in `tools/verification/zxdlss` |
| 020 | [nedoos-layer](020-nedoos-layer/) | NedoOS seen from outside: tasks, pages, pipes, sockets, files, kernel-busy report, kernel calls on behalf of a task, ending a stuck call (Python over the WebAPI) | concluded (pre-guideline) | All ten requirements met against a live NedoOS (results.md); next: the core `NedoOsAnalyzer` design |
| 021 | [eve-accel](021-eve-accel/) | FT812 (VDAC2) renderer acceleration: line-parallel CPU, Metal compute (bit-exact), per-line vs per-frame dispatch, 256-subpixel AA vs the distance table, three emulation profiles; summary in `docs/inprogress/2026-10-01-tsconf-vdac2/acceleration-experiments.md` | concluded (pre-guideline) | Line-parallel CPU renderer bit-exact, adopt first; then a Metal backend (04c); keep the AA distance table |
| 022 | [breakpoint-matching](022-breakpoint-matching/) | Breakpoint matching on the debug hot path | concluded (pre-guideline) | Winning matcher combination chosen (2026-10-04); integrated into the core in `099ea9f12` |
| 023 | [steamdeck-client](../../apps/steamdeck-client/README.md) (moved to `apps/steamdeck-client/`) | Steam Deck native client (SDL3 / SDL_GPU): one folder per experiment P-01…P-25 of `docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md`, built in the steamrt4 SDK container, deployed to a Deck over SSH | running (pre-guideline) | 5 of 25 experiments built, none measured on a Deck yet |
| 024 | [dream-banks](024-dream-banks/README.md) | Dream SAM5000 compiled sound banks (.DXB / .B16): reverse-engineered reader, sample extraction, SF2 conversion for libsam2695, render comparison; spec in `docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md` | concluded (pre-guideline) | .DXB / .B16 format decoded and validated on 24 banks, SF2 converter; envelopes approximate, filter / LFO not decoded; integration proposed in `sam7-dream-banks.md` |
| 025 | [next-memory-model](025-next-memory-model/) | ZX Spectrum Next memory model (8 x 8K slot table behind the CPU library's callbacks), page-table and flat buses, mapping-change cost, per-step cost of unreal-next-z80 | concluded (pre-guideline) | Decision D6 stands (8-slot table behind callbacks costs the same); paged bus not worth it for N2 |
| 026 | [crt-effects-test](026-crt-effects-test/) | CRT shader effects GPU / CPU parity (was 001, renumbered 2026-10-10: 001 belongs to the debugger widget series) | concluded (pre-guideline) | 30 / 30 profile x resolution cases within 1 LSB, on Python models of the shader and CPU filter; superseded 2026-10-19: the shipped code differed, replaced by `CRTRenderer` (bit-exact GPU / CPU, `docs/features/crt-effects.md`) |
| 027 | [ttd-gui](027-ttd-gui/) | TTD scrubber/timeline GUI (was 010, renumbered 2026-10-10: 010 is ttd-compression) | abandoned (pre-guideline) | Build disabled since 2026-09-11, reader stuck on TTD schema 3; listed for retirement in the TTD v2 migration |

| 028 | [next-video-pipeline-alignment](028-next-video-pipeline-alignment/README.md) | ZX Next: clocks from a copper MOVE of a fetch-stage register (Layer 2 / ULA / tilemap / sprite scroll, clip, bank) to the first changed output pixel, and the Layer 2 vs ULA pixel alignment, from an RTL simulation of the video path; feature `docs/inprogress/2026-10-07-zx-next` | concluded | The whole zxnext core simulated in GHDL: a fetch-stage register change reaches the output after 22 clocks (chains, pixel clips), 26 (Layer 2), per 8-pixel cell +56 / +53 (ULA / tilemap scroll); Layer 2 and the ULA are aligned (2026-10-12) |
| 029 | [next-sprite-engine-timing](029-next-sprite-engine-timing/README.md) | ZX Next: the sprite engine's 28 MHz clock budget per line (overtime flag, cut-off), the cost of a sprite beyond x = 320 and its wrap, and what counts for the collision flag, from `sprites.vhd` alone in GHDL; feature `docs/inprogress/2026-10-07-zx-next` | concluded | Overtime when 1 + 128 + sum(16 << x scale) >= 4 * line pixels (1792 / 1824), the clocks after it are not drawn; collision is before the clip window and the 224-line limit; the engine runs with the sprites off (2026-10-12) |
| 030 | [surface-dial-haptics](030-surface-dial-haptics/README.md) | Surface Dial over Bluetooth LE: what an application gets per OS (input, descriptor, feature and output reports, unseized), what each haptic field does when felt, counts per turn, latency; feature `docs/inprogress/2026-10-18-control-surfaces` | open | macOS: input and haptic reports work unseized; no local clicks by default (2026-10-18) |
| 031 | [authentic-osd-keyboards](031-authentic-osd-keyboards/README.md) | The on-screen keyboard: an authentic photorealistic vector keyboard for each of the 21 models (the PC clones also with the ZX matrix), every key mapped to the matrix or the PC key; feature `docs/inprogress/2026-10-17-controller-manager` | in progress | |
| 032 | [streamdeck-jpeg-encoder](032-streamdeck-jpeg-encoder/README.md) | Can a small embedded baseline 4:4:4 JPEG encoder (jpge, stb_image_write, TooJpeg) replace the system libjpeg-turbo in the Stream Deck companion: encode time, bytes per frame, PSNR, device compatibility on the + XL, binary size; feature `docs/inprogress/2026-10-19-streamdeck-integration` (L-6, SD2) | concluded | Stage 1: TooJpeg had turbojpeg's bytes / PSNR at 2.1x its time; stage 2: own encoder `sdb::jpeg` (TooJpeg + libjpeg's integer arithmetic) has turbojpeg's exact scan data at 1.37x its time (2.37 ms per + XL frame vs ~90 ms of USB), renders on the + XL; jpge, stb_image_write and SIMD not worth it (2026-10-20) |
| 033 | [vector-controller-pictures](033-vector-controller-pictures/README.md) | The controller pictures of the controllers dialog as authentic vector art (TODO Q-1, owner 2026-10-23: vector, made as POC 031): a generator draws each host pad and retro stick from measured dimensions into one matte house style, for `ControllerPicture` unchanged; feature `docs/inprogress/2026-10-17-controller-manager` (C5) | done, owner review pending | 23 sets drawn (Series, One S, Elite 2, DualSense, DualShock 4, Switch Pro, 8BitDo x9, generic pad x4 with the PlayStation one on the DualShock shape, DB9 stick x4); DualShock and 8BitDo outlines measured with `trace.py` on orthographic views, checked by 50 % overlays; Qt vs rsvg 0.016-0.085/255 |
| 034 | [media-signatures](034-media-signatures/README.md) | Do the media signatures (core/swsignature: tape content, TR-DOS file set with the fuzzy rule, snapshot memory) recognise titles across re-packs, renames and saved-to disks without false matches, fast enough: recall, false-match rate and files/s over the vtrd.in mirror; gate P-12 of `docs/inprogress/2026-10-08-steamdeck-client`, signature hook C6 of `docs/inprogress/2026-10-17-controller-manager` | done 2026-10-23 | passed with changes: re-pack 100 % (after two loader fixes), saved-to 83.9 % with the catalog rule (Jaccard 14.8 %), 0.39 % cross-release matches (almost all the same title), 10k files/s on all cores; adopted in core/swsignature |
| 035 | [debugger-gui-bench](035-debugger-gui-bench/README.md) | Which GUI technology for the new debugger: the same debugger screen (hex with writes, disassembly following PC, a 500k-event TTD timeline zooming every frame, four 640 x 512 layer images) in Dear ImGui, RmlUi (DOM / custom elements), Qt 6 Widgets and Quick, and an own CPU raster of the dense views under Widgets / Quick; CPU of the process and the compositor, footprint, frame times, headroom, idle, with and without a GPU, HiDPI and run-time font / scale switching; Linux and macOS; feature `docs/inprogress/2026-09-28-debugger-model` | in progress | |
| 036 | [next-cpu-timing-rtl](036-next-cpu-timing-rtl/README.md) | Do the Next's CPU-side timings (contention on the 48K / 128K / +3 timings, Z80N instructions and `NEXTREG` under contention, the DMA on the contended clock, the NR #08 switch) match the RTL: the zxnext core of POC 028 in GHDL runs Z80 test ROMs, every instruction start and port cycle against this emulator's; feature `docs/inprogress/2026-10-07-zx-next` | done 2026-10-26 | 21 runs equal to the T after six fixes (port high-byte contention, Z80N internal T-states, the +3 WAIT window, the DMA hand-over, DMA port / idle contention, the +3 DMA's WAIT sampling); pinned by `NextRtlTiming_Test.PinnedCasesTakeTheRtlsTime`; Chg8kBan settled (MAME is off) |

Next free number: **036**. Local folders not in the repository (`012-calltrace-viz`, `015-ios-remote-control`, `016-ios-videocube`) are not registered; give them a free number when they are committed.
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
