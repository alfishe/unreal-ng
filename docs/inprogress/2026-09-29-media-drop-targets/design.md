# Media drop targets: one compatibility analysis, one slot chooser

- **Date:** 2026-09-29
- **Status:** design for review. No code yet. PLAN row **#77**.
- **Builds on:** the media manager ([storage-manager](../2026-09-28-storage-manager/technical-design.md),
  user reference [docs/features/media.md](../../features/media.md)) and the IDE / ATAPI work
  ([ide-atapi](../2026-09-28-ide-atapi/implementation-plan.md)).

> **In one line.** Every way a file reaches a machine (a drop on the window, a drop on the media
> panel, File > Open, the command line, `media insert` without a slot on CLI / WebAPI / MCP) asks
> one core service which slots can take it; the Qt window shows the answer while the file is
> dragged: one target is used directly, none is a red refusal, and several are the user's choice -
> a slot chooser menu on a quick drop, labeled drop zones with device icons on a 1.5 s hold. The
> one shortcut: a floppy image goes to drive A with autostart.

## Contents

- [1. What goes wrong today](#1-what-goes-wrong-today)
- [2. Rules](#2-rules)
- [3. Worked examples](#3-worked-examples)
- [4. Core: the compatibility analysis](#4-core-the-compatibility-analysis)
- [5. Qt: the drop overlay](#5-qt-the-drop-overlay)
- [6. Other entry points](#6-other-entry-points)
- [7. Tests](#7-tests)
- [8. Phases](#8-phases)
- [9. Decisions](#9-decisions)

## Glossary

| Term | Meaning |
|---|---|
| **Slot** | A place a medium goes: `fdd.a` ... `fdd.d`, `tape`, `ide0.master`, `ide0.slave`, `sd.zc`, `sd.ngs` (media manager, `IMediaSlot`) |
| **Target** | A slot a given file can go into on the running machine, or a non-slot action (load a snapshot, play an RZX, load symbols) |
| **Slot chooser** | One Qt component listing the targets, each with its label, device icon and current medium; shown as a menu at the cursor, as drop zones, or as a dialog |
| **Drop zone** | A labeled tile of the drop overlay standing for one target; dropping on it picks that target |

## 1. What goes wrong today

Three places decide where a file goes, each by its own rules:

| Entry point | How it decides | Code |
|---|---|---|
| Drop on the main window, File > Open, command line | an extension table: `.img` is a floppy; `.iso`, `.hdf`, `.hdi`, `.vhd` are unknown | `unreal-qt/src/emulator/filemanager.cpp`, `MainWindow::loadFile` |
| `media insert <file>` without a slot (CLI, WebAPI, MCP) | the floppy probe by content first, then the format registry's extensions, then the first empty slot of that kind | `MediaControl::ChooseSlot` (`core/src/emulator/media/mediacontrol.cpp`) |
| Drop on a row of the media panel | the row under the cursor, no check | `MediaPanelWindow::onFilesDropped` |

Reported symptoms (2026-09-29):

- An ISO dropped on the window ended up in `fdd.a` (seen in the media panel). **Not reproduced
  yet** - phase M0. Suspects: a drop on the panel's `fdd.a` row; the floppy probe's rules
  without a signature (a TR-DOS volume byte at #8E7, Hobeta) taking an ISO; a floppy slot that
  opens an image of another kind.
- A hard-disk `.img` goes to `fdd.a`: the extension table calls `.img` a floppy.
- An SD card image has two candidate slots on a ZX-Evo with NeoGS (`sd.zc`, `sd.ngs`); nothing
  chooses between them visibly.
- A drop of a file nobody can take does nothing but a log line.

## 2. Rules

The user's rules, made precise:

1. **One analysis.** The core decides, for a file and a machine, the list of targets and the
   default. Every entry point uses it; the Qt window only shows it.
2. **A medium goes to a slot of its kind or nowhere.** An ISO goes to a CD drive, a hard-disk image
   to an IDE unit, a card image to an SD slot, a floppy image to a floppy drive, a tape to the
   tape. A file no slot of the machine takes is refused, with the reason; it is never forced into
   another kind of slot.
3. **A CD needs a CD-ROM drive the machine has.** Only an IDE unit that *is* a CD-ROM drive by
   the machine's configuration (the ZX-Evo slave, `CD1=1`) takes an ISO. A machine without one
   refuses it ("no CD-ROM drive on this machine"); a drop never turns a hard-disk unit into a
   CD-ROM drive. The explicit switch stays where it is: `media insert <iso> device=cdrom` or the
   media panel ([media.md](../../features/media.md) "Hard disks and the CD-ROM drive").
4. **No machine, no guessing.** With no emulator running, a file starts one only when the file
   itself says which machine: a snapshot (its header), an RZX (its start snapshot), a ZX-Poly
   file. A floppy image keeps today's rule (a Pentagon 128 boots it). An ISO, a hard-disk image
   or a card image does **not** start a machine: which one would be a guess; the drop is refused
   with a hint ("start a ZX-Evo or a Profi first").
5. **One target: no questions.** A drop uses it.
6. **Several targets: the user chooses** (user, 2026-09-29). A quick drop opens the **slot
   chooser menu** at the cursor; holding the file over the window for **1.5 s** without dropping
   draws a **drop zone per target** instead, and a drop on a zone picks it. Every entry is labeled
   (slot name, device icon, what is in it now). **The one shortcut:** a floppy image on a quick
   drop goes to drive A with autostart, as today (Shift: mount only); its other drives are on the
   hold.
7. **No target: say so at once.** While a file nobody can take is dragged over the window, the
   drop area turns red with the reason; the drop is rejected (no action, nothing inserted).

## 3. Worked examples

Machines as shipped (`data/configs`): Pentagon 128 has a Nemo IDE board with two hard-disk units
and no CD drive; ZX-Evo (`ATM3`) has NemoIDE with a CD drive on the slave and a Z-Controller SD
slot; a NeoGS card adds `sd.ngs`; a 48K has neither IDE nor SD.

| Machine | File | Quick drop | Hold 1.5 s |
|---|---|---|---|
| Pentagon 128 | `game.trd` | `fdd.a`, autostart (the floppy shortcut) | zones A, B, C, D |
| Pentagon 128 | `disc.iso` | refused, red: "no CD-ROM drive on this machine" (its IDE units are hard disks) | red at once |
| ZX-Evo | `disc.iso` | `ide0.slave` (already a CD drive) | only one target: nothing shown |
| 48K | `disc.iso` | refused, red: "no CD drive: this machine has no IDE board" | red at once, no zones |
| no machine | `disc.iso` | refused, red: "start a machine with a CD drive first (ZX-Evo)" | red at once |
| Profi | `system.hdf` | chooser menu: "IDE master", "IDE slave" | zones master, slave |
| Pentagon 128 | `system.hdf` | chooser menu: "IDE master", "IDE slave" | zones master, slave |
| ZX-Evo | `system.hdf` | `ide0.master` (the slave is the CD drive: one target) | nothing shown |
| ZX-Evo + NeoGS | `card.img` (FAT) | chooser menu: "SD - Z-Controller", "SD - NeoGS" | zones for both |
| Pentagon + NeoGS | `card.img` (FAT) | `sd.ngs` (the only SD slot) | nothing shown |
| 48K | `card.img` (FAT) | refused: "no SD slot on this machine" | red at once |
| ZX-Evo | `nedoos/` (a folder) | chooser menu: SD, floppy drives (folder as a TR-DOS disk) | zones for each |
| any | `game.szx` | the snapshot; another model is replaced first (existing) | no zones |
| any | `notes.txt` | refused, red: "not a medium, snapshot or recording" | red at once |

## 4. Core: the compatibility analysis

A new core class, `MediaTargets` (`core/src/emulator/media/mediatargets.{h,cpp}`), with two steps.

### 4.1 Classify the file (machine-independent)

`MediaTargets::Classify(path) -> FileClass`:

| Field | Content |
|---|---|
| `kinds` | candidate kinds in order of confidence: `floppy`, `tape`, `hdd`, `sdcard`, `optical`, `snapshot`, `rzx`, `zxpoly`, `symbols`, `rom`, `unknown` |
| `format` | the format id the loader would use (`trd`, `udi`, `iso`, `hdf`, `raw`, `fat`, `szx`, ...) |
| `evidence` | why: a signature, a size rule, the extension, the folder's content |

Content before extension, with the extension as the tie-breaker; one table of rules, each with a
test:

| Evidence | Class |
|---|---|
| `CD001` at byte #8001 (ISO 9660 volume descriptor, sector 16) | optical |
| an MBR partition table or a FAT boot sector at byte 0 | sdcard or hdd (both kinds, `hdd` first for `.img/.hdf/.vhd/.hdi`, `sdcard` first otherwise) |
| HDF / HDI / VHD header | hdd |
| a floppy signature (UDI, FDI, DSK, SCP, HFE, SCL, TD0 with its extension) | floppy |
| TR-DOS volume sector (#8E7 = #10) **and** a floppy size (at most 2 x 86 x 16 x 256 bytes) | floppy |
| Hobeta header, file of at most 64 KB | floppy |
| a tape extension the tape registry reads | tape |
| `ZXST`, `.z80`, `.sna` | snapshot; `RZX!` rzx; `.zxp` / `.prom` zxpoly; `.map` / `.sym` symbols |
| a folder | floppy and sdcard (and hdd) |

The size cap on the TR-DOS and Hobeta rules closes the suspected ISO-as-floppy path of §1.

### 4.2 Plan targets on a machine

`MediaTargets::Plan(context, FileClass) -> Plan`:

| Field | Content |
|---|---|
| `targets` | each: `slotId` or an action (`snapshot`, `rzx`, `symbols`), a label for the UI ("Drive A", "IDE slave (CD-ROM)"), `occupiedBy` (the medium it replaces), `dirty` (that medium has unsaved writes), `autostart` (drive A on a TR-DOS machine) |
| `defaultTarget` | the target used without asking: set only when there is one target, or for the floppy shortcut (drive A); none otherwise, so the caller asks |
| `refusal` | when `targets` is empty: the reason, in the user's words ("no CD drive: this machine has no IDE board") |

Order of the targets (the order the chooser lists them; one table, tested):

1. the kind order of `FileClass::kinds`;
2. within a kind: an empty slot before an occupied one;
3. then the slot tags: `boot` / `primary` first, `addon` last (so `sd.zc` before `sd.ngs`,
   `fdd.a` before `fdd.b`);
4. for a hard disk the master before the slave. A CD goes only to a unit that is a CD-ROM drive
   (rule 3); a hard-disk unit is never a CD target.

No machine (`context == nullptr`): `Plan` returns the machine-starting actions only (a snapshot,
an RZX, a ZX-Poly file, a floppy image with the Pentagon default) and a refusal for the rest
(rule 4).

### 4.3 One apply path

`MediaTargets::Apply(context, Plan, targetIndex, options)` performs the choice through
`MediaControl` (the insert with the slot's default access, the TTD guard, the dirty-medium
disposition) so no entry point repeats that logic. It performs insert targets only: a `Load`
target (snapshot, recording, ZX-Poly group, labels) and a `NewMachine` target (no machine yet)
belong to the caller, which owns the model replacement and the machine start.
`MediaControl::ChooseSlot` becomes `Plan` + `defaultTarget`; with several targets and no default
it names them instead of guessing.

**As built in M1 (2026-10-01):** `ChooseSlot` runs `Classify` + `Plan` and takes the chooser's
first entry (an empty slot before an occupied one, the primary / boot slot first, an add-on's
last), naming the alternatives in the reply's report ("several slots take it (sd.zc, sd.ngs,
ide0.master): sd.zc chosen; name the slot to pick another"; not for floppy drives). The refusal
of several targets waits for M2 + M3: the Qt window still sends `insert auto` for CD, hard-disk
and card images until it asks through the chooser, and a refusal there would undo the M0 fix.
The TR-DOS rule of the floppy probe (`FloppyFormats::Probe`) is capped at the largest TR-DOS
image (2 x 86 x 16 x 256 bytes); sector 0 is read by `ClassifySectorZero`
(`blockadvisory.h`), the rules the boot advisory of §10 uses.

### 4.4 Surfaces

Automation parity: `media targets <file>` (CLI), `GET /api/v1/emulator/{id}/media/targets?path=`
(WebAPI + OpenAPI), MCP `media` action `targets`, Lua / Python `media_targets(path)`: the plan as
data (targets, default, refusal). `media insert <file>` without a slot takes the single target (or the
floppy shortcut); with several it names them, and it refuses what no slot takes.

## 5. Qt: the drop overlay

A widget `DropTargetOverlay` over the main window's content (`unreal-qt/src/media/`), fed by
`MediaTargets::Plan` for the dragged file.

| Moment | What the user sees |
|---|---|
| drag enters, **no target** | the drop area turns red with the refusal text; the cursor shows "not allowed"; a drop does nothing |
| drag enters, **one target** | today's highlight; a drop uses it |
| drag enters, **several targets** | the highlight and a hint "hold to see the slots" |
| the file is held **1.5 s** without dropping (several targets) | the overlay draws one labeled tile per target: device icon, slot name, what it holds now, badges ("autostart", "unsaved writes!"); the tile under the cursor lights up |
| drop on a tile | that target |
| quick drop (before 1.5 s), several targets | the slot chooser menu at the cursor; nothing happens until an entry is picked (Esc: nothing) |
| quick drop of a floppy image | drive A with autostart (the shortcut) |
| drop outside the tiles after they appeared | the slot chooser menu |
| drag leaves / Esc | overlay closed, nothing done |

Details:

- **The timer** starts on drag enter and restarts when the cursor leaves the window; the hold is a
  fixed 1.5 s. A modifier opens the tiles at once (Alt / Option), Shift keeps its meaning (mount,
  no autostart).
- **Several files** (a disk set): the first file decides the plan; files 2..n go to the following
  targets of the same kind (A, B, C, D) - phase M4, the single-file behavior first.
- **The media panel** uses the same plan while a file is dragged over its rows: a row that cannot
  take the file turns red with the reason, a drop there is refused; its free area acts like the
  window.
- **One slot chooser component**, three presentations: a menu at the cursor (quick drop), drop
  zones (hold), a dialog (**File > Insert medium...**, keyboard users). File > Open behaves like a
  quick drop.
- **Device icons** per slot kind: floppy drive, cassette, hard disk, CD-ROM drive, SD card; the
  label is the slot's own name from the media manager ("Drive A", "IDE master", "SD - Z-Controller",
  "SD - NeoGS").
- **A dirty medium** in the chosen slot asks what to do with its writes (the media manager's
  disposition), as the panel does today.

## 6. Other entry points

| Entry point | After |
|---|---|
| `MainWindow::loadFile` (drop, File > Open, command line) | `Classify` + `Plan`; one target used directly, several through the slot chooser, none refused with a message; no machine is started for an ISO / HDD / SD image. The command line cannot ask: it uses the chooser's first entry and says so in the log |
| `FileManager` extension table | removed from the decision (kept only for file-dialog filters, built from the format registries) |
| media panel row drop | validated by `Plan`: only a target row accepts |
| `media insert` without a slot | the single target (or the floppy shortcut); with several, the error lists them ("name the slot: ide0.master, ide0.slave") instead of guessing |

## 7. Tests

| Kind | Content |
|---|---|
| Classify | one case per evidence row of §4.1, incl. an ISO that happens to have #10 at #8E7 (not a floppy), a FAT card image named `.img` (sdcard), an HDF, a 720 KB raw image |
| Plan | the table of §3 as data-driven cases on the shipped machines (Pentagon, ZX-Evo, ZX-Evo + NeoGS, Profi, 48K, no machine) |
| Apply | an ISO into the ZX-Evo CD unit; an ISO refused on a machine whose units are hard disks; the dirty-medium disposition |
| Regression | the ISO-in-`fdd.a` repro of phase M0 |
| Qt | the overlay's state machine (enter, hold, leave, drop in / out of a tile) with a fake plan |

## 8. Phases

| Phase | Content | Size |
|---|---|---|
| M0 | Reproduce the ISO-in-`fdd.a` report; fix the root cause at its layer (a floppy slot refusing a non-floppy image; the probe's size caps) with a regression test | S |
| M1 | `MediaTargets::Classify` + `Plan` + `Apply` in core; `ChooseSlot` on top; tests of §7 | M |
| M2 | Surfaces: `media targets`, the refusal in `media insert`; docs of every surface | S |
| M3 | Qt: `loadFile` on the plan (no machine for ISO / HDD / SD), the red refusal, media panel row validation | S-M |
| M4 | Qt: `DropTargetOverlay` with the 1.5 s hold, File > Insert medium..., multi-file sets | M |

## 10. Boot compatibility advisory (added 2026-09-30, implemented M1-slice on master)

Everything above (`Classify` / `Plan`) answers "which slot kind takes this file" - a floppy image
goes to a floppy drive, a hard-disk image to an IDE unit. It does not answer "will this specific
file actually boot there": an ATM3 IDE hard-disk unit and a ZX-Evo SD card slot are both `Block`
kind, both take a plausible-looking disk image, but each boots a different on-disk layout - and a
file built for one silently fails on the other. Root cause found 2026-09-30
(`testdata/machines/tsconf/wildcommander/README.md`): a raw FAT32-no-MBR TS-Conf SD image
(`sd.zc` layout) mounted into ATM3's `ide0.master` reads a sector, then the CPU parks in `DI`+
`HALT` within a couple of frames - a silent, activity-free hang, not an error. The confirmed
working counter-example is `testdata/machines/baseconf/hdd-images/hdd_nedo.vhd`
(MBR + FAT partitions, built by NedoOS's `hddfdisk`): real scattered `READ SECTORS` traffic, PC/R
changing every frame.

**Decision: bake this into the media manager as a non-blocking advisory, keyed by slot *tags*, not
a per-machine-config callback.** Every IDE hard-disk unit across every machine (Pentagon, ATM710,
ATM3, Profi, Scorpion, ...) expects the same MBR+FAT layout; every Z-Controller / NeoGS SD slot
expects the same raw-FAT-at-sector-0 layout. That is a property of the *slot kind* (already
expressed as `SlotDescriptor::tags`: `{"ide", ..., "hdd"}` vs `{"sd", ...}`), not of the machine as
a whole - asking each machine config "can I offer this" would mean duplicating the same two rules
into every machine that has an IDE board or an SD slot, for no gain over reading the tags already
on the descriptor. If a machine ever needs a genuinely different rule, it overrides its slot's
tags or descriptor, not this analysis.

- **Mounting is never refused over this.** The insert still succeeds; the mismatch note rides the
  media manager's existing `report` field (`MediaResult::report`, `medium->Report()` -
  `media-control-design.md` MC-1, already used for "skipped folder entries, a format guessed from
  the extension"). Every surface already renders `report` in the envelope, so CLI, WebAPI, MCP,
  Lua and Python get this for free the moment `media insert` runs - no new endpoint, no per-surface
  work (automation parity by construction, not by repetition).
- **The check**: `core/src/emulator/media/blockadvisory.{h,cpp}`,
  `DescribeBlockLayoutMismatch(IBlockDevice&, tags)`. Reads sector 0 once (already-open medium, no
  extra I/O beyond one 512-byte read); recognizes a FAT12/16/32 VBR (jump opcode + `"FAT"` BPB
  marker) and an MBR partition table (`0x55AA` signature, a plausible non-zero type/count entry at
  446/462/478/494) well enough to tell the two apart, deliberately not a full parser. A slot whose
  tags ask for neither (`Plan`'s `hdd`/`sdcard` classification stays the source of truth for *can
  this file go here at all*) gets no check.
- **Wired at one point**: `MediaManager::Insert(slotId, source, options)`, right after
  `MediaFormatRegistry::Open` succeeds and before the medium is attached - so every entry point
  (`media insert`, the drop targets of §4-§6 once M1 lands, `ChooseSlot`'s auto-insert) goes
  through it without change.
- **Not done here**: `MediaTargets::Plan` (§4.2) itself does not yet call this - `Plan` is
  pre-insert (no medium open yet to read a sector from) and machine-agnostic by design; this
  advisory is deliberately post-insert, informational only. A future `Classify` could read sector 0
  too and fold the same signal into `evidence`/`kinds` before a target is even chosen, but that is
  M1 scope, not this slice.

## 11. Async folder insert off the UI thread (BUGS.md #3, added 2026-10-01)

`MediaManager::Insert`'s folder path (`FolderSnapshot::Scan`, then `HostFolderFat::Build` or
`FolderDiskBuilder::BuildTrd`) is plain synchronous C++ - correct for every automation surface
(WebAPI, CLI, MCP, Lua, Python all run it on their own request thread, which is fine, there is no
UI to freeze), but `MediaPanelWindow::insertInto` called it inline on the Qt UI thread, so a large
folder or a slow/network disk froze the whole window for the scan's duration: no repaints, no
input, no cancel (confirmed live: the comment "File I/O and folder scans happen here, on the
caller's thread" already sat in `mediamanager.cpp` next to the call).

**Decision: keep `core/` synchronous, add the async wrapper only in the GUI.** `MediaManager`/
`MediaControl`/`MediaFormatRegistry` gained two optional `std::function` fields threaded end to end
(`MediaRequest` → `InsertOptions` → `OpenRequest` → `FolderScanOptions` /
`FolderDiskBuilder::BuildTrd`'s new parameters):

- `cancelRequested` - polled once per directory entry in `FolderSnapshot::Scan`'s walk and once per
  file in `FolderDiskBuilder::BuildTrd`'s read loop. Returning true unwinds every recursion level
  (not just the innermost one) and fails the whole operation with the new `MediaError::Cancelled`
  (`FolderSnapshot::kCancelledError`, HTTP 499).
- `onProgress(uint64_t entriesScanned)` - called after every entry visited (accepted or skipped),
  off whatever thread calls `Scan`/`BuildTrd`. A monotonic counter, nothing else - no ETA, no
  per-item name, kept deliberately minimal.

Both are plain C++ callables, never serialized - `MediaRequest` carries them as struct fields
alongside the existing string `options` map, so the wire protocols (WebAPI's JSON, the CLI's
strings) have no way to set them and keep running `Insert` synchronously on their own calling
thread exactly as before. Only `MediaPanelWindow` (the one caller with a UI thread to protect)
sets them.

**The GUI side** (`unreal-qt/src/media/mediapanelwindow.{h,cpp}`) follows the existing
`std::thread` + `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` idiom already used by
`TapeExportAudioDialog`/`TapeImportAudioDialog` - no `QThread` subclass, no `QtConcurrent`:

- `insertInto` branches on `QFileInfo(path).isDir()`: a file insert stays exactly as it was
  (synchronous, the existing disposition dialog applies); a folder insert goes through
  `insertFolderAsync`, which starts one `std::thread`, disables every action button
  (`updateButtons()` checks `_insertWorker.joinable()` first) and shows an indeterminate
  `QProgressBar` (`setMaximum(0)`, the same pattern as `TapeImportAudioDialog`'s "busy" bar) plus a
  `QLabel` status text updated once a second with the running entry count.
- **Emulator lifetime across the worker**: the worker does not touch `EmulatorBinding`'s raw
  `Emulator*` - it captures the emulator's id and does a fresh `EmulatorManager::GetEmulator(id)`
  lookup on its own thread, taking its own `shared_ptr<Emulator>` for the scan's duration. This
  keeps the `Emulator`/`EmulatorContext`/`MediaManager` alive even if the GUI unbinds or rebinds to
  a different emulator while the scan is in flight (`EmulatorBinding::unbound` also best-effort
  cancels the in-flight worker, since there is no reason to keep scanning for a panel that moved
  on, but the worker is correct either way). If the emulator was removed outright, the lookup
  returns null and the worker reports a plain failure instead of touching freed state.
- **Watchdog, not a hard timeout**: `_scanWatchdog` (a 1 s `QTimer`) compares the progress counter
  across ticks - a folder that is genuinely still being walked (the count keeps advancing) is never
  aborted, however long it takes; only `_insertStallTimeoutSeconds` (30, a plain member field a
  test can lower) consecutive ticks with **no** advancement trip `cancelRequested`. This
  distinguishes "slow" from "stuck" per the bug's acceptance criteria - a naive "abort after 30 s
  no matter what" would have cut off legitimately large folders.
- **Progress display and row highlight**: `onProgress` carries both the entry count and the total
  bytes of files accepted so far; the status label shows the slot (bold), the path, the entry count
  and `QLocale::formattedDataSize(bytes)` (KB/MB/GB, not a raw byte count). `rebuildTable()` tints
  the scanning slot's row with `QPalette::Accent` - deliberately not `QPalette::Highlight`, which
  fades to a dull gray the moment the window is not key/focused, defeating the point of a "this one
  is busy" indicator.
- **Toolbar button**: the Tools menu's existing "Media" `QAction` (`MenuManager::mediaPanelAction()`,
  already wired to show/hide the panel and already kept in sync by `visibilityChanged`) is also
  added to the transport toolbar, with the `hdd` icon - no new action, no duplicated show/hide logic.
- **Destructor safety**: the worker's completion lambda captures `this` and is marshaled back via
  `QMetaObject::invokeMethod`; `MediaPanelWindow`'s destructor cancels and joins unconditionally
  before any member is torn down, so that queued call can never fire on a half-destroyed object
  (same contract `TapeExportAudioDialog` already relies on).

**Not done here**: a true mid-scan cancellation only exists for the folder-scan and
TR-DOS-file-read loops added above; nothing else in the insert path (`HostFolderFat::Build`'s
in-memory FAT layout math, `MediaFormatRegistry::WrapBlock`) has a cancellation hook, because
neither does any I/O - per `OpenFolderVolume`'s own trace (§3 of the research that fed this
section), the scan is already the dominant cost for the SD/IDE block-volume path this bug names.
Verification: `FolderSnapshot_Test` (progress counting incl. bytes, cancellation, nested-recursion
unwind) and `FolderDiskBuilder_Test` (cancellation during the scan vs. during the file-read loop) in
`core/tests/`; the GUI wiring was also interactively verified (built and run in an isolated git
worktree - the shared working tree had unrelated concurrent breakage at the time - no automation
surface reaches `QFileDialog`/native file pickers, so this had to be a manual run), developer-
confirmed against a real folder insert, including the follow-up display/highlight/toolbar tweaks.

## 9. Decisions

1. **An ISO goes only to a unit that is a CD-ROM drive** by the machine's configuration; no drop
   turns a hard-disk unit into a CD-ROM drive (user, 2026-09-29).
2. **The hold is a fixed 1.5 s** (user, 2026-09-29).
3. **Several targets: the user chooses** - a slot chooser menu on a quick drop, labeled drop zones
   with device icons on the hold; the floppy's drive A with autostart is the only shortcut (user,
   2026-09-29). No open questions left.
4. **The boot-compatibility check lives in the media manager, keyed by slot tags**, not as a
   per-machine-config callback - see §10 (user, 2026-09-30).
