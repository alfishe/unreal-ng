# Proof of Concept (POC) Directory

Research tools, benchmarks, and feasibility studies for emulator features.

---

## POC Guidelines

### Naming Convention

```
NNN-<descriptive-name>/
```

- **NNN**: Three-digit sequence number (001, 002, ...)
- **descriptive-name**: kebab-case, describes what POC investigates
- Example: `011-ttd-v2-capture-analysis`

### Structure

Each POC folder should contain:

```
NNN-name/
├── README.md           # Goals, results, conclusions
├── *.md                # Additional analysis documents (lowercase-hyphen)
├── *.cpp               # Measurement tools, benchmarks
└── CMakeLists.txt      # Optional, if integrated with build
```

### Documentation Standards

- Include concrete measurements in tables
- Link to relevant source files with relative paths
- Record conclusions and recommendations
- Reference related core code

---

## POC Index

| # | Name | Purpose |
|---|------|---------|
| 001 | [crt-effects-test](001-crt-effects-test/) | CRT shader effects testing |
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
| 010 | [ttd-gui](010-ttd-gui/) | TTD scrubber/timeline GUI |
| 011 | [ttd-v2-capture-analysis](011-ttd-v2-capture-analysis/) | TTD v2 format: page-granular capture, compression, index overhead |
| 012 | [calltrace-viz](012-calltrace-viz/) | Call trace visualization |
| 013 | [nvenc-poc](013-nvenc-poc/) | NVIDIA NVENC video encoding |
| 014 | [qt-gui](014-qt-gui/) | Qt GUI framework exploration |

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

1. Find the next available number in the index
2. Create folder: `mkdir NNN-descriptive-name`
3. Add README.md with goals and expected results
4. Implement measurement tools
5. Document findings with tables and conclusions
6. Update this index
