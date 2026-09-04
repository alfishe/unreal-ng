# Tape ↔ Audio Bridge: Render WAV/FLAC From Tape Images, Import Audio Into TAP/TZX

| | |
|---|---|
| **Status** | r0 — draft for review |
| **Date** | 2026-09-04 |
| **Feature** | Offline tape→audio render (whole tape or any block range → WAV/FLAC) and audio→tape import (WAV/FLAC/MP3 → TZX, or TAP when content is ROM-standard) |
| **Affects** | `core/src/emulator/io/tape/`, `core/src/loaders/tape/`, `core/src/tapeaudio/` (new), `core/recording/src/`, `core/automation/cli/`, `core/automation/webapi/`, `unreal-qt/src/tape/` |
| **Related** | [2026-09-01-tape-manager design](../2026-09-01-tape-manager/design.md) (§5.4 format landscape — WAV was "rejected, out of scope" as an *insertable* format; this plan does not reopen that, it converts), §5.7 block representations, §7 surface conventions; [tzx-loader-design.md](../2026-09-01-tape-manager/tzx-loader-design.md) (byte-level TZX layouts) |

## Revision History

| Rev | Date | Notes |
|-----|------|-------|
| r0 | 2026-09-04 | Initial design: offline render pipeline over `edgePulseTimings`, encoder reuse via `EncoderBase` (tinywav native WAV / FFmpeg FLAC), import pipeline (decode → Schmitt-trigger pulse extraction → segmentation → recognition → TZX/TAP writers, `TzxTapeBuilder` promoted to production) |

## 1. Goals and Motivation

### 1.1 User-facing goals

1. **Play a tape image on a real Spectrum.** Render the whole tape — or any individual block / block range — to a WAV or FLAC file, write it to a phone/PC, feed it into a real tape deck's AUX input, and LOAD "" on hardware. The rendered audio must be loadable by the physical machine: correct pilot/sync/data pulse widths, including turbo and custom-loader timings, because the render path uses the engine's own pulse representation rather than a re-implementation.
2. **Archive and inspect.** A block-level export makes it possible to listen to what a custom loader actually sounds like, share a suspect block for diagnosis, and keep audio-era artifacts alongside `.tzx` preservation copies.
3. **Digitize real tapes.** The reverse direction: load a WAV/FLAC/MP3 capture of a real cassette, extract the pulse train, and save it back as a `.tzx` (always possible — exact pulse preservation) or `.tap` (only when the content decodes as ROM-standard blocks). Non-standard speeds survive only in TZX — TAP carries no timing information, so turbo/custom loaders can never round-trip through TAP; the UI states this rather than silently degrading.
4. **Every surface.** CLI (`tape render` / `tape import`), WebAPI, and the Tape Manager window all expose both directions, following the tape-manager §7 conventions.

### 1.2 Reuse mandates (why this is cheap)

The feature is almost entirely wiring of existing pieces:

- **Canonical pulse representation already exists.** Every playable block — TAP bytes, TZX `$10`/`$11`/`$14` byte payloads, `$12`/`$13`/`$15`/`$18` pulse streams — is materialized by the tape engine into `edgePulseTimings`: a `std::vector<uint32_t>` of half-period durations in T-states, level alternating per entry (tape-manager §5.7, representations 1–3). Rendering audio is a linear walk over that vector; nothing about encoding, timing or speed profiles needs to be re-derived.
- **Encoders already exist.** The recording module's `EncoderBase` contract (`Start` / `OnAudioSamples` / `Stop`, `encoder_base.h`) explicitly anticipates audio-only encoders; `FFmpegPipeEncoder` already resolves `flac`, `wav`/`pcm_s16le` audio codecs over its pipe transport, and `core/src/3rdparty/tinywav` provides a dependency-free WAV writer *and* reader.
- **TZX byte layout already exists — in tests.** `TzxTapeBuilder` (`core/tests/_helpers/tzxtapebuilder.h`) encodes every relevant block type at byte level and is load-tested by the TZX loader suite. Promoting it to production gives the import side its writer with golden coverage already in place.
- **Subprocess + probe infrastructure already exists.** `Subprocess` / `FFmpegProbe` (recording module) locate and drive the external `ffmpeg` binary — the same mechanism serves FLAC/MP3 *decode* for the import direction.

### 1.3 Honest scope statement

Render is an **offline, faster-than-realtime** conversion — not a realtime "tape dub" through the emulator's audio output device, and not a way to insert a WAV as a playable tape. Import produces a **`.tzx`/`.tap` file** that is then inserted through the normal tape path; WAV does not become an insertable format (tape-manager §3.5 keeps that verdict).

## 2. Non-Goals

1. **WAV/CSW/PZX as insertable tape formats.** Stays with the tape-manager decision. (CSW *loading* remains the tape-manager plan's own P3; this plan only *emits* TZX and TAP.)
2. **Realtime rendering** through the sound device while the emulator runs.
3. **MP3 encoding.** MP3 is decode-only on the import side; lossy compression is never an acceptable output for signal preservation.
4. **ZX80/ZX81, CDT/Amstrad tapes.** ZX Spectrum encodings only; the TZX structures used are Spectrum-appropriate.
5. **TZX writer coverage of the full control-flow block set.** The writer emits the linear playable set (`$10`–`$15`, `$20`, `$30`, `$35`, `$5A`); jump/loop/call/select are import outputs only if trivially linearizable — v1 emits linearized pulses instead (see §6.4).

## 3. In-Tree Assets and Prior Art

### 3.1 Inventory

| Asset | Location | Role in this design |
|---|---|---|
| `edgePulseTimings` (u32 half-periods, T-states) | `tapetypes.h` `TapeBlock` | Single source of truth for render; single target for import recognition |
| `generateBitstream` / block preparation | `tape.cpp` (`prepareTapeBlock` path) | Produces `edgePulseTimings` for byte-payload blocks — must be factored out of the live engine into a shared pure function (§4.3) |
| ROM timing constants (`PILOT_TONE_HALF_PERIOD` etc.) | `tape.h` | Recognition reference values on import (±tolerance) |
| `TzxTapeBuilder` | `core/tests/_helpers/tzxtapebuilder.h` | Promoted to `core/src/loaders/tape/writer_tzx.h/.cpp` — production TZX writer; the test helper becomes a thin alias so existing tests are untouched |
| `tinywav` writer + reader | `core/src/3rdparty/tinywav/` | Native WAV output (zero deps) and WAV input (`tinywav_read_f`) |
| `FFmpegPipeEncoder`, `Subprocess`, `FFmpegProbe` | `core/recording/src/` | FLAC render; FLAC/MP3 decode pipes; ffmpeg availability probing |
| `EncoderBase` | `core/recording/src/encoder_base.h` | The encoder contract both render paths feed |
| `TapeLoaderRegistry` + `TapeImage` contract | `core/src/loaders/tape/` | Render input: content-probed image load, headless, no emulator instance |
| `TapeBlockDescriptor` catalog | `tapecatalog.h` | Block selection semantics (indices identical to `GetBlockCatalog()`) |
| zlib | vendored under `core/automation/webapi/zlib` only | **Not** linkable from core (dependency direction) — rules out TZX `$18`/CSW-v2 emission in v1 (§6.4, R4) |

### 3.2 Prior art (approach validation)

- **`audio2tape` / `tape2wav`** (Fuse utilities): same two directions; edge extraction via level-triggered slicing. Confirms the Schmitt-trigger + segmentation + recognition pipeline shape and the "TAP only for ROM-standard" rule.
- **WAV2TZX / MakeTZX** (RAMSOFT lineage): pilot/sync recognition with per-block timing measurement; the measured-timings → `$11` mapping used here follows theirs.
- Real-hardware playback convention: bipolar square wave, ~±0.8 FS, mono, 44100 Hz is the de-facto community standard (both accepted here; §5.2 quantization analysis).

## 4. Core Representation Decisions

### 4.1 The pulse timeline is the interchange format

Both directions meet at `std::vector<uint32_t>` half-periods in T-states:

```
render:  TapeImage.blocks → (per-block pulse materialization) → PulseTimeline → PCM → EncoderBase → WAV/FLAC
import:  WAV/FLAC/MP3 → PCM → Schmitt trigger → half-periods (samples) → T-states → segment → recognize → TapeImage → writer
```

The T-state clock is the Z80 nominal 3.5 MHz for every supported model (all Spectrum targets run the CPU at 3.5 MHz — no per-model branching needed; kept as a single named constant `TAPE_AUDIO_TSTATE_HZ = 3'500'000` in the new module so a future TS-Conf-style exception is one edit away).

Sample↔T-state conversion is exact-rational per pulse, never cumulative float drift (§5.2 / §6.2): durations are computed as `round(samples * TAPE_AUDIO_TSTATE_HZ / sampleRate)` on import and `round(tStates * sampleRate / TAPE_AUDIO_TSTATE_HZ)` on render, with a fixed-point remainder carried between edges so long trains keep sub-sample accuracy.

### 4.2 Level conventions

- `edgePulseTimings` entries alternate level starting **low** (the engine's own convention — `_lastTapeBit` starts false); `TapeTimingProfile.invertedLevel` (TZX `$2B`) flips the initial level only, never the durations.
- PCM mapping: low = `−A`, high = `+A` (bipolar, DC-balanced by construction — every half-period has a partner). Amplitude `A` defaults to 0.8 FS, configurable.
- **Pauses render as digital silence (0)**, not a held level: a second of held `−A` would DC-bias downstream gear, and real cassette gaps are silence anyway. Loaders only measure edge-to-edge timing; the silence→first-edge transition is the pilot onset, which is exactly what a deck sees.

### 4.3 Shared pulse materialization (the one engine refactor)

`Tape::prepareTapeBlock()` (tape.cpp, `generateBitstream` dispatch over representations 1/2/3) currently lives inside the live engine and mutates the block in place. The renderer needs the same output without an emulator instance. Refactor, zero behavior change:

- Extract the pure part into `core/src/emulator/io/tape/tapepulsegen.h/.cpp`: `bool MaterializePulses(const TapeBlock& block, std::vector<uint32_t>& outHalfPeriods, uint32_t& outPauseMs)` — same dispatch: representation 3 / empty payload returns the loader-supplied train; representation 2 generates from the timing profile; representation 1 from ROM constants.
- `Tape::prepareTapeBlock()` becomes a thin wrapper that assigns the result to the block (byte-identical `edgePulseTimings` output — the existing engine tests are the regression gate).
- Pause handling: the pause is returned separately (`outPauseMs`) rather than appended as a giant pulse, so the renderer can emit silence and the importer can treat gaps structurally. The engine keeps appending it to the train exactly as today (its consumption cursor semantics rely on the current shape).

### 4.4 Module placement and dependency direction

```
core/src/tapeaudio/                  (new — bridge orchestrators, no emulator dependency)
    tapeaudiorenderer.h/.cpp         image/range → PCM → EncoderBase
    tapeaudioimporter.h/.cpp         PCM → pulses → segments → TapeImage
    tapepulseextractor.h/.cpp        Schmitt trigger + segmentation (pure DSP)
    tapeaudioconfig.h                shared constants (T-state clock, defaults, thresholds)

core/src/emulator/io/tape/tapepulsegen.*   (pure pulse materialization, §4.3)
core/src/loaders/tape/writer_tzx.*         (TZX serialization — promoted builder)
core/src/loaders/tape/writer_tap.*         (TAP serialization — trivial)
core/recording/src/…                       (untouched; consumed via EncoderBase/Subprocess/FFmpegProbe)
```

Dependency arrows run one way: `tapeaudio → {loaders, io/tape, recording}`, never the reverse. The bridge never touches `EmulatorContext` — render input is a `TapeImage` from `TapeLoaderRegistry` (path in, image out, headless by construction), which also makes it trivially unit-testable.

## 5. Design: Render (tape image → WAV/FLAC)

### 5.1 Public API

```cpp
// core/src/tapeaudio/tapeaudiorenderer.h
struct TapeRenderRequest
{
    std::string sourcePath;            // .tap/.tzx (content-probed, extension-agnostic per registry)
    // Block selection: indices are TapeBlockDescriptor catalog indices (identical
    // to GetBlockCatalog() / the Tape Manager table). Empty = whole tape.
    size_t firstBlock = 0;
    size_t lastBlock  = SIZE_MAX;      // inclusive
    std::string outputPath;            // extension picks the encoder: .wav native, .flac via ffmpeg
    uint32_t sampleRate = 44100;       // 44100 default (community standard); 48000/96000 accepted
    double   amplitude  = 0.8;         // FS fraction
    bool     invertLevel = false;      // polarity override (R2)
    std::function<void(size_t blocksDone, size_t blocksTotal)> onProgress;  // worker thread
    std::function<bool()> cancelRequested;                                 // polled per block
};

struct TapeRenderResult
{
    bool ok = false;
    std::string errorText;             // probe/decode/encoder failures, ffmpeg-missing guidance
    std::vector<std::string> warnings; // e.g. "block 7: no signal (control-only), skipped"
    double durationSec = 0.0;          // audio duration
    uint64_t samplesWritten = 0;
    size_t  blocksRendered = 0;
    std::string encoderUsed;           // "tinywav" | "ffmpeg(flac)"
};

TapeRenderResult RenderTapeToAudio(const TapeRenderRequest& request);
```

### 5.2 Synthesis: exact edges under a finite sample rate

Per block: materialize pulses (§4.3), then emit samples while walking a fixed-point T-state cursor:

- Maintain `tNextEdge` (T-states, u64) and a 32.32 fixed-point position; each output sample advances `sampleRate/TAPE_AUDIO_TSTATE_HZ` T-states; when the cursor crosses `tNextEdge`, flip the level and advance to the next half-period.
- Quantization worst case (smallest useful pulse = sync1 667 T ≈ 190 µs): 8.4 samples @ 44.1 kHz → edge jitter ≤ ±0.5 sample ≈ ±6 % of the half-period — comfortably inside the ROM loader's ±15 % tolerance (tape.h documents the tolerance rationale); 48 kHz gives ±5.5 %. This is why no resampling or interpolation stage is needed: durations are exact in the T-state domain, and per-edge rounding error never accumulates (remainder carried).
- Trailing state: each block ends with `pauseMs` silence; the level resets to the *next* block's initial level (independent blocks — not a continuous level stream), matching how `TZX $2B` behaves between blocks.

### 5.3 Encoder plumbing (EncoderBase reuse, RecordingManager not used)

- **WAV**: a 30-line `EncoderBase` adapter over the tinywav writer (mono `int16`, `TW_INTERLEAVED`). Zero external dependencies — always available, the default and the fallback.
- **FLAC**: `FFmpegPipeEncoder` driven directly with `EncoderConfig` audio-only (no video pipe thread; `resolveAudioEncoder("flac")` path already exists). ffmpeg availability is probed via `FFmpegProbe` first; a missing binary produces an `errorText` that explicitly offers WAV as the dependency-free alternative.
- **Why not `RecordingManager`**: it is context-bound (emulator instance, audio tracks, stats) and its CaptureAudio path assumes the live sound pipeline. The bridge pushes PCM chunks (100 ms) straight into the encoder — the realtime backpressure machinery is bypassed (`_blocking` mode), so an offline render can outrun realtime without dropped chunks.
- Both encoders finalize via `Stop()`; partial output on cancel is deleted (no orphan half-written files — the tempfile-then-rename pattern the recording module already uses).

### 5.4 Block selection semantics

- Selection is expressed in catalog indices (same as `GetBlockCatalog()` — the Tape Manager table, WebAPI `blocks[]`, CLI `tape blocks`).
- Included kinds: Header/Data/Custom/Tone/PulseStream render their signal; Control entries contribute their `pauseMs` as silence (a `$20` between two data blocks keeps the gap audible — a deck needs it for motor control); metadata-only images render to silence + a warning.
- Single-block export = `firstBlock == lastBlock`; the file still contains that block's trailing pause, so what you hear matches what the machine would hear at that position.

### 5.5 Threading

One worker `std::thread` per render job (`std::async` is enough; jobs are short — a 5-minute tape renders in ~1–2 s). Progress/cancel callbacks fire per block. No shared state with the emulator; no locks beyond the encoder's own.

## 6. Design: Import (WAV/FLAC/MP3 → TZX / TAP)

### 6.1 Public API

```cpp
// core/src/tapeaudio/tapeaudioimporter.h
struct TapeImportRequest
{
    std::string sourcePath;             // .wav/.flac/.mp3 (extension routes the decoder)
    std::string outputPath;             // .tzx or .tap
    // "prefer" only orders the decision; the content decides (§6.5). tap is
    // refused with a structured reason when any segment is non-ROM-standard.
    enum class Target { Auto, Tzx, Tap } target = Target::Auto;
    double hysteresis = 0.2;            // trigger threshold as fraction of peak amplitude
    uint32_t minHalfPeriodTstates = 50; // glitch/runt filter (≈14 µs — below any real pulse)
    uint32_t pauseThresholdMs = 5;      // gaps ≥ this split segments (tape-manager §5.4 merge bound)
    bool     invertLevel = false;       // polarity override; default auto (§6.4)
    std::function<void(double secondsDone, double secondsTotal)> onProgress;
    std::function<bool()> cancelRequested;
};

struct TapeImportResult
{
    bool ok = false;
    std::string errorText;              // decode failure, ffmpeg missing, target refused
    std::string outputPath;             // as written
    size_t segmentsTotal = 0;           // pulse-train segments after splitting
    size_t blocksRecognized = 0;        // decoded to byte payloads ($10/$11/$14)
    size_t blocksPulseOnly = 0;         // preserved as $13 pulse sequences
    std::string tapRefusalReason;       // filled when target=Tap was refused
    std::vector<std::string> warnings;  // per-segment anomalies (runts ignored, DC offset, …)
    std::vector<TapeBlockDescriptor> catalog;  // preview of what was written (same shape as any loader)
};

TapeImportResult ImportAudioToTape(const TapeImportRequest& request);
```

The importer always writes a file (that is its point), and returns the produced image's catalog so surfaces can show exactly what was recognized — same descriptor shape every other loader reports.

### 6.2 Decode front-end

| Source | Decoder | Notes |
|---|---|---|
| WAV | native, via tinywav reader (`tinywav_read_f`) | Accept s16/float, mono/stereo, any sample rate — **no resampling**: edges are converted sample→T-state rationally, so 22.05k…192k all work natively. Stereo is downmixed (average) before triggering. |
| FLAC / MP3 | external ffmpeg subprocess: `ffmpeg -i <in> -f s16le -acodec pcm_s16le -ac 1 -` → stdout PCM stream | Reuses `Subprocess`; native rate is kept (`-ar` never passed — resampling would re-jitter edges). ffmpeg probed via `FFmpegProbe`; missing binary → clear error suggesting WAV input. |

DC offset is removed (running mean) before the trigger; amplitude is irrelevant beyond hysteresis scaling (peak-normalized internally).

### 6.3 Pulse extraction (`tapepulseextractor`)

Schmitt trigger with hysteresis, then half-period measurement:

1. Compute `hi = +h·peak`, `lo = −h·peak` (`h` default 0.2). State machine: signal is HIGH after crossing `hi` upward, LOW after crossing `lo` downward — crossings of the intermediate band are ignored (this is what rejects tape hiss, DC wobble and MP3 pre-echo ripple).
2. Every HIGH→LOW or LOW→HIGH transition emits a half-period duration in samples; converted to T-states rationally (`round(samples * 3.5e6 / sampleRate)`).
3. Runt filter: half-periods below `minHalfPeriodTstates` are merged into their neighbor (a single glitch must not split a pulse).
4. Segmentation: a half-period ≥ `pauseThresholdMs` (5 ms default — the same bound the tape-manager design uses for pause merging, now in the opposite role) ends the segment; the gap becomes the following block's `pauseMs` (millisecond resolution, matching TZX `$20`).

Output: `std::vector<TapePulseSegment> { std::vector<uint32_t> halfPeriodsTstates; uint32_t pauseAfterMs; }` — this is deliberately the same shape as representation-3 blocks, so unrecognized segments become playable blocks with no further transformation.

### 6.4 Recognition and TZX mapping

Per segment, a three-stage classifier runs. All thresholds are relative to **measured** medians, never absolute, except where a ROM constant is named:

| Stage | What it does | Output |
|---|---|---|
| **A. Pilot/sync** | Count the leading run of uniform half-periods (±10 %): candidate pilot `P`, count `N`. Then look for the ROM sync signature (short-then-long: 667 T then 735 T ±10 %) or any sharp discontinuity pair. | pilot parameters or "no pilot" |
| **B. Bit decode** | After sync, group pulse pairs; classify each bit by its first half-period against the bimodal distribution (cluster split by median); derive measured `zeroHalf`/`oneHalf` as per-bit medians. Assemble bits MSB-first into bytes; trailing partial byte keeps its bit count. | raw bit/byte stream + measured timings |
| **C. Framing** | XOR-checksum test (flag…payload…parity). Valid → byte payload confirmed; flag `$00` + 19 bytes additionally qualifies as a ROM header (reuse `TapeCatalogParser::DerivePairing` later — the catalog is built by the normal path once blocks exist). | payload block or fall through |

Mapping to TZX (first match wins):

| Recognition outcome | TZX block | Why |
|---|---|---|
| Pilot/sync/bit-decode OK, timings within ±10 % of ROM constants (855/1710, pilot 2168, syncs 667/735) | **`$10` standard speed** (pause measured from the following gap) | Byte-identical to what a `.tap`-born block serializes to; also the only outcome that may become a TAP block (§6.5) |
| Decode OK, timings non-ROM (turbo) | **`$11` turbo** — pilot half/pulses, sync1/2, zero/one, bitsInLastByte all measured | The RAMSOFT/WAV2TZX mapping; preserves the loader's speed exactly |
| Bytes decode but no pilot (custom loader mid-stream, `$14`-style) | **`$14` pure data** (zero/one measured, bitsInLastByte) | No invented pilot — nothing is fabricated |
| No reliable bit decode | **`$13` pulse sequence**, chunked at ≤255 pulses (the u8 count limit), u16 half-periods (≤65535 T ≈ 18.7 ms — pauses already split out, so pulses fit) | **Exact preservation, zero information loss** — this is why "any audio → TZX" always succeeds |
| Inter-segment gap | `$20` pause (`pauseAfterMs`; gaps > 65.5 s clamp with a warning — R6) | motor-control fidelity |

**Polarity**: stages A–C measure durations and distributions, both polarity-agnostic; the only polarity-sensitive artifact is the implied initial level, which TZX records via `$2B` when the renderer's low-start convention was inverted. Auto-mode tries both and keeps the decode with more recognized blocks; `invertLevel` forces one.

**Why not `$18`/CSW or `$15` for the fallback**: `$18` requires CSW v2 (zlib) — zlib is vendored only under `core/automation/webapi/`, and core must not depend on an automation module's 3rdparty tree (R4); CSW v1's u8 RLE durations cannot hold pilots at arbitrary sample rates. `$15` direct-recording quantizes to a fixed T-states-per-sample grid — lossy. `$13` is exact and dependency-free; its verbosity (2 bytes/pulse) is the accepted cost.

### 6.5 TAP export gate

`.tap` is offered (CLI/WebAPI/UI) only when **every** segment classified as `$10` in §6.4. The block bytes (flag + payload + parity) serialize as `[u16 len][bytes]` — `writer_tap` is ~40 lines reusing `LoaderTAP`'s checksum helpers in reverse. If any segment is turbo/custom/pulse-only, the request is **refused with `tapRefusalReason`** ("block 4 loads at non-standard speed; TAP carries no timing — export as TZX") instead of degrading: silently re-encoding a turbo block at ROM speed would produce a file that loads wrong on real hardware. `Target::Auto` writes TZX whenever the gate fails, and says so in `warnings`.

### 6.6 Writers (promotion, not rewrite)

- `writer_tzx` — `TzxTapeBuilder` moves from `core/tests/_helpers/` to `core/src/loaders/tape/writer_tzx.h/.cpp` and gains `AddTapeBlock(const TapeBlock&)` (dispatch: representation/timing profile → `$10`/`$11`/`$14`/`$13`/`$20`) and `SerializeImage(const TapeImage&, std::ostream&)`. The test helper header keeps existing by including the production one (tests unchanged; golden coverage transfers for free).
- Provenance: imports append a `$35` custom-info block (`"unreal-import "`, JSON-ish text with source path, sample rate, thresholds, date) — self-describing files, and it is exactly the block the TZX loader already skips-and-logs.
- `writer_tap` — trivial serializer as above; refuses blocks lacking byte payloads (should never happen behind the §6.5 gate — assert-style internal error).

### 6.7 Fidelity contract and round-trip

The keystone invariant, testable end-to-end without hardware:

> **Render→import of any TAP/TZX round-trips the pulse train within ±1 sample of edge jitter, and recognized blocks round-trip byte-identically.**

Concretely: `foo.tzx` → render WAV 44.1k → import TZX → (a) `$13`-born segments' half-period lists compare within tolerance; (b) `$10`/`$11`-born blocks' byte payloads are identical and measured timings within ±10 % of the original profile; (c) the imported image, inserted into the emulator, trap-loads the same way the original did (existing `tapeloading_integration_test.cpp` machinery, second pass over the imported file). This is the same verification shape the tape-manager plan used for the loader rewrite.

## 7. Control Surfaces

All three surfaces follow the tape-manager §7 conventions (same command family, same endpoint family, same lowercase CLI register, code-built UI per project rules).

### 7.1 CLI (`cli-processor-tape.cpp`)

```
tape render <image.tap|image.tzx> [--blocks first[-last]] [--rate 44100] [--amp 0.8]
            [--invert] -o out.wav|out.flac
tape import <audio.wav|audio.flac|audio.mp3> [--target auto|tzx|tap]
            [--hysteresis 0.2] [--invert] -o out.tzx|out.tap
```

- `tape render` without `--blocks` renders the whole tape; `--blocks 3` renders exactly block 3; `--blocks 2-4` the inclusive range.
- `tape import` prints the recognition summary (per-segment table: block type chosen, measured timings, warning count) — the same information density as `tape blocks`, since that output already exists as the preview vocabulary.
- Exit codes and stderr style match the existing CLI error conventions.

### 7.2 WebAPI (`tape_disk_api.cpp` + `openapi_tape_disk.inc`)

| Endpoint | Body | Response |
|---|---|---|
| `POST /api/v1/emulator/{id}/tape/render` | `{ "sourcePath": "…"` (optional — see next row)`, "blocks": "all" \| [0,3], "format": "wav"\|"flac", "sampleRate": 44100, "amplitude": 0.8, "invertLevel": false, "outputPath": "…" }` | render result (blocks rendered, duration, encoder used, warnings) |
| … inserted-tape mode | body without `sourcePath` | renders the instance's **inserted** tape (same catalog indices as `GET /tape`) |
| `POST /api/v1/emulator/{id}/tape/import` | `{ "sourcePath": "…", "outputPath": "…", "target": "auto"\|"tzx"\|"tap", "insert": false }` | import result (segments, recognized/pulse-only counts, `tapRefusalReason`, catalog preview) |

- Render-from-path works on any instance (the operation never touches emulator state — the instance-scoped path is kept for surface conventions and future per-instance job tracking).
- `insert: true` swaps `coreState.tapeFilePath` to the new image through the existing insert path (same sequence CLI `tape insert` uses) — off by default (Q2).
- Jobs are synchronous in v1 (renders take ~1–2 s); a `202 + job id` async form is the documented follow-up if real-world tapes prove slower (Q3).

### 7.3 UI (Tape Manager + File menu)

- **Tape Manager** gains "Export to audio…" (toolbar button + context menu on the block table): pre-filled with the current selection (no selection → whole tape), dialog with format (WAV/FLAC), sample rate, amplitude, invert; progress bar + cancel while the worker runs; FLAC disabled with an explanatory tooltip when ffmpeg is absent.
- **File → Import audio → tape image…** (`unreal-qt/src/tape/`, code-built per AUTOUIC-OFF convention): pick audio file, preview recognition (runs the importer to a temp image and shows the resulting block table — the same table model the Tape Manager already renders), then Save As `.tzx`/`.tap` (`.tap` disabled with reason when the gate refuses) and an "Insert now" checkbox.
- Both dialogs run the bridge on a worker thread and marshal results back via queued signals — the non-blocking-by-construction pattern from tape-manager §9.3.

## 8. Testing

### 8.1 Unit tests (`core/tests/tapeaudio/`, fixtures in `testdata/loaders/` + scratch outputs)

| Area | Cases |
|---|---|
| Synthesis (`tapeaudiorenderer`) | exact sample count for a known pulse train; edge sample indices land within ±1 of the rational prediction at 44.1k/48k; DC balance over a block; pause renders as zero samples; `invertLevel` flips only polarity; single-block range == whole-file prefix property |
| Encoder adapters | tinywav WAV header fields (mono/s16/rate); FLAC path fails gracefully when `FFmpegProbe` finds no binary (deterministic: probe result injected) |
| Extraction (`tapepulseextractor`) | synthetic square waves: clean, +DC offset, +white noise at 3× hysteresis, both polarities, 22.05k/44.1k/48k/96k rates; runt merging; gap splitting at exactly the threshold |
| Recognition | synthetic ROM block (generated from the engine's own constants) recognized as `$10`; turbo timings recognized as `$11` with measured values within ±10 % of ground truth; random noise → `$13` fallback; MSB-first bit assembly incl. partial trailing byte |
| Writers | `writer_tzx` output byte-identical to the promoted builder's existing golden tests; `writer_tap` framing; `$35` provenance block round-trips through `LoaderTZX` (skipped+logged, image intact) |

### 8.2 Round-trip and integration

- **Round-trip (§6.7)** over the existing fixture set: `parallax-demo.tzx` (real `$10` content), the synthetic turbo `$11` builder fixtures, a `$12`/`$13`-heavy synthetic image, and `DIZZY_X….tap` (headerless custom payloads — must come back pulse-exact, **not** misrecognized).
- **Load-back**: imported images inserted into a headless emulator instance trap-load identically to their sources (`tapeloading_integration_test.cpp` pattern, parameterized over source/imported pairs).
- **Real-world audio fixture**: one genuinely captured cassette WAV under `testdata/loaders/` (to be sourced; until then the round-trip fixtures stand in).
- Scratch outputs via `TestPathHelper::GetTestScratchPath()` per AGENTS.md; no artifacts in repo root.

## 9. Performance and Limits

| Operation | Cost | Bound |
|---|---|---|
| Render WAV | O(total T-states) synthesis; ~100× realtime for plain C++ loops | PCM streamed to the encoder in 100 ms chunks — never the whole file in RAM; memory O(one block) |
| Render FLAC | ffmpeg encode dominates; still ≫ realtime | pipe backpressure paces it |
| Import WAV | single pass trigger + segmentation | O(samples) |
| Import FLAC/MP3 | ffmpeg decode + same pass | decode-bound |
| TZX `$13` fallback size | 2 bytes/pulse + 2 bytes/chunk header | a 30-s noise segment ≈ 250 KB — acceptable; R4 tracks the CSW alternative |

## 10. Risks and Open Questions

| # | Risk / question | Mitigation / default |
|---|---|---|
| R1 | ffmpeg absent on user boxes (FLAC render, FLAC/MP3 import) | probed up front; every failure names the limitation and the WAV/native alternative; WAV↔TZX is fully dependency-free |
| R2 | Polarity of external recordings is unknowable a priori | auto-detect (better-decode-wins) + `invertLevel` override; polarity only affects `$2B` emission, never recognition |
| R3 | MP3 lossy artifacts smear edges (pre-echo) | hysteresis + median-based timing measurement absorb jitter; documented limitation — WAV/FLAC recommended for archival re-import |
| R4 | `$13` verbosity for noise-heavy imports | accepted for v1; `$18`/CSW-v2 emission requires a core-linkable zlib (currently only webapi-vendored — wrong dependency direction) — separate decision, not smuggled into this plan |
| R5 | Non-3.5 MHz tape sources (none today) | single `TAPE_AUDIO_TSTATE_HZ` constant; per-model override is a one-line follow-up if a variant ever needs it |
| R6 | TZX `$20` pause is u16 ms (≤ 65.5 s) | clamp with warning; nothing real exceeds it |
| Q1 | Does the renderer's low-start + silence-pause convention load on real hardware? | round-trip tests prove emulator-side correctness; true hardware validation is manual (one cassette + one deck) — the only non-automatable gate |
| Q2 | Should import auto-insert the produced image? | default no (`insert: false` / unchecked); the option exists everywhere and uses the existing insert path |
| Q3 | Render job asynchrony in WebAPI | synchronous v1 (jobs ≈ seconds); async job endpoint is the documented escape hatch |

## 11. Implementation Phases

| Phase | Scope | Exit criteria |
|---|---|---|
| **A1** | `tapepulsegen` extraction refactor (§4.3) — pure `MaterializePulses`, engine becomes a thin wrapper | zero behavior change: full existing tape suite green, `edgePulseTimings` byte-identical on all fixtures |
| **A2** | WAV render end-to-end: renderer + tinywav adapter + CLI `tape render` + synthesis tests | `tape render x.tap -o s.wav` produces loadable audio; unit tests §8.1 synthesis/encoder rows green |
| **A3** | FLAC render (`FFmpegPipeEncoder` audio-only) + WebAPI `/tape/render` + UI "Export to audio…" | manual WebAPI + UI checklist per AGENTS.md flow; graceful ffmpeg-absent path proven |
| **B1** | Import v1: WAV decode + extractor + segmentation + recognition + `writer_tzx`/`writer_tap` promotion + CLI `tape import` | §6.7 round-trip invariant green over the fixture set; DIZZY_X imports pulse-exact; TAP gate refusal tested |
| **B2** | FLAC/MP3 decode pipes + UI import flow with recognition preview + `insert` option | full §8.2 including load-back integration tests; live UI smoke (macOS AX automation) |

Ordering rationale: A1 unblocks everything; A2+B1 together close the dependency-free WAV↔TZX loop (testable without ffmpeg); FLAC/MP3 and UI polish layer on top. Every phase exits on the standard gates: zero-warning build, full `core-tests` suite, live verification per touched surface.

## 12. References

- Tape-manager design (model, representations, surfaces): [2026-09-01-tape-manager/design.md](../2026-09-01-tape-manager/design.md) — §5.4 landscape (WAV/CSW rows), §5.7 representations, §7 CLI/WebAPI conventions, §9 UI patterns
- TZX byte-level layouts: [tzx-loader-design.md](../2026-09-01-tape-manager/tzx-loader-design.md); in-tree TZX spec transcription `docs/file-formats/tape-images/tzx-tape.md`; `loader_tzx.h` header block
- ROM timing constants and loader tolerance: `tape.h` documentation region
- Encoder contract and FFmpeg transport: `core/recording/src/encoder_base.h`, `encoders/ffmpeg_pipe_encoder.h/.cpp`
- tinywav: `core/src/3rdparty/tinywav/tinywav.h` (writer + `tinywav_read_f`)
- Prior art: Fuse `audio2tape`/`tape2wav` utilities; RAMSOFT WAV2TZX/MakeTZX measured-timings approach
- CSW v1/v2 specification (context for the R4 `$18` decision)
