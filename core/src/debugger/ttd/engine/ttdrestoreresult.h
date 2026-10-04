#pragma once

/// @file ttdrestoreresult.h
/// @brief How exact a restore or replay was, and what it lacked (engine decision D7; FR-7, FR-14).

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdserializable.h"

namespace ttd
{

/// How exact a restore was (engine decision D7). Phase 1 restores are always
/// exact or damaged; "not bit-exact" and "degraded" come with Phases 2-3
enum class TTDRestoreStatus : uint8_t
{
    Exact = 0,
    NotBitExact = 1,   ///< state restored, but the session was recorded with other settings
    Degraded = 2,      ///< some items could not be restored; see the message
    Damaged = 3,       ///< stored data failed its integrity check
};

/// What one item of a restore lacked (Phase 2, Step 3; FR-7)
enum class TTDRestoreIssueKind : uint8_t
{
    DeviceMissingState,   ///< the device exists here, the checkpoint has no state for it
    DeviceNotPresent,     ///< the checkpoint has state for a device this machine lacks
    LayoutUnsupported,
    SizeMismatch,
    DeviceSetDiffers,
    FirmwareDiffers,      ///< restored exactly, but a replay may differ
    ConfigurationDiffers,
    DataDamaged,
    AfterRestoreFailed,
    MediaVersionDiffers,  ///< a medium changed since the checkpoint and cannot go back (Phase 3, Step 4)
};

/// What a device not restored holds now
enum class TTDLiveStateAction : uint8_t
{
    NotApplicable,
    KeptLive,         ///< what it held before the restore (D38: within a session every checkpoint holds
                      ///< every device; this happens only on a machine whose devices differ, or on damage)
};

struct TTDRestoreIssue
{
    TTDRestoreIssueKind kind = TTDRestoreIssueKind::DeviceMissingState;
    TTDRestoreStatus severity = TTDRestoreStatus::Degraded;
    TTDDeviceKey device;   ///< empty instance for machine-wide issues
    TTDLiveStateAction action = TTDLiveStateAction::NotApplicable;
    std::string detail;
    /// DataDamaged, DeviceMissingState in CheckSession: the frames the issue
    /// reaches (a damaged version: from the change that stored it to the
    /// piece's next change that does not depend on it)
    uint64_t firstFrame = 0;
    uint64_t lastFrame = 0;
};

struct TTDRestoreResult
{
    TTDRestoreStatus status = TTDRestoreStatus::Exact;   ///< the worst issue
    std::string message;
    std::vector<TTDRestoreIssue> issues;
    bool Ok() const { return status == TTDRestoreStatus::Exact || status == TTDRestoreStatus::NotBitExact; }

    void Add(TTDRestoreIssue issue)
    {
        if (static_cast<uint8_t>(issue.severity) > static_cast<uint8_t>(status))
            status = issue.severity;
        if (!message.empty())
            message += "; ";
        message += issue.device.instance.empty() ? issue.detail : issue.device.instance + ": " + issue.detail;
        issues.push_back(std::move(issue));
    }
};

}  // namespace ttd
