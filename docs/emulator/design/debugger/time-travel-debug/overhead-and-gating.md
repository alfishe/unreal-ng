# TTD Overhead Budget & Runtime Feature Gating

| | |
|---|---|
| **Status** | Draft for review (estimates; to be replaced by measured numbers from `ttd_capture_benchmark`) |
| **Version** | 1.0 |
| **Last updated** | 2026-07-19 |
| **Parent TDD** | [time-travel-debugging-tdd.md](./time-travel-debugging-tdd.md) §6–§9, §14 |

This document quantifies what TTD costs — CPU during capture, CPU during playback/search, and memory — and specifies **which parts can be switched off at runtime** when their capability isn't needed. All figures are engineering estimates for the reference workload; the Phase-1 benchmark suite replaces them with measurements, and this document is the ledger those measurements get written back into.

---

## 1. Reference Workload Assumptions

| Parameter | Value | Source |
|---|---|---|
| Machine | Pentagon 128, 3.5 MHz, 50 fps | primary target |
| T-states / frame | 71,680 | `CONFIG::frame` |
| Instructions / frame | ~15–18 K (avg ~4.5 t/instr) | typical Z80 mix |
| Memory writes / frame | ~2–6 K typical; 15 K pathological (LDIR-heavy clears) | demo profiling experience |
| Port OUTs / frame | ~10–500 (border effects push the top) | |
| Dirty 16 KB pages / frame | 2–6 typical; 8 (all) pathological | working set + screen |
| Host | modern x86/ARM desktop; emulator core runs ~100–500× realtime unthrottled | |

An important baseline: **TTD requires `debugmode` + the debug memory interface** (parent TDD §7.3). The debug read/write path already costs more than the fast path (tracker checks, breakpoint checks). That cost is attributed to *debug mode*, not TTD — but Section 5 revisits it, because for the gamer-rewind scenario it dominates.

---

## 2. CPU Overhead — Capture (recording while running)

Per-hook costs, per event and aggregated per frame (20 ms budget):

| # | Cost center | Trigger rate | Cost / event | Cost / frame | % of 20 ms frame |
|---|---|---|---|---|---|
| C1 | Dirty-bit set (`MarkDirty`: table lookup + OR) | every RAM write | ~1–2 ns | 4–12 µs | ~0.05% |
| C2 | Write-journal append (12 B packed store + ptr bump) | every RAM write (journal on) | ~3–5 ns | 10–30 µs | ~0.1% |
| C3 | OUT-journal append | every port OUT (journal on) | ~3–5 ns | <3 µs | negl. |
| C4 | Input-journal append | per host key event | ~50 ns | negl. | negl. |
| C5 | Frame checkpoint: dirty collect + page memcpy (2–6 × 16 KB) | 1 / frame | 30–100 µs | 30–100 µs | 0.15–0.5% |
| C6 | Frame checkpoint: CPU/chipset/peripheral serialize (<4 KB) | 1 / frame | 5–15 µs | 5–15 µs | <0.1% |
| C7 | Page-store bookkeeping (refcounts, thinning amortized) | 1 / frame | 5–20 µs | 5–20 µs | <0.1% |
| C8 | UI timeline-summary update (aggregates for lanes) | 1 / frame | 2–5 µs | 2–5 µs | negl. |
| | **Total TTD capture** | | | **~60–180 µs/frame** | **≈ 0.3–1%** |

Pathological worst case (all 8 pages dirty + 15 K writes/frame): C5 → ~250 µs, C1+C2 → ~100 µs; still **< 2%**. The <5% goal in the parent TDD has generous headroom — the real risk is not the steady state but:

- **First checkpoint after touching many new pages** (e.g., depacker unpacks 300 KB on a 512K machine): one-off multi-page intern burst. Mitigation: cap interns per frame and spread the backlog over subsequent frames (pages stay dirty until interned; correctness unaffected, only checkpoint density at that instant).
- **Thinning storms** when the budget is hit: eviction is amortized (release ≤ N checkpoints per frame), never a single long stall.

---

## 3. CPU Overhead — Playback, Seek, Search

These run only on user action, with the emulator paused — they cost latency, not emulation throughput.

| Operation | Mechanism | Estimated latency |
|---|---|---|
| Seek within dense tier (≤10 s back) | restore (page-diff memcpy, usually a few pages) + ≤1 frame silent replay | **1–20 ms** |
| Seek into 1/10 tier | restore + ≤10 frame replay (unpaced, unrendered ≈ 0.2–2 ms/frame) | **10–40 ms** |
| Seek into 1/50 tier | restore + ≤50 frame replay | **30–200 ms** |
| Step back one instruction | intra-frame replay from previous checkpoint | ≤ 20 ms |
| Frame-compare "step both" | 2 seeks in dense tier | 5–40 ms |
| Reverse search, journal path | linear backward scan of packed 12 B records, ~2 GB/s effective | **< 100 ms even for a full 64 MB ring** |
| Reverse search, replay fallback | instrumented silent replay of scanned intervals | **~0.5–2 s per 10 s of history scanned** |
| Timeline UI paint | per-frame aggregates only, O(visible px) | < 1 ms, no journal iteration |

Silent replay throughput is the key playback constant: with rendering and audio suppressed and breakpoints skipped, one emulated frame replays in ~0.2–2 ms depending on ScreenHQ-off batch rendering reuse. The `ttd_seek_benchmark` pins this number.

**Hidden cost to watch:** during any replay the *capture* hooks must be off (parent TDD §8.2) — replay must not re-journal or re-checkpoint. This is a mode flag check, not a measurable cost, but getting it wrong doubles replay time and corrupts the store; the divergence test catches it.

---

## 4. Memory Overhead

| Component | Sizing rule | Typical (128K demo) | Worst case |
|---|---|---|---|
| Page store (COW) | dirty-page interns, session-scoped; never-touched pages free | 2–6 pages/frame dense + thinned tiers ≈ **30–60 MB** at default budget | hard-capped at `budget_mb` (64) |
| Checkpoint fixed records | <2.5 KB × retained checkpoints (~1,000) | ~2.5 MB | ~3 MB |
| Write journal ring | `journal_mb` knob | 64 MB (default) | capped |
| Input journal | ~8 B/event, session-scoped | KBs | ~1 MB (pathological mashing) |
| Timeline index + UI summaries | ~50 B/frame retained | ~1 MB | ~2 MB |
| Thumbnails (UX opt-in, §UX-12.1) | ~2–6 KB per bookmark/anchor | ~1 MB | bounded by anchors |
| **Total, defaults** | | **~100–130 MB** | **~135 MB hard-capped** |

Key scaling properties (established in the parent TDD):

- **Scales with working set, not installed RAM** — never-touched pages cost zero; 512K Pentagon running 128K software = 128K cost.
- Everything large is a **ring or a budgeted pool** — memory use is a configuration decision, not a workload surprise. Halving `budget_mb`+`journal_mb` halves footprint and shortens history/search reach proportionally, with no other behavior change.
- A "lean" preset (`budget_mb=16, journal_mb=8`) still gives ~1–3 minutes of frame-accurate history and in-journal search over the last ~10–20 s: **~25 MB total** — viable on modest hosts.

---

## 5. Runtime Gating Matrix

What can be turned off *at runtime* to shed load, what each gate saves, and what capability it costs. Gates are FeatureManager modes on `timetravel` (plus the master flags), all switchable live except where noted.

| Gate | Default | Saves when off | Loses when off | Live-switchable? |
|---|---|---|---|---|
| `timetravel` master | off | everything (all hooks early-return on one cached bool; buffers freed on disable) | all TTD | yes — enable arms at next frame boundary; disable frees stores |
| `journal` (write journal C2+C3, 64 MB) | on | ~0.1% CPU + 64 MB | fast reverse search (falls back to replay scan: seconds instead of <100 ms); journal-fed timeline lanes (paging/io marks, Event Inspector) degrade to aggregates | yes — turning off mid-session keeps old ring readable; turning on starts coverage at "now" (coverage gap surfaced in UI `ⓘ` line) |
| `journal=io_only` mode | — | ~99% of journal traffic and ring churn (OUTs are ~100× rarer than writes) | per-write search (replay fallback only); keeps paging/IO marks — the cheapest way to keep the timeline *informative* | yes |
| `dense_seconds` (every-frame window) | 10 | page-store churn ∝ window; smaller window → more of history in thin tiers | seek latency in recent history rises (1/10 tier: tens of ms — barely noticeable) | yes — applies at next thinning pass |
| `budget_mb` / `journal_mb` | 64 / 64 | RAM, linearly | history depth / search reach | yes — shrink triggers amortized eviction |
| Input journal | always on while recording | ~nothing (it's noise-level) | determinism for interactive workloads — **never worth gating**; listed for completeness | — |
| Thumbnails (UX) | off | ~1 MB + capture blips | video-editor-style ruler previews | yes |
| Timeline UI summaries (C8) | auto | 2–5 µs/frame | live lane updates | automatic: suspended while the timeline panel is closed |
| `memorytracking` / `calltrace` / `opcodeprofiler` | user's choice | their own documented costs | heatmaps, stack view enrichment — **orthogonal to TTD**: TTD neither requires nor pays for them (own dirty tracker, parent TDD §6.2) | yes (existing) |
| Third "TTD-lite" memory interface (future, TDD §7.3) | n/a | the debug-*read*-path overhead — the largest single win for the gamer scenario, since reads outnumber writes ~3:1 | breakpoints while recording (debugger features need the full debug interface) | planned: switch interfaces at frame boundary |

### 5.1 Recommended Presets

| Preset | Settings | Cost | For |
|---|---|---|---|
| **Full debug** (default when TTD enabled) | journal=on, dense=10 s, 64+64 MB | ~1% CPU, ~130 MB | developer, demo-maker |
| **Timeline-only** | journal=io_only, dense=10 s, 64+8 MB | ~0.5% CPU, ~70 MB | scrubbing & frame compare without write-level search |
| **Lean** | journal=off, dense=5 s, 16 MB | ~0.4% CPU, ~20 MB | low-end hosts, long soak sessions |
| **Rewind** (future, needs TTD-lite interface) | journal=off, dense=15 s, 24 MB, breakpoints off | target <2% total incl. interface swap | gamer hold-to-rewind |

### 5.2 Gating Mechanics (implementation rules)

- Every hot-path gate is a **single cached bool** (`_feature_ttd_enabled`, `_ttd_journal_writes`, `_ttd_journal_io`) refreshed via the existing `UpdateFeatureCache` pattern — no FeatureManager lookups on the hot path, mirroring `_feature_memorytracking_enabled`.
- Gate transitions are applied **at frame boundaries only** (the same discipline as `next_z80_frequency_multiplier`): flipping a journal flag mid-frame would split a frame's journal range and complicate the index for zero benefit.
- Disabling never destroys data silently: `timetravel off` prompts if history exists (UX doc §9 invalidation rules); `journal off` retains the ring read-only.
- Every degradation caused by a gate is **surfaced where the user hits it**, not where they set it: the search dialog's coverage line, greyed Event Inspector columns, the timeline density strip. (UX Principle 2.)

---

## 6. Measurement Plan

The estimates above are replaced by CI-tracked numbers from three benchmarks (parent TDD §15.4), each run over the demo corpus and reported per-preset:

1. `ttd_capture_benchmark` — frame time with TTD off / lean / timeline-only / full, isolating C1–C8 via counters. Regression gate: full preset < 5% (goal ~1%).
2. `ttd_seek_benchmark` — latency distribution across tiers; silent-replay frames/sec constant.
3. `ttd_find_last_benchmark` — journal-path scan rate and replay-fallback rate per scanned second.

Additionally a one-off **debug-vs-fast interface microbenchmark** to size the case for the TTD-lite interface (Section 5, last row) with real numbers before committing to that work.
