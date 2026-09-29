# .z80 reference files from libspectrum

Written by libspectrum 1.5.0 (Fuse's library) with
[`szxtool synth <machine> <name>.z80`](../../../../tools/verification/szx/README.md):
known values (registers `#1122`..., 12345 T-states after the INT, `#7FFD = #13`,
page n filled with `n * 16 + offset / 1024`, `#1FFD = #04` on +2A / +3) for
128K, +2A, +3, Pentagon 128 and Scorpion ZS-256 (16 pages, 55-byte header with
`#1FFD`). Each `.libspectrum.txt` is what libspectrum reads back.
`LoaderZ80Models_Test` loads them on their model and checks our save.
