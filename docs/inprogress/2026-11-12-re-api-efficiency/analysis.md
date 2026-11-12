# Reverse-engineering agents vs the MCP / WebAPI: faults, slow paths, missing verbs

Date: 2026-11-12.
Scope: what the reverse-engineering agents hit in the MCP tools, the WebAPI and the TTD / analysis / recording plane behind them.
The agents work in the reverse cases repository `unrealng-reverse-cases` (called R below).
Code places are in this repository at the state of `github/master` on 2026-11-12.

## How to read this document

Evidence paths name files in R, relative to its root, as plain text (for example games/chuckie-egg/work/stage2/NOTES.md).
Code paths name files in this repository as `path:line`.
"Still present" means the code place was read on 2026-11-12.
Nothing was built or run for this analysis.
A claim marked **unverified** comes from the agents' notes only, or from reading code without a reproduction.

Priorities:

- **P1**: it gives wrong results or loses work; fix first.
- **P2**: it costs every case hours, or makes agents write their own tools.
- **P3**: a smaller cost, or it is rare.

Sizes: **S** is under a day, **M** is a few days, **L** is a week or more.

Items are numbered by group: F (faults), S (slow paths), V (missing verbs), E (agent ergonomics).

## The cost in one table

The cases hold many copies of the same workaround tools.
The line counts below are `wc -l` totals over all copies in R.

| Tool | Copies | Lines | What it works around |
|:--|--:|--:|:--|
| lab.py | 13 | 2219 | A fresh machine, card plug and poll, mute, control, one call per key (V3, S3) |
| record.py | 5 | 1920 | Frame-by-frame recording of play scripts (S3) |
| ttd_dump.py | 11 | 636 | Memory over many frames in 16 K chunks (S1) |
| gs_session.py | 2 | 589 | General Sound sessions (V6) |
| drive.py | 6 | 578 | Input scripts sent call by call (S3) |
| rzx_script.py | 7 | 435 | Turning an RZX into an input script, because analysis refuses RZX sessions (F4) |
| l1merge.py | 8 | 346 | Merging L1 layers of several sessions by hand (V1) |
| rzx_dump.py | 4 | 335 | Memory of RZX playback (F4, S1) |
| rzx_inputs.py | 5 | 244 | RZX input extraction (F4) |
| rs_capture.py | 6 | 249 | Capture flow around `re_session` (S2, E5) |
| rzx_replay.py | 2 | 229 | Frame-by-frame RZX stepping (F4) |
| rzx_align.py | 2 | 215 | Aligning our run to an RZX one frame per call (F4, S3) |
| rec_rzx.py | 2 | 152 | RZX to TTD in chunks; survives HTTP timeouts (F4, S2) |
| rzx_capture.py | 2 | 98 | RZX capture fallback (F4) |

About 8 200 lines of client code exist only because of the items below.

---

## 1. Faults

### F1. `run_frames` silently runs at most 10 000 frames, and the MCP text says it ran all

**What happened.**
Chuckie Egg stage 2 compared one `run_frames 31000` call with 31 calls of 1000 frames.
The two ended in different states (lives 4 against lives 1).
The agent reported this as "the result depends on how the run is split".
In fact the single call ran only 10 000 frames.
Deflektor saw "one `run_frames` call of 15 400 frames advanced the tape far less than asked".

**Cases.**

- games/chuckie-egg/work/stage2/faults.py, games/chuckie-egg/work/stage2/faults.out (variant A against variant B).
- games/chuckie-egg/tools/lab.py, lines 91-94 (`run()` sends the whole count in one call).
- docs/unreal-ng-run-frames-faults.md (Fault 1, row A: `run_frames 31000`).
- games/deflektor/work/stage1/NOTES.md, line 11.
- games/nether-earth/work/path/NOTES.md, line 24 (the same family: `/steps` stops at 100 000 instructions).

**Cost.**
A fault report and a bisection built on a wrong reading.
Every script gap over 10 000 frames is cut without a word.
`tools/verification/analysis/mm_session.py` (`Player.run`) passes gaps through unsplit, so its users are exposed too.
Only `corpus.run_frames` splits its runs (`tools/verification/analysis/corpus.py:515-535`), by chance, into steps of `SCREEN_EVERY` = 250.

**Still present.** Yes.

- `core/automation/webapi/src/api/debug_api.cpp:759`: `if (count > 10000) count = 10000; // Safety limit`.
  The REST answer reports `count` 10000, but no client reads it.
- `core/automation/mcp/src/mcp-tools.cpp:2115-2119` builds the text from the requested number: "Ran 31000 frame(s)."
- The OpenAPI text says "max: 10000" (`openapi_stepping.inc:142`).
  The MCP tool description does not.
- `debug_api.cpp:354` clamps `/steps` to 100 000 instructions in the same way.
- `debug_api.cpp:539` clamps `run_tstates` to 10 000 000.

**Proposal.**

- No silent clamp on any run route.
  Either run the whole count in slices on the server, or answer HTTP 400 naming the limit.
  Running in slices is better: the safety limit exists for the HTTP thread, and a slice loop can check the control token and breakpoints between slices.
- The answer of every run route carries what really happened:
  - `frames_run` (or `instructions_run`, `tstates_run`);
  - `frame` and `t` at the end;
  - `stop` (`done`, `breakpoint`, `limit`).
- The MCP text uses `frames_run` from the answer, never the requested number.
- The MCP tool descriptions state any limit that remains.
- A test: `run_frames 31000` reports 31000 frames and lands on frame start + 31000.

**Priority / size.** P1, S.

### F2. Splitting a run into calls drifts by a few T-states per call

**What happened.**
After F1 is taken out, a smaller split dependence remains.
Fault 2 in the faults document: runs in 50-frame chunks diverge from one long run at frame 8574 (a sprite at x 169 against 170).
The bisect table there shows a few T-states of difference per call.

**Cases.**
docs/unreal-ng-run-frames-faults.md (Fault 2 and the bisect table).

**Cost.**
Recordings made through different clients (frame by frame, chunks of 50, chunks of 1000) do not reproduce each other.
Agents distrust replays and re-record.

**Still present.** Yes, by code reading.

- `core/src/emulator/emulator.cpp:3381-3423`, `Emulator::RunNFrames`, builds `TStateRunBudget::Frames(z80._frameLimit, frames)` relative to the T-state where the call starts (`core/src/emulator/tstaterunbudget.h`).
- Each call stops after the instruction that crosses its budget.
  The overshoot is dropped: `_hasFrameStepTarget = false`.
- `RunFrame` (`emulator.cpp:3330-3378`) keeps a persistent target and does not drift.

**Proposal.**

- `RunNFrames` aims at an absolute target: the frame boundary N frames after the current frame, the same way `RunFrame` does.
  The overshoot of the last instruction is then carried into the next call.
- Ten calls of 1 frame, one call of 10 frames and two calls of 5 frames end at the same `frame` and `t`.
- A test asserts this equality, plus the same for `run_tstates` slices.

**Priority / size.** P1, M (the budget code is shared with breakpoints and the debugger's frame step).

### F3. A TTD dump that fails says "disk full" when the disk is not full

**What happened.**
SimCity stage 1 dumped a TTD session of 139 382 frames.
The dump failed with HTTP 409, "a write failed (disk full or device gone)".
894 GiB were free.
A 57 788 179-byte partial file stayed behind.
The agent split the city into two sessions (city, city2).
A session of 96 503 frames (117 491 148 bytes) dumped fine.

**Cases.**

- games/simcity/work/stage1/NOTES.md, lines 24-25.
- games/simcity/work/stage2/NOTES.md, lines 274-281 (ttd139k.py, the reproduction).

**Cost.**
The longest recordings cannot be kept.
Work is split by the tool's limit, not by the game; every case since keeps recordings under about 100 000 frames.

**Still present.** Yes, the message.

- `core/src/debugger/ttd/engine/ttdsessionfile.cpp:443-449` maps every `false` from `AddRecord` / `EndPart` to the disk-full text.
- `core/src/debugger/ttd/engine/ttdcontainer.cpp:150`, `WriteRecord`, also returns `false` when `storedSize > UINT32_MAX` or `rawSize > kMaxRawSize`.
  `kMaxRawSize` is `1 << 30` (line 23).
- So a record over 1 GiB raw is reported as a full disk.
- **Unverified**: that this is SimCity's cause.
  A 139 k-frame session with a large per-frame stream can plausibly build one record over 1 GiB, but the record sizes were not measured.

**Proposal.**

- `WriteRecord` returns a reason (I/O error, record too large, part layout), and the 409 text names it with the sizes.
- Large streams are written as several records (chunked), so no session length hits a record limit.
- A failed dump removes its partial file, or renames it to `*.partial`, and says so.
- A test writes a session whose stream exceeds the record limit.

**Priority / size.** P1, M.

### F4. Analysis refuses sessions recorded during RZX playback

**What happened.**
A TTD session recorded while an RZX plays cannot be analysed.
The analysis build fails with "this machine cannot take the session".
Switchblade turned the RZX into an input script and replayed it instead.
91 segments were replayed, and 84 diverged (median 182 frames before divergence).
It then fell back to capturing memory during RZX playback (rzx_capture.py).
Myth, Rick Dangerous 2 and Tujad hit the same wall.

**Cases.**

- games/switchblade/tools/rec_rzx.py.
- games/switchblade/work/stage2/NOTES.md, lines 40-47 and 58.
- games/myth/work/stage1/NOTES.md, lines 38-41.
- games/rick-dangerous-2/work/stage1/NOTES.md, line 59.
- games/tujad/work/stage2/NOTES.md, section 3.6 (lines 349-365).

**Cost.**
RZX files are the best walkthroughs there are (whole games played by humans).
Without analysis on them, agents write and keep about 1 700 lines of RZX tools (table above), and the replays diverge anyway.

**Still present.** Yes.

- `core/src/debugger/analyzers/analysis/analysisinstance.cpp:114-185` (`Make`) creates a fresh emulator and loads no RZX into it.
- `core/src/debugger/ttd/ttdmachineperipherals.cpp:139-146` registers `RzxTtdState` only when `LoadedRzxSession()` exists.
- The session holds an RZX playback state blob (84 bytes, with a fingerprint of the recording; `core/src/emulator/rzx/rzxttdstate.h`).
- `core/src/debugger/ttd/ttdcontrollersessionfile.cpp:358-361`: `BindLive` finds no peripheral for that blob and refuses.
- PLAN row #27 lists this as R5, "TTD interop possibly later, low priority".

**Proposal.**

- The trace bundle carries the RZX path (or the RZX bytes) and its fingerprint.
- `AnalysisInstance::Make` loads the same RZX into the fresh instance before `DeserializeSession`, so the RZX peripheral exists and binds.
- If the RZX is missing, the error says "this session was recorded during RZX playback of <file>; give `rzx` in the build call".
- MCP `analysis build` and `POST /analysis/build` take an optional `rzx` path for sessions made elsewhere.
- Raise PLAN #27 R5 from "low priority"; this is the part of R5 the agents need.
- A test: record a TTD session during RZX playback, build L1, compare with L1 from a session recorded by input replay of the same frames.

**Priority / size.** P1, M (L if the bundle format needs a new section).

### F5. Tape play after a TZX `$2A` stop block or a pause does not move the tape

**What happened.**
Terramex: the tape stops at a `$2A` ("stop the tape if in 48K mode") block.
The play call answers "Tape playback resumed (in place)", but the tape stays where it is.
The agent had to step around it.
Deflektor: a TZX `$11` block that is the last one, with pause 0, gets no final edge, so the loader waits.

**Cases.**

- TODO.md, "For unreal-ng", the Terramex item.
- games/terramex/work/stage2/NOTES.md, lines 269-271.
- games/terramex/work/stage1/NOTES.md, line 39.
- games/deflektor/work/stage1/NOTES.md, lines 84-91.

**Cost.**
Multi-load games stop at their second load; each case finds its own workaround.

**Still present.** Probably; the cause is **unverified**.

- `core/src/loaders/tape/loader_tzx.cpp:1387-1399` turns `$2A` into an inert Control block (`playable=false`, no pulses): "inert in v1 (no machine check here)".
- `$20` pause 0 continues past (`loader_tzx.cpp:1298-1318`).
- The loader watchdog `handleFrameEnd` (`core/src/emulator/io/tape/tape.cpp:813-857`) parks at the next block, or pauses playback after `TAPE_*_HOLD_FRAMES` frames without loader reads.
- `ResumePlaybackFromPause` (`tape.cpp:530-570`) resumes in place when the playback was frozen in flight, else calls `StartPlaybackAtCursor` (`tape.cpp:389-416`).
- The WebAPI says "Tape playback resumed (in place)" (`core/automation/webapi/src/api/tape_disk_api.cpp:395-409`) without reporting the state after the resume.
- Hypothesis: the resume lands on a pulse-less block, or the watchdog pauses again in the next frame because the loader is not reading yet.
- Deflektor's `$11` last edge was not checked in the code.

**Proposal.**

- A decision for `$2A`: honour it on 48K models (stop), skip it on 128K models, as the TZX specification says.
- Play / resume skip pulse-less blocks to the next playable one.
- The play answer reports the tape state, the block index and its type after one frame, so a client sees "paused again by the watchdog" at once.
- The final edge after the last data block (TZX `$10` / `$11` / `$14` with pause 0) is emitted.
- Tests on a synthetic TZX with `$2A`, `$20` pause 0 and a last `$11` with pause 0.

**Priority / size.** P2, M.

### F6. `find_event` on execution returns empty hits that change from call to call

**What happened.**
Nether Earth editor: `find_event` for the first run of an address gave hits with no text at moments like `f5556+0`.
The hits changed per call, and real hits were missing.

**Cases.**
games/nether-earth-editor/work/edB/NOTES.md, lines 23-24.

**Cost.**
The agent dropped the verb and searched by stepping.

**Still present.** Probably; **unverified** (code reading, not reproduced).

- `core/src/debugger/ttd/ttdcontrollerfindevent.cpp:832-900`: the coverage-index path adds a sentinel hit with time `{frame, 0}` and no text once `k >= limit`.
  It also drops frames where the `runIn` probe misses.
- Lines 347-353 cut the list to `limit`, which can leave sentinels in the answer and drop real hits.

**Proposal.**

- Sentinels never reach the answer.
  The "more results" fact is a separate `truncated` flag with `next_from_frame`.
- A frame the index marks as a hit but `runIn` misses is reported as an index fault in a debug field, not dropped silently.
- A test: exec event over a session with a known run count, limits 1, 2 and above the count.

**Priority / size.** P2, S.

### F7. General Sound: COMRG read logged with value 0; NeoGS not in SZX

**What happened.**
Nether Earth GS: the GS command register read appears in the history with value 0.
NeoGS state is not stored in SZX snapshots.
Zynaps GS: the GS state in SZX is approximate.

**Cases.**

- games/nether-earth-gs/work/stage1/NOTES.md, lines 76-92.
- games/zynaps-gs/work/stage1/NOTES.md, lines 108-127.

**Cost.**
The GS side of GS games is studied through workarounds (gs_session.py, 589 lines in two copies).

**Still present.** **Unverified** (not checked in code).

**Proposal.**
Check the history writer for the COMRG read value; check the SZX writer for NeoGS and GS blocks; fix and add tests.

**Priority / size.** P3, M.

### F8. The zxbus port history is buffered until it is turned off; a snapshot load restarts its frame counter

**Cases.**
games/zynaps-gs/work/stage1/NOTES.md, lines 108-127.

**Cost.**
The history cannot be read while it records; frames after a snapshot load cannot be matched with TTD frames.

**Still present.** **Unverified**.

**Proposal.**
Flush on read (or on every frame end for the disk mode); keep the emulator's frame counter in the records, not a per-history counter.

**Priority / size.** P3, S.

### F9. L1 marks a false `root` at a snapshot's first PC

**Cases.**
games/switchblade/work/stage2/NOTES.md (the L1 roots section, near line 589).

**Cost.**
A false entry point in the code map; the agent has to explain it away.

**Still present.** **Unverified**.

**Proposal.**
Mark the first PC of a session that starts from a snapshot as `resume`, not `root`.

**Priority / size.** P3, S.

---

## 2. Slow paths

### S1. Memory over many frames needs one call per frame and per 64 K

**What happened.**
`memory_at` reads one moment per call.
Every case that studies a variable over time wrote its own sampler.

**Cases.**

- ttd_dump.py in 11 copies (for example games/terramex/tools/ttd_dump.py) loops 16 K chunks per frame.
- games/chuckie-egg/work/docs/ceC-NOTES.md, line 6: sample.py reads every frame 0 .. 25 491.
- The Exolon fill notes: rd.py, reads.py, readsat.py, series.py, firstchange.py (bisection for the first change).
- The Zynaps docs: `ttd_dump --frames 0:75000 --every 10`.
- Tujad (tjB) and Earth Shaker (esB): peek.py.
- games/battle-command/work/docs/bcD-NOTES.md: the frame semantics confused the writer (see E4).

**Cost.**
Tens of thousands of HTTP calls per study, hex text, and hours per series.

**Still present.** Yes.

- `POST /ttd/memory-at` reads at most 65 536 bytes at one frame, as hex (`core/automation/webapi/src/api/ttd_api.cpp:609-623`; MCP `mcp-tools.cpp:6022`).
- `probe-frames` takes at most 16 probes with pagination (`ttd_api.cpp:674-716`).
  It answers questions about frames, not byte series.

**Proposal.**
A series verb: `POST /ttd/memory-series`, MCP `time_travel action=memory_series`.

- Parameters:
  - `ranges`: a list of `{address, length}` or `{page, offset, length}`;
  - `frames`: `from`, `to`, `step`, or an explicit list;
  - `changes_only`: answer only the frames where any range changed (this replaces the bisection scripts);
  - `format`: `base64` (default for the REST plane), `hex`, or `values` (bytes / words for short ranges);
  - `max_chars` as everywhere (#118).
- Answer: one row per frame `{frame, exact, bytes}`, plus `next_from_frame` when the budget runs out.
- The server walks the checkpoints forward once, not per frame.
- A `first_change` mode answers the first frame where a condition on the range holds (or reuse `find_event` with a memory condition, if it already covers this).

**Priority / size.** P1, M.

### S2. Long calls block a client until its HTTP timeout

**What happened.**
SimCity: `probe_frames` over all of city2 timed out.
Zynaps GS: the GS bot died on a urllib timeout near frame 3500.
`rec_rzx.py` saves the TTD first, because "a build that outlives the HTTP timeout must not lose the recording".

**Cases.**

- games/simcity/work/docs/scA-NOTES.md, line 11.
- games/zynaps-gs/work/docs/zgsB-NOTES.md, line 6.
- games/switchblade/tools/rec_rzx.py (comment).
- games/battle-command/tools/record.py, lines 533-537 (a poll loop on `analysis status`).

**Cost.**
Lost runs, and sleep-poll loops in every client.

**Still present.** Yes.

- `tools/verification/mcp/mcplib.py:76`: RPC timeout 120 s.
- The MCP bridge receive timeout is 300 s (`mcp/bridge/src/mini-http.cpp:343-357`).
- `analysis build` takes `wait` 0 .. 30 000 ms, default 20 000 (`core/automation/mcp/src/mcp-analysis.cpp:1515, 1594, 1712-1716`).
- `tools/verification/analysis/re_session.py:155-159` polls `analysis status` with `time.sleep(5)`.
- The WebSocket already has `analysis.progress` and `analysis.ready` topics; no Python client uses them.

**Proposal.**

- One job model for the long verbs (analysis build, TTD dump, `probe_frames`, the series of S1, the script player of S3):
  - a call with `async: true` answers `{job, state}` at once;
  - `GET /jobs/{id}` (MCP `jobs action=status`) gives progress and the result;
  - `wait_ms` long-polls up to a bound;
  - `jobs action=cancel`.
- The job publishes progress on the WebSocket (the analysis topics generalised).
- `re_session.py` and `mcplib` use `wait_ms` long-poll instead of `sleep(5)`.

**Priority / size.** P2, M.

### S3. Input scripts are played one call per key and one call per frame

**What happened.**
Every case drives its game from Python: one `type_input` / `joystick_input` call per key, `run_frames` between keys.
Several step one frame per call to align with an RZX.
The tape is started by a separate call between steps, so its phase against the frame varies from run to run.
Recordings of play scripts are not shared; each writer replays its own.

**Cases.**

- lab.py (13 copies), record.py (5), drive.py (6); see the table above.
- games/earth-shaker/tools/rzx_align.py; games/earth-shaker/work/stage1/NOTES.md, line 15 ("our instance one frame a call").
- games/rick-dangerous/tools/rzx_replay.py; games/rick-dangerous/work/stage1/NOTES.md, line 129.
- games/nether-earth-gs/work/stage2/NOTES-shots.md, line 38 (tape phase).
- games/video-pool/work/docs/vpC-NOTES.md, line 155; vpD-NOTES.md, line 197 (each writer replays).

**Cost.**
Thousands of calls per recording, F1 / F2 exposure on every one, and runs that are not reproducible.

**Still present.** Yes.
`run_until` and watches (#117) cover "run until X", but not "play this script".
`mm_session.py` has a client-side player (`Player.run`), which is F1-exposed.

**Proposal.**
A server-side script player: `POST /input/script`, MCP `input action=play_script`.

- Parameters:
  - `script`: a list of `{frame, keys_down | keys_up | joystick | mouse | tape: play | stop}` (frame offsets from the start), or the text format of `mm_session`;
  - `record_ttd: true` to record the run as a TTD session;
  - `screens_every` to save screens on the way;
  - `until`: an optional #117 watch spec to stop early.
- The player applies each event at the exact frame boundary, on the emulation thread.
- The answer: frames run, final `frame` / `t`, the events applied, and the session path when recorded.
- The script and the session are stored together, so other writers reuse the recording instead of replaying (link with `capture.json` in E5).
- Runs as a job (S2).

**Priority / size.** P2, M.

### S4. Device histories and traces are too large to keep

**What happened.**
Nether Earth GS: the history was 1.7 GB for 2 600 frames, with no host-only filter.
Video Pool: traces are gigabytes, so sound_events.py records 200-frame traces and deletes them.

**Cases.**

- games/nether-earth-gs/work/stage1/NOTES.md, lines 76-92.
- games/video-pool/work/docs/vpD-NOTES.md, line 30.

**Cost.**
Short windows only; repeated recording.

**Still present.** **Unverified** (filters not checked in the history code).

**Proposal.**
Filters at record time: CPU side (host / GS), port set, address set.
An event-only trace mode (port writes and reads with frame and PC, no instruction stream) for sound studies.

**Priority / size.** P3, M.

---

## 3. Missing verbs

### V1. No L1 over several sessions

**What happened.**
A game played in several sessions (levels, loads, scripts) gives several L1 layers.
Agents merge them by hand: instructions, byte counts per page, replay sums.

**Cases.**
l1merge.py in 8 copies (346 lines), for example under games/switchblade/tools/ and games/simcity/tools/.

**Cost.**
Each case re-implements the merge; the merged numbers are not checked by anyone.

**Still present.** Yes; analysis builds over one session.

**Proposal.**
`analysis build` takes `sessions: [...]`, or `POST /analysis/merge` takes built analysis ids.
The answer is one L1 with per-session provenance (which sessions ran each instruction, first / last frame in each).
Links with PLAN #111.

**Priority / size.** P2, M.

### V2. No "screen as shown" at a past frame

**What happened.**
128K games draw in a shadow screen (bank 7) and flip.
Agents rebuild the shown screen from the port `$7FFD` history and two memory reads.

**Cases.**

- games/battle-command/work/docs/bcB-NOTES.md (fig_shownscreen.py).
- games/advanced-nether-earth/work/stage3/NOTES.md, lines 69-70.
- games/advanced-nether-earth docs, docR-NOTES.md, line 15.

**Cost.**
A figure script per case, and wrong screens when the flip is mid-frame.

**Still present.** Yes.
The `time_travel` actions (`core/automation/mcp/src/mcp-tools.cpp:5686-5694`) have no screen-at-frame.
Live `inspect_state` has `screen_image` / `screen_attributes` with a choice of screen 0 / 1 (`mcp-tools.cpp:2699`).

**Proposal.**
`time_travel action=screen_at frame=F`, `GET /ttd/screen-at`:

- `which`: `shown` (default: the screen the ULA displayed at the end of frame F), `normal`, `shadow`;
- `format`: `png`, `bitmap`, `attributes`;
- the answer names the bank shown and whether it changed during the frame.

**Priority / size.** P2, S.

### V3. No "lab machine" in one call

**What happened.**
Every case's lab.py builds the same machine:

- a fresh instance;
- a Kempston card plugged, then a poll loop because the plug restarts the machine;
- pause;
- mute through `invoke_api PUT /audio/mixer/master` after every load;
- take control;
- fast / turbo tape off;
- memory pages read through `invoke_api`;
- screenshots made by reading `$4000` and rendering locally.

**Cases.**
lab.py in 13 copies (2 219 lines), for example games/bumpy/tools/lab.py and games/chuckie-egg/tools/lab.py.

**Cost.**
About 170 lines per case, and per-case differences that make runs not comparable.

**Still present.** Yes.

**Proposal.**
`emulator_manage action=lab` (`POST /emulator/lab`):

- parameters: `model`, `ram: zero | random`, `cards: [...]`, `features: {kempstonmouse: false, ...}`, `muted: true`, `paused: true`, `take_control: true`, `fast_tape: false`;
- it answers when the machine is ready (no client polling after a plug);
- the answer lists the effective settings.

Also `keys action=set` with a whole key state at once.

**Priority / size.** P2, M.

### V4. Breakpoint hit counts and per-hit frames without stopping

**Cases.**
games/zynaps-gs/work/stage1/NOTES.md, lines 108-127 (used `memory_counters` instead).

**Still present.** Hit counters are part of PLAN #6 (slice 1f), not landed.

**Proposal.**
A counting breakpoint mode (`action: count`) that keeps the count and a bounded list of `{frame, t}` per hit.
Deliver it with PLAN #6.

**Priority / size.** P3, M.

### V5. L1 has no first / last frame per instruction; depacker reads pollute it

**Cases.**
games/nether-earth-gs/work/stage2/NOTES-trd.md, lines 75-90.

**Still present.** **Unverified** in code; PLAN #111 plans first / last frame for L0.

**Proposal.**
First and last frame per instruction in L1.
A filter on reads by reader PC range (to drop a depacker's reads).

**Priority / size.** P3, S.

### V6. No view of the General Sound firmware variables

**Cases.**
games/nether-earth-gs/work/stage1/NOTES.md, lines 76-92.

**Proposal.**
A GS state verb: the GS CPU registers, the firmware's channel and sample tables, decoded.
Combine with F7.

**Priority / size.** P3, M.

### V7. Port bases collide between parallel agents

**What happened.**
Each instance needs six ports at a step of 100.
Agents collide ("port 35100 is taken"), find foreign instances in their range, and `lsof` hangs.
With five cases run in parallel (2026-11-12) the lead hands out port bases by hand in every brief.

**Cases.**

- games/video-pool/work/docs/vpC-NOTES.md, lines 152-154.
- games/video-pool/work/docs/vpD-NOTES.md, line 204.
- games/tujad/work/docs/tjB-NOTES.md, line 22.

**Still present.** Yes; `mcplib.launch` refuses a taken port (`tools/verification/mcp/mcplib.py:248-254`).

**Proposal.**
`--port-base auto`: the app binds free ports and writes them to a ports file (`--ports-file path`).
`mcplib.launch` uses it.

**Priority / size.** P3, S.

---

## 4. Agent ergonomics

### E1. An unknown action answers without the valid ones

**What happened.**
Agents asked for `slots_unplug`; the action is `slots_remove`.
The answer was a bare "Unknown action".

**Cases.**

- games/zynaps-gs/work/stage1/NOTES.md, lines 110-111.
- games/nether-earth-gs/work/stage2/NOTES-shots.md, line 41.

**Still present.** Yes.
`core/automation/mcp/src/mcp-tools.cpp:804, 2172, 5032` answer "Unknown action '...'" with no list.
`slots_remove` exists since b7802d012 (2026-10-05; `mcp-tools.cpp:161`).

**Proposal.**
Every unknown action, parameter value and route answers with the valid ones and the nearest match ("did you mean `slots_remove`").
One helper used by all tools.
Add `unplug` as an alias of `remove`.

**Priority / size.** P2, S.

### E2. Bumpy: the Kempston mouse answers on port `$DF`, and the agents did not find how to turn it off

**What happened.**
Bumpy reads `IN A,($DF)` at `$B42B` (port `$FFDF`).
With the Kempston mouse on, the mouse Y counter (85) gives `$EA` at `$B43E`, and the "wait for no key" loop at `$B401` hangs.
The recordings had to hold the mouse counters at 0 (`POST /mouse/counters {"x":0,"y":0}`; 255 failed).
The Kempston stick could not drive the game: with menu option 4 (Kempston) the game stayed at READY?.

**Cases.**

- games/bumpy/WALKTHROUGH.md, line 100 (the port read and the hang), line 112 (the `menu` recording), section 5.
- games/bumpy/tools/lab.py, lines 86-92 (`quiet_mouse`).
- games/bumpy/work/stage1/NOTES.md, lines 31-32 and 106-108 ("For the lead").
- Related: games/zynaps-128/work/docs/z128A-NOTES.md, lines 75-77 (the `mouse_input` `dy` sign and the button bit mapping; **unverified**); games/battle-command/work/stage1/NOTES.md, line 13 (mouse gave no pointer motion).

**Cost.**
A stage of recordings built on a workaround, and play through the stick not possible.

**Still present.** The capability exists; it is not discoverable.

- The mouse can be switched off per instance: feature `kempstonmouse` (alias `kmouse`), `PUT /api/v1/emulator/{id}/feature/kempstonmouse` with `{"enabled": false}`.
  - Route: `core/automation/webapi/src/emulator_api.h:240`; handler `core/automation/webapi/src/api/features_api.cpp:166-210` (the body needs `enabled`).
  - The feature is on by default (`core/src/base/featuremanager.cpp:691-697`); the change applies at once (`featuremanager.cpp:867-876`, `mouse.cpp:67-82`).
  - It exists since a87674555 (2026-09-12), before Bumpy.
  - The config has `[INPUT] Mouse=NONE|KEMPSTON` (`config.cpp:706-720`).
- MCP has no action for features.
  It reaches `/feature` only for breakpoints (`mcp-tools.cpp:1724-1736`); an agent must know the route and use `invoke_api`.
- The Kempston joystick is scriptable: MCP `joystick_input` `press` / `release` / `set` / `tap` / `status` (`mcp-tools.cpp:5285-5293`).
  On 48K / 128K / +2 / +3 it reads at `#1F` only with a card: `emulator_manage slots_plug edge.1 kempston` (lab.py does this).
- **Unverified**: why the stick could not drive Bumpy with the mouse off.
  The stick was tried with the mouse on, so the `$DF` read still returned mouse values.
  The agents did not retry with the mouse feature off.

**Proposal.**

- MCP `emulator_manage action=features` (list) and `action=feature name=... enabled=...` (set), forwarding to the existing routes.
- The `joystick_input` and `mouse_input` descriptions say:
  - the Kempston mouse is on by default and answers on `$FADF` / `$FBDF` / `$FFDF`;
  - `kempstonmouse` off frees those ports;
  - the stick needs a card on models without a built-in port.
- `joystick_input status` reports whether a Kempston port is decoded on this machine and whether a mouse shares the address space.
- The lab preset of V3 takes `features`.
- Retry Bumpy with the mouse feature off and the stick on the card, to close the unverified part (Bumpy stage 2 is asked to do this).

**Priority / size.** P2, S.

### E3. No frame counter in the registers or the run answers

**What happened.**
Chuckie Egg tried `/cpu/state` and guessed routes (all 404) to read the frame number.

**Cases.**
games/chuckie-egg/work/stage2/faults_clock.out.

**Still present.** Yes.
`GET /registers` gives only `special.t` (`core/src/debugger/snapshot/debugsnapshot.cpp:61`).
The frame is in `GET /debug/snapshot` (`debugsnapshot.cpp:347`, `time.frame`), `GET /frame_cost` and `GET /video/beam`.

**Proposal.**
`frame` next to `t` in `/registers` and in every run answer (the F1 answer).

**Priority / size.** P3, S.

### E4. `memory_at` does not say which moment it read

**Cases.**
games/battle-command/work/docs/bcD-NOTES.md.

**Still present.** Yes; the route reads the newest checkpoint at or before the frame and returns an `exact` flag (`ttd_api.cpp:609-623`).

**Proposal.**
The answer states the moment in words: "state at the start of frame F (before its first instruction)", plus `frame`, `t` and `exact`.
The tool description says the same.

**Priority / size.** P3, S.

### E5. `re_session capture --record-load` does not write the load offset

**Cases.**
games/advanced-nether-earth/work/stage3/NOTES.md, lines 185-188 (script frame + 1602).

**Still present.** Yes; `capture.json` has `load_frames`, but the script frame offset is not written (`tools/verification/analysis/re_session.py:117, 171`).

**Proposal.**
`capture.json` gets `script_start_frame` (the TTD frame of script frame 0) and the input script itself.

**Priority / size.** P3, S.

### E6. The key script error does not explain the syntax

**What happened.**
Myth wrote "W+O"; keys are separated by spaces.
Earlier SS / symbol-shift name problems are fixed.

**Cases.**

- games/myth/work/stage1/NOTES.md, line 45.
- Fixed: games/nether-earth-gs/work/stage1/NOTES.md, line 81; games/zynaps-gs/work/stage1/NOTES.md, line 127; games/advanced-nether-earth/work/stage1/NOTES.md, line 20.

**Still present.** Partly.
Names are fixed (74739a0df, 2026-11-05; `tools/verification/analysis/mm_session.py:35`; `debugkeyboardmanager.cpp:1017-1019`).
The error at `hostactions.cpp:230` still does not mention the separator.

**Proposal.**
The error names the separator and gives an example ("keys are separated by spaces: `W O`").
Accept `+` as a chord separator.

**Priority / size.** P3, S.

### E7. RZX seek exists but agents step to the start frame

**Cases.**
games/switchblade/tools/rec_rzx.py (5 000-frame chunks to reach `FROM`).

**Still present.** The verb exists: `POST /rzx/seek` (`core/automation/webapi/src/api/rzx_api.cpp:240-253`, since 4516abfa5, 2026-09-29), MCP `rzx_playback action=seek` (`mcp-tools.cpp:7797, 7861`).

**Proposal.**
Mention seek in the `rzx_playback` description and in `.recipe/media/play-rzx.md` as the way to a start frame.
Name it in the F4 error text.

**Priority / size.** P3, S.

### E8. A binary without a tool fails late

**What happened.**
The main checkout's binary had no `analysis` MCP tool; the agent found out by failure.

**Cases.**
games/nether-earth-editor/work/edB/NOTES.md, lines 4-5.

**Still present.** **Unverified** whether a capability call exists (PLAN #9 plans one).

**Proposal.**
`mcplib.launch` and `re_session` check the tool list (or a capabilities route, #9) against what they need and refuse with "this binary has no `analysis`; build current master".

**Priority / size.** P3, S.

---

## 5. Not for the API

- Pentagon ROM ISR patch hangs TR-DOS from the 128 menu; the owner decided it (games/nether-earth-zs-edition/work/stage1/NOTES.md, lines 31-40).
  Not counted.

## 6. Summary

| # | Item | Group | Priority | Size | Still present |
|:--|:--|:--|:--|:--|:--|
| F1 | `run_frames` silent 10 000 clamp, MCP text wrong; same for `/steps`, `run_tstates` | Fault | P1 | S | Yes |
| F2 | Split-dependent T-state drift in `RunNFrames` | Fault | P1 | M | Yes (code) |
| F3 | TTD dump: "disk full" for every failure; records over 1 GiB | Fault | P1 | M | Yes (message); cause unverified |
| F4 | Analysis refuses sessions recorded during RZX playback | Fault | P1 | M | Yes |
| S1 | Memory series verb (frames range, several ranges, changes only) | Slow path | P1 | M | Yes |
| F5 | Tape play after `$2A` / pause does not move; play answer has no state | Fault | P2 | M | Probably; cause unverified |
| F6 | `find_event` exec sentinel hits, missed hits | Fault | P2 | S | Probably (code reading) |
| S2 | Job model / long-poll for long verbs | Slow path | P2 | M | Yes |
| S3 | Server-side input script player with frame-exact events and recording | Slow path | P2 | M | Yes |
| V1 | L1 over several sessions | Verb | P2 | M | Yes |
| V2 | Screen as shown at a past frame | Verb | P2 | S | Yes |
| V3 | Lab machine preset in one call; `keys set` | Verb | P2 | M | Yes |
| E1 | Unknown action lists valid ones, "did you mean" | Ergonomics | P2 | S | Yes |
| E2 | Bumpy: mouse on `$DF`; features in MCP; mouse / Kempston discoverability | Ergonomics | P2 | S | Capability exists, not discoverable |
| F7 | GS COMRG value 0; NeoGS not in SZX | Fault | P3 | M | Unverified |
| F8 | Port history buffered until off; frame counter reset on load | Fault | P3 | S | Unverified |
| F9 | L1 false `root` at a snapshot's first PC | Fault | P3 | S | Unverified |
| S4 | History / trace filters, event-only trace | Slow path | P3 | M | Unverified |
| V4 | Counting breakpoints with per-hit frames (with PLAN #6) | Verb | P3 | M | Yes |
| V5 | L1 first / last frame per instruction; reader-PC filter | Verb | P3 | S | Unverified |
| V6 | GS firmware state view | Verb | P3 | M | Unverified |
| V7 | `--port-base auto` and a ports file | Verb | P3 | S | Yes |
| E3 | `frame` in `/registers` and run answers | Ergonomics | P3 | S | Yes |
| E4 | `memory_at` states the moment it read | Ergonomics | P3 | S | Yes |
| E5 | `capture.json` writes the script start frame | Ergonomics | P3 | S | Yes |
| E6 | Key script error explains the separator | Ergonomics | P3 | S | Partly |
| E7 | RZX seek discoverability | Ergonomics | P3 | S | Verb exists |
| E8 | Capability check before a session | Ergonomics | P3 | S | Unverified |

In total: 28 items (5 P1, 9 P2, 14 P3).
F1 and E3 share the run answer; S2 carries S1 and S3 when they run long; E2 feeds V3.

## 7. Review (2026-11-12)

The lead re-read these code places against `github/master` and confirmed them:

- F1: `debug_api.cpp:759` clamps `count` to 10000; `mcp-tools.cpp:2119` writes the requested number into the text; Chuckie Egg's Fault 1 row A is `run_frames 31000` (R docs/unreal-ng-run-frames-faults.md line 23).
- F3: `ttdcontainer.cpp:23` (`kMaxRawSize = 1u << 30`) and `:150` (`WriteRecord` fails on it); `ttdsessionfile.cpp:443-449` turns any failure into the disk-full text.
- E2: the feature route `emulator_api.h:240` exists.

The **unverified** marks stand: F5-F9, S4, V5, E8 need a reproduction before work starts.
