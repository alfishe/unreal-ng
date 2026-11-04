# TTD agent bookmarks: design

## The bookmark

`TTDBookmark` (`core/src/debugger/ttd/ttdbookmarks.h`) keeps its TD-4 fields (time, label) and gains:

| Field | What | Bound |
|-------|------|-------|
| `note` | free text, the agent's finding; lines kept | 4096 characters |
| `tags` | short words to group bookmarks (`lives`, `loader`, `boss.phase2`) | 8 tags of 1..31 of `[A-Za-z0-9_.-]`, no duplicates |
| `context` | the moment as it was: PC, SP, AF, BC, DE, HL, IX, IY, IFF1, IM, the `#7FFD` latch, the code page under PC (ROM / RAM / cache and its number), the instruction at PC | instruction at most 63 characters |
| `watches` | up to 8 ranges `address[:length]` with their bytes at the moment | 8 ranges of 1..16 bytes |

A value over a bound is refused with a message (`ValidateBookmark`), never cut. Bookmarks stay advisory: they are never
replay barriers and a seek to one lands as any seek does.

## Taking the context

The context is taken once, by `bookmark-add` (`TimeTravelController::AddBookmark` in `ttdcontrollerbookmarks.cpp`):

- the bounds, the label and the time (inside the history, not past its end) are checked first, so a refused bookmark
  costs no seek;
- the current position: the machine is read as it stands; a running recording keeps running;
- another moment: one silent seek under the session's `Change` operation, the same pattern as the other read-only
  queries (`SaveLiveState`, `ReplayModeScope`, `SeekToInternal`, `RestoreLiveState`): the machine, its state and
  the session state come back as they were. A recording pauses for browsing first, as every past read does;
- the seek lands at the first instruction boundary at or after the T asked for; the bookmark keeps the time asked for;
- a moment the seek cannot reach (the history's start moved past it, a barrier) gives a bookmark without context;
- `context=false` skips the capture: a plain TD-4 bookmark with a note and tags.

The registers come from `SaveLiveState` (the same CPU and chipset copy the session file stores), the code page from
`MapZ80AddressToPhysicalPage`, the bytes from `DirectReadFromZ80Memory` (no access hooks, no counters), the
instruction from the debug manager's disassembler.

## Editing

`bookmark-note` (`TTDBookmarkJournal::Edit` under the journal's lock, on a copy validated before it replaces the
original): `note` replaces the note or, with `append=true`, adds it as a new line; `tags` replaces the tags,
`add_tags` / `remove_tags` change them; `rename` moves the label (unique: a taken one is a conflict). The time, the
context and the watched bytes do not change: a different moment is a new bookmark.

## The verbs

| Verb | Options | Reply |
|------|---------|-------|
| `bookmarks` | `tag`, `text` | `count`, `bookmarks[]` (frame, tinframe, label, note, tags, context or null, watch[]), `text`: one line per bookmark |
| `bookmark-add` | `label`, `frame`, `tinframe`, `note`, `tags`, `watch`, `context` | `added`, the bookmark, `text` |
| `bookmark-note` | `label`, `note`, `append`, `tags`, `add_tags`, `remove_tags`, `rename` | `updated`, the bookmark, `text` |
| `bookmark-export` | `format`, `tag`, `title`, `path` | `format`, `count`, `tag`, `text`, `path`, `bytes` |

Lists (`tags`, `watch`) are comma lists in the verb; WebAPI JSON and MCP take arrays, Lua tables, Python lists.
Errors: a bound or a missing field 400, an unknown label 404, a taken label or a time out of the history 409.

Planes: CLI `ttd bookmark list|add|note|export|del`, WebAPI `GET /ttd/bookmarks?tag=&text=`,
`POST /ttd/bookmarks`, `PUT /ttd/bookmarks/{label}`, `GET|POST /ttd/export-bookmarks` (also a socket `call`; the
export is not under `/ttd/bookmarks/` so a label named `export` stays a label), Lua `ttd_bookmark_add`,
`ttd_bookmarks`, `ttd_bookmark_note`, `ttd_bookmark_export`, Python the same names, MCP `time_travel` actions
`bookmark_add`, `bookmark_list`, `bookmark_note`, `bookmark_export`.

## Export

`TTDBookmarksExportText` (`ttdbookmarkexport.h`), the bookmarks in time order, all or one tag; the machine does not
move (the context was taken at add).

Markdown: a WALKTHROUGH section; hex in code spans (GitHub reads `$..$` as math):

```markdown
## Green Beret

3 bookmark(s), Pentagon history f0+0 .. f300+0.

### lives-decrement (`f212+31540`)

The life counter drops when the soldier touches an enemy.

- **At:** `PC #A1F3 (RAM 2) dec (hl); SP #FF42 AF #0344 BC #0000 DE #5A20 HL #A13C IX #0000 IY #5C3A IM 1 EI`
- **Watched:** `#A13C=03`
- **Tags:** `lives`
- **Seek:** `time_travel {"action":"seek_bookmark","label":"lives-decrement"}`
```

Profile: `#` comment lines wrapped at 120 columns, for the header of a game profile
(`data/controllers/profiles/games/*/*.yaml`):

```yaml
# Green Beret: found with TTD (3 bookmark(s), Pentagon history f0+0 .. f300+0).
# - lives-decrement f212+31540, PC #A1F3 dec (hl), #A13C=03: The life counter drops when the soldier touches an
#   enemy.
```

JSON: `{title, machine, count, bookmarks}`, the bookmarks as the `bookmarks` reply gives them.

`path` also writes the text to a file on the emulator's machine (`FileHelper::ToFsPath`); a file that cannot be
written is a conflict, the text is not returned then.

## The file stream

Holder stream 12 `bookmarks` goes to version 2 (`ttdsessionfacts.cpp`): per bookmark frame u64, T u32, label, note,
tag count + tags, context valid u8 (+ the registers u16, code page u16, IFF1, IM, `#7FFD`, code kind u8, instruction),
watch count + per watch address u16, length u8, byte count u8, the bytes. Version 1 (frame, T, label) still decodes;
a version above 2 is refused. A bookmark in a file that breaks a bound is left out, the rest load.
