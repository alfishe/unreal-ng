#pragma once

/// @file ttdsessionfile.h
/// @brief A TimeTravelEngine session in the session container (Phase 4,
/// Step 2; design: phase-4-session-file-tdd.md §5.2). Save writes the
/// session's parts; Load rebuilds a session from them, read-only.
///
/// What goes where:
///
///   header tables  the memory regions, the device table, the snapshot interval
///   stream 1       piece versions in the order they appeared: encoding, base
///                  (by its number in the file), depth, CRC, payload as stored
///   stream 3       checkpoints: frame, start, CPU, chipset, journal cursors and,
///                  per region, the pieces taking the next versions
///   stream 5       events with their payloads
///   stream 6       configuration entries and media-version changes
///   stream 7       the write journal and its segments (ancillary, D40)
///   streams 13-16  the bus journals (IN, OUT, interrupt vectors) and the
///                  sector reads
///
/// Versions are numbered in the file as they appear: the store reuses its
/// ids, the file does not. Reference tables are not stored: Load rebuilds them
/// the way CaptureFrame builds them. A part depends on the parts holding the
/// current version of every piece at its end, and the bases of its own
/// versions, so a damaged part makes exactly the frames that need it
/// unreachable.
///
/// Load reads the parts in order and stops before the first unreachable one;
/// the report says how many frames came in and why it stopped. The engine's
/// own state for continuing a capture (delta base, time lines) is not in the
/// file: a loaded session is read-only.
///
/// Worked example: a 300-frame session saved with 50 checkpoints per part
/// gives six parts. Damage in part 3's piece record: Load brings frames of
/// parts 0-2 back (150 frames) and reports part 3 as the reason it stopped.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdcontainer.h"

namespace ttd
{

class TimeTravelEngine;

namespace sessionstream
{
constexpr uint16_t kPieces = 1;
constexpr uint16_t kCheckpoints = 3;
constexpr uint16_t kEvents = 5;
constexpr uint16_t kConfiguration = 6;
constexpr uint16_t kWriteJournal = 7;
constexpr uint16_t kBusReads = 13;
constexpr uint16_t kBusWrites = 14;
constexpr uint16_t kBusVectors = 15;
constexpr uint16_t kMediaReads = 16;
}  // namespace sessionstream

struct TTDSessionSaveParams
{
    uint32_t checkpointsPerPart = 50;   ///< about a second of recording
    uint64_t createdMicros = 0;         ///< the header's creation time (tests fix it)
    std::array<uint8_t, 16> uuid{};     ///< the session's identity
};

struct TTDSessionLoadReport
{
    size_t checkpoints = 0;      ///< loaded
    size_t partsLoaded = 0;
    size_t partsInFile = 0;
    bool complete = false;       ///< every part came in
    std::string stoppedAt;       ///< why the load stopped early
    std::vector<std::string> notes;   ///< the container's notes (scan, skipped streams)
};

class TTDSessionFile
{
public:
    static bool Save(const TimeTravelEngine& engine, ITTDByteSink& sink, std::string& error,
                     const TTDSessionSaveParams& params = {});
    /// Replace @p engine's session by the file's. False with the reason when
    /// nothing could be loaded (not a session file, damaged header or tables)
    static bool Load(TimeTravelEngine& engine, const ITTDByteSource& source, std::string& error,
                     TTDSessionLoadReport* report = nullptr);

    /// The stream ids this version reads
    static bool KnownStream(uint16_t id);
};

}  // namespace ttd
