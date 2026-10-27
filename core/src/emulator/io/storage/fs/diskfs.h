#pragma once

/// @file diskfs.h
/// @brief The files on a slot's disk through unreal-disk's DiskFs (unrealdisk/diskfs.h): TR-DOS, +3DOS, iS-DOS /
/// TASiS, ATM CP/M, MB-02, FAT on floppies, hard disks and cards. Design: docs/emulator/design/disk-file-systems.md.

#include <memory>

#include "emulator/io/storage/iblockdevice.h"
#include "unrealdisk/diskfs.h"

class Medium;

using unrealdisk::DiskFs;
using unrealdisk::DiskFsEntry;
using unrealdisk::DiskFsVolume;
using unrealdisk::DiskFsWriteOptions;

/// A slot's medium: a floppy, or a block medium read under the time travel read tap (nothing is journalled). Null for
/// tapes, CDs and empty slots
std::unique_ptr<DiskFs> OpenDiskFs(Medium& medium);

/// A block medium as a tool edits it (the media verbs write, delete, rename): reads under the time travel read tap
/// (nothing journalled), writes through it (the media manager's undo log keeps every sector as it was, the access
/// layer below takes the change as it takes a guest's). Null for a medium that is no block medium
std::unique_ptr<IBlockDevice> ToolEditDevice(Medium& medium);
