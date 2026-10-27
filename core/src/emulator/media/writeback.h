#pragma once

/// @file writeback.h
/// @brief S4: a composite's guest writes carried back into its host folder
/// layers file by file (flatten-strategies.md S4, DT-10 to DT-12). The plan
/// routes every file operation from `media changes` to a layer: a writable
/// folder layer that owns it, else the upper layer (copy-up); a guest delete
/// follows the owner layer's `onDelete`. Host files changed since the build
/// are conflicts. Applying stages new contents next to their targets, lists
/// the steps in `<descriptor>.writeback` and then performs them, so an
/// interrupted apply is completed the next time the descriptor is inserted.
/// Deletes that keep the host file go to `<descriptor>.whiteout`: the user's
/// descriptor is never rewritten.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c8-commit-writeback.md §3.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "emulator/media/mediatypes.h"

class Medium;
struct ComposeDescriptor;

struct WriteBackStep
{
    enum class Kind : uint8_t
    {
        Write,     ///< a file's new content into `host` (staged, then renamed into place)
        Mkdir,     ///< a host directory
        Rename,    ///< `from` renamed to `host` inside one layer
        Remove,    ///< `host` removed (onDelete: delete), a directory when it is empty
        Move,      ///< `from` moved to `host` under the layer's deleted-files folder (onDelete: move)
        Whiteout,  ///< `path` hidden from the next build (onDelete: keep, or a read-only owner)
        Note,      ///< nothing done; `detail` says why (onDelete: ignore)
        Attributes,  ///< `path`'s FAT attribute bits (`detail`: `RHS` / `-`) into the descriptor's `.attributes` sidecar
        Trash,       ///< `host` moved to the host's trash (onDelete: trash)
    };
    Kind kind = Kind::Note;
    std::string path;              ///< the guest's path (in its partition, when `partition` is set)
    std::string partition;         ///< a partitioned disk's partition (p1, p2, ...); empty: the whole disk is the volume
    std::string layer;             ///< the layer it lands in
    std::filesystem::path host;    ///< the host path written, made, removed or moved to
    std::filesystem::path from;    ///< Rename / Move: the host path before
    uint64_t bytes = 0;            ///< Write
    int64_t mtimeUtc = 0;          ///< Write: the guest's file time, so the next build gives the same entry
    std::string detail;

    static const char* KindName(Kind kind);
};

struct WriteBackPlan
{
    std::vector<WriteBackStep> steps;
    std::vector<std::string> errors;     ///< conflicts, no writable layer, names the host cannot store: S4 does not run
};

struct WriteBackOptions
{
    bool force = false;         ///< run although the guest's file system has lost clusters or cross-links
    bool keepBoth = false;      ///< a conflict writes "name (guest).ext" next to the host file instead of refusing
};

class WriteBack
{
public:
    /// The plan for `medium` (a composite with session writes, built from `descriptor`)
    static MediaResult Plan(Medium& medium, const ComposeDescriptor& descriptor, const WriteBackOptions& options, WriteBackPlan& plan);

    /// Perform `plan` (without errors): stage, journal, apply, drop the journal. The caller rebuilds the composite
    static MediaResult Apply(Medium& medium, const ComposeDescriptor& descriptor, const WriteBackPlan& plan);

    /// Before a descriptor is built: finish an apply that was cut short. A report line, or empty
    static std::string Recover(const std::filesystem::path& descriptorFile);

    static std::filesystem::path JournalFor(const std::filesystem::path& descriptorFile);
};
