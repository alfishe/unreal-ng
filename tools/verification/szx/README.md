# SZX reference tool

`szxtool.c` is a small C program on top of libspectrum 1.5 (the library Fuse
uses for SZX). It gives the SZX work an outside reader and writer, so our
loader is checked against the implementation most other emulators are checked
against. Design: [docs/inprogress/2026-09-29-szx-snapshots](../../../docs/inprogress/2026-09-29-szx-snapshots/design.md).

| Command | What it does |
|---|---|
| `szxtool synth <machine> <out>` | writes a snapshot with known values: registers `#1122`..., IM 2, MEMPTR `#4321`, 12345 T-states after the INT, EI shadow and FSET set, border 5, `#7FFD = #13` (page 3 at `#C000`), page n filled with `n * 16 + offset / 1024`, AY registers `r * 3 + 1` (register 7 = `#38`), Beta 128 on Pentagon / Scorpion. Machines: `48 128 plus2 plus2a plus3 pentagon pentagon512 pentagon1024 scorpion` |
| `szxtool convert <in> <out>` | reads any snapshot libspectrum knows (SNA, Z80, SZX, ...) and writes it as SZX, or as `.z80` / `.sna` by the output's extension |
| `szxtool dump <file>` | prints the state libspectrum reads: registers, flags, T-states, ports, AY, Beta 128, a CRC-32 per RAM page |

## Build

```bash
cc -std=c11 -O1 -o scratch/szxtool tools/verification/szx/szxtool.c \
   $(pkg-config --cflags --libs libspectrum) $(pkg-config --cflags libgcrypt)
```

(`libspectrum.h` includes `gcrypt.h`; its `.pc` file does not carry that path.)

## Regenerating the reference files

```bash
T=scratch/szxtool; D=testdata/loaders/szx
for m in 48 128 plus2 plus2a plus3 pentagon pentagon512 pentagon1024 scorpion; do
    $T synth $m $D/libspectrum/synth-$m.szx; done
$T convert testdata/loaders/sna/z80full.sna $D/libspectrum/z80full-48k.szx
$T convert testdata/loaders/z80/BBG128.z80 $D/libspectrum/bbg128.szx
for f in $D/*/*.szx; do $T dump "$f" > "${f%.szx}.libspectrum.txt"; done
```

The `.libspectrum.txt` dumps are the oracle `SzxReader_Test.CorpusAgreesWithLibspectrum`
compares against, so the test suite needs no libspectrum.

The `.z80` reference files in `testdata/loaders/z80/libspectrum/` are made the
same way: `szxtool synth <machine> <name>.z80` (128, plus2a, plus3, pentagon,
scorpion), each with its `.libspectrum.txt` dump.

## Interop check

`check-interop.sh [build-dir]` builds the tool, runs
`LoaderSZX_Test.ExportForTheLibspectrumCheck` with `UNREALNG_SZX_EXPORT_DIR`
set (unreal-ng loads each `synth-*.szx` and saves it again), and compares
what libspectrum reads from our files with the reference dumps. The `beta`
line is left out: the shipped 48K, 128K and +2 configs fit a Beta 128, so our
files carry a B128 block the libspectrum-made reference does not have.
The `.z80` files unreal-ng saves from `testdata/loaders/z80/libspectrum/`
(`LoaderZ80Models_Test`) are checked the same way, leaving out what `.z80`
cannot hold (MEMPTR, the EI shadow, FSET). Result on 2026-09-29: nine SZX and
five `.z80` files match.
