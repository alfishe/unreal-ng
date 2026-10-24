# Media library: tests and quality

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Plan for review |
| **Plan** | [extraction-plan.md](extraction-plan.md) (phases X0-X11) |
| **Rules followed** | [core/tests/README.md](../../../../core/tests/README.md) (no `sleep_for`, < 50 ms per unit test, unique scratch paths), [performance-guidelines.md](../../../guidelines/performance-guidelines.md) §4 (A/B), AGENTS.md (`tools/build/` wrappers, 50% cores) |

## 1. Test layers after the extraction

| Layer | Binary | Lives in | Needs an emulator | Runs on |
|---|---|---|---|---|
| **Library unit tests** | `umedia-tests` | `libs/unreal-media/tests/` | no | Linux gcc / clang, macOS clang, Windows MSVC / MinGW (CI) |
| **Oracle tests** | `umedia-tests` with the `Oracle` label | same | no; needs external tools (mtools, dosfstools, cpmtools, amitools, VICE `c1541`, fuse-utils, xorriso) | Linux CI image (tools installed); skipped elsewhere |
| **Golden tests** | `umedia-tests` (`Golden` label) + `core-tests` (`MediaControlParity_Test`) | both | JSON goldens need the emulator adapter | Linux CI |
| **Fuzz targets** | `umedia-fuzz-<target>` | `libs/unreal-media/fuzz/` | no | nightly, clang |
| **Integration tests** (adapter, slots, TTD with media) | `core-tests` | `core/tests/integration/media/` (the ~47 moved emulator-backed tests) + slot / peripheral suites | yes | Linux CI (as today) |
| **Acceptance (real guests)** | `core-tests` | `core/tests/emulator/machines/...` | yes, real ROMs and OSes | Linux CI |
| **Benchmarks** | `umedia-benchmarks`, `core-benchmarks` | both | some | on demand, quiet machine |

## 2. Moving the existing tests

| Group (current-state §5) | TESTs | Goes to | Change |
|---|---|---|---|
| Storage pure tests | 105 | `umedia-tests` | namespace and include paths |
| Storage emulator-backed (folder disk builder 7, folder tape builder 1, audio folder disc 2) | 10 | `umedia-tests` | after K12 / K13 they need no emulator (the folder disk builder needed one only for the TR-DOS interleave). The two audio-disc tests that drive an ATM3 machine stay as integration tests |
| Media pure (17 `MediaManager(nullptr)` + registry, types, medium, config, blockformats, blockadvisory) | ~46 | `umedia-tests` | `MediaManager({})` with null ports |
| Media emulator-backed (mediacontrol 13, mediatargets 7, modelswitch 5, mediamanager 5, floppyformats 2, …) | ~37 | `core/tests/integration/media/` | include paths; `mediatargets` and `mediacontrol` tests are split: the verb logic gets library tests with a fake host, and the integration copy keeps one test per verb through the real emulator |
| `unicodehelper`, `DiskImage` | 37 | `umedia-tests` | |
| Floppy / tape loader tests | measured in X0 | `umedia-tests` | context-free loaders (K13) |

Helpers that move: `fatimagebuilder.h`, `cdtestdisc.h`, `tzxtapebuilder.h`, the CHD test helper,
a library `ScratchPath` (unique per process, like `TestPathHelper::GetUniqueTestScratchPath`).
Helpers that stay: `trdostesthelper` (drives the real ROM), `zcsdtesthelper`, `gsslot.h`.

**Counting rule:** the number of tests never drops during a move. A CI check compares the set of
test names in `umedia-tests` + `core-tests` before and after each move commit, and every removed
name must reappear (possibly in the other binary).

## 3. New tests per phase

| Phase | Tests |
|---|---|
| X1 | ports' null implementations; `Doc` JSON round trip; `HexDump`; CRC vectors; host path helpers on all three OSes (UNC paths, drive letters, case-insensitive file systems) |
| X2-X4 | moved tests only, plus the goldens |
| X5 | fake-port tests of `MediaManager`: recording guard branches (refuse, end recording, invalidate), apply now vs queue, event payloads (each `MediaEvent` kind ↔ the `NC_MEDIA_*` the adapter posts), config through `IConfigSource`; `MediaControlParity_Test` (every verb's JSON byte-identical to the X0 golden) |
| X6 | include-graph rule, no forwarding headers left, test-name set unchanged |
| X7 | per driver: (1) **unit** tests on built-in fixtures; (2) **oracle** agreement: list / read / write compared with the external tool, both directions (our writes read by the tool, the tool's writes read by us); (3) **round trip**: extract with `--meta manifest` and `wrap`, rebuild, compare bytes and metadata; (4) **check after write**: `Check()` clean after every write in every test; (5) **probe corpus**: this driver wins only on its own fixtures; (6) **guest acceptance** where an emulated guest exists (TR-DOS ROM: CAT / LOAD / MOVE after driver writes; +3 ROM: CAT / LOAD; NedoOS / DSS read FAT written in place) |
| X8 | cross-FS composition: TRD + SCL + folder → TRD, TAP → TRD, FAT image → +3 DSK, each booted / loaded on the real ROM; FAT / ISO composite goldens unchanged |
| X10 | CLI: every command with `--json` against goldens, exit codes; Python: pytest suite over the wheel on three OSes; new verbs on every surface (parity tests like the existing `media` verbs); Qt file view smoke test |
| X11 | fuzz targets green for their nightly budget; `find_package` consumer project builds |

## 4. Oracles

| Driver | Oracle | Agreement checked |
|---|---|---|
| `fat` | mtools (`mdir`, `mcopy`, `mdel`), dosfstools `fsck.fat -n`, `mkfs.fat` | listings, contents, our writes pass `fsck.fat`, files written by `mcopy` read by us |
| `cpm`, `plus3dos` | cpmtools (`cpmls`, `cpmcp`, `cpmrm`, `fsck.cpm`) with the same disk definitions | as above, per disk definition |
| `iso9660` | `isoinfo -l -J`, `xorriso -indev … -ls` | listings, Joliet names, El Torito catalog |
| `tape` | `tzxlist` (fuse-utils) | block list, headers |
| `trdos`, `scl`, `hobeta` | the real TR-DOS ROM through the emulator (acceptance), `tools/diskconverter` during its overlap | CAT, LOAD, MOVE, file bytes |
| `amiga` | amitools `xdftool` (list, read, write, create), ADFlib's command-line tools where installed | as for FAT |
| `cbm` | VICE `c1541` (`-list`, `-read`, `-write`, `-validate`) | as for FAT |
| tier-2 ZX file systems | corpora of real disks plus the behaviour notes of each driver; an emulated guest where one exists | listings and contents of known-good disks |

Oracles are executed as separate processes. Tests that need a missing tool call
`GTEST_SKIP()` with the tool's name. The CI image (`docker/linux`) installs all of them.

## 5. Fixtures and corpora

- **Generated, never committed binaries** where a builder exists: FAT (`FatImageBuilder`), ISO
  (`IsoImageBuilder`), TRD / SCL / TZX from the library's own builders cross-checked by oracles,
  CP/M via `mkfs.cpm` in the oracle job.
- **Real-disk corpora** for detection and tier-2 / tier-3 drivers: a curated set under
  `testdata/media-corpus/` (only images whose redistribution is allowed: freeware, public domain,
  or images made for the project), each with a sidecar `expected.json` (driver, dialect, listing,
  content hashes).
- **Malformed inputs:** every fuzz crash becomes a regression fixture (minimized) with its test.

## 6. Fuzzing

| Target family | Entry | Invariants |
|---|---|---|
| Container parsers (11 floppy, TAP, TZX, CHD, CUE, ISO, VHD / HDF / HDI) | `Parse(span)` | no crash, no out-of-bounds read (ASan / UBSan), bounded allocation (≤ 64 × input size or a fixed cap), deterministic result |
| File-system drivers | `Probe` → `Mount` → `List` (recursive, bounded) → `Read` all → `Check` | no crash, bounded time per byte, `Check()` never reports "clean" on a medium `Read` failed on |
| Write paths | a random sequence of `Create / Remove / Rename / Compact` on a valid fixture | after every step `Check()` is clean and the listing equals a model kept by the harness |
| Composition | random descriptors over small fixtures | build either succeeds with a medium the target driver mounts and checks clean, or fails with a typed error |

Nightly budget: 10 minutes per target on the Linux CI image (clang, libFuzzer, ASan + UBSan).
Corpora are seeded from `testdata/` and kept in a cache, not in the repository.

## 7. Performance

| Benchmark set | Purpose | When |
|---|---|---|
| X0 baseline: CHD, Z-Controller SD, compose C1-C8, floppy load / save (new), tape load (new) | detect regressions caused by the move | every phase X2-X6 (A/B, quiet machine, `UNREAL_NICE=0`, medians of 10, noise band) |
| Emulator hot paths that touch the library: ATA / SD sector reads, WD1793 track reads, tape block playback | the library boundary must cost nothing (static library, headers inline as before, LTO) | X2, X3, X5, X6 |
| Driver benchmarks: list / read / write throughput per driver on a 1 000-file volume; probe time over the corpus | regressions inside the library | X7 onward, on demand |

## 8. CI jobs

| Job | Trigger | Steps |
|---|---|---|
| `umedia-standalone` | every push touching `libs/unreal-media/` | configure the library alone; build with warnings as errors; `umedia-tests` (unit + golden); include-graph check; matrix Linux gcc / clang, macOS, Windows MSVC / MinGW |
| `umedia-oracles` | same, Linux only | the CI image with the oracle tools; `umedia-tests --gtest_filter=*Oracle*` |
| `core` (existing) | every push | unchanged: builds core with the library, runs `core-tests` incl. integration and acceptance |
| `umedia-fuzz` | nightly | all fuzz targets, 10 minutes each; crashes open issues with the minimized input |
| `umedia-wheel` | tags | Python wheels for three OSes; pytest over each |

## 9. Quality bars

| Bar | Value |
|---|---|
| Warnings | zero, all compilers, `-Wpedantic` / `/W4` |
| Line coverage of the library (gcov / llvm-cov, Linux) | ≥ 85% for drivers and containers, ≥ 75% overall; measured from X7, reported per module |
| Every driver | the six test kinds of §3 X7, a design note with its format sources, a format spec page in the library docs |
| Every public header | documented (what, units, ownership, thread rule) in the house comment style |
| Determinism | builders produce byte-identical media for identical inputs on all OSes (golden hashes in the standalone job on all three OSes) |
