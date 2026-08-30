# DeZog Reverse Debugging over DZRP (TTD-backed)

Status: **implemented (proof-of-concept), on by default**. The intra-frame
ring-cache in §5 is a **proposal only — NOT implemented**. §6 is the agreed
design (2026-08-30) for non-destructive live history across browse cycles —
once implemented it supersedes §3's snapshot+restart return path. The adapter
covered here is shared by the DZRP and ZRCP servers, so §6 applies to both
transports (the reported failure came through ZRCP).

## 1. What DeZog needs

DeZog gets "full" reverse debugging (its `CpuHistory`, as opposed to the
"lite" `StepHistory` that any DZRP remote gets for free) when the remote hands
it **one executed-instruction record per index**, index 0 = most recently
executed. Each record is the `CMD_GET_REGISTERS` payload plus the 4 opcode
bytes at PC and the word at (SP). Memory is read **live** while browsing — DeZog
never stores it — so a remote that can show *historic* memory during reverse
stepping is strictly better than ZEsarUX/zsim, which show present memory.

Source of truth for the contract: DeZog `src/remotes/cpuhistory.ts`,
`decodehistinfo.ts` (`DecodeStandardHistoryInfo`), `zesaruxcpuhistory.ts`.

## 1a. DZRP framing (asymmetric — matched to DeZog)

The wire framing is **asymmetric** per direction (verified against DeZog
`src/remotes/dzrpbuffer/dzrpbufferremote.ts` — `sendDzrpCmd` and `dataReceived`):

- **Command (client → us):** `length(4) = DATA length ONLY` (excludes the seqNo
  and command bytes), then `seqNo(1) + command(1) + data`. Full frame = 6 + data.
- **Response (us → client):** `length(4) = seqNo(1) + data` (includes the seqNo),
  then `seqNo(1) + data`. DeZog reads `length` bytes after the 4-byte header.
- **Notification (us → client):** `length(4) = seqNo(1) + notifyId(1) + data`.

> **Bug fixed here:** our command parser originally counted the seqNo+command in
> the command length (like our own client did), so it agreed with itself but
> **not with real DeZog** — DeZog's data-only length left 2 stray bytes per
> command, desyncing the stream after `CMD_INIT` and surfacing as DeZog's
> *"No response received from remote."* `readFramedMessage` now consumes
> `4 + 2 + dataLen`; responses/notifications were already correct. The Python
> verifier client frames byte-identically to DeZog, so it faithfully guards this.

## 2. Protocol extension (implemented)

Two commands outside the DZRP 2.x range, advertised via
`CMD_GET_SUPPORTED_COMMANDS` so stock DeZog ignores them until a
`CpuHistory` subclass is added client-side:

| Cmd | ID | Request | Response |
|-----|----|---------|----------|
| `CMD_GET_HISTORY_INFO` | `0xE0` | — | `available(1) recording(1) reserved(2)` |
| `CMD_GET_HISTORY_ENTRY` | `0xE1` | `index(4, LE)` | `error(1)` [+ reg block(29, `nslots`@28) + slots(`nslots`) + opcodes(4) + SP-word(2)] |

`error`: 0 ok, 1 out-of-range/no-history, 2 history-not-available.

Note: the register block is exactly 29 bytes + `nslots` slot bytes — the DZRP
`CMD_GET_REGISTERS` layout with **no trailing padding**, so the opcode/SP fields
follow immediately. (An earlier phantom trailing byte in the shared serializer
shifted the history payload by one; fixed.)

### History size is server-managed — external buffer sizes are ignored

DeZog (and ZEsarUX-style remotes) carry a `reverseDebugInstructionCount` /
`cpu-history set-max-size` notion — a fixed client-side ring bound. We have no
such bound: history is the **whole unbounded TTD session**, and lookup memory is
the transient per-frame decode cache (§5), both managed automatically. So **any
client request to set a history/buffer size is accepted and ignored** — we
respond OK and keep auto-managing. If a future protocol revision adds a
note/reason field to that response (or to `CMD_GET_HISTORY_INFO`, which today has
2 reserved bytes available), we advertise "auto-managed; requested size ignored"
so the client knows not to bother sizing. Stock DZRP does not send a size command
at all (DeZog's `StepHistoryClass.init()` only stores the count locally), so
today this is purely a forward-compatibility policy for a custom `CpuHistory`
client: don't size us; we size ourselves.

## 3. How it maps onto TTD (implemented)

`DezogDebugAdapter` drives the existing `TimeTravelManager`:

- **CMD_INIT** → `StartRecording()` (unless `UNREAL_DEZOG_HISTORY=0`). Capture
  runs continuously while the target executes.
- **`getHistoryEntry(index)`** — served from the **per-frame decode cache** (§5),
  the winning strategy (see §"Best strategy for DeZog"). On the first call it
  snapshots the present (`.sna` bytes, for an exact return) and `StopRecording()`
  to freeze the timeline. It then maps the global DeZog index to `(frame,
  entry-in-frame)` via `resolveHistoryIndex` — a small `_historySegs` table built
  lazily from the present backward, one segment per frame, memoizing each frame's
  visible-entry count. The present frame is *partial* (only instructions with
  `tInFrame < presentTInFrame` are already-executed history); earlier frames are
  complete. The record is read straight from `TimeTravelManager::GetFrameCache`
  (regs / opcodes / SP-word / slots), **O(1)** after a one-time ~ms frame fill —
  no seek per read.
- **Memory while browsing** — the emulator stays at the present (the cache serves
  the registers), so `readMemory` returns **present** memory. This matches
  DeZog's own model ("the memory you see during reverse debugging is the actual
  one"). Historic memory is still available on demand via a TTD `SeekTo` if a
  future consumer needs it.
- **Return to present** — any forward-moving command (resume, pause, register/
  memory/slot/bank write, state restore, session close) restores the snapshot
  taken on entry and `StartRecording()` again. A snapshot restore is used rather
  than a TTD seek because the present is a *mid-frame* stop, always beyond the
  last frame-boundary checkpoint, so `SeekTo`/`ResumeRecordingFrom` cannot target
  it. **Superseded by §6 once implemented:** browse becomes read-only (no
  snapshot, no restore) and the return appends to the timeline instead of
  wiping it. This bullet documents the shipped proof-of-concept behavior that
  causes the history loss §6 fixes.
- **Debugger edits** (register/memory/bank writes from DeZog) record a
  `DebuggerEdit` marker and restart recording from the edited state — replay
  cannot reproduce an out-of-band edit, so the pre-edit segment is dropped.

Measured effect of the cache wiring (`DezogHistory_test.LatencyReport*`):
sequential step-back ~0.54 ms/entry (one frame fill amortized over the run);
**jump to index 500: 0.74 s → 167 ns**, **jump to index 20000: 17 s → 3.8 ms**
(same-frame jumps are cache hits; cross-frame jumps pay one ~ms rebuild).

*To change the strategy:* it is localized to `getHistoryEntry` +
`resolveHistoryIndex` + the `_historySegs` map. Reverting to a seek-per-read
(historic memory, no cache) is dropping the `GetFrameCache` calls and doing a
`SeekTo` per entry — the code carries a comment pointing here.

### Semantics caveat (why index↔instruction is not pinned in tests)

TTD reverse-seek is M1-granular over coverage-pruned replay. The exact
instruction a given index lands on depends on where the stop occurred (a
pre-execution breakpoint has already fetched the current instruction's M1;
a mid-frame manual pause is ambiguous by 1–2 M1s). The tests therefore assert
**coherence invariants** — opcodes match live memory at the entry PC, positions
move strictly backward, PCs are in-program, out-of-range is non-destructive
(the browsable history survives a bad index; the return to the present happens
on the next forward command) —
not a hard-coded PC-per-index trace. This is good enough for DeZog's UI (it
disassembles the returned opcodes and shows the reverse trace); pinning exact
DeZog-index semantics is one of the motivations for the ring cache in §5.

## 4. Measured performance (current implementation)

Release build, macOS arm64, Pentagon model (`frameT = 71680` t-states/frame).
All numbers from `core/benchmarks/debugger/ttd/ttd_reverse_benchmark.cpp`
(`BM_TTD_Busy_*`, `BM_TTD_Ring_ReadEntry`), Google Benchmark, run on a
**busy-frame workload** (a continuous straight-line ALU/load/jump loop, so a
frame is packed with real instructions — the ROM boot workload idles in a HALT
loop at ~1 instruction/frame and is not representative). Measured density for
this workload: **13,562 instructions/frame**.

### The measured cost

Every single `StepBackInstruction` restores the frame-start checkpoint and
replays forward to the target t-mark. So the cost of reverse stepping is
**linear in the number of instructions you step back**, at ~1.37 ms each — each
step redundantly re-replays from the frame start.

| Measurement (busy frame, 13,562 instr/frame) | Value |
|----------------------------------------------|-------|
| One back-step, average (`BM_TTD_Busy_Rewind_500`) | **1.37 ms** |
| One back-step from the browse point (`BM_TTD_Busy_StepBack_Single`) | **5.24 ms** |

What this means for the operations users actually perform:

| Realistic action | Instructions stepped | Current cost |
|------------------|----------------------|--------------|
| One reverse step | 1 | ~1.4 ms — imperceptible |
| A stop's history prefetch (DeZog `spotCount`) | ~10 | ~14 ms — fine |
| Step back a screenful of a routine | ~50–100 | ~70–140 ms — noticeable |
| Reverse-continue scanning a subroutine | ~1,000 | **~1.4 s** — sluggish |
| Reverse-continue across a whole frame+ | ~13,000 | **~18 s** — unusable |

Single steps and the 10-entry prefetch are already fine. The cost only bites
once an action walks back **many** instructions — reverse-continue over a
distance, or holding step-back. The key asymmetry: each back-step re-replays the
frame from its start, so N steps pay for N replays — which is exactly what the
frame cache (§5) collapses into a single fill.

## 5. Per-frame CPU/memory/port decode cache — IMPLEMENTED in TTD

### It is a decode cache, not a history limit

History depth stays **unbounded** — the whole TTD session is replayable, so you
can reverse-debug to the very first recorded frame. The ring is a **transient
decode cache for the frame(s) currently being browsed**, nothing more. There is
no "keep the last N instructions" buffer and no reason to impose one (that would
throw away TTD's infinite history to imitate ZEsarUX's bounded `cpu-history`).
Cost scales with *how many distinct frames you are looking at right now*, not
with how far back the history goes.

### Design (`core/src/debugger/ttd/timetravelframecache.h` + `TimeTravelManager`)

The first time browsing needs a frame, `TimeTravelManager::GetFrameCache(frame)`
replays that **one frame** forward once with an M1 capture hook installed,
recording per executed instruction a `TTDFrameCacheEntry` (CPU regs in DZRP
order, 4 opcode bytes at PC, word at SP, slots). The instruction's **memory and
port writes** are captured in the same replay via the existing
`RecordMemoryWrite` / `RecordIoWrite` paths (a couple of stores each, guarded so
they cost nothing outside a build). Subsequent reads of that frame are plain
array indexing. The cache also pins the index↔instruction mapping exactly (the
array *is* the M1 sequence), removing the §3 caveat.

**Transparency is a verbatim snapshot restore, not a seek-back.** The build
replay overwrites live state (Z80, RAM, port latches), so `GetFrameCache`
snapshots the live machine (CPU, chipset, `z80.t`, full model RAM, peripheral
blobs) before the build and restores it verbatim after — `SaveLiveState` /
`RestoreLiveState`, mirroring `RestoreCheckpoint`'s ordering including the
screen-cache resync. A seek-back cannot do this job: replay cannot cross
external-event markers (their effects are not reproducible), so any recorded
debugger edit inside the present frame — every soft breakpoint is a WRITE_MEM
edit — would park the emulator at the marker instead of the present.

**Hot/cold record split (see layout study below).** The record is the minimal
fixed **hot** part; the variable per-instruction accesses live in one shared
**arena** (`TTDFrameCache::accesses`), referenced by `(accessOffset, accessCount)`.
The arena is a single segment packed **sequentially** during the build — no
fragmentation, because it is append-only in one pass and `clear()`ed (capacity
reused) on the next frame build. `AccessesOf(i, count)` returns an entry's
access span.

**Pre-reservation sized to the largest possible frame, scaled with CPU speed.**
A frame is `config.frame` t-states at 1× (e.g. 71680 for Pentagon) and scales
with `current_z80_frequency_multiplier` — turbo packs proportionally more
instructions per frame (e.g. 16× at 56 MHz ≈ 287 k instructions/frame, ~16 MB of
records). The shortest Z80 instruction is `kMinInstructionTStates = 4` (NOP), so
entries and arena are reserved once to
`ceil(config.frame × multiplier / 4) + kFrameReserveMargin`. Ceiling division
plus the margin cover a boundary-straddling instruction (an instruction can
start just before the frame end and run up to the longest opcode past it) and an
injected interrupt, so a fill **never** reallocates mid-capture — verified for
56 MHz (16×) in `TTD_FrameCache_Test.ScalesWithCpuFrequencyMultiplier`. On a
reused block the reserve is a no-op. `BuildFrameCache` likewise replays the full
`config.frame × multiplier` t-states, so a turbo frame is captured whole (not
`1/multiplier` of it).

> **Known TTD limitation (not a cache issue):** `SeekTo` clamps its target
> `tInFrame` to the *base* `config.frame`, so seeking to a mid-frame position in
> a turbo frame lands short. The frame cache stores correct turbo `tInFrame`
> values and serves records directly (no seek needed), but the "return to a
> historical mid-frame position" path needs a separate turbo-aware `SeekTo` fix.

**Lifecycle — replay-scope only (as required):** built only when the session is
**not Recording** (Detached / Idle-with-history); a call while Recording returns
`nullptr`. **Freed, memory released,** the moment the session leaves the browse
scope — `ClearFrameCache()` is called from `StartRecording`,
`ResumeRecordingFrom`, and `InvalidateSession` (every path back to the live
present). The block itself is reused across frame crossings within a browse
scope (clear + refill), so it allocates at most once. It never exists during
live forward recording, and lives in TTD, not the DeZog module.

### Measured performance — cached lookup vs replay baseline

Apples-to-apples, same busy workload (13,562 instr/frame), same manager, from
`ttd_reverse_benchmark.cpp` (`BM_TTD_FrameCache_*`):

| Operation | Measured |
|-----------|----------|
| Baseline — one entry via `StepBackInstruction` (replay per read) | **5.48 ms** |
| Cached — one entry via `GetFrameCache` index (post-fill) | **42 ns** |
| First-touch fill (`GetFrameCache`, one frame replay) | **4.83 ms** |
| Cache footprint (13,562 entries + write arena) | **1.075 MB** |

**Cached lookup is ~130,000× faster than the replay baseline** (42 ns vs
5.48 ms) after a one-time ~4.8 ms fill. The cost model changes from *per
instruction reached* to *per distinct frame entered*:

| Realistic action within one frame | Replay baseline | Frame cache |
|-----------------------------------|-----------------|-------------|
| First entry into a frame | 5.48 ms | **4.83 ms** (fill) |
| Each further entry in that frame | 5.48 ms | **42 ns** |
| 10-entry stop prefetch | ~55 ms | 4.83 ms + ~0.4 µs |
| Reverse-continue scanning ~1,000 | ~5.5 s | 4.83 ms + ~42 µs |
| Scan the whole frame (~13.5k) | ~74 s | **~4.9 ms** |

### Record layout study — why hot/cold split

`BM_TTD_Layout_HotLookup` materializes the **same** captured frame into three
candidate layouts and random-reads the CPU/history fields (the DeZog
`getHistoryEntry` path) at three working-set sizes. Working set = the base frame
**tiled** to model a windowed cache holding that many browsed frames — so all
layouts hold the same number of cached instructions and differ only in bytes.

- **Fat AoS** — inline memory/port arrays in the record (76 B).
- **Split** — minimal record (56 B) + referenced shared access arena. *(chosen)*
- **CpuOnly** — minimal record (48 B), accesses dropped (lower bound).

| Working set | Fat AoS (76 B) | Split (56 B) | CpuOnly (48 B) |
|-------------|----------------|--------------|----------------|
| 1 frame (~0.7–1.0 MB, fits L2) | 2.13 ns | 2.11 ns | 2.12 ns |
| 8 frames (~5–8 MB, exceeds L2) | 3.76 ns | **2.82 ns** | 2.58 ns |
| 32 frames (~20–31 MB, exceeds L3) | 10.5 ns | **8.99 ns** | 8.80 ns |
| Memory @ 32 frames | 31.5 MB | **23.2 MB** | 19.9 MB |

**Conclusions:**
1. At a cache-fitting working set (one frame, <L2) record size is **invisible** —
   all three are ~2.1 ns. Measuring only there (the initial mistake) hides the
   real trade-off.
2. At realistic large working sets (browsing across many frames / a windowed
   cache) random lookup **misses to L3/RAM**, so the smaller record wins:
   **Split is 14–25 % faster than Fat** and uses **~26 % less memory**.
3. **CpuOnly** is marginally faster/smaller than Split but throws away the
   mem/port data. Split keeps it at near-CpuOnly cost — the right balance of
   *reasonable memory, fast lookup, minimal allocations* (records + one arena =
   two amortized allocations, reused across builds).

#### Why not push it further — a *tiny* hot record (just `{tInFrame, pc}`)?

Tempting on small-cache CPUs: keep only a couple of fields hot and move the
whole CPU state into a referenced extension. `BM_TTD_Layout_HotLookup` (which=3,
`TinyHot`) and `BM_TTD_Layout_PcScan` measure it, and it cuts both ways:

| Access pattern | Split (full CPU hot, 56 B) | TinyHot ({tInFrame,pc} + cold ext) |
|----------------|----------------------------|------------------------------------|
| Full-state lookup, 8 frames (`getHistoryEntry`) | **2.67 ns** | 3.07 ns |
| Full-state lookup, 32 frames | **9.26 ns** | 10.4 ns |
| PC-only scan, 8 frames (`reverse-continue`) | 63 µs | **23 µs** |
| PC-only scan, 32 frames | 450 µs | **92 µs** |

- **Full-state lookup is ~15 % slower** with TinyHot: the field you always need
  (CPU state) now costs a *second* cache miss (hot → cold), for no memory saving.
- **Single-field scans are 3–6× faster** with TinyHot: the scan streams only the
  ~10 B hot array instead of a 56 B stride — and the win grows on smaller caches.

**Conclusion:** DeZog's dominant path is full-state `getHistoryEntry`, so the CPU
state belongs in the hot record — TinyHot would be a net loss there. Split is the
right default. A tiny record only wins for scan-dominated work (reverse-continue),
and the better answer for that is an *additional* parallel scan index
(`{tInFrame, pc}` array) layered on top of the full record, not moving the CPU
state out of the hot path. Not built now; noted as an option if reverse-continue
latency ever dominates.

### Memory cost

Real footprint **1.075 MB for a busy 13,562-instruction frame** (records +
write arena; ~56 B fixed record + a few bytes of arena per writing instruction).
Typical (non-tight-loop) code runs fewer instructions per frame, so this is an
upper-ish bound. The footprint is bounded by the **browse window** — one frame
in the current single-frame implementation — **not** by history depth, which
remains the full (unbounded) TTD session. A windowed variant (last _W_ browsed
frames) would cost _W_ × ~1.1 MB and is a trivial extension if back-and-forth
scrubbing across frame boundaries warrants it.

### Status / next

- **Implemented in TTD:** `timetravelframecache.h` (`TTDFrameCacheEntry` split
  record + `TTDFrameCache` arena) and `GetFrameCache` / `ClearFrameCache` /
  `GetFrameCacheBytes` / `GetCachedFrame` in `TimeTravelManager`; CPU +
  memory/port-write capture; frame-length-scaled pre-reservation; replay-scope
  lifecycle with block reuse. Tests: `TTD_FrameCache_Test` (8 — cached entries
  match live replay, mem/port capture via the arena, position transparency,
  freed on StartRecording/Invalidate). Benchmarks: `BM_TTD_FrameCache_*`
  (cached-vs-replay), `BM_TTD_Layout_HotLookup` (layout study).
- **Wired into DeZog:** `DezogDebugAdapter::getHistoryEntry` now serves records
  from `GetFrameCache` via `resolveHistoryIndex` (global index → (frame, entry)),
  turning the per-read win into DeZog latency — measured jump-to-500 0.74 s →
  167 ns, jump-to-20000 17 s → 3.8 ms. Tests: `DezogHistory_test.*` (15) updated
  for the cache-backed semantics (present memory during browse, per DeZog's
  model). The index↔instruction mapping is now exact (the cache is the M1
  sequence), so the §3 "semantics caveat" no longer applies to cached reads.
- Read **values** (mem/port reads) can be captured in the same build pass and
  appended to the arena with `TTDAccessKind::MemRead` / `PortRead` if a consumer
  needs them; writes are captured today.

## 6. Live history: non-destructive browse/resume (design — 2026-08-30)

Status: **design agreed, NOT yet implemented.** Post-review decision
(2026-08-30): implemented as a **separate DebuggerLive recording mode**
(6.3) — the existing session mode stays byte-identical. Fixes the reported
failure: with DeZog attached, Reverse-Step / Reverse-Continue always ends
in *"Break: Reached end of instruction history."* Reproduced live twice
(ZRCP, DeZog 3.7.4 flow); the fix lives in the shared `DezogDebugAdapter` +
TTD engine, so both transports benefit.

### 6.1 Two reverse-debugging models — do not conflate them

1. **Read-only history browsing** — what DeZog actually does. Its
   `ZesaruxCpuHistory` (verified in the installed 3.7.4 bundle) fetches
   `cpu-history get 0,1,2…` (index 0 = most recent) and renders the trace
   **client-side**; the emulator never moves. At every stop DeZog
   spot-fetches a few entries, and `continue` from a browsed position first
   walks its local array forward, only then resumes the machine. The server
   needs to *serve entries* and *keep recording* — no seek, no truncate, no
   rewrite.
2. **True time travel** — unreal-qt scrubber, WebAPI TTD endpoints, a future
   DZRP reverse-step. The machine genuinely moves back (Detached), and
   resuming from the past **forgets the recorded future and rewrites it**.
   These semantics already exist: `SeekTo` + `ResumeRecordingFrom(T)` seeks
   back, truncates the timeline after `T`, releases page refs, drops journal
   future, and resumes recording. Nothing in §6 changes them.

### 6.2 Root cause

The state machine has no *Idle-with-history → Recording* transition that
keeps the timeline, and browse **requires** leaving Recording
(`GetFrameCache` is replay-scope-only, §5):

1. DeZog's stop-time spot fetch issues `cpu-history get …` at **every** stop
   → `getHistoryEntry` enters browse → `StopRecording()` (timeline frozen,
   kept).
2. The next forward command → `leaveHistory` → restores the SNA snapshot and
   calls `StartRecording()` → **`_timeline.clear()` — the entire past is
   dropped, every single stop.**

So history at any stop contains only what executed since the previous
browse-exit; reverse-continue immediately hits "end of instruction history".
Secondary contributor: recording starts at session open while the emulator
is paused, so the first stop's history is near-empty (accepted — see 6.3.3).

### 6.3 Design — decision (2026-08-30, post-review): a separate DebuggerLive mode

The live-history capability is implemented as a **recording mode on
`TimeTravelManager`, separate from the existing session mode**, not as bare
new transitions on the shared state machine:

- `TTDRecordMode::Session` (default) — today's semantics byte-identical:
  `StartRecording` wipe+baseline, scrub/seek/Detached,
  `ResumeRecordingFrom` truncate+rewrite, `.ttd` save/load. WebAPI, CLI and
  the scrubber keep this mode; nothing they call changes behavior.
- `TTDRecordMode::DebuggerLive` (new) — entered by debug sessions (DZRP
  `CMD_INIT` / ZRCP session open via the adapter). Recording is continuous
  for the session's lifetime, browsing never stops it, and the only
  invariant relaxed is **mode-scoped**: `GetFrameCache` may build while
  Recording **when the emulator is paused** — safe because the emulator
  thread is parked and the build already round-trips live state internally
  (`SaveLiveState` → build → `RestoreLiveState`, §5). `SeekTo`/
  `StepBack` stay forbidden while Recording, so the scrubbing-corruption
  defenses all hold. Leaving the mode (`EndDebuggerLiveHistory`) stops
  recording and **keeps the timeline**, handing a normal Idle-with-history
  to the scrubber/`.ttd` flows.

The transition both modes share — the actual bug fix, history must survive
a browse cycle without a stop/resume dance — is still the append-resume:

| From | Call | To | Timeline |
|------|------|----|----------|
| any | `StartRecording` | Recording | wiped; fresh baseline (unchanged) |
| Recording | `StopRecording` | Idle | kept for browse (unchanged) |
| **Idle (history)** | **`ResumeRecordingLive` (new)** | **Recording** | **kept; appends after the recorded end** |
| Idle (history) | `SeekTo` / `StepBack` | Detached | kept (unchanged) |
| Detached | `ResumeRecordingFrom(T)` | Recording | truncated after T; future rewritten (unchanged — the "return forward forgets and rewrites" semantics) |

#### 6.3.1 Engine primitives

New API on `TimeTravelManager`:

- **`BeginDebuggerLiveHistory()`** — enter DebuggerLive. If already
  Recording (Session start hijacked by an attach): keep the timeline and
  switch the mode flag. If Idle-with-history and no gap: append-resume via
  `ResumeRecordingLive`. If Idle-empty or gapped: fresh start (baseline,
  like `StartRecording`). Feature stewardship as in `StartRecording`.
- **`EndDebuggerLiveHistory()`** — Recording → Idle, timeline **kept**,
  mode → Session; the scrubber and `.ttd` save can then take over.
- **`IsDebuggerLive()`**.
- **`ResumeRecordingLive()`** — `Idle`-with-history → `Recording`,
  **appending** after `timeline.back()`: no wipe, no baseline capture,
  `ClearFrameCache()`, feature stewardship as in `StartRecording`. This is
  the shared transition from the table above (also usable in Session mode
  by future callers); in DebuggerLive it is mostly a `Begin…` building
  block.
- Debugger edits keep the **existing marker + restart path in both
  modes** — no `RecordDebuggerEdit` API shipped. A marker-only handling
  (marker + `ClearFrameCache()`, wipe-free) was implemented for
  DebuggerLive and **reverted after live testing proved it unsound**:
  replay re-executes from the last checkpoint, and an out-of-band edit
  (registers/PC/memory) is invisible to the write journal, so every
  entry decoded between that checkpoint and the marker is *fabricated
  execution that never ran* (the original regression observation — ROM
  PCs — was later traced to a probe-side number-format artifact, §6.7,
  but the journal-visibility argument alone proves the fabrication). A
  mid-frame baseline checkpoint cannot fix this
  either — `CaptureNow` always stamps `tInFrame == 0`, so it would
  collide with the frame's existing boundary checkpoint at the same
  time coordinate. The only sound Phase-1 semantics is restarting the
  recording at the edited state; Phase 3 journals debugger writes to
  remove the wipe.
- `GetFrameCache` gains the mode-scoped exemption: allowed while
  Recording **iff** `_recordMode == DebuggerLive` and the emulator is
  paused;
  otherwise unchanged (replay-scope only). `OnFrameBoundary` clears the
  frame cache unconditionally (cheap, and it kills same-frame staleness
  when a paused-build is followed by a resume — in Session mode nothing
  changes because browse scopes never cross a live boundary).
- **The cache build must be state-transparent**: `BuildFrameCache`
  replays through `SeekToInternal`, which transitions the session to
  `Detached` (TDD §4.2). `GetFrameCache` now saves/restores `_state`
  around the build — a cache build is a facility, not a state
  transition. Without this, every browse stranded the manager in
  `Detached` while the capture wiring was still live:
  `BeginDebuggerLiveHistory` then refused (Detached guard) and history
  silently died at the first browse — found by the §6 regression suite
  (`HistorySurvivesBrowseAndStopCycles`), invisible before because the
  old `leaveHistory` force-called `StartRecording`, which has no
  Detached check.

Guards on `ResumeRecordingLive` (return false + `MLOGWARNING`, caller
falls back to a fresh start):

- already `Recording` → return true (idempotent);
- `Detached` → false (the scrubber owns the machine; it uses
  `ResumeRecordingFrom`);
- empty timeline → false (nothing to append to; caller starts fresh);
- **no unrecorded gap**: `CurrentPosition().frame ==
  timeline.back().frame`. A mid-frame present in the *same* frame as the
  recorded end is fine — the partial frame simply continues where it
  paused. This is tighter than the first draft ("present ≥
  `SessionEndPosition()`"): if the emulator ran while recording was stopped
  (e.g. resumed from the GUI during a browse), the gap frames have **no
  checkpoints and no journaled writes** — replaying across such a gap from
  the older checkpoint would silently produce wrong state. Gapped resumes
  must wipe (fall back to `StartRecording`), which is exactly today's
  behavior, so the guard can only ever *preserve* correctness.

#### 6.3.2 Adapter: browse is read-only, recording never stops

In DebuggerLive the adapter's browse becomes trivially read-only — the SNA
snapshot/restore pair and `_presentSnapshot` are deleted; `_present`
(the `TTDTimePoint`) stays for the partial-frame counting in
`resolveHistoryIndex`. Changes:

- `ensureHistoryRecording` (init/continue/restore/session-open paths) →
  `BeginDebuggerLiveHistory()` when history is enabled; a no-op when
  already in the mode.
- `getHistoryEntry` entry block: remember the present `TTDTimePoint` and
  browse — **no `StopRecording`, no snapshot**; `GetFrameCache` builds
  under the paused exemption. Requires the mode (else nullopt =
  history-not-available).
- `leaveHistory`: drop browse bookkeeping (`_historyCursor = -1`, clear
  `_historySegs`) + `ClearFrameCache()` — recording was never stopped, so
  there is nothing to restart. Cache clear is mandatory here: a re-pause
  in the *same* frame before any boundary must not observe the stale
  partial build (the engine's boundary-clear only fires at the next
  frame).
- `onDebuggerEdit` stays the marker + restart path **in both modes**
  (6.3.1): the marker lands, recording restarts from the edited state,
  and pre-edit history is dropped. DeZog breakpoints are native
  breakpoint objects, not memory edits, so toggling them never triggers
  it.
- `onSessionClosed` → `EndDebuggerLiveHistory()` (timeline kept for the
  scrubber) + the existing resume.
- If another surface moved the machine mid-browse (`Detached` or
  `CurrentPosition().frame < _present.frame`): drop bookkeeping only and
  do **not** touch recording — that surface owns the machine and will
  `ResumeRecordingFrom` itself.
- WebAPI and the CLI keep calling `StartRecording` directly (Session
  mode); while DebuggerLive is active they are refused with a warning —
  explicit `EndDebuggerLiveHistory` first, no implicit mode switch.

#### 6.3.3 Recording start policy — decision (2026-08-30): debug sessions only

Recording auto-starts when a debug session attaches (DZRP `CMD_INIT`, ZRCP
session open — the existing `ensureHistoryRecording` wiring). Pre-attach
execution is unrecorded **by design**: it matches DeZog itself, which sends
`cpu-history clear` at init (our clear stays a no-op, §2; server-side
pre-open history would simply never be asked for). Rejected alternatives:
always-on auto-record at emulator creation (forces the `kDebugMode` write
path and ~1 KB/frame heap onto gaming/headless runs) and a features.ini
feature flag (rollout complexity for a policy we do not want as default).

### 6.4 Edge rules

- **Debugger edit during browse** (register/memory/slot write): the
  out-of-band edit is invisible to the write journal, so any cached decode
  of the edited partial frame after the edit would diverge from what
  executes. Phase 1 keeps it conservative — return to the present, record
  the marker, then `StopRecording()` + `StartRecording()` (fresh baseline;
  pre-edit history is dropped) — and this now holds in **both** modes: the
  marker-only variant shipped for DebuggerLive during implementation was
  reverted: replay after an out-of-band edit fabricates entries
  (journal-visibility argument, 6.3.1; the ROM-PC observation cited at
  the time was a probe artifact, §6.7). Phase 3
  journals debugger writes to keep pre-edit history (below). Note DeZog's
  breakpoints are native `CMD_ADD_BREAKPOINT` objects, *not* memory edits —
  toggling breakpoints never triggers this.
- **Unrecorded gap**: covered by the 6.3.1 guard — wipe and restart, never
  replay across a gap.
- **Emulator paused vs running**: browse and leave are control-thread-only
  while paused (existing invariant); forward commands leave history *before*
  resuming, so frames only ever advance with recording already on.
- **Growth**: unbounded by design (§5); debug-session-scoped start bounds it
  to session length; DeZog `set-max-size` requests stay ignored (§2).

### 6.5 Phases

- **Phase 1 (this fix):** DebuggerLive mode — engine API (6.3.1) + adapter
  rework (6.3.2) + tests — GTest regression (step ×N → browse
  (`cpu-history get 0`) → step forward → assert the pre-browse entries
  still resolve; with the bug the timeline is wiped and they return
  out-of-range), verifier step, live DeZog 3.7.4 run of the exact reported
  flow (attach → run → stop → reverse-continue walks back).
- **Phase 2 (decision only, no engine work):** recording start stays
  debug-session-scoped — 6.3.3.
- **Phase 3 (deferred, in priority order):** journal debugger writes
  (edits keep pre-edit history); intra-frame present addressing via journal
  replay (makes the trailing partial frame a legal `SeekTo`/
  `ResumeRecordingFrom` target — removes the mid-frame-unrepresentable
  limitation everywhere, enables true DZRP reverse-step); frame-cache build
  during *paused* Recording (removes the stop/resume dance entirely —
  landed ahead of schedule as the mode-gated paused-build exemption,
  6.3.1);
  trim-oldest policy honoring `set-max-size` hints instead of ignoring them.

### 6.6 Alternatives considered

- **Bare new transitions, no mode** (the pre-review §6 draft) — rejected
  post-review (2026-08-30): it would relax `GetFrameCache`'s replay-scope
  invariant and re-shape `StartRecording` semantics for *every* consumer
  (scubber, WebAPI, CLI) to serve debuggers. The DebuggerLive mode isolates
  the relaxation to the surface that needs it and leaves Session semantics
  byte-identical.
- **Always-on auto-record** — see 6.3.3.
- **Relaxing `GetFrameCache` for all Recording states** — the scrubbing-
  corruption defenses rely on Recording ⇒ no replay-scope builds; the
  mode exemption is pause-gated exactly so a running emulator can never
  enter a build.
- **Keeping the SNA snapshot for the return trip** — unnecessary once
  browse is read-only; it existed only because the old flow had no
  non-destructive resume.

### 6.7 Live validation outcome (2026-08-30)

The reported live anomaly — "history goes chaotic on the run after a
browse, 128k WebAPI instances only" — was investigated to root cause
and closed as **no product defect**. Chain of evidence:

- Symptom (probe rounds 4–6): the cycle-0 walk showed the test program
  "executing at ROM PCs 0x1F40–0x1F46" with program opcodes; the run
  after the browse free-ran ~1.15 s of chaos (289k entries, NOP sled,
  ROM IM1 register signatures) before the breakpoint fired.
- All in-process GTests (48K and 128K, browse+resume cycles, deep browse
  across frame boundaries) passed — the divergence had to be in the
  command stream, not the engine.
- Discriminating probe (raw `/ttd/status`, pre-edit register dump): the
  deepest walk entry matched the **pre-edit machine state
  register-for-register** except PC/SP — and PC=0x1F40 = **decimal**
  8000, SP=0xFF00 = decimal 65280.
- Root cause: the probe sent `set-register PC=8000` /
  `write-memory-raw 8000 …` *intending hex*. ZRCP plain numbers are
  **decimal** (ZEsarUX `parse_string_to_number` semantics — what DeZog
  itself sends; `zrcp-server.md` documents `<addrDec>`; the verifier
  uses `set-register PC=4660` for 0x1234). The server obeyed: program
  written and executed at 0x1F40, while the breakpoint `08006h`
  (h-suffix → hex) sat at 0x8006, reached only after the `JP #8001`
  landed in zeroed RAM. The "chaos" was real execution of a mistargeted
  program; the "stale byte-identical entries across cycles" were one
  continuous appended session (correct DebuggerLive semantics).
- Clean re-run with decimal values: 4-entry coherent history (R=f1→f4,
  AF 1d74→0174 at the real `LD A,1`), post-browse re-hit in 18 ms,
  7 entries appended across the browse, machine state and ROM
  untouched, `state=recording` throughout.

Lesson for probes and future test clients: ZRCP plain digits are
**decimal**; use decimal or an explicit `h`/`#`/`0x` form for hex. The
"fabricated ROM PCs" observation cited when reverting the marker-only
edit handling (6.3.1/6.4) predates this discovery and was the same
artifact; the journal-visibility argument independently justifies that
revert.
