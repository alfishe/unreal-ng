// libsam2695 tools - Standard MIDI File reader (formats 0, 1 and 2; PPQN and SMPTE time division).
//
// Produces the MIDI byte stream of the file with absolute times in seconds: channel messages with
// running status expanded, System Exclusive as F0 ... F7, F7 escape packets as raw bytes. Meta events
// are consumed (tempo changes drive the time conversion) and not emitted.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sam2695tools
{

struct SmfEvent
{
    double seconds = 0.0;
    std::vector<uint8_t> bytes;
};

struct SmfFile
{
    std::vector<SmfEvent> events; // in time order
    double durationSeconds = 0.0;
    uint16_t format = 0;
    uint16_t tracks = 0;
};

// false (and `error`) when the file is not a readable SMF
bool ReadSmf(const std::string& path, SmfFile& out, std::string& error);

} // namespace sam2695tools
