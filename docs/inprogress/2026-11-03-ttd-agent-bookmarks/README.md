# TTD agent bookmarks: notes, tags, the moment's context, export

**Status 2026-11-03:** done on all six planes, measured, accepted ([plan.md](plan.md)). The fifth item of the agent UX roadmap, after the diff of two moments
([2026-11-03-ttd-diff-moments](../2026-11-03-ttd-diff-moments/README.md)). Accepted only if WebAPI / MCP measurements
prove it (owner 2026-11-01).

Files:

| File | What |
|------|------|
| [design.md](design.md) | the bookmark's new fields, the context capture, the verbs, the export formats, the file stream |
| [plan.md](plan.md) | steps, tests, measurements (export against the old loop), acceptance |

## The problem

A TD-4 bookmark is a label and a time. An agent working through a recording finds moments ("lives decrease here",
"the level loader starts", "the boss's pattern switches") and has to keep what it learned in its own context: the
label says where, nothing says what. When it writes the result up (a WALKTHROUGH step, the header comment of a game
profile), it seeks to each bookmark again, reads the registers, disassembles the instruction, reads the variables it
cares about, and seeks back: per bookmark three to five calls, and the notes themselves are gone once the
conversation is cleared. A saved `.ttd` file keeps the labels only.

## The goal

A bookmark carries the agent's note (free text, editable, appendable), tags, and the moment's context taken once when
it is added: PC, the code page, the instruction, the main registers, the paging latch and the bytes of up to eight
watched addresses. The note, tags and context live in the session and in the `.ttd` file. One call exports the
bookmarks (all, or by tag) as a WALKTHROUGH section in Markdown, as a YAML comment block for a game profile's header,
or as JSON; optionally written to a file.

## Requirements

| # | Requirement |
|---|-------------|
| AB-1 | `bookmark-add` takes `note`, `tags`, `watch` (addresses with lengths) and captures the context at the bookmark's time (the current position read live, another moment by one silent seek); `context=false` skips it |
| AB-2 | `bookmark-note` edits a bookmark: replace or append the note, replace / add / remove tags, rename |
| AB-3 | `bookmarks` lists the notes, tags and context; filter by tag; MCP text shows them |
| AB-4 | `bookmark-export` with `format=markdown\|profile\|json`, `tag`, optional `path` (server-side file); the machine does not move |
| AB-5 | Persisted: the bookmarks stream version 2 in the `.ttd` file (note, tags, context, watched bytes); version 1 files still load |
| AB-6 | All six planes: CLI, WebAPI (+ socket `call`), Lua, Python, MCP; docs chain, recipes |
| AB-7 | Bounded: note at most 4096 characters, 8 tags of at most 31, 8 watched ranges of at most 16 bytes; rejected, not truncated |
| AB-8 | Measured against the old loop through WebAPI and MCP (calls, bytes, MCP text, ms); accepted only if better |
| AB-9 | Tests about 10 ms each |

## Acceptance

- Unit tests B1-B8: add with note / tags / watch and the context at the current position and in the past, edit, list
  by tag, export in all three formats, file round trip (v2 and a v1 file), bounds.
- A live run on a real game: bookmarks with notes exported as a WALKTHROUGH section and a profile comment.
- The one export call against the old loop: fewer calls, bytes and MCP text characters. Numbers in plan.md.
- Zero warnings; full core-tests; the live MCP scripts.
