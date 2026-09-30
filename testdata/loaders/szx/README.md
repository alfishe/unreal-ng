# SZX test files

Fixtures for the SZX loader (`core/tests/loaders/snapshot/szx/`). Every
`<name>.szx` has a `<name>.libspectrum.txt` next to it: the state libspectrum
1.5 (Fuse's library) reads from the file, written by
[`tools/verification/szx/szxtool`](../../../tools/verification/szx/README.md).
The reader test compares every field and every RAM page (CRC-32) with it.

| File | Written by | Version | Machine | Contents |
|---|---|---|---|---|
| `libspectrum/synth-{48,128,plus2,plus2a,plus3,pentagon,pentagon512,pentagon1024,scorpion}.szx` | libspectrum 1.5.0 (`szxtool synth`) | 1.5 | as named | known values on every machine id we emulate (see the tool's README) |
| `libspectrum/z80full-48k.szx` | libspectrum 1.5.0 (`szxtool convert`) | 1.5 | 48K | `testdata/loaders/sna/z80full.sna` (z80test by Patrik Rak, MIT); the loader test checks it equals our own SNA load |
| `libspectrum/bbg128.szx` | libspectrum 1.5.0 (`szxtool convert`) | 1.5 | 128K | `testdata/loaders/z80/BBG128.z80` |
| `other/spectaculator-pentagon-crazylove.szx` | Spectaculator (CRTR version 62.540) | 1.1 | Pentagon 128 | a real music-disk snapshot with B128, BDSK, IF1, MFCE, ZXPR, KEYB, JOY, AMXM blocks; from the zxtune sample set (`samples/archived/szx/CrazyLove.szx`) |
| `other/zxmak2-pentagon-cpd-test.szx` | ZXMAK2 2.2 | 1.4 | Pentagon 128 | ALTERNATETIMINGS set, B128 without CONNECTED; from the kozynax test fixtures |
| `other/zx-m8xxx-128k-shock.szx` | ZX-M8XXX | 1.4 | 128K | no CRTR block; from the ZX-M8XXX test files |

The files from other emulators' repositories are test material only (see
`testdata/NOTICE.md`); the programs in them belong to their authors.
