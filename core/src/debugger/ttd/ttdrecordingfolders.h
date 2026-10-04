#pragma once

/// @file ttdrecordingfolders.h
/// @brief Where TTD recordings live on disk (Phase 4, owner decision
/// 2026-10-04): every recording writes into a folder of its own,
/// <user data>/ttd/<date-time>-<name>/, holding its segment files and, while
/// a process records into it, the file "owner.pid" with that process's id.
/// Saved recordings are .ttd files in <user data>/ttd/ itself.
///
/// A folder whose owner process no longer runs is a recording left behind by
/// a crash. It can be opened or repaired for 7 days; then the startup cleanup
/// (CleanupManager, step "ttd-crashed-recordings") deletes it.
///
/// Worked example: ttd/2026-10-04-153012-pentagon/ holds owner.pid = 4711 and
/// three segment files. The emulator crashed; ten days later no process 4711
/// runs and the newest file in the folder is ten days old: the step deletes
/// the folder. A folder whose owner.pid names a running process is never
/// touched, however old.

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "common/cleanupmanager.h"

namespace ttd
{
    constexpr const char* kRecordingOwnerFile = "owner.pid";
    constexpr const char* kCrashedRecordingsCleanupStep = "ttd-crashed-recordings";
    constexpr std::chrono::hours kCrashedRecordingKeep{24 * 7};

    /// <user data>/ttd (not created; empty when the user folder is unknown)
    std::string RecordingsRoot();

    /// The process id an owner file names (0: missing or unreadable)
    uint32_t ReadRecordingOwner(const std::string& folder);
    /// Write this process's id into the folder's owner file
    bool WriteRecordingOwner(const std::string& folder);

    /// Delete the folders under @p root whose owner does not run and whose
    /// newest file is older than @p keep at @p now. Files directly in @p root
    /// (saved recordings) are never touched
    void CleanCrashedRecordings(CleanupContext& context, const std::string& root,
                                std::filesystem::file_time_type now, std::chrono::seconds keep);

    /// The step for CleanupManager: crashed recordings in RecordingsRoot(), weekly
    CleanupStep CrashedRecordingsCleanupStep();

    /// One recording's folder: created when a recording starts, its owner
    /// file naming this process, its segment files numbered from 0
    /// (segment-0000.ttd). "Save as" joins the segments into one file at the
    /// chosen path; "discard" deletes the folder.
    class TTDRecordingFolder
    {
    public:
        /// <root>/<date-time>-<name>, local time ("2026-10-04-153012-pentagon"); "-2", "-3" when taken
        static std::unique_ptr<TTDRecordingFolder> Create(const std::string& root, const std::string& name,
                                                          std::time_t when, std::string& error);

        const std::string& Path() const { return _path; }
        std::string SegmentPath(uint32_t index) const;
        /// The segment files that exist, in order
        std::vector<std::string> Segments() const;

        /// One .ttd file at @p target from the segments (they must be finished); refuses an existing
        /// target unless @p overwrite
        bool SaveAs(const std::string& target, bool overwrite, std::string& error) const;
        /// Delete the folder and everything in it
        bool Discard(std::string* error = nullptr);

    private:
        std::string _path;
    };
}  // namespace ttd
