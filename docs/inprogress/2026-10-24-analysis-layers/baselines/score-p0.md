# P0 baseline: reference scores for Manic Miner

The reference scorer ([score_draft.py](../../../../tools/verification/analysis/score_draft.py), how to run:
[README](../../../../tools/verification/analysis/README.md)) on today's automatic draft and on the two hand-made
runs of 2026-10-24. Every later phase reports the same table for its export. No agent was involved in any number
here.

Reference: skoolkid's `mm.skool` (SkoolKit 10.x, decimal addresses). Program range $8000-$FFFF (the game image; the
reference's buffers below $8000 and its `@isub` stub at $7FFD are outside it). Categories: `c` = code; `b`, `w`,
`g`, `s` = data; `t` = text; `u` = unused; `i` or no entry = unknown.

## Summary

| Draft | Byte categories (by entry) | by line / sub-block | Code entries | Graphics regions covered by data | ... starting at a draft boundary | Records |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `sna2ctl -m` (all 14 run-B maps merged), no agent | **79.8 %** | 81.8 % | **28 / 33** | 44 / 106 (62 partial) | 21 / 106 | n/a |
| run B `game.ctl` (zxinit, fuzz map only), no agent | 79.7 % | 81.7 % | 27 / 33 | 44 / 106 (62 partial) | 21 / 106 | n/a |
| run A final skool (`compare/A.skool`) | **92.4 %** | 92.4 % | **33 / 33** (+2 extra) | 106 / 106 | 22 / 106 | n/a |
| run B final skool (`compare/B.skool`) | **92.3 %** | 92.3 % | **33 / 33** (+5 extra) | 106 / 106 | 22 / 106 | n/a |

Targets from [plan.md](../plan.md#how-we-measure): categories ≥ 95 %, code entries ≥ 32 of the reference's,
all 5 record layouts, ≥ 90 % of graphics with the right geometry. Records and geometry are "n/a" for all four
drafts: none of them states field lists or geometry in a machine-readable form (the scorer's `--fields` and
`--geometry` inputs are for the P3 / P4 export).

## Corrections to the proposal

- **"90.8 % byte categories by hand"** is right only under zx-reaper's `zxcompare.py` convention: `g` entries are a
  separate category "vars", the reference is read in `@isub` mode (which adds the `c$7FFD JP 33792` stub) and the
  range starts at $7FFD. The scorer reproduces it exactly in that mode (`--split-vars --isub`): run A 29,749 bytes
  agree (90.8 %), run B 29,762 (90.8 %). With the plan's categories (`g` counted as data, range $8000-$FFFF) the
  hand runs reach **92.4 % (A) and 92.3 % (B)**; the 95 % target is measured on this scale.
- **"≥ 32 of 34 code entries"**: the reference has **33** `c` entries in the program range. The 34th is the
  `@isub+begin` stub at $7FFD, which exists only in the assembled `@isub` output, not in the tape image. Both hand
  runs match all 33; the plain `sna2ctl -m` gets 28.
- **"Manic Miner has 2,232 bytes of leftover source"** (README, text detector): the reference's unused entry
  `u37708` is **2,484** bytes ($934C-$9CFF). 2,232 is the part run A happened to classify as text; it called the
  remaining 252 bytes data.

## Where the automatic baseline loses

`sna2ctl -m` against the reference, by entry type (rows: draft, columns: reference):

| draft \ reference | code | data | text | unused | unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| code | 3463 | 0 | 0 | 0 | 0 |
| data | 0 | 22388 | 1 | 1221 | 0 |
| text | 0 | 4130 | 298 | 1267 | 0 |
| unused | 0 | 0 | 0 | 0 | 0 |
| unknown | 0 | 0 | 0 | 0 | 0 |

- Code bytes are all right (100 % code vs non-code): the merged map covers all 1,719 executed instructions.
- 4,130 data bytes are called text: sna2ctl's text heuristic fires on printable runs in graphics, attributes and
  the cavern records. This is the largest single loss (12.6 points).
- The leftover source (2,488 unused bytes in the reference) is split between data and text; sna2ctl has no
  "unused" category.
- Code entries missed: $8684 $870E $8944 $8FF4 $9028, each joined to the entry above it. Three are reached by
  fall-through from the routine above ($8684, $870E, $8FF4: the instruction before is not a jump or return);
  $8944 follows a `JP` and $9028 a `RET`, with executed code directly before them.
- Graphics: 62 of the 106 reference graphics regions are cut by a `t` entry somewhere inside (partial); only the
  cavern tile blocks start at a draft boundary (19 of 20), by luck of the zero runs.

The hand runs, by entry type:

| run A: draft \ reference | code | data | text | unused | unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| code | 3463 | 0 | 0 | 0 | 0 |
| data | 0 | 26502 | 0 | 256 | 0 |
| text | 0 | 16 | 299 | 2232 | 0 |
| unused | 0 | 0 | 0 | 0 | 0 |
| unknown | 0 | 0 | 0 | 0 | 0 |

| run B: draft \ reference | code | data | text | unused | unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| code | 3463 | 0 | 0 | 0 | 0 |
| data | 0 | 26470 | 0 | 2484 | 0 |
| text | 0 | 48 | 299 | 4 | 0 |
| unused | 0 | 0 | 0 | 0 | 0 |
| unknown | 0 | 0 | 0 | 0 | 0 |

Almost all of the hand runs' loss (7.6 and 7.7 points) is one decision: 2,488 of A's 2,504 wrong bytes and of B's
2,536 are the reference's `u` bytes (the 2,484-byte leftover source text plus 4 bytes at $841B). Neither run used
`u`: A called most of the source text text, B called it data. Code entries only in the drafts: A $8691 $88FF;
B $8691 $88A8 $88FF $8C83 $8E5F (all of them `*` entry points inside a routine in the reference; they cost no
bytes). The hand runs split the cavern records into one line per field with an instruction comment, not with
mid-block comments or separate entries, so only the 20 cavern layouts, the title screen and the Willy sprite start
at a boundary the scorer can see (22 of 106).

## Commands

Run from the worktree root. `MM_SKOOL` is skoolkid's Manic Miner disassembly (`sources/mm.skool` of the
skoolkid/manicminer repository; on the dev machine a clone in `/Volumes/TB4-4Tb/Projects/emulators/github/`).
`scratch/reaper-mm/` holds the run artifacts of 2026-10-24 (not in the repo).

```bash
MM_SKOOL=/Volumes/TB4-4Tb/Projects/emulators/github/skoolkid-manicminer/sources/mm.skool
S=tools/verification/analysis/score_draft.py
E=tools/verification/analysis/expected/manic-miner-records.json

# SkoolKit 10.1 for sna2ctl (sna2ctl takes a single -m, so the maps are merged first; all are rzxplay-style
# "$XXXX" lists)
python3 -m venv scratch/skoolkit-venv && scratch/skoolkit-venv/bin/pip install skoolkit==10.1
mkdir -p scratch/analysis-p0
cat scratch/reaper-mm/run-B/re/maps/*.map | grep -v '^\s*$' | sort -u > scratch/analysis-p0/run-b-all.map   # 1,719 addresses
scratch/skoolkit-venv/bin/sna2ctl.py -m scratch/analysis-p0/run-b-all.map scratch/reaper-mm/run-B/re/orig.z80 \
    > scratch/analysis-p0/run-b-sna2ctl.ctl

# the four rows of the summary
python3 $S scratch/analysis-p0/run-b-sna2ctl.ctl $MM_SKOOL --expected $E --json scratch/analysis-p0/score-sna2ctl.json
python3 $S scratch/reaper-mm/run-B/re/game.ctl   $MM_SKOOL --expected $E --json scratch/analysis-p0/score-gamectl-B.json
python3 $S scratch/reaper-mm/compare/A.skool     $MM_SKOOL --expected $E --json scratch/analysis-p0/score-A.json
python3 $S scratch/reaper-mm/compare/B.skool     $MM_SKOOL --expected $E --json scratch/analysis-p0/score-B.json

# the zxcompare.py convention behind "90.8 %" (zxcompare's ranges: $7FFD-$FFF0 for A, $7FFD-$FFF8 for B)
python3 $S scratch/reaper-mm/compare/A.skool $MM_SKOOL --split-vars --isub --start 0x7FFD --end 0xFFF1
python3 $S scratch/reaper-mm/compare/B.skool $MM_SKOOL --split-vars --isub --start 0x7FFD --end 0xFFF9
```

Run A's `game.ctl` is byte-identical to run B's (both come from zxinit), so it scores the same.
