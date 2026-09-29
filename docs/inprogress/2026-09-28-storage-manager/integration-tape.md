# Integration: the tape deck (`tape`)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; manager phase M3 (after block M1 and floppy M2) |
| **Layers** | port decoder → port adapter → device → medium, with the slot and the manager beside them: [technical-design.md §1.1](technical-design.md#11-layers-from-the-guests-port-to-the-medium) |
| **Today** | the medium is `CoreState::tapeFilePath`; `Tape::EnsureImageLoaded` parses lazily; `Emulator::LoadTape` accepts tap/tzx only while `TapeLoaderRegistry` and the GUI offer more; eject copied in four surfaces, no notification (research §3) |

## 1. What changes for the user

- Every format the tape registry understands can be loaded from every surface (SPC, STA, LTP, ZXT,
  CSW, WAV included).
- Eject is one operation with a notification.
- The tape shows in the media panel with its position.

## 2. The slot

| Field | Value |
|---|---|
| id | `tape` |
| kind | `Tape` (the medium is the parsed tape image) |
| removable | yes, no swap delay (the deck is stopped on eject; no guest detection exists) |
| default access | `ReadOnly`. The emulator has no tape recording today (`SAVE` to tape goes nowhere). If recording is added later, the recorded tape is a `Blank` source that the manager exports to TZX / TAP |
| registered by | `Tape` (created by `Core`) |

## 3. Migration

| Today | After |
|---|---|
| `LoadTape` hard-codes tap/tzx | `Insert("tape", File)`; the registry's tape entries are `TapeLoaderRegistry`, folded in unchanged (content probe, extension tie-break) |
| the path string is the medium; parsing is keyed on it (reloading the same path does not re-parse) | the medium owns the parsed image; `Attach` hands it to `Tape`; re-inserting the same file re-parses |
| eject = `stopTape()` + clear the path, four copies | `Eject("tape")` → `Tape::Stop` + detach + `NC_MEDIA_EJECTED` |
| TTD: load invalidates; eject does nothing; transport controls are barriers | insert and eject are refused while recording unless the caller ends it (common rule, [integration-ttd-snapshots.md](integration-ttd-snapshots.md)); transport controls stay barriers |
| uploads (`UploadHelper` `MediaType::Tape`) | source type `Upload`: the staged file is deleted on eject |

## 4. Folder as a tape (M3)

`FolderTapeBuilder` takes the shared folder pipeline (technical design §6.0) and builds an in-memory
TZX (standard-speed blocks, ID `#10`). The tape deck plays it like any other tape.

| Rule | Value |
|---|---|
| **One folder, order, compatible names** | as for disk images ([integration-floppy.md](integration-floppy.md) §4.1); names are the ROM's 10 characters |
| **Hobeta files** | a standard header (`$B` → program, `$C` → bytes, `$D` → array) + data block; name, start, length from the Hobeta header |
| **`.tap` / `.tzx` files in the folder** | their blocks are appended unchanged, so a folder can mix ready tapes and loose files |
| **Other files** | a `Bytes` header (name from the file, start 32768 or the manifest's `files:` entry) + data block |
| **Per-file limit** | 65 535 bytes (the header's length field); a bigger file is skipped and reported |
| **Pause between files** | manifest `tape.pause`, default 1 000 ms |
| **Capacity** | no real size limit, but an artificial one: `kCassetteSideMs = 45 × 60 × 1000`, **one side of a C90 cassette**. The duration of each block is computed from the ROM timings (pilot, sync, 855 / 1 710 T-state bit pulses at 3.5 MHz) plus pauses. A file that would pass the end is skipped and reported, and placing continues with the next file, as for disks |

Example: a folder of 30 code files of 40 KB each. One file takes about 5 s (header) + 1 s pause +
242 s (data at ~170 bytes/s) + 1 s pause ≈ 4.2 minutes, so the 30 files would need about two hours.
The first 10 files (≈ 42 minutes) go on the tape; the report lists the other 20 as "does not fit on
one side of a C90".

## 5. Tests

- The existing tape tests run through `Insert`.
- Every registry extension loads from the WebAPI and the CLI.
- Eject notifies once; while a recording runs it is refused unless `endRecording`.
