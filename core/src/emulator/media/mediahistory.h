#pragma once

/// @file mediahistory.h
/// @brief Versions of the media in their slots, for time travel (TTD v2, Phase 3, Step 4).
///
/// A TTD session lists, per checkpoint, the version of every medium that
/// changed since the checkpoint before; a seek sets each medium back to the
/// version it had there, so a controller that reads during the replay sees
/// what it saw when recording, and its state where the replay stops matches.
///
/// The versions come from the media layer's change layer (storage manager
/// phases H1 and H5). Until it exists a version is the source's ContentId
/// plus a count of the frames that wrote the medium, and no slot can go back
/// (HasVersions false): the replay stays sealed - the CPU reads the recorded
/// sector bytes - and the session reports the medium, without a barrier.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.5.

#include <cstdint>
#include <string>
#include <vector>

struct MediaVersionInfo
{
    std::string slot;          ///< "fdd.a", "sd.zc", "ide0.master"
    std::string format;
    uint64_t contentId = 0;    ///< 0: the slot is empty
    uint64_t version = 0;      ///< the change layer's version; until then, frames that wrote it
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
};
