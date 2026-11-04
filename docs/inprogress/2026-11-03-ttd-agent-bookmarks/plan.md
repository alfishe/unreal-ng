# TTD agent bookmarks: plan

**Status 2026-11-03:** done on all six planes; measured; accepted.

## Steps

| # | Step | State |
|---|------|-------|
| 1 | `TTDBookmark` gains note, tags, context, watches; `ValidateBookmark`; `TTDBookmarkJournal::Edit`; the bookmarks stream version 2 (version 1 still reads) | Done |
| 2 | `TimeTravelController::AddAgentBookmark` (the context at the current position live, another moment by one silent seek) and `EditBookmark` (`ttdcontrollerbookmarks.cpp`); `TTDBookmarksExportText` (`ttdbookmarkexport.h`): Markdown, profile comments | Done |
| 3 | Verbs: `bookmarks` (tag, text), `bookmark-add` (note, tags, watch, context), `bookmark-note`, `bookmark-export` (format, tag, title, path) | Done |
| 4 | Planes: CLI `ttd bookmark list\|add\|note\|export\|del`, WebAPI `GET /ttd/bookmarks?tag=&text=`, `POST /ttd/bookmarks`, `PUT /ttd/bookmarks/{label}`, `GET\|POST /ttd/export-bookmarks` (+ OpenAPI), Lua and Python `ttd_bookmark_add` / `ttd_bookmarks` / `ttd_bookmark_note` / `ttd_bookmark_export`, MCP `time_travel` `bookmark_add` / `bookmark_list` / `bookmark_note` / `bookmark_export` | Done |
| 5 | Unit tests B1-B8, MCP unit tests, the live MCP test, the live measurement (`scratch/ttd-agent-bookmarks/live_bookmarks.py` in the worktree) | Done |
| 6 | Docs chain: command-interface.md, the plane docs, the MCP README, the recording and reverse-debugging recipes | Done |

## Tests

`core/tests/debugger/ttd/ttdagentbookmarks_test.cpp`, a Pentagon running an IM 2 counter:

| # | What |
|---|------|
| B1 | The current position: PC and SP as the machine stands, IM 2, the code page, the watched byte as memory holds it |
| B2 | A past moment (`f+3000`): the context and the watched bytes as a seek there shows them; the machine and the session state unchanged |
| B3 | `context=false`, and the old two-argument `AddBookmark`: no context, no bytes |
| B4 | Edits through the verb: replace, append, tags replaced / added / removed, rename; nothing to change, a taken label, an unknown label, a bad tag refused |
| B5 | Add and list through the verbs: the reply node, the text line, the tag filter; watch and tag bounds refused |
| B6 | Export as Markdown (code spans, the seek line), profile (every line a comment, at most 120 columns), JSON; an unknown format refused |
| B7 | A `.ttd` file round trip keeps note, tags, context and bytes |
| B8 | A version 1 stream still reads; version 3 is refused |

MCP unit tests (`mcp-tools-test.cpp`): the list passes the server's lines on; `bookmark_note` puts the edit to the
label's path; `bookmark_export` shows the text and the written file. The live MCP test
(`tools/verification/mcp/test_time_travel.py`): a note, tags and a watched range with the moment's instruction, the
position kept, an append and a tag removed, the list by tag, Markdown and profile exports, refusals.

## Measurements (2026-11-03, M-series Mac, release build, unreal-qt offscreen)

The task: the agent marks N moments, each with a note and 2-3 watched ranges, then writes them up (time, PC,
instruction, registers, the watched bytes, the note). The old loop: `bookmark_add` with the label; for the write-up
`bookmark_list`, then per bookmark `seek_bookmark`, the registers, the disassembly at PC, a memory read per watched
range, and a seek back; the notes stay in the agent's context (lost on a clear, not in the `.ttd` file). New:
`bookmark_add` with note, tags and watch; one `bookmark_export`.

The write-up alone, old / new:

| Case | Surface | Calls | Bytes | MCP text | ms |
|------|---------|-------|-------|----------|----|
| 48K ROM, 3 bookmarks, 2 ranges | REST | 17 / 1 | 3885 / 1390 (2.8x) | - | 19.6 / 1.1 (18x) |
| | MCP | 17 / 1 | 3317 / 1395 (2.4x) | 3173 / 1295 (2.5x) | 23.6 / 2.1 (11x) |
| 48K ROM, 10 bookmarks | REST | 52 / 1 | 12524 / 4418 (2.8x) | - | 60.1 / 1.3 (45x) |
| | MCP | 52 / 1 | 10534 / 4423 (2.4x) | 10211 / 4203 (2.4x) | 72.8 / 1.6 (44x) |
| Green Beret, 3 bookmarks, 3 ranges | REST | 20 / 1 | 4315 / 1487 (2.9x) | - | 22.0 / 1.4 (15x) |
| | MCP | 20 / 1 | 3713 / 1492 (2.5x) | 3476 / 1392 (2.5x) | 26.5 / 1.5 (17x) |
| Green Beret, 10 bookmarks | REST | 62 / 1 | 13981 / 4726 (3.0x) | - | 66.2 / 1.3 (52x) |
| | MCP | 62 / 1 | 11838 / 4731 (2.5x) | 11208 / 4511 (2.5x) | 82.6 / 1.7 (49x) |

Adds and write-up together: 5.0-6.5x fewer calls, 1.1-1.3x fewer bytes, 1.6-1.7x less MCP text, 3.9-4.5x faster.
An add with the context costs the same wall time as a bare add (4.8 / 4.8 ms for three, 17.7 / 17.6 ms for ten over
REST): the silent seek is short. The new add replies are larger (the bookmark with its context comes back), and the
new export carries the notes, which the old loop's replies do not: the agent of the old loop still has to merge its
notes with the readouts by hand, the export is the finished WALKTHROUGH section or profile comment.

## Acceptance

- B1-B8 green. Done.
- Green Beret in play: bookmarks with notes exported as a WALKTHROUGH section and a profile comment. Done.
- The write-up in one call against 17-62; 2.4-3.0x fewer bytes, 2.5x less MCP text, 11-52x faster; the notes live
  in the session and the `.ttd` file. Accepted.
