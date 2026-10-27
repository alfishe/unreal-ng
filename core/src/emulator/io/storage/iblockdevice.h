#pragma once

/// @file iblockdevice.h
/// @brief The emulator's name for unreal-disk's block device: a disk as a numbered list of 512-byte sectors
/// (unrealdisk/blockdevice.h)

#include "unrealdisk/blockdevice.h"

using IBlockDevice = unrealdisk::BlockDevice;
using BlockGeometry = unrealdisk::BlockGeometry;
