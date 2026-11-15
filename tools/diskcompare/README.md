# diskcompare.py

Compares a disk image (typically one the emulator exported after mastering a disk) with a reference dump, track by
track. Reads every format the sibling `tools/diskconverter` reads (**SCL, TRD, FDI, UDI, uncompressed TD0**) through
its `diskconverter` package, so the two images may be in different formats.

```
python3 diskcompare.py <image> <reference> [--image-format FMT] [--reference-format FMT] [--json] [-v]
```

## What is compared

Per physical track (cylinder, head) the sectors are compared in stream order:

| Field | Notes |
|---|---|
| C, H, R, N | N is the raw ID byte (`#F7` stays `#F7`) |
| data | bytes; the report gives the number of differing bytes and the first offset |
| deleted, ID-only | F8 mark; a sector without a data field |
| ID CRC | validity, and the two bytes on the disk when both sides know them |
| data CRC | validity, and the bytes when both sides know them and the data is the same |

A valid CRC is the computed one. A broken CRC is known only from formats that store the raw bytes (UDI); FDI, TRD
and TD0 keep just a flag, so for them only the validity is compared. When one track's sector list is a rotation of the
other's (the dump started at another index position) the rotation is undone before comparing.

## Unreliable reference tracks

A dumper that runs past the formatted area keeps returning the last track it found. A reference track that repeats
the previous track (logical order, `cylinder * heads + head`) sector for sector and whose ID fields all claim a
cylinder other than the physical one is flagged `unreliable-dump` and skipped. Example: in `ZXF61_O.FDI` logical
tracks 120..159 repeat track 119 (the key track, `C=#A4 H=#42 R=#F7 N=#F7`). A repeat whose IDs carry the physical
cylinder is compared as usual.

Tracks outside the reference geometry (the image has more cylinders or heads) are listed as `extra` and are not
mismatches.

## Output

The text report lists every mismatching track with its differences, the skipped and extra tracks, and a summary
line; `-v` lists every track. `--json` prints the same as JSON:

```json
{"image_path": "...", "reference_path": "...",
 "image": {"cylinders": 83, "heads": 2}, "reference": {"cylinders": 80, "heads": 2},
 "summary": {"match": 119, "mismatch": 1, "unreliable-dump": 40, "extra": 6},
 "tracks": [{"cylinder": 0, "head": 0, "logical": 0, "status": "match", "differences": []}, ...]}
```

Reader warnings go to stderr, so `--json` output stays clean.

## Exit codes

`0` every compared track matches; `1` at least one mismatch; `2` bad usage or an image could not be read.

## Tests

`python3 -m pytest tools/diskconverter/tests/test_diskcompare.py` (synthetic images: match, rotated track, raw N
mismatch, ID CRC value and validity mismatches, duplicate reference tracks skipped, extra cylinders).
