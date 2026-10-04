# TODO — TTD v1 → v2 migration

Status: **Phase 0 done, Phase 1 next.** v2 is a new engine, `ttd::TimeTravelEngine`, built next to v1 and checked against it ([engine-approach-and-naming.md](engine-approach-and-naming.md)); decisions D1–D33 in [engine-decisions.md](engine-decisions.md). Roadmap: [README.md](README.md); where the earlier steps went: [README §5](README.md#5-former-step-names).

After every phase: the quality bar of D33 on the whole benchmark matrix (identical restores; file size, memory and capture work not larger than v1's in any case, for the same history kept; seek time within PR-5).

Still open for the user: the default memory budget (Phase 4) and the integrity and versioning mechanism ([integrity-and-versioning.md](integrity-and-versioning.md), Phase 4, Step 1). Neither blocks Phase 1.

## Phase 0 — Preparation (done)

- [x] Step 0 of the merge strategy: `PeripheralId` table and notification enum on master
- [x] Step 1 — Make v1 honest (done 2026-09-28)
- [x] Step 2 — Benchmark harness (done 2026-09-29, [results](v0b-benchmark-results.md))
- [x] Step 3 — Merge the feature branches
- [x] ~~Step 4 — Checkpoints inside a frame~~ (dropped 2026-09-29)
- [x] Since: experiments E1–E6 ([POC 011](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md)); v1 stores compressed data at its exact size (`67e5aff28`)

## Phase 1 — Engine core: memory that costs only what changes

Design: [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md).

- [x] Step 1 — Engine skeleton and verification: `TimeTravelEngine`, machine time, frame table, positions with a branch, optional streams; v1 file reader (`bench/ttdv1feeder`); the oracle against v1 (`TTDV1Feeder_Test`: every corpus checkpoint identical); both engines in the benchmark (`UNREAL_TTD_BENCH_ENGINE=all`) — branch `ttd-engine`, 2026-10-02
- [x] Step 2 — Piece store: change stored once, encoded once (T = 128 B), chain limit per piece (K = 50), arena with exact sizes, dependencies, shareable by sessions — RAM payload already below v1 on every `ci` case (up to −28%)
- [x] Step 3 — Regions and the reference table: per-checkpoint change records (8 B per changed piece) + a full copy-on-write table every 64 checkpoints; parent link — references below v1 on every measured case (ZX-Evo 4,103 → 78 B per frame, Pentagon game 128 → 62); copy-on-write blocks alone lost to v1 on small busy machines (TDD §4.4)
- [x] Step 4 — Live capture next to v1 (`TimeTravelManager::SetShadowEngine`), delta base for changed pieces only — capture p50 v1 → engine: ZX-Evo 355 → 18.5 µs, Pentagon game 426 → 64 µs; counted work below v1 on every measured case; `TimeTravelManager_Shadow_Test` checks every live frame against v1
- [x] Step 5 — Restore only the pieces that differ (`TimeTravelEngine::RestoreToMemory`) — memory restore p50 v1 → engine: ZX-Evo 731 → 125 µs, Pentagon 1024 174 → 27; Pentagon game 311 → 337 µs (+8%, hot pieces with long chains; p99 568 µs, within PR-5)
- [ ] Step 6 — Device memory as regions, large memories first (`ITTDRegionSource`, `TTDRegionTracker`; registered next to the device's serializer, fed by shadow mode):
  - [x] NeoGS RAM (2–4 MB) and flash (512 KB): marks in `NeoGSMemory::write` / `poke` / `powerOn` and in the flash chip's program / erase / load; A/B `BM_HostFrame_NeoGS_*` within noise (−0.6…+0.7%)
  - [x] MoonSound wave RAM (up to 1 MiB): taken from the wave memory's own dirty bitmap before each capture, no new hook
  - [x] General Sound RAM (128–512 KB): marks in `writeMem` and on a state load; the engine's GS blob holds the 95-byte registers only (`TTDStateWithoutRegions`), the v1 feeder splits v1's GS blob the same way. GS 512 upload, bytes per frame (memory pieces + device state): v1 2,304 → engine 1,624; capture p50 293 → 20 µs
  - [ ] The lightweight GS player's upload store
  - [x] Machines with large RAM — region 0 on every model, no per-model code: `TimeTravelManager_ShadowModels_Test` checks every frame against v1 on Pentagon, Scorpion, ProfScorpion, Profi, ATM710, ATM450, ZX-Evo, TS-Conf and Sprinter. Matrix (600 frames, v1 → engine): capture p50 TS-Conf 223 → 6.3 µs, ZX-Evo 273 → 16.6, ATM710 112 → 7.5, Profi 74 → 4.9; memory restore p50 TS-Conf 585 → 32 µs, ZX-Evo 728 → 124; references TS-Conf / ZX-Evo 4,103 → 87 / 111 B per frame
  - [x] Region records per checkpoint only for regions that changed (an idle card costs nothing per frame; references below v1 on every model, 48K 96 → 60 B per frame)
  - [x] Sprinter video RAM (256 KB) and fast RAM (64 KB): compared at each capture (no write hook: the fast RAM is written through the generic CPU path); restore rebuilds the palette and the INT list; the engine's blobs hold the version byte only. Sprinter per frame v1 → engine: 13,290 → 1,611 bytes, capture p50 367 → 17.8 µs, memory restore 754 → 169 µs
  - [x] VDAC2 (FT812): the chip's seven memory regions (RAM_G 1 MB, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT 1.06 MB) from eve-emu's own dirty bitmap; restore = copy + `EveMemoryRestored`, as v1's blob; the engine's `Vdac2Memory` blob is the 8-byte header. Test writes RAM_G / display list / FIFO every frame through #77/#57 and checks each checkpoint against v1's blob (a mutation dropping the bitmap fails it). TSL-VDAC2 idle, 600 frames, v1 → engine: capture p50 603 → 5.7 µs (v1 encodes 2 MB per frame), memory restore 585 → 53 µs, resident 23.7 → 12.8 MB
  - [x] ZX-Evo AVR EEPROM (4 KB) and SMUC EEPROM (2 KB): regions 15, 16, compared at each capture (one write path each, rarely used; the comparison costs a fraction of a microsecond per frame while recording). Test: EepromsAreRegionsOfEveryCheckpoint (ATM3, TS-Conf, Scorpion)
  - [ ] Each region keeps a delta-base copy of its memory (the engine's working memory, fixed: 2.3 MB for VDAC2): look at dropping it for regions whose pieces are mostly zero
  - [ ] ZX-Evo AVR and Scorpion SMUC EEPROMs
- [ ] Phase check: D33 on the matrix, bytes per stream against the E6 model

## State registry gaps ([state-registry.md](state-registry.md#gaps))

Found by the 2026-10-02 audit. Gaps 1–16 break replay in v1 today; each is fixed in v1 (it runs the emulator until Phase 5) and carried into the engine. Order follows severity.

- [x] 1 Network references after eviction: v1 only, not fixed (v1 is for verification); the engine keeps referenced payloads (D24) and evicts memory to its file, not history (D28). Phase 3 test: a payload referenced by a surviving checkpoint survives a retention cut, record numbers absolute
- [x] 2 GS lightweight: not recorded by design, named in the header (flag bit 11); region 2 stays unused
- [ ] 3 Region-only memories restored (NeoGS, MoonSound): needs the engine restore path, or v1 blobs until Phase 5
- [x] 4 WD1793 command context on every Beta machine (registered with the BetaDisk; fixtures re-recorded). Test FDCCommandInFlight_RestoreContinuesIt: restore inside a Read Track
- [x] 5 ZX keyboard matrix in the checkpoint (decision 37); keys held on the host at a resume enter as new events
- [x] 6 `scorpion_turbo` stored, waits resynced after a restore
- [x] 7 `current_z80_frequency` restored (derived from the multiplier)
- [x] 8 SMUC: `pFFBA` / `p7FBA`, IDE registers, NVRAM I2C state (id 44); the contents go with gap 10
- [x] 9 TS-Conf: ZX-Evo AVR volatile bytes (id 45)
- [x] 10 ZX-Evo AVR and SMUC EEPROMs as regions 15, 16 (engine; compared at each capture, no write hook; decoders hand them over through `PortDecoder::CollectTTDRegionSources`)
- [ ] 11–13 Media: written sectors, write-protect toggles, queued swaps as events and media versions (decision 25)
- [x] 14 ESP module `_zxLine`
- [ ] 15 ZX-Evo F12 timer on emulated time
- [ ] 16 Edge cases: NMI pending, the +3 floating-bus byte and the RZX playback position (2026-10-04, Phase 3 Step 2) done; disk autostart, "incomplete" network state open
- [ ] 17 Telemetry streams (decision 35), starting with the VDAC2 line-budget metrics if emulation does not read them
- [ ] 18 `ttd.ksy:532` NeoGS memory note
- [ ] Fill the registry's Size and Variability columns from per-stream benchmark measurements

## Phase 1 check ([phase-1-results.md](phase-1-results.md))

- [x] Full matrix against decision 33: bytes, memory, counted work not above v1 on all 46 configurations (`tools/verification/ttd-bench/ttd_engine_d33.py`; baseline `testdata/ttd/bench/engine-phase1-full.json`). Fixed on the way: checkpoint records in a deque, arena chunks growing from 64 KB
- [x] v1 → engine oracle on the whole corpus (9 sessions)
- [x] Timings on an idle host (load 5.7-12.4): PR-5 holds (restore p99 ≤ 1.8 ms); PR-3 as a ratio fails on 25 of 46 (v1: 39), the slow captures follow the frame's work
- [x] PR-3 is now "capture p99 ≤ 1 ms, the first frame not counted" (2026-10-03): met on all 46 (at most 280 µs)

## Phase 2 — Device state with versions

Design: [phase-2-device-state-tdd.md](phase-2-device-state-tdd.md).

- [x] Step 1 — Device registry: type id u16 + instance name, layout version, restore order, firmware fingerprint (2026-10-03; descriptor and device table, see the TDD's as-built note)
  - [x] Firmware fingerprints (`ttd::FirmwareFingerprint`, taken when the image loads): GS ROM, ATM2 keyboard controller ROM, MoonSound wave ROM (2026-10-03). The Sprinter BIOS is the machine ROM: with every model's ROMs it belongs to the configuration fingerprint (Phase 3, Step 4)
- [ ] Step 2 — Unchanged state shared, changed fields only, time-derived counters
  - [x] Device states stored only when they change, as differences (device-state regions, 2026-10-03): device bytes 3-10x below v1, D33 on all 46
  - [x] Time fields (engine-side residual from a line; MoonSound, NeoGS, TSFM declare theirs): device bytes 7-28% lower, none worse (2026-10-03)
  - [x] ATM2 keyboard controller clocks as time fields (73 → 61 B per frame on ATM710); 1-byte time fields re-anchor beyond ±15 (2026-10-03). What still changes every frame (AY / TSFM noise generators, TSFM decimator phases, the controller MCU's RAM) is real device state (Q1)
  - [x] Changed-ranges encoding in the piece store (`Encoding::Ranges`), for memory and devices alike: totals 7-27% lower, compressions per frame 2-8x fewer, none worse (2026-10-03)
- [x] Step 3 — Restore result in the engine: `RestoreDevices` with issues per device, the v1 oracle on 10 models (2026-10-03); surfaces in Phase 5
  - [x] ~~Devices implement `TTDResetToPowerOn`~~ — dropped 2026-10-03: a device without state cannot occur within a session (D38); a machine reset stops the recording, a new recording is a new session
  - [x] Damage and `CheckSession` (2026-10-03): a version failing its CRC32C is `DataDamaged` with the frames it reaches (to the piece's next change that does not depend on it), device named for device state; `CheckSession` checks every version once and lists damage, frames without a device's state, devices this machine lacks and firmware differences, without touching the machine
- [x] Step 4 — Sound devices on the contract; the device set fixed for a session (D38)
  - [x] Every device checked against its descriptor and the engine's device table built at registration (refused by name); `TTDSyncedTime` on TSFM, MoonSound, GS and NeoGS, checked at every capture and after every restore (2026-10-03)
  - [x] ~~Device-set change as an event~~ — dropped 2026-10-03: the device set is fixed for a session (D38); a change while recording stays refused

- [x] Phase 2 results against the quality bar (D33): [phase-2-results.md](phase-2-results.md), baseline `testdata/ttd/bench/engine-phase2-full.json` (2026-10-03)

## Phase 3 — Everything a replay needs

Design: [phase-3-replay-inputs-tdd.md](phase-3-replay-inputs-tdd.md).

- [ ] Step 1 — One event stream (input, external events, markers, port reads, bus data, DMA, network)
  - [x] Event log and payload store; no barriers in a sealed replay (only v1 records without data) (2026-10-03)
  - [x] v1's input, network and markers reach the engine: live in shadow mode at every frame boundary, and from v1 files (`FeedV1Events`); the corpus imports one for one (2026-10-03)
  - [x] `IN` / `OUT` bus journals with per-checkpoint cursors (v1's block format; shadow at each boundary and at stop, v1 files whole; counted in D33 memory on both sides) (2026-10-03)
  - [x] The engine replays a frame from its own data (seek inside a frame) and matches v1: the first full-scenario A/B (2026-10-03). `TimeTravelManager::SetReplaySource(engine)`: restore, input and bus data from the engine, v1's replay loop; `TimeTravelEngine::BindLive` binds a fed session to the machine. Live recording with keys, Dizzy X, Green Beret's tape load: CPU, all RAM and every device equal
  - [x] Fixed (2026-10-03): on Green Beret's tape load the live tape answered differently from the recording during a replay (780-3,444 value mismatches, v1 and engine alike). Two bugs in the tape's restore: a restore into a deck whose blocks were not installed yet (installed lazily when the deck starts) left it empty; and a block whose edges had to be generated again (freed when the deck moved on, or freshly installed) restarted from its first pulse, losing the restored position. Now zero with the session's tape in the deck
  - [x] Debugger edits with their bytes (2026-10-03): `EditMemoryFromTool` records the RAM pages and device-memory pieces dirty since the last checkpoint and every device state the edit changed; the engine's replay applies them and crosses the edit (v1 still stops at its marker)
  - [x] Media read journal (owner decision 2026-10-03: record at the sector read): every block medium's stack ends in `MediaReadTap`; while recording each sector read from an image goes into the engine's `TTDMediaJournal` (time, slot, LBA, bytes; a cursor per checkpoint), a replay from the engine hands them back. Covers the CPU's IN, TS-Conf's SD / IDE DMA and the NeoGS card's own SD reads in one place (2026-10-03). The ATAPI drive's CD data reads (`CdImage::ReadUser` / `ReadFrame`, past the block stack) are journaled too; CD audio samples are the drive's output, not recorded
  - [x] Interrupt vectors recorded (`TTDEngine::BusVectors`, one record per acknowledge on machines with their own INT logic; played back by an engine replay) (2026-10-03)
  - [x] Port journals recorded on every machine (the engine's bus data); v1's gate now only decides whether v1's own replay plays them (`_portJournalRecorded` / `_portJournalValid`). A/B on Pentagon + NeoGS, TS-Conf, Sprinter, Scorpion, Profi, ZX-Evo: engine seeks land on v1's machine. Cost: v1's memory grows on the formerly gated machines (Pentagon + NeoGS idle 1,487 → 1,979 B per frame, TS-Conf 6,433 → 9,081), the engine's stays below v1's
- [x] Step 2 — Replay modes: input events, `IN` values (RZX) (2026-10-04): TTD records while an RZX plays (RZ-F19). The playback position is a device state in every checkpoint (`RzxPlayback`, id 47), so a seek back lands inside the playback and it plays on; each RZX frame end (with or without its interrupt) is an `InterruptFrame` fact and the playback's start / end a `ReplaySourceChange` in the engine; `TimeTravelEngine::RzxFrameTime(n)` maps RZX frame N to machine time. A seek there equals the RZX player's keyframe seek (CPU, all RAM, `#7FFD`, player position) on the 6 recordings in testdata (48K, 128K, +2, Pentagon), from v1's data and from the engine's. Before: a seek back inside an RZX playback desynced the player
  - [ ] `rzx/seek` through the engine and `RzxKeyframeStore` removed: with the switch-over (Phase 5)
  - [ ] A session replayable without the RZX file (its forced interrupts from the facts): with media in the session (Phase 4)
- [ ] Step 3 — Several CPUs: own cycle counters, clock-change events, positions on any CPU
  - [x] Machine time from the frame table (2026-10-03): in shadow mode a frame starts where the last one started plus its measured length (`emulatorState.t_states` x `ttd_clock_units`), not frame x the current length; a change of length is a `FrameLengthChange` fact. Sprinter 320 / 312 lines: machine time never goes back
  - [ ] CPU table, clock map (`ClockChange` facts), per-checkpoint CPU counter residuals — deferred (owner decision 2026-10-03) to the GS debugger, its user; the card CPUs' counters are already in every checkpoint (device state, time fields)
  - [ ] Positions on a card CPU (seek, reverse step in card instructions) — deferred with it
- [x] Step 4 — Configuration fingerprint and media versions (2026-10-03): named-field fingerprint (model, RAM, frame / INT timing, audio + render settings, board options, the ROM set of every model incl. the Sprinter BIOS) per session with `ConfigChange` cuts; a seek on other settings runs and reports `NotBitExact` naming them (`LastEngineCheck`); media slot table + per-checkpoint versions behind `IMediaHistory` (interim: ContentId + written-frame count, no going back: a changed medium is reported, never a barrier)
  - [ ] Media heads that go back: with the storage manager's change layer (H1 / H5)
  - [ ] Show the check on every surface with the switchover (Phase 5)
- [x] Step 5 — Emulated real-time clocks (2026-10-03): one session time base (host wall time + emulated time at the recording start, `PortDecoder::SessionWallMicros`) for every DS12887 user; emulated microseconds from the base T-states, each frame at its own length (Sprinter 320 / 312 lines never goes back)
- [x] Step 6 — No writes outside the session during replay (2026-10-03): write-through block media held in memory and released to their files when the replay ends (`HostWriteHold`, `MediaManager::HoldHostWrites`, engaged by v1's replay); floppy write-through waits for the next live frame; VDAC2 bus capture and the video recording's audio skip replayed frames
- [ ] Step 7 — The write journal on demand (D40, owner decision 2026-10-03 after E7; design: phase-3 TDD §4.8)
  - [x] E7 (2026-10-03): which operations use the journal, its size against the rest of a recording, search time without it ([write-journal-e7.md](write-journal-e7.md))
  - [x] J1 (2026-10-03) Core: segments; switch at any instruction (also mid-frame, from the emulation thread); find-last per segment, coverage index for writes outside; port find-last from the port journal; off by default; status; header flag + segment table; ttdfileinfo, Python analyzer, ttd.ksy; format docs
  - [x] J2 (2026-10-04) Core: `BuildWriteJournal(from, to)` by replay, progress and cancel; built records equal recorded ones
  - [x] J3 (2026-10-04) Automation: CLI, WebAPI + OpenAPI, MCP, Lua, Python (start option, `journal on|off|build|status`; `development`/`gaming` removed); command-interface.md, webapi/lua/python interface docs, MCP README, recipes
  - [x] J4 (2026-10-04) Qt: journal switch in the TTD panel, segments band on the scrubber, build for the selection; time-travel-ux.md
  - [x] J5 (2026-10-04) Python tool via WebAPI: load a .ttd, build a span, save; analyzer README
  - [x] J6 (2026-10-04, engine side; the file in Phase 4) Engine: segments in TimeTravelEngine (shadow-fed), segment table in the Phase 4 file
  - [x] Groundwork (2026-10-03): `SetWriteJournalCapacity` (sessions with their whole write history), `RegenerateFrameWrites` (a frame's writes by replay, equal to the journal's), TTDE7 benchmark; exactness 0 mismatching frames on 17 sessions x 200 frames
- [ ] **On landing (merge master into ttd-engine):** master fixed the toolbar's cross-thread session summary crash its own way (1de1b07bc: `GetPublishedSessionInfo`, StopRecording parks the machine). Keep master's mechanism; drop this branch's `GetLatestSessionInfo` / `PublishSessionInfo` / `_infoMutex` and `timetravelmanager_sessioninfo_test.cpp` (5f18b937b)

- [x] Phase 3 results against the quality bar (D33): [phase-3-results.md](phase-3-results.md), baseline `testdata/ttd/bench/engine-phase3-full.json` (2026-10-04)

## Phase 4 — The session file

Design: [phase-4-session-file-tdd.md](phase-4-session-file-tdd.md).

- [x] Step 1 — Integrity and versioning decision (owner, 2026-10-04): CRC32C per record, header and index; open with holes (only frames depending on a damaged record are unreachable); no compatibility promise before the release
- [ ] Step 2 — Written as it records: append-only, background writer, crash-safe
  - Decided 2026-10-04 (owner): nothing is written until a recording is asked for; history in memory is a ring of segments (default: the last 5 minutes) or, on request at start, a longer ring or a list that grows (seek over the whole session); every closed segment is written to a file, so the disk holds the whole session; loading a long session reads the last 5 minutes by default, or a range; each recording in its own folder `~/.unreal-ng/ttd/<date-time>-<name>/` (Windows `%USERPROFILE%\.unreal-ng\`), saved recordings as files in `~/.unreal-ng/ttd/`; an asynchronous `CleanupManager` at startup (steps from any subsystem, errors and exceptions caught per step, each step at least weekly) removes crashed recordings older than 7 days
  - [x] Groundwork (2026-10-04): `FileHelper::GetHomePath` / `GetUserDataPath` / `GetUserDataFolder` / `CreateFolders` / `DeleteFolder` / `FromFsPath` (cross-platform, UTF-8; a test override for the user folder); `platform::CurrentProcessId` / `IsProcessAlive` (POSIX, Windows); `CleanupManager` (`common/cleanupmanager.h`: steps from any subsystem, background run, errors and exceptions caught per step, last run per step in `<user data>/cleanup-state.txt`, weekly); TTD recording folders (`ttdrecordingfolders.h`: `owner.pid`, the "ttd-crashed-recordings" step); `StartStartupCleanup()` in unreal-qt and the headless automation host
  - [x] Container written synchronously (2026-10-04): `engine/ttdcontainer.*` — header with the stream table (required / ancillary) and the session's tables, records with CRC32C of header and payload (zstd when smaller), part-end records (frames, record offsets, dependencies), index and trailer; the reader checks header, index and part ends at open and payloads when read, opens a file without a trailer by scanning (resyncs past damaged bytes, drops an incomplete last part), turns damage into holes (`IsReachable` through the dependencies), refuses an unknown required stream by name; `platform::AppendFile` / `RandomAccessFile` (POSIX, Windows). Tests: round trip, scan, a cut at every byte of the tail, payload / part-end / record-header / header damage, unknown streams, byte-identical rewrites, a real file at a non-ASCII path; two mutants caught
  - [x] The engine session into the container (2026-10-04): `engine/ttdsessionfile.*` — `TTDSessionFile::Save` / `Load`; versions as stored (no re-encoding), numbered in the file; reference tables rebuilt by `TimeTravelEngine::ImportCheckpoint` (the commit path shared with `CaptureFrame`); events, configuration, media versions, bus and sector journals per part; write journal in v1's column blocks; a part depends on the parts of its current versions and its bases; load stops before the first unreachable part; a loaded session is read-only. Every corpus session survives save and load checkpoint for checkpoint, a re-save gives the same bytes; files 3.5-9x smaller than v1's (sizes in the phase-4 TDD)
  - [x] Writer thread (2026-10-04): `TTDSessionWriter` — the engine's thread lays out every complete part (`Collect`), the writer thread compresses, checks, appends and syncs it; capture never waits (a stalled disk: lag reported, past 512 MB the writer stops with the reason); a write error stops it with the reason, the file valid to its last part; writing as it records gives the same bytes as `Save` at the end. `TTDRecordingFolder`: `<date-time>-<model>` (`-2` on a collision), owner file, segment files, save as (one segment: a copy; joining several comes with Step 3), discard. Crash test: a forked child aborts mid-recording, the parent loads every complete part exactly
  - [x] Wired into the shadow recording (2026-10-04): `TimeTravelManager::SetShadowRecordingRoot` (off by default; tests and the benchmark) — a recording folder per session, `TTDRecordingWriter::Collect` after each shadow capture, `Finish` at stop, the folder deleted when the session is invalidated, kept when a new one starts (`TimeTravelManager_ShadowFile_Test`). The user path (the default folder `~/.unreal-ng/ttd/`) comes with Phase 5
- [ ] Step 3 — Segments: a baseline per segment, the ring (default 5 minutes) and the growable list, loading a range of a long session, accounting (replaces "memory as a cache" with budget, eviction and rebasing, owner decision 2026-10-04)
  - [x] Segments in the engine (2026-10-04): `TTDHistoryPolicy` (Ring, default 5 minutes; Growable; a segment per minute); a baseline checkpoint stores every known piece whole and starts fresh reference tables, so nothing crosses a segment; the ring drops the oldest segment whole (versions, tables, frames, events and payloads, the RZX index, bus journal blocks) once the next one covers the window; checkpoint indices count from the session start (`FirstCheckpoint()`); the baseline flag is in the file; events are split into parts by time (indices shift when the ring drops). A file written while a ring records holds the whole session. Not dropped yet: the sector-read journal and the write journal (both rare or off by default)
  - [x] A file per segment in the recording's folder (2026-10-04): `TTDRecordingWriter` (`engine/ttdrecordingwriter.*`) opens the next segment's file at its baseline; journal positions in a file count from its first record and a file's first part is marked, so files load one after another (`TTDSessionFile::Load` with append) and join; `LoadRecording` reads the last window (whole files from the end) or everything; `JoinSessionFiles` copies the records as stored into one .ttd (`TTDRecordingFolder::SaveAs`). Found on the way: a file that did not start at the session's start loaded its bus journals with absolute positions (memory restored right, a replay would have read the wrong records); fixed by the relative positions, tested on every checkpoint
- [x] Step 4 — Optional frame-boundary streams in the file (screenshot first) (2026-10-04): a registered stream's capture hands its copy to the engine (`AddFrameStreamCopy`); the writer encodes the copies of each part (XOR with the previous copy, zstd, a full copy every 50 frames) into stream 0x0100 + id (ancillary) and the copies leave memory (written through; `frameStreams` in the heap report); `TTDSessionFile::ReadFrameStream` reads a frame's copy back, "not recorded" where the stream was off. Screenshot: the shadow session registers stream 0 (width, height, video mode, framebuffer). Size per matrix case (`bm3_stream_screenshot_bpf`) and the surfaces come with Phase 5
- [x] Step 5 — v1 files read into the engine's format (2026-10-04): `bench::ConvertV1Session` feeds a v1 session into the engine (growable, one segment) and writes it with the header flag `kSessionConvertedFromV1` (what v1 lacks stays absent); the load report names it. Verification tools only (D32). Every corpus session converts and loads as fed (`TTDSessionFile_Test.EveryV1SessionConverts`)
- [x] Step 6 — `ttd.ksy` and the Python analyzer (2026-10-04): `engine/ttdsession.ksy` describes schema 2 (container, tables, records, part ends, index, trailer, every stream's layout after decompression); it becomes `ttd.ksy` at the switch-over (schema 1 stays `ttd.ksy` while users have it). `ttd-analyzer/src/ttdcontainer.py` reads schema 2 and validates it beyond the C++ reader (every version decoded against its CRC, dependency lists); `info` / `validate` dispatch on the schema, new `parts` and `recover`. Conformance: fixtures written by the C++ writer in `testdata/ttd/v2/` (finished, unfinished, an unknown ancillary stream, a converted v1 session) with the C++ reader's counts in `expected.json`; the Python tests find the same, `CommittedFixturesStillLoad` keeps them current. Not done: a Kaitai-generated parser run (no compiler on the build host) and the fuzz test (QR-4)

## Phase 5 — Switch the emulator to the engine

Design: [phase-5-switchover-tdd.md](phase-5-switchover-tdd.md).

- [ ] Step 1 — The emulator and every surface on the engine; clean stop on TTD / debug mode off
- [ ] Step 2 — History never cut short: branches on resume and edit in the past, seek while recording, loads as events
- [ ] Step 3 — Black-box setting in unreal-qt (off for automation); session file location in the UI, default `scratch/ttd/`
- [ ] Step 4 — v1 only in the verification tools

## Phase 6 — Cleanup

- [ ] Delete `TimeTravelManager` (keep the v1 file reader for verification), retire the proof-of-concept readers, TDD truth pass, move this folder to DONE

## Later

- [ ] Branch operations in the UI: PLAN #76 ([design](../2026-09-29-model-what-if/design.md))
- [ ] Groups of machines (ZX-Poly) over a shared piece store (D22)
