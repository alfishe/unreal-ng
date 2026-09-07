#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstdint>
#include <string>
#include <vector>

/// region <Documentation>

/// TapeImage → TAP archive (tape-audio-bridge design §6.4).
///
/// TAP carries nothing but [u16 len][flag...checksum] byte blocks — no
/// timings, no pulse streams, no pauses. The export is therefore GATED, not
/// degraded: IsExportable() answers whether every block is a representation-1
/// ROM-standard byte payload, and Save() refuses otherwise with a reason
/// pointing at .tzx (design §6.5 — fabricating ROM timings for turbo content
/// would produce a tape that loads wrong, which is worse than no file).

/// endregion </Documentation>

class TapArchiveWriter
{
public:
    /// True when every block is a ROM-standard byte payload within the u16
    /// length limit. When false and `reason` is set, it names the first
    /// offending block and the .tzx alternative.
    static bool IsExportable(const TapeImage& image, std::string* reason = nullptr);

    /// Serialize to TAP bytes. Precondition: IsExportable(image) — callers
    /// gate on it; blocks would otherwise be silently wrong.
    static std::vector<uint8_t> Write(const TapeImage& image);

    /// Gate + write in one step. Returns false (errorText set) when the
    /// image is not TAP-exportable or on I/O failure.
    static bool Save(const TapeImage& image, const std::string& path, std::string& errorText);
};
