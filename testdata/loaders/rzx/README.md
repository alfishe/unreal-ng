# RZX test files

Fixtures for the RZX reader and player (`core/tests/loaders/rzx/`,
`core/tests/emulator/rzx/`). The expected states come from SkoolKit
`rzxplay.py` with its MEMPTR-exact simulator
([`tools/verification/rzx`](../../../tools/verification/rzx/README.md)).

| File | Machine | Recorded with | Frames | Used for |
|---|---|---|---|---|
| `archive/ericfloaters.rzx` | 48K | Spectaculator 80.3092 | 32315 | first 300 frames and the whole file against SkoolKit |
| `archive/garfield.rzx` | 48K | SPIN 0.5 | 23234 | the same |
| `archive/greenberet.rzx` | 128K | Spectaculator 62.552 | 39041 | the same; the model switch (a 128K recording on a 48K) |
| `archive/thundercats.rzx` | 128K | SPIN 0.5 | 61282 | the same; trailing frames of 0 fetches |
| `archive/dargonscrypt.rzx` | +2 | Spectaculator 80.3092 | 33678 | the same; `BIT n,(HL)` flags from MEMPTR (frame 3963) |
| `archive/darkwingduck.rzx` | Pentagon 128 | Spectaculator 70.1310 | 11670 | plays without a desync (SkoolKit has no Pentagon) |
| `external/ericfloaters-ext.rzx` + `ericfloaters-ext-start.z80` | 48K | cut from ericfloaters (`rzxtrim.py --frames 300 --external`) | 300 | the start snapshot stored next to the recording |
| `cases/memptr-bit-hl.rzx` | +2 | cut from dargonscrypt at frame 3962 (`rzxtrim.py --frames 2`) | 2 | one frame ending in `BIT 5,(HL)` after a taken `JR`: F = #74 (#5C with a stale MEMPTR) |

`oracle/<name>-300.z80` is SkoolKit's state after 300 frames (`--stop 300`),
`oracle/<name>-end.z80` after the whole recording; `cases/memptr-bit-hl-1.z80`
after one frame. The tests stop where SkoolKit does: right after the interrupt
that ends frame N (none when interrupts are disabled there).

The whole-recording checks are slow (seconds per file) and run with
`UNREAL_RZX_FULL=1`; `UNREAL_RZX_CORPUS=<folder>` plays every `.rzx` in a
folder (and compares with `<name>.end.z80` when present).

Sources: `ericfloaters.rzx` from the ZX-M8XXX test files; `garfield`,
`greenberet`, `thundercats` from the Zero emulator's sample recordings;
`dargonscrypt`, `darkwingduck` from the RZX Archive (rzxarchive.co.uk, through
the Internet Archive). They are test material only (see `testdata/NOTICE.md`);
the games belong to their publishers, the recordings to their authors.
