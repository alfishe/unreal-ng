# Recipe: Reverse engineering a game with the analysis layers

Goal: from a game's own tape, disk or snapshot to a reviewed SkoolKit disassembly that builds back into the bytes
the game ran, with code, data, text and graphics told apart, routines and variables named, and every claim tied to
what the emulator saw. The emulator does the mechanics (it runs, records, replays and classifies); the agent spends
tokens only on what needs judgement: names, kinds the detectors could not tell, bugs, conflicts. Read nothing else
first: this recipe is the whole workflow for an agent that has never seen the analysis code.

## Why this works in an emulator

The analysis lives inside unreal-ng (module doc: [analysis-layers.md](../../docs/emulator/analysis/analysis-layers.md)),
so it sees what a disassembler working from a snapshot cannot:

| Layer | What it gives you | Action |
| --- | --- | --- |
| L0 | per-byte access facts: fetched as an opcode or an operand, read, written, used as stack, by which instruction, when | `explain` (a byte) |
| L1 | routines, the call graph, loops, record fields and the value flow (where a byte read was written from), from a TTD recording replayed on a muted clone | `build`, `explain` (a routine) |
| L2 | regions with a kind (code, text, font, sprite, tile, records, pointer table, music, ...), geometry, a confidence 0..1000, the draft | `query`, `export`, `gallery` |
| L3 | the findings register, the review queue, experiments on clones, directed coverage | `findings`, `review`, `experiment`, `cover` |

- **Every question goes to the machine.** "Who writes $5C00", "is this branch ever taken", "what does this routine
  draw" are a run on a clone of the recording, not reasoning over code: `experiment` answers in seconds.
- **The run is the evidence.** A claim is `observed` (seen in the recording or an experiment), `inferred` (read from
  code or a rule), `confirmed` (a reviewer said ok) or `refuted` (kept, shown wrong). Negative claims stay bounded:
  "not executed in this recording", never "dead code".
- **Any machine, any medium.** A 48K tape, a 128K game, a TR-DOS disk: the layers work on the Z80 view and the RAM
  pages the game mapped. No loader is reimplemented: the emulator loads the medium the way a user does.

Everything goes through **one MCP tool, `analysis`**, with an `action`. The same actions exist on the CLI
(`analysis review next`), Lua / Python (`emu.analysis("review", ...)`), REST (`/api/v1/emulator/{id}/analysis/...`)
and the WebSocket. The host-side mechanics (launch, load, play, export, the gates) are one script,
`tools/verification/analysis/re_session.py`.

## Stages

| Stage | Who | What happens | Done when |
| --- | --- | --- | --- |
| 1. Capture | script | load the medium, find the keys to play, save the start image, record a play session, build L1 + L2 | `analysis status` says done; `start.bin` and the first queue page written |
| 2. Coverage | script, then agent | directed coverage takes the branches the session took one way only; more input scripts for parts random play missed | `cover` ran once; the code share of the range stops growing |
| 3. Review | agent | the queue: bugs, conflicts, unnamed hot routines, low-confidence claims, unknown areas, entries, naming batches | the queue is empty except skips with a comment |
| 4. Export | script | the draft in every format, the findings register | `W/draft/` written after the last resolve |
| 5. Gates | script | rebuild, assemble, score | `rebuild` and `assemble` ok; the score written |

The levels of the result, each checked by a gate:

| Level | What it means | Gate |
| --- | --- | --- |
| R0 | the draft's bytes are the image the game ran from | `check`: `rebuild-plain` |
| R1 | the draft assembles into the same bytes with every instruction labelled, `@isub` applied | `check`: `assemble` |
| R2 | code and data are told apart, graphics have their geometry | `check` with `--reference`: byte categories, code entries, graphics |
| R3 | routines and variables are named and described | the score's names; the queue holds no `name` batch |
| R4 | operands are labels, not numbers (the source relocates) | `assemble`'s `skool2asm_warnings` falls to 0 (a moved build is not run yet) |

## Budget and rules

- **Work the queue, not the draft.** It holds only what needs a decision, at most 40 items a page plus one summary
  line; `offset` pages on, `reason` picks one reason.
- **Never print a whole draft, map, recording or findings file.** Each review item comes with a fact sheet under
  1 KB; ask for more with `explain` (one address) or `gallery` (one picture as PNG: look at graphics, never at DEFBs).
- **Do not edit the draft by hand.** Every decision goes through `review resolve`; the export carries it (labels,
  entry titles, comments, kinds). A hand edit is lost at the next export.
- **One decision per item.** If the sheet does not tell, run one experiment, then decide. For findings (`ok` /
  `wrong`), skip rather than guess. For names the rule is the other way round: name every subject of a batch, as a
  human reverser would, from what the facts show (`DrawCharacter`, `LessonTimer`, `ScoreDigits`); a name that says
  what the code does or the data holds beats an auto label. Skip a subject only when its facts are empty.
- **Blind on the reference.** The reference disassembly scores the result at the end; never read it while reviewing.
- **No per-game rules.** A detector that misses here is a known limit or a general fix with a fixture of its own,
  never a rule for this game.
- Mute the emulator (`re_session.py` does), keep its files in `scratch/`, end it with `re_session.py stop`.

## 1. Capture

```bash
R=tools/verification/analysis/re_session.py
python3 $R capture --work scratch/re/<game> --media path/Game.tzx --model 48K --frames 15000 --port-base 9720
python3 $R capture --work scratch/re/<game> --media path/Game.trd --disk --model PENTAGON ...      # a disk
python3 $R capture --work scratch/re/<game> --snapshot game.z80 --inputs a.txt b.txt ...           # own inputs
```

A tape loads as a user loads it (LOAD "" typed in emulated frames, the tape played), a disk boots. Without input
scripts an explorer finds the keys from the loaded game to play (keys tried from saved states until holding opposite
directions moves something on screen), then the recording plays those keys and seeded random keyboard and Kempston
play. Input scripts (`FRAME KEYS...`, `+N`, `FRAME -`, `FRAME END`: keys A-Z 0-9 ENTER SPACE CS SS, Kempston KF KL KR
KU KD) replace the exploration. `--ranges` (default `5B00-FFFF`) is the program range of the exports and the gates.

The script writes into the work folder: `start.szx` and `start.bin` (the state and the 64 K Z80 view the recording
starts from: the draft decodes from it and the rebuild gate compares with it), `screens/` (a screen every 250
frames), `screens.png` (eight of them on one sheet), `capture.json` (the load, the keys the explorer found, the
build status), `l1.json`, `review.txt` (the first queue page), `findings.txt` (counts by kind and status) and
`session.json`. The instance stays up: its MCP endpoint is `http://127.0.0.1:<port-base + 200>/mcp`.

**Look at `screens.png` before anything else.** The analysis knows only what the recording ran: a session that sat
in a menu, a name prompt or the attract mode classifies the game's code as data. When the sheet shows no play, stop
the instance and capture again with input scripts that get past that screen (a few lines: `100 N`, `+8 -`, ...).

With the MCP endpoint connected to the agent, call `control_execution {"action":"take_control"}` once, then
`analysis` directly. From a shell, every call goes through the script (it takes control first):

```bash
python3 $R call --work W review '{"op":"next"}'
python3 $R call --work W explain '{"address":"$8A3F","what":"routine"}'
```

## 2. Coverage

```text
analysis {"action":"cover"}                          # budget 64 experiments, 8 per branch, 50 frames each
analysis {"action":"findings","kind":"coverage"}
```

Directed coverage holds the byte a one-way branch tests, or presses the keys of the port it reads, and records each
flip as a `coverage` finding and a draft note ("needs poke $8100=$05"). Run it once, after the capture; when
`findings kind=coverage` lists flips, it ran. Code no run reached stays `unknown` or data: a second capture with
input scripts for those parts (a later level, a menu option) is cheaper than reading the code to classify it.

## 3. Review

```text
analysis {"action":"findings","limit":20}            # counts by kind and status, the first rows
analysis {"action":"review","op":"list"}             # the first 40 items: id, reason, range, claim
analysis {"action":"review","op":"list","reason":"name","limit":100}
analysis {"action":"review"}                         # op next: the next open item and its fact sheet
```

Reasons, in queue order: `bug` (a candidate bug rule fired), `conflict` (two specific claims overlap),
`unnamed-hot` (a routine with a big time share and no name), `low-confidence` (a specific claim below 700),
`unknown` (more than 16 bytes nothing claimed), `entry` (a jump target that may start a routine of its own), `name`
(a batch of up to 10 draft labels like `sub_8A3F`, `var_8068`, `L8A50`). In `list`, an open item is `queued`, or
`shown` once `next` / `show` returned it: filter on "not resolved", not on `queued`.

The sheet: the finding, its evidence, the first 32 bytes (as `text` too when most of them print), a 4-line
disassembly or the routine's L1 fact sheet, and the resolve line to copy. Decide and resolve:

```text
analysis {"action":"review","op":"resolve","id":"F57","verdict":"ok","label":"DrawSprite",
          "title":"Draw a 16x16 masked sprite","comment":"HL: sprite, DE: screen address."}
analysis {"action":"review","op":"resolve","id":"F61","verdict":"wrong","comment":"Not a table: a code fragment."}
analysis {"action":"review","op":"resolve","id":"F88","verdict":"wrong","kind":"text","label":"LessonNames"}
analysis {"action":"review","op":"resolve","id":"F90","verdict":"ok","kind":"music","label":"ThemeTune"}   # unknown
analysis {"action":"review","op":"resolve","id":"F63","verdict":"skip","comment":"needs a later level"}
```

- `verdict`: `ok` (confirmed), `wrong` (refuted), `skip` (out of the queue, undecided: the finding stays `inferred`).
- `label`: letters, digits, `_`, not a digit first, at most 32 characters, unique in the draft (a pattern repeated
  per level gets one name each: `Level1Map`, `Level2Map`); it names the entry starting at the finding. `title`
  replaces the entry title; `comment` goes first in the entry description.
- `kind` says what the range is when the claim's kind is wrong: `code`, `data`, `text`, `font`, `sprite`, `tile`,
  `screen`, `variable`, `pointer_table`, `handler_table`, `rng`, `music`, `packed`, `level_map`, `unused`. On a
  claim it goes with `wrong` (`ok` keeps the claim and its fields); on an `unknown` item, with `ok`. The export makes
  the range one entry of that kind (code decoded from the start image).
- Per reason:
  - `bug`: does the game really do it? An experiment usually answers (below).
  - `conflict`: the sheet names both claims; `wrong` on the one that is not right (resolve each id).
  - `unnamed-hot`: read the routine sheet (inputs, outputs, loops, callers); a label and a title.
  - `low-confidence`: `ok` when the sheet shows the kind, else `wrong` plus a comment or a `kind`.
  - `unknown`: `explain` the first byte; graphics: `gallery` with the address (it draws only a region a detector
    claimed: for unclaimed bytes, look at the bytes `explain` prints); then a `kind`, or a comment
    (`"leftover source text"`, `"unused buffer"`), or a skip saying what is missing.
  - `entry`: the jump sites and the code there (`not executed` when the replay never ran it). `ok` when it is a
    routine of its own (the draft splits the entry; give a `label`), `wrong` when it is a part of the one before.
  - `name`: up to 10 labels of one class with a few facts each. Name them all in one call:

    ```text
    analysis {"action":"review","op":"resolve","id":"N3","names":{"$8068":"PlayerY",
              "$8A3F":{"label":"MovePlayer","title":"Move the player left or right"}}}
    ```

    The ones left out are skipped, except a subject labelled before (it keeps that label: the reply's `kept:`).
    A batch takes `names`, or `verdict` `skip` for all of them; `ok` / `wrong` are refused. Labels are global: when
    one is refused as in use, the other place already has it, so pick a more specific one (`DrawCharacterSprite`,
    not a second `DrawSprite`).
  - `conflict` with both claims true (a small record inside a page-strided table, a text inside a buffer): `ok` on
    both. With both false: `wrong` on both, a `kind` on one.
- `records` is a structure: "One record: N fields the same routines use together" says these variables are one
  record; `ok` keeps it and its field list, `wrong` with a `kind` only when they are unrelated.
- A resolve may be repeated: the last label, title or kind wins; a batch keeps the names it gave unless new ones come.

### Experiments

An experiment runs on a muted clone from the recording, never on the live instance: poke bytes, set registers, press
keys, run N frames, check. With `finding`, the result marks it `observed` (every check held) or `refuted`; a run
that only explores goes without it.

```text
analysis {"action":"experiment","finding":"F12","spec":{
  "poke":{"$8100":5}, "frames":50,
  "watch":[{"pc":"$8130","as":"rare"},{"write":"$8101","as":"w"}],
  "expect":{"rare":"reached","w":">=1","mem:$8101":"$AA"}}}
analysis {"action":"experiment","spec":[ {...}, {...} ]}     # several at once, each on its own clone
analysis {"action":"review","op":"resolve","id":"F12","verdict":"ok","experiment":{...}}   # run, record, decide
```

Spec fields: `from` (`start` default, `end`, `frame:N`), `poke` {address: byte}, `regs` {a, f, b, c, d, e, h, l, af,
bc, de, hl, ix, iy, sp, pc}, `input` [{frame, key | joystick | release_all, up}], `frames` (default 50, at most 5000),
`watch` [{pc | write | read, as}], `expect` {name: check}. Checks: a watch `reached`, `not_reached`, `>=N`, `==N`;
`mem:<address>` `N`, `!=N`, `>=N`, `<=N`, `changed`, `unchanged`; `screen` `changed` / `unchanged`; `new_code` `>0`.
Keys: `a`-`z`, `0`-`9`, `enter`, `space`, `caps`, `sym`. Joystick: `up`, `down`, `left`, `right`, `fire`.

## 4. Export

```bash
python3 $R export --work W
```

Into `W/draft/`, all over the session's ranges:

| File | What it is |
| --- | --- |
| `draft.skool` | the SkoolKit skool: entries by kind, `@label`, titles and comments from the review, `@isub` / `@keep` / ALIGN for relocation |
| `draft.ctl` | the same plan as a control file (`sna2skool.py -c draft.ctl start.szx` rebuilds the skool) |
| `draft.map.txt` | the claims, one line each (range, kind, detector, confidence, status, title) |
| `draft.fields.json` | the record field lists |
| `draft.geometry.json` | the graphics geometry (width, height, frames, stride, mask, layout) |
| `draft.symbols` | the labels for the debugger (`symbols` import) |
| `draft.layout.mk` | the region bounds of the relocatable part |
| `findings.md`, `findings.jsonl` | the findings register with evidence and status |

## 5. Gates

```bash
python3 $R check --work W --venv <SkoolKit 9+ virtualenv> --sjasmplus <sjasmplus> --reference ref.skool
```

- `rebuild-plain`: `skool2bin.py` of the draft equals `start.bin` over every byte of the ranges. A difference names
  its address: a hand-typed instruction, a kind that turned data into code that does not decode the same.
- `assemble`: `skool2asm.py -H -c` (the `@isub` operands applied; skool2bin cannot resolve labels in them, so this
  gate checks them) through sjasmplus (`DEVICE ZXSPECTRUM48`, `SAVEBIN` of the ranges) equals the same bytes. Its
  `skool2asm_warnings` count the operands left as numbers that look like addresses in the program: the relocation
  work still open. A `Label not found` from sjasmplus with no wrong operand in sight is skool2asm stopping early:
  run it by hand and read its first `ERROR`.
- `score` (with `--reference`): `score_draft.py` against the reference disassembly (byte categories by entry and by
  line, code entry starts, graphics regions and their geometry, record fields, names), in `W/check.json`. The
  reference must be of the same release as the medium.
- Names: `exact` and `normalized` pairs count by themselves; the rest (`judge`) need a judge. After the last naming
  resolve, `score_draft.py ... --names-pairs pairs.tsv` writes them, the judge fills the `judge` column (`same`,
  `close`, `different`, `wrong`) and `--names-judged pairs.tsv` adds `same with the judge`. The judge reads the
  reference, so nobody names anything in that session after it.

`check` exits non-zero when a gate fails. Fix through the review (a `kind`, a `wrong`), export again, check again.

## 6. Report

Per game: the gate lines, the score's headline numbers, the queue's counts (decided / skipped by reason), the token
ledger (`{"agents": [{"id", "group", "model", "tokens", "tool_calls", "wall_s"}]}`) and the limits met: what no run
reached, claims skipped and why.

## WebAPI

The same actions; GET takes the options in the query, POST in the body (`spec`, `experiment` and `names` as JSON).

```bash
BASE=http://localhost:<port>/api/v1/emulator/$EMU_ID/analysis
curl -s "$BASE/findings?kind=bug"                                   | jq '.findings[] | {id, start, subject, claim}'
curl -s "$BASE/review?op=list"                                      | jq '.items[] | {id, reason}'
curl -s -X POST "$BASE/review" -H 'Content-Type: application/json' -d '{"op":"next"}' | jq -r .text
curl -s -X POST "$BASE/review" -H 'Content-Type: application/json' \
     -d '{"op":"resolve","id":"N3","names":{"$8068":"PlayerY"}}' | jq -r .text
```

Errors: 409 before a build or for a label in use, 404 for an unknown finding or batch id, 400 for a bad option, spec
or names (the message names the field and what it takes).

## Pitfalls

- **Naming batch ids are per build.** After a `build` list the batches again (given names stay, by address).
- **A rebuild renumbers the findings.** Decisions are kept by the finding's key (kind, range, subject, source), but
  `F57` may name another finding afterwards: list the queue again after a `build`.
- **Experiments start at the recording's first keyframe** unless `from` says otherwise; a poke into a buffer the game
  rebuilds at a level start needs `from` after that point, or a held value (coverage holds bytes for you).
- **The draft decodes from the start image**, the memory when the recording began: variables hold the values of that
  moment, not of the tape. The rebuild gate compares with the same image; a reference made from the tape's load
  point differs in those bytes, which the score does not count (it compares categories, boundaries and names).
- **The draft carries decisions, not the queue.** Export after the last resolve.
- **`show` does not say an item is resolved.** Before reviewing ids handed out earlier, `list` them and keep only
  those not `resolved`.
- **A `#` in a title or comment is fine.** The export writes it so that no SkoolKit writer reads it as a macro.
- **One instance per work folder.** `capture` launches its own on `--port-base` (+100 CLI, +200 MCP, ...); pick free
  ports when other runs are up, and `stop` it when done.
