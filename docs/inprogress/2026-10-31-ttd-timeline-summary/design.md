# TD-5 timeline summary: design

Requirements and acceptance: [README.md](README.md). Steps, tests, benchmarks: [plan.md](plan.md).

## 1. What the recording already holds

Every source below is filled by the recording as it is today; the query reads them and adds nothing to the capture
(TL-3). "Frame N" is the span from checkpoint N (the machine at the start of frame N) to checkpoint N+1.

| Source | Per frame fact | How the query reads a window | Cost |
|--------|----------------|------------------------------|------|
| Engine checkpoint cursors `busReadCursor`, `busWriteCursor`, `mediaReadCursor`, `busVectorCursor` (`TTDEngineCheckpoint`, timetravelengine.h) | IN count, OUT count, sector reads, interrupt vectors taken: cursor[N+1] - cursor[N] | index walk | O(frames), no decode |
| Region changes `ChangeCount(index, 0)`, `ChangesAt(index, 0)` (RAM region, 4 KB pieces) | RAM pieces changed in the frame; the screen pieces among them | per checkpoint | O(frames + changes), no decode |
| Checkpoint CPU state `TTDCpuState` | halted at the frame start | per checkpoint | O(frames) |
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

Not in the recording, so not in the timeline: HALT idle time inside a frame (only the frame-start `halted`), sound
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
| `bucket_size` | automatic: ceil(frames / `limit`) | frames per bucket |
| `limit` | 100 (1..500) | at most this many buckets (same rule as `coverage-summary`) |
| `buckets` | false | include the per-bucket rows; the phases are always there |
| `ports` | true | classify the port journals (tier 2); false keeps the call O(frames) |
| `min_phase` | 2 buckets | a run shorter than this joins a neighbour (§4) |

`TimeTravelController::QueryTimeline(window, options)` builds the result under `SessionOperation{Read}` (a live
recording is parked for the call, as `QueryCoverageSummary` does), in one pass over the window's checkpoints and one
pass over each journal.

## 3. The reply

Top level: `from_frame`, `to_frame`, `bucket_size`, `bucket_count`, `fps`, `phases[]`, `buckets[]` (when asked),
`events[]`, `missing[]`, `cost` (`frames`, `port_records`, `ms`).

A bucket (and a phase, which sums its buckets):

| Field | From |
|-------|------|
| `frame_start`, `frame_end` | the window |
| `executed`, `written`, `read` | coverage distinct keys (the bucket's union, as `coverage-summary`) |
| `ram_pieces` | RAM pieces changed (sum over frames) |
| `screen_frames` | frames that changed a displayed-screen piece |
| `halted_frames` | frames that started halted |
| `in`, `out`, `sectors`, `vectors` | cursor deltas |
| `keys` | the keys pressed (names, distinct), `key_events` |
| `input_other` | joystick / mouse / other input events |
| `key_polls`, `ear_reads`, `border_outs`, `beeper_toggles` | the ULA's port records (tier 2) |
| `devices[]` | per device on the port map: name, tags, `in`, `out` (tier 2); per media slot: `sectors`; per card with its own CPU / DMA: frames its state changed |
| `border_changes`, `paging_changes`, `length_changes` | frame-start state against the previous frame |
| `writes` | write records (write journal on only) |
| `tags` | the activity tags (§4) |

`events[]`: every marker, bookmark, media change, config change and replay source change in the window, with frame,
kind and text.

`missing[]`: each fact the session cannot give, with the reason (TL-6): `writes` (journal off), `ports` (journal
invalid: the reason from the session facts), `coverage` (frames not indexed), `screen` (the video mode's memory is not
known for this model, §5).

## 4. Phases

A bucket's **tags** come from its facts against the session's own levels; the same rule for every recording (TL-5):

| Tag | The bucket has |
|-----|----------------|
| `tape` | a tape marker says playing for part of it, or EAR reads dominate the IN records |
| `disk` | sector reads on any media slot, storage-tagged port traffic, or a disk write marker |
| `input` | key / joystick / mouse events |
| `net` | network-tagged port traffic or net events |
| `sound` | OUTs to any sound-tagged device (AY, Covox, SounDrive, GS, MoonSound, TS-FM, ...) or beeper toggles |
| `screen` | screen-changing frames in at least a quarter of its frames |
| `burst` | written keys above 4x the median of the window's buckets (an unpacker, a level build, a clear) |
| `idle` | most frames start halted and few keys executed |
| `paging` | paging changes |

A **phase** is a maximal run of buckets with the same tag set. A run shorter than `min_phase` buckets joins the
neighbour whose tag set is closer (fewer differing tags; the earlier one on a tie). Each phase carries its frame
range, its tags, the summed facts and the busiest bucket (most `executed`), so the agent can seek there or run
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

Tier 1 (all but the port classification and the write records): one pass over the checkpoints plus the coverage
decode `coverage-summary` already does. Target: 50,000 frames in under 100 ms on the Mac (the coverage decode is
about 10 ms per 5,000 frames today).

Tier 2 (`ports`): one sequential decode of the window's port records. A tape loader reads EAR about 1,000 times a
frame, so a minute of loading is about 3 M records (about 100 blocks). Measured in plan.md B1 before the default is
fixed; if a long window is too slow, the default becomes `ports: auto` (on up to a record budget, with the cut
reported in `missing[]`).

The reply: `limit` bounds the buckets; phases are at most the bucket count, usually a handful. The MCP text lists the
phases (one line each) and the events; buckets only when asked.

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
