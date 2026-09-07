#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// region <Documentation>

/// Offline tape image → WAV/FLAC renderer (tape-audio-bridge design §5).
///
/// Whole tape or any catalog-index block range becomes a bipolar square wave
/// the engine's own pulse representation describes: MaterializePulses() is
/// the single source of timing truth, so ROM, turbo and custom-loader blocks
/// render with their exact pulse widths — what a real deck needs to LOAD.
///
/// Headless by construction: path in, TapeImage via TapeLoaderRegistry, no
/// EmulatorContext. Encoders: native tinywav for WAV, FFmpegPipeEncoder
/// (audio-only) for FLAC — both behind the EncoderBase contract.

/// endregion </Documentation>

struct TapeRenderRequest
{
    std::string sourcePath;             // .tap/.tzx/.spc/… — content-probed, extension-agnostic per the registry
    // Block selection: indices are TapeBlockDescriptor catalog indices
    // (identical to GetBlockCatalog() / the Tape Manager table). Empty range = whole tape.
    size_t firstBlock = 0;
    size_t lastBlock = SIZE_MAX;        // inclusive
    std::string outputPath;             // extension picks the encoder: .wav native, .flac via ffmpeg
    uint32_t sampleRate = 44100;        // 44100 default (community standard); 48000/96000 accepted
    double amplitude = 0.8;             // FS fraction
    bool invertLevel = false;           // polarity override (design R2)
    std::function<void(size_t blocksDone, size_t blocksTotal)> onProgress;  // worker thread
    std::function<bool()> cancelRequested;                                 // polled per block
};

struct TapeRenderResult
{
    bool ok = false;
    std::string errorText;              // probe/decode/encoder failures, ffmpeg-missing guidance
    std::vector<std::string> warnings;  // e.g. "block 7: no signal (control-only), silence only"
    double durationSec = 0.0;           // audio duration
    uint64_t samplesWritten = 0;
    size_t blocksRendered = 0;
    std::string encoderUsed;            // "tinywav" | "ffmpeg(flac)"
};

/// Render the requested block range of `request.sourcePath` to audio.
/// Never throws; all failures land in TapeRenderResult::errorText.
TapeRenderResult RenderTapeToAudio(const TapeRenderRequest& request);

/// True when FLAC output is possible (ffmpeg found on this system). UIs use
/// this to disable the FLAC choice with an explanatory tooltip (design §7.3)
/// instead of letting the render fail after the fact.
bool IsFlacRenderAvailable();
