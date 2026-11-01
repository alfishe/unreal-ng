# TD-5 timeline summary: design

Requirements and acceptance: [README.md](README.md). Steps, tests, benchmarks: [plan.md](plan.md).

## 1. What the recording already holds

Every source below is filled by the recording as it is today; the query reads them and adds nothing to the capture
(TL-3). "Frame N" is the span from checkpoint N (the machine at the start of frame N) to checkpoint N+1.

| Source | Per frame fact | How the query reads a window | Cost |
|--------|----------------|------------------------------|------|
| Engine checkpoint cursors `busReadCursor`, `busWriteCursor`, `mediaReadCursor`, `busVectorCursor` (`TTDEngineCheckpoint`, timetravelengine.h) | IN count, OUT count, sector reads, interrupt vectors taken: cursor[N+1] - cursor[N] | index walk | O(frames), no decode |
| Region changes `ChangeCount(index, 0)`, `ChangesAt(index, 0)` (RAM region, 4 KB pieces) | RAM pieces changed in the frame; with the layout captured at the checkpoint and the coverage keys, the screen bytes among them (§5) | per checkpoint | O(frames + changes), no decode |
| Checkpoint `haltedInterrupts` (Z80 `interruptsAcceptedHalted`) | the INT that ended the frame came while halted (a checkpoint is taken past that acknowledge, so the CPU state's own `halted` is clear there); live captures only, not in the session file (`halt` missing after a load) | per checkpoint | O(frames) |
| Checkpoint chipset `TTDChipsetState` | border, `p7FFD` (paging, displayed screen, ROM), turbo ratio at the frame start; a change against the previous frame | per checkpoint | O(frames) |
| Frame table `Frames()` | frame length (turbo / clock changes show as a different length) | per frame | O(frames) |
| Coverage index `TTDCoverageIndex` | distinct executed / written / read keys (what `coverage-summary` reports) | the summary's own path | O(frames x keys), zstd blocks of 64 frames |
| Input journal `TTDInputJournal` | key downs (names), joystick, mouse, other input | binary search + walk | O(log n + k) |
| External events `TTDExternalEvents` | tape play / stop / ..., disk write, debugger edit, NeoGS barrier | snapshot + range | O(n) |
| Bookmarks | the user's labels | snapshot + range | O(n) |
| Engine event log `TTDEventLog` | media change (0x0201, not in the marker journal), config change (0x0202), frame length change (0x0301), replay source change (RZX, 0x0302) | `CursorAt` + walk | O(log n + k) |
| Port journals `BusReads()` / `BusWrites()` (main CPU) | each record attributed to its device by the model's port map (`PortDecoder::getPortMapEntries`: mask / match, device name, tags - the map `/ports` shows): IN / OUT per device and per tag (keyboard, sound members AY / Covox / SounDrive / GS / MoonSound / TS-FM, storage members FDC / IDE / SD / CF, mouse, joystick, network, RTC, printer, serial, DMA, video, memory / ROM / screen latches); for the ULA also EAR reads, border OUTs and beeper toggles | blocks skipped by frame, then decoded; the port to device lookup built once per call | O(port records), zstd blocks of 32768 records |
| Media read journal `MediaReads()` | sector reads per media slot read from the host (`ide0.master`, `sd.zc`, a CD; the journal is what replays them). A floppy is no such slot: its image is held in the session (versions per checkpoint), so a disk read shows as FDC port traffic of a storage-tagged device and the `disk` tag | `CountUpTo` per frame | O(log n) per frame |
| Device regions `ChangeCount(index, region)` | which device memory regions changed at a checkpoint (the RAM of cards with their own CPU or DMA - GS / NeoGS, VDAC2 -, video RAM - Sprinter -, the Evo font RAM; TS-Conf and Next DMA write machine RAM, counted there) | per checkpoint | O(frames x devices), no decode |
| Write index `Writes()` (only when the write journal is on, off by default) | memory write records | `ForEach` over the window | O(records) |
| Session facts | port journal valid, input history complete, write journal segments | O(1) | O(1) |

A device's state blob may carry time fields that change every frame (a card's own clock), so a blob's change says
nothing about activity. The timeline counts only the device memory regions (card RAM, video RAM, fonts: what a CPU or
DMA wrote) and the port traffic of each device; the blobs (registers, clocks, palettes) are not counted.

Not in the recording, so not in the timeline: HALT idle time inside a frame (only whether the frame ended in HALT), sound
energy, a screen picture (the screenshot stream is registered but off), interrupts per frame outside RZX and the
vector machines. These are listed as limits, not estimated.

Retention: the timeline answers for the history the session keeps (segments drop whole). The coverage index and the
media read journal are not trimmed by the history limit; the timeline uses only their frames inside the history, so
every bucket's facts describe the same frames.

## 2. The query

Verb `timeline` (CLI `ttd timeline`, `GET /api/v1/emulator/{id}/ttd/timeline`, Lua / Python `ttd_timeline`, MCP
`time_travel` action `timeline`).

| Option | Default | Meaning |
|--------|---------|---------|
| `from_frame`, `to_frame` | the history | the window |
| `buckets` | false | add an overview of `limit` buckets; the phases are always there and never depend on it |
| `limit` | 20 (1..500) | at most this many overview buckets (same rule as `coverage-summary`) |
| `bucket_size` | automatic: ceil(frames / `limit`) | frames per overview bucket; an explicit size cuts the window at `limit` |
| `ports` | true | classify the port journals (tier 2); false keeps the call O(frames) |
| `min_phase_ms` | 200 | the shortest phase in machine time, and the window a frame's tags are measured over (§4) |

`TimeTravelController::QueryTimeline(window, options)` builds the result under `SessionOperation{Read}` (a live
recording is parked for the call, as `QueryCoverageSummary` does), in one pass over the window's checkpoints and one
pass over each journal.

## 3. The reply

Top level: `from_frame`, `to_frame`, `bucket_size`, `bucket_count`, `fps`, `phases[]`, `buckets[]` (when asked),
`events[]`, `missing[]`, `devices[]`, `truncated`, `cost` (`frames`, `port_records`, `ms`).

A phase (and an overview bucket; both sum their frames):

| Field | From |
|-------|------|
| `frame_start`, `frame_end` | the window |
| `executed`, `written`, `read` | coverage distinct keys (the bucket's union, as `coverage-summary`) |
| `ram_pieces` | RAM pieces changed (sum over frames) |
| `screen_frames` | frames that wrote a byte the picture reads (§5) |
| `halted_frames` | frames that waited in HALT for their INT |
| `in`, `out`, `sectors`, `vectors` | cursor deltas |
| `keys` | the keys pressed (names, distinct), `key_events` |
| `input_other`, `net_events` | joystick / mouse / other input events; network events |
| `ula_reads`, `border_outs`, `beeper_toggles` | the port records of the port map row bound to the #FE latch (tier 2). A keyboard poll and an EAR read are the same IN: a loader is told apart by the rate, not by the port |
| `devices[]` | per used device: `device` (index into the top-level `devices[]`: name, port tags), `in`, `out` (tier 2; a port belongs to the matching row with the most decoded bits); per used media slot (read from the host, table above): `slot`, `sectors`; per device memory region that changed (a card with its own CPU or DMA - GS / NeoGS RAM, VDAC2 RAM -, a video RAM, a font RAM): `region` (its name), `frames` (frames whose checkpoint found it changed; memory, so no clock field moves it) |
| `border_changes`, `paging_changes`, `length_changes` | frame-start state against the previous frame |
| `writes` | write records (write journal on only) |
| `tags` | the activity tags (§4) |

`events[]`: every marker, bookmark, media change, config change and replay source change in the window, with frame,
kind and text.

`missing[]`: each fact the session cannot give, with the reason (TL-6): `writes` (journal off), `ports` (journal
invalid: the reason from the session facts), `coverage` (frames not indexed), `screen` (a session from a file on a model
other than the ULA ones: its layouts are not kept, §5), `halt` (a session loaded from a file: the HALT waits are not kept there).

## 4. Phases

Phases are cut per frame, not per bucket: an earlier draft tagged buckets and merged runs of them, so the phases
moved with `limit` (100 buckets over 3,000 frames gave other boundaries than 20). Now the buckets are an overview
only, and `limit` changes nothing in the phases (a test holds it).

Each **frame** gets the regime tags of the window of `min_phase_ms` centred on it (10 frames at 50 Hz by default);
the same rule for every recording (TL-5):

| Tag | The window has |
|-----|----------------|
| `tape` | a tape marker says playing in a quarter of its frames, or 200+ #FE reads per frame while a tape is in the deck (an EAR loop; a keyboard scan is a few dozen, and a tight keyboard poll with no tape in - the ZX Next firmware menu reads #FE 3,000 times a frame - is no tape; a session that does not know the tape slot counts the reads alone) |
| `disk` | sector reads, storage-tagged port traffic or a disk write marker in a quarter of its frames |
| `net` | network-tagged port traffic or net events in a quarter of its frames |
| `sound` | OUTs to a sound-tagged device (AY, Covox, SounDrive, GS, MoonSound, TS-FM, ...) or beeper toggles in a quarter of its frames |
| `screen` | screen-changing frames in a quarter of its frames |
| `burst` | written keys 256+ per frame and above 4x the median frame of the window (an unpacker, a level build, a clear; the floor only guards a near-zero median: UMT23X's depacker writes about 510 a frame) |
| `halt` | most frames wait in HALT for their INT (a HALT-paced loop) |
| `paging` | paging changes in a quarter of its frames |

Two tags mark a phase but never cut it: `input` (a key press is an event, not a regime) and a rare `paging` change;
a phase carries them when any of its frames has them.

A **phase** is a maximal run of frames with the same regime tags. The shortest run under `min_phase_ms` joins the
neighbour whose tags are closer (fewer differing tags; the earlier one on a tie), and so on until none is left; two
neighbours that end up with the same tags join. Each phase carries its frame range, its tags, the summed facts and
the busiest frame (most distinct keys executed, else most port records), so the agent can seek there or run
`coverage-rare` over the phase.

Thresholds are relative (medians and fractions of the window), not constants tuned to a game. The synthetic program
(plan.md, S1) fixes the rule; UMT23X and the other hold-outs only check it.

## 5. The screen area

`screen_frames` needs the memory the displayed picture is built from at each frame, for every video family.

- **What the picture reads.** `Screen::DescribeDisplayedMemory(std::vector<ScreenMemorySpan>&)` lists the byte ranges
  the renderer reads in its current mode: a region (null = machine RAM, offset = page * 16 KB + offset in the page, or
  a device region by name) with an offset and a byte count. The base `Screen` covers the RAM page families by mode: ZX
  (6,912 bytes of page 5 / 7), ATM 16-colour and hi-res (both bit-plane halves of the video page and page - 4), ATM
  text (the two 0x629-byte text and attribute windows of both pages plus `device.EvoFontRam`), ZX-Evo text linear
  (page 8 / 10 plus the font), Pentagon 16-colour (pages 4+5 or 6+7), hardware multicolour (pixels plus the 8 x 1
  attributes at 0x2000), Profi hi-res (page 4 / 6 plus its attribute page, none in monochrome). Overrides: TS-Conf
  (VConfig mode at V_PAGE: ZX 6,912 bytes, 16 colours 128 KB, 256 colours 256 KB, text 16 KB plus 2 KB of
  V_PAGE ^ 1; the tile map, tile graphics and sprite graphics pages TConfig enables; the VDAC2 RAM, display lists and
  registers while VDAC2 shows), Next (ULA bank 5 / 7 with the Timex halves, LoRes, Layer 2 banks at 256 x 192 /
  320 x 256 / 640 x 256, the tilemap and its definitions), Sprinter (`sprinter.vram`, 256 KB).
- **Captured at the boundary.** The recording calls it at every checkpoint (FeedShadow), resolves the regions, merges
  the ranges and interns the layout in the engine (`TTDEngineCheckpoint::screenLayout`, an index; a layout repeats
  for thousands of frames, so a checkpoint costs one compare). The mode at a frame is the one the machine had there,
  never re-derived from the live machine.
- **Byte exact.** A frame is `screen` when a 4 KB piece it changed overlaps a range of its layout (at its start, or
  at its end after a mode switch inside it) and, in machine RAM with the coverage index on, the CPU wrote a byte
  inside the range (a coverage key is the byte's RAM address) - or no CPU write landed in that piece at all (a DMA or
  device write: the piece is all there is to go by). Outside RAM (device regions) or with coverage off the piece
  decides. Without this a 48K counted every frame: the ROM's FRAMES counter (#5C78) shares piece 1 of page 5 with
  the attributes.
- **Not counted:** palettes and sprite patterns held in device-state blobs (Next, TS-Conf CRAM / SFILE): a palette
  change alone is no `screen` frame.
- **Sessions from a file.** The layouts are not in the session file. A ULA model there falls back to the 7FFD page
  5 / 7 6,912 bytes; any other model answers `screen` in `missing[]` ("the session was loaded from a file, which does
  not keep this model's screen layouts"), never a guess.

Order (owner 2026-10-31): the ULA families first; when their quality and reply sizes are checked, every other family
and every peripheral at once, in the same task - a gap left for later turns into surprises later.

## 6. Costs

Target (owner 2026-11-01): a whole 50,000-frame window in under 1 s, with every option.

Tier 1 (all but the port classification and the write records): one pass over the checkpoints plus the coverage
decode. Each frame's keys are decoded four times (written for the cut, then executed / written / read for the facts of
its phase; three more with `buckets=true`), and a range's distinct keys are counted with one stamp per key (the key's
last range) instead of sorting them - the sort was three quarters of the call. What is left is the coverage index's
own decode (about 70 %); keeping the frames' keys in memory to save one decode would cost hundreds of MB on a
burst-heavy recording, so it is not done.

Tier 2 (`ports`): one sequential decode of the window's port records. A tape loader reads EAR about 1,000 times a
frame: UMT23X's 12,000 loading frames are 11.3 M records, about 80 ms of the 90 ms call.

Measured on the M3 Ultra (plan.md B1 / B2, 2026-11-01), one call against the agent's old loop of 126 calls (20
buckets) or 606 (100 buckets):

| Window | `timeline` (default / `ports=false` / +20 buckets) | old loop, 20 / 100 buckets | faster | smaller reply |
|--------|--------------------------------------------------|-----------------------------|--------|---------------|
| synthetic 3,000 frames | 37.5 / 25.7 / 54.6 ms | 10.4 / 11.8 s | 277x / 314x | 33x / 36x |
| synthetic 50,000 frames | 588 / 403 / 863 ms | 111 / 192 s | 189x / 327x | 74x / 117x |
| UMT23X load, 12,061 frames, REST | 93 ms (12 ms `ports=false`) | 3.1 s | 33x | 9x |
| same over MCP | 90 ms, text 807 chars | 3.1 s, text 7,295 chars | 34x | 22x JSON, 9x text |

The reply: phases only by default (2.7 KB on UMT23X); overview buckets on request, `limit` 20 by default: on UMT23X
5 / 10 / 20 / 50 / 100 / 500 buckets make the reply 1.8 / 2.6 / 4.1 / 8.6 / 16 / 74 times the phases-only one and
change no phase. The MCP text lists the phases (one line each) and the events; buckets only when asked.

## 7. Planes

- TTDControl verb `timeline` (Verbs, OptionsFor, dispatch as a read verb, like `coverage-summary`).
- CLI `ttd timeline [from] [to] [--buckets] [--no-ports] [--bucket-size N] [--limit N]`.
- WebAPI `GET /ttd/timeline` (query string; OpenAPI entry); the socket gets it as a call. Listed among the debug
  routes (`companion-routes_test.cpp`).
- Lua `ttd_timeline({...})`, Python `ttd_timeline(...)`.
- MCP `time_travel` action `timeline`: text = the phases, one line each (`frames 311-402 (1.9 s): burst, screen -
  written 9,214 keys, busiest bucket 340`), then the events and `missing`.
- Docs: command-interface.md, the plane docs, `.recipe/analysis/ttd-reverse-debugging.md` (start a recording's
  analysis with `timeline`).

## 8. Not in scope

- A `.ttd` file without loading it (port search can; the timeline can follow later).
- Sound energy, a picture per frame, HALT time inside a frame: not recorded (§1), and recording them would break TL-3.
- Labels such as "menu" or "gameplay": the tags say what the machine did; naming the phase is the agent's job.
