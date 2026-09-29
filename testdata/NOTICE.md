# Test data notice

Files under `testdata/` (and `data/testsnapshots/`, `data/testtapes/`, `tools/verification/*/resources/`) are used
by the automated test suite as fixtures. Most of them are **third-party programs** (games, demos, disk and tape
images, snapshots) whose copyright belongs to their respective authors and publishers. They are **not** licensed
under the GPL and are not part of the unreal-ng source code; they are included solely to exercise the emulator and
will be removed on request of a rights holder.

Fixtures with a known license:

| Fixture | Author | License |
|---------|--------|---------|
| ZEXALL / ZEXDOC (`data/testsoft/ZEXALL/`) | Frank Cringle, Z80 port by J.G. Harston | GPL-2.0-or-later |
| z80test (`core/tests/z80/z80test/`, `testdata/loaders/sna/z80full.sna`, `z80flags.sna`) | Patrik Rak | MIT |
| z80bltst (`data/testsoft/Test/`) | see file header | MIT |
| Z80 XCF Flavor (`testdata/loaders/sna/z80-xcf-flavor.sna`) | see `docs/inprogress/2026-01-18-z80-hidden-flags/` | GPL-3.0-or-later |
| FUSE Z80 test vectors (`testdata/z80/fuse/`) | FUSE project | GPL-2.0-or-later |
| ZX Diagnostics (`data/testrom/zx-diagnostics.rom`) | Brendan Alford | GPL-3.0 |
| Timing Test v0.3 (`testdata/contention/rak-timing-test/`, source and tape) | Patrik Rak, after Jan Bobrowski's zxtests | GPL (stated in `timing.bas`, no version: any GPL version) |
| ctprobe (`tools/verification/contention/ctprobe/`, outside testdata but listed here with the Timing Test it derives from): `engine.asm` is the Timing Test's measuring engine ported to the in-tree assembler, `ctprobe.asm` the project's case table and driver; `ctprobe.tap` / `ctprobe.trd` are built from them | engine: Jan Bobrowski, Patrik Rak; driver and cases: unreal-ng | engine GPL (as the Timing Test); the probe as a whole is GPL |
| ZX Spectrum Timing Tests 48K v1.0 (`testdata/loaders/sna/Timing_Tests-48k_v1.0.sna`) | Richard and Tim Butler | none stated (test material) |

Everything else (commercial games such as Dizzy X and Green Beret, demo-scene productions such as EyeAche,
Satisfaction, Insult, Echology, Across the Edge, 7th Reality, the TR-DOS / FDI / UDI disk images, the TurboSound FM
material in `testdata/sound/tsfm/` and the ZXM-MoonSound demo disks in `testdata/sound/moonsound/` — see their
`SOURCES.md`) is copyrighted by its authors and used here as test material only.
