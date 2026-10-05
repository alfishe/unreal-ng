#pragma once

/// @file ttdsessionfacts.h
/// @brief What the time-travel controller keeps with a session file beside
/// the engine's data (Phase 5, C4b): holder streams of the engine's session
/// file (TTDHolderStream). The facts say which machine recorded the session
/// and how complete its history is - what a loader checks and what file-info
/// shows without loading; the coverage index and the bookmarks are the
/// controller's own indexes over the session.
///
/// Worked example: a PENTAGON session saved with coverage on and one bookmark
/// carries three holder streams: facts {model 2, ROM signature, recorded by
/// "emu-1", input complete}, the coverage index as TTDCoverageIndex writes
/// it, and one bookmark {frame 120, T 0, "boss"}.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdsessionfile.h"

namespace ttd
{

class TTDBookmarkJournal;

namespace holderstream
{
constexpr uint16_t kFacts = sessionstream::kHolderFirst;
constexpr uint16_t kCoverage = sessionstream::kHolderFirst + 1;
constexpr uint16_t kBookmarks = sessionstream::kHolderFirst + 2;
}  // namespace holderstream

struct TTDSessionFacts
{
    uint8_t modelId = 0;              ///< MEM_MODEL the session was recorded on
    uint16_t modelRamPages = 0;
    uint64_t romSignature = 0;        ///< 0: unknown (not checked)
    uint64_t capturedAtUnixMs = 0;
    std::string recordedBy;           ///< the recording instance's symbolic id
    uint64_t peripheralMask = 0;      ///< the recorded devices (PeripheralId bits)
    uint64_t notRecordedMask = 0;     ///< fitted, not recorded by design
    bool inputHistoryComplete = false;
    bool portJournalValid = false;    ///< the bus journals hold every IN of the history
    std::string portJournalOffReason;
};

std::vector<uint8_t> EncodeSessionFacts(const TTDSessionFacts& facts);
bool DecodeSessionFacts(const std::vector<uint8_t>& bytes, TTDSessionFacts& facts);

std::vector<uint8_t> EncodeBookmarks(const TTDBookmarkJournal& bookmarks);
bool DecodeBookmarks(const std::vector<uint8_t>& bytes, TTDBookmarkJournal& bookmarks);

/// Find holder stream @p id in a session file without loading the session
/// (the last record of that stream). False when absent or unreadable
bool ReadHolderStream(const ITTDByteSource& source, uint16_t id, std::vector<uint8_t>& out, std::string& error);

struct TTDFileInfo;
/// file-info for a session file in the engine's format: the container's parts
/// (frames, records) and the controller's facts, without loading the session
bool ReadEngineFileInfo(const ITTDByteSource& source, TTDFileInfo& info, std::string& error);

}  // namespace ttd
