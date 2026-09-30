# `AccessPatternTracer` — design: memory, CPU overhead, storage, TTD integration

- **Date:** 2026-09-29
- **Status:** draft, revision 1
- **Parent:** [tdd.md](tdd.md) §2.4.2 (requirements F26/F27), which introduced
  this component as a font-agnostic shared primitive. This document is
  self-contained on purpose — per F26, `AccessPatternTracer` knows nothing
  about fonts, so its cost model, storage and TTD integration shouldn't be
  buried inside a font-specific document either.
- **Supersedes**: tdd.md revision 2's rough sketch of this component (the
  "a few thousand events" ring buffer, the `std::vector`-based `AccessBurst`,
  and the "depends on a not-yet-built TTD v2 replay API" framing) — all
  three are revised here with concrete numbers and a grounding in mechanisms
  that already exist in this codebase (§5, §7). tdd.md should be read as
  pointing here for §2.4.2's detail once this lands.

## 1. What this component does (recap)

Watches Z80 bus traffic and reports **correlated bursts**: "N bytes read
from an ascending address range, then N bytes written to another address
set, each write a simple function of its paired read." It has two modes:

- **Live** (backs Method C): a `HostBusOverlay`-based observer attached to a
  running emulator instance.
- **Offline** (backs Method D): the same classification logic run against a
  TTD-recorded session instead of a live feed (§7).

It does not know what a "glyph" is — the destination-shape check ("do these
addresses form one ZX character cell") lives in `FontFinder`, supplied as a
predicate (tdd.md §2.4.2's `DestinationShapePredicate`).

## 2. Access volume — what we actually know vs. what we're estimating

The only **measured** number in this codebase for a directly comparable
workload is in
[`ttdwritejournal.h`](../../../core/src/debugger/ttd/ttdwritejournal.h)'s
own doc comment: *"~2300 writes/frame peak (action game), ~130 writes/frame
typical game"* on a 48K-class machine (69888 T-states/frame, 50 fps,
3.5 MHz). That figure is for **writes only** — TTD never journals reads
(they're deterministically reproducible from recorded writes/inputs, so
there's nothing to store), which is exactly why this component needs its
own read-side capture and can't just piggyback entirely on existing TTD
data (§7.1 covers the one place it partially can).

**Reads are not measured anywhere in this codebase today.** A rough
extrapolation, stated explicitly as an estimate: 3.5 MHz ÷ ~4 T-states
average per instruction ≈ 875,000 instructions/sec ≈ 17,500
instructions/frame; each instruction performs on the order of 2-4 bus
accesses (opcode/operand fetch bytes plus any data read/write), giving a
back-of-envelope **45,000-70,000 total memory bus accesses/frame** if
observed across the full 64 KB space, of which the known ~130-2300 are
writes and the rest reads. **This number is not verified and must be
confirmed by benchmark (§8, B2) before it is used to justify any design
decision** — it exists here only to size the two storage tiers (§4) and the
CPU-overhead estimate (§3) with *something* concrete rather than a guess
with no basis at all.

## 3. CPU overhead — cost model and estimate

Two distinct costs, per `hostbusoverlay.h`'s own contract:

1. **Global indirection tax**: once *any* `HostBusOverlay` is installed on
   an emulator instance, the Z80 core routes every memory access — not only
   ones inside the overlay's window — through the overlay-aware interface
   (`Memory::MemoryReadOverlay`/`MemoryWriteOverlay`) instead of the plain
   fast path, which does one window-bounds check per access before falling
   through unchanged for out-of-window addresses. This tax is paid on
   **every** access in the whole address space while *any* overlay is
   installed (this component's or another device's), regardless of how
   narrow this component's own window is.
2. **In-window capture cost**: for accesses that do fall in the window,
   push an `AccessEvent` into tier 1 (§4) and run one step of the
   incremental burst classifier (a handful of branches — is this
   continuing a read-run, does it start a write-run, does §4's gap
   tolerance still hold).

Using §2's estimate (45,000-70,000 accesses/frame) and assuming a
full-64 KB window (the worst case — §3.1 below covers the cheaper scoped
case) at a generous 3-8 ns per in-window classifier step: **roughly
0.15-0.5 ms of added CPU work per 20 ms (48K) frame, i.e. under ~2.5% of
the frame budget** in the worst case. This is a **planning estimate only**;
§8 (B1-B3) is the actual measurement this design is gated on before any
default is chosen, per this project's standing rule that a change touching
a hot-path-adjacent interface needs a real A/B number, not a guess (a rule
this document is deliberately following rather than skipping, since
`AccessPatternTracer` sits directly on the memory access path when active).

### 3.1. Two window strategies

- **Full address space (0x0000-0xFFFF)**: catches a table anywhere with no
  scoping guess, at the full cost above. Reasonable as an explicit,
  short-lived, user-requested session (mirrors how TTD recording and
  `MemoryAccessTracker`'s own tracking are already opt-in, non-free-while-on
  features in this codebase) — not something left running by default.
- **Scoped dual-window**: a destination window (screen bitmap, ~6912 bytes)
  plus a source window (a caller-supplied candidate range, typically from a
  prior Method A/B scan per F25's "start cheap, escalate" flow). This cuts
  the **in-window** cost roughly in proportion to window size, but **not**
  the global indirection tax from item 1 above — that tax is a property of
  "is any overlay installed at all", not of window size. Recommended as the
  default *combined* flow (run Method A/B first, then scope Method C to
  its top candidates) rather than defaulting Method C itself to a narrow
  window nobody chose.

## 4. Storage — two tiers, both small and bounded

tdd.md revision 2 guessed "a ring buffer of a few thousand events (a few
hundred KB to low MB)" for a single undifferentiated log. That number is
too large by roughly two orders of magnitude for what classification
actually needs, once split into what it's really for:

### 4.1. Tier 1 — sliding classification window

Classification only needs enough history to recognize **one in-progress
burst** before it resolves (matches, or ages out) — not a long trace.

```cpp
#pragma pack(push, 1)
/// Mirrors TTDWriteRecord's (core/src/debugger/ttd/ttdwritejournal.h)
/// bit-packed 12-byte convention deliberately — same idea, read+write
/// instead of write-only, and scoped to a short rolling window instead of
/// a multi-million-record session-length ring.
struct AccessEvent
{
    uint64_t tstate  : 40;   // matches TTDWriteRecord's globalT width
    uint64_t address : 16;
    uint64_t isWrite : 1;
    uint64_t pad     : 7;    // reserved
    uint16_t pc;
    uint8_t  value;
    uint8_t  reserved;       // e.g. physical page, for banked-machine
                             // disambiguation if/when needed (parallels
                             // TTDWriteRecord::physPage) — unused for now
};
#pragma pack(pop)
static_assert(sizeof(AccessEvent) == 12);
```

Default capacity: **256 records** (generous headroom over an 8-16-event
burst with gap tolerance, §4.3) = **3 KB per active session**. Ring
semantics identical to `TTDWriteJournal`'s: once an event's window has
either resolved into a tier-2 burst or aged out unmatched, the ring simply
overwrites it — no separate eviction step.

### 4.2. Tier 2 — confirmed bursts

Grows only with actual witnessed print/blit events, not with raw traffic.

```cpp
struct AccessBurst
{
    uint16_t sourceAddress;
    std::array<uint16_t, kMaxBurstLength> destinationAddresses; // fixed,
        // NOT std::vector — corrects tdd.md revision 2, which used a
        // heap-allocating vector here; a per-burst heap allocation on a
        // path that may fire hundreds of times per second is exactly the
        // kind of avoidable cost this design tries to rule out up front.
    uint8_t length;           // <= kMaxBurstLength
    uint16_t sourcePc;
    uint16_t destPc;
    enum class Transform : uint8_t { Identity, And, Or, Xor, BitReverse, Unknown };
    Transform transform;
    uint8_t transformOperand;
};
```

`kMaxBurstLength = 8` for this feature (every ZX font glyph is 8 rows); a
future sprite/tile consumer needing a taller fixed shape picks its own
constant when it's actually built (requirements §7-O5) — not guessed here.
Per-record size ≈ 2 + 8×2 + 1 + 2 + 2 + 1 + 1 = 25 bytes, ~32 with
alignment. Default capacity **4096 bursts = 128 KB**, ring behavior
(oldest evicted first) — a session running for the length of normal
gameplay naturally keeps only its most recent ~4096 witnessed events, far
more than F24 needs for high confidence on a handful of glyphs.

### 4.3. Total footprint

**~3 KB (tier 1) + up to 128 KB (tier 2, only as it fills) ≈ well under
256 KB per active session** — this matters because a caller may reasonably
leave a session running for an entire play session (NF3), not just a short
capture window, and the footprint must not grow with how long the session
runs.

## 5. Memory management

- **Lazy allocation on `start()` only**, mirroring two existing patterns in
  this codebase: `MemoryAccessTracker::AllocateCounters()` ("counters only
  allocated when tracking is enabled") and `TTDWriteJournal::LazyRing`
  (chunk-committed on first write, so construction itself is cheap even at
  a much larger 64 MB default capacity). Concretely: reuse the `LazyRing`
  chunking idea for tier 1/tier 2 rather than a straight up-front
  allocation — at this component's much smaller sizes (§4.3) the benefit is
  smaller than it is for the 64 MB write journal, but the pattern costs
  nothing to reuse and avoids a fixed allocation cost on every session
  start regardless of whether it's ever filled.
- **Deallocation on `stop()`**: both tiers freed back to zero, matching
  `MemoryAccessTracker::DeallocateCounters()`'s existing convention — no
  memory held between sessions (G6).
- **One tracer instance per emulator id**, one overlay installed at a time.
  Multiplexing several independent `AccessPatternTracer` sessions with
  different windows on the same emulator (e.g. a live font session and a
  hypothetical concurrent sprite-finder session) is out of scope for this
  revision; the generic multi-overlay mechanism already exists at the
  `HostBusOverlayChain` level (`kMaxOverlays = 4`, shared with TSConf's FM
  window and NeoGS's ZX-DMA), so this component installs one overlay and
  coexists with *other devices'* overlays the way anything else does — it
  just doesn't itself serve multiple independent callers concurrently yet.

## 6. Thread safety and lifecycle

`start()`/`stop()` run on the emulation thread or with the emulator paused
— the same contract `Core::AddBusOverlay`/`RemoveBusOverlay` already have,
no new locking model introduced for installation. `drainBursts()` is called
from an automation thread and takes a short-held mutex around the tier-2
read/pop, not around every `onAccess()` call (`onAccess` itself only needs
to be safe against a concurrent `drainBursts` reading tier 2 — tier 1 is
single-producer/emulator-thread-only, consumed internally by the
classifier on the same thread, and only classified *results* cross into
tier 2 where the mutex applies) — mirrors `TTDWriteJournal`'s own
single-producer/multi-consumer model rather than inventing a new one.

## 7. TTD integration

Two integration points — both narrower than tdd.md revision 2's placeholder
("depends on TTD v2 exposing a replayable instruction/memory event
stream"). **Neither needs to wait for TTD v2** (PLAN #40); both can be built
against what the TTD v1 engine already has today.

### 7.1. Reuse the existing write journal when TTD is already recording

If a TTD recording session is active on the target emulator,
`TTDWriteJournal` is *already* capturing every memory write at 12
bytes/record — a superset of tier 1's write-side data for that session.
When a TTD recording is detected active, `AccessPatternTracer`'s write-side
capture should read from the existing journal
(`TTDWriteJournal::RecordAt`/`SeqHead`/`SeqTail`) instead of duplicating a
second write hook and a second copy of write events — saves memory, avoids
a second hook site on the same writes, and means tier 1 in this mode holds
**reads only** (roughly halving its already-small footprint). This is an
optimization, not a requirement: `AccessPatternTracer` must still work
correctly with its own write capture when no TTD recording is active.

### 7.2. Method D — offline correlation, built on what already exists

Method D (tdd.md §2.4.2/F23) is **not** blocked on a new TTD v2 timeline
API. It is a two-phase search entirely on top of existing TTD v1
mechanisms:

1. **Phase 1 — cheap destination-shape scan of the write journal.**
   `TTDWriteJournal::FindLast` already supports a backward predicate scan;
   Method D adds a small sibling query (a new method on the journal, not a
   storage change) that scans for **write-record clusters**: N writes
   landing on one screen character cell's 8 interleaved addresses within a
   short `globalT` span. This is the same memory-bandwidth-bound access
   pattern the journal's own doc already documents a cost for ("~25 ms
   memory-bandwidth-bound scan" at a full 64 MB journal) — cheap, and
   **needs no new storage** at all.
2. **Phase 2 — bounded silent replay to recover the read side.** For each
   candidate window Phase 1 finds, restore the nearest checkpoint before it
   and drive `Emulator::RunTStates` forward to the window — the exact
   mechanism `TimeTravelManager` already uses for intra-frame silent replay
   and for the write journal's own documented "two-pass silent replay"
   fallback (`core/src/debugger/ttd/timetravelmanager.cpp`,
   `RestoreCheckpoint`/`RunTStates`) — with a live `AccessPatternTracer`
   session (§5-§6, same as Method C) attached only for that short window.
   This recovers the source table address and the confirmed code→glyph
   mapping (F24) the same way Method C does live, just driven by replay.
3. Cost scales with **the number of candidate bursts Phase 1 finds**, not
   with session length: a session with a few dozen distinct "screen
   redrawn" moments costs a few dozen short bounded replays (each on the
   order of one frame's worth of T-states), not a full-session re-run.

**Correction to tdd.md**: its §5 (implementation plan, P1.8) and §7 (risks)
should drop the framing that Method D is a hard dependency on TTD v2's
migration landing first. TTD v2 (PLAN #40 V1, per-region memory pages)
remains relevant the same way it already is for Methods A/B on banked
machines (F5's documented fallback), not as a new blocker specific to
Method D.

## 8. Benchmarking plan

Every number in §3-§4 is a planning estimate. This design is gated on
confirming them, per this project's rule that hot-path-adjacent changes
need a measured A/B number before the default ships:

- **B1 — idle cost**: confirm an installed-but-never-matching window costs
  only the "one flag test" `hostbusoverlay.h` documents for an idle
  overlay, A/B against a build with no overlay installed at all.
- **B2 — full-64 KB-window active cost**: real FPS impact with tiers 1/2
  active on a real game (reuse the font-finder verification titles, V3/V4)
  — confirms or corrects §3's ~2.5% estimate and §2's access-count
  extrapolation.
- **B3 — scoped-window active cost**: same measurement with §3.1's
  dual-window scoping, to quantify the "run Method A/B first, then scope
  Method C" benefit.
- **B4 — Method D phase-1 scan cost**: time to scan a full
  `TTDWriteJournal` (default 64 MB / ~5.5M records) for destination-shape
  clusters — expected to track the journal's own documented ~25 ms
  full-scan figure closely, since it's the same access pattern over the
  same data structure.
- **B5 — Method D phase-2 replay cost**: wall-clock cost per candidate
  window replay, and total cost across a realistic candidate count (a few
  dozen) for a typical session length.

## 9. Summary table

| Concern | Answer |
|---|---|
| Memory, live session | ~3 KB (tier 1) + up to 128 KB (tier 2) ≈ <256 KB, bounded regardless of session length |
| Memory, idle | 0 (lazy allocation, freed on `stop()`) |
| CPU, idle | ~0 (one flag test per `hostbusoverlay.h`'s existing contract) |
| CPU, active (estimate, unconfirmed) | ~0.15-0.5 ms/frame worst case (full 64 KB window); less when scoped (§3.1) |
| New on-disk storage | None required — bursts are a live/replay-derived view, not persisted by this component itself (a caller wanting to keep results persists `FontCandidate`s, not raw bursts) |
| TTD dependency | None hard; §7.1 is an optional optimization when TTD is recording, §7.2 (Method D) builds on existing TTD v1 write-journal + replay primitives, not a future TTD v2 API |
