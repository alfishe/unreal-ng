#pragma once

/// @file mediachanges.h
/// @brief `media changes`: the guest's unsaved writes on a block medium with
/// session writes as file operations, each with the layer of a composite it
/// touched; per FAT partition on a partitioned composite.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c6-provenance-flatten.md §3,
/// c7-partitions.md §5.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/media/mediatypes.h"

class Medium;

struct MediumChange
{
    std::string op;       ///< create | modify | delete | rename | mkdir | rmdir | attributes
    std::string path;     ///< "/GAMES/ELITE.TRD"; on a partitioned disk "name:/GAMES/ELITE.TRD"
    std::string oldPath;  ///< rename
    std::string layer;    ///< the composite layer's name; empty: new, or not known
    uint64_t sizeBefore = 0;
    uint64_t sizeAfter = 0;
};

struct MediumChanges
{
    std::vector<MediumChange> changes;
    std::vector<std::string> warnings;
    uint64_t changedSectors = 0;
    uint32_t directoriesRead = 0;
    bool fullScan = false;
};

/// The changes of `medium` (its session layer against what is under it). Not supported for media without
/// session writes or that are not disks. The caller keeps the guest from writing meanwhile
MediaResult ListMediumChanges(Medium& medium, MediumChanges& out);
