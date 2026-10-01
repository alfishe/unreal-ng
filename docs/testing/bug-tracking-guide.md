# Bug tracking: from discovery to a verified fix

This document defines the **methodology**: how a defect travels from first sighting
to a verified, committed fix, and what an AI agent may and may not do at each step.

The **registry** — the live list of bugs and their state — is
[`docs/inprogress/BUGS.md`](../inprogress/BUGS.md). That file holds the state; this
file defines the process. When the two disagree, the developer decides.

Audience: AI agents working in this repository **and** humans. Every stage below
names its decision owner; where the owner is the developer, the agent stops and asks.

### Scope: when a defect belongs here vs. in an `inprogress/` folder

A defect found **while a feature is still active** — its `docs/inprogress/<date>-<name>/`
folder exists and work continues — is **not** a `BUGS.md` entry. It is tracked where the
feature already is: a note in that folder's `TODO.md` / requirements / design, following
that folder's own conventions. Reopening a long-closed `inprogress/` folder for a one-line
defect note is not worth it either way, so the line is drawn by whether the folder is still
live, not by the defect's size.

`BUGS.md` is for defects found in code whose feature shipped and closed — nobody is
actively iterating on it, there is no live folder to add a note to, and the fix stands on
its own. Size does not matter here: a one-line fix in long-closed code is still a `BUGS.md`
entry, exactly like a large one, so the registry stays the single source of truth for
"shipped-and-forgotten" defects however small they are.

## Contents

- [1. Principles](#1-principles)
- [2. Registry format and status lifecycle](#2-registry-format-and-status-lifecycle)
- [3. The pipeline at a glance](#3-the-pipeline-at-a-glance)
- [4. Stage 0 — Discovery](#4-stage-0--discovery)
- [5. Stage 1 — Triage](#5-stage-1--triage)
- [6. Stage 2 — Confirmation](#6-stage-2--confirmation)
- [7. Stage 3 — Reproduction procedure](#7-stage-3--reproduction-procedure)
- [8. Stage 4 — Root cause](#8-stage-4--root-cause)
- [9. Stage 5 — Fix strategy](#9-stage-5--fix-strategy)
- [10. Stage 6 — Proposal](#10-stage-6--proposal)
- [11. Stage 7 — Implementation and the verification ladder](#11-stage-7--implementation-and-the-verification-ladder)
- [12. Stage 8 — Closing](#12-stage-8--closing)
- [13. Agent permissions](#13-agent-permissions)
- [14. Quick checklist](#14-quick-checklist)
- [15. References](#15-references)

## 1. Principles

1. **Evidence over opinion.** Every claim in a bug entry (repro, root cause, "fixed")
   is backed by something another agent can re-run: a command, a test, a trace, a TTD session.
2. **The registry is the single source of state.** Work that is not reflected in
   [`BUGS.md`](../inprogress/BUGS.md) does not exist; work that is finished stays
   recorded until the retention policy wipes it.
3. **Reproduce before analyzing, analyze before fixing.** No fix is written against a
   bug that has no confirmed reproduction procedure.
4. **Root cause, not symptom.** A fix is acceptable only when the mechanism producing
   the wrong behavior is understood and the fix removes that mechanism.
5. **Verification ladder, climbed in order:** isolated test → module tests → full
   suite → benchmark A/B. No level is skipped because a lower one passed.
6. **The agent never commits and never closes.** Committing, pushing, and marking a
   bug fixed are developer-owned actions (see [§13](#13-agent-permissions) and the
   CRITICAL rule in [`AGENTS.md`](../../AGENTS.md)).

## 2. Registry format and status lifecycle

Entry format, numbering, and the retention policy are defined by the annotation block
at the top of [`BUGS.md`](../inprogress/BUGS.md) and are not repeated here.

### Status vocabulary

The entry marker tracks where in the pipeline a bug is. An entry always carries
exactly one status; the status only ever moves forward except by developer decision.

| Marker | Status | Meaning | Who may set it |
|:--|:--|:--|:--|
| 🔴 | `[Open]` | Filed, not yet reproduced by the agent | anyone |
| 🟠 | `[Confirmed]` | Reproduced; reproduction procedure recorded in the entry | agent |
| 🔵 | `[In Progress]` | Root-cause hunt or fix implementation under way | agent (claims the entry) |
| 🟣 | `[Fix Proposed]` | Root cause found, fix strategy + test plan awaiting developer review | agent |
| 🟢 | `[Fixed]` | Fix landed; `Date Fixed` and `Commit ID` filled in | **developer only** |

`Date Fixed` and `Commit ID` are filled **only** from a commit the developer has
actually made and confirmed to the agent. An entry never goes 🟢 on the agent's
own authority, even when every test passes.

### Severity (recommended addition to the entry format)

Set provisionally at triage by the agent, confirmed by the developer:

| Level | Meaning |
|:--|:--|
| S1 | Crash, data corruption, hang of the emulator or a client |
| S2 | Wrong emulation behavior (CPU, memory, timing, video, sound, media) |
| S3 | Tooling/automation defect (WebAPI, MCP, debugger UI) with a workaround |
| S4 | Cosmetic or documentation defect |

## 3. The pipeline at a glance

| Stage | Output | Who decides to advance |
|:--|:--|:--|
| 0 Discovery | Raw report with environment evidence | — |
| 1 Triage | Registry entry, dedupe + classification | developer (accept/defer/reject) |
| 2 Confirmation | Status 🔴 → 🟠, determinism level recorded | agent |
| 3 Reproduction procedure | A cold-runnable procedure in the entry | agent |
| 4 Root cause | Cause statement explaining every symptom | agent (verified by evidence) |
| 5 Fix strategy | Options + recommendation | — |
| 6 Proposal | Status 🔵 → 🟣, developer approval | **developer** |
| 7 Implementation | Fix + tests, ladder fully climbed | **developer** (to commit) |
| 8 Closing | Status 🟢, dates/commit filled, docs updated | **developer** |

## 4. Stage 0 — Discovery

A raw defect report (user observation, failed verification, flaky test, crash log)
is **not** yet a registry entry. Before filing, capture:

- **Build fingerprint:** `GET /api/v1/emulator/status` → `server.git_branch` /
  `server.git_commit` (or the commit the client was built from).
- **Machine model** and relevant config (ROM, port scheme, attached media).
- **Exact steps or materials**: what was loaded/typed/dragged; file paths or
  `.recipe`-style command sequences.
- **Expected vs observed** behavior, with screenshots/logs where they exist.
- **Frequency**: always / intermittent / once.
- **Artifacts** (crash dumps, images, recordings) go to `scratch/` — never the
  project root. Artifacts that must survive the session move to `testdata/` only by
  developer decision.

A report missing the build fingerprint and exact steps is filed with those fields
marked *unknown* and flagged for follow-up — it is not silently dropped.

## 5. Stage 1 — Triage

1. **Deduplicate.** Search [`BUGS.md`](../inprogress/BUGS.md),
   [`PLAN.md`](../inprogress/PLAN.md) rows, `git log --grep`, and
   [`docs/inprogress/`](../inprogress/) folders for the same symptom. A duplicate
   becomes a comment on the existing entry, not a new number.
2. **Classify.** Real defect vs intended machine behavior (check the hardware docs
   under [`docs/hardware/`](../hardware/) — many "bugs" are faithful emulation of
   quirky silicon) vs feature request vs test defect.
3. **Severity + priority.** Provisional severity per [§2](#2-registry-format-and-status-lifecycle);
   priority follows the T1–T4 vocabulary of [`PLAN.md`](../inprogress/PLAN.md).
4. **Link related work.** If the defect touches functionality that is in flight,
   update the corresponding `docs/inprogress/*.md` folder and [`PLAN.md`](../inprogress/PLAN.md)
   (registry rule 3).
5. **Accept / defer / reject is a developer decision.** The agent files and
   classifies; it never rejects a report as "not a bug" or "won't fix" on its own
   authority.

## 6. Stage 2 — Confirmation

The agent attempts to reproduce, in a **fresh emulator instance** (kill stale
processes, verify port 8090 is free — see the WebAPI sequence in
[`AGENTS.md`](../../AGENTS.md)). Check [`.recipe/`](../../.recipe/README.md) first:
media loading, TTD, port tracing and TR-DOS recipes already exist.

- Deterministic symptoms → reproduce once or twice, record the exact sequence.
- Input-dependent symptoms → RZX playback if a recording exists.
- Intermittent symptoms → **time-travel debugging**: start a TTD recording, run until
  the fault, then `find_last` / `reverse_continue` / `seek` to isolate the first
  divergence. TTD evidence (frame/cycle where state goes wrong) is the strongest
  confirmation this project can produce.

Record a **determinism level** in the entry:

| Level | Meaning |
|:--|:--|
| D1 | Reproduces on every run from a cold start |
| D2 | Reproduces only under recorded conditions (TTD/RZX replay, specific media state) — attach the recording |
| D3 | Not reproduced — document every attempt (instance, build, steps) |

D3 entries stay 🔴 `[Open]`. The agent never closes a D3 entry as "cannot reproduce"
— that is a developer decision after reviewing the attempts.

## 7. Stage 3 — Reproduction procedure

Every 🟠 entry must contain a procedure precise enough that **another agent (or the
developer) can run it cold**, without this conversation's context. Required parts:

1. **Setup:** machine model, ROM/config, fresh-instance preamble, media to load.
2. **Steps:** exact commands — WebAPI `curl` calls, MCP tool invocations, or
   keyboard/mouse input sequences. Copy-pasteable, no prose summaries in place of
   commands.
3. **Pass/fail criteria:** expected state vs actual state, and **where to look**
   (memory address, port value, screen region, log line).
4. **Runtime budget:** target under a few minutes; anything slower needs a note.
5. **Artifacts:** paths under `scratch/`, unique per process (TestPathHelper naming
   in C++ tests).

Preference order for the procedure's final form:

1. A **minimal C++ test** in `core/tests/` (the repro becomes executable forever —
   this is the goal whenever the defect is reachable from the core API).
2. A scripted WebAPI/`.recipe` sequence (for UI, media-manager and client-level defects).

## 8. Stage 4 — Root cause

Tools, roughly in order of cost:

- State inspection: `inspect_state`, disassembly with symbols, memory maps,
  [`docs/z80/`](../z80/) and [`docs/hardware/`](../hardware/) as ground truth.
- Port/memory tracing recipes from [`.recipe/`](../../.recipe/README.md).
- TTD reverse execution on a recorded repro — walk back from the fault to the first
  wrong state.
- `git bisect` **only inside a dedicated worktree**, and only after confirming the
  working tree holds no uncommitted work that could be lost (see the git-safety rules
  in [`AGENTS.md`](../../AGENTS.md)).

The **root cause statement** written into the entry must:

- name the mechanism, not the symptom ("the DMA counter is reloaded on every port
  write instead of only on STRB", not "DMA is buggy"),
- explain **every** symptom listed in the entry — a cause that explains only the
  primary symptom is a hypothesis, not a root cause,
- cite the evidence (trace excerpt, TTD seek position, disassembly, hardware doc
  section).

If the cause cannot be found but the bug must be worked around, the workaround is
marked **stopgap** in the entry and the entry stays open at the developer's
discretion — a stopgap is never recorded as 🟢 `[Fixed]` on its own.

## 9. Stage 5 — Fix strategy

For anything beyond a one-line fix, enumerate **at least two** strategies (typical
axis: minimal local patch vs structural fix in the shared infrastructure). For each,
record:

- **Blast radius:** which machines/devices/code paths share the touched code
  (a fix in `core/` usually affects every model — say which ones and why it is safe).
- **Hot-path impact:** if the code is per-instruction / per-memory-access /
  per-port (see the hot-path table in
  [performance-guidelines.md](../guidelines/performance-guidelines.md) §1), the fix
  must follow the zero-cost patterns of §2–§3 there and get an A/B measurement.
- **Constraints:** cross-platform, zero warnings
  ([cross-platform-compatibility.md](../guidelines/cross-platform-compatibility.md));
  test impact (which existing tests pin this behavior).

End with one **recommendation** and its rationale. The recommendation may be the
minimal patch — structural cleanups belong in [`PLAN.md`](../inprogress/PLAN.md),
not smuggled into a bug fix.

## 10. Stage 6 — Proposal

The agent stops coding shared state and presents to the developer:

1. Root cause + evidence (from Stage 4).
2. Strategy options + recommendation (from Stage 5).
3. **Test plan mapped to the ladder** (Stage 7): the new test's name and what it
   pins, the existing suites that must still pass, the benchmark A/B plan if a hot
   path is touched.
4. Risks: known regressions scenarios, machines that need manual sanity checks.

Status moves 🔵 → 🟣 `[Fix Proposed]`. The agent **may** validate the strategy with a
throwaway proof-of-concept in a scratch worktree before proposing — but no
implementation is treated as "the fix" until the developer approves the proposal.

## 11. Stage 7 — Implementation and the verification ladder

Implementation happens **in isolation** — a dedicated git worktree (or a provably
clean working tree), so the fix and its measurements are not contaminated by
unrelated in-flight changes. Test-first where feasible: the new test is written
against the unfixed code and must **fail** before the fix lands.

Then climb the ladder — in this order, no level skipped:

| Level | Scope | How | Gate to pass |
|:--|:--|:--|:--|
| 1. Isolated | the new failing test + tests nearest the touched code | rebuild the test binary explicitly (`ninja -C <build> -j "$JOBS" core-tests` — plain `ninja` leaves a **stale** `bin/core-tests`), then `--gtest_filter` | red → green; no neighbor test broken |
| 2. Module | the owning suite(s) | `--gtest_filter="SuitePrefix*"` for every suite touching the changed code | all pass |
| 3. Full | everything | full build with **zero warnings** (mandatory for `core/` and client C++), then `cmake --build <build> --target test-parallel -- -j "$JOBS"` | zero failures |
| 4. Benchmark | hot paths only | A/B procedure in [performance-guidelines.md](../guidelines/performance-guidelines.md) §4, back-to-back runs, before/after the fix | no regression beyond measured noise |

Non-negotiables while climbing:

- `-j` capped at 50% of logical cores — every build/test command, not just the big ones.
- New tests follow [`core/tests/README.md`](../../core/tests/README.md): no
  `sleep_for` (use `TestWait::*`), under 50 ms per test, `EnableTurboMode()` on
  boot-bound tests, unique scratch paths.
- Test artifacts only in `scratch/`.
- "No regression beyond noise" is judged with the numbers recorded in the entry (or
  the commit message the developer will write), including the honest "no change"
  result.

For defects not reachable from the C++ API (UI, media manager, clients), Level 1 is
the scripted procedure from Stage 3 executed against a fresh instance of the freshly
built binary, plus the client's own checks where they exist.

## 12. Stage 8 — Closing

1. **Developer reviews and commits.** The agent reports ladder results and waits —
   commits are one-time, explicit instructions per [`AGENTS.md`](../../AGENTS.md).
2. The agent fills `Date Fixed` and `Commit ID` **from the developer's actual
   commit**, and the status moves 🟢 `[Fixed]` — this edit happens only after the
   developer confirms the commit exists.
3. Update the related `docs/inprogress/*.md` folders and [`PLAN.md`](../inprogress/PLAN.md)
   rows (registry rule 3): a fixed defect that was tracked as a row or a folder note
   gets its resolution recorded there in the same change.
4. Wiping: per the registry retention policy, closed entries are wiped no later than
   a week after `Date Fixed`. Wiping fully-closed (🟢, commit recorded) entries past
   that deadline is mechanical housekeeping; open entries are never wiped.

## 13. Agent permissions

### The agent may, autonomously

- Investigate freely: read all code and docs, start/stop emulator instances, load
  media, inspect state, record TTD/RZX, run traces — always from
  [`.recipe/`](../../.recipe/README.md) first where a recipe exists.
- File new registry entries and **append** findings, evidence, repro procedures,
  root causes, and proposals to entries it works on.
- Set statuses 🟠 `[Confirmed]`, 🔵 `[In Progress]`, 🟣 `[Fix Proposed]`, and
  provisional severity at triage.
- Write tests and implement the approved fix in an isolated worktree / the working
  tree.
- Run builds, filtered tests, `test-parallel`, and benchmarks — with the `-j` cap.
- Fill `Date Fixed` / `Commit ID` and set 🟢 **after** the developer confirms the
  commit landed (including when the developer explicitly hands over the commit hash).
- Wipe 🟢 entries older than the retention deadline (see [§12](#12-stage-8--closing)).

### The agent must never

- **Commit, push, create PRs/issues, or touch any remote.** Commit permission is
  one-time and explicit per [`AGENTS.md`](../../AGENTS.md) — there is no blanket
  grant, ever.
- **Close a bug on its own authority**: no 🟢 status, no `Date Fixed`, no `Commit ID`,
  and no closing as duplicate / cannot-reproduce (D3) / won't fix / not-a-bug — those
  are all developer decisions; the agent proposes.
- **Delete or rewrite open entries.** Registry history is append-only: new findings
  are added; earlier findings are corrected by a follow-up note, not by silent edits.
- **Change severity or priority unilaterally** once the developer has set them.
- **Skip or bypass the ladder or the mandatory checks** (zero-warning build,
  `test-parallel`, benchmark A/B on hot paths) — "it's a small fix" is not an
  exemption; only the developer may waive a level, explicitly.
- **Run destructive git commands** (`reset --hard`, `clean -fd`, force-push,
  `--no-verify`) or lose uncommitted work in pursuit of a clean tree — isolate in a
  worktree instead.
- **Run unbounded parallel builds/tests** — the `-j` 50% cap applies to every command.
- **Write test artifacts outside `scratch/`** or leak machine-specific absolute paths
  into documentation (run `tools/fix-absolute-paths.py` on doc changes).

## 14. Quick checklist

```
File:      fingerprint + model + steps + expected/actual + frequency → BUGS.md
Triage:    dedupe → classify (real / intended behavior / feature) → severity → link PLAN.md
Confirm:   fresh instance, .recipe first, TTD for intermittent → D1/D2/D3 + status 🟠
Repro:     cold-runnable procedure (commands, criteria, budget, scratch paths) in the entry
Cause:     mechanism + explains every symptom + evidence; stopgaps are marked, not closed
Strategy:  ≥2 options, blast radius, hot-path impact → recommendation
Proposal:  cause + options + ladder-mapped test plan → status 🟣 → WAIT for approval
Implement: isolated worktree, failing test first, fix, then:
             1. isolated test (rebuild core-tests first!)   2. module suites
             3. zero-warning build + test-parallel          4. benchmark A/B if hot
Close:     developer commits → agent fills Date Fixed + Commit ID → 🟢 → update
           inprogress docs + PLAN.md → wipe after ≤1 week
```

## 15. References

- [`docs/inprogress/BUGS.md`](../inprogress/BUGS.md) — the registry (format, numbering, retention)
- [`docs/inprogress/PLAN.md`](../inprogress/PLAN.md) — cumulative plan, T1–T4 priorities
- [`AGENTS.md`](../../AGENTS.md) — project rules: commits, build caps, checks matrix, WebAPI test sequence
- [`core/tests/README.md`](../../core/tests/README.md) — test patterns and non-negotiables
- [`docs/guidelines/performance-guidelines.md`](../guidelines/performance-guidelines.md) — hot paths and the A/B measurement procedure (§4)
- [`docs/guidelines/cross-platform-compatibility.md`](../guidelines/cross-platform-compatibility.md) — zero-warning cross-platform constraints
- [`.recipe/README.md`](../../.recipe/README.md) — MCP/WebAPI automation recipes (media, TTD, tracing, TR-DOS)
