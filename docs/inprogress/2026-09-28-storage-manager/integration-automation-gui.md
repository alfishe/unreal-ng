# Integration: automation surfaces and the Qt GUI

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; manager phase M4 (after block, floppy and tape slots exist) |
| **Today** | per-type code on every surface: WebAPI `/tape/*` and `/disk/{drive}/*`, CLI `tape` / `disk`, MCP `load_software` with its own extension table, Lua / Python `disk_*` / `tape_*` that edit `CoreState` directly; nothing for SD, HDD, CD; GUI has no eject, no drive picker, no SD / HDD UI (research §3) |

## 1. One vocabulary

| Verb | Arguments | Result |
|---|---|---|
| `media list` | — | every slot of the machine: id, kind, label, removable, content (source, format, access), dirty (changed units), activity |
| `media info` | slot | the same for one slot + format details (geometry, FAT type and layout for folders, skipped-entry report) |
| `media insert` | slot, path (file or folder), `access`, `format` (optional hint), `fs` (folder FAT type) | `Result` with the report |
| `media eject` | slot, `force` | refused when dirty unless `force` |
| `media create` | slot, blank spec (floppy format, card / disk size) | |
| `media save` | slot | writes session changes back to the source file |
| `media export` | slot, path, format | writes the current contents to a new file |
| `media discard` | slot | drops session changes |
| `media rescan` | slot | folder volumes, only when not dirty |

| Surface | Form |
|---|---|
| WebAPI | `GET /api/v1/emulator/{id}/media`, `GET /media/{slot}`, `POST /media/{slot}/insert` (JSON `{path, access, format, fs}` or a multipart upload → source `Upload`), `POST /media/{slot}/eject`, `/create`, `/save`, `/export`, `/discard`, `/rescan` |
| CLI | `media list`, `media insert sd.zc ~/zx/sd --access session`, ... |
| MCP | one `media` tool with an `action` argument (like `emulator_manage`); `load_software` asks the registry for kind and slot instead of its own table |
| Lua / Python | `emu:media_list()`, `emu:media_insert(slot, path, opts)`, ... (same names) |

**Compatibility.** `/disk/{drive}/insert|eject|create`, `/tape/load|eject`, the CLI `disk` / `tape`
verbs and the Lua / Python `disk_*` / `tape_*` functions stay. They become one-line wrappers over
the `media` operations, with `drive` mapped to `fdd.<letter>`. Disk inspection endpoints (sector,
track, catalog, sysinfo) stay floppy-specific and look the medium up through the manager.

**Errors.** Every surface maps `Result` codes the same way:

| Code | Meaning | HTTP |
|---|---|---|
| `unknown-slot` | no such slot on this model | 404 |
| `kind-mismatch` | the source cannot go in this slot (a folder into the tape, an ISO into `fdd.a`) | 400 |
| `unreadable-source` | missing file or folder, no permission | 400 |
| `unknown-format` | no format matched; the message lists the kinds tried | 400 |
| `dirty` | unsaved changes; retry with `force` | 409 |
| `recording` | refused while TTD records (see [integration-ttd-snapshots.md](integration-ttd-snapshots.md)) | 409 |

## 2. Qt GUI

```
┌ Media ─────────────────────────────────────────────────────────────────┐
│ ● fdd.a   Drive A        elite.trd            session   *3 tracks  [⏏] │
│ ○ fdd.b   Drive B        (empty)                                   [+] │
│ ● tape    Tape           demo.tzx  12:34/40:00 read-only           [⏏] │
│ ● sd.zc   SD card        ~/zx/sd/ (folder)    session   *48 sectors[⏏] │
│ ○ ide0.master HDD        (none)                                    [+] │
│  [Insert file…] [Insert folder…] [Eject] [Save] [Export…] [Discard]    │
└────────────────────────────────────────────────────────────────────────┘
```

- A dock / dialog listing `media list`: one row per slot; the LED shows activity from
  `NC_MEDIA_ACTIVITY`; `*` shows dirty units. WinUAE's hard-disk panel, generalized to every kind
  (research §1).
- "Insert folder…" appears only on slots that accept folders. The skipped-entry report shows as a
  non-blocking message.
- File dialogs take their filters from `MediaFormatRegistry::Extensions(kind)`.
- **Drag-and-drop**: onto a row → that slot; onto the screen → the registry's probe picks the kind
  and the machine's default slot. Shift keeps today's meaning (insert without autostart).
- **Recent media** (the disabled placeholder today): kept per kind; the list stores the source and
  the access mode.
- **Model switch**: the dirty prompt lists the dirty slots with Save / Export / Discard per slot
  (FR-21), then the media set follows (M5).
- The File menu's Save Disk entries become "Save / Export medium…" for the selected row. The
  "current drive = the WD1793's selected drive" rule goes away.

## 3. Tests

- A WebAPI / CLI round trip per verb against a test emulator.
- Error mapping.
- Compatibility wrappers keep their old responses.
- Eject of drive B leaves A (the current bug) on every surface.
- GUI: a model test for the media list view.
