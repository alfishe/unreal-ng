# unreal-media: extracting the media manager into a standalone library

A worked-out plan to turn the media manager, the storage stack, the floppy and tape containers,
the guest file systems and the multi-source composition engine into **unreal-media**: a standalone
MIT-licensed C++20 library (`unrealng::media`) with a platform-neutral core. It also covers:

- **platform packs** as plugins: file systems, containers and tape codecs per platform (ZX, CP/M,
  Amiga, CBM, MSX, …);
- one VFS for every guest file system;
- a CLI, a Python module, a C facade and a WASM build;
- reference integrations into the common kinds of emulators and platforms.

**Runs after** the multi-source media work (C0-C9) is integrated and fully tested.

## Documents

| File | Topic |
|---|---|
| [current-state.md](current-state.md) | **Measured** starting point: every file and line count, the 59 consumer edges, the coupling edges K1-K19 with their replacements, third-party code, the 235 tests that move, build facts, the guest file-system code today (TR-DOS parsed 6 times) |
| [library-architecture.md](library-architecture.md) | Target: layers L0-L5 and modules, dependency rules, directory layout, core types (medium, views, volumes, drivers), threading, errors, versioning |
| [filesystem-unification.md](filesystem-unification.md) | One interface for every guest file system: the four media views, `IVolume` / `IFsDriver`, metadata with the ZX header pivot, host round trip, name rules, builders, detection, the driver catalog with effort per driver, cross-FS operations, write safety |
| [plugins-and-usage.md](plugins-and-usage.md) | Core vs platform packs, extension points, **tape codecs** (`ITapeCodec`, `ITapeEncoding`), plugin delivery (static C++, dynamic C ABI, out-of-process, Python), discovery, the nine usage schemes U1-U9, packaging |
| [api-and-integration.md](api-and-integration.md) | The ports (`IMachineHost`, `IRecordingGuard`, `IMediaEventSink`, `ILogSink`, `IConfigSource`), runtime sequences, `Doc` replies, new `media fs` verbs, CMake integration, the `umedia` CLI, the Python module |
| [reference-integrations.md](reference-integrations.md) | Reference integrations R1-R9 with diagrams and code: unreal-ng, a C emulator with its own FDC, a multi-system emulator with a format framework, an Amiga emulator (RDB / FFS hardfiles), a web / WASM page, FPGA SD-card preparation, a homebrew toolchain, a preservation catalogue, a mobile app |
| [extraction-plan.md](extraction-plan.md) | **The plan**: effort model, phases X0-X12 with tasks, days and exit criteria, Gantt chart, critical path, gates, risks, definition of done |
| [test-and-quality-plan.md](test-and-quality-plan.md) | Test layers, moving the existing tests, new tests per phase, oracles, fixtures, fuzzing, performance A/B, CI jobs, quality bars |

## Totals

| Block | Days |
|---|---|
| A. Extraction (X0-X6) | 79 |
| B. Unification: VFS + tier-1 drivers + FS-neutral composition (X7-X8) | 80.5 |
| C. Tools and surfaces (X10) | 29 |
| D. Hardening (X11) | 15.5 |
| G. Packs, plugins, reference integrations (X12) | 66 |
| **Planned total** | **270** (±25%): ~13 months for one developer, **~6.5 months with three parallel streams** |
| E / F. More file systems and foreign packs (X9a / X9b) | 49 + 35.5, demand-driven |
