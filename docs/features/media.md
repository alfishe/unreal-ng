# Media: drives, cards and what is in them

Every place a medium goes on the running machine — floppy drives, the tape deck, the SD card
socket, IDE hard disks and the CD-ROM drive — is a **slot**. One set of verbs puts media in and takes them out,
the same on the Qt media panel, the WebAPI, the CLI, MCP, Lua and Python. This page is the
reference; each surface's own documentation links here.

Design: [media-control-design.md](../inprogress/2026-09-28-storage-manager/media-control-design.md);
layers of a storage peripheral: [technical-design.md §1.1](../inprogress/2026-09-28-storage-manager/technical-design.md#11-layers-from-the-guests-port-to-the-medium).

Trying to get a specific machine to actually **boot** something (which ROM it uses, what it can
boot from, what filesystem/geometry a disk or SD card needs, which files must be present)? See
[machine-boot-requirements.md](../hardware/machine-boot-requirements.md) — it covers every
creatable model plus the planned Sprinter, and calls out the MBR-vs-no-MBR trap that is the most
common way to get a disk image rejected.

## Words used here

| Word | Meaning |
|---|---|
| **Slot** | A place a medium goes: `fdd.a` (floppy drive A), `tape` (the tape deck), `sd.zc` (the Z-Controller SD socket), `ide0.master` (the IDE master unit) |
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
| Pentagon | `fdd.a` … `fdd.d` (aliases `A` … `D`), `tape`, `ide0.master` (`hd`), `ide0.slave` (Nemo IDE) |
| Scorpion, 128K with Beta 128 | `fdd.a` … `fdd.d`, `tape` |
| +3 | `fdd.a`, `fdd.b` (`A`, `B`) — there is no drive C —, `tape` |
| Profi | `fdd.a` … `fdd.d`, `tape`, `ide0.master`, `ide0.slave` (Profi IDE) |
| ATM Turbo 2+ | `fdd.a` … `fdd.d`, `tape`, `ide0.master`, `ide0.slave` (ATM IDE) |
| ZX-Evo | `fdd.a` … `fdd.d`, `tape`, `sd.zc` (`sd`), `ide0.master`, `ide0.slave` (NemoIDE) |

Every slot reports: id, kind, label, index, aliases, tags, whether it is removable and takes
folders, the write-protect switch, its state (`empty`, `present`, `pending`, `detached`) and the
medium (source, format, access, dirty, dirty units, and `changes`: the unsaved changes in words —
`1 track: 3 sectors`, `1 track: whole` for a track rewritten by FORMAT / WRITE TRACK,
`5 tracks: 20 sectors total`, or `48 sectors` on a card; a tape is never written). `info` of a medium in
`session` access adds `sessionWrites`: where the guest's writes are kept and whether they survive a crash
([below](#where-session-writes-are-kept)).

## Naming a slot: selectors

| Form | Example | Matches |
|---|---|---|
| id | `fdd.b`, `sd.zc` | that slot |
| alias | `B`, `b:`, `sd` | the slot with that alias (case does not matter; a trailing `:` is ignored) |
| kind:index | `floppy:1` | the second floppy slot |
| tag query | `tag:sd+neogs` | the one slot with all these tags |
| `auto` | (insert only) | the slot the file's content calls for, when one takes it (`targets` lists them); with several it answers `ambiguous-slot` and names them - except floppy drives, where the first empty drive is used (drive A first) |

A selector that matches nothing answers `unknown-slot` and lists the machine's slots; one that
matches several answers `ambiguous-slot` and lists them. A letter the machine does not have (`C`
on a +3) is `unknown-slot`, never drive A.

## Verbs

| Verb | Arguments | What it does |
|---|---|---|
| `list` | — | every slot and the detached media |
| `info` | slot | one slot, and the medium's report (skipped folder entries, notes) |
| `formats` | `kind`? | accepted file extensions per kind |
| `targets` | path | where a file can go on this machine: what it is, the slots that take it, the default, or why nothing does (below) |
| `insert` | slot or `auto`, path | a file or a folder into the slot |
| `swap` | slot, path | eject + insert in one step |
| `eject` | slot | take the medium out |
| `save` | slot, path? | write the medium back into its file, or to `path` (it then stands for that file): a floppy in its format; a hard disk or card's changed sectors into a raw / HDF / HDI / VHD file, a [CHD](../file-formats/disk-images/chd.md) written again; a [composite](#composite-media-several-sources-in-one-disk) without a path: its session delta file |
| `export` | slot, path | write a copy of the medium as it is now; the medium keeps its unsaved writes. A hard disk or card goes to a raw image (zero sectors not written: sparse where the host can), a VHD for a `.vhd` path (fixed by default, `vhd: dynamic` for a sparse one), or a CHD for a `.chd` path. Free space a composite or a sparse image knows to be zeros is skipped without being read |
| `discard` | slot | drop the unsaved writes (a floppy is opened again from its file) |
| `rescan` | slot | build a folder medium again after the host folder changed (refused while dirty) |
| `create` | slot | a blank floppy (`format`, `cylinders`, `sides`) or card (`size`); a blank card or hard disk holds memory only for what the guest writes (64 KiB chunks) |
| `protect` | slot, `on` | the slot's write-protect switch |
| `compose` | descriptor | build a [composite](#composite-media-several-sources-in-one-disk) (`*.ucompose.yaml`, or its JSON text) without inserting it: the layout and the report |
| `layers` | slot | a composite's layers (and partitions) |
| `flatten` | slot, path? | a composite by a named `strategy`: `flat` (a new image at `path`), `delta`, `commit` (everything into the graft's base image, journaled; the slot then holds that image), `write-back` (the guest's file changes into the writable folder layers); `plan` reports and writes nothing |
| `changes` | slot | the guest's unsaved writes on a disk or card with session writes as file operations: `create`, `modify`, `delete`, `rename` (a move too), `mkdir`, `rmdir`, `attributes`, each with the composite layer it touched; nothing is written |

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
| `format` | `audio-cd` | a folder in a CD slot is one anyway | insert, swap: a folder of MP3 / FLAC / WAV files into a CD-ROM drive as an audio CD |
| `cylinders`, `sides` | 40 / 80, 1 / 2 | the format's | create |
| `size` | bytes, a multiple of 512, up to 128 GiB | — | create (cards, hard disks) |
| `wp` | bool | false | insert, swap |
| `journal` | `replay`, `discard`, `off` | `[MEDIA] SessionJournal` (`replay` when on) | insert, swap: a session journal left next to the medium by a crash ([below](#where-session-writes-are-kept)) |
| `save`, `export <path>`, `discard` | disposition | none | insert, swap, eject, create |
| `retarget` | bool | true | save: a disk TRD cannot hold goes to `<name>.udi` |
| `compression` | `none`, `default` (lzma, zlib, huff, flac), or up to four of `zlib`, `lzma`, `huff`, `flac`, `zstd` | the source CHD's codecs, else `default` | save, export of a hard disk or card to a `.chd` |
| `parent` | a CHD file | — | export to a `.chd`: a child of that CHD (only the hunks that differ are stored) |
| `vhd` | `fixed`, `dynamic` | `fixed` | save to a path, export, flatten `flat` of a hard disk or card to a `.vhd`: `dynamic` stores only the 2 MiB blocks that hold data (a dynamic VHD in a slot is written in place: a new block goes at the end) |
| `compact` | bool | false | save, export of a FAT disk or card: write the merged volume laid out again (every file contiguous, deleted data and lost clusters gone, label / MBR / boot code kept); `save` with `compact` needs a path |
| `fs`, `size` | `fat16` / `fat32`; bytes or `64MiB` | the volume's; the medium's | save, export with `compact`: convert, resize (a FAT12 floppy needs `fs`) |
| `strategy` | `delta`, `flat`, `commit`, `write-back` | a path: `flat`; none: the descriptor's `writes.save`, else `delta` | save of a composite (`commit` and `write-back`: a later phase) |
| `force` | bool | false | save of a composite as `delta` over a delta written over other sources; `commit` although the guest's file system has lost clusters or cross-links |
| `plan` | bool | false | save, flatten with `strategy: commit` or `write-back`: what would be written, nothing written |
| `onConflict` | `refuse`, `keep-both` | `refuse` | write-back: a host file changed since the build is a conflict; `keep-both` writes the guest's as `name (guest).ext` |
| `on` | bool | true | protect |
| `end_recording` | bool | false | insert, swap, eject, create: stop a TTD recording instead of refusing |
| `async` | bool | false | insert, swap, eject, discard, rescan, create |
| `immediate` | bool | false | insert, swap: no swap delay |
| `device` | `disk`, `cdrom` | the unit's | insert, swap on an IDE unit: swap its drive first (the unit must be empty) |

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

### Where session writes are kept

A medium in `session` access (folders, CHDs, composites, blank media, any image inserted with
`access: session`) keeps the guest's writes beside its source until they are saved or discarded:

- **In memory**, up to `[MEDIA] SessionMemoryLimit` (16 MiB), in arenas of `SessionArenaKiB` (1 MiB) whose pages go
  back to the system when they are freed.
- **In a journal** next to the source: `<image>.usession`, `<folder>.usession`, `<descriptor>.usession`. When the
  arenas pass the limit, the oldest one moves there; every write reaches it at most `SessionFlushSeconds` (30 s)
  after it was made, and it is synced to the disk every `SessionSyncSeconds` (30 s).
- **Off the emulation thread**: the journal is written and synced by a few I/O threads shared by every emulator
  instance of the process; the emulation only waits for the disk when it falls behind the guest by half the limit.
- **After a crash** of the emulator (or an exit with unsaved writes) the next insert of the same medium replays the
  journal: the medium comes back dirty with the guest's writes, and the report says
  `session journal disk.img.usession replayed: N sector(s) ...`. At most the last `SessionFlushSeconds` of writes are
  lost. A composite's journal replaces its `.delta` (the journal holds everything since that insert).
- **Ends**: a save, a discard, an eject with a disposition, a commit or a write-back delete the journal: there is
  nothing left to recover.

The insert option `journal` decides what happens to a journal found next to the medium:

| `journal` | |
|---|---|
| `replay` (default with `SessionJournal = on`) | replay it |
| `discard` | delete it unread, start a new one |
| `off` (default with `SessionJournal = off`) | leave it as it is; this insert keeps its writes in a temp file in `SpillFolder`, gone with the medium |

A journal written over another disk (the source changed meanwhile) or damaged is never replayed and never deleted:
it is renamed to `<name>.usession.<n>.stale` and the report says why. A medium without a place of its own for a
journal (a blank medium, an upload, an inline descriptor, a read-only folder) keeps it in `SpillFolder`, not
recoverable; so does a second slot holding the same source. A write never fails because of the journal: when it
cannot be written, the writes stay in memory over the limit and `info` says `journalFailed`.

```ini
[MEDIA]
SessionMemoryLimit  = 16       ; MiB in memory per session; 0: no limit
SessionArenaKiB     = 1024     ; the unit of a flush (64 ... 16384, a power of two)
SessionFlushSeconds = 30       ; 0: only when the limit is passed
SessionSyncSeconds  = 30       ; 0: never fsync
SessionJournal      = on
SpillFolder         = /var/tmp
```

`info` reports it per medium:

| Field | Meaning |
|---|---|
| `sessionWrites.sectors` | sectors the guest changed |
| `sessionWrites.memoryBytes`, `memoryLimit` | the arenas held in memory, and the limit |
| `sessionWrites.journalBytes`, `journalFile` | what the journal holds, and its path (`(deleted) ...` for a temp one already unlinked) |
| `sessionWrites.journalRecoverable` | the journal is next to the medium and replayed after a crash |
| `sessionWrites.journalFailed` | a journal write failed (disk full, no folder): the writes stay in memory |

Memory per mode: an image file in `readonly` or `writethrough` holds nothing per sector; a composite holds its
metadata; a blank card holds a pointer per GiB and 128 KiB per GiB the guest wrote, plus its session.

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

## Hard disks and the CD-ROM drive (IDE)

A machine with an IDE board has two units, `ide0.master` and `ide0.slave`. The board comes from
the machine's config, `[HDD] Scheme`:

| Scheme | Board | Shipped on |
|---|---|---|
| `PROFI` | Profi IDE (answers in the Profi's EXT mode) | Profi |
| `NEMO-DIVIDE` | ZX-Evo NemoIDE | ZX-Evo; TSConf config (the model is not creatable yet) |
| `ATM` | ATM Turbo 2+ IDE (with the TR-DOS ports) | ATM Turbo 2+ |
| `NEMO`, `NEMO-A8` | Nemo IDE card (with the TR-DOS ports off) | Pentagon |
| `SMUC` | Scorpion SMUC card (with the TR-DOS ports) | none: set it on Scorpion / ProfScorp |
| `DIVIDE` | DivIDE ports (no DivIDE paging) | none |
| `NONE` | no IDE | the other machines |

Each unit is a **hard disk** unless the config says it is a **CD-ROM drive**: `CD0=1` / `CD1=1`,
or an `.iso` or `.cue` configured as the unit's image. `CF0=1` / `CF1=1` (`CF2` / `CF3` for the Sprinter's
second channel) make a disk unit a **CompactFlash card** on an IDE adapter: the same disk, but it answers
IDENTIFY as a CF card does (word 0 = `#848A`, the CFA feature set, model `UNREAL-NG CF`) and takes the
CF 8-bit transfer mode (SET FEATURES `#01` / `#81`). An empty unit changes its drive with the insert
option `device=cdrom` / `device=disk` / `device=cf` (the Qt media panel asks when you insert a CD image or a
folder of music files into a disk unit, or a disk image into a CD drive; its **CF Card** button makes the
next disk image of an empty IDE unit a CompactFlash card);
the change lasts for this machine's session (a reset keeps it; a new machine or a model switch starts
from the config file). ZX-Evo ships with a CD drive in the slave position
(`CD1=1`), where the ERS "D. CD boot" looks for it; the other machines ship without one, because
an empty drive changes what some firmware does at boot.

| Unit | Kind | Takes | Default access | Removable |
|---|---|---|---|---|
| hard disk | `block` | `.img` `.ima` `.hdd` `.hd` (raw), `.hdf` (RS-IDE, 8-bit halved too), `.hdi`, fixed and dynamic `.vhd` (written in place too; a differencing VHD is refused), MAME's `.chd` (any hard-disk CHD, [chd.md](../file-formats/disk-images/chd.md)), or a folder (a FAT16 volume) | `writethrough`: the guest writes into the image file, as on UnrealSpeccy (a folder or a CHD: `session`, a CHD is written by `save`) | no: insert and eject while paused |
| CD-ROM drive | `optical` | `.iso` (ISO 9660), `.cue` (a CUE sheet with its BINARY / MOTOROLA / WAVE files: data and audio tracks, INDEX 00 pregaps, PREGAP / POSTGAP, several files, several sessions with `REM SESSION`), a lone raw `.bin` of 2352-byte frames, MAME's CD-ROM `.chd` (cdlz / cdzl / cdzs / cdfl, v5, multisession too), or a folder of MP3 / FLAC / WAV files (an audio CD, below); read-only | `readonly` | yes: a swap keeps the drive empty for 3 s and the guest sees "medium changed" |

The geometry is `[HDD] CHS0` / `CHS1` (`C/H/S`), else the image header's, else the largest standard
one for the size (16 heads, 63 sectors). On the Profi board a disk's own ProfiHiDD header decides
(16 x 16 from the SYS ROM, 16 x 63 from Karabas); a disk without one gets the SYS ROM's 16 x 16.
`HD0RO=1` makes the master read-only (WRITE aborts, like a jumper on the drive). The legacy
`Image0` / `Image1` keys work as `[MEDIA] ide0.master` / `ide0.slave`.

Example: `media insert hd ~/zx/nedoos.img` puts an image on the master; on ZX-Evo
`media insert cd ~/zx/disc.iso` and the ERS "D. CD boot" runs the disc's `AUTORUN.ZX`. Started with the
drive empty, the ERS keeps retrying; insert a disc and it boots.

`state ide` (WebAPI `/state/ide`, Lua / Python `ide_state()`, MCP aspect `ide`) shows the board,
its latches and each unit's task file, command in progress and, on a CD drive, the sense data.

**CD audio.** A disc with audio tracks plays through the drive's own audio commands (PLAY AUDIO,
PAUSE / RESUME, STOP, READ SUB-CHANNEL, the page 0Eh volume and routing, READ CD for raw frames) at
75 frames per second of emulated time, whatever the host speed or turbo. Each CD drive has a mixer
row of its own (`CD ide0.slave`: volume, mute, solo, recording source, HUD "CD"). `state cdaudio` /
`cdaudio` (WebAPI `/state/cdaudio` and `POST /cdaudio/{verb}`, Lua / Python `cdaudio_state()` /
`cdaudio()`, MCP aspect `cdaudio`) shows the disc's tracks and the head, and plays, pauses or
stops from outside the guest (not while TTD records). Recipe:
[cd-audio.md](../../.recipe/media/cd-audio.md); design and tests:
[2026-10-02-cd-audio](../inprogress/2026-10-02-cd-audio/README.md).

The drive's rules for audio, as MMC-3 sets them, check the start: a PLAY whose start is past the disc
fails with LBA OUT OF RANGE (05h / 21h), one whose start is not in an audio track with ILLEGAL MODE FOR
THIS TRACK (05h / 64h / 00h); neither moves the head nor changes the audio status. The end is not
checked: an end past the disc (players ask for 80:00:74 or FF:FF:FF, "to the end") plays to the
session's lead-out. A data track inside the range: PLAY AUDIO (10) / (12) fail (END OF USER AREA
ENCOUNTERED ON THIS TRACK, 05h / 63h), PLAY AUDIO MSF plays the audio before it. The IDE activity LED lights
only while the drive moves data from the disc to the host (READ (10) / (12), READ CD); status polls
and audio play leave it dark - playing audio shows on the HUD's "CD" indicator (the drive's mixer row).

**The guest's eject empties the slot.** When the guest ejects the disc (START STOP UNIT with LoEj,
Start 0, accepted - not when PREVENT ALLOW MEDIUM REMOVAL holds it), the media manager takes the disc
out of the slot at the next frame boundary through its normal eject: the slot is `empty` on every
surface (media panel, HUD, `media info`, WebAPI / MCP / CLI / Lua / Python), with the usual
`MEDIA_EJECTED` event. The tray stays open (NOT READY, MEDIUM NOT PRESENT - TRAY OPEN, 02h / 3Ah / 02h)
until the guest loads (closes) it - with no disc then: 02h / 3Ah / 01h, tray closed - or a disc is
inserted, which closes it (UNIT ATTENTION). A user's eject also leaves the tray open, and is reported
to the guest as a change as before. Under time travel the guest's eject is guest I/O, not an outside
input: it does not end or invalidate a recording, and a replay across it reproduces the drive's state
without touching the slot again (the slot stays as the live run left it; continuing live from before
the eject leaves the drive empty - insert the disc again).

**Enhanced CD (CD-Extra, multisession).** A disc with its audio tracks in session 1 and a data track in
session 2 plays in an audio player and reads in a computer drive. Between the two sessions lie the
first session's lead-out (1:30) and the second's lead-in (1:00): 11 250 frames nothing reads, then
the data track's 2-second pregap. A CUE sheet marks the sessions with `REM SESSION nn` (one BIN for the
whole disc, or one per track as Redump writes them; `REM LEAD-OUT` / `REM LEAD-IN` / `REM PREGAP` give
other lengths), a CHD with MAME's `CHSE` entries. READ TOC lists every track (format 0), the sessions
(format 1) and the full TOC with each session's A0 / A1 / A2 and the B0 / C0 pointers (format 2).
`tools/cd/make-audio-disc.py` writes one (`--layout mixed` for the older data-track-first disc).

**Audio CD from a folder.** A folder of MP3, FLAC and WAV files in a CD slot is an audio CD: insert
it like an image (`media insert cd ~/music/album`, or with the option `format=audio-cd`). The files are
read once, at insert, and the disc is held in memory (10 MiB per minute of music); changing the
folder afterwards changes nothing. The rules are a CD burner's:

| Rule | What happens |
|---|---|
| Which files | the folder's own `*.mp3`, `*.flac`, `*.wav`; subfolders, hidden files and other files are left out (the report counts them) |
| Order | natural: by name ignoring case, a run of digits as one number - `2 Intro.mp3` before `10 Outro.mp3` |
| Tracks | one per file, at most 99; each at least 4 seconds (shorter files are padded with silence) |
| Sound | 44 100 Hz, 16-bit stereo: other rates resampled, mono on both sides, surround mixed down |
| Gaps | 2 seconds of silence before every track after the first |
| Length | 80 minutes (an 80-minute CD-R): the files are taken in order while they fit; the first that does not fit ends the disc (no reordering to fill it) |
| A file that does not decode | skipped, with the reason; the next one is tried |

Example: a folder with `1 intro.mp3` (30 s), `2 song.flac` (3:00), `10 outro.wav` (2 s) and
`cover.jpg` gives three tracks at 00:02:00, 00:34:00 and 03:36:00 (the outro padded to 4 s); the
insert's report says `track 01: 1 intro.mp3 (mp3, 0:30)` ... and `ignored: 1 folder entries`. `media info cd`
lists the tracks with their files (`info.medium.disc.tracks[].title`). An 80-minute folder of MP3s takes
about 5 s to insert. Recipe: [cd-audio.md](../../.recipe/media/cd-audio.md#audio-cd-from-a-folder).

Time travel records through disk activity: a write is a replay barrier, and the board's state is in
every checkpoint.

## Composite media: several sources in one disk

A **composite** is a disk, card or CD built from several sources at once: host folders, FAT disk images (or one of
their partitions) and ISO images, stacked as **layers** (an upper layer's file shadows a lower one's of the same name;
directories merge). It is described by a descriptor, `<name>.ucompose.yaml` (YAML or JSON), and goes into a slot like
any file. The sources are only read: the guest's writes stay in the session until you save them.

```yaml
version: 1
target: {fs: auto, free: 64MiB}          # kind block|optical, fs auto|fat16|fat32|iso9660, build auto|rebuild|graft,
                                         # size, label, codepage, partition mbr|none, fixedTime, iso: {level, joliet}
layers:
  - {name: dss,   source: {image: dss.img}}                # a FAT image (or {image: x.img, partition: 1})
  - {name: util,  source: {folder: ~/zx/util}, mount: /UTIL}
  - {name: games, source: {iso: games.iso}, from: /GAMES, mount: /GAMES, exclude: ["*.txt"]}
writes: {save: delta}                     # what `save` does without a path; delta: <descriptor>.delta
```

| Topic | Rule |
|---|---|
| **Build** | `rebuild` lays a new FAT volume out from the merged tree. `graft` keeps a FAT image layer at the bottom as it is (MBR, loaders, system files at their places) and writes the upper layers' files into its free clusters; it needs a FAT image at the bottom. `auto` (the default) grafts when the bottom layer is a FAT image that the slot reads and that has room, else rebuilds and says why. A graft reads only the base directories the upper layers reach (a base of 20 000 files grafts in about 1 ms); the rest is read when asked for (the file counts of `layers`, the layer of a guest change in `media changes`). A base layer with `include` / `exclude` filters is read in full |
| **CD** | in a CD-ROM drive (or `target.kind: optical`) the layers become one ISO 9660 disc (Joliet names by default), read-only |
| **Boot code** | the bottom image's MBR code, boot sector code and reserved sectors (the sectors between the MBR and the partition too: the DSS loader) are carried into a rebuilt volume; a bootable ISO keeps its El Torito entries. A `boot:` section names other files (`mbrCode`, `volumeCode`, `reserved`, `eltorito`) |
| **Partitions** | `partitions:` instead of `layers:` makes a partitioned disk: each entry a passthrough `{source: {image: x.img, partition: 1}}` or a composition `{fs: fat16, size: 64MiB, compose: {build: graft, layers: [...]}}`, 1 MiB aligned, more than four as logical partitions. The first source disk's MBR code is carried (the Profi BIOS runs it) and each boot sector gets its partition's start |
| **Unsaved writes** | `media changes` lists them as file operations with their layers. `save` without a path writes them to `<descriptor>.delta` (S2) and the medium is clean; the next insert of the same descriptor restores them ("session restored"). A delta written over other sources (a host file changed since) is not applied: the report names the layer, and saving over it needs `force`. A damaged delta is renamed `*.delta.bad`. `save` with a path (or `strategy: flat`) writes one image and the slot then holds it; `export` writes one and leaves the composite as it is |
| **Commit (S3)** | `flatten <slot> --strategy commit` writes a graft's re-encoded sectors, its grafted files and the guest's writes into the base image (raw, HDF, HDI or fixed VHD; not a CHD, and not while another slot uses it). The old sectors go to `<image>.ujournal` first; if the commit is cut short, the next open of the image puts them back. The sectors are streamed in order (a commit holds no list of them in memory, whatever the session's size). Afterwards the slot holds the base image; the descriptor is not changed |
| **Write-back (S4)** | `flatten <slot> --strategy write-back` carries the guest's file changes into the folder layers marked `writable: true`: a file of such a layer is rewritten in place, a file of a read-only layer (an image, an ISO, a read-only folder) is copied up into the upper layer (`writes.upper`, else the topmost writable one), new files go where their directory is. A delete follows the owner layer's `onDelete`: `keep` (the default: the host file stays, the path is hidden from the next build through `<descriptor>.whiteout`), `move` (into `deletedFolder`), `trash` (the host's trash: the Recycle Bin on Windows, `~/.Trash` on macOS, the freedesktop.org trash on Linux), `delete`, or `ignore`. Attribute changes (read-only, hidden, system) go to `<descriptor>.attributes` (`RH<TAB>/PATH`; `-` for none), never to the host files, and apply at every build. On a partitioned disk each composed partition is written back into its own layers (the sidecars name the partition: `work:/PATH`); a change on a passthrough partition is a plan error (commit or flatten it instead). A host file changed since the build is a conflict (`onConflict`). The steps are journaled in `<descriptor>.writeback`, and the slot is rebuilt from the layers |
| **Rescan** | `rescan` builds the composite again from its sources (refused while there are unsaved writes) |

### Which file system a slot takes

A slot's file-system rule applies to folders, composites and images alike: a volume of another type is refused with
the reason, never built or mounted silently. `fs: auto` picks the slot's default.

| Slot | File systems | Why |
|---|---|---|
| Sprinter IDE hard disks (`ide0.*`, `ide1.*`) | FAT12 / FAT16 only | Estex DSS reads no FAT32 |
| Profi IDE hard disks (`ide0.*`) | FAT12 / FAT16 only | PQ-DOS 2023-09 boots from FAT16 and ignores a FAT32 partition (checked 2026-10-06) |
| TS-Conf SD card (`sd.zc`) | FAT32 only | TS-BIOS and Wild Commander mount FAT32 |
| ZX-Evo SD card, the other IDE boards, NeoGS SD, ZX Next | FAT16 (default) and FAT32 | their drivers read both |
| CD-ROM drives | ISO 9660 | a composite there is always an ISO |

Where an MBR is needed and where it must not be (an IDE disk on ZX-Evo vs. an SD card on TS-Conf) is in
[machine-boot-requirements.md](../hardware/machine-boot-requirements.md#common-pitfalls). Design and as-built notes:
`docs/inprogress/2026-10-05-media-multisource/` (the phase documents in `phases/`); recipes:
[.recipe/media/use-media-slots.md](../../.recipe/media/use-media-slots.md).

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
| verb fields | `slots` and `detached` (list), `info`, `formats`, `file` / `targets` / `default` / `refusal` (targets), `savedPath` / `retargeted` (save), ... |

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

### Where a file can go: `targets`

`targets <path>` answers before anything is inserted. The file is recognized by its content
first and its extension second: a CD image by the ISO 9660 mark, a folder with MP3 / FLAC / WAV files
as an audio CD (format `audio-folder`, a CD-ROM drive first), a hard-disk image by its HDF or
VHD header, a MAME CHD by `MComprHD` (a hard disk first, an SD card too), a card or hard-disk image by a FAT boot sector or a partition table, a floppy image by
the floppy formats' rules, a tape by its extension. Then the machine's slots that take it are
listed in the order a chooser shows them: an empty slot before an occupied one, the main (boot)
slot first, an add-on card's slot last. A CD image goes only to a unit that is a CD-ROM drive.

| Field | Meaning |
|---|---|
| `file` | `kinds` (most likely first: `floppy`, `tape`, `hdd`, `sdcard`, `optical`, `snapshot`, `rzx`, `zxpoly`, `symbols`, `rom`), `format`, `evidence` (why) |
| `targets` | each: `action` (`insert`; `load` for a snapshot, a recording or labels), `slot`, `label`, `occupiedBy` (what it replaces), `dirty`, `autostart` (drive A of a TR-DOS machine) |
| `default` | the index used without asking: the only target, or drive A for a floppy image; `null`: ask |
| `refusal` | why nothing takes the file ("no CD-ROM drive on this machine (its IDE units are hard disks)") |

A refusal is an answer, not an error: `ok` stays true. Example, a card image on a ZX-Evo with a
NeoGS card:

```text
> media targets /cards/nedoos.img
  file: sdcard hdd (fat) - FAT boot sector at sector 0
    sd.zc       SD card (Z-Controller)    empty
    sd.ngs      SD card (NeoGS)           empty
    ide0.master IDE master (hard disk)    empty
  several targets: name the slot (media insert <slot> <path>)
```

## On each surface

### WebAPI

```bash
BASE=http://localhost:8090/api/v1
curl -s $BASE/emulator/$ID/media                                     # list
curl -s "$BASE/emulator/$ID/media/b:"                                # info
curl -s "$BASE/emulator/$ID/media/formats?kind=floppy"               # formats
curl -s "$BASE/emulator/$ID/media/targets?path=/discs/dna_nemo.iso"  # targets
curl -s -X POST $BASE/emulator/$ID/media/A/insert -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-1.trd"}'
curl -s -X POST $BASE/emulator/$ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/games/elite-2.trd","save":true}'
curl -s -X POST $BASE/emulator/$ID/media/C/insert -F "file=@game.trd"   # upload (up to 1 MB)
curl -s -X POST $BASE/emulator/$ID/media/cd/insert -H 'Content-Type: application/json' \
     -d '{"path":"/home/me/music/album","format":"audio-cd"}'          # report: the tracks; info: medium.disc
```

`POST /media/{slot}/{verb}` takes the options in the JSON body or the query string; the HTTP
status follows the error table. OpenAPI: tag **Media**.

### CLI

```text
media list
media insert A /games/elite-1.trd
media insert sd ~/zx/sdcard/ --fs fat32
media insert cd ~/music/album --format audio-cd
media swap A /games/elite-2.trd --save
media eject B --export /tmp/b-saved.trd
media info sd --json
media targets /discs/dna_nemo.iso
media help
```

### MCP

One tool, `media`, with `action` and the same argument names:

```json
{"action": "insert", "slot": "auto", "path": "/games/dizzy.trd"}
{"action": "swap", "slot": "A", "path": "/games/elite-2.trd", "save": true}
{"action": "list"}
{"action": "targets", "path": "/cards/nedoos.img"}
{"action": "insert", "slot": "cd", "path": "/home/me/music/album", "format": "audio-cd"}
```

### Lua

```lua
local r = media_insert("A", "/games/elite-1.trd")
if not r.ok then print(r.error, r.message) end
media_swap("A", "/games/elite-2.trd", {save = true})
for _, s in ipairs(media_list().slots) do print(s.id, s.state) end
```

Also `media_info`, `media_formats`, `media_targets(path)`, `media_eject`, `media_save`, `media_export`,
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

**Tools → Media** shows the slots in a table. Insert a file or a folder into the
selected slot, drop a file on a row, eject, save, export, discard, protect, create a blank
medium. When a dirty medium would leave, the panel asks Save / Export / Discard. On a composite, Save opens the strategy dialog (one image, keep the session, commit into the base image, write back into the folders; the ones that do not apply are greyed out with the reason, the descriptor's `writes.save` preselected, Preview shows a commit or write-back plan), and so does Save in the unsaved-changes question; Layers... lists its layers, partitions and the guest's changes. A row whose slot
cannot take the dropped file says why and inserts nothing. **Insert Folder** into a CD-ROM drive
builds an audio CD of the folder's MP3 / FLAC / WAV files (off the UI thread, with progress - a folder
dropped on the main window or opened with File > Open goes through the same worker); into an
empty IDE unit of the other kind the panel asks to swap the unit's drive first (a CD image or a
music folder into a hard-disk unit, a disk image into a CD drive).

**Drag and drop on the main window** uses the same analysis as `targets`:

- a file only one slot takes goes there at once (a CD image on a ZX-Evo: its CD-ROM drive); a
  folder with MP3 / FLAC / WAV files is offered to the CD-ROM drive first (an audio CD), then to the
  slots that take a folder;
- a floppy image goes to drive A and boots (Shift: mount only); several floppy images go to A, B,
  C, D in order;
- a file several slots take (a card image on a ZX-Evo with NeoGS) opens the **slot chooser** over
  the screen after the drop: one tile per slot with the device's icon, its name and what it holds
  now; click one, press its number, or Esc;
- **holding** the file over the window for 1.5 s (or pressing Alt / Option) shows the same tiles
  while dragging - drop on the slot you want, e.g. a disk into drive B;
- a file no slot takes turns the screen red with the reason at once, and a drop does nothing; a CD,
  hard-disk or card image never starts a machine (which one would be a guess).

**File → Insert Medium...** (Ctrl+Shift+I) picks a file, then opens the slot chooser with every
slot that takes it.

## Older calls

`disk insert` / `disk eject` (CLI), `/disk/{drive}/insert|eject|create` (WebAPI),
`disk_load` / `disk_eject` / `disk_create` (Lua, Python) and `load_software` (MCP) keep working
and keep their answers. They replace or eject a disk even when it has unsaved writes, as they
always did; the `media` verbs ask for a disposition instead. `tape load` / `tape eject` on every
surface go through the `tape` slot (see [Tapes](#tapes)).
