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

Everything else (commercial games such as Dizzy X and Green Beret, demo-scene productions such as EyeAche,
Satisfaction, Insult, Echology, Across the Edge, 7th Reality, and the TR-DOS / FDI / UDI disk images) is
copyrighted by its authors and used here as test material only.
