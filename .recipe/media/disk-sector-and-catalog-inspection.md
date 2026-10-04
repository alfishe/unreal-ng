# Recipe: Inspect a Mounted Disk (Catalog, Sectors, Raw Tracks, Image)

Scope: reading a floppy that is already in a drive, below the file level:
the drives overview, the TR-DOS system sector and catalog, one sector with
its address-mark and CRC verdicts, a track's sector table, the raw MFM/FM
byte stream of a track with its clock bitmap, the whole image as bytes, and
creating or ejecting a disk. Use it to check what a disk really contains
(fake catalog entries, odd sector layouts, bad CRCs). Mounting a disk, the
image formats and booting are in [insert-disk.md](insert-disk.md); every
slot, folders and swaps are in [use-media-slots.md](use-media-slots.md); the
protection-hunting workflow that puts these calls to use is
[articles/disk-protection-triage.md](../articles/disk-protection-triage.md).

Ground truth: the handlers in
[tape_disk_api.cpp](../../core/automation/webapi/src/api/tape_disk_api.cpp)
(`getDiskDrives`, `getDiskInfo`, `getDiskSysinfo`, `getDiskCatalog`,
`getDiskSector`, `getDiskSectorRaw`, `getDiskTrack`, `getDiskTrackRaw`,
`getDiskImage`, `createDisk`, `ejectDisk`); the feature landed with
[2026-01-17-disk-inspection](../../docs/inprogress/2026-01-17-disk-inspection/DONE.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. The
> inspection endpoints have no dedicated tool: call them with `invoke_api`
> (`{id}` is replaced with the resolved target). Use [WebAPI](#webapi) only
> inside host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

**Drive names:** `A`-`D` (any case) or `0`-`3`, the same in every path
(`/disk/B/...` is `/disk/1/...`). Anything else is `400` ("use A-D or
0-3"). Which drives a machine really has depends on the model (Beta Disk:
A-D; +3: A and B; Sprinter: its own pair, see
[insert-disk.md](insert-disk.md)).

## MCP (preferred)

```text
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk"}                       # all drives
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/info"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sysinfo"}             # TR-DOS system sector
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}

# One sector (logical decode) and the same sector as raw stream bytes
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sector/0/0/1"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sector/0/0/1/raw"}

# A track: sector table, then the raw stream and its clock bitmap
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/track/0/0"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/track/0/0/raw"}

# The whole image (large)
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/image"}

# Create a blank disk / eject
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/disk/B/create","body":{"format":"unformatted","cylinders":80,"sides":2}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/disk/A/eject"}
media      {"action":"eject","slot":"A"}                        # the same through the media tool
inspect_state {"aspects":["fdc"]}                               # live controller registers (Beta Disk, +3)
```

## WebAPI

```bash
BASE=http://localhost:8090/api/v1

# Drives overview and one drive
curl -s "$BASE/emulator/$EMU_ID/disk" | jq '{mounted_count, auto_selected, drives: [.drives[] | {letter, mounted, file, write_protected, cylinders, sides}], fdc_state}'
curl -s "$BASE/emulator/$EMU_ID/disk/A/info" | jq '{status, drive, file, write_protected}'   # /disk/A is the same report

# TR-DOS: system sector (track 0, side 0, sector 9) and the directory
curl -s "$BASE/emulator/$EMU_ID/disk/A/sysinfo" | jq '{dos_type, disk_type_decoded, label, file_count, free_sectors, first_free_track, first_free_sector, trdos_signature, signature_valid}'
curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" | jq '{file_count, files: [.files[] | {name, type, start, length, sectors, first_track, first_sector}]}'

# One sector: cylinder / side / sector number
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/9" \
  | jq '{address_mark, has_data, data_size, data_mark, data_crc_valid, deleted}'
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/9" | jq -r '.data_preview'            # first 64 bytes as hex
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/9" | jq -r '.data_base64' | base64 -d > scratch/sector.bin

# Raw sector: ID mark through data CRC, straight from the track stream
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/9/raw" | jq '{raw_offset, raw_size, data_size, has_data}'

# Track summary: every sector's ID, size code and CRC verdicts
curl -s "$BASE/emulator/$EMU_ID/disk/A/track/0/0" \
  | jq '{encoding, raw_size, sector_count, sectors: [.sectors[] | {index, id_cyl, id_head, id_sector, id_size_code, id_crc_valid, data_crc_valid, has_data, deleted}]}'

# Raw track bytes and the clock bitmap (which bit cells carry missing-clock marks)
curl -s "$BASE/emulator/$EMU_ID/disk/A/track/0/0/raw" > scratch/track-0-0.json
jq '{raw_size, encoding}' scratch/track-0-0.json
jq -r '.raw_base64' scratch/track-0-0.json | base64 -d > scratch/track-0-0.bin
jq -r '.clock_bitmap_base64' scratch/track-0-0.json | base64 -d > scratch/track-0-0.clk

# Whole image as one buffer: cylinders * sides tracks, back to back
curl -s "$BASE/emulator/$EMU_ID/disk/A/image" > scratch/image.json
jq '{cylinders, sides, total_tracks, track_size, image_size}' scratch/image.json
jq -r '.image_base64' scratch/image.json | base64 -d > scratch/image.raw

# Create (see insert-disk.md for the formats) and eject
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/B/create" -H 'Content-Type: application/json' \
     -d '{"format":"plus3"}' | jq '{success, format, cylinders, sides}'
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/B/eject" | jq '{status, message}'
```

Lua and Python have `disk_info(drive)`; CLI `disk create B 80 2 unformatted`
(see [insert-disk.md](insert-disk.md)). The sector, track, raw and image
reads are WebAPI/MCP (`invoke_api`) only.

## What each response tells you

- **`info`**: `status` is `inserted` or `empty`; `file` is the image path (a
  blank disk reports `<blank>`); `write_protected`. A drive that does not
  exist on this machine answers `status: "unavailable"`.
- **`sysinfo`** (TR-DOS only, reads track 0 sector 9): `disk_type` and
  `disk_type_decoded`, `label`, `file_count`, `free_sectors`,
  `first_free_track` / `first_free_sector`, `trdos_signature`,
  `signature_valid` (true when the byte is `0x10`). A false `signature_valid`
  on a disk that boots is a protection or a non-TR-DOS format.
- **`catalog`**: `files[]` `{name, type, start, length, sectors,
  first_sector, first_track}` straight from the directory; names are 8
  characters, space padded. It always labels the result `dos_type:
  "TR-DOS"`, so on a CP/M, +3 or MGT image the entries are garbage: use the
  sector calls instead.
- **`sector`**: `address_mark` `{id_mark, cylinder, head, sector,
  sector_size, crc, crc_valid, offset}` is the ID field as *recorded*;
  `data_mark`, `data_crc`, `data_crc_valid`, `deleted` (deleted-data mark),
  `has_data`, `data_size`. Compare the recorded `cylinder`/`head`/`sector`
  with the ones you asked for: protections write IDs that lie.
- **`sector/.../raw`**: `raw_offset` and `raw_size` locate the sector inside
  the track stream, `raw_base64` carries the bytes from the ID mark's sync
  through the data CRC.
- **`track`**: `encoding` (`MFM` or `FM`), `raw_size` (bytes in the track
  stream), `sector_count`, and per sector `index` (position on the track),
  `id_*` fields, `id_crc_valid`, `data_crc_valid`, `has_data`, `data_size`,
  `deleted`. A non-ascending `id_sector` order, a size code other than 1
  (256 bytes) or a `false` CRC is the interesting part.
- **`track/.../raw`**: `raw_size`, `encoding`, `raw_base64` (the byte
  stream) and `clock_bitmap_base64` (one bit per stream bit, set where a
  clock bit is present; missing-clock address marks show there).
- **`image`**: `cylinders`, `sides`, `total_tracks`, `track_size`,
  `image_size` and `image_base64`: every track's raw stream concatenated,
  zero-filled for a missing track. It is a *raw stream dump*, not a `.trd`;
  to export a file in a format use the media export in
  [use-media-slots.md](use-media-slots.md).

## Typical uses

| Question | Calls |
|:--|:--|
| What is on this disk? | `sysinfo` then `catalog` |
| Is a catalog entry real? | `catalog` entry's `first_track`/`first_sector`, then `sector/{cyl}/{side}/{sec}` and look at `data_preview` |
| Does the disk have a non-standard layout? | `track/{c}/{s}` on several tracks: sector order, size codes, `sector_count` |
| Is there a CRC-protected or weak sector? | `track` -> `data_crc_valid: false`; raw bytes through `track/.../raw` |
| Is there a missing-clock mark? | `track/.../raw` -> `clock_bitmap_base64` vs `raw_base64` |
| What did the guest write? | `image` before and after, diff the decoded bytes (and see `Dirty writes` in [insert-disk.md](insert-disk.md)) |

## Pitfalls

- **The sector number is a position, not the recorded ID.** The `{sec}` in
  `/sector/{cyl}/{side}/{sec}` is 1-based and indexes the track's sectors
  in the order found on the track (`sec - 1`), the same way TR-DOS numbers
  them on a standard disk. On a disk with shuffled or duplicated IDs, ask
  `track/{cyl}/{side}` first and read `index`; `sec` 0 is a `400`
  ("Sector must be >= 1").
- **`data_base64` of the logical `sector` call is always 256 bytes** (the
  handler encodes a fixed 256 bytes), even when `data_size` says 512 or
  more. For a larger sector take the `raw` call, whose `raw_size` is the
  real extent, or use the `image` and slice it.
- **Empty or missing parts are `400`/`404`, not empty JSON.** No disk in the
  drive or no drive: `400` ("Disk drive not available" / "No disk image");
  a missing track or sector: `404` ("Track not found", "Sector not found
  (may need reindex)"). The ranges are not checked up front: ask only for
  cylinders and sides the disk has (`cylinders`/`sides` in `/disk`).
  Non-numeric `cyl`/`side`/`sec` is not validated by the handler
  (unconfirmed: expect a failure, not a clean `400`).
- **`sysinfo` reads track 0 sector 9; `404` if it is missing** ("may need
  reindex"): a non-TR-DOS or damaged disk. It is not a sign that the disk
  is unreadable.
- **Reads are of the in-memory image**, live: a guest `SAVE` shows up
  immediately in `catalog`. The raw track bytes are the emulator's track
  model as loaded from the container, so for container-level questions
  (what a `.td0` or `.scl` held) look at the file itself; see
  [author-udi-images.md](author-udi-images.md) for the format limits.
- **Raw responses are large.** `image` of an 80x2 disk is several hundred
  KB of base64 in one JSON; write it to `scratch/` and decode there, do not
  print it.
- **Create while running** can be refused with `409` (the handler returns
  the refusal text, for instance during a TTD recording); `format` is
  `auto`, `unformatted` or `plus3`, `cylinders` 40 or 80, `sides` 1 or 2,
  otherwise `400`. Eject of a drive the machine lacks fails (status unconfirmed).
- **Live FDC state is a different endpoint.** These calls read the image;
  what the controller is doing right now (phase, last command, motor) is
  `inspect_state {"aspects":["fdc"]}`.
