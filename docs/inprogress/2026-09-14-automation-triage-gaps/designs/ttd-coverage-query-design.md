# Technical Design Document: TD-7 TTD Coverage Index Query

**Document Path:** `docs/inprogress/2026-09-14-automation-triage-gaps/designs/ttd-coverage-query-design.md`
**Feature Target:** TD-7 (P2) — Query the Coverage Index
**Status:** Architectural Specification
**Target Interfaces:** WebAPI, MCP, CLI, Lua, Python
**Engine Overhead:** 0% new capture cost (queries pre-existing compressed index)
**Parent:** [ttd-coverage-evaluation.md](../ttd-coverage-evaluation.md) gap G-5

---

## 1. Executive Summary

### 1.1 What This Enables

The TTD engine already maintains a per-frame coverage index — for every
recorded frame, it knows exactly which physical addresses were **executed**,
**written**, and **read**. This index currently powers internal frame-skipping
in `FindLastAccess` and `reverse-continue`, but is completely invisible to
automation clients. TD-7 exposes it.

### 1.2 Why This Matters for AI Agents

Unlike TD-5's proposed "timeline summary" (which attempted to give macro-level
write counts), TD-7 answers semantically precise questions that directly drive
RE workflows:

| Agent Question | TD-7 Query | Without TD-7 |
|:---|:---|:---|
| "Which frames ran code in 0xBF00..0xBFFF?" | `coverage/scan` → list of frame numbers | Replay every frame and check — $O(N)$ frames × 1.3 ms each |
| "Did frame 1420 touch VRAM (0x4000..0x5AFF)?" | `coverage/probe` → boolean | Seek + replay + check registers — ~2 ms + inspection overhead |
| "Where does the depacker start and end?" | Two `coverage/scan` calls | Iterative `find-last` binary search, ~5-10 round trips |
| "Show me the execution heatmap over 1000 frames" | `coverage/summary` → per-bucket distinct-address counts | Impossible without replay |

The key insight: `find-last` finds **one** matching access (the last). Coverage
scan finds **all** frames containing an access. This is the difference between
"where did X last happen" and "where does X happen" — the latter is the
fundamental RE orientation question.

### 1.3 Cost Model

The index is already being captured (measured overhead: ~50 µs/frame, already
amortized into TTD's capture budget). Queries decompress 64-frame blocks
(~1-5 KB each) and scan sorted key arrays — sub-millisecond per block.

---

## 2. Existing Engine Internals (Verified)

### 2.1 `TTDCoverageIndex` ([ttdcoverageindex.h](core/src/debugger/ttd/ttdcoverageindex.h))

| Aspect | Detail |
|:---|:---|
| **Key space** | 22-bit physical keys: `(physPage << 14) | (z80Addr & 0x3FFF)` |
| **Three kinds** | `Executed` (M1 fetches), `Written` (memory writes), `Read` (memory reads) |
| **Storage** | 64-frame compressed blocks; delta-varint keys per frame inside each block |
| **Size** | ~307 B/frame compressed (~54 MB/hour) — already measured on real demos |
| **Distinct keys/frame** | Executed: ~318, Written: ~75, Read: ~749 (measured) |

### 2.2 Existing Query Primitives (Internal, Not Exposed)

| Method | Signature | Purpose |
|:---|:---|:---|
| `FindLastFrameTouching` | `(kind, key, beforeFrame) → frame` | Backward linear scan for exact key match |
| `FrameTouches` | `(kind, frame, key) → bool` | Exact key membership test for one frame |
| `FrameMayContain` | `(kind, frame, offsetRange, page?) → bool` | Conservative range containment test |
| `CoversFrame` | `(kind, frame) → bool` | "Was this frame observed by the index?" |
| `CoveredRange` | `(kind) → [firstFrame, lastFrame]` | Index observation bounds |
| `SealedFrameCount` | `(kind) → count` | Number of indexed frames |
| `EncodedBytes` | `(kind) → bytes` | Compressed storage cost |

### 2.3 Accessor from `TimeTravelManager`

```cpp
// Already public:
inline const TTDCoverageIndex& GetCoverageIndex() const { return _coverageIndex; }
```

No new core engine methods are needed. All queries compose from the
existing primitives above.

### 2.4 Limitations (Important for Design)

1. **Loaded sessions serve coverage too.** The coverage index **is** persisted
   in `.ttd` files (shipped in `4f501d13`, before TD-7 landed):
   `DeserializeSession` restores `_coverageIndex` alongside the timeline, so
   queries against loaded sessions return real data, with the covered window
   echoed as `covered_from`/`covered_to` (the request window is clamped to it).
   An unrecorded or pre-coverage-format session still reports
   `index_available: false` — a clear "no index" signal, not empty results.

2. **Physical keys, not Z80 addresses.** A query for Z80 address 0xC000 must
   know (or enumerate) which physical page(s) map there. The automation layer
   must accept Z80 addresses as the user-facing input and translate through the
   active paging state — or accept physical `(page, offset)` pairs directly.

3. **`FindLastFrameTouching` is single-key, linear scan.** For range queries
   spanning many addresses, we must iterate the decoded frame's key set against
   the range rather than calling `FindLastFrameTouching` per address.

4. **No write/read *values* in the index.** The coverage index records
   *which* addresses were touched, not *what* was written. Value-aware queries
   still require `FindLastAccess` with its probe replay path.

---

## 3. Proposed API Surface

### 3.1 Query Types

Three query modes, each serving a distinct RE workflow:

#### 3.1.1 `coverage/probe` — "Did frame F touch address range R?"

Boolean point query. Fastest path.

```
GET /api/v1/emulator/{id}/ttd/coverage/probe
  ?frame=1420
  &kind=executed          # executed | written | read
  &addr_from=0xBF00
  &addr_to=0xBFFF
  [&phys_page=5]          # optional: restrict to a specific physical page
```

Response:
```json
{
  "frame": 1420,
  "kind": "executed",
  "addr_from": "0xBF00",
  "addr_to": "0xBFFF",
  "touched": true,
  "index_available": true
}
```

**Implementation:** Wraps `FrameMayContain`. $O(\text{keys\_in\_frame})$ —
sub-millisecond.

#### 3.1.2 `coverage/scan` — "Which frames in [F₁, F₂] touched range R?"

Multi-frame scan. The high-value query for RE orientation.

```
GET /api/v1/emulator/{id}/ttd/coverage/scan
  ?from_frame=0
  &to_frame=5000
  &kind=executed
  &addr_from=0xBF00
  &addr_to=0xBFFF
  [&phys_page=5]
  [&limit=200]            # max frames returned (default 200, max 1000)
```

Response:
```json
{
  "kind": "executed",
  "addr_from": "0xBF00",
  "addr_to": "0xBFFF",
  "scanned_frames": 5000,
  "matching_frames": 250,
  "frames": [1400, 1401, 1402, ..., 1649],
  "first_match": 1400,
  "last_match": 1649,
  "truncated": false,
  "index_available": true
}
```

**Implementation:** Walk blocks in `[from_frame, to_frame]`, decompress each,
test each frame's key set against the range. Returns the first `limit` matching
frame numbers. Cost: $O(\text{blocks}) \times O(\text{decompress}) + O(\text{frames\_in\_range}) \times O(\text{keys\_per\_frame})$.

For a 5000-frame scan with 64-frame blocks: ~78 block decompressions at ~50 µs
each ≈ 4 ms. Well within budget.

#### 3.1.3 `coverage/summary` — "Activity heatmap over [F₁, F₂]"

Aggregated activity counts per bucket — the useful replacement for TD-5's
broken timeline summary.

```
GET /api/v1/emulator/{id}/ttd/coverage/summary
  ?from_frame=0
  &to_frame=5000
  [&kind=executed]        # optional; omit for all three kinds
  [&bucket_size=50]       # frames per bucket (default: auto from limit)
  [&limit=100]            # max buckets (default 100, max 500)
```

Response:
```json
{
  "from_frame": 0,
  "to_frame": 5000,
  "bucket_size": 50,
  "bucket_count": 100,
  "buckets": [
    {
      "frame_start": 0,
      "frame_end": 49,
      "executed_distinct": 318,
      "written_distinct": 75,
      "read_distinct": 749,
      "has_keyframe": true
    },
    {
      "frame_start": 1400,
      "frame_end": 1449,
      "executed_distinct": 1247,
      "written_distinct": 892,
      "read_distinct": 2103,
      "has_keyframe": true
    }
  ],
  "index_available": true
}
```

**Implementation:** For each bucket, decode frames and count union-distinct
keys per kind. The `executed_distinct` spike at the depacker range reveals
the burst without knowing any addresses — the agent sees "frames 1400-1649
executed 4x more unique addresses than surrounding frames" and knows where
to look.

> [!IMPORTANT]
> **This is the real timeline summary.** TD-5 tried to use `ramPages.size()`
> (constant per model — wrong) and `writeJournalOffset` (ring buffer wraps —
> wrong for long sessions). `coverage/summary` uses actually-correct per-frame
> distinct address counts from the decompressed coverage sets.

#### 3.1.4 Validation and Covered-Window Contract (2026-09-17 fixes)

Verification of the initial TD-7 implementation surfaced four defects, fixed
and regression-tested the same day:

- **Per-frame availability.** `probe` reports `index_available: false` for any
  frame outside the covered range instead of leaking `FrameMayContain`'s
  conservative `true` as a false-positive `touched`.
- **Covered-window echo.** `scan` clamps `[from_frame, to_frame]` to the index
  covered range and echoes the effective window as `covered_from`/`covered_to`
  (summary echoes the union across covered kinds). A request window fully
  outside coverage scans 0 frames but still reports the covered window.
- **Hard validation.** All three endpoints return **400** instead of silently
  defaulting: missing `frame` (probe), invalid `kind`, non-numeric
  frame/addr/limit/bucket values, `limit < 1`, `phys_page > 255`, and
  `addr_from > addr_to` (which previously fell back to the whole bank).

---

## 4. Multi-Surface Parity

### 4.1 WebAPI

Three new routes under `GET /api/v1/emulator/{id}/ttd/coverage/`:
- `probe` — boolean point query
- `scan` — multi-frame range scan
- `summary` — bucketed activity heatmap

All surfaces serve loaded `.ttd` sessions identically (coverage is persisted
  in the dump format), echo the covered window on scan/summary, and surface the
  WebAPI's 400 validation errors verbatim (MCP: `isError` + `HTTP 400` text).

### 4.2 MCP (`time_travel` tool)

New actions on the existing `time_travel` tool:

```json
{"action": "coverage_probe", "frame": 1420, "kind": "executed", "addr_from": "0xBF00", "addr_to": "0xBFFF"}
{"action": "coverage_scan", "from_frame": 0, "to_frame": 5000, "kind": "executed", "addr_from": "0xBF00", "addr_to": "0xBFFF"}
{"action": "coverage_summary", "from_frame": 0, "to_frame": 5000, "limit": 100}
```

The `coverage_summary` human text includes a compact ASCII sparkline:
```
Coverage 0..5000 (50/bucket):
  exec  ▁▁▁▁▁▁▁▁▃▃▃▃▃███████▃▃▃▁▁▁▁▁▁▁
  write ▁▁▁▁▁▁▁▁▁▁▁▁▁▃██████▃▁▁▁▁▁▁▁▁▁▁
  read  ▂▂▂▂▂▂▂▂▃▃▃▃▃███████▃▃▃▂▂▂▂▂▂▂▂
                              ^ depacker burst (frames 1400-1650)
```

### 4.3 CLI

```
ttd coverage probe --frame 1420 --kind executed --from 0xBF00 --to 0xBFFF
ttd coverage scan --from-frame 0 --to-frame 5000 --kind executed --from 0xBF00 --to 0xBFFF
ttd coverage summary [--from-frame 0] [--to-frame 5000] [--bucket 50]
```

### 4.4 Lua

```lua
emu.ttd_coverage_probe{frame=1420, kind="executed", addr_from=0xBF00, addr_to=0xBFFF}
emu.ttd_coverage_scan{from_frame=0, to_frame=5000, kind="executed", addr_from=0xBF00, addr_to=0xBFFF}
emu.ttd_coverage_summary{from_frame=0, to_frame=5000, limit=100}
```

### 4.5 Python

```python
emu.ttd_coverage_probe(frame=1420, kind="executed", addr_from=0xBF00, addr_to=0xBFFF)
emu.ttd_coverage_scan(from_frame=0, to_frame=5000, kind="executed", addr_from=0xBF00, addr_to=0xBFFF)
emu.ttd_coverage_summary(from_frame=0, to_frame=5000, limit=100)
```

---

## 5. Core Implementation

### 5.1 New Method: `TimeTravelManager::QueryCoverage`

A thin orchestration method that translates Z80 address ranges into physical
coverage keys and delegates to the existing `TTDCoverageIndex` primitives.

```cpp
/// @brief Result of a coverage scan query.
struct TTDCoverageScanResult
{
    std::vector<uint64_t> matchingFrames;   ///< Frame numbers that matched
    uint64_t scannedFrames = 0;
    uint64_t firstMatch = 0;
    uint64_t lastMatch = 0;
    bool truncated = false;                 ///< True if limit was hit
    bool indexAvailable = false;            ///< False for loaded sessions
};

/// @brief Result of a coverage summary query.
struct TTDCoverageSummaryBucket
{
    uint64_t frameStart = 0;
    uint64_t frameEnd = 0;
    uint32_t executedDistinct = 0;
    uint32_t writtenDistinct = 0;
    uint32_t readDistinct = 0;
    bool hasKeyframe = false;
};
```

### 5.2 Z80-to-Physical Translation

The critical bridge. The agent says "0xBF00..0xBFFF" — this is a Z80 address
in the range 0x8000-0xBFFF, which maps to different physical pages depending
on paging state. The coverage index stores physical keys.

**Strategy:** Accept Z80 addresses as the primary input (that's what agents
know). For `probe` on a specific frame, use that frame's checkpoint paging
state to resolve the physical page. For `scan` across many frames, accept an
**optional** `phys_page` filter — when provided, it restricts to that page;
when omitted, the offset portion of the key is matched against any page
(conservative: may return false positives for writes to the same offset on
different pages, but never false negatives).

This matches the existing `FrameMayContain(kind, frame, offsetLow, offsetHigh,
hasPage=false, page=0)` call pattern — already tested and used in `FindLastAccess`.

### 5.3 Performance Budget

| Query | Typical Cost | Bound |
|:---|:---|:---|
| `probe` (1 frame) | <100 µs | Decompress 1 block + scan ~318 keys |
| `scan` (5000 frames) | ~4 ms | 78 block decompressions + key scans |
| `scan` (50000 frames) | ~40 ms | 781 blocks — still fast enough |
| `summary` (5000 frames, 100 buckets) | ~10 ms | Full decompress + union count |

All well within WebAPI response budget (target: <100 ms).

---

## 6. File Inventory (Changes Required)

### 6.1 Core Engine (New Methods Only, No Format Changes)

| File | Change |
|:---|:---|
| `timetravelmanager.h` | Add `QueryCoverageProbe`, `QueryCoverageScan`, `QueryCoverageSummary` + result structs |
| `timetravelmanager.cpp` | Implement the three methods using existing `TTDCoverageIndex` API |

> [!NOTE]
> **Zero changes to `TTDCoverageIndex` itself.** All required primitives
> (`FrameMayContain`, `MaterializeBlock`, `DecodeFrameFromCache`) already exist.
> No .ttd format changes. No new capture overhead.

### 6.2 Automation Surfaces

| File | Change |
|:---|:---|
| `ttd_api.cpp` | 3 new routes: `GET /ttd/coverage/{probe,scan,summary}` |
| `openapi_ttd.inc` | Schema for the three endpoints |
| `mcp-tools.cpp` | 3 new actions on `time_travel` tool |
| `cli-processor-ttd.cpp` | `ttd coverage {probe,scan,summary}` subcommands |
| `lua_emulator.h` | `ttd_coverage_probe/scan/summary` bindings |
| `python_emulator.h` | Same three methods |

### 6.3 Tests

| File | Content |
|:---|:---|
| `ttdcoveragequery_test.cpp` (new) | Unit tests for the three query methods |
| `ttdautomationcontract_test.cpp` | WebAPI/CLI integration tests |

---

## 7. Non-Goals (Explicit)

1. **Coverage persistence in .ttd files.** ~~Not included in the dump format.~~
   **Shipped in `4f501d13`** (predates TD-7): `_coverageIndex` serializes with
   the session, so loaded sessions serve queries. The original cost estimate
   (~54 MB/hour vs ~28 MB/hour) motivated keeping it out of scope for TD-7
   itself; that concern is now historical — measured dump sizes on a 3.7k-frame
   session add roughly 20 KB of coverage payload.

2. **Value-aware coverage queries.** "Which frames wrote 0x42 to 0xC000" is
   not answerable from the coverage index (it tracks *addresses*, not *values*).
   Use `find-last` with `value` filter for that — it replays and checks.

3. **Real-time streaming.** Coverage data is available after `SealFrame` (i.e.,
   at the frame boundary). No sub-frame granularity.

---

## 8. Comparison: Why TD-7 Succeeds Where TD-5 Failed

| Aspect | TD-5 (Timeline Summary) | TD-7 (Coverage Query) |
|:---|:---|:---|
| **`dirty_pages` metric** | ❌ `ramPages.size()` is constant per model — always equals `_modelRamPages` | ✅ Actual per-frame distinct address counts from decompressed coverage sets |
| **Write count metric** | ❌ `writeJournalOffset` wraps in the 64 MB ring buffer — garbage for sessions >50s | ✅ Distinct written-address count from the coverage index (no ring buffer) |
| **Answers "where did X happen"** | ❌ Only "something was busy around frame N" — still needs `find-last` to learn *what* | ✅ Directly: "code in 0xBF00..0xBFFF executed in frames 1400-1649" |
| **Loaded sessions** | ❌ Claims to work on loaded sessions but `writeJournalOffset` is not loaded | ✅ Explicitly reports `index_available: false` — no silent wrong answers |
| **Token efficiency** | ~200 tokens for a generic heatmap | ~100 tokens for a targeted boolean; ~200 tokens for a precise frame list |
| **New capture cost** | 0% | 0% |
| **Engine changes** | Requires adding `dirtyPageCount` field to `TTDCheckpoint` (format change) | Zero engine changes — all primitives exist |

> [!TIP]
> **TD-5 can be salvaged** as a thin wrapper around `coverage/summary` (§3.1.3)
> — that *is* the correctly-implemented timeline summary. The original TD-5
> endpoint (`GET /ttd/timeline`) could simply be an alias for
> `GET /ttd/coverage/summary` with all three kinds aggregated.

---

## 9. Acceptance Criteria

1. **Probe query:** On a live recording, `coverage/probe` for the ROM entry
   point (PC=0x0000, kind=executed) returns `touched: true` for frame 0 and
   `touched: false` for an address never executed.

2. **Scan query:** On the `umt23x` session, `coverage/scan` with
   `kind=executed, addr_from=0xBF00, addr_to=0xBFFF` returns a contiguous
   block of frames matching the depacker burst. The `first_match` and
   `last_match` frame numbers bracket the depacker's execution window.

3. **Summary query:** `coverage/summary` for the full session shows a clear
   spike in `executed_distinct` at the depacker frames — matching the
   `coverage/scan` results without needing to know any addresses.

4. **Loaded sessions serve coverage:** a dump/load round-trip restores the
   covered window (`covered_from`/`covered_to` match the live session) and
   `probe`/`scan`/`summary` answer identically. An unrecorded session still
   returns `index_available: false` (not empty results).

5. **All five surfaces:** WebAPI, MCP, CLI, Lua, Python return equivalent
   results.

6. **Per-frame honesty (2026-09-17):** probing a frame outside the covered
   range returns `index_available: false, touched: false` — never a
   conservative false positive.

7. **Validation (2026-09-17):** missing `frame`, `kind=bogus`, `addr_from >
   addr_to`, `phys_page > 255`, `limit < 1` and non-numeric values all fail
   with HTTP 400 and a descriptive message (all five surfaces).
