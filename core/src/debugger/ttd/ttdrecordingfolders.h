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

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "common/cleanupmanager.h"

namespace ttd
{
    constexpr const char* kRecordingOwnerFile = "owner.pid";
    constexpr const char* kBranchIndexFile = "branches.tdx";
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

    /// A segment file's name: "segment-0003.ttd" (the trunk) or "segment-0003-b2.ttd" (branch line 2); false for
    /// any other name
    bool ParseSegmentFileName(const std::string& name, uint32_t& number, uint32_t& line);

    /// branches.tdx read back (storage.md §4): the branch events and the closed segments, in order
    struct TTDBranchIndex
    {
        struct Segment
        {
            std::string file;   ///< the name, in the folder
            uint32_t line = 0;
            uint64_t firstFrame = 0;
            uint64_t frameSpan = 0;
            uint64_t parts = 0;
            uint64_t bytes = 0;
            bool operator==(const Segment& o) const
            {
                return file == o.file && line == o.line && firstFrame == o.firstFrame && frameSpan == o.frameSpan &&
                       parts == o.parts && bytes == o.bytes;
            }
        };
        /// The session file a loaded session's folder continues (Q-11): its branches' parents are there
        struct Source
        {
            std::string path;               ///< a .ttd file or a recording folder
            uint64_t size = 0;              ///< a file's size (0: a folder)
            std::array<uint8_t, 16> uuid{}; ///< a file's header UUID
        };
        std::optional<Source> source;
        std::vector<std::vector<uint8_t>> creates;   ///< the branch table's create records, as written
        std::set<uint32_t> deleted;                  ///< lines with a delete record
        std::vector<Segment> segments;
        size_t records = 0;                          ///< read (a cut file keeps its complete ones)
    };

    /// One recording's folder: created when a recording starts, its owner
    /// file naming this process, its segment files numbered from 0
    /// (segment-0000.ttd). "Save as" joins the segments into one file at the
    /// chosen path; "discard" deletes the folder.
    ///
    /// With branches (storage.md §4) each file holds one line: a branch's files are named "-b<line>", numbered in
    /// capture order with the trunk's; branches.tdx (from the first fork on) lists the branch events and the closed
    /// segments, append-only, each record synced. A deleted branch gets a delete record first, then its files go: a
    /// crash between leaves files that Segments() skips and Open() deletes.
    class TTDRecordingFolder
    {
    public:
        ~TTDRecordingFolder();

        /// <root>/<date-time>-<name>, local time ("2026-10-04-153012-pentagon"); "-2", "-3" when taken
        static std::unique_ptr<TTDRecordingFolder> Create(const std::string& root, const std::string& name,
                                                          std::time_t when, std::string& error);

        /// The recording folder at @p path, or the one holding the segment file @p path
        /// (".../2026-10-04-153012-pentagon/segment-0003.ttd"); null when it is neither
        static std::unique_ptr<TTDRecordingFolder> Open(const std::string& path);

        const std::string& Path() const { return _path; }
        std::string SegmentPath(uint32_t index) const;
        /// File @p number of line @p line (0: the trunk, SegmentPath)
        std::string SegmentPath(uint32_t number, uint32_t line) const;
        /// The paths of a writer's files on @p line: its file n the next number in capture order the first time
        /// it is asked for (the same number after: a rewind writes it again)
        std::function<std::string(uint32_t)> LinePaths(uint32_t line);
        /// The segment files that exist, in order (the oldest may be gone: DropOldestSegments); without the
        /// deleted branches' files
        std::vector<std::string> Segments() const;

        /// branches.tdx: created at the first branch event from the files there (a segment record each, a create
        /// for each branch's first file); then each event appended and synced
        bool NoteBranchCreated(const std::vector<uint8_t>& createRecord, std::string* error = nullptr);
        /// The folder continues the session loaded from @p path (a file or a recording folder): a source record
        /// in branches.tdx, so SaveAs joins that file first (Q-11)
        bool NoteSource(const std::string& path, std::string* error = nullptr);
        /// A segment file closed (after the index has the branches; before, a rebuild finds it)
        bool NoteSegmentClosed(const std::string& path);
        /// Delete branch lines (children first): delete records appended, then their files removed
        bool DeleteLines(const std::vector<uint32_t>& lines, std::string* error = nullptr);
        bool HasBranchIndex() const;
        std::string BranchIndexPath() const;
        /// Read branches.tdx (false: missing or no header)
        static bool ReadBranchIndex(const std::string& path, TTDBranchIndex& out, std::string& error);
        /// What branches.tdx would hold if written from the segment files now (a create from each branch's first
        /// file, a segment record per file)
        TTDBranchIndex IndexFromFiles() const;
        /// Write branches.tdx again from the files (it is missing or damaged), keeping the delete records it has
        bool RebuildBranchIndex(std::string* error = nullptr);
        /// Delete all but the newest @p keep segment files (a black box's window moved on); how many went
        size_t DropOldestSegments(size_t keep);

        /// One .ttd file at @p target from the segments (they must be finished); refuses an existing
        /// target unless @p overwrite
        bool SaveAs(const std::string& target, bool overwrite, std::string& error) const;
        /// Delete the folder and everything in it
        bool Discard(std::string* error = nullptr);

    private:
        struct IndexWriter;
        bool AppendIndexRecord(const std::vector<uint8_t>& record, std::string* error);
        /// The files SaveAs joins: the source's (checked: there, the same size and UUID), then the segments
        bool JoinList(std::vector<std::string>& out, std::string& error, int depth) const;
        bool OpenIndex(const TTDBranchIndex& content, std::string* error);
        void FinishDeletions();
        uint32_t NextNumber() const;

        std::string _path;
        uint32_t _nextNumber = 0;
        std::unique_ptr<IndexWriter> _index;
    };
}  // namespace ttd
