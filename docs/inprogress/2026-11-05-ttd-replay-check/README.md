# TTD replay check

Status (2026-11-06): landed on github/master (`124fb512f`): the verb, the
seek check, the findings of section 6 fixed.

## 1. Problem

A seek restores the checkpoint at or before the target and replays the rest of the frame. Nothing checks that the
replay reproduces what the recording saw:

- `machinestatehash` hashes the machine, but only `CaptureRestoreSelfTest` uses it: one capture, one restore, no
  replay.
- The divergence corpus tests (`ttddivergencecorpus_test.cpp`) seek to frame boundaries. A seek to a boundary lands
  on the checkpoint itself, so they test restore and never run the replay.
- At run time only the port read journal counts anything: replayed IN values against the live devices' answers
  (`port_replay_value_mismatches`). A device that answers the same IN but evolves differently, state a restore
  misses, or an engine outside the CPU's IN (the ZX Next DMA reads ports itself, `nextdma.cpp`, bypassing
  `Z80::in` and the journal) diverge silently.

A silent divergence makes every answer after it wrong: the screen, the memory, the cause chain, the found events.

## 2. Method

Every frame has a checkpoint at its start, so frame N's replay has a reference: the checkpoint of frame N + 1, taken
from the live machine when it got there.

For frame N:
1. Restore checkpoint N + 1 and take the machine's state without its memory (A, the recorded truth: CPU, chipset,
   every registered device's whole state).
2. Restore checkpoint N, replay the frame to its end (the frame-end processing included, as a seek and a picture
   replay run it; the picture is not drawn, as on a skipped turbo frame), take the same state again (B).
3. Compare A and B: CPU registers, chipset latches and the CPU's position in the frame, every registered device's whole
   state (`TTDPeripheralRegistry::SaveLive`).
4. Compare memory - the model RAM and the engine's device regions - per 4 KB piece against checkpoint N + 1's stored
   versions: only the pieces N + 1 recorded a new version of and the pieces the replay wrote (the dirty trackers) are
   decoded and compared; every other piece holds N's version, which N + 1 shares. Without the replay's writes known
   (memory the engine does not track) every piece is compared.

The answer names what differs: the register and both values, the RAM page with the first offset and the byte count,
the device by its instance name with the first offset and the byte count. The machine is put back where it stood.

Frames whose replay cannot reproduce the recording by design are skipped and counted: a barrier in the frame (an
unrecorded change: a snapshot load, a tool edit without data), a cut at the next frame's start, the last frame (no
next checkpoint). Nothing is recorded for the check: it costs nothing while recording and nothing when not used.

## 3. Surfaces

- Verb `replay-check` on every plane (CLI `ttd replay-check`, REST `GET /api/v1/emulator/{id}/ttd/replay-check`,
  MCP `time_travel` action `replay_check`, Lua `ttd_replay_check`, Python `ttd_replay_check`, WebSocket `call`).
  Options: `from_frame`, `to_frame` (default: the whole kept history), `stride` (every n-th frame, default 1),
  `limit` (diverged frames to describe, then stop; default 8), `text`.
  Answer: frames checked, skipped (by reason), diverged; per diverged frame the differing items; time.
- Setting `check_seeks` (off by default): each seek also checks the frame it lands in, and the seek answer carries
  the outcome. `/ttd/status` counts checks and divergences and names the last divergence.

## 4. Acceptance

- A test per surface; a unit test that breaks a device's restore on purpose (a serializer that drops a field) and
  sees the check name the device.
- The whole corpus of creatable models: record, check every frame; every divergence found is a defect to fix (or a
  known limit with its mechanism).
- ZX Next with the DMA reading a port (gap: the DMA bypasses the port journal): record, check, report.
- Cost per checked frame, measured.

## 5. ZX Next DMA

`docs/inprogress/2026-10-07-zx-next/design-ttd.md` §2 said the Next refuses the port journals because its DMA moves
data without IN. That was the first version's gate; the engine records the journals on every machine since
2026-10-09 (ee81b3cbf), and `PortDecoder::TtdEnginesSealed`, the gate's leftover hook, was never called (removed).
The DMA's port reads go to the live devices in the recording and in the replay alike (`PortDecoder_Next`
`bus.readIo` calls `DecodePortIn`); the devices' answers follow from the checkpointed state and the journaled
input, and media sectors come from the media read journal.

Measured: `TTDReplayCheck_Test.R6_NextDmaReadingAPortReplaysExactly`, a DMA block of 64 reads of `#FE` into RAM every
frame while a key goes down and up through the journaled input: every frame replays exactly.

## 6. Findings

The check over the ROM boot of every creatable model (`TTDReplayCheckModels_Test.R5_*`, 14 models) found two
defects, both fixed:

| Machines | What diverged | Mechanism | Fix |
|---|---|---|---|
| Scorpion GMX, ATM 3 | frame 0: z80.t short of the frame end, the frame counter not advanced | the ROM switches the hardware turbo inside the frame; `ApplyHardwareTurboNow` rescales `z80.t` and the frame length at once, so `RunToFrameEnd`'s remainder, counted before the run, ended the run short of the frame end. Every frame replay (a seek to a frame end, the picture's composition) stopped short on such a frame | `RunToFrameEnd` runs on until the frame ends, on the geometry the switch left |
| ZX Next | device `nextstate`, 9..27 bytes in the AY generators (tone counters, noise) every frame | the audio render clock (`_tickT`, `_frameStartT`) was not in the image and a load reset it (`_timeKnown = false`): the replayed frame did not tick the generators at all; with the output stage off (turbo without audio) they did not tick while recording either, so the state depended on host audio settings | image version 17 carries the clock; with a TTD session recording or replaying the generators tick with the output stage off too (the TSFM rule) |


## 7. Cost

Measured 2026-11-05 (release build, a ROM boot, minimum over repetitions on a loaded shared host), ms:

| Model | live frame, TTD off | live frame, recording | one checked frame | per frame of a 20-frame check |
|-------|--------------------:|----------------------:|------------------:|------------------------------:|
| Pentagon | 0.21 | 0.28 | 0.31 | 0.28 |
| TS-Conf | 0.43 | 0.96 | 2.9 | 1.0-2.6 |
| ATM 3 | 0.00 (halted, idle skip) | 0.19 | 2.5 | 1.2 |
| ZX Next | 0.56 | 1.15 | 1.8 | 0.96 |

The first version took 4.3 ms (Next), 5.3 ms (ATM 3) and 6 ms (TS-Conf) per checked frame. Profiling found:

- two whole-state copies (2-4 MB of RAM, the framebuffer) and a byte loop over them per frame: memory is now
  compared through the engine's piece versions (section 2, step 4);
- the piece versions looked up one by one (`VersionAt` walks back to the last full table: 1024 lookups per frame on
  a 4 MB machine) - the candidates come from checkpoint N + 1's change list and the trackers, the version in memory
  from the engine's own table (`VersionInMemory`);
- every restore decoded each device state whole (the Next's 256 KB system area: 64 pieces, 0.23 ms per restore): the
  engine keeps the pieces it decoded last per device and decodes only the versions that changed (`DecodedState`,
  valid while the store's `ReuseGeneration` stands - a freed version's id may come back with other content);
- `NextStateCodec::Load` saved the whole image once only to learn its length, and every save grew a new buffer: the
  TTD serializer passes the length it knows and reuses its buffer;
- the replayed frame drew its picture: the frame-end batch render (`RenderFrameBatch`, ScreenHQ off) did not follow
  the screen's draw hold. It does now; on the loop's own frames the hold is clear at the frame end or follows the
  turbo decimation, so nothing else changes. The Next's replayed frame went from 1.03 to 0.62 ms.

What is left per frame is the replay itself and two incremental restores. A check run by the verb pays one more
whole-memory restore for its first frame, the live memory being unknown to the engine (1.2-1.3 ms on a 4 MB
machine), and one copy of the caller's state each way. A seek's check runs inside the seek's own restores (the memory
it left is known), so its restores stay incremental.
