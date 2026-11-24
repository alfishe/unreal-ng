# VDAC4 time travel: ESP keyframes and an input journal

Date: 2026-11-23. Status: landed on master (`1f0154632`, `293d25062`), checked live on DOOM (§9). Parent:
[design.md §10.1](design.md#101-ttd), open item in [TODO.md](TODO.md).

## 1. What the numbers say

Measured on DOOM-VDAC4 in its demo (TODO.md, 2026-11-23):

| What | Value |
|---|---|
| ESP memory in use | 887 of 2152 pieces of 4 KB (PSRAM 8 MB + SRAM 416 KB) |
| Pieces a frame changes | 7-95 (about 90 on a game tick, every third frame) |
| Every frame, zstd of the XOR | 45-62 KB/frame, 140-190 MB per minute |
| Pieces changed over 250 frames | 125 (500 KB): the working set is small and hot |
| Every 50 frames | 1.4 KB/frame, about 4 MB per minute |
| The Z80 side of the same recording | 5-10 KB/frame |

A checkpoint of the ESP memory every frame costs 15-30 times the rest of the machine, and every frame end would wait
for the ESP worker thread. A checkpoint every K frames costs little, if the frames between can be computed again.

## 2. The idea

The ESP app is a deterministic function of its inputs. They pass a narrow surface of `vdac4::EspAppHost`:

- the calls into it: `Advance(us)`, `StartApp(name, elf)`, `StopApp()`, `Command(command, bytes)`,
  `RunUntilLoopIdle(limit)`, `Reset()`;
- the answers its ports give: `linkRead` (a Tang LINK word), and the results of `reply`, `event` and `mediaRange`.

Everything else the app does goes out (LINK writes, FT812 words, PCM, log lines, events, `ft_open`, `app_done`). The
SD and resource calls of `zm_core_t` are not served, so no file data comes in.

So the ESP is recorded as:

- a **keyframe** every K frames: the host's whole state (memory as engine regions, the rest as a blob);
- an **input journal** since the keyframe, in the per-frame state blob.

A restore to frame f loads the keyframe k <= f and runs the ESP again from k to f on the journal: the ports answer
from the journal, and what goes out is dropped (the rest of the machine at f is restored from its own checkpoint).

At a frame end no ESP job is in flight (`Vdac4Board::OnFrameEnd` flushes the runtime), so a frame boundary is a clean
point for both.

## 3. Devices

| Device | Type | State | Regions |
|---|---|---|---|
| The Tang's FT812 | `Vdac2Memory` (42), `Vdac2` (43) | as the VDAC2 card: EveHostCard time, edges, chip state | RAM_G (3 MB), DL, REG, CMD, SPECIAL, INFLIGHT, through eve-emu's dirty bitmap |
| `vdac4-tang` | `Vdac4Tang` (78) | `Vdac4Tang::State`: personality, timers, FT_CFG, LINK FT_ADDR, scratch | - |
| `vdac4-esp` | `Vdac4Esp` (79) | the board's ESP clock, `Vdac4Esp::Save` (SPI slave, link, frame transport, firmware) | - |
| `vdac4-esp-cpu` | `Vdac4EspCpu` (80) | keyframe header, the host state at the keyframe, the journal since | PSRAM, SRAM, the HLE ROM's RW copy |

The FT812 reuses the VDAC2 types: it is the same `EveHostCard` code, the region sizes come from the chip.
`vdac4-esp-cpu` restores after `vdac4-esp` (the firmware's load touches the runtime).

## 4. Keyframes

- When: every capture the engine rescans (a session's first, the first after a restore: every piece is offered then,
  so the ESP memory of that frame is stored whatever the device says), every K frames (K = 50), and when the journal
  holds more than J bytes (J = 256 KB; an `APP_START` carries the ELF). The engine tells its region sources at the
  start of each capture, before the device states are taken: `ITTDRegionSource::TTDCaptureBegins(rescan)`.
- At a keyframe the device marks every ESP piece written; the engine compares each with its delta base and stores the
  ones that changed (XOR with the previous version, zstd). No write tracking in the interpreter or the JIT: the cost is
  one compare of 8.7 MB per keyframe.
- Between keyframes no piece is marked, so a checkpoint refers to the keyframe's versions, and the engine's baselines
  (every piece whole) take them from the delta base, the keyframe's contents.

## 5. The journal

A byte stream of tokens, varint numbers:

| Token | Payload |
|---|---|
| Advance | the target time, microseconds after the previous Advance's target |
| StartApp | name, ELF bytes |
| StopApp, Reset | - |
| Command | command byte, payload bytes |
| RunUntilLoopIdle | the limit |
| Read | a LINK word that differs from the last one read at that address |
| ReadSame | n: the next n LINK reads return the last word read at their address (polling loops) |
| Result | a port result (`reply`, `event`, `mediaRange`), zigzag |

Only the outermost call is journaled: `RunUntilLoopIdle` advancing the ESP, or a `StopApp` from inside a finished
command, are its own work. The addresses of the reads are not stored: the replay asks for the same ones.

A replay checks itself: a token of the wrong kind, or the stream ending early, is a divergence. The per-frame blob also
carries a hash of the host state (not its memory) at the frame end; the restore compares it after the replay.

## 6. Restore

1. The engine restores the regions: the ESP memory of keyframe k. Before it, the device lets a job in flight finish
   and drops its outputs, and marks every ESP piece as written (the engine hook `ITTDRegionSource::TTDBeforeRestore`,
   called where the controller asks what was written since the last restore): the live memory moved on without
   marks, and a restore re-runs it.
2. `vdac4-esp` loads the firmware's state; the runtime stops its app as state only (no LINK traffic, no journal).
3. `vdac4-esp-cpu` loads the host state of k, drops the JIT's code, replays the journal to f, checks the hash, and
   keeps the journal as the live one (recording continues from f).

The cost of a seek: the ESP's work from k to f, at most K frames (about 1 ms of host time per DOOM frame on the M3
Ultra), plus decoding the ESP pieces.

A replay that does not reach the recorded host state (or does not fit its journal) makes the position not
bit-exact: the engine asks the source after the load (`ITTDRegionSource::TTDLoadDiverged`) and the seek's check
reports `replay_diverged` (`not_bit_exact`, `GET /ttd/position` `check`). The hash covers the host state, not the
ESP memory: a memory difference that leaves the host state alike is not seen.

### 6.2 Replay points

A seek restores its frame f, then its picture restores the frame before (`ComposeDisplay`): two replays from the same
keyframe. The per-frame blob also carries the journal's length at the capture before, so a replay to f stops there
once and keeps a **replay point** (the host state, the read map, the ESP memory, the journal so far), then runs on to
f and keeps another. A restore whose keyframe is the same and whose journal begins with a point's goes on from the
longest such point: the picture's restore costs a copy of 9 MB, a step forward replays one frame. Three points are
kept (27 MB, least recently used out), only after a restore.

### 6.1 What a restore must not do

Found on the live check, fixed with the devices:

- **No ESP code runs between the engine's memory restore and the ESP devices' load.** The SD card's TTD state
  (`TTDEvoSdCard`) sets the Z-Controller's state, which told every SPI device its chip select again. The ESP slave
  took that for a bus access and ran the ESP up to now, on the restored memory with the old host state, before
  `vdac4-esp` was loaded. `ZControllerSpi::SetState` now announces through `SpiDevice::restoreSelect` (a level, no
  bus cycle); the ESP slave only takes the level, its own state comes with `vdac4-esp`. A reset and the config port
  stay bus activity (`select`).
- **No save while an ESP job runs.** The seek's picture (`ComposeDisplay`) saves the live state of every device
  while the replay's last frame end may still have an ESP job on the worker: the device waits for it
  (`WaitIdle`) before it reads the host or its memory.

## 7. Storage, what else is saved

- The journal compresses by run length (`ReadSame`), and the engine stores the blob as XOR with the frame before:
  a frame appends its tokens, the rest of the blob is unchanged.
- The keyframe's host state repeats in every blob until the next keyframe: XOR zero.
- The ESP pieces only change at keyframes.

- The engine's blob of `vdac4-esp-cpu` grows with the journal. The registry sized a region source's blob by its
  probe and dropped a longer one silently ("no state at this position"): a source now states its limit
  (`ITTDRegionSource::TTDStateWithoutRegionsLimit`, 4 MB here); 0 keeps the probe's size (VDAC2, Sprinter).
- The FT812's memory device (`Vdac2Memory`) answered the registry's probe "no state without regions" while its
  regions were not collected yet, which is always the case at pairing: every frame then stored its whole memory
  (3 MB, 31 KB/frame compressed on DOOM) besides the regions. It answers by the chip now (VDAC2 too).

## 8. Acceptance

- Unit: the host state round trip (save, run, load, run: the same outputs and memory), the journal replay (the same
  state and memory as the recorded run), the keyframe policy.
- Live (WebAPI, the M3 Ultra build host): DOOM recorded, seek back 10, 37, 100 frames: the ESP memory equals the one
  recorded at that frame; frames continued from the seek show the recorded pictures.
- Costs: bytes per minute, capture time per frame, seek time; recording off: no cost (A/B).

## 9. Results (2026-11-23, M3 Ultra build host, DOOM demo after 6000 frames)

| What | Value |
|---|---|
| Recording off | 1.94-1.96 ms per frame; the base commit 1.95-1.99 (4 interleaved rounds of 600 frames, `run_frames`) |
| Recording on | 2.80-2.85 ms per frame; the base commit 2.28-2.33 (it records no VDAC4 device, an incomplete history) |
| History | 18.7 KB per frame over 600 frames, 27 KB over 300 (one baseline of 8.7 MB included); the base 1.6 KB |
| of it the FT812's RAM_G (300 frames) | 12.8 KB per frame (DOOM's frames go through it) |
| of it the ESP memory (300 frames) | 7.4 KB per frame (one baseline of 8.7 MB plus a keyframe every 50 frames) |
| of it `vdac4-esp-cpu` (host state, journal) | 0.5 KB per frame; the journal grows about 115 bytes per frame |
| Host state at a keyframe | 26-27 KB |
| Seek | median 36-37 ms, 12-99 ms (280 seeks over a recording of 200 frames); 56 ms before the replay points |
| Seeks checked | 55 of 55, again after the replay points: ESP PSRAM + SRAM equal to the memory at that moment of the recording, seeks back, forward, to the end, on grids of 13, 25, 37 frames, recordings of 100-600 frames |

Before the fixes of §6.1 and §7 the same recording stored 57.8 KB per frame at 5.5 ms per frame, and only seeks to
a keyframe were right.

Check (a WebAPI script, not kept in the repo): record, hash the ESP memory through `GET /vdac4/esp/memory` at each
mark, seek to each mark's `/ttd/position`, compare the hashes; time the seeks for the numbers above.

## 10. Open

- A replay divergence is seen through the host state's hash only (§6): the ESP memory is not hashed per frame (about
  1 ms per frame).
- The blob's limit (4 MB) holds: a journal over 256 KB keys the capture that finds it, so the blob carries the host
  state and at most 256 KB of journal whatever one frame sent (an `APP_START` with its ELF). Only a host state over
  3.7 MB would pass it.
- The rest of a seek: the replay to f itself (on average 25 frames), the ESP pieces the engine decodes on every
  restore (all marked, `TTDBeforeRestore`) even when a replay point overwrites them, the FT812's full save in the
  picture's live snapshot.
- Resources of the firmware (`_resources`) stay in the `vdac4-esp` blob, stored per frame while held.
- K = 50 is not tuned: K = 100 halves the ESP memory's share and doubles the worst seek's re-run.
