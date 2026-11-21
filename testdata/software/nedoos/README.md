# NedoOS General Sound / NeoGS fixtures

Code the NedoOS programs upload to a General Sound / NeoGS card, for the GS debugger's upload profiles (M5) and its
validation scenarios V-1..V-6 (`docs/inprogress/2026-09-27-gs-debugger/design.md` §8A.10).

Source: the NedoOS repository at revision `44049473` (2026-09-27).

| File | From | Uploaded to | Profile |
|------|------|-------------|---------|
| `card-code/gscode.bin` | `release/bin/gp/gp.plr`, offset 43098, 990 bytes (`src/gp/ngsdec/gscode.asm`) | `#4000` | `nedoos-gp-gscode` |
| `card-code/ngsldr.bin` | `src/kapps/ngsplay/gs/ngsldr.bin` | `#5100` | `nedoos-ngsldr` |
| `card-code/ngsdrv.bin` | `src/kapps/ngsplay/gs/ngsdrv.bin` | `#0000` | `nedoos-neotracker` |
| `card-code/neopg2.bin` | `src/kapps/ngsplay/gs/neopg2.bin` | `#D000`, RAM page 2 | `nedoos-neopg2` |
| `card-code/ngssd.bin` | `src/kernel/ngssd.bin` (the kernel sends 768 bytes: these 686 and 82 of its own) | `#5B00` | `nedoos-ngssd` |

The profiles live in `data/symbols/gs/profiles/`; what each program sends is in
`docs/inprogress/2026-09-27-gs-debugger/nedoos-gs-software.md`.

## Golden command logs (`golden/`)

`core/tests/emulator/sound/chips/gs_nedoos_scenarios_test.cpp` (long tier, `UNREAL_LONG_TESTS=1`) boots NedoOS on
the shipped ZX-Evo from `testdata/machines/zxevo/nedoos/sdcard` plus the program and an `autoexec.bat` that starts
it, and compares the card's command log with these files: one `GSCommandLogEntry::Text()` line per entry, a run of
equal lines kept once with `(repeated)`, a run of an equal block of 2 to 4 lines (a poll and its reply) kept once with
`(the N lines above repeat)`. On a difference the actual log goes to
`scratch/gs-nedoos-scenarios/<name>.actual.txt`; review it before copying it here.

| File | Scenario | Program | Card |
|------|----------|---------|------|
| `golden/v1-gstest.txt` | V-1 | `gstest.com` from `testdata/machines/p2666/software/nedoos/sdcard/bin` (NedoOS `d1d011f8`) | classic GS, v1.05 |
| `golden/v2-gp-mod.txt` | V-2, to the end of the song | `gp.com` + `gp/gp.plr` (same folder), `media/afterwr2.mod` | classic GS, v1.05, 128 KB |
| `golden/v3-gp-mp3.txt` | V-3, without the streamer's polls `#01` / `#06` (the test counts them) | `gp.com` + `gp/gp.plr`, `testdata/sound/neogs/mp3/eyeache1-22k-mono-vbr-id3.mp3` as `eyeache1.mp3` | the ZX-Evo's NeoGS, v1.11 |
| `golden/v4-ngsplay-s3m.txt` | V-4: ngsplay loads and starts the S3M, [S] stops it (`#E4`) | `bin/ngsplay.com`, `testdata/sound/wildsound/src/kj_jose_-_a_new_frontend.s3m` as `frontend.s3m` | the ZX-Evo's NeoGS, v1.11 |
| `golden/v5-kernel-ngssd.txt` | V-5: the P2666 kernel mounts the NeoGS SD card (drive N) and `type n:/ngssd.txt` reads a file from it: the installer upload, `#1E` init, the sector reads (`sectors`, M12) | the P2666 kernel rebuilt with the NGS SD driver fix (`testdata/machines/p2666/software/nedoos/kernel-ngsfix/`) | NeoGS on the Pentagon 2.666's ZX-BUS, v1.11 |
| `golden/v6-moon-rabbit-mod.txt` | V-6: Moon Rabbit opens a `.mod` row and streams the module from a scripted gopher server (`ComPort=AT`, the virtual network) into the GS: `#30`, `#D1`, one 61902-byte block, `#D2`, `#31`, the `#60` play loop | `testdata/machines/p2666/software/nedoos/sdcard/bin/mrfue.com`, `browser/auth.pwd`, a generated `browser/index.gph`; `media/afterwr2.mod` served | the classic GS (128 KB, stock firmware) on the ZX-Evo |
| `golden/v2-gp-cancel.txt` | V-2, Esc during the load (a PS/2 key: NedoOS for the ZX-Evo reads the AVR's PS/2 log) | the same | the same |

## Programs (`bin/`)

| File | What | Source |
|------|------|--------|
| `bin/ngsplay.com` | ngsplay, the NeoTracker BIOS player (50682 bytes, SHA-256 `bc99e4f1…ed2d`); not on the P2666 card | NedoOS `release/bin/ngsplay.com` (NedoOS `bbd427dd`) |

## Media (`media/`)

| File | What | Source |
|------|------|--------|
| `media/gp.ini` | GP's default settings without comments (`ModDevice=0`), CRLF; without it GP opens its settings screen | NedoOS `src/gp/gp.ini` |
| `media/afterwr2.mod` | Karbofos, "After the war 2 GS (final cut)" (2006): a General Sound module, 61902 bytes, 8 positions (an 8.3 name: GP does not open a longer one from its command line), SHA-256 `095834a8…6d9fa7` | zxart.ee, entry 599585 |
