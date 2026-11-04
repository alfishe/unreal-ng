# Answer budgets: "fit the answer in N characters"

Status: done 2026-11-04, accepted on the measurement below. Roadmap: the agent-TTD UX list (after run until / frame watches). Accepted only if a WebAPI / MCP
measurement shows it pays.

## Problem

An agent pays for every character it reads. Most MCP answers are already short summaries, but some are not:

| Call (Green Beret, 300 recorded frames) | content[] text | structuredContent |
|:--|--:|--:|
| `memory_access read` 4096 bytes | 19,754 | 20,040 |
| `manage_symbols list` | 7,482 | 337,379 |
| `debug_code disassemble` 200 lines | 6,141 | 13,061 |
| `debug_code trace` 1 frame | 2,120 | 5,058 |
| `time_travel coverage_summary` | 329 | 9,605 |

(`scratch/answer-budgets/survey.py`.) Today the agent finds out after it paid: it reads the long answer, then asks
again with a smaller `count` / `length` / `limit`. With `response_format: "json"` the echoed JSON is cut at 64 KiB
mid-text.

## Design

One argument on every MCP tool and one query parameter on every REST route (and so every WebSocket `call`):
`max_chars` (500 .. 10,000,000). The server fits the answer and says what it left out.

- **Text** (MCP `content[]` text; REST `text/plain`): kept whole when it fits. Otherwise cut on line boundaries: the
  first lines (the summary every tool puts first), one omission line, and up to three last lines (totals, the last
  address) when they fit in a quarter of the budget. The omission line names the count and how to get the rest:
  `... 230 of 260 lines (17,412 chars) left out for max_chars=2000; narrow the call or raise max_chars`. A single line longer than the budget is cut with `...`.
- **JSON** (MCP `structuredContent` and its `response_format: "json"` echo; REST `application/json`): the biggest
  array is shortened, then the next, and long strings are cut, until the compact JSON fits. An object root gets
  `"truncated": {"max_chars": N, "chars": <full size>, "omitted": {"<path>": <items left out>}}` (paths like
  `symbols`, `frames[3].events`); REST also sends `X-Unreal-Truncated: <full size>`.
- MCP fits what the model reads: `content[]` as a whole (the text alone gets the budget; with `response_format:
  "json"` the text gets a third and the JSON echo the rest), and apart from it `structuredContent` (a client that reads
  it reads it instead of the text). The notes name the budget asked for. Images are not characters and stay.
- A JSON string of several lines (a hexdump, a listing) is cut on line boundaries like a text.
- Characters are Unicode code points; nothing is ever split inside a UTF-8 sequence.
- A bad `max_chars` (not a number, below 500) is refused before the handler runs: MCP a tool error, REST 400.
- No budget: answers are unchanged.

**Planes.** MCP, REST, WebSocket (the REST routes as calls). CLI, Lua and Python run in the process and hand the full
values to a script; a character budget means nothing there, so they get none.

Code: `core/automation/common/answerbudget.h` (header-only: the MCP and WebAPI libraries both use it), the MCP
dispatcher, the WebAPI request gates (the check) and a post-handling advice + the socket's forward (the fit).

## Measurement

`scratch/answer-budgets/live_budget.py`: the same questions answered by the old way (one call, read everything; then
the narrower repeat an agent makes) and by one call with `max_chars`, over MCP and REST: calls, characters, and
whether the answer still holds its summary line and the way to the rest.

Green Beret, 300 recorded frames (characters an agent reads; "old" = the whole answer plus the narrower repeat):

| plane | case | whole | `max_chars=2000` | less | `max_chars=600` | less | old calls / chars |
|:--|:--|--:|--:|--:|--:|--:|:--|
| MCP | `memory_access read` 4K | 19,754 | 1,990 | 9.9x | 526 | 37.6x | 2 / 21,027 |
| MCP | `debug_code disassemble` 200 | 6,141 | 1,952 | 3.1x | 547 | 11.2x | 2 / 7,002 |
| MCP | `manage_symbols list` | 7,482 | 1,968 | 3.8x | 569 | 13.1x | 2 / 9,026 |
| MCP | `coverage_summary`, json | 9,929 | 1,679 | 5.9x | 566 | 17.5x | 1 / 9,929 |
| MCP | `list_models`, json | 4,055 | 1,562 | 2.6x | 325 | 12.5x | 1 / 4,055 |
| REST | `/memory/read` 4K | 24,741 | 1,866 | 13.3x | 465 | 53.2x | 2 / 26,256 |
| REST | `/disasm` 200 | 13,061 | 1,837 | 7.1x | 393 | 33.2x | 2 / 14,625 |
| REST | `/labels` | 337,379 | 1,722 | 195.9x | 324 | 1041x | 1 / 337,379 (no filter) |
| REST | `/emulator/models` | 4,030 | 1,537 | 2.6x | 300 | 13.4x | 1 / 4,030 |

Every fitted answer keeps its first (summary) line and says what was left out and how to get it; one call instead of
two; time unchanged (the fit costs well under a millisecond next to the handler). A bad `max_chars` is an MCP tool
error / REST 400 before the handler runs; a WebSocket `call` with `?max_chars` comes back fitted with its `truncated`
note. `structuredContent` gets its own budget: `memory_access read` at 800 keeps the hexdump's head lines and
`truncated.max_chars: 800`.
