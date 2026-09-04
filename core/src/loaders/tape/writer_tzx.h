#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstdint>
#include <string>
#include <vector>

/// region <Documentation>

/// TapeImage → TZX 1.20 archive (tape-audio-bridge design §6.4).
///
/// The production mirror of LoaderTZX: every representation the loaders
/// produce is serialized back to the block type it came from —
///   representation 1 (data, no timing)          → $10 standard speed
///   representation 2 (data + timing profile)    → $11 turbo speed
///   representation 3 (edgePulseTimings)         → $12 tone / $13 sequence /
///                                                 $20 pause, split on gaps
/// Control markers with no content (stop-the-tape) round-trip as $20 0.
/// Over-long ROM blocks (> 64 KiB, no legal $10 length) degrade to $11 with
/// the ROM timing set — byte-faithful, format-legal.

/// endregion </Documentation>

class TzxArchiveWriter
{
public:
    /// Serialize to TZX bytes. Never fails; the image model guarantees the
    /// invariants the block types require (u16 pulse values etc. via the
    /// clamps documented at each emit site).
    static std::vector<uint8_t> Write(const TapeImage& image);

    /// Write to `path`. Returns false (errorText set) on I/O failure only.
    static bool Save(const TapeImage& image, const std::string& path, std::string& errorText);
};
