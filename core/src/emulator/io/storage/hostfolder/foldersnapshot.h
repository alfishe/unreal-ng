#pragma once

/// @file foldersnapshot.h
/// @brief One scan of a host folder, taken when a medium is built from it:
/// names, sizes, modification times, the tree. File contents are never read
/// here, so a large folder scans in time proportional to its number of
/// entries. Later host changes are ignored until an explicit rescan.
///
/// Order is deterministic on every host: in each folder the subfolders come
/// first, then the files, each sorted byte-wise by their UTF-8 names.
/// Modification times are seconds since 1970-01-01 UTC, independent of the
/// host time zone. Everything left out is reported with a reason, never
/// dropped silently. Design: docs/inprogress/2026-09-28-storage-manager/
/// technical-design.md §6.0, §6.4.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class ServiceFileFilter;

struct FolderEntry
{
    std::string name;                 ///< UTF-8, no path
    std::filesystem::path hostPath;   ///< where to read the contents from
    bool isDirectory = false;
    uint64_t size = 0;                ///< files only
    int64_t mtimeUtc = 0;             ///< seconds since 1970-01-01 UTC
    std::vector<FolderEntry> children;
};

struct SkippedEntry
{
    std::string path;    ///< relative to the folder, '/' separated
    std::string reason;  ///< "service (macos)", "symlink", "4 GiB or larger", ...
};

struct FolderScanOptions
{
    bool recursive = true;                  ///< false: top level only, subfolders reported
    bool followLinks = false;               ///< follow symbolic links (with cycle detection)
    uint32_t maxDepth = 32;
    uint32_t maxEntries = 65000;            ///< in the whole tree
    uint32_t maxEntriesPerFolder = 65535;
    uint64_t maxFileSize = 0xFFFFFFFFull;   ///< FAT's limit: larger files are skipped
    const ServiceFileFilter* filter = nullptr;  ///< nullptr: the built-in collections
    std::vector<std::string> excludePatterns;   ///< wildcards on names (the manifest's `exclude`)

    /// Polled every entry (cheap: a function-pointer check, not a syscall) so
    /// a caller scanning a large or slow/network folder off the UI thread can
    /// abort early. Returning true stops the walk at the next checkpoint;
    /// Scan() then fails with error = "cancelled". Empty = never cancels
    std::function<bool()> cancelRequested;

    /// Called after each directory entry is visited (accepted or skipped)
    /// with the running count and the total bytes of every file accepted so
    /// far (directories don't count; a skipped file's size is not added
    /// either - this is "how much did we actually keep", not "how much did
    /// we look at"), off whatever thread calls Scan - never assumed to be the
    /// UI thread. Empty = no progress reporting. A stall watchdog built on
    /// this must compare the count across ticks (slow-but-moving is not
    /// stuck; the count not advancing at all for N seconds is)
    std::function<void(uint64_t entriesScanned, uint64_t bytesScanned)> onProgress;
};

class FolderSnapshot
{
public:
    /// Scan `folder`. False (with `error`) only when the folder itself cannot
    /// be read, or cancelRequested() returned true mid-walk (error ==
    /// "cancelled", checked by the caller with ==, not string-matched);
    /// problems with individual entries go to Skipped() instead
    static bool Scan(const std::filesystem::path& folder, const FolderScanOptions& options, FolderSnapshot& out,
                     std::string* error = nullptr);

    /// The exact string Scan() sets in `error` when cancelRequested() aborted
    /// the walk - compare with ==, never guess at the text
    static constexpr const char* kCancelledError = "cancelled";

    const FolderEntry& Root() const { return _root; }
    const std::vector<SkippedEntry>& Skipped() const { return _skipped; }
    uint64_t TotalFileBytes() const { return _totalFileBytes; }
    uint32_t EntryCount() const { return _entryCount; }

    /// Hash of every entry's relative path, kind, size and time: two scans of
    /// an unchanged folder give the same value (the medium's source identity)
    uint64_t Identity() const { return _identity; }

private:
    FolderEntry _root;
    std::vector<SkippedEntry> _skipped;
    uint64_t _totalFileBytes = 0;
    uint32_t _entryCount = 0;
    uint64_t _identity = 0;
};
