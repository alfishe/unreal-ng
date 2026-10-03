# Recipe: BASIC Program Injection, Run and Extraction

Goal: put a numbered BASIC listing into the machine without typing it key by
key, `RUN` it, and read the program back as text (a round trip). Also covers
the BASIC environment (`basic/state`, `basic/mode`: 48K vs 128K editor), typing
one BASIC line through the real editor (`keyboard/type` with `tokenized`), and
what changes at the TR-DOS `A>` prompt.

Prerequisite: a running instance on a model that has a BASIC ROM (`48K`,
`128k`, `PENTAGON`, ...). For driving TR-DOS commands and running programs from
a disk see [manual-trdos-run.md](manual-trdos-run.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred -
> `type_input` types a BASIC line; injection, extraction and `RUN` ride
> `invoke_api` (there is no dedicated BASIC tool). Use [WebAPI](#webapi) only
> inside host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Two ways in, and which to pick

| Way | Call | What it does | Use for |
|:--|:--|:--|:--|
| **Program write** | `basic/inject` with `program` | Tokenizes a numbered multi-line listing and writes it straight into program memory (a deterministic memory write: the ROM editor is not involved) | Whole programs, any length |
| **Typed line** | `basic/run` with `command`, or `keyboard/type` with `tokenized` | Types the line through the keyboard matrix into the ROM editor, every key proven taken on the ROM's control points; `basic/run` then presses ENTER | Direct commands (`RUN`, `CAT`, `PRINT 2+2`, `LOAD ""`), TR-DOS commands, one numbered line the way a user enters it |

`basic/inject` with `command` (instead of `program`) types the line into the
editor *without* ENTER via the older ROM-injection path; prefer `basic/run`
(with `press_enter: false` when you want it left on the line).

## MCP (preferred)

```text
# 1. check where the machine is (a fresh 128k/PENTAGON sits in the 128K menu)
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/basic/state"}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/basic/mode","body":{"mode":"48k"}}

# 2. write a listing into program memory (does not run it)
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/basic/inject",
                   "body":{"program":"10 FOR I=1 TO 3\n20 PRINT I\n30 NEXT I"}}

# 3. run it: typed RUN + ENTER, verified on the ROM; wait for the report
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/basic/run",
                   "body":{"command":"RUN","wait_report":true}}
inspect_state      {"aspects":["screen_ocr"]}

# 4. read the program back
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/basic/extract"}

# typing one line without ENTER (tokenized, keyword-aware) via the keyboard tool
type_input         {"action":"type","text":"PRINT 2+2","tokenized":true}

# empty the program (NEW equivalent)
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/basic/clear"}
```

`type_input` `type` with `tokenized: true` forwards `text` and `tokenized`
(and `delay_frames`, which does not apply in that mode) to
`POST /keyboard/type`. Without `tokenized` it is plain host-text typing with
`delay_frames` between keys (default 2), queued, not verified. The MCP
resource `unreal://basic-reference` has a keyword and system-variable crib.

## WebAPI

### State and mode

```bash
curl -s "$BASE/emulator/$EMU_ID/basic/state" | jq .
#   → {"success":true,"in_editor":..,"state":"menu128k|basic128k|basic48k|unknown",
#      "description":..,"ready_for_commands":..}

# leave the 128K menu: "48k" (48 BASIC, SOS ROM) or "128k" (128 BASIC editor)
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/mode" \
     -H 'Content-Type: application/json' -d '{"mode": "48k"}' | jq .
```

`mode` is only an action from the **128K menu** (it navigates and returns
`success: true`, `mode`). Already in the requested mode: `success: true` with
"Already in ... mode". Anywhere else it returns `success: false` with
`current_state` (a number) and still HTTP 200. Anything but `48k` / `128k`
(case: `48k`/`48K`, `128k`/`128K`) is a 400.

### Inject, run, extract (the round trip)

```bash
# 1. inject a listing (numbered lines, \n separated)
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/inject" \
     -H 'Content-Type: application/json' \
     -d '{"program": "10 FOR I=1 TO 3\n20 PRINT I\n30 NEXT I"}' | jq .

# 2. RUN it (the default command is "RUN")
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/run" \
     -H 'Content-Type: application/json' -d '{"wait_report": true}' | jq .

# 3. extract it back and compare
curl -s "$BASE/emulator/$EMU_ID/basic/extract" | jq -r '.program'
```

Compare in a script by normalizing both sides (trim trailing whitespace and
the final newline) before diffing; the extractor re-renders keywords from the
tokenized bytes, so spacing can differ from your source. The exact output
layout (line separators, keyword spacing) was not confirmed from source: check
the first round trip on your build before asserting on whole strings.

### `basic/run` options

Body (all optional): `command` (default `"RUN"`), `press_enter` (default
`true`), `wait_report` (default `false`: with `true` the call waits until the
command reports, up to the timeout), `trace` (default `false`: add the
`cyclogram` of ROM control points to the reply), `timeout_ms` (default 30000,
wall clock).

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/run" \
     -H 'Content-Type: application/json' \
     -d '{"command": "PRINT 2+2", "wait_report": true}' | jq '{success, outcome, failure, message, editor, basic_mode, report}'
```

### Typing a line without ENTER

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/type" \
     -H 'Content-Type: application/json' \
     -d '{"text": "10 PRINT \"HI\"", "tokenized": true}' | jq .
# then ENTER yourself when ready:
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" \
     -H 'Content-Type: application/json' -d '{"key": "Enter"}' | jq .
```

`tokenized: true` types the line the way the active editor needs it (keywords
as single keys in the 48K editor, spelled-out letters in the 128K editor, which
tokenizes at ENTER), proves each key, never presses ENTER, and returns the
same result object as `basic/run` plus `text` and `tokenized: true`. Add
`"trace": true` for the cyclogram. Without `tokenized`, `keyboard/type` just
queues host characters with `delay_frames` between them and answers at once
(`success`, `text`, `length`, `delay_frames`, `tokenized: false`, `message`):
good for typing into a running game or a prompt, wrong for BASIC keywords.

### Clear

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/clear" | jq .
#   → {"success":true,"message":"BASIC program cleared"}
```

## 48K vs 128K editor

- A fresh `128k` / `PENTAGON` / `SCORPION` boots into the **128K menu**.
  `basic/inject` with `program`, `basic/inject` with `command`, and `basic/run`
  leave the menu themselves (they select 128 BASIC and wait for the editor, a
  few frame batches); `basic/mode` does it explicitly and lets you pick **48K**
  instead (the 48 BASIC entry, the SOS ROM editor).
- The editors differ in how a line is typed: the 48K editor takes keywords as
  keys (`J` in K mode is `LOAD`); the 128K editor takes letters and tokenizes at
  ENTER. `basic/run` and `keyboard/type` + `tokenized` choose by the detected
  editor, so pass plain text (`LOAD ""`, `PRINT 2+2`). The reply's `editor` and
  `basic_mode` (`48K` / `128K` / `trdos` / `unknown`) say which one it was.
- `basic/inject` with `program` writes the tokenized program at the default
  program start `0x5CCB` and rebuilds the 48K system variables (PROG, VARS,
  E_LINE, WORKSP, STKEND, NXTLIN, CH_ADD, ERR_NR). It is the same write in both
  editors; whether the 128K editor's own edit state is fully consistent
  afterwards was not checked here: after injecting on 128K, `RUN` and
  `extract` are the proof, not the screen.
- `state` in the `inject` reply is `basic48k`, `basic128k`, `trdos` or
  `unknown`; `basic/state` itself reports `menu128k`, `basic128k`, `basic48k`
  or `unknown`.

## What happens in TR-DOS

At the TR-DOS `A>` prompt the keyboard is read by the 48K editor (TR-DOS
borrows it), so:

- `basic/run` with `command` types the line through that editor and reports
  what TR-DOS did: `outcome: "trdos_command"` when TR-DOS took it,
  `failure: "trdos_rejected"` with `err_nr` otherwise, `basic_mode: "trdos"`.
  With no disk in the drive every command ends in `err_nr` 26. Details and the
  command crib: [manual-trdos-run.md](manual-trdos-run.md).
- `basic/inject` with `command` is accepted at the prompt (the line is placed
  in the editor buffer); `basic/inject` with `program` is **not**: it requires
  a real BASIC editor and fails with "Not in BASIC editor" (HTTP 400,
  `state: "trdos"`).
- `basic/state` has no TR-DOS branch: it answers `state: "unknown"`,
  `ready_for_commands: false` while TR-DOS owns the machine. Do not wait for
  `basic48k` there; check `basic_mode` in a `basic/run` reply, or OCR for `A>`.
- `basic/extract` reads the BASIC program area (PROG..VARS) whatever ROM is
  paged, so after `LOAD "name"` from a disk it returns the loaded program
  (not confirmed on a live run).

## Response fields worth asserting

| Call | Fields |
|:--|:--|
| `basic/inject` | `success`, `message`, `state`; HTTP 400 when `success` is false or when neither `command` nor `program` is in the body |
| `basic/extract` | `success`, `program` (text, `""` when none), `message` ("No BASIC program found in memory" when empty) |
| `basic/clear` | `success`, `message` |
| `basic/state` | `success`, `in_editor`, `state`, `description`, `ready_for_commands` |
| `basic/mode` | `success`, `message`, `mode` or `current_state` |
| `basic/run`, `keyboard/type` (tokenized) | `success`, `outcome` (`typed`, `started`, `finished`, `stored`, `syntax_error`, `trdos_command`, `failed`; the exact wire strings come from `CommandTyper::OutcomeName`, not confirmed), `failure` (only on failure), `message`, `editor`, `basic_mode`, `err_nr`, `report` (`err_nr + 1`), `bytes_typed`, `frames`, `command` or `text`, `cyclogram` (with `trace`) |

HTTP status of `basic/run` / tokenized `keyboard/type`: 200 when the ROM did
what was asked; **409** when the machine could not take input (keyboard
locked by a TTD replay, other automation still typing, editor never idle,
emulator paused, wall-clock timeout); **400** for an unknown target (no
recognised editor ROM at `#0000`); **422** when the ROM refused the line
(syntax error, key rejected, line full, ...). The failure names are listed in
`core/src/debugger/analyzers/basic-lang/commandtyper.h`; the design is in
`core/src/debugger/analyzers/basic-lang/input-verification.md`.

## Pitfalls

- **The emulator must be running for typed lines.** A paused machine runs no
  frames: `basic/run` and tokenized `keyboard/type` fail at once with an
  emulator-paused failure (409). `basic/inject` with `program` writes memory
  and works paused when the editor is already up (from the menu it steps
  frames itself and restores the running state).
- **`basic/inject` needs numbered lines.** Lines without a number are not part
  of a program; a listing with no valid numbered line fails with "No valid
  numbered BASIC lines found in program". A keyword the tokenizer does not
  know is a failure of the same kind (exact tokenizer rules: not confirmed;
  see `docs/inprogress/2026-01-17-basic injection/zx-spectrum-basic-tokenization.md`).
- **Injection does not run anything.** It also does not reset variables or the
  screen; `RUN` does that. After `basic/clear`, `extract` returns `""`.
- **`basic/inject` has two bodies:** `command` wins when both `command` and
  `program` are present.
- **`"` in JSON.** Escape quotes in listings (`\"HELLO\"`); in bash build the
  body with `jq -n --arg p "$LISTING" '{program:$p}'` instead of hand-quoting.
- **Typing speed is not a knob.** `delay_frames` only affects untokenized
  `keyboard/type`; verified typing paces itself on the ROM.
- **Raw `type_input` taps are keyword keys in K mode.** `tap C` in the 48K
  editor is `CONTINUE`, not the letter; use `basic/run` / `tokenized` for BASIC
  text (same trap as in [manual-trdos-run.md](manual-trdos-run.md)).
- **A game or loader owns the keyboard.** Typed BASIC only works in a
  recognised editor; in anything else the result is an unknown-target or
  busy failure, never a silent write.
- **Not covered:** the CLI, Lua (`basic_run`, `basic_inject`, `basic_extract`,
  `basic_clear`, `basic_state`) and Python client calls exist alongside these
  routes (per the 2026-01-17 design notes); their exact signatures were not
  re-checked for this recipe.
