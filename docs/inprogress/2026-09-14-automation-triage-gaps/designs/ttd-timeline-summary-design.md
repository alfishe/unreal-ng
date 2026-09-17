# Technical Design Document: TD-5 TTD Timeline Summary Endpoint

**Document Path:** [`docs/inprogress/2026-09-14-automation-triage-gaps/designs/ttd-timeline-summary-design.md`](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/docs/inprogress/2026-09-14-automation-triage-gaps/designs/ttd-timeline-summary-design.md)  
**Feature Target:** TD-5 (P1) — Timeline Summary Endpoint (`GET /ttd/timeline`)  
**Status:** Architectural Specification  
**Target Interfaces:** WebAPI, MCP, CLI, Lua, Python  
**Engine Overhead:** 0% new capture cost (operates purely on pre-existing checkpoint & journal metadata)

---

## 1. Executive Summary & Problem Statement

### 1.1 Problem (G-2)
When an AI agent or human developer inspects a long Time Travel Debugging (TTD) session (e.g. 5,000 to 50,000 frames), identifying *where* significant execution or memory mutation occurred currently requires blind guesswork or expensive per-byte reverse queries (`find-last`).

### 1.2 Objective
Provide a macro "bird's-eye view" of an entire TTD session in a single ~200-token summary call. The endpoint computes per-frame or downsampled bucket activity metrics—dirty RAM page count, journal write ticks, keyframe indicators, and external marker flags—giving immediate quantitative telemetry for depackers, I/O routines, and phase transitions.

---

## 2. Core Engine Architecture & Data Model

### 2.1 Pre-Existing Engine Metadata
An audit of `TimeTravelManager` (`core/src/debugger/ttd/timetravelmanager.h`) and `TTDCheckpoint` (`core/src/debugger/ttd/ttdcheckpoint.h`) confirms that all required timeline metrics are already recorded during emulation:

1. **`_timeline` Storage (`std::vector<TTDCheckpoint>`):**
   - Direct $\mathcal{O}(1)$ contiguous random access to any frame $F \in [0, N-1]$.
2. **Keyframe Flag (`TTDFrameKind frameKind`):**
   - Identifies I-frames (`KeyFrame`) vs P-frames (`DeltaFrame`).
3. **Dirty RAM Page Count (`std::vector<TTDPageRef> ramPages`):**
   - `ramPages.size()` gives the count of modified physical 4 KB sub-page slots in frame $F$.
4. **Write Tick Offset (`uint64_t writeJournalOffset`):**
   - Monotonically increasing offset into `TTDWriteJournal`.
   - The write tick count in any range $[F_1, F_2]$ is computed instantly in $\mathcal{O}(1)$ time:
     $$\text{WriteCount}(F_1, F_2) = \text{checkpoint}[F_2].\text{writeJournalOffset} - \text{checkpoint}[F_1].\text{writeJournalOffset}$$
5. **External Event Journal & Markers (`TTDExternalEventJournal`):**
   - Tracks tape edges, disk sector operations, breakpoint hits, and user bookmarks.

---

## 3. Algorithmic Design & Downsampling

### 3.1 Downsampling & Bucketing Strategy
To keep LLM prompt token consumption under control ($\approx 200$ tokens for 50,000 frames), the endpoint accepts an optional `limit` parameter (default `500` entries) and automatically downsamples wide ranges.

$$\text{range} = F_{\text{to}} - F_{\text{from}} + 1$$
$$\text{step} = \max\left(1, \left\lceil \frac{\text{range}}{\text{limit}} \right\rceil\right)$$

For each bucket $B_k = [F_{\text{from}} + k \cdot \text{step}, \min(F_{\text{from}} + (k+1) \cdot \text{step} - 1, F_{\text{to}})]$:
- `write_count`: `checkpoint[bucket_end].writeJournalOffset - checkpoint[bucket_start].writeJournalOffset`
- `dirty_pages`: $\max_{f \in B_k} (\text{ramPages.size()})$
- `has_marker`: `true` if any external event or bookmark falls within $B_k$
- `is_keyframe`: `true` if any checkpoint in $B_k$ is an I-frame
- `activity_score`: Normalized activity float $[0.0, 1.0]$ relative to the peak write/dirty burst in the requested window.

### 3.2 Time & Space Complexity
- **Time Complexity:** $\mathcal{O}(\text{limit})$ bounds overall execution to $\le 0.5\text{ ms}$ regardless of session length.
- **Space Complexity:** Bounded strictly by `limit` entries ($\le 15\text{ KB}$ JSON memory overhead).

---

## 4. Multi-Surface API Specification

### 4.1 WebAPI Endpoint (`POST /api/v1/emulator/{id}/ttd/timeline` / `GET`)
- **Query / Body Parameters:**
  - `from_frame` (uint64, default `0`)
  - `to_frame` (uint64, default `session_end`)
  - `step` (uint32, optional explicit step)
  - `limit` (uint32, default `500`, max `2000`)

- **JSON Response Schema:**
```json
{
  "from_frame": 0,
  "to_frame": 5000,
  "total_frames": 5000,
  "sample_step": 10,
  "entry_count": 500,
  "peak_writes_per_step": 1450,
  "peak_dirty_pages": 28,
  "timeline": [
    {
      "frame": 0,
      "is_keyframe": true,
      "dirty_pages": 4,
      "write_count": 12,
      "has_marker": true,
      "activity_score": 0.01
    },
    {
      "frame": 1420,
      "is_keyframe": false,
      "dirty_pages": 28,
      "write_count": 1450,
      "has_marker": false,
      "activity_score": 1.00
    }
  ]
}
```

### 4.2 MCP Integration (`core/automation/mcp/src/mcp-tools.cpp`)
- **Dynamic Router:** Exposed via `search_api` / `invoke_api` on route `/api/v1/emulator/{id}/ttd/timeline`.
- **`inspect_state` Aspect:** Adds `aspect="ttd_timeline"` returning JSON plus a compact ASCII text sparkline summary:
  ```
  [ttd_timeline] Range 0..5000 (step 50, peak 1450 writes/frame):
  [0000..0500]  ▃ █ ▃   (Spike at frame 1400..1650: dirty=28 pages, writes=1450)
  ```

### 4.3 CLI Interface (`core/automation/cli/src/commands/cli-processor-ttd.cpp`)
- **Syntax:** `ttd timeline [--from <F>] [--to <T>] [--step <S>] [--limit <N>]`
- **Output:** Formatted tabular view with ASCII activity graph visualizing unpack bursts.

### 4.4 Lua & Python Interfaces
- **Lua:** `emu.ttd_timeline{from_frame=0, to_frame=5000, step=10, limit=500}` returning a array of table entries.
- **Python:** `emu.ttd_timeline(from_frame=0, to_frame=5000, step=10, limit=500)` returning a list of dictionaries.

---

## 5. Acceptance Criteria & Test Strategy

1. **`umt23x` Test Fixture Validation:**
   - Loading `umt23x` session fixture into `TimeTravelManager`.
   - `GET /ttd/timeline` returns a distinct activity spike (`dirty_pages = 28`, `write_count = 1450`) during frames 1400–1650 (the depacker phase) without executing any `find-last` reverse queries.
2. **Boundary & Downsampling Unit Tests:**
   - Verify `step` calculation for ranges exceeding `limit`.
   - Verify empty timeline behavior.
   - Verify single-frame requests (`from_frame == to_frame`).

---

## 6. Required TTD Changes & Comprehensive File Inventory

### 6.1 Required Core Engine Changes in TTD (`core/src/debugger/ttd/`)

To support timeline queries without new capture cost, the following additions are required in the core TTD engine:

1. **`core/src/debugger/ttd/timetravelmanager.h`**:
   - Define data structs `TTDTimelineEntry` and `TTDTimelineSummary`.
   - Declare public query method:
     `TTDTimelineSummary GetTimelineSummary(uint64_t fromFrame = 0, uint64_t toFrame = UINT64_MAX, uint32_t limit = 500) const;`
   - Declare helper:
     `bool HasExternalEventInRange(uint64_t fromFrame, uint64_t toFrame) const;`

2. **`core/src/debugger/ttd/timetravelmanager.cpp`**:
   - Implement `GetTimelineSummary(...)`:
     - Range bounds clamping against `_timeline.size()`.
     - Downsampling bucket calculation (`step = ceil(range / limit)`).
     - Subtract monotonic journal offsets (`endCp.writeJournalOffset - startCp.writeJournalOffset`) for $\mathcal{O}(1)$ bucket write totals.
     - Scan `cp.ramPages.size()` for peak dirty RAM pages.
     - Normalize relative `activityScore` $[0.0, 1.0]$.

### 6.2 Full Inventory of Files to be Modified or Created

| File | Change Category | Description of Changes |
|:---|:---|:---|
| **`core/src/debugger/ttd/timetravelmanager.h`** | [MODIFY] Core Engine Header | Struct definitions (`TTDTimelineEntry`, `TTDTimelineSummary`) and `GetTimelineSummary` declaration. |
| **`core/src/debugger/ttd/timetravelmanager.cpp`** | [MODIFY] Core Engine Impl | $\mathcal{O}(\text{limit})$ `GetTimelineSummary` downsampling logic. |
| **`core/automation/webapi/src/api/ttd_api.cpp`** | [MODIFY] WebAPI Surface | Route handler for `GET /api/v1/emulator/{id}/ttd/timeline` and query parameter parsing. |
| **`core/automation/webapi/src/openapi/openapi_ttd.inc`** | [MODIFY] OpenAPI Spec | Path documentation & parameter descriptions for `/ttd/timeline`. |
| **`core/automation/webapi/src/openapi/openapi_schemas.inc`** | [MODIFY] OpenAPI Spec | Schema definitions for `TTDTimelineSummary` and `TTDTimelineEntry`. |
| **`core/automation/mcp/src/mcp-tools.cpp`** | [MODIFY] MCP Surface | Add `ttd_timeline` aspect to `inspect_state` tool + ASCII sparkline renderer. |
| **`core/automation/cli/include/cli-processor.h`** | [MODIFY] CLI Header | Command declaration for `ExecuteTtdTimeline(...)`. |
| **`core/automation/cli/src/commands/cli-processor-ttd.cpp`** | [MODIFY] CLI Surface | Command parser for `ttd timeline` (`--from`, `--to`, `--step`, `--limit`) + tabular/sparkline formatter. |
| **`core/automation/lua/src/emulator/lua_emulator.h`** | [MODIFY] Lua Surface | `emu.ttd_timeline{...}` table argument function registration. |
| **`core/automation/python/src/emulator/python_emulator.h`** | [MODIFY] Python Surface | `emu.ttd_timeline(...)` pybind11 method registration. |
| **`core/tests/debugger/ttd/ttdtimelinesummary_test.cpp`** | [NEW] Core Unit Tests | Unit tests for `GetTimelineSummary` bounds, downsampling step logic, and journal offset subtraction. |
| **`core/tests/debugger/ttd/ttdautomationcontract_test.cpp`** | [MODIFY] Automation Contract | Integration & contract verification tests across WebAPI, CLI, Lua, Python. |
| **`docs/emulator/design/control-interfaces/command-interface.md`** | [MODIFY] Documentation | Document `ttd timeline` CLI command. |
| **`docs/emulator/design/control-interfaces/lua-interface.md`** | [MODIFY] Documentation | Document `ttd_timeline` Lua method. |
| **`docs/emulator/design/control-interfaces/python-interface.md`** | [MODIFY] Documentation | Document `ttd_timeline` Python method. |

---

## 7. Storage Overhead & Performance Impact Analysis

### 7.1 TTD Stream & Dump File Size Overhead: **0 Bytes (0% increase)**
- **Zero Schema Additions:** Timeline generation consumes metrics already present in every `TTDCheckpoint` (`frameKind`, `writeJournalOffset`, `ramPages.size()`) and `TTDExternalEventJournal`.
- **Stream Parity:** No new fields are appended to the serialized `.ttd` stream format or disk snapshots.
- **Overhead:** Exactly **0 bytes** increase in recording file size or memory footprint.

### 7.2 Emulation Capture Overhead: **0% CPU Impact**
- **Recording Overhead:** Because no new fields are captured or written during frame execution, the recording loop runs at **100% full speed with zero performance penalty**.

### 7.3 Query Latency, Memory Footprint & Unbounded Session Handling

| Metric | Estimated Value | Technical Analysis & Constraints |
|:---|:---|:---|
| **Query Latency (50,000 frames $\rightarrow$ 500 limit)** | **$\sim 0.15 \text{ ms}$** ($< 1 \text{ ms}$) | $\mathcal{O}(\text{limit})$ subtraction of 64-bit monotonic `writeJournalOffset` values eliminates frame scanning. |
| **Response Memory Footprint (Clamped)** | **$\sim 16 \text{ KB}$ C++ / $\sim 18 \text{ KB}$ JSON** | Bounded strictly when request `limit` is enforced (e.g., max `limit = 2000`). |
| **Response Memory Footprint (Unbounded $N \rightarrow \infty$)** | **$\mathcal{O}(N)$ ($\sim 32 \text{ bytes/frame}$)** | For long/infinite TTD streams, raw un-bucketed timeline queries scale linearly ($3.2 \text{ MB}$ per $100,000$ frames). **Mandatory server-side clamping (`limit \le 2000`) and window pagination (`from_frame`/`to_frame`) prevent memory exhaustion.** |
| **Numeric Overflow Safety** | **64-bit `uint64_t` counters** | `writeJournalOffset` uses 64-bit integers ($\approx 1.8 \times 10^{19}$ ticks), safely handling continuous multi-day recordings without wrap-around. |
| **LLM Context Token Consumption** | **$\sim 200 - 300$ tokens** | Downsampled JSON / ASCII sparkline summarizes arbitrary timeline spans within prompt limits. |


