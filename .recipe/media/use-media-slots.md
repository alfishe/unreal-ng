# Recipe: Media Slots — Floppies, SD Card, Hard Disks (CHD), Folders, Swaps

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
media {"action":"insert","slot":"hd","path":"/mame/sp_hdd_sys.chd"}       # a MAME CHD: any IDE unit or SD card
media {"action":"save","slot":"hd"}                                  # write the guest's changes into the CHD
media {"action":"export","slot":"hd","path":"scratch/disk.chd","compression":"zstd"}
media {"action":"export","slot":"hd","path":"scratch/diff.chd","parent":"/mame/sp_hdd_sys.chd"}  # only the changes
media {"action":"export","slot":"hd","path":"scratch/disk.vhd"}                     # raw data + a fixed VHD footer
media {"action":"export","slot":"hd","path":"scratch/flat.img","compact":true}      # FAT re-synthesized: files contiguous
media {"action":"flatten","slot":"hd","strategy":"commit","plan":true}  # what a commit into the graft base writes
media {"action":"flatten","slot":"sd","strategy":"write-back","plan":true}  # guest files back into writable folder layers
media {"action":"changes","slot":"hd"}   # the guest's unsaved writes as file operations, with their layers
media {"action":"eject","slot":"B","discard":true}
media {"action":"info","slot":"A"}                                   # one slot: medium, access, dirty state (a CD: the disc's tracks)
media {"action":"formats","kind":"floppy"}                           # accepted extensions per kind: floppy, tape, block, optical
media {"action":"rescan","slot":"sd"}                                # re-read a host folder after it changed (refused while dirty)
media {"action":"create","slot":"B","format":"plus3"}                # blank floppy; a block slot needs "size" (bytes, multiple of 512, up to 2 GiB)
media {"action":"protect","slot":"A","on":true}                      # the write-protect switch
media {"action":"insert","slot":"ide0.master","path":"/discs/game.iso","device":"cdrom"}  # an empty IDE unit becomes a CD-ROM drive (device: disk | cdrom | cf)
media {"action":"insert","slot":"ide0.master","path":"/music/album","device":"cdrom","format":"audio-cd"}  # a folder of MP3 / FLAC / WAV as an audio CD
```

- Verbs: `list info formats targets insert eject swap save export discard rescan create protect compose layers changes`.
- `insert` / `swap` options: `access` (`readonly` | `session` | `writethrough`), `format` (a hint, e.g. `audio-cd`),
  `fs` (`fat16` | `fat32`), `codepage` (`cp866` | `cp1251`), `free` (bytes of room for guest writes), `wp` (insert write-protected),
  `kind`, `device`, `immediate` (skip the swap delay), plus `save` / `export` / `discard` / `end_recording` / `async`.
  `swap` takes every `insert` option.
- `save` takes `retarget` (a disk that no longer fits its format is kept losslessly as `.udi`) and `compression`; `export` takes `compression` and `parent`; save to a path and export take `vhd` (`fixed`, the default, or `dynamic`: a sparse VHD) for a `.vhd` target.
  Both take `compact` (a FAT disk or card written as a new volume: every file contiguous, deleted data and lost
  clusters gone, label / MBR / boot code carried), with `fs` (`fat16` / `fat32`: converts; a FAT12 floppy needs it)
  and `size` (bytes or `64MiB`; default: the medium's size). `save` with `compact` needs a `path`; the medium then
  reads the new file. Raw exports are sparse (zero sectors not written); a `.vhd` target gets a fixed VHD footer.
- `changes` (block media with session writes): the guest's unsaved writes as file operations - `create`, `modify`,
  `delete`, `rename` (also a move; `oldPath`), `mkdir`, `rmdir`, `attributes` - each with the layer of a composite it
  touched (empty: new). `warnings` names lost clusters and changed boot sectors. Nothing is written. On a composite
  only the directories the writes touched are read; on other media every directory is (`fullScan: true`).
- Composites (`*.ucompose.yaml`) and `save`: without a path the session goes into `<descriptor>.delta` (S2; the
  descriptor's `writes.save` / `writes.delta` can say otherwise) and the medium is clean; the next insert of the same
  descriptor restores it ("session restored"). A delta written over other sources (a host file changed since) is not
  applied: the report names the layer, and a new `save` over it needs `force`. A damaged delta is renamed
  `*.delta.bad`. `strategy: flat` with a path writes one image instead (as `export`, then the slot holds that image).
- A partitioned disk (`*.ucompose.yaml` with `partitions:` instead of `layers:`): each entry is a passthrough
  `{source: {image: x.img, partition: 1}}` or a composition `{fs: fat16, size: 64MiB, compose: {build: graft,
  layers: [...]}}`; partitions are 1 MiB aligned, more than four go logical. The first source MBR's boot code is
  carried (the Profi BIOS runs it). `media layers` lists the partitions; `media changes` paths read `name:/PATH`.

- `slot` takes `A`, `b:`, `fdd.b`, `sd`, `floppy:1`, `tag:sd+neogs` or (insert) `auto`.
- Paths are read by the **emulator** process (the `media` tool does not upload; `load_software` does, and the WebAPI `insert` / `swap` also take a multipart file or a raw body with `X-Filename`).
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

# A MAME CHD (hard disk or SD card), then a zstd copy and a raw image of it
curl -s -X POST $BASE/emulator/$EMU_ID/media/sd/insert -H 'Content-Type: application/json' \
     -d '{"path":"/mame/neogs.chd"}' | jq '{ok, slot, report}'
curl -s -X POST $BASE/emulator/$EMU_ID/media/sd/export -H 'Content-Type: application/json' \
     -d '{"path":"/tmp/card-zstd.chd","compression":"zstd"}' | jq '{ok, error, message}'
curl -s -X POST $BASE/emulator/$EMU_ID/media/sd/export -H 'Content-Type: application/json' \
     -d '{"path":"/tmp/card.img"}' | jq '{ok}'

# Two-disk game: disk 1 has a save on it
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd"}' | jq '{ok, error, message}'
#  -> {"ok":false,"error":"dirty","message":"slot 'fdd.a' has 1 unsaved changes: say save, export <path> or discard"}  (HTTP 409)
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd","save":true}' | jq '{ok, slot, pending}'
```

## Assert on

- `ok` and `error` (codes: `unknown-slot` 404, `dirty` / `recording` / `in-use` 409,
  `bad-request` / `ambiguous-slot` / `unknown-format` / `unreadable-source` / `does-not-fit` / `kind-mismatch` 400,
  `not-supported` 501, `io-error` 500, `cancelled` 499).
- `pending: false` after a sync request (the default): the medium is in. With
  `"async": true` the reply is immediate, `pending: true`, and the slot's
  `state` shows `pending` until the swap delay (floppy 2 s, SD 0.5 s, IDE CD-ROM 3 s) is over; `immediate` skips it.
- `report` for skipped host files (`.DS_Store`, files that do not fit a TR-DOS disk).

## Pitfalls

- **A dirty medium never leaves silently** through `media`: add `save`,
  `export: <path>` or `discard`. The older `/disk/{drive}/eject` and
  `load_software` still replace / drop it as before.
- **A folder is never written.** Guest writes stay in the session; `export`
  them (a card image or a disk image) to keep them.
- **`save` needs a file of its own**: a disk built from a folder, a blank disk
  or a Hobeta file answers `not-supported` — `save` with a `path`, or `export`.
- **A CHD is never written in place**: guest writes stay in the session (an IDE unit's default
  `writethrough` becomes `session`, said in `report`); `save` writes the CHD again (its codecs, or
  `compression`: `none`, `default`, `lzma,zlib,huff,flac,zstd`), `export <x>.chd` writes a new one,
  `export <x>.img` a raw image. A child CHD needs its parent `.chd` in the same folder. A CD-ROM CHD
  goes into a CD drive ([cd-audio.md](cd-audio.md)). Format: [chd.md](../../docs/file-formats/disk-images/chd.md).
- **TTD recording fixes the media set**: `recording` (409) unless
  `"end_recording": true`.
- **Drive letters follow the machine**: `C` on a +3 is `unknown-slot`.
