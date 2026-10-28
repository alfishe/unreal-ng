# TTD v2 queries: feedback from game analysis

A game-analysis agent used TTD v2 to find event addresses (coverage scan over rare code, find-last on a speed byte,
reverse-continue). It works, and the answers reproduce across two independent recordings. The loop around each
query costs too much, though. This folder holds the fixes.

Order (owner, 2026-10-28):

1. **Phase 1:** query fixes 1-6 below, full tests, regression check, land on master and push.
2. **Phase 2:** a replay query engine ("replay a window with observers, collect every hit"). This phase starts
   only after phase 1 has landed. It needs a POC in `tools/poc/`, A/B benchmarks and a quality check.

No phase 1 change touches the recording hot path. Every change is opt-in or fixes a wrong value, so existing
clients keep their answers (QR-8).

## The feedback and what the code does

| # | Feedback | Finding in the code |
|---|----------|---------------------|
| 1 | coverage/scan gives only a count and the first/last frame | The WebAPI already returns `frames[]` (200 by default, at most 1000). The MCP text drops it, and an agent may see only the text. `last_match` is wrong when the list is truncated. The scan keeps the oldest frames, so the newest are cut. |
| 2 | Without `phys_page`, addresses alias across pages | This is by design for the coverage queries: keys are page + offset, and without a page every page matches. reverse-continue has no `phys_page` at all: it matches `pc` only, although every M1 record carries its page. ROM cannot be named (`phys_page` is 0..255). |
| 3 | reverse-continue: one hit per call | Each candidate frame's M1 list is already enumerated in full, but all hits except the newest are dropped. Every hit also costs a seek and a present (4-8 ms). |
| 4 | find-last returns writes of the same value | The write record has no old value: memory passes a literal 0. A loop must dedupe the results and step `before_tin` back itself. |
| 5 | memory-at offsets are physical; 64 KB per call | Physical by design (memory spaces). There is no Z80 view and no paging in the reply. |
| 6 | port-events works only on a stopped recording | Since b8c8d6f34 the v2 code answers a live recording: it holds the CPU and reads the bus journals. The MCP description, the WebAPI / command-interface docs and the recipe still say "stopped". |
| 7 | Profile probes fire only in real time | Deliberate (breakpoint taps design). Owner decision 2026-10-28: TTD answers "in which frames did probe X fire" as a query. Replays never drive haptics, sound or the deck. This is phase 2, on the engine. |

## Phase 1 changes

### 1. coverage-scan

- `last_match` (and `first_match`) are the true first and last matching frames, even when the list is truncated.
- `order`: `oldest` (default) or `newest`. With `newest`, the list holds the newest `limit` matches.
- `ranges`: `true` returns `ranges: [[from, to], ...]`, runs of consecutive matching frames, instead of `frames`.
  `limit` then counts runs.
- `limit` goes up to 10000.
- Cursor: a truncated answer carries `next_from_frame` (oldest order) or `next_to_frame` (newest order). The next
  call passes it back unchanged.
- `exact`: `true` when a page names the bytes (`phys_page` or another `space`). `false` means "may contain": the
  offset matched on any page.
- The MCP text lists the frames (as runs), the cursor, and a note when the answer is not exact.
- The block lookup is a binary search over blocks (it was linear).

### 2. Pages

- `phys_page` takes `rom`: the one bucket all ROM shares (and other page-less memory such as the cache). This works
  in coverage-probe, coverage-scan, find-last (read / execute) and reverse-continue.
- reverse-continue takes `phys_page`. It prunes frames by that page, and a hit must run on that page.

### 3. reverse-continue `all`

- `all: true` collects hits newest first: `hits: [{frame, tinframe, t, pc, phys_page}]`, then `truncated` and
  `next_before`. The machine does not move.
- `limit`: 100 by default, 1..10000.
- Bounds in both modes:
  - `before_frame` + `before_tin` or `before` (machine T): at or before that point. The default is strictly before
    the current position, as now.
  - `from_frame`: do not look older than this frame.
- A barrier ends the walk as it does now. The hits newer than the barrier are returned with `blocked_by_marker`.

### 4. find-last `changes_only`, `all`

- `changes_only: true` (memory writes only) skips a write that stores the byte already there. Each hit carries
  `old_value`.
  - **On the replay path**, the probe reads the byte before the store, during the replay.
  - **Inside a write-journal segment**, the old value is the previous write to the same page and address in the
    journal. When there is none, it is the byte at the segment start, read from that frame's checkpoint.
  - Nothing is added to the recording. A stored old byte would cost +1 B per write (12 -> 13 B) on the hot path.
- `all: true`, `limit`, `from_frame`: the hit list newest first, plus `truncated` and `next_before`. The machine
  does not move.
- Without these options, find-last behaves exactly as before.
- The MCP `value` text is fixed: it filters on the written value.

### 5. memory-at `space: cpu`

- `offset` is a Z80 address and `length` is at most 65536 - offset. The bytes are those the CPU saw at
  `frame` / `tinframe`.
- `banks[4]` gives, for each 16 KB window, `kind` (rom / ram / cache), the `page` number and the bank name.
- How it works: save the live state, replay to the point silently, read through the CPU's mapping, restore the live
  state verbatim (as the frame cache does). The machine does not move.
- `ramN` stays as it is (physical). The docs state the Pentagon 48K case (0xC000 is page 0).

### 6. port-events

- Checked live on a running recording.
- The MCP description, `webapi-interface.md`, `command-interface.md` and `.recipe/analysis/ttd-recording.md` are
  fixed. If the CPU hold times out, the error says to pause.

## Tests

- Controller-level tests for each option. Truncation, the cursor, both orders and ranges for scan. ROM. Page
  filters that separate two banks with the same Z80 address. reverse-continue `all` against single-hit calls run in
  a loop (the same hits). changes_only on the journal path and on the replay path (same answers). The memory-at cpu
  view against the live machine at the same point.
- TTDControl option parsing and errors. The MCP text carries the lists.
- Live: `tools/verification/mcp/run_all.py` and a WebAPI check of each new option.

## Benchmarks

The phase 1 changes are query-side; the recording hot path is not touched. Before and after:

- Recording overhead: `TTDMatrix` BM-1 with `UNREAL_TTD_BENCH_ENGINE=controller`, and `BM_Frame_*`. Expect no
  change.
- Query latency: coverage scan over 50k frames (linear vs binary block lookup); reverse-continue `all` against N
  single calls; find-last `changes_only` on the journal and replay paths; memory-at cpu.

Most `core/benchmarks/debugger/ttd/*` files include only `timetravelmanager.h`, so they appear to measure v1.
Phase 2 ports them to the controller.

### Results (2026-10-28, M-series Mac, Release)

`core/benchmarks/debugger/ttd/ttd_analysis_query_benchmark.cpp`, `--benchmark_filter=BM_Analysis`. The session is
3,000 Pentagon frames. The baseline binary is master `bb0513b80` built with `TTD_ANALYSIS_BASELINE`. Three
interleaved A/B rounds; the medians are CPU time. A build ran on the machine at the same time, so real time was about
twice CPU time on both sides.

| Query | Baseline | Phase 1 |
|-------|----------|---------|
| coverage scan, oldest first (old path) | 304 us | 286 us |
| reverse-continue, 20 single calls (old path) | 103 ms | 99 ms |
| find-last loop, journal (old path) | 20.0 ms | 20.2 ms |
| find-last loop, replay (old path) | 578 ms | 571 ms |
| seek, read 64 KB, seek back (old path) | 4.9 ms | 5.0 ms |
| coverage scan, newest first + ranges | - | 289 us |
| reverse-continue `all`, limit 20 | - | 28 ms (3.6x fewer than 20 singles) |
| find-last `changes_only`, 10 changes, journal | - | 0.19 ms (100x fewer than the loop) |
| find-last `changes_only`, 10 changes, replay | - | 12.6 ms (45x fewer than the loop) |
| memory-at `space: cpu`, 64 KB | - | 1.2 ms (4x fewer than seek/read/seek) |

The old paths are unchanged within noise. Each new single call replaces a client loop and costs less than that loop.

The recording side has no change: `UNREAL_TTD_BENCH_ENGINE=controller`, `TTDMatrix/controller|BM_Frame_`, three
interleaved rounds, median CPU time. All 12 rows are within -8 % .. +1 % of the baseline with no sign in favour of
either side; for example, `TTDMatrix/controller/PENTAGON/game` 1804 vs 1776 ms and `BM_Frame_TTD_Gaming` 890 vs 899 us
per 1000 frames. This is the noise of a loaded machine.

## Engine fix: TurboSound FM replays on any host (POC 038 findings F1, F2)

POC 038 found two cases where a replay from a checkpoint did not reach the next checkpoint's state. Both are in the
TurboSound FM (TSFM), whose TTD state holds its render loop's position: sample and decimator phases, the render
cursor and the frame's render progress.

- **F2: host settings.** The render loop runs differently with the sound off, in LQ and in turbo. A replay with
  other host settings than the recording's ended with other TSFM bytes. CPU, RAM and every answer stayed exact; the
  device state and later audio did not.
- **F1: the first frame.** A recording started right after a reset, before the FM chips adopted the CPU's clock.
  The baseline checkpoint did not store that the adoption was still due, so a replay from it clocked the chips
  differently. Every later interval was exact.

The fix:

- **The render mode is pinned while the machine is bound to a timeline** (`ITurboSoundDevice::setTtdRenderPinned`).
  The mode is the quality (HQ or LQ) and whether synthesis is suppressed (sound off, turbo without audio).
  - A recording changes the mode only at a frame start, before the frame's checkpoint. The checkpoint stores it in
    the TSFM board byte: bit 4 LQ, bit 6 suppressed.
  - A replay keeps the mode of the checkpoint it loaded, whatever the host asks. Sound off or turbo on the host only
    silences the output.
  - A mode change while recording is a `ConfigChange` cut: the fingerprint has `render.sound_hq` and, new,
    `render.sound_generation`. A replay that crosses that frame start takes the checkpoint there.
- **The baseline stores the pending clock adoption** (board byte bit 5), and the timeline is stored relative to the
  axis origin while the adoption is due (`Ym2203Pair::saveTimeline` / `loadTimeline`).
- **The mixer's sample phase advances in turbo** when the TSFM rendered, so the next frame's phase matches.

The blob size and version do not change. Blobs written before have 0 in the new bits: HQ, not suppressed, no
adoption due.

A sound-off recording costs what it cost before. A first version rendered the full path for every pinned
recording, which doubled the cost of a sound-off recording. The pinned mode avoids that.

### Proof

- `TTDTsfmRenderMode_Test`: 16 cases. It records on a fresh Pentagon with the TSFM, starting right after the reset.
  The recordings are HQ, LQ, sound off, and HQ then LQ, sound on then off, sound off then on from frame 2. The replays
  run as recorded, sound off, LQ, turbo and turbo with audio. For every interval, the baseline one included, the
  state at the frame end is compared with the next checkpoint: CPU, chipset, decoded devices and RAM. The base
  commit fails all 16: every case replays the baseline interval.
- POC 038 with clones running with the sound off, in LQ, in turbo and in turbo with audio: 0 mismatches on
  W1-W3, the baseline interval included.
- `testdata/slots/fitted-devices.txt`: the TSFM blob of a freshly created machine is 2 bytes longer compressed
  (107 -> 109): bit 5 is set, since the adoption is due right after the reset.
- Goldens moved by the format only. `TsfmGolden_Test.LowQuality44100` changed because of bit 4 in its blobs (the audio is the
  same). `TtdBlobAndRestore` changed because the load pins the mode: the restored device keeps rendering through the
  script's suppressed frames.

### Cost (2026-10-28, M-series Mac, Release)

`BM_Prof_Pentagon_TtdRecord*` and `BM_Prof_Pentagon_TsfmTune_*` in `frame_profile_benchmark.cpp` (new rows: a v2
recording with the sound on, off and in LQ, idle ROM and a TSFM tune). Three interleaved rounds, median CPU time per
frame, base `e851b0f35` vs the fix:

| Row | Base | Fix |
|-----|------|-----|
| TtdRecordNormal (idle) | 352 us | 361 us |
| TtdRecordSoundOff (idle) | 454 us | 464 us |
| TtdRecordSoundLQ (idle) | 618 us | 625 us |
| TsfmTune ShipNormal (no TTD) | 854 us | 827 us |
| TsfmTune TtdRecordNormal | 1385 us | 1327 us |
| TsfmTune TtdRecordSoundOff | 1032 us | 1039 us |
| TsfmTune TtdRecordSoundLQ | 1121 us | 1097 us |

All rows are within -4 % .. +3 %. The rounds themselves differ by up to 15 %, so this is noise. `BM_Frame_*` in
`ttd_frame_overhead_benchmark.cpp` drives the v1 manager: with the sound off it skips the FM core, which a v2
recording never does, so it does not measure this change.
