# 03 — LLM Analysis Loop

| | |
|---|---|
| **Status** | Proposal / draft for review |
| **Date** | 2026-09-21 |
| **Baseline** | master `ae4d40b` |
| **Related** | MCP server (`core/automation/mcp/`), MCP design (tool search meta-tool, recipes with verified/draft statuses), `2026-09-14-automation-triage-gaps/`, `2026-01-14-analyzers/`, doc 02 (triggers), doc 01 (TTD, UNS) |

---

## 1. Summary

Goal: many cycles of LLM work through MCP, with recipes recorded, so that an LLM can find **bugs in the emulator itself** and **dissect demos and games**, producing data for progressively stronger analyzers.

Today the loop is one-directional and interactive: an LLM drives the emulator through MCP tools. This document adds:

1. **Oracles** so that "found a bug" means something.
2. **Batch corpus runs** so that data is collected at scale cheaply, with the LLM spending tokens only on anomalies.
3. A **title knowledge base** as the durable output.
4. **Recipe regression** so that recipes do not rot.
5. **Emulator-initiated tasks**: triggers enqueue work for an LLM (delegate actions, doc 02 §5).
6. **Hypothesis verification through TTD**, turning LLM output from plausible text into checked facts.

---

## 2. Current state (verified)

| Item | Finding |
|---|---|
| MCP server | Own thin implementation; Streamable HTTP with SSE framing and `notifications/progress` (`mcp-sse.h`); modules for analysis, media, resources, symbols |
| Tool discovery | Intent-based `find_capability` meta-tool; recipes with lifecycle (verified first, drafts only when no verified match) — per MCP design |
| Server→client requests | No `sampling/createMessage`, no general server notifications beyond progress |
| Multi-instance | `EmulatorManager` creates instances (`CreateEmulator*`); no "fork from checkpoint" |
| Triage evidence | `2026-09-14-automation-triage-gaps/` documents gaps found by manual LLM triage sessions |
| Analyzers | Analyzer manager + TR-DOS, BASIC, AY log, coverage, audio capture, ROM print; interrupt/routine classifiers are research designs (PLAN T4 #29) |
| Batch mode | Branch `automation-batch-mode`, last activity 2026-08-09 |
| Access provenance | `MemoryAccessTracker` tracks caller addresses per region (aggregated counts) |

---

## 3. Oracles and differential testing

Without ground truth, an LLM will "discover" emulator bugs that are its own misunderstanding of the hardware.

### 3.1 Oracle sources

| Oracle | Coverage | Mechanism |
|---|---|---|
| FPGA RTL (own MiSTer ZX Spectrum fork) | Timing, video, sound at cycle level | Verilator co-simulation (debug_hub infrastructure exists) on identical inputs |
| Reference emulators | Fuse (Sinclair models), Xpeccy (TSConf), CSpect (Next) | Same image + same input journal → compare screen digests / memory at checkpoints |
| Test suites | z80test, fusetest, ULA/contention/floating-bus tests, FDC tests | Pass/fail with expected outputs |
| Real hardware captures | Video/audio recorded from real machines | Frame/audio alignment and comparison |

### 3.2 Requirements

- **O-1** A differential harness: `run(image, journal, machine) → {frame digests, memory hashes at sample points, audio hash}` for unreal-ng and each oracle adapter.
- **O-2** First-divergence localization: bisect over frames, then T-states, using TTD on the unreal-ng side.
- **O-3** A finding is classified as `emulator_bug` only when it diverges from an oracle and reproduces. Otherwise it stays a `hypothesis`.
- **O-4** Every confirmed finding produces a regression fixture: UNS + journal + expected digests.

---

## 4. Batch corpus runner

Interactive MCP sessions are expensive. Data for analyzers should come from bulk runs.

- **B-1** Headless, parallel, maximum speed; one process per core; deterministic.
- **B-2** Corpora: game archives, demo archives (ZXArt, Pouet), TR-DOS collections, per-machine software (ATM, Evo, TSConf, Next).
- **B-3** Cheap deterministic collectors per run:
  - loader and packer detection;
  - music-player detection;
  - memory map and paging usage;
  - coverage;
  - interrupt mode and timing;
  - video modes used;
  - crash/hang detection;
  - oracle-divergence sampling.
- **B-4** Output: per-title records into the knowledge base (§5) plus an anomaly queue.
- **B-5** The LLM is invoked only for anomalies and unexplained patterns, via the task queue (§7).
- **B-6** Revive or replace `automation-batch-mode` on top of UNS fixtures and the trigger engine.

---

## 5. Title knowledge base

### 5.1 Keying

Content-addressed: hash of loaded code (per module/segment), plus image hash, plus machine config. The same routine in many titles (a depacker, a music player) is recognized once and linked everywhere.

### 5.2 Content

| Kind | Examples |
|---|---|
| Identification | Title, version, machine requirements, peripherals used |
| Loading | Loader type, turbo scheme, protection |
| Code | Labeled routines (depackers, players, sprite blitters, text printers, keyboard/joystick readers, main loop), with confidence |
| Data | Variables (lives, score, room, entity tables), graphics banks, fonts, music data |
| Semantics | Sample points, events (death, pickup, level change) — feeds the game manifest (doc 04) |
| Evidence | Links to TTD fixtures, recipes and verifications that produced each fact |

### 5.3 Bootstrap sources

- **POKE databases.** Community cheat lists identify lives/energy/time variables for thousands of titles. Each POKE is a labeled variable and seeds manifests and analyzer training data.
- **Fingerprint libraries** of known routines: depackers (ties into the ZX packers collection idea), PT2/PT3/STC/SQT players, beeper engines, standard turbo loaders.

### 5.4 Status model

`draft` (heuristic or LLM) → `verified` (passed an automated check, §8) → `curated` (human-confirmed). Queries rank verified/curated first, consistent with the existing recipe status model.

---

## 6. Recipes

### 6.1 Definition

A recipe is a reusable procedure for reaching an intent ("find the lives variable", "extract the sprite set", "locate the music player and dump patterns"), expressed as MCP tool calls, triggers and checks.

### 6.2 Requirements

- **R-1 Fixtures.** Each verified recipe ships with a fixture (UNS/TTD recording) and expected outputs.
- **R-2 Regression in CI.** Recipes run against their fixtures on every build; failures mark the recipe `broken` and open an API-gap item.
- **R-3 Automatic gap reports.** Failed recipes, plus intents for which the LLM found no working path, are aggregated into a gap report (today produced manually in `automation-triage-gaps`).
- **R-4 Notebook form.** Recipes are storable as notebooks (see toolchain doc §15): readable by humans, writable and runnable by LLMs.
- **R-5 Evidence links.** Every knowledge-base fact records which recipe run produced it.

---

## 7. Emulator-initiated LLM tasks

### 7.1 Principle

Inversion of control: the emulator (via triggers) enqueues work; an external worker runs the LLM. Triggers fire in nanoseconds, LLMs answer in seconds to minutes, hence:

- **LT-1 Delegate action class** (doc 02 §5): asynchronous, never blocks emulation, never mutates the live instance.
- **LT-2 Fork from checkpoint.** The task carries a TTD bookmark. The worker investigates in a separate instance restored from that checkpoint, free to step, seek and experiment. **New core requirement:** `EmulatorManager::Fork(sourceId, checkpoint) → instance` (cheap with TTD v2).
- **LT-3 Prebuilt dossier.** At fire time, the core and daemon assemble a compact context bundle:
  - registers;
  - disassembly window with labels;
  - reconstructed call stack;
  - relevant memory regions;
  - TTD range;
  - title manifest excerpt;
  - recipe hints.

  The LLM starts with context instead of spending many tool calls to gather it.
- **LT-4 Results are drafts.** All outputs enter the knowledge base as `draft` until verified (§8).

### 7.2 Trigger catalog (initial)

| Trigger | Task |
|---|---|
| First execution of an unknown routine (not in KB by hash) | Name, describe, propose signature |
| Anomaly: jump into screen/attribute memory, stack overflow into code, `RST 38` loop, write to ROM area, FDC/port deadlock, watchdog | Explain, with TTD fixture attached |
| Oracle divergence | Draft emulator bug report: minimal range, diffs, hypothesis |
| ZXDLSS discrepancy (object on ZX screen not in manifest) | Extend manifest |
| New title loaded | Initial dossier: machine, loader, packer, player, variable candidates (from POKE DB) |
| Batch-run anomaly | Same as above, queued with priority by confidence |

### 7.3 Operational requirements

- **OP-1 Dedup and budget.** Fold by signature (PC, condition, context hash); debounce; per-session and per-day cost caps.
- **OP-2 Tiered models.** A small local model on homelab inference hardware for classification and triage; a large model for deep investigation. User accept/reject decisions become a labeled dataset for fine-tuning the small model.
- **OP-3 Transport.** Task queue in the core (or daemon), consumed by an external worker (subscription via event stream). MCP `sampling` is optional for clients that support it. No API keys inside the emulator.
- **OP-4 Permissions.** Automatic tasks get read-only tools against the fork. Mutating tools only in interactive sessions with a human present. This also contains prompt-injection risk from guest data (strings in memory).
- **OP-5 Determinism untouched.** Labels, manifests and comments are metadata, not machine state.
- **OP-6 UI.** Draft annotations appear in disassembly and on the TTD timeline in a distinct style, with one-click accept/reject.

---

## 8. Hypothesis verification

TTD is the oracle for the LLM's own claims:

| Hypothesis | Automated check |
|---|---|
| "Address X is the lives counter" | Retroactive query: writes to X correlate with death events; decrement pattern; POKE cross-check |
| "Routine R is a depacker" | Run R on the fork with a captured input buffer; output matches the later in-memory data; compare against fingerprint library |
| "Routine R draws sprites" | R's writes land in screen memory and explain observed screen deltas |
| "Routine R is a music player" | R executes at interrupt rate; AY writes originate from R; pattern data located |
| "This is an emulator bug" | Oracle divergence reproduces (§3) |

- Passed → `verified`. Failed → returned to the LLM with the counterexample.
- The loop is closed: generation → check → feedback, instead of accumulating plausible but unchecked comments.

---

## 9. Analyzer evolution

1. Collect: batch runs + LLM-verified facts populate the knowledge base.
2. Generalize: recurring verified patterns become deterministic analyzers or fingerprint entries (no LLM needed at run time).
3. Redeploy: new analyzers run in batch and interactive modes, finding more candidates.
4. Repeat: the LLM moves to the frontier of unexplained behavior.

Candidate analyzers from existing research designs: interrupt analyzer, routine classifiers, beam-to-execution correlation (PLAN T4 #29).

---

## 10. Phasing

| Phase | Deliverables |
|---|---|
| L0 | Differential harness with test suites and one reference emulator; regression fixtures in UNS |
| L1 | Batch runner (headless, parallel) with basic collectors; knowledge-base schema and storage |
| L2 | Recipe fixtures + CI regression + gap reports |
| L3 | `EmulatorManager::Fork`; dossier builder; task queue + worker protocol; delegate action |
| L4 | Verification checks (§8) and draft/verified promotion; UI for drafts |
| L5 | FPGA RTL co-simulation oracle integration; POKE DB import; fingerprint library |
| L6 | Tiered models and feedback dataset |

---

## 11. Open questions

1. Knowledge-base storage: embedded (SQLite) in the daemon vs. a shared service for multiple machines/workers?
2. Licensing and redistribution of corpora and POKE databases: store references and hashes only, never content?
3. Where does the dossier builder live: core (fast, complete state) or daemon (knows sources and projects)? Likely split: the core produces a machine-state dossier, the daemon enriches it with source/project context.
