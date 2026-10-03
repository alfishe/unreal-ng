#pragma once

/// @file ttdv1events.h
/// @brief v1's input journal (with its network records and received bytes)
/// and external events into the engine's event log (Phase 3, Step 1): the
/// shadow session feeds what v1 journaled since the last frame, the v1 file
/// reader feeds a whole session. Kinds keep their numbers (a marker adds
/// 0x0100); network bytes and a marker's reason become payloads.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ttd
{

class TimeTravelEngine;
class TTDInputJournal;
class TTDExternalEventJournal;

/// How far each v1 journal has been fed
struct TTDV1EventCursor
{
    size_t input = 0;
    size_t external = 0;
};

/// Feed the entries after @p cursor whose frame is at most @p throughFrame,
/// merged in time order (input before a marker at the same instant), and
/// advance the cursor. Returns how many were appended; an entry the engine
/// refuses (before its first frame) is skipped and counted in @p refused
/// @p editData: a debugger edit's bytes by marker index (live recording,
/// TimeTravelManager::ToolEditPayloads); such an edit becomes input with its
/// bytes, one without them (v1 files) a barrier
size_t FeedV1Events(TimeTravelEngine& engine, const TTDInputJournal& input, const TTDExternalEventJournal& external,
                    TTDV1EventCursor& cursor, uint64_t throughFrame, size_t* refused = nullptr,
                    const std::unordered_map<size_t, std::vector<uint8_t>>* editData = nullptr);

}  // namespace ttd
