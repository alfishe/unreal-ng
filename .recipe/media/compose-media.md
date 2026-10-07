# Recipe: Composite Media — One Disk from Folders, Images and ISOs

Goal: build a hard disk, SD card or CD from several sources at once (a system image at the bottom, host folders
and ISOs on top), see what it is made of, find out what the guest changed, and keep those changes the right way:
a session delta, one new image, a commit into the base image, or the guest's files written back into the folders.

Reference (the descriptor, every rule and option): [docs/features/media.md](../../docs/features/media.md#composite-media-several-sources-in-one-disk).
Related: [use-media-slots.md](use-media-slots.md) (slots, `insert`, `save`, `export`), [sprinter-hdd.md](sprinter-hdd.md)
(a DSS hard disk), [cd-audio.md](cd-audio.md) (CD drives).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the `media`
> tool. Use [WebAPI](#webapi) inside host-side pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## The descriptor

`<name>.ucompose.yaml` (or `.json`, or its JSON text inline). Paths are relative to the descriptor.

```yaml
version: 1
target: {free: 64MiB}                     # build auto|rebuild|graft, fs auto|fat16|fat32|iso9660, size, label, fixedTime
layers:                                   # bottom first: an upper layer's file shadows a lower one's
  - {name: dss,   source: {image: dss.img}}                        # a FAT image: grafted onto when it has room
  - {name: games, source: {iso: games.iso}, from: /GAMES, mount: /GAMES}
  - {name: work,  source: {folder: work}, mount: /WORK, writable: true, onDelete: trash}
writes: {save: delta, upper: work}        # what `save` does without a path; where copy-ups go on write-back
```

- `build: auto` (default) grafts onto the bottom FAT image (its MBR, loaders and system files stay where they
  are) and only reads the base directories the upper layers reach; else it rebuilds a new volume and says why.
- `partitions:` instead of `layers:` makes a partitioned disk: each entry a passthrough `{source: {image: x.img,
  partition: 1}}` or `{name: work, fs: fat16, compose: {layers: [...]}}`.
- Per layer: `include` / `exclude` (wildcards), `whiteout` / `opaque` (paths), `conflict` (`shadow`, `keep-lower`,
  `error`), and for folders `writable`, `onDelete` (`keep` default, `move` + `deletedFolder`, `trash`, `delete`,
  `ignore`).

## MCP (preferred)

```text
media {"action":"compose","path":"/zx/disk.ucompose.yaml"}           # build it without inserting: layout + report
media {"action":"insert","slot":"ide0.master","path":"/zx/disk.ucompose.yaml"}
media {"action":"layers","slot":"ide0.master"}                       # build (graft|rebuild|partitions), layers, counts
media {"action":"changes","slot":"ide0.master"}                      # the guest's writes as file operations + layer
media {"action":"save","slot":"ide0.master"}                         # S2: the session into <descriptor>.delta
media {"action":"flatten","slot":"ide0.master","strategy":"flat","path":"scratch/disk.img"}   # S1: one image
media {"action":"flatten","slot":"ide0.master","strategy":"commit","plan":true}               # S3 plan: nothing written
media {"action":"flatten","slot":"ide0.master","strategy":"commit"}                           # S3: into the base image
media {"action":"flatten","slot":"ide0.master","strategy":"write-back","plan":true}           # S4 plan
media {"action":"flatten","slot":"ide0.master","strategy":"write-back","onConflict":"keep-both"}  # S4
media {"action":"rescan","slot":"ide0.master"}                       # build again after the sources changed (clean only)
```

Pick the strategy:

| You want | Strategy | Writes |
|---|---|---|
| keep the session for next time, sources untouched | `save` (or `flatten` `delta`) | `<descriptor>.delta`; restored on the next insert ("session restored") |
| one ordinary image of what the guest sees | `flatten` `flat` + `path` | a new `.img` / `.vhd` / `.chd`; the slot then holds it |
| everything inside the bottom image | `flatten` `commit` | the base image, journaled in `<image>.ujournal` (raw, HDF, HDI, fixed VHD; not CHD) |
| the guest's files back on the host | `flatten` `write-back` | the `writable: true` folders; deletes per `onDelete`; attributes into `<descriptor>.attributes` |

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
D=/zx/disk.ucompose.yaml

# Build without inserting
curl -s "$BASE/emulator/$EMU_ID/media/compose?path=$D" | jq '{ok, compose: .compose | {build, fs, files, layers}, report}'

# Insert, then what it is made of
curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$D\"}" | jq '{ok, slot, report}'
curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/layers | jq '.layers | {build, files, layers, partitions}'

# What the guest changed, then the write-back plan, then the write-back
curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/changes | jq '.changes[] | {op, path, layer}'
curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/flatten -H 'Content-Type: application/json' \
     -d '{"strategy":"write-back","plan":true}' | jq '{ok, error, message, report}'
curl -s -X POST $BASE/emulator/$EMU_ID/media/ide0.master/flatten -H 'Content-Type: application/json' \
     -d '{"strategy":"write-back"}' | jq '{ok, error, message, report}'
```

## Assert on

- `compose` / `layers`: `build` (`graft`, `rebuild`, `partitions`), `files`, each layer's `files` and `writable`.
  A `report` line `rebuild instead of a graft: <why>` says why `auto` did not graft.
- `changes`: `op` (`create`, `modify`, `delete`, `rename`, `mkdir`, `rmdir`, `attributes`), `path` (`name:/PATH` on a
  partitioned disk), `layer` (empty: new). `warnings` for lost clusters and boot sectors.
- `flatten` with `plan: true`: `report` lists each step (`write` - a copy-up too, `mkdir`, `rename`, `remove`, `trash`, `move`,
  `whiteout`, `attributes`, `note`) and changes nothing. Errors list every conflict.
- `insert` of a descriptor with a delta: `session restored` in `report`; with a stale delta: the layer is named.

## Pitfalls

- **Sources are only read.** Until `save` / `flatten`, the guest's writes live in the session (and its journal).
- **A host file changed since the build is a conflict** for write-back (the plan fails and names each path):
  `onConflict: keep-both` writes the guest's version beside it instead.
- **A passthrough partition cannot be written back** (it is an image as it is): commit or flatten it.
- **`commit` refuses** a CHD base, a base another slot uses, and a guest volume with lost clusters (`force` overrides
  the last).
- **`rescan` is refused while dirty**: save or discard first.
- **Counts of a graft** (`files` of `layers`) read the untouched base directories at the first `layers` call: the
  first one costs a few milliseconds on a big base, later ones nothing.
