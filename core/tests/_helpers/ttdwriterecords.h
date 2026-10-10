#pragma once

/// @file ttdwriterecords.h
/// @brief The memory and port writes an engine session's write journal holds, as a test reads them: the engine's
/// write index (each frame's records, drained into it at the frame's boundary) followed by the live ring (the
/// frame being recorded). v1 kept every record in its ring; the controller's ring holds one frame at most.

#include <cstddef>
#include <optional>
#include <vector>

#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdwritejournal.h"

namespace ttdtest
{
inline std::vector<ttd::TTDWriteRecord> WriteRecords(const ttd::TimeTravelController& ttd)
{
    std::vector<ttd::TTDWriteRecord> out;
    ttd.GetEngine().Writes().ForEach([&out](const ttd::TTDWriteRecord& r) { out.push_back(r); });
    if (const ttd::TTDWriteJournal* ring = ttd.GetWriteJournal())
        for (uint64_t seq = ring->SeqTail(); seq < ring->SeqHead(); ++seq)
            out.push_back(ring->RecordAt(seq));
    return out;
}

inline size_t WriteRecordCount(const ttd::TimeTravelController& ttd)
{
    return WriteRecords(ttd).size();
}

/// The newest record @p pred accepts
template <class Pred>
std::optional<ttd::TTDWriteRecord> LastWriteRecord(const ttd::TimeTravelController& ttd, Pred pred)
{
    const std::vector<ttd::TTDWriteRecord> records = WriteRecords(ttd);
    for (auto it = records.rbegin(); it != records.rend(); ++it)
        if (pred(*it))
            return *it;
    return std::nullopt;
}
}  // namespace ttdtest
