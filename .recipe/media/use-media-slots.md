# Recipe: Media Slots — Floppies, SD Card, Folders, Swaps

Goal: see every media slot of the machine, put a file or a **host folder** into
the right one (floppy drive, tape deck, SD card), swap multi-disk software without losing
its saves, and keep or drop what the guest wrote.

Reference (verbs, options, errors, all surfaces): [docs/features/media.md](../../docs/features/media.md).
Related: [insert-disk.md](insert-disk.md) (the older drive-letter calls, disk inspection),
[insert-tape.md](insert-tape.md) (tape transport and the block catalog).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the `media`
> tool. Use [WebAPI](#webapi) inside host-side pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
media {"action":"list"}                                             # slots, media, dirty state, revision
media {"action":"targets","path":"/discs/dna_nemo.iso"}             # where it can go: slots in order, default, refusal
media {"action":"insert","slot":"auto","path":"/games/elite-1.trd"} # the slot comes from the content
media {"action":"insert","slot":"sd","path":"/home/me/zx/sdcard"}   # ZX-Evo: a folder as the SD card (FAT16)
media {"action":"insert","slot":"tape","path":"/home/me/zx/tapefiles"}  # a folder as a tape (then LOAD "")
media {"action":"swap","slot":"A","path":"/games/elite-2.trd","save":true}
media {"action":"export","slot":"sd","path":"scratch/card-after.img"}
media {"action":"eject","slot":"B","discard":true}
```

- `slot` takes `A`, `b:`, `fdd.b`, `sd`, `floppy:1`, `tag:sd+neogs` or (insert) `auto`.
- Paths are read by the **emulator** process (no upload here; `load_software` uploads).
- The reply's `slot` is the canonical id; `revision` grows with every change.
- **Unsure which slot?** Ask `targets` first. `default` is the index to use without asking
  (`null`: several slots fit - pick one, or ask the user); `refusal` says why nothing takes the
  file (a CD image on a machine without a CD-ROM drive). `insert auto` works when one slot takes
  the file (or for floppies: the first empty drive); with several it answers `ambiguous-slot`
  and names them.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
curl -s $BASE/emulator/$EMU_ID/media | jq '.slots[] | {id, aliases, state, medium}'

# Where a file can go (nothing is inserted)
curl -s "$BASE/emulator/$EMU_ID/media/targets?path=/discs/dna_nemo.iso" | jq '{file, targets, default, refusal}'

# A folder as the SD card, FAT32, room for 16 MiB of guest writes
curl -s -X POST $BASE/emulator/$EMU_ID/media/sd/insert -H 'Content-Type: application/json' \
     -d '{"path":"/home/me/zx/sdcard","fs":"fat32","free":16777216}' | jq '{ok, slot, report}'

# Two-disk game: disk 1 has a save on it
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd"}' | jq '{ok, error, message}'
#  -> {"ok":false,"error":"dirty","message":"slot 'fdd.a' has 1 unsaved changes: say save, export <path> or discard"}  (HTTP 409)
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd","save":true}' | jq '{ok, slot, pending}'
```

## Assert on

- `ok` and `error` (codes: `unknown-slot` 404, `dirty` / `recording` / `in-use` 409,
  `bad-request` / `ambiguous-slot` / `unknown-format` 400).
- `pending: false` after a sync request (the default): the medium is in. With
  `"async": true` the reply is immediate, `pending: true`, and the slot's
  `state` shows `pending` until the swap delay (floppy 2 s, SD 0.5 s) is over.
- `report` for skipped host files (`.DS_Store`, files that do not fit a TR-DOS disk).

## Pitfalls

- **A dirty medium never leaves silently** through `media`: add `save`,
  `export: <path>` or `discard`. The older `/disk/{drive}/eject` and
  `load_software` still replace / drop it as before.
- **A folder is never written.** Guest writes stay in the session; `export`
  them (a card image or a disk image) to keep them.
- **`save` needs a file of its own**: a disk built from a folder, a blank disk
  or a Hobeta file answers `not-supported` — `save` with a `path`, or `export`.
- **TTD recording fixes the media set**: `recording` (409) unless
  `"end_recording": true`.
- **Drive letters follow the machine**: `C` on a +3 is `unknown-slot`.
