#pragma once

/// @file recentfiles.h
/// @brief The files opened lately, per kind, for every surface: the Qt File > Recent Files menu, the CLI `recent`,
/// Lua / Python `recent_files()`, MCP `media` action `recent`, REST `GET /api/v1/recent`, the readout and WebSocket
/// topic `media.recent`, the catalogue action `media.open_recent`.
///
/// Kinds: "snapshot", "tape", "disk", "rzx", "image" (hard disk / card images), "cd". The emulator notes a file when it
/// opened it (Emulator::LoadSnapshot / LoadTape / LoadDisk, a media insert or swap of a file, an RZX playback); a
/// file opened again moves to the front. Ten per kind. A path is kept as the absolute path it was opened with.
///
/// Persistence is the client's choice: unreal-qt keeps the list in `<user data>/recent-files.json` (SetStoreFile);
/// without a store file (tests, headless tools) the list lives in memory only.
///
/// Example: Note("tape", "/games/elite.tzx"); List("") -> [{kind tape, path /games/elite.tzx, name elite.tzx, ...}]

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class RecentFiles
{
public:
    struct Entry
    {
        std::string kind;
        std::string path;
        std::string name;    ///< the file name (the last path component)
        int64_t time = 0;    ///< when it was opened: seconds since 1970 (UTC)
    };

    static constexpr size_t kPerKind = 10;

    static RecentFiles& Instance();
    /// The kinds, in the order lists show them
    static const std::vector<std::string>& Kinds();
    static bool IsKind(const std::string& kind);
    /// A media slot kind (MediaKindName: floppy, tape, block, optical) as the kind a recent file is listed under
    static std::string KindOfSlot(const std::string& slotKind);

    /// Note a file that was opened (an unknown kind or an empty path is ignored); saved to the store file
    void Note(const std::string& kind, const std::string& path);
    /// The entries of one kind, newest first; "" or "all": every kind merged, newest first
    std::vector<Entry> List(const std::string& kind = {}) const;
    /// Forget one kind ("" / "all": everything)
    void Clear(const std::string& kind = {});
    /// Bumped by every change (a poll sees the list moved)
    uint64_t Revision() const;

    /// Load the list from `path` (JSON, written by Save) and save it there after every change; "" stops saving.
    /// False with `error` when the file exists but is not a list (the list is then empty and the file is replaced
    /// at the next change)
    bool SetStoreFile(const std::string& path, std::string* error = nullptr);
    const std::string& StoreFile() const { return _storeFile; }

    /// The list as surfaces show it: {revision, kinds: {snapshot: [..], ...}, all: [..]}; `kind` narrows it to
    /// {revision, kind, files: [..]}
    StateNode ToValue(const std::string& kind = {}) const;
    static StateNode EntryValue(const Entry& entry, size_t index);

private:
    RecentFiles() = default;
    void SaveLocked() const;

    mutable std::mutex _mutex;
    std::vector<Entry> _entries;  ///< newest first, at most kPerKind per kind
    std::string _storeFile;
    uint64_t _revision = 0;
};
