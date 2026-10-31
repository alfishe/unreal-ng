# P4 gate: graphics geometry in the L2 draft of Manic Miner

The P3 gate ([score-p3.md](score-p3.md)) again, now with the media detectors (graphics, level maps, screens, RNG,
unpackers, music): one recorded session, the L2 export put into a fresh zx-reaper project with no hand fixes, scored
with [score_draft.py](../../../../tools/verification/analysis/score_draft.py) against skoolkid's `mm.skool` (program
range $8000-$FFFF). The geometry of a graphics region is right when the draft's geometry JSON states the reference
macro's width, height and frame count for the region the macro draws. No agent touched the draft. Measured 2026-10-26
on an M1 Ultra, worktree `analysis-layers-proposal`, zx-reaper `unreal-ng-fixes` at f98c784 (no new zx-reaper fixes).

## Summary

| Measure | P3 draft | **P4 draft** | Target (P4 brief) |
| --- | ---: | ---: | --- |
| Graphics regions with the right geometry | n/a | **106 / 106** | ≥ 96 of 106 - met |
| Graphics regions covered by data | 106 / 106 | 106 / 106 (86 start at a draft boundary) | - |
| Byte categories (by entry) | 99.98 % | **99.98 %** (32761 / 32768) | keep P3 - met |
| Byte categories (by line / sub-block) | 97.98 % | 97.98 % | - |
| Code entries | 31 / 33 | **31 / 33** (misses $8684 $8944, as P3) | keep P3 - met |
| Record field lists | 5 / 5 | **5 / 5** | keep P3 - met |
| zx-reaper `make` (tape identical) | yes | **yes** | yes - met |
| `make test` (4 scripts, per-frame hashes) | yes | **yes** (27536 frames) | yes - met |
| shift test +$100 / +$101 | yes / yes | **yes / yes** | yes - met |

By reference macro:

| reference macro | regions | geometry right | start at a draft boundary |
| --- | ---: | ---: | ---: |
| `#CAVERN` (level maps, 32x16 cells) | 20 | 20 | 20 |
| `#ITEM` | 20 | 20 | 20 |
| `#PORTAL` | 20 | 20 | 0 |
| `#SCR` (the title screen) | 1 | 1 | 1 |
| `#SPRITE` (Willy, guardians) | 23 | 23 | 23 |
| `#TILES` | 20 | 20 | 20 |
| `#UDGARRAY` | 2 | 2 | 2 |

## The session

The same `mm_session.py` run as P3 (27536 frames, the mixer muted): L1 42 routines, 373 blocks, 65 loops, 130.6 M
instructions; L2 372 regions (P3: 239), 160 conflicts. The draft: 303 entries (P3: 103), 9123 lines; the new entries
are the graphics, tiles, level maps and screens that P3 left inside larger data blocks. Claims in the map: 85
sprite, 21 tile, 20 level map, 2 screen. No RNG, unpacker or music claims: Manic Miner has none of the three
mechanisms (its tunes are beeper loops; the detectors fire only on their fixtures). Layout as P3: `REGION_START
$8000`, `SHIFT_END $9D00`. Two sessions gave byte-identical skool files.

Each graphics entry carries a SkoolKit image macro (`#UDGARRAY`, `#FONT`, `#SCR` inside `#UDGTABLE { ... } TABLE#`).
skool2asm drops them, so the build is unchanged. `skool2html.py` on the draft: rc 0, 389 PNG files. Willy, the
guardians and the tiles draw right.

## Commands

As in [score-p3.md](score-p3.md#commands), with `--out scratch/p4/mm` for the session and `--draft scratch/p4/mm
--work scratch/p4/gate` for `reaper_gate.py`. The MCP live block (`run_all.py --launch ...`) covers the gallery with
the sprite fixture (`test_analysis_layers.py::test_gallery`).

## What is left

- **The 20 portals start one byte early.** The portal is drawn with its attribute byte: the tile detector takes the
  attribute as the frame's prefix and starts the region there ($xx8F), while the reference starts it at the graphic
  ($xx90). The geometry is right (16x16, one frame), and the score counts the region as covered and right, but not as
  starting at a draft boundary.
- **The title `#SCR` colours.** The macro draws the display file with the attributes in memory at the snapshot, not the
  ones the title routine writes later: the pixels are right, the colours are not.
- **The mask order is assumed.** With an interleaved mask (two reads a display write, an AND) the macro takes the mask
  as the byte before each graphic byte. The replay knows which stream fed the AND, but the export does not yet carry
  it. A game with the mask after the graphic would draw inverted in skool2html. The geometry JSON is unaffected.
- **Horizontal blits are skipped.** A loop that writes ±1 in a pass (a horizontal copy, a scroller) is no blit, so a
  graphic drawn byte by byte across a row is not found. Manic Miner has none.
- **No beeper player signature.** The repo has no beeper engine code to take a signature from. The music detector
  knows the Vortex Tracker II PT3 player r.7 (from the WildPlayer bank 1 build). The AY rule (read-only data an `OUT
  ($FFFD)` writer reads) gives music data without a signature.
- **RNG and unpacker detectors are proven on fixtures only.** They are mechanism rules (a short routine that keeps a
  state through `LD A,R` or XOR plus rotate; a routine that copies back references from its own output). They need
  the P6 hold-out titles to show they generalise.
