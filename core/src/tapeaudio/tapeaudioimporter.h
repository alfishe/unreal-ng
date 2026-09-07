#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// region <Documentation>

/// Sound file → tape image (tape-audio-bridge design §6).
///
/// Pipeline: decode (WAV in-tree via tinywav; FLAC/MP3 via the existing
/// ffmpeg subprocess plumbing — no custom format code anywhere) →
/// TapePulseExtractor → TapeRecognizer → a loader-shaped TapeImage the
/// engine, CLI, WebAPI and UI consume exactly like a loaded file.
///
/// Saving is extension-dispatched to the production writers: .tzx always
/// (exact pulse preservation), .tap only through IsExportable() — the gate
/// refuses non-ROM-standard content instead of degrading it.

/// endregion </Documentation>

struct TapeImportRequest
{
    std::string sourcePath;
    /// Schmitt band override (design §6.1): 0.05..0.45 of the signal
    /// half-range. The default 0.2 is the calibrated cassette margin —
    /// lower tightens noise rejection, higher tolerates noisier captures.
    double hysteresis = 0.2;
    std::function<bool()> cancelRequested;          // polled between stages
    std::function<void(const std::string& stage)> onStage;  // "decode"/"extract"/"recognize"
};

struct TapeImportResult
{
    bool ok = false;
    std::string errorText;
    std::vector<std::string> warnings;      // recognizer notes + decode anomalies

    TapeImage image;                        // recognized tape; formatId "tzx" (§6.4)
    std::string decoderUsed;                // "tinywav(wav)" / "ffmpeg(flac)" / "ffmpeg(mp3)"
    uint32_t sampleRate = 0;
    uint64_t samplesDecoded = 0;
    size_t blocksRecognized = 0;            // every kind, pauses included
    size_t signalEdges = 0;                 // extractor diagnostics
};

struct TapeSaveResult
{
    bool ok = false;
    std::string errorText;
    size_t blocksWritten = 0;
};

/// Decode + extract + recognize. Never throws; every failure is a
/// structured errorText (missing file, unsupported type, no signal, decode
/// failure), and recognizer notes surface as warnings on success.
TapeImportResult ImportAudioToTape(const TapeImportRequest& request);

/// Extension-dispatched save: .tzx → TzxArchiveWriter (always possible),
/// .tap → TapArchiveWriter behind IsExportable() (refusal names the block
/// and the .tzx alternative).
TapeSaveResult SaveTapeImage(const TapeImage& image, const std::string& outputPath);
