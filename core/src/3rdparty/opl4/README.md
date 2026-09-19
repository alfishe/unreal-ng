# libopl4 — YMF278B (OPL4) synthesis

In-house emulation of the Yamaha YMF278B (OPL4) as fitted to the ZXM-MoonSound
card: the OPL3-class FM half, the 24-slot wavetable (PCM) half, the chip's bus
and timing model, and a host render layer. Standard library only; no
allocation on the audio path after `Configure()`; deterministic, fully
serialisable chip state.

## Using it

```cpp
#include "opl4/opl4.h"
#include "opl4/wavememory.h"

opl4::Opl4Config cfg;
cfg.hostTickRate = 3500000;           // the time axis of every call below
cfg.outputRate = 48000;               // 44100 .. 192000
cfg.mode = opl4::RenderMode::HiFi;    // band-limited FM; Authentic = HoldDrop reducer

opl4::WaveMemory mem;
mem.Configure(2u << 20, 1u << 20);    // 2 MiB wave ROM (YRW801), 1 MiB SRAM
// ... copy the ROM image into mem ...

opl4::Opl4 chip;
chip.Configure(cfg, &mem);
chip.EnableSplitStreams(true);        // separate FM and PCM outputs
chip.Reset(0);

chip.WriteFm(t, bank, reg, value);    // guest port traffic, timestamped
chip.WriteWave(t, reg, value);
chip.Run(frameEnd);                   // advance the chip to a time
chip.RenderSplit(fm, pcm, frames);    // or Render() for the mixed stream
chip.SetOutputRate(96000);            // live, at a frame boundary: chip state is kept
```

- **Time:** every call takes a timestamp on the host axis (`hostTickRate`).
  The chip advances lazily on its two internal grids: FM at 33.8688 MHz / 684
  (49 516.4 Hz) and the output at / 768 (44 100 Hz).
- **Render modes:** `HiFi` band-limits the FM grid straight to the output
  rate (the recommended default). `Authentic` models a hold/drop reducer from
  the FM grid to 44.1 kHz. It is not confirmed on hardware and adds jitter
  distortion (−16 dB THD+N on a 4 kHz FM tone).
- **Render options:** `SetQuality` (resampler length), `SetBoardAnalog` (the
  ZXM-MoonSound output filter, RC 4.08 kHz + Sallen-Key 27.7 kHz), `SetPunch`,
  `SetRoom`. Settings survive mode, quality and rate changes.
- **State:** `SaveState` / `LoadState` / `StateSize` capture the chip. The
  render layer (filter and resampler history) is not captured and restarts on
  load.
- **Taps:** `ChannelPeak` and `SetChannelMute` per FM channel and PCM slot.

## Layout

```
include/opl4/   public API: opl4.h, opl4config.h, wavememory.h, iwavememory.h, ifmsynth.h
src/opl4.cpp    chip top level: time model, bus, reducer, mix, state
src/fm/         FM engine (Opl4Fm) behind FmBus (registers, timers, routing)
src/pcm/        PCM engine (24 slots, tone headers, envelopes, LFO)
src/common/     shared exponential / mix tables
src/opl4render* render layer: polyphase resampler, board filter, character chain, DC blocker
tests/          self-contained test harness (no third-party framework)
```

## Tests

```bash
cmake -S . -B build -G Ninja && ninja -C build opl4tests && ./build/opl4tests
```

When embedded, `opl4tests` is excluded from the default build. The suites
cover:

- **Units:** tables, header decode, bus timing, save/restore neutrality,
  determinism.
- **Behaviour vectors:** chip-visible register semantics.
- **Per-field conformance sweeps.**
- **FM engine behaviour:** key-on semantics, the envelope range, NTS, drum,
  lead and pad voices from real MoonBlaster tunes.
- **Output stage:** sine purity per render mode and render API, the board
  filter response, and live switching through every standard output rate.

## Design and history

Design documents and investigation logs live in
`docs/inprogress/2026-09-13-moonsound/` (core TDD, integration, TTD,
verification findings, output-stage analysis). Code comments cite their
decision and section numbers (for example "D2", "§8.3").
