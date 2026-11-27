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
| `vdac4-q2` | `Vdac4Q2` (81) | the Live part of the Tang's `q2` state (`SaveQ2(Live)`: running, picture, the core's Evo side, the core's hash) | `vdac4.q2.sdram` (8 MB, marked at the ESP's keyframes only, §11) |

The FT812 reuses the VDAC2 types: it is the same `EveHostCard` code, the region sizes come from the chip.
`vdac4-esp-cpu` restores after `vdac4-esp` (the firmware's load touches the runtime).
`vdac4-q2` restores after `vdac4-esp-cpu`, which rebuilds the core (§11); the Tang keeps the model across
`core_back`, so the region's pointer stays put for the whole session ([quake2.md](quake2.md) §8.1).

## 4. Keyframes

- When: every capture the engine rescans (a session's first, the first after a restore: every piece is offered then,
  so the ESP memory of that frame is stored whatever the device says), every K captures (K = 25 in slot 0 and the FT812, §9.1; 50 while the Quake II core runs, §9.5), and when the journal
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
3. `vdac4-esp-cpu` loads the host state of k, replays the journal to f, checks the hash, and keeps the journal as
   the live one (recording continues from f). The CPU's predecoded and translated code stays when the bytes under
   it came back the same: the device asks the CPU to copy them before the restore (`EspAppHost::HoldCode`, in
   `TTDBeforeRestore`), the load compares (`xtensa::Cpu::KeepHeldCode`) and drops the code only on a difference
   (another app, another run of it). Without a hold a load drops the code, as before.

The cost of a seek: the ESP's work from k to f, at most K frames (about 0.5 ms of host time per DOOM frame on the
M3 Ultra with the code kept), plus decoding the ESP pieces.

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

### 9.1 Seek time: the code kept, K (2026-11-24)

A profile of the seeks (`sample`, 35 ms a seek): 63 % the replay; inside it the translated code 43 %, the block
interpreter 25 %, decoding and translating again 10 %: every restore dropped the CPU's code, and a seek restores
twice. Same host, DOOM, 24 marks on a grid of 37 frames, 72 seeks of each kind (random, back, forward), all exact,
ESP memory checked at 72:

| Variant | Seek, median | p90 | History in memory | Recording |
|---|---|---|---|---|
| K = 50, code dropped (before) | 31-33 ms | 41-45 ms | 44.9 KB/frame | 2.8 ms/frame |
| K = 50, code kept | 26-27 ms | 36-39 ms | 44.9 KB/frame | 2.9 ms/frame |
| K = 25, code dropped | 24-27 ms | 32-34 ms | 47.2 KB/frame | 2.9 ms/frame |
| K = 25, code kept (now) | 19-21 ms | 25-28 ms | 47.2 KB/frame | 2.9 ms/frame |

K = 25 costs 5 % of the history (the ESP keyframes twice as often) for 6-7 ms of every seek.

### 9.5 K per personality: the Quake II core at 50 (2026-11-27)

K swept on one build (`UNREAL_VDAC4_TTD_KEYFRAME`, one headless, muted instance per K, all K at once, launch order
rotated, two rounds; .20 M3 Ultra, load average 30-100 from other sessions, the same for every K). The streams are
byte for byte the same in both rounds; seek times agree within a few ms.

- **Quake II** (`_Q2.SPG`): recording from frame 300 (TS-Conf loader, `core_load` at ~2100, the map load) with
  `run_frames`, points at fixed frames, then 60 s of the demo in real time. 13 points, 26 seeks in a shuffled order,
  9 seeks across `core_load`, 9 step-backs.
- **Metal Slug** (`_MSLUG.SPG`, slot 0 with the ESP's app): recorded 60 s in real time after `MS BOOT` and one
  `MS PERF` window (as `msfps.py`), 7 points, 14 seeks.

| K | Q2 demo MB/min | Q2 session MB/min | Q2 file | Q2 seek median / p90 / max | MS MB/min | MS file | MS seek median / p90 / max |
|---|---|---|---|---|---|---|---|
| 10 | 72.8 | 45.6 | 300 MB | 20 / 58 / 89 ms | 78.0 | 92 MB | 36 / 47 / 67 ms |
| 25 | 40.7 | 33.4 | 228 MB | 19 / 166 / 184 ms | 63.0 | 77 MB | 53 / 93 / 105 ms |
| 35 | 33.2 | 30.8 | 213 MB | 19 / 137 / 149 ms | 58.8 | 73 MB | 33 / 42 / 153 ms |
| 50 | 27.8 | 28.6 | 199 MB | 18 / 181 / 326 ms | 55.6 | 70 MB | 63 / 111 / 181 ms |
| 100 | 21.4 | 25.9 | 183 MB | 19 / 382 / 490 ms | 52.3 | 66 MB | 85 / 287 / 310 ms |
| 200 | 20.3 | 25.3 | 180 MB | 23 / 466 / 622 ms | 50.2 | 64 MB | 197 / 470 / 532 ms |

The demo rate from the history counter; the session rate and the regions from the session file. Across `core_load`
13-19 ms at every K up to 100 (the loader's span is short of keyframes anyway), step back 16-18 ms (Q2), recording
4.6-5.0 ms/frame at every K (Q2 demo), 5.5-6.0 (MS): K does not show in the recording's cost.

- **Where the bytes go.** An ESP keyframe stores the ESP memory that changed since the last one and the CPU state;
  while the Quake II core runs it also stores the SDRAM pieces the frame and z buffers rewrote (§11): Q2 demo at
  K = 25: SDRAM 21.6, PSRAM 11.0, ESP CPU state 7.5 MB/min; at K = 50: 14.2, 7.6, 6.0. Above K = 100 the other
  keys (the journal over 256 KB, the firmware's LINK writes) come first: K = 200 and 400 store the same.
- **Where the seek goes.** The replay from the keyframe, a few ms per capture of the demo: the tail (p90, max)
  grows with K, the median is the points near a keyframe. DOOM (§9.1): 25 -> 50 is 5 % of the history for 6-7 ms
  of every seek.
- **The knee.** Q2: 25 -> 50 takes 32 % off the demo's stream and 13 % off the session file; 50 -> 100 only 23 %
  more of the stream for twice the tail. Metal Slug: 25 -> 50 only 12 % of the stream (9 % of the file) for a third
  more seek; DOOM 5 %. So K = 50 while the Quake II core runs (`kKeyframeFramesQ2`), 25 elsewhere
  (`kKeyframeFrames`): `TTDCaptureBegins` takes the interval of the Tang's personality at the capture. A change of
  personality keys the capture anyway (`Q2Epoch`), so a span never mixes the two. Keyframes only place what is
  stored: the replay and the restore are the same either way.
- **Checked on the default build** (no override, beside `UNREAL_VDAC4_TTD_KEYFRAME=25`, same round): Quake II
  stores what K = 50 stored (199 MB, 28.6 MB/min; the loader's span at 25), Metal Slug what K = 25 stored (77 MB).
- **Not taken.** A rule by the bytes since the keyframe: the bytes a keyframe stores are known only after the
  engine compared the pieces, after `TTDCaptureBegins` decided; and the seek's cost is the captures replayed, not
  the bytes. K = 35 for Q2: half the gain (-18 % of the stream).
- **Seen on the way, not K's.** Seeks to the loader's points read a stale `tang.q2.latched` (the count of the last
  run of the core): `LeaveQ2` now zeroes it, as a state without the core carries none (22/26 -> 26/26 exact). Metal Slug: 6-10 of 14 seeks
  show another picture than the live point at a pause in real time (the ESP's app on its worker thread), at every
  K, while the engine's check says exact and PC, memory, Tang agree; open (§10).

### 9.2 Seek time: the ESP memory left to the replay point (2026-11-25)

A profile of the seeks (DOOM, the grid above, `sample` of the seek phase only): of a seek, the memory restore
decoded the ESP pieces (all marked, `TTDBeforeRestore`) about a fifth of the time, the replay of the journal as
much, the picture's frame run a fifth, the picture's own restore, the FT812's full save and load in its live
snapshot the rest. A restore that goes on from a replay point (the picture's restore of the seek's frame always,
steps back and forth often) copies the point's 8.4 MB over all that decoding. Now the controller asks the device
first (`ITTDRegionSource::TTDLoadMayRestoreRegions` / `TTDLoadRestoresRegions` with the checkpoint's blob, read
through the restore's decoded copy, `TimeTravelEngine::DeviceStateForRestore`): when the load will go on from a
point, `RestoreToMemory` leaves those regions alone (their content unknown to the engine, so the next restore
writes them whole). A load that was told yes and finds no point reports a replay divergence (cannot happen: the
same blob, its own save). Same host, 4 interleaved rounds of 216 seeks (random, back, forward), load 7-24:

| | Seek, median | p90 | Fastest |
|---|---|---|---|
| Before | 36.2-36.9 ms | 46.6-47.8 ms | 16.9-17.9 ms |
| Regions left to the point | 35.2-35.3 ms (41.2 at load 24) | 45.3-45.4 ms | 15.6-16.0 ms |

About 1.3 ms of every seek (the 8.4 MB the picture's restore no longer copies twice), 1.6 ms of the fastest. All
seeks exact; the ESP memory (PSRAM and SRAM, 7.9 MB) hashed after every seek against the recording's. Guard:
`TTDEngineModels_Fixture.Vdac4EspMemoryWithoutAReplayPointIsRestoredByTheEngine` (no point: the engine still
restores memory changed behind it).

### 9.3 Seek time: the FT812 memory blob scanned by words (2026-11-25)

The FT812's memory blob (RAM_G, display lists, registers, command FIFO: 1.1 MB, zero runs of 64+ bytes dropped) is
written on every live snapshot, twice per seek (the seek's and its picture's). It was scanned a byte at a time;
`EveHostCard::TtdEncodeZeroRuns` looks at the aligned 8-byte words only (a run of 64 zeros holds whole ones) and
measures a run both ways from a zero word. Same bytes out (the stream is unchanged), byte scan as the reference:
`TTDVdac2ZeroRuns_Test.TheWordScanWritesWhatTheByteScanWrote`.

| 1 MB of | Byte scan | Word scan |
|---|---|---|
| no zero bytes | 0.37 ms | 0.08 ms |
| 10 % single zero bytes | 1.15 ms | 0.08 ms |
| 4 KB zero / 4 KB data | 0.43 ms | 0.09 ms |

In the DOOM seeks (`sample` of the seek phase): the encoder went from 12 % of `PresentPosition` (101 of 843
samples) to 3.8 % (27 of 702). Wall-clock A/B on a host at load 50-250 does not resolve it (5 rounds, 1080 seeks,
all exact). What the live snapshot still copies: the ESP memory (`PutSparse`, already by words, 8.4 MB, about 8 %
of a seek).

### 9.4 Seek time: the FT812 frame drawn when it is due (2026-11-25)

On VDAC2 (`test6.spg`, a seek every 24-37 frames) the FT812's drawing was 60 % of a seek. A quarter of it drew the
caller's frame again after the picture's replay: `TtdLoadState` called `EveSetOutput`, which catches the drawing up,
and a load leaves the frame's lines owed (the drawing is not state, `DrawingInvalidate`). The live state put back is
mostly loaded again by the next seek before it runs, so those lines were never shown. The checkpoint's load did the
same before the replay draws them anyway.

- `EveSetOutputAfterLoad`: the output without the catch-up; the lines owed are drawn (or passed) with it at the next
  catch-up, which comes before any input of theirs changes (a RAM_G write, a drawing register, the frame event).
  `TtdLoadState` uses it (`ConfigureOutput(false)`), and the switch into a replay right after a load
  (`AdvanceTo`, `Drawing()` changed) draws the lines owed with the replay's output, as the live run drew them.
- With the drawing deferred, a load can stand unseen by the renderer thread: the switch off of the asynchronous
  drawing and `EveSaveState` took the renderer's handles, context and metrics even when the loaded ones were not
  handed to it yet (the stale metrics block of a later frame; `TTDVdac2_Test.SeekInsideFrameShowsTheFrameDrawnSoFar`).
  A part the chip took itself that no operation carried on yet is now the chip's.

A/B (144 seeks per round, all exact): VDAC2 median 20.6 -> 17.6 ms on a quiet host (4 rounds, every one), 21.7 ->
18.9 ms at load 40; VDAC4 DOOM unchanged in the median (the FT812 is 5 % of its seek), minimum 15 -> 13.4 ms; TS-Conf
and Pentagon unchanged. The stream is unchanged.

Tried before and dropped (patches kept outside the tree):

- Decoding a restore's pieces on a pool of threads: VDAC4 20-25 % faster, but threads for one rarely used machine.
  Without a pool the restore's decode is 7 % of a VDAC2 seek.
- Keeping the engine's knowledge of the machine RAM across the picture's live state (`RestoreLiveState` writes back
  only the pieces that differ next time): no measurable change, since the restore is not where the time goes.

## 10. Open

- A replay divergence is seen through the host state's hash only (§6): the ESP memory is not hashed per frame (about
  1 ms per frame).
- The blob's limit (4 MB) holds: a journal over 256 KB keys the capture that finds it, so the blob carries the host
  state and at most 256 KB of journal whatever one frame sent (an `APP_START` with its ELF). Only a host state over
  3.7 MB would pass it.
- The rest of a seek: the replay to f itself (on average 12 frames), the ESP pieces the engine decodes on a restore
  no replay point covers (all marked, `TTDBeforeRestore`: the ESP moves on without marks), the ESP memory copied by
  the picture's live snapshot (`PutSparse`, about 8 %). On VDAC2: the FT812 drawing of the replay and of the
  picture itself (§9.4), about 45 % of a seek.
- Resources of the firmware (`_resources`) stay in the `vdac4-esp` blob, stored per frame while held.
- Fixed 2026-11-27: `Vdac4EspFirmware::Save` wrote the delivery in progress (`_delivery`, growing with every piece)
  before `_resources` and the app state, so every later byte moved each frame (0.43 MB per frame of the Quake II
  loader). The bytes now go last, from a 4 KB boundary of the state (`Vdac4EspStateWriter::AlignBlob`): a new piece
  changes only the pieces it lands in, and the parts before may change size within the step. Older states (the
  bytes in the delivery's header, no tail) still load. The Quake II loading run: 205 -> 22 MB stored.
- Fixed 2026-11-27: `vdac4.q2.sdram` was stored at every capture while Quake II draws (278 MB/min in the demo); now
  at the ESP's keyframes only (§11), 27 MB/min. The rest of the demo's stream is the ESP's own keyframes (PSRAM
  17 MB/min, the ESP CPU state 9 MB/min): K (§9.1) trades those for the seek's replay; K = 50 while the core
  runs since 2026-11-27 (§9.5).
- Metal Slug (slot 0, the ESP's app on its worker thread): a seek to a point taken at a pause in real time shows
  another picture in 6-10 of 14 seeks at every K, the engine's check exact (§9.5). Not looked into yet: the live
  picture at a pause or the restored one.

## 11. The Quake II core between keyframes (2026-11-27)

The `q2` core takes every input from the ESP's app through LINK writes, apart from three: the firmware's own writes
(the link watch's mark, a Z80 `TANG_WRITE`), `core_load` / `core_back`, and the Evo side (M_SEL, the Evo's FT
transfers, which arm the reset rule). So the core between two ESP keyframes is the core at the keyframe plus the LINK
writes of the journal's replay, which the replay used to drop.

- **Keyframe.** At each ESP keyframe `vdac4-esp-cpu` stores the whole core (`SaveQ2(Whole)`, 26 KB) next to the
  host state, before the journal (blob magic `VD4L`; the older `VD4K` without it still loads), and the SDRAM region
  is marked whole for that capture (`TTDVdac4Q2::TTDBeforeCapture` asks `LastCaptureKeyed`): the engine stores the
  pieces that changed since. Between keyframes the core marks nothing.
- **What forces a keyframe.** A change of `Vdac4Tang::Q2Epoch` (`EnterQ2`, `LeaveQ2`, a load of `q2`) or of
  `Q2OutsideInputs` (`FirmwareLinkWrite`, the firmware's path through `IVdac4TangLink`): the capture after it keys,
  so the journal of a keyframe holds only the app's writes.
- **Replay.** `EspAppHost::SetReplayLinkSink`: while it replays, the host hands the app's LINK writes (and FT
  writes) to the sink instead of the port. `vdac4-esp-cpu` loads the keyframe's core (or a replay point's core and
  SDRAM copy, §6.2) and feeds the sink into `Vdac4Tang::Q2ReplayLinkWrite` / `Q2ReplayLinkFtWrite`: the core alone,
  no Tang register, no picture.
- **Per capture.** `vdac4-q2` stores the Live part: running, picture size and counters, the Evo side
  (`Vdac4BomzhCore::SaveEvoSide`), the core's hash (`StateHash`, FNV-1a over the state's 64-bit words) and the
  picture's source. Its load takes the Evo side and compares the hash with the core the replay computed: a
  difference (a write that was not the app's and was not counted) is a divergence (`TTDLoadDiverged`, the seek's
  check reports `not_bit_exact` with "Quake II").
- **Restore order.** `vdac4-tang` -> `vdac4-esp` -> `vdac4-esp-cpu` (the core) -> `vdac4-q2` (the Live part, the
  picture). The SDRAM region is marked whole before a restore (`TTDBeforeRestore`): the live SDRAM moved on without
  marks.
- **TTD off.** Nothing per frame: no marks, no sink; `FirmwareLinkWrite` counts one integer while `q2` runs.

Numbers (live, .20, 2026-11-27, [quake2.md](quake2.md) §8.1): the demo 278 -> 58 MB/min stored, `vdac4.q2.sdram`
about 250 -> 27 MB/min; a whole session of 446 s 132 -> 43 MB/min; all seeks, steps, branches and session files
exact.

Tests: `Vdac4Tang_Test.Quake2ReplayFeedEpochAndLivePart`, `Vdac4BomzhState_Test.EvoSideGoesApartFromTheLinkSide`,
`TTDEngineModels_Fixture.Vdac4Quake2CoreRestoresAtEveryCheckpoint` (the firmware's writes key every capture) and
`Vdac4Quake2CoreOffTheJournalIsReportedNotExact`, `Vdac4EspFirmware_Test.DeliveryBytesGoLastOnAPieceBoundary`.
