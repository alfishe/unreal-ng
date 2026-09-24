# Recipe: Creating Proper UDI Images (Preserving Protection Layouts)

UDI is the lossless native format for the universal track model: every track
is the raw byte stream plus a clock bitmap, so arbitrary CHRN geometry,
duplicate/missing/ID-only sectors, deleted (`F8`) marks, deliberate CRC
errors, FM/mixed encodings and non-standard track lengths all survive. When
TRD/SCL refuse an image ("non-standard geometry"), UDI is the target.

**The one hard limit** (verified, `loader_udi.cpp`): UDI **cannot store
weak/flaky bits**. Saving a track with weak bits writes it without them and
warns `UDI cannot store weak bits: track cylinder N side M saved without
them`; the reserved multi-revolution track type (`0x80|n`) is not implemented.
Flaky-sector protections require **SCP, HFE or extended DSK** — see
[physical-protection-forensics.md](../articles/physical-protection-forensics.md).

The clock bitmap is *missing-clock marks* (A1/C2 sync positions), not weak
bits — related but distinct concepts.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred for every
> emulator interaction (insert, verify, driving the formatter). Paths A/B/D
> below are host-side CLI tooling — transport-neutral. Use [WebAPI](#webapi)
> only inside host-side Python/bash pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

Everything the emulator does in this recipe:

```text
load_software       {"path":"scratch/protected.udi"}              # insert your authored image (Path B/C output)
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/track/40/1/raw"}
                    #   → track_size + clock_bitmap_base64 in structuredContent
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sector/40/0/1/raw"}
control_execution  {"action":"run_frames","frames":600}          # Path C: let the formatter write tracks
type_input         {"action":"type","text":"RANDOMIZE USR 60000","tokenized":true}
inspect_state      {"aspects":["screen_ocr"]}                      # formatter done? menu says so
```

The final Save As `.udi` (Path C) is a Qt UI step — no smart tool, no
WebAPI endpoint; see the note in Path C.

## Path A — host-side conversion (fully automated)

`tools/diskconverter` (pure Python, no emulator needed):

```bash
# sector-preserving chain, any order: TRD ↔ FDI ↔ UDI
python3 tools/diskconverter/diskconvert.py scratch/game.fdi scratch/game.udi

# lossy targets refuse instead of corrupting:
python3 tools/diskconverter/diskconvert.py scratch/protected.udi scratch/out.trd
# → error: non-standard geometry (that refusal is the point)
```

Sector bytes and ID fields round-trip exactly (verified for
`scl → trd → fdi → udi → trd → scl`). TD0 is read-only input.

## Path B — host-side authoring of protection layouts

Import the package and build tracks sector-by-sector (real API,
`diskconverter/disk_image.py`); `mfm.encode_track` synthesizes the raw MFM
stream + clock bitmap on save:

```python
import sys; sys.path.insert(0, 'tools/diskconverter')
from diskconverter import DiskImage
from diskconverter.disk_image import Sector
from diskconverter.formats import udi

disk = DiskImage(cylinders=80, heads=2)
sec = lambda r, **kw: Sector(cylinder=40, head=0, number=r,
                             size_code=1, data=bytes(256), **kw)
t = disk.track(40, 0)
t.sectors += [sec(1), sec(1)]          # duplicate sector IDs (R=1 twice)
t.sectors.append(sec(0xC5))            # sector number R >= 0xC0
t.sectors.append(sec(2, crc_valid=False))   # deliberate data-CRC error
t.sectors.append(sec(3, deleted=True))      # deleted data mark (F8)
t.sectors.append(Sector(cylinder=40, head=0, number=4,   # ID-only sector
                        size_code=1, data=b'', no_data=True))
t.sectors.append(Sector(cylinder=40, head=0, number=5,   # 512-byte (N=2)
                        size_code=2, data=bytes(512)))
# ID/position mismatch: a sector on side 1 whose CHRN claims H=0
disk.track(40, 1).sectors.append(sec(1))

udi.write(disk, 'scratch/protected.udi')
```

Sector-level flags cover CHRN games, duplicates, bad CRC, F8, ID-only and
size tricks. Raw-stream-level features — FM/mixed-encoding tracks, exotic
gaps, non-standard track lengths — need Path C or D (the Python writer emits
MFM with synthesized gaps only).

Then verify what you built (never ship an image you did not re-read):

```bash
python3 tools/diskinfo/diskinfo.py scratch/protected.udi --tracks
# protection analysis block should list exactly the signals you planted
python3 tools/diskconverter/diskconvert.py scratch/protected.udi scratch/rt.udi  # udi→udi sanity
```

## Path C — author inside the emulator (byte-authentic)

The emulated WD1793 WRITE TRACK records clock marks, so a disk formatted by
*real formatter code* inside the emulator and saved as UDI is indistinguishable
from one imaged from real hardware:

1. `POST /disk/A/create` — blank disk (or insert a blank image).
2. Run the original formatter / protection-installer (type it, or assemble via
   MCP `debug_code` `assemble`, or load its code snapshot).
3. Let it format/write through the FDC at authentic speed.
4. Save: Qt **Save As `.udi`** (Disk menu).

There is **no WebAPI disk-save endpoint** (`GET /disk/{drive}/image` returns a
raw concatenated track dump, not a format file) — the save step is the one
manual click; everything before it is automatable.

## Path D — real-hardware dumps

KryoFlux/Greaseweazle flux dumps: export **SCP or HFE** (both loaders
implemented) — these are also the only formats that carry weak bits through.
`scratch/` them, then insert directly. The flux-bridge work (PLAN.md T2 #12)
will wire Greaseweazle capture live; loaders already read the files.

## Choosing the format (capability matrix)

| Need | TRD | FDI | UDI | SCP/HFE/extDSK |
|:--|:--|:--|:--|:--|
| standard 16×256 TR-DOS only | yes | yes | yes | yes |
| arbitrary CHRN / duplicates / ID-only | refuse | yes | yes | yes |
| deleted marks, deliberate bad CRC | refuse | yes | yes | yes |
| FM / mixed-encoding tracks | refuse | no | yes | yes |
| **weak / flaky bits** | no | no | **no** | **yes** |
| written by tools/diskconverter | yes | yes | yes | no (read-only loaders) |

## WebAPI

Curl form of the same verification (for Python/bash pipelines):

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -H 'Content-Type: application/json' -d '{"path":"scratch/protected.udi"}' | jq .
curl -s "$BASE/emulator/$EMU_ID/disk/A/track/40/1/raw" \
  | jq '{track_size, has_clock: (.clock_bitmap_base64 | length > 0)}'
```

Reads through the FDC must return your planted bytes/IDs: round-trip a
`sector/{cyl}/{side}/{sec}/raw` request against what you authored. If a
protection relies on flaky behavior, also test the UDI copy *fails* the check
(weak bits were dropped) while the HFE original passes — that differential is
proof of flaky-bit reliance.

## Pitfalls

- **Weak bits silently dropped on save** — always read loader/tool warnings;
  the emulator's Qt save shows the same warning.
- **Compressed `"udi!"` variant** is rejected outright; convert it externally.
- **UDI CRC is not zlib CRC-32** — it is the `crcUDI` signed-arithmetic-shift
  variant (`fdc.h`); hand-written files must use `LoaderUDI::computeCrc` or
  the Python writer.
- **Extended header / trailer round-trip** verbatim — do not strip the TRX2X
  comment when editing files in place.
- **diskinfo's UDI decode is sector-level** (it re-derives sync positions and
  skips the clock bitmap) — inspect clock marks via the WebAPI `track/raw`
  response instead.
