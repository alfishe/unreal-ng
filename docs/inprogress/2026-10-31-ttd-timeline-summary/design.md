# TD-5 timeline summary: design

Requirements and acceptance: [README.md](README.md). Steps, tests, benchmarks: [plan.md](plan.md).

## 1. What the recording already holds

Every source below is filled by the recording as it is today; the query reads them and adds nothing to the capture
(TL-3). "Frame N" is the span from checkpoint N (the machine at the start of frame N) to checkpoint N+1.

| Source | Per frame fact | How the query reads a window | Cost |
|--------|----------------|------------------------------|------|
| Engine checkpoint cursors `busReadCursor`, `busWriteCursor`, `mediaReadCursor`, `busVectorCursor` (`TTDEngineCheckpoint`, timetravelengine.h) | IN count, OUT count, sector reads, interrupt vectors taken: cursor[N+1] - cursor[N] | index walk | O(frames), no decode |
| Region changes `ChangeCount(index, 0)`, `ChangesAt(index, 0)` (RAM region, 4 KB pieces) | RAM pieces changed in the frame; the screen pieces among them | per checkpoint | O(frames + changes), no decode |
| Checkpoint `haltedInterrupts` (Z80 `interruptsAcceptedHalted`) | the INT that ended the frame came while halted (a checkpoint is taken past that acknowledge, so the CPU state's own `halted` is clear there); live captures only, not in the session file (`halt` missing after a load) | per checkpoint | O(frames) |
| Checkpoint chipset `TTDChipsetState` | border, `p7FFD` (paging, displayed screen, ROM), turbo ratio at the frame start; a change against the previous frame | per checkpoint | O(frames) |
| Frame table `Frames()` | frame length (turbo / clock changes show as a different length) | per frame | O(frames) |
| Coverage index `TTDCoverageIndex` | distinct executed / written / read keys (what `coverage-summary` reports) | the summary's own path | O(frames x keys), zstd blocks of 64 frames |
| Input journal `TTDInputJournal` | key downs (names), joystick, mouse, other input | binary search + walk | O(log n + k) |
| External events `TTDExternalEvents` | tape play / stop / ..., disk write, debugger edit, NeoGS barrier | snapshot + range | O(n) |
| Bookmarks | the user's labels | snapshot + range | O(n) |
| Engine event log `TTDEventLog` | media change (0x0201, not in the marker journal), config change (0x0202), frame length change (0x0301), replay source change (RZX, 0x0302) | `CursorAt` + walk | O(log n + k) |
| Port journals `BusReads()` / `BusWrites()` (main CPU) | each record attributed to its device by the model's port map (`PortDecoder::getPortMapEntries`: mask / match, device name, tags - the map `/ports` shows): IN / OUT per device and per tag (keyboard, sound members AY / Covox / SounDrive / GS / MoonSound / TS-FM, storage members FDC / IDE / SD / CF, mouse, joystick, network, RTC, printer, serial, DMA, video, memory / ROM / screen latches); for the ULA also EAR reads, border OUTs and beeper toggles | blocks skipped by frame, then decoded; the port to device lookup built once per call | O(port records), zstd blocks of 32768 records |
| Media read journal `MediaReads()` | sector reads per media slot (slot names `fdd.a`, `sd.zc`, `ide0.master`, ...) | `CountUpTo` per frame | O(log n) per frame |
| Device regions `ChangeCount(index, region)` | which device states changed at a checkpoint (cards with their own CPU or DMA: GS / NeoGS, TS-Conf DMA, Next DMA / copper, Sprinter accelerator) | per checkpoint | O(frames x devices), no decode |
| Write index `Writes()` (only when the write journal is on, off by default) | memory write records | `ForEach` over the window | O(records) |
| Session facts | port journal valid, input history complete, write journal segments | O(1) | O(1) |

A device state region may carry time fields that change every frame (a card's own clock); such a region's change
count says nothing about activity. Each device that has one is listed at step 4 (plan.md) with what it reports
instead (its port traffic, or the change of the fields that are not time), checked per device, never assumed.

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
| `screen_frames` | frames that changed a displayed-screen piece |
| `halted_frames` | frames that waited in HALT for their INT |
| `in`, `out`, `sectors`, `vectors` | cursor deltas |
| `keys` | the keys pressed (names, distinct), `key_events` |
| `input_other`, `net_events` | joystick / mouse / other input events; network events |
| `ula_reads`, `border_outs`, `beeper_toggles` | the port records of the port map row bound to the #FE latch (tier 2). A keyboard poll and an EAR read are the same IN: a loader is told apart by the rate, not by the port |
| `devices[]` | per used device: `device` (index into the top-level `devices[]`: name, port tags), `in`, `out` (tier 2; a port belongs to the matching row with the most decoded bits); per used media slot: `slot`, `sectors`; per card with its own CPU / DMA: frames its state changed (step 4) |
| `border_changes`, `paging_changes`, `length_changes` | frame-start state against the previous frame |
| `writes` | write records (write journal on only) |
| `tags` | the activity tags (§4) |

`events[]`: every marker, bookmark, media change, config change and replay source change in the window, with frame,
kind and text.

`missing[]`: each fact the session cannot give, with the reason (TL-6): `writes` (journal off), `ports` (journal
invalid: the reason from the session facts), `coverage` (frames not indexed), `screen` (the video mode's memory is not
known for this model, §5), `halt` (a session loaded from a file: the HALT waits are not kept there).

## 4. Phases

Phases are cut per frame, not per bucket: an earlier draft tagged buckets and merged runs of them, so the phases
moved with `limit` (100 buckets over 3,000 frames gave other boundaries than 20). Now the buckets are an overview
only, and `limit` changes nothing in the phases (a test holds it).

Each **frame** gets the regime tags of the window of `min_phase_ms` centred on it (10 frames at 50 Hz by default);
the same rule for every recording (TL-5):

| Tag | The window has |
|-----|----------------|
| `tape` | a tape marker says playing in a quarter of its frames, or 200+ #FE reads per frame (an EAR loop; a keyboard poll is a few) |
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

`screen_frames` needs the memory the displayed picture is built from at each frame, for every video family:

- RAM page families: `Screen::GetDisplayedRAMPages(mode, model, p7FFD)` already names the pages per video mode (ZX
  pages 5 / 7, ATM 16-colour / hi-res / text bit-plane pairs, ZX-Evo text, Pentagon 16-colour, hardware multicolour,
  Profi hi-res); the pieces are those pages' 4 KB pieces the mode reads (the 6,912 bytes of a ZX screen are pieces
  0-1 of its page). The mode at a frame comes from the checkpoint's latches through a pure function of the family
  (the same decision its restore makes), never from the live machine.
- Families whose mode or video memory lives in a device state: TS-Conf (VConfig, VPage, the tile / sprite pages),
  Next (ULA, Layer 2 banks, tilemap, sprites), Sprinter (its own video RAM region). Their device region is decoded only
  at checkpoints where it changed; the screen pieces follow from it.
- A family not resolved yet answers `screen` in `missing[]` for its frames, never a guess; the plan does not end
  with such an entry for a corpus model.

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
