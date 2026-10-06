#pragma once

/// @file changeattributor.h
/// @brief The guest's writes to a FAT medium as file operations (DT-8): the
/// change layer's sectors say where to look (with a composed layout: the
/// directories they name), the volume is re-read only there, before and
/// after the writes, and the two listings are compared. Creates, modifies,
/// deletes, renames and moves, new and removed directories, attribute
/// changes; each with the layer of the file it touched. Lost clusters are
/// warned about. Nothing is written.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c6-provenance-flatten.md §3.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"

class IComposedLayout;

struct FileChange
{
    enum class Op : uint8_t
    {
        Create,
        Modify,
        Delete,
        Rename,      ///< also a move to another directory: `oldPath` is where it was
        Mkdir,
        Rmdir,
        Attributes,  ///< only the attribute bits changed
    };
    Op op = Op::Create;
    std::string path;     ///< "/GAMES/ELITE.TRD" (the volume's names: long where there is one)
    std::string oldPath;  ///< Rename
    int layer = -1;       ///< the layer of the file before the change; -1: new, or not known
    uint64_t sizeBefore = 0;
    uint64_t sizeAfter = 0;

    static const char* OpName(Op op);
};

struct ChangeSet
{
    std::vector<FileChange> changes;  ///< in path order
    std::vector<std::string> warnings;
    uint64_t changedSectors = 0;
    uint32_t directoriesRead = 0;  ///< directory listings read (before and after): the work done
    bool fullScan = false;         ///< no layout named the directories: every one was compared
};

class ChangeAttributor
{
public:
    /// `before` is the medium without the guest's writes (the change layer's base), `after` with them;
    /// `changes` the change layer's sectors; `layout` the composed layout of `before` when it has one
    /// (else every directory is compared). False with `error` when `before` is not a FAT volume
    static bool Attribute(IBlockDevice& before, IBlockDevice& after,
                          const std::map<uint64_t, std::array<uint8_t, IBlockDevice::kSectorSize>>& changes,
                          const IComposedLayout* layout, ChangeSet& out, std::string* error = nullptr);
};
