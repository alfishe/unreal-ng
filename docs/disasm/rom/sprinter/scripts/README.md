# Generation pipeline for the Sprinter BIOS 3.04 listings

Regenerates and verifies the listings one level up and the symbol files in
`data/symbols/sprinter/`. Background: [../README.md](../README.md).

Needs Python 3 and `z80dasm` 1.2.0 on `PATH`. Only `build-refs.sh` needs more (sjasmplus and the
source repositories, see below).

## Files

| File | Kind | Role |
|---|---|---|
| `extract.py` | tool | slices `data/rom/sprinter/sp2k-3.04.rom` (checks CRC32 `1729cb5c`) into the `bios304-*.bin` pieces, unpacks SETUP |
| `hrust.py` | tool | Hrust 1.x depacker (port of mhmt's, byte-identical output) |
| `z80.py` | tool | instruction length, control flow, operand masking |
| `targets.py` | curated | per listing: binary, origin, entry points, jump tables, named data blocks, forced data |
| `analyze.py` | tool | code reachability from the entries (+ code labels carried from a source run) → `bios304-*.blocks` |
| `refs.py` | tool | sjasmplus listing of a reference build → `refs-*.json` (labels, instruction starts); temporary |
| `transfer.py` | tool | carries reference labels onto 3.04 by masked byte matching → `labels-*.json` |
| `labels-*.json` | curated (derived) | the carried names, kept because rebuilding them needs the external sources |
| `diffregions.py` | tool | code regions with no counterpart in the source (the tables in the page READMEs) |
| `biosfn.py` | curated | BIOS function numbers → names (from Shared_Includes `BIOS_equ.inc` dd760c8) |
| `fntables.py` | tool | reads the dispatch table / chains from the ROM, names the handlers `FnNN_NAME` |
| `dict_*.py` | curated | hand names, comments, file header, symbol-file name per listing |
| `gen.py` | tool | runs z80dasm, inserts the comments and source notes, writes the `.asm` and the `.map` symbol file |
| `checkcov.py` | tool | every listing must rebuild its binary byte for byte |
| `build-refs.sh` | tool | rebuilds the reference sources and re-runs `refs.py`, `transfer.py`, `diffregions.py` |
| `bios304-*.bin`, `*.blocks` | derived | from `extract.py` / `analyze.py` |

## Regenerate (no external sources needed)

```sh
cd docs/disasm/rom/sprinter/scripts
python3 extract.py        # bios304-*.bin
python3 analyze.py        # *.blocks
python3 gen.py            # ../*/*.asm and data/symbols/sprinter/*.map
python3 checkcov.py       # must print N/N bytes verified for all five
```

To refine: edit a `dict_*.py` (names win over carried ones; `DROP` removes a wrong carried name) or
`targets.py` (entries, data blocks), then run `analyze.py`, `gen.py`, `checkcov.py`.

## Rebuild the carried names (needs the sources)

`build-refs.sh <scratch-dir> <emulators-dir>` expects sjasmplus on `PATH` (the corpus copy in
`emulators/github/sjasmplus` builds with CMake) and these clones under the emulators directory:

| Clone | Commit | Used for |
|---|---|---|
| `zxgit/Sprinter-BIOS` (zxgit.org/Tolik-Trek/Sprinter-BIOS) | `d0456af` = `0271ac3` + the submodule link | page 8 names (`bios/exp/*.asm`) |
| `zxgit/Shared_Includes` (zxgit.org/Tolik-Trek/Shared_Includes) | `66d8b07` | includes of that build |
| `gitlab/sprinter-computer-bios` (gitlab.com/sprinter-computer/bios) | `1273243` | page 0 drivers and SETUP names |

The BIOS-TT build stops with a few "block truncated" errors (its code grew past fixed addresses); the
listing is complete anyway and only the listing is used. Steps: assemble the three references, turn
their listings into `refs-*.json`, place the labels (`transfer.py`), print the difference tables
(`diffregions.py`), delete the temporary `refs-*.json`.

### How the names are matched

Both images are masked: inside every instruction the absolute 16-bit addresses (JP / CALL / LD nn)
and the relative jump offsets become zero, so code that only moved still matches; data bytes stay.
`difflib` then finds the longest common runs (8 bytes or more) in order; a reference label inside a
run lands at the same offset in the matching 3.04 run. A label not placed that way is placed when
the 16 (then 12, 10) masked bytes starting at it occur exactly once in 3.04 (a "window" match,
marked `~`). Window matches that several different reference addresses claim are dropped (filler or a
repeated idiom).
