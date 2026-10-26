# Recipe: Reviewing an analysis-layers draft (review-only workflow)

Goal: turn the automatic disassembly draft of a game into a reviewed draft that builds byte-identical and
relocatable in a zx-reaper project, by deciding only what the emulator could not decide. Read nothing else first:
this recipe is the whole workflow for an agent that has never seen the analysis code.

The emulator does the analysis itself (module doc: [analysis-layers.md](../../docs/emulator/analysis/analysis-layers.md)):

| Layer | What it gives you | Action |
| --- | --- | --- |
| L0 | per-byte access facts (code, operand, data, stack, who read / wrote it, when) | `explain` (a byte) |
| L1 | routines, call graph, loops, record fields, data flow, from a TTD recording replayed on a muted clone | `build`, `explain` (a routine) |
| L2 | regions with a kind (code, text, sprite, tile, records, pointer table, ...), confidence 0..1000, the draft export | `query`, `export`, `gallery` |
| L3 | the findings register, the review queue, experiments, directed coverage | `findings`, `review`, `experiment`, `cover` |

Everything goes through **one MCP tool, `analysis`**, with an `action`. The same actions exist on the CLI
(`analysis review next`), Lua / Python (`emu.analysis("review", ...)`) and REST (`/api/v1/emulator/{id}/analysis/...`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) only inside host-side
> scripts (policy: [_common/transports.md](../_common/transports.md)).

## Budget and rules

- The queue holds only what needs a decision (candidate bugs, conflicts, unnamed hot routines, low-confidence
  claims, unknown areas over 16 bytes, entry candidates, then naming batches), at most 40 items plus one summary
  line; `offset` pages on, `reason` picks one reason. Work the queue, not the draft.
- **Never print a whole draft, map or findings file.** Each review item comes with a fact sheet under 1 KB; ask for
  more with `explain` (one address) or `gallery` (one picture).
- **Do not edit the draft by hand.** Every decision goes through `review resolve`; the export carries it (labels,
  entry titles, comments). A hand edit is lost at the next export.
- One decision per item. If you cannot tell from the sheet, run one experiment, then decide. For findings (`ok` /
  `wrong`), skip rather than guess. For names the rule is the other way round: name every subject of a batch, as a
  human reverser would, from what the facts show (`DrawGuardians`, `AirSupply`, `TitleTuneData`); a name that says
  what the code does or what the data holds beats an auto label. Skip a subject only when its facts are empty.
- Mute the emulator you start (`PUT /audio/mixer/master {"muted": true}`).
- Call `control_execution {"action":"take_control"}` once before the first `analysis` call: every POST action
  (review resolutions, exports and pictures written to a path, experiments, coverage) counts as changing the
  emulator, and without a session each one answers "session opened implicitly". The session belongs to the MCP
  connection: keep one client (one process) for the whole review, or take control again in every new connection.
- A `resolve` may be repeated on an item already decided (a better label, more names for a batch): the last label
  wins, a batch's names add up.

## MCP (preferred)

### 1. Session and build (skip when the caller hands you a built instance)

```text
emulator_manage   {"action":"create","model":"48K"}
invoke_api        {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/master","body":{"muted":true}}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/snapshot/load","body":{"path":"/abs/game.z80"}}
analysis          {"action":"start","record":true}
#   play the game: type_input / joystick_input + control_execution run_frames
analysis          {"action":"build"}                 # waits up to 20 s; state "done" when L1 + L2 are ready
analysis          {"action":"status"}
```

`tools/verification/analysis/mm_session.py --l3` does all of step 1 from a snapshot and zx-reaper input scripts,
plus steps 2 and 4, and writes the drafts: use it when you have the scripts.

### 2. Look at the register and the queue once

```text
analysis {"action":"findings","limit":20}            # counts by kind and status, the first rows
analysis {"action":"review","op":"list"}             # the first 40 items: id, reason, range, claim (one line each)
analysis {"action":"review","op":"list","offset":40}  # the next 40
analysis {"action":"review","op":"list","reason":"name","limit":100}   # one reason only
```

Reasons, in queue order: `bug` (a candidate bug rule fired), `conflict` (two specific claims overlap),
`unnamed-hot` (a routine with a big time share and no name), `low-confidence` (a specific claim below 700),
`unknown` (more than 16 bytes nothing claimed), `entry` (a jump target that may start a routine of its own), `name`
(a naming batch: up to 10 draft labels like `sub_8A3F`, `var_8068`, `L8A50` that still need a name). `by_reason` in
the answer counts the whole queue. Statuses: `observed` (seen in a run or experiment), `inferred`
(read from code or a rule), `confirmed` (a reviewer said ok), `refuted` (shown wrong; kept). In `list`, an open item is `queued`, or `shown` once `next` / `show`
returned it: both still need a decision, so filter on "not resolved", not on `queued`.

### 3. Work the queue

```text
analysis {"action":"review"}                         # op next: the next open item and its fact sheet
```

The sheet: the finding, its evidence, the first 16 bytes, a 4-line disassembly or the routine's L1 fact sheet, and
the resolve line to copy. Decide and resolve:

```text
analysis {"action":"review","op":"resolve","id":"F57","verdict":"ok","label":"DrawSprite",
          "title":"Draw a 16x16 masked sprite","comment":"HL: sprite, DE: screen address."}
analysis {"action":"review","op":"resolve","id":"F61","verdict":"wrong","comment":"Not a table: a code fragment."}
analysis {"action":"review","op":"resolve","id":"F63","verdict":"skip"}
```

- `verdict`: `ok` (the finding is confirmed), `wrong` (refuted), `skip` (out of the queue, undecided).
- `label`: an identifier (letters, digits, `_`; not a digit first; at most 32 characters), unique in the draft; it
  names the entry starting at the finding (or the point inside an entry). `title` replaces the entry title; `comment`
  goes first in the entry description. A resolve without a verdict, label, title, comment, kind or experiment is
  refused.
- `kind`: when the claim's kind is wrong, say what the range is: `code`, `data`, `text`, `font`, `sprite`, `tile`,
  `screen`, `variable`, `pointer_table`, `handler_table`, `rng`, `music`, `packed`, `level_map`, `unused`. The export
  makes the range one entry of that kind (code decoded from the start image) and drops the old claim's geometry and
  fields. On a claim it goes with `wrong` (`ok` with another kind is refused: `ok` keeps the claim, its fields too);
  on an `unknown` item, with `ok`. A kind without a verdict decides the item the same way:

  ```text
  analysis {"action":"review","op":"resolve","id":"F88","verdict":"wrong","kind":"text","label":"CavernName"}
  ```
- Per reason:
  - `unnamed-hot`: read the routine sheet (inputs, outputs, loops, callers). Give a label and a title.
  - `conflict`: the sheet names both claims. `wrong` on the one that is not right (resolve each id).
  - `low-confidence`: `ok` when the sheet shows the kind, else `wrong` plus a comment with what it is.
  - `unknown`: `explain` the first byte; if it is graphics, `gallery` with the address; then a comment
    (`"leftover source text"`, `"unused buffer"`), or skip.
  - `bug`: decide whether the game really does it. A quick experiment usually answers (next section).
  - `entry`: the sheet shows the jump sites and the code there (marked `not executed` when the replay never ran it).
    `ok` when it is a routine of its own (the draft splits the entry there; give a `label`), `wrong` when it is a
    part of the routine before it.
  - `name`: one item, up to 10 draft labels of one class (code or data) with a few facts each (callers, callees, the
    data a routine uses, a 4-line head; reads, writes, values, users and the first bytes of data). Name them all in
    one call (every subject: see the rules above); the ones you leave out are skipped (the reply lists them):

    ```text
    analysis {"action":"review","op":"show","id":"N3"}
    analysis {"action":"review","op":"resolve","id":"N3","names":{"$8068":"WillyY",
              "$8A3F":{"label":"MoveWilly","title":"Move Willy left or right"}}}
    analysis {"action":"review","op":"resolve","id":"N4","verdict":"skip"}     # only when the facts are empty
    ```

The queue's next item moves on after each `review` call; `review {"op":"show","id":"F12"}` shows one again.

### 4. Experiments and coverage

An experiment runs on a muted clone from the recording, never on your instance: poke bytes, set registers, press
keys, run N frames, check. The result marks the finding `observed` (every check held) or `refuted`.

```text
analysis {"action":"experiment","finding":"F12","spec":{
  "poke":{"$8100":5}, "frames":50,
  "watch":[{"pc":"$8130","as":"rare"},{"write":"$8101","as":"w"}],
  "expect":{"rare":"reached","w":">=1","mem:$8101":"$AA"}}}
analysis {"action":"experiment","spec":[ {...}, {...} ]}     # several at once, each on its own clone
analysis {"action":"review","op":"resolve","id":"F12","verdict":"ok","experiment":{
  "poke":{"$8100":5}, "frames":50, "watch":[{"pc":"$8130","as":"rare"}], "expect":{"rare":"reached"}}}
```

`resolve ... experiment` runs the spec (one or an array) on clones like `analysis experiment`, records the result on
the finding (observed / refuted, the result line in its evidence) and applies the rest of the resolve (verdict,
label, ...) in the same call; the answer lists the result line under the decision.

Spec fields: `from` (`start` default, `end`, `frame:N`), `poke` {address: byte}, `regs` {a, f, b, c, d, e, h, l, af,
bc, de, hl, ix, iy, sp, pc}, `input` [{frame, key | joystick | release_all, up}], `frames` (default 50, at most 5000),
`watch` [{pc | write | read, as}], `expect` {name: check}. Checks: a watch `reached`, `not_reached`, `>=N`, `==N`;
`mem:<address>` `N`, `!=N`, `>=N`, `<=N`, `changed`, `unchanged`; `screen` `changed` / `unchanged`; `new_code` `>0`.
Key names: `a`-`z`, `0`-`9`, `enter`, `space`, `caps`, `sym`. Joystick: `up`, `down`, `left`, `right`, `fire`.

Directed coverage tries to take the branches the session took one way only (by holding the byte a branch tests, or
pressing the keys of the port it reads) and records each flip as a `coverage` finding and a draft note
("needs poke $8100=$05"):

```text
analysis {"action":"cover"}                          # budget 64 experiments, 8 per branch, 50 frames each
analysis {"action":"findings","kind":"coverage"}
```

Run it once, after the first pass over the queue; new code it reached shows up in the next `review list`.

### 5. Export and finish in zx-reaper

Export every format, all with the same `ranges` (the draft directory `reaper_gate.py` and `run_ab.py` read):

| `format` | File | What it is |
| --- | --- | --- |
| `skool` | `draft.skool` | the SkoolKit skool draft (relocatable: `@label`, `@isub`, `@keep`, ALIGN) |
| `ctl` | `draft.ctl` | the same plan as a control file (`sna2skool.py -c`) |
| `layout` | `draft.layout.mk` | zx-reaper's `REGION_START` / `SHIFT_END` |
| `fields` | `draft.fields.json` | the record field lists |
| `geometry` | `draft.geometry.json` | the graphics geometry |
| `symbols` | `draft.symbols` | the labels through a symbol codec (skoolkit-ctl) |
| `map` | `draft.map.txt` | the claims, one line each (range, kind, detector, confidence, status, title) |

```text
analysis {"action":"findings","format":"markdown","path":"/abs/project/re/findings.md"}   # zx-reaper's findings table
analysis {"action":"export","format":"skool","path":"/abs/draft/draft.skool","ranges":"8000-FFFF"}
analysis {"action":"export","format":"ctl","path":"/abs/draft/draft.ctl","ranges":"8000-FFFF"}
analysis {"action":"export","format":"layout","path":"/abs/draft/draft.layout.mk","ranges":"8000-FFFF"}
analysis {"action":"export","format":"fields","path":"/abs/draft/draft.fields.json","ranges":"8000-FFFF"}
analysis {"action":"export","format":"geometry","path":"/abs/draft/draft.geometry.json","ranges":"8000-FFFF"}
analysis {"action":"export","format":"symbols","path":"/abs/draft/draft.symbols","ranges":"8000-FFFF"}
analysis {"action":"export","format":"map","path":"/abs/draft/draft.map.txt","ranges":"8000-FFFF"}
```

Then build, test and shift-test in a fresh zx-reaper project. `reaper_gate.py` does exactly these steps, with no
hand fixes, and scores the draft:

```bash
python3 tools/verification/analysis/reaper_gate.py --reaper /path/to/zx-reaper --tape original/game.tap \
    --tests tests --draft /abs/draft --work scratch/<task>/gate --reference mm.skool \
    --expected tools/verification/analysis/expected/manic-miner-records.json --venv <skoolkit venv> --bin <sjasmplus dir>
#   1. zxinit.py game.tap project; 2. tests/inputs + expected hashes; 3. zxq.py split draft.skool re/skool;
#   4. re/layout.mk takes REGION_START / SHIFT_END; 5. make (IDENTICAL), make test, make shift-test SHIFT=0x100 / 0x101
```

By hand in the project: `tools/zxq.py split /abs/draft/draft.skool re/skool`, then `make`, `make test`,
`make shift-test SHIFT=0x100`, `make shift-test SHIFT=0x101`. All four must pass.

### 6. Report the tokens

Write the run's token ledger in run B's format
([run-b-token-ledger.json](../../docs/inprogress/2026-10-24-analysis-layers/baselines/run-b-token-ledger.json)):
`{"run_c": {"agents": [{"id": "R1", "group": "review", "model": "...", "tokens": N, "tool_calls": N, "wall_s": N}]}}`.
`tools/verification/analysis/run_ab.py --draft ... --ledger ...` compares it and the draft's score with run B.

## WebAPI

The same actions; GET takes the options in the query, POST in the body (`spec`, `experiment` and `names` as JSON
objects).

```bash
BASE=http://localhost:8090/api/v1/emulator/$EMU_ID/analysis
curl -s "$BASE/findings?kind=bug"                                   | jq '.findings[] | {id, start, subject, claim}'
curl -s "$BASE/review?op=list"                                      | jq '.items[] | {id, reason}'
curl -s -X POST "$BASE/review" -H 'Content-Type: application/json' -d '{"op":"next"}' | jq -r .text
curl -s -X POST "$BASE/review" -H 'Content-Type: application/json' \
     -d '{"op":"resolve","id":"F57","verdict":"ok","label":"DrawSprite"}' | jq -r .text
curl -s -X POST "$BASE/review" -H 'Content-Type: application/json' \
     -d '{"op":"resolve","id":"N3","names":{"$8068":"WillyY"}}' | jq -r .text
curl -s -X POST "$BASE/experiment" -H 'Content-Type: application/json' \
     -d '{"spec":{"poke":{"$8100":5},"frames":50,"expect":{"mem:$8101":"$AA"}},"finding":"F12"}' | jq -r .text
curl -s -X POST "$BASE/cover" -H 'Content-Type: application/json' -d '{"budget":32}' | jq -r .text
```

Errors: 409 before a build (`build` first) or for a label in use, 404 for an unknown finding or batch id, 400 for a
bad option, spec or names (the message names the field and what it takes).

## Pitfalls

- **Naming batch ids are per build.** `N3` comes from the draft without your notes, so it stays while you resolve;
  after a `build` list the batches again (the names you gave stay, by address).
- **A rebuild renumbers the findings.** Decisions are kept by the finding's key (kind, range, subject, source), so
  they survive, but `F57` may name another finding afterwards: list the queue again after a `build`.
- **Experiments and coverage answer when done** (no job id). A batch of 64 short experiments on a 48K game takes
  seconds; keep `frames` small (the branch decides early in most loops).
- **Experiments start at the recording's first keyframe** unless `from` says otherwise; a poke into a buffer the game
  rebuilds at a level start needs `from` after that point, or a held value (coverage holds bytes for you).
- **`skip` is not a verdict.** The finding stays `inferred` and leaves the queue only; the draft does not change.
- **The draft carries decisions, not the queue.** Export after the last resolve.
