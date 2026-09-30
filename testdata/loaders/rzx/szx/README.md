# RZX recordings with an SZX start snapshot

The recordings of `../archive/` (Spectaculator and SPIN, `.z80` start
snapshots) re-written by libspectrum 1.5.0 - the library Fuse writes RZX with -
so that their start snapshot, and the snapshots inside, are SZX:

```bash
tools/verification/szx/szxtool rzx-resnap testdata/loaders/rzx/archive/<name>.rzx testdata/loaders/rzx/szx/<name>.rzx
```

The input frames are unchanged, so each one must replay exactly like its
original: `RzxArchive_Test` compares both with the same SkoolKit oracles in
`../oracle/`. Same authors and terms as `../archive/` (see `testdata/NOTICE.md`).
