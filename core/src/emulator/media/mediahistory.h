#pragma once

/// @file mediahistory.h
/// @brief Versions of the media in their slots, for time travel (TTD v2, Phase 3, Step 4).
///
/// A TTD session lists, per checkpoint, the version of every medium that
/// changed since the checkpoint before; a seek sets each medium back to the
/// version it had there, so a controller that reads during the replay sees
/// what it saw when recording, and its state where the replay stops matches.
///
/// A version is the volume's identity plus a count of the medium's changes
/// (the frames that wrote it, write-protect toggles). While a TTD history
/// exists the media manager keeps every change in the slot's undo log
/// (MediaUndoLog; owner decision 2026-10-10, gaps 11-12), and SetHead sets a
/// block medium or a floppy to any version the log reaches. A version it does
/// not reach (another medium, past the log's budget) is reported; the replay
/// stays sealed there - the CPU reads the recorded sector bytes.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.5.

#include <cstdint>
#include <string>
#include <vector>

struct MediaVersionInfo
{
    std::string slot;          ///< "fdd.a", "sd.zc", "ide0.master"
    std::string format;
    uint64_t contentId = 0;    ///< the volume (Medium::VolumeId, unchanged by writes); 0: the slot is empty
    uint64_t version = 0;      ///< the medium's changes (frames that wrote it, write-protect toggles)
    bool hasVersions = false;  ///< SetHead can set it back
};

class IMediaHistory
{
public:
    virtual ~IMediaHistory() = default;

    /// Moves whenever a medium is inserted, ejected or written (first write of
    /// a frame): a caller that saw the same stamp has the same versions
    virtual uint64_t VersionStamp() const = 0;
    /// Every slot's medium and version now
    virtual void CurrentVersions(std::vector<MediaVersionInfo>& out) const = 0;
    /// Set @p slot's medium to @p version (a seek). False: the slot keeps no
    /// versions, or not that one
    virtual bool SetHead(const std::string& slot, uint64_t version) = 0;
    /// Whether SetHead(@p slot, @p version) would go ahead; changes nothing (a timeline cut's plan)
    virtual bool Reaches(const std::string& /*slot*/, uint64_t /*version*/) const { return false; }
};
