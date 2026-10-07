#pragma once

/// @file hostfolderfat.h
/// @brief A host folder presented as a FAT16 / FAT32 disk, sector by sector:
/// a FatSynthVolume with one layer, the folder.
///
/// Built from a FolderSnapshot. Nothing is copied: the volume's layout,
/// boot records, FAT and directories are FatSynthVolume's (see there), and
/// file data is read from the host files when the guest reads it. The volume
/// is byte-identical to the one this class built before the multi-source
/// refactor (HostFolderFatParity_Test). Guest writes go to the change layer
/// on top (SessionWriteMap).
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §6,
/// docs/inprogress/2026-10-05-media-multisource/tdd.md §5.

#include <memory>
#include <string>
#include <vector>

#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"

class HostFolderFat : public FatSynthVolume
{
public:
    /// Build the volume. Nullptr with `error` when it cannot be built (the
    /// folder does not fit the FAT type); `report` lists names that could not
    /// be stored
    static std::unique_ptr<HostFolderFat> Build(const FolderSnapshot& snapshot, const FatVolumeOptions& options,
                                                std::string* error, std::vector<std::string>* report);

private:
    HostFolderFat() = default;
};
