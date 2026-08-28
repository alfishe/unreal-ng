# DeZog Reverse Debugging over DZRP (TTD-backed)

Status: **implemented (proof-of-concept), on by default**. The intra-frame
ring-cache in §5 is a **proposal only — NOT implemented**.

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

## 3. How it maps onto TTD (implemented)

`DezogDebugAdapter` drives the existing `TimeTravelManager`:

- **CMD_INIT** → `StartRecording()` (unless `UNREAL_DEZOG_HISTORY=0`). Capture
  runs continuously while the target executes.
- **`getHistoryEntry(index)`** — on first call it snapshots the present state
  (`.sna` bytes) and `StopRecording()` to freeze the timeline; then it walks the
  timeline backward one M1 at a time (`StepBackInstruction`), caching the
  distinct `TTDTimePoint` each DeZog index resolves to. Materialising an index
  is `SeekTo(cachedTimePoint)` then read regs / opcodes / (SP) / slots. While
  browsing, `readMemory` returns the historic memory at that point.
- **Return to present** — any forward-moving command (resume, pause, register/
  memory/slot/bank write, state restore, session close) first restores the
  snapshot taken on entry and `StartRecording()` again. A snapshot restore is
  used rather than a TTD seek because the present is a *mid-frame* stop, always
  beyond the last frame-boundary checkpoint, so `SeekTo`/`ResumeRecordingFrom`
  cannot target it.
- **Debugger edits** (register/memory/bank writes from DeZog) record a
  `DebuggerEdit` marker and restart recording from the edited state — replay
  cannot reproduce an out-of-band edit, so the pre-edit segment is dropped.

### Semantics caveat (why index↔instruction is not pinned in tests)

TTD reverse-seek is M1-granular over coverage-pruned replay. The exact
instruction a given index lands on depends on where the stop occurred (a
pre-execution breakpoint has already fetched the current instruction's M1;
a mid-frame manual pause is ambiguous by 1–2 M1s). The tests therefore assert
**coherence invariants** — opcodes match live memory at the entry PC, positions
move strictly backward, PCs are in-program, out-of-range resyncs to present —
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
| One back-step, average | **1.37 ms** |
| One back-step, worst case (target late in frame) | 5.24 ms |
| One full-frame **forward replay** (`SeekTo` frame-start → frame-end) | **0.67 ms** |

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
distance, or holding step-back. The key asymmetry: replaying a frame **once**
forward is **0.67 ms**, yet stepping back through those same instructions pays
0.67 ms of replay *per step*.

## 5. PROPOSAL (not implemented): per-frame CPU/memory/port decode ring

### It is a decode cache, not a history limit

History depth stays **unbounded** — the whole TTD session is replayable, so you
can reverse-debug to the very first recorded frame. The ring is a **transient
decode cache for the frame(s) currently being browsed**, nothing more. There is
no "keep the last N instructions" buffer and no reason to impose one (that would
throw away TTD's infinite history to imitate ZEsarUX's bounded `cpu-history`).
Cost scales with *how many distinct frames you are looking at right now*, not
with how far back the history goes.

### Idea

The first time browsing enters a frame, replay that **one frame** forward once
(the 0.67 ms pass above) and record, per executed M1, a fixed record into an
array covering the frame:

```
struct M1Record {              // CPU/regs/flags — ~40 B
    uint16_t pc, sp, af, bc, de, hl, ix, iy;   // 16 B
    uint16_t af2, bc2, de2, hl2;               //  8 B
    uint8_t  i, r, im, _pad;                   //  4 B
    uint8_t  opcodes[4];                       //  4 B  (bytes at PC)
    uint16_t spContent;                        //  2 B  (word at SP)
    uint8_t  slots[4];                         //  4 B  (bank per slot)
};
```

Every subsequent `getHistoryEntry` in that frame is then an **array read**.
Crossing to the previous frame replays that frame once (0.67 ms) to fill its
ring. This also pins the index↔instruction mapping exactly (the ring *is* the
M1 sequence), removing the §3 semantics caveat.

**Richer records are cheap (recommended).** Because the ring is filled during a
replay pass that already re-executes every instruction, capturing each
instruction's **memory and port accesses (address + value + R/W)** alongside the
CPU record adds only a handful of bytes and a couple of stores per instruction —
no extra replay. That gives O(1) reverse answers to "what did this instruction
read/write / what was at this address / what went to this port at this moment"
directly from cache, without touching the TTD write/IO journal. Budget ~8–16 B
per instruction for a small per-instruction access list (most instructions touch
0–2 bytes + at most one port), i.e. a ~50–56 B record.

### Projected performance vs current (from measured numbers)

The ring changes the cost model from **"per instruction stepped"** to **"per
distinct frame entered"**: the first reverse step into a frame pays one
0.67 ms fill, then every step, prefetch entry, or reverse-continue check inside
that frame is a 0.80 ns array read.

| Realistic action (within one frame) | Current (measured) | Ring cache (projected) |
|--------------------------------------|--------------------|------------------------|
| First reverse step into a frame | 1.37 ms | **0.67 ms** (fill) |
| Each further step in that frame | 1.37 ms | **0.80 ns** |
| 10-entry stop prefetch | ~14 ms | **0.67 ms** (fill) + ~8 ns |
| Step back ~100 in the frame | ~140 ms | **0.67 ms** + ~80 ns |
| Reverse-continue scanning ~1,000 | ~1.4 s | **0.67 ms** + ~0.8 µs |
| Reverse-continue across the frame (~13k) | ~18 s | **0.67 ms** + ~11 µs |

`BM_TTD_Ring_ReadEntry` measured a full 40-byte record read at **0.80 ns**; the
fill (`BM_TTD_Busy_RingFill_Frame`, one forward frame replay) at **0.67 ms**.
The win grows with how far an action reaches inside the frame: negligible for a
single step, decisive for reverse-continue and scrubbing (seconds → sub-ms).

(Deep random *jumps* to a far index are a separate matter and don't need the
ring at all: the current adapter naively single-steps to reach a far index; a
direct `SeekTo(targetFrame)` — ~one frame replay, up to a full I-frame interval
— reaches any point in ~ms. Fixing the adapter's cache-fill to seek instead of
single-step is worth doing regardless of the ring.)

### Memory cost

Per-instruction record ~40 B (CPU only) to ~50–56 B (with the memory/port
access list). At the measured 13,562 instr/frame:

- **One frame:** 13,562 × 40 B ≈ **0.54 MB** (CPU only); ≈ **0.68–0.76 MB** with
  accesses. Typical (non-tight-loop) code runs fewer instr/frame, so this is an
  upper-ish bound.
- **On-demand single frame** (fill on enter, discard on frame cross): **~0.5–0.8 MB**
  steady — negligible next to the TTD page store, which is already several MB
  over tens of recorded frames.
- **Windowed** (keep the last _W_ browsed frames for smooth back-and-forth
  scrubbing): _W_ × ~0.6 MB — e.g. 8 frames ≈ **~5 MB**.

Either way the ring's footprint is bounded by the browse window (1 to a few
frames), **not** by history depth, which remains the full TTD session.

### Recommended sequence (when we act on this)

1. **Baseline is committed** — the `BM_TTD_Busy_*` / `BM_TTD_Ring_ReadEntry`
   cases above are the reproducible baseline. Any ring implementation is
   measured against them (latency + peak RSS via the session heap counters).
2. Implement the **on-demand single-frame decode ring** in `TimeTravelManager`:
   fill it during the existing replay pass (capturing CPU + memory/port
   accesses), have the DeZog adapter read records instead of re-seeking.
   Expected: within-frame stepping/reverse-continue drops from ~1.37 ms *per
   instruction reached* to a one-time **0.67 ms** frame fill + **0.80 ns/entry**
   (reverse-continue over a subroutine: ~1.4 s → sub-ms); cost ~0.5–0.8 MB per
   browsed frame.
3. Add a small **windowed** cache (a few MB) only if back-and-forth scrubbing
   across frame boundaries feels laggy.

Do **not** implement before an implementation is measured against step 1's
baseline.
