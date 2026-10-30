# TTD analysis feedback: phase 2 plan

Owner decision 2026-10-28. Phase 2 makes window questions fast on the user's own machine; the clone pool of
POC 038 is deferred. Facts behind the plan:
[POC 038](../../../tools/poc/038-ttd-replay-query-engine/README.md) (clones: exact, 0.22 ms a frame, linear to 16)
and [POC 040](../../../tools/poc/040-ttd-inplace-replay-profile/README.md) (where today's in-place replay spends its
time, and what a recording costs).

## Why no clone pool now

After blocks B and C a window replays on the user's machine at about the speed of a debug-mode frame (0.3-0.8 ms):
50,000 frames (about 17 minutes of play) in 15-40 s. Most questions do not replay at all:

- exec of an address: the coverage index prunes (W2: 2 µs a frame);
- writes: the write journal answers (3-15 µs a frame: 50k frames in under 1 s);
- port values: the port journals are recorded and are read without a replay.

What still needs a full replay: reads with their PC (fonts, tables), probes on reads, a PC profile, a trace. A pool
would answer those 8x faster and without holding the machine, at the cost of the largest block of the phase
(pool, queue, jobs on six planes, 35-77 MB a clone). That is horizontal scaling for batch analysis.

Next steps if measurements on real agent questions ask for it, in this order:

1. one background clone (the user's machine is not held; no parallelism);
2. the pool of POC 038's [proposal](../../../tools/poc/038-ttd-replay-query-engine/proposal.md), for batch work.

## Blocks

Order: B, C, D, with E along the way and at the end.

### B. In-place replay (POC 040 causes 1-4, 6)

| # | Change | Measured today | Expected |
|---|--------|----------------|----------|
| B1 | A checkpoint restore keeps what memory holds: no `ForgetMemory()` per restore; the dirty tracker's writes since the last restore are the `written` set of `RestoreToMemory` | memory 0.74-1.1 ms (Pentagon: 951 pieces, 896 of them sound-card memory) | 6-38 µs |
| B2 | Internal seeks without the picture (POC 038 F3): verbs, memory-at and the frame cache never compose; the public seek keeps composing | the picture is 55-70 % of a seek (1.0-2.3 ms) | 0 for internal seeks |
| B3 | A window verb runs on from one seek instead of restoring every frame (where the verb replays frames in order) | W2: 2069 µs a frame against 823 continuous | about the frame's run |
| B4 | memory-at at a checkpoint (a frame start) reads the mapped pieces from the store, without touching the machine | 0.5-1.5 ms | tens of µs |
| B5 | Input playback wakes at the next event's time, not at every instruction (an atomic "pending" flag for live input instead of three mutex lock/unlocks a step) | W3 replay 805 µs against 352 in debug mode | about 352 µs |
| B6 | The session publish (`GetSessionInfo`, `GetHeapBreakdown`) not after every operation of a verb | ~6 % of the samples, ~0.25-0.65 ms a call with the live state save/restore | once per call |

Proof: today's verb answers unchanged (unit tests on W1-W3 shapes), POC 040 rerun before and after, the TTD
corpus tests, A/B benchmarks of seek and the verbs.

Status:

- B1 done. A `KnownMemoryScope` spans the frame loops of find-last, find (`all`, `changes_only`), the
  M1 enumeration (reverse-continue) and the write journal build. Inside it a restore trusts what the previous one
  left, apart from the pieces the trackers marked since. The live state put back, a tool edit and the scope's end
  forget it. Test: `ttdincrementalrestore_test.cpp` (memory equal to full restores, fewer pieces decoded). POC 040
  rerun (thread CPU time, loaded host, about ±20 %): W1 writes-all 1101 → 356 µs a frame, changes-all 1174 → 421;
  W2 writes-all 30 → 20.
- B2 done. The internal seeks (memory-at, the frame cache, a resume) already never composed; only the
  public seek does, and the user needs its picture. The public seek is a KnownMemoryScope instead, so the
  picture's restore of the frame before decodes only what differs from the seek's: seek W1 2446 → 1644 µs, W2
  4193 → 3082, W3 1815 → 1543 (no sound card memory there); mid-frame seek W1 2704 → 1807, W2 5026 → 3742, W3
  2799 → 2348. Next candidate: the picture's replay of the frame
  before lands exactly on a frame-start target, so the seek's own restore and the live state save and put back
  can go (one restore and one frame); needs a machine-state equality test on every model and the barrier case.
- B5 done. Live input and machine tasks raise an atomic flag; the per-instruction playback takes no
  mutex while it is clear and looks the frame start up once a frame. The step-work flag is set again when a
  producer queued between the check and the clear (a lost wake before). W3 (input playback): continuous replay
  891 → 477 µs a frame, exec-all 1251 → 802, writes-all 109 → 66; W1 and W2 (no input) unchanged.
- B4 done. A checkpoint keeps the CPU's four windows at its capture (`Memory::GetWindowMap`: offsets into
  the memory block and modes). memory-at space cpu at a frame start, up to the CPU's overshoot, decodes the mapped
  pages from the store (ROM read as it stands), the machine untouched; elsewhere, without a window map (a loaded
  session) or where the windows are not what the CPU reads (the Next's 8K slots, the Sprinter's redirected
  windows), it replays as before. POC 040 cpu-view at frame starts: W1 1372 → 22 µs, W2 1526 → 239, W3 534 → 32.
  Test: `ttdcpuviewstore_test.cpp` on nine models (view equal to the machine after a seek; 48K, Pentagon,
  Scorpion, Profi, ATM3, TS-Conf and +3 answer every frame start from the store).
- B6 done. The publish already ran once per outermost operation; its cost was the port journal's size,
  which compressed the open block on every `GetSessionInfo`. The compressed size is now kept until the open block
  changes. Seek W2: the publish was about 7 % of the samples, the compression half of it; seek W2 2848 → 2566 µs,
  W3 1170 → 861 (W1 within the noise).
- B3: little left after B1 (W1 writes-all 343 µs a frame against 272 continuous), and the verbs walk newest
  first, so there is nothing to run on from.

### C. HALT idle while TTD records and replays

POC 040 follow-up: a recording that may idle through HALT is exact (2998 of 2998 frames on W1-W3) and costs 39 %
less on a game that halts (W2: 930 → 568-591 µs a frame; the recording overhead +108 % → +27 %).

| # | Change |
|---|--------|
| C1 | `Z80::IdleStepsInert`: TTD coverage alone is no observer of an idle stretch; the idle marks the HALT's coverage once per stretch (the frame where it idles still shows the HALT executed) |
| C2 | The `IsRecording()` refusal goes (no reason recorded since `39417bffc`) |
| C3 | The same for a replay (`IsReplayActive()`), with the replay's input and journal wake-ups as idle limits |

Proof: a test per machine family that a recording made with the idle replays stepped to every checkpoint bit for
bit and the reverse; coverage scan, reverse-continue and find answers equal with and without the idle; the
recording A/B on `frame_profile_benchmark` rows.

Status: C1 and C2 done, in debug mode, with no new mode. The idle runs while no breakpoint or tap
watches the HALT (exec) or the byte after it (read), the access tracker does not count and no M1 trace or access
probe is armed; the debug memory interfaces count as pure under those rules. While recording, the floppy controller has to
be asleep: awake and inert it stamps its time on every step (`WD1793::handleStepAwake`), the stamp is in its
checkpoint state, and a replay steps (found by the CI gate on ATM3 idle: the blob differed, the behavior did not). The coverage index keeps the HALT in
every frame (each idle stretch starts with a real fetch). POC 040, thread CPU time: W2 recording 944 → 664-686 µs a
frame, and debug mode alone 795 → 544-547 (no TTD: 519-526); every recording replays exactly, 2998 of 2998 frames
on W1-W3. Tests: `HaltedIdle_Test` debug mode, TTD recording (traces and coverage equal), watched HALT per step.

C3 done. A replay idles through HALT as a recording does, under the same floppy rule (asleep while recording or
replaying). Its limits: the driver's (a barrier, the seek target) and the input playback's, which stops the idle at
the next event's CPU T (`TimeTravelHooks::InputIdleLimit`), so the event applies at the boundary it was recorded at;
live input queued or a replay frame not yet known refuses the idle. The port and bus journals need no limit: a
halted CPU reads nothing. POC 040, thread CPU time: continuous replay W1 426 → 275 µs a frame, W2 823 → 486; W3 523
(the 48K editor waits for a key by polling, no HALT). Every frame replays exactly, 2998 of 2998 on W1-W3. Tests:
`HaltedIdle_Test.HaltLoopPentagonTtdReplay` (seeks into idle stretches equal the per-step replay's) and
`HaltLoopPentagonTtdReplayWithInput` (keys pressed and released inside halted stretches; fails with the input limit
removed).

Not in this block: a separate TTD-only memory path (a third memory interface beside fast and debug). The CPU's
debug work costs nothing measurable, and such a path saves part of 41-60 µs a frame at most.

### D. Game-analysis feedback, round 2

| # | Feedback | Change (to design in detail when the block starts) |
|---|----------|----------------------------------------------------|
| D1 | coverage/scan: finding rare code takes about 600 requests | one request for "rare blocks" over a range: the executed keys whose frame count is at most N, with their frames |
| D2 | reverse-continue `all`: every hit is too much | an option for the first hit per PC only |
| D3 | memory-at `space: cpu` answers 409 on a stopped recording without recent frames (the agent recorded 3 frames to read memory) | answer from the last checkpoint and the journals; no recording needed |
| D4 | loading a tape from zero takes minutes | measure first (turbo with TTD, the tape's warp while recording), then fix the cause |
| D5 | port-events: the port value is not enough | each event carries the caller's PC, HL and the port mask of the read |

D1 done: `coverage-rare` (CLI `ttd coverage rare`, `GET /ttd/coverage/rare`, Lua `ttd_coverage_rare`, Python
`ttd_coverage_rare`, MCP `time_travel coverage_rare`). One pass over the range's coverage keys counts each key's
frames and keeps the frames of the keys seen in at most `max_frames` of them (default 1, at most 1000); neighbouring
keys of one page with the same frames join into a block (gaps up to 4 bytes for executed code, 1 otherwise), the
rarest first, cut at `limit`. A block names its page and offsets, and its Z80 addresses when the page was mapped in
exactly one window at its first frame (the checkpoint's CPU windows). Frames not indexed yet are counted, not guessed.

D2 done: `first_per_pc` on reverse-continue (`--first-per-pc`, MCP `reverse_continue`): each listed PC's newest hit
in the window, the search stops once every PC has one; `pcs_without_hit` names the others. v1 refuses it.

D3 done: memory-at `space: cpu` without `frame` (or at the machine's own frame past the history) reads the machine
now: `live: true`, the mapping and banks as they are, no recording needed. A frame past the history otherwise answers
409 naming the history's end and the machine's frame.

D4 done. The cause: the acceleration lock held turbo mode, turbo tape and fast tape off for the whole recording
(fast tape is on by default), so a tape loaded at the real tape speed. Turbo only takes the host's pacing away (the
frames are the same) and the tape trap was already a recorded edit (the black box's D29 path), so the engine's
recording now keeps all three (`ITimeTravelHooks::RecordsShortcuts`; v1 keeps its lock); the host speed stays at 1x.
Fast disk stays held, and is now held for the black box too: besides its trap it compresses the WD1793's timing,
which no edit records, and a DSS floppy boot recorded by a black box with it on replayed another FDC (frame 78, the
BetaDisk and WD1793 context blobs) - a bug the black box had since D29. Measured (unreal-qt offscreen, Pentagon,
`LOAD ""` from the prompt inside a recording): DIZZY_X_SAN-SAN.tap (348 s of tape, headerless blocks the trap
cannot take) 352 s at the old 1x, 15.3 s in turbo; turbo records about 1500 frames a second. The Qt menu now asks
the core (`refusalReason`, `CanSetSpeedMultiplier`) instead of repeating the rule; it also offered no host speed
during a black box, which the core allows. Tests: `TimeTravelController_Test.AnExplicitRecordingKeepsTurboAndTheTapeShortcuts`
(an AY tone, a trap in turbo, seeks equal the live states), `TapeLoading_Integration_Test.TTDRoundTripAcrossFastLoad`
(LOAD "" recorded in turbo, every frame's seek equals the run), the black box's fast disk hold.

D5 done. A port-events hit now says who read: a ULA read carries `rows` (the half-rows its address selects) and
`keys` (the keys it saw down, by the names `key` takes; one half-row only, a several-row read's 0 bit could be any
of their keys), from the record; `context` (default on; off while a recording runs, `true` pauses it; refused for a
file; v1 has none) adds `registers` (af bc de hl ix iy sp) at the IN / OUT instruction's M1 and `stack_top`, the
word at SP - the caller of a key routine that pushed nothing. The journals are unchanged (recording stays as it
was): `TimeTravelController::PortAccessContexts` finds each hit's M1 (the last at its PC in a window before it),
seeks there under one replay and puts the machine back verbatim. Test:
`TTDControllerPortJournals_Test.PortEventsCarryTheCallersRegisters` (a CALLed key routine: SP, A, the return
address, HL counting the calls, the machine unmoved), `TTDPortSearch_Test.KeyboardRowsAndKeysOfARead`.

### E. Checks

- `tools/build/test.sh`, zero warnings; gcc, MSVC and MinGW before landing.
- MCP: `tools/verification/mcp/run_all.py --launch` for every changed tool.
- The TTD corpus test with the baseline interval, every model (POC 038 proposal step 1).
- A/B benchmarks for every hot-path change (C, B5).
