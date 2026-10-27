#pragma once

/// @file diskfs.h
/// @brief The files on a slot's disk through unreal-disk's DiskFs (unrealdisk/diskfs.h): TR-DOS, +3DOS, iS-DOS /
/// TASiS, FAT on floppies, hard disks and cards. Design: docs/emulator/design/disk-file-systems.md.

#include <memory>

#include "unrealdisk/diskfs.h"

class Medium;

using unrealdisk::DiskFs;
using unrealdisk::DiskFsEntry;
using unrealdisk::DiskFsVolume;
using unrealdisk::DiskFsWriteOptions;

/// A slot's medium: a floppy, or a block medium read under the time travel read tap (nothing is journalled). Null for
/// tapes, CDs and empty slots
std::unique_ptr<DiskFs> OpenDiskFs(Medium& medium);
