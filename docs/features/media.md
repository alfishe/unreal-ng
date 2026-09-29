# Media: drives, cards and what is in them

Every place a medium goes on the running machine — floppy drives, the tape deck, the SD card
socket, later hard disks and CD — is a **slot**. One set of verbs puts media in and takes them out,
the same on the Qt media panel, the WebAPI, the CLI, MCP, Lua and Python. This page is the
reference; each surface's own documentation links here.

Design: [media-control-design.md](../inprogress/2026-09-28-storage-manager/media-control-design.md);
layers of a storage peripheral: [technical-design.md §1.1](../inprogress/2026-09-28-storage-manager/technical-design.md#11-layers-from-the-guests-port-to-the-medium).

## Words used here

| Word | Meaning |
|---|---|
| **Slot** | A place a medium goes: `fdd.a` (floppy drive A), `tape` (the tape deck), `sd.zc` (the Z-Controller SD socket) |
| **Medium** | What is in a slot: a disk image, a tape, a card image, or a host folder presented as a disk, a tape or a card |
| **Kind** | `floppy`, `tape`, `block` (SD card / hard disk), `optical` (CD) |
| **Tag** | A word describing a slot: `sd`, `boot`, `trdos`, `neogs` |
| **Alias** | A short name: `A` for `fdd.a`, `sd` for the machine's main SD slot |
| **Selector** | Any way of naming a slot (below) |
| **Dirty** | The guest wrote to the medium and the writes are not saved anywhere yet |
| **Disposition** | What happens to a dirty medium's writes when it leaves its slot: `save`, `export` or `discard` |
| **Detached** | A medium whose slot went away (an add-on card was removed); it keeps its writes |

## Slots

`media list` shows the slots of the machine at hand. Examples:

| Machine | Slots |
|---|---|
| Pentagon, Scorpion, 128K with Beta 128 | `fdd.a` … `fdd.d` (aliases `A` … `D`), `tape` |
| +3 | `fdd.a`, `fdd.b` (`A`, `B`) — there is no drive C —, `tape` |
| ZX-Evo | `fdd.a` … `fdd.d`, `tape`, `sd.zc` (`sd`) |

Every slot reports: id, kind, label, index, aliases, tags, whether it is removable and takes
folders, the write-protect switch, its state (`empty`, `present`, `pending`, `detached`) and the
medium (source, format, access, dirty, dirty units, and `changes`: the unsaved changes in words —
`1 track: 3 sectors`, `1 track: whole` for a track rewritten by FORMAT / WRITE TRACK,
`5 tracks: 20 sectors total`, or `48 sectors` on a card; a tape is never written).

## Naming a slot: selectors

| Form | Example | Matches |
|---|---|---|
| id | `fdd.b`, `sd.zc` | that slot |
| alias | `B`, `b:`, `sd` | the slot with that alias (case does not matter; a trailing `:` is ignored) |
| kind:index | `floppy:1` | the second floppy slot |
| tag query | `tag:sd+neogs` | the one slot with all these tags |
| `auto` | (insert only) | the slot the file's content calls for: the first empty slot of its kind, else the main one |

A selector that matches nothing answers `unknown-slot` and lists the machine's slots; one that
matches several answers `ambiguous-slot` and lists them. A letter the machine does not have (`C`
on a +3) is `unknown-slot`, never drive A.

## Verbs

| Verb | Arguments | What it does |
|---|---|---|
| `list` | — | every slot and the detached media |
| `info` | slot | one slot, and the medium's report (skipped folder entries, notes) |
| `formats` | `kind`? | accepted file extensions per kind |
| `insert` | slot or `auto`, path | a file or a folder into the slot |
| `swap` | slot, path | eject + insert in one step |
| `eject` | slot | take the medium out |
| `save` | slot, path? | floppies: write the disk back into its file, or to `path` (the disk then stands for that file) |
| `export` | slot, path | write a copy of the medium as it is now; the medium keeps its unsaved writes |
| `discard` | slot | drop the unsaved writes (a floppy is opened again from its file) |
| `rescan` | slot | build a folder medium again after the host folder changed (refused while dirty) |
| `create` | slot | a blank floppy (`format`, `cylinders`, `sides`) or card (`size`) |
| `protect` | slot, `on` | the slot's write-protect switch |

`save`, `export` and `discard` also take a detached medium's slot id.

### Options

| Option | Values | Default | Verbs |
|---|---|---|---|
| `access` | `readonly`, `session`, `writethrough` | `session` | insert, swap |
| `fs` | `fat16`, `fat32` | `fat16` | insert, swap (folder into a card slot) |
| `codepage` | `cp866`, `cp1251` | the folder's manifest, else `cp866` | insert, swap (folders) |
| `free` | bytes | 256 MiB | insert, swap (folder volumes: room for guest writes) |
| `kind` | `floppy`, `tape`, `block` | floppy when the machine has drives | insert `auto` of a folder; `formats` filter |
| `format` | `auto`, `unformatted`, `plus3` | `auto` | create (floppies) |
| `cylinders`, `sides` | 40 / 80, 1 / 2 | the format's | create |
| `size` | bytes, a multiple of 512 | — | create (cards) |
| `wp` | bool | false | insert, swap |
| `save`, `export <path>`, `discard` | disposition | none | insert, swap, eject, create |
| `retarget` | bool | true | save: a disk TRD cannot hold goes to `<name>.udi` |
| `on` | bool | true | protect |
| `end_recording` | bool | false | insert, swap, eject, create: stop a TTD recording instead of refusing |
| `async` | bool | false | insert, swap, eject, discard, rescan, create |
| `immediate` | bool | false | insert, swap: no swap delay |

**Access.** `session` (the default) keeps guest writes in memory: the file or folder never
changes until you save or export. `readonly` refuses writes (the guest sees a write-protected
medium). `writethrough` writes into the file (never into a folder).

**Sync and async.** A request returns when the medium is in or out of the slot — a floppy swap
keeps the drive empty for 2 s so TR-DOS notices the change, an SD swap 0.5 s. `async: true`
returns at once with `pending: true`. While the emulator is paused or stopped both apply at once.

**Unsaved writes.** A dirty medium leaves its slot only with a disposition in the same request:
`save` (into its own file), `export <path>` (into a new file, the source untouched) or
`discard`. Without one the request answers `dirty` and nothing changes.

**Folders.** A folder into a floppy slot becomes a TR-DOS disk built once from its files; into the
tape slot it becomes a tape ([Tapes](#tapes)); into a card slot it becomes a FAT16 (or FAT32) volume. The folder is never written: guest writes stay in
the session; export them to keep them. Host service files (`.DS_Store`, `Thumbs.db`, ...) are left
out and reported.

## Tapes

The tape deck is the slot `tape`. A tape file of any format the tape loaders read goes in —
`.tap`, `.tzx`, `.spc`, `.sta`, `.ltp`, `.zxt`; the content decides the format, the extension only
breaks a tie — or a folder. A tape is read-only: `access` does not apply, and there is nothing to
save or discard. `export` writes a copy: `.tap` when every block is a standard ROM block, else use
`.tzx`. There is no blank tape (`create`), and no swap delay: the deck stops, and the new tape
plays from its first block.

`tape load` / `tape eject` (CLI, WebAPI `/tape/load` and `/tape/eject`, Lua and Python
`tape_load` / `tape_eject`, the Qt tape window's Stop & eject) are the same insert and eject.
Transport — play, pause, stop, rewind, seek — stays with the `tape` commands.

**A folder as a tape.** The files, in the order of the folder's manifest (`order:`) and then by
name, become standard-speed blocks:

| File | On the tape |
|---|---|
| Hobeta (`boot.$B`, `game.$C`) | a header with the Hobeta name, type, start and length, then the data. `$B` is a program (with its autorun line), `$D` a number array, the rest bytes |
| `.tap`, `.tzx` | its blocks, unchanged |
| anything else | a header named after the file (10 characters), then the data: a program for `.bas` / `.b`, else bytes at 32768 (a 6 912-byte `.scr` at 16384) |

The manifest's `files:` sets a file's name, type, start and autorun `line`, and `tape: {pause:
500}` the pause after each block (1000 ms by default). A file over 65 533 bytes is left out, and so
is a file that would run past one side of a C90 cassette (45 minutes at ROM speed); the report
lists them. Example: `boot.$B` (a 26-byte program) and `intro.scr` make a tape of about a minute
without fast loading: `LOAD ""` runs the program, `LOAD "" SCREEN$` shows the picture.

## Model switch

A model switch (Qt **Machine** menu, WebAPI `POST /emulator/{id}/model`, CLI `model <name>`, MCP
`emulator_manage` action `switch_model`) builds a new machine and destroys the old one. The media go
with it: each medium goes into the slot with the same id on the new machine, as it is — unsaved
writes included, nothing read again. What the new machine's config put into that slot gives way.

A medium the new model has no slot for:

| Its state | What happens |
|---|---|
| nothing unsaved | closed, and listed in the reply |
| unsaved writes | the switch is refused (`dirty`, the media listed) and nothing changes, unless the request says `stranded`: `save` (into its own file, floppies), `discard`, or `keep` (a detached medium on the new machine: save or export it later, or it goes back in when a slot with its id returns) |

Example: Pentagon → ZX-Evo keeps drive A, its unsaved writes and the tape. ZX-Evo → Pentagon has no
`sd.zc`: a card with 48 unsaved sectors refuses the switch until you say `save`, `discard` or
`keep`; the Qt menu asks Save / Discard / Keep Detached / Cancel.

Lua and Python have no model switch (a script's emulator object would outlive its machine).

## Results and errors

Every surface returns the same fields:

| Field | Meaning |
|---|---|
| `ok` | true or false |
| `error` | the code when `ok` is false (below) |
| `message` | what went wrong, for people |
| `slot` | the resolved slot id (`B` comes back as `fdd.b`) |
| `pending` | the change waits for the next frame boundary |
| `revision` | grows with every change; poll it to know when to reload `list` |
| `report` | notes: skipped folder entries, a retargeted save |
| verb fields | `slots` and `detached` (list), `info`, `formats`, `savedPath` / `retargeted` (save), ... |

| Code | HTTP | Meaning |
|---|---|---|
| `unknown-slot` | 404 | no such slot on this machine |
| `ambiguous-slot` | 400 | the selector names several slots |
| `bad-request` | 400 | unknown verb or option, malformed value |
| `kind-mismatch` | 400 | the source cannot go into this slot |
| `unreadable-source` | 400 | missing file or folder |
| `unknown-format` | 400 | not a format this slot takes |
| `does-not-fit` | 400 | the folder does not fit the medium |
| `dirty` | 409 | unsaved writes: add `save`, `export` or `discard` |
| `recording` | 409 | a TTD recording runs: stop it, or `end_recording` |
| `in-use` | 409 | the same file is in another slot (only read-only media can share) |
| `io-error` | 500 | host I/O failed |
| `not-supported` | 501 | a valid request this build cannot do yet |

## On each surface

### WebAPI

```bash
BASE=http://localhost:8090/api/v1
curl -s $BASE/emulator/$ID/media                                     # list
curl -s "$BASE/emulator/$ID/media/b:"                                # info
curl -s "$BASE/emulator/$ID/media/formats?kind=floppy"               # formats
curl -s -X POST $BASE/emulator/$ID/media/A/insert -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-1.trd"}'
curl -s -X POST $BASE/emulator/$ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd","save":true}'
curl -s -X POST $BASE/emulator/$ID/media/C/insert -F "file=@game.trd"   # upload (up to 1 MB)
```

`POST /media/{slot}/{verb}` takes the options in the JSON body or the query string; the HTTP
status follows the error table. OpenAPI: tag **Media**.

### CLI

```text
media list
media insert A /games/elite-1.trd
media insert sd ~/zx/sdcard/ --fs fat32
media swap A /games/elite-2.trd --save
media eject B --export /tmp/b-saved.trd
media info sd --json
media help
```

### MCP

One tool, `media`, with `action` and the same argument names:

```json
{"action": "insert", "slot": "auto", "path": "/games/dizzy.trd"}
{"action": "swap", "slot": "A", "path": "/games/elite-2.trd", "save": true}
{"action": "list"}
```

### Lua

```lua
local r = media_insert("A", "/games/elite-1.trd")
if not r.ok then print(r.error, r.message) end
media_swap("A", "/games/elite-2.trd", {save = true})
for _, s in ipairs(media_list().slots) do print(s.id, s.state) end
```

Also `media_info`, `media_formats`, `media_eject`, `media_save`, `media_export`,
`media_discard`, `media_rescan`, `media_create`, `media_protect`, and `media(verb, slot, path,
opts)`. They act on the selected emulator.

### Python

```python
emu = unreal_emulator.emu_get_selected()
emu.media_insert("sd", "/home/me/zx/sdcard", fs="fat16")
emu.media_swap("A", "/games/elite-2.trd", save=True)
emu.media_eject("B", async_=True)          # "async" is a Python keyword
print(emu.media_list()["slots"])
```

Each method returns the result as a dict.

### Qt

**Tools → Media** (Ctrl+4) shows the slots in a table. Insert a file or a folder into the
selected slot, drop a file on a row, eject, save, export, discard, protect, create a blank
medium. When a dirty medium would leave, the panel asks Save / Export / Discard.

## Older calls

`disk insert` / `disk eject` (CLI), `/disk/{drive}/insert|eject|create` (WebAPI),
`disk_load` / `disk_eject` / `disk_create` (Lua, Python) and `load_software` (MCP) keep working
and keep their answers. They replace or eject a disk even when it has unsaved writes, as they
always did; the `media` verbs ask for a disposition instead. `tape load` / `tape eject` on every
surface go through the `tape` slot (see [Tapes](#tapes)).
