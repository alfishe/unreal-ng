#pragma once

/// @file operationlog.h
/// @brief The last operations on the machine, always on (architecture §7, owner decision 2026-10-09): who did
/// what, at which frame, from which surface, with which result - refusals included.
///
/// Threading: the coordinator appends (any thread, one mutex, a few entries per operation at most); any thread
/// reads a copy.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "machineop.h"

namespace coordinator
{
struct OperationLogEntry
{
    uint64_t seq = 0;   ///< 1, 2, ... in append order; never reused, also after the ring wraps
    MachineTime time;
    OpKind kind = OpKind::Reset;
    OpSource source = OpSource::Unknown;
    MachineMode mode = MachineMode::Live;   ///< the mode the rules saw
    OpStatus status = OpStatus::Done;
    SessionEffect effect = SessionEffect::None;   ///< the session effect applied
    std::string summary;   ///< MachineOp::summary
    std::string detail;    ///< refused / failed: the reason; a note on an owner that broke its contract
    /// How many times it happened in a row: the same tool edit again (a script's pokes) takes the newest entry's
    /// place with the next seq instead of a slot of its own, so the edits never push the other operations out
    uint32_t repeats = 1;
};

class OperationLog
{
public:
    static constexpr size_t kCapacity = 1024;

    /// Stores @p entry with the next sequence number and returns it. A tool edit equal to the newest entry
    /// (OpKind::ToolEdit: source, mode, status, summary, detail) counts there (repeats) under the new number
    uint64_t Append(OperationLogEntry entry);
    /// The entries kept, oldest first; @p since: only those with a larger seq
    std::vector<OperationLogEntry> Entries(uint64_t since = 0) const;
    /// The newest entry's seq; 0 when nothing was logged
    uint64_t LastSeq() const;

private:
    mutable std::mutex _mutex;
    std::vector<OperationLogEntry> _ring;   ///< grows to kCapacity, then overwritten at _next
    size_t _next = 0;
    uint64_t _seq = 0;
};
}  // namespace coordinator
