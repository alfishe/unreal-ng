#pragma once

/// @file fatvolumereader.h
/// @brief The FAT12 / FAT16 / FAT32 reader of unreal-disk (unrealdisk/fatvolumereader.h)

#include "common/unicodehelper.h"
#include "emulator/io/storage/iblockdevice.h"
#include "unrealdisk/fatvolumereader.h"

using unrealdisk::FatChainExtent;
using unrealdisk::FatDirEntryInfo;
using unrealdisk::FatPartition;
using unrealdisk::FatRawEntry;
using unrealdisk::FatReaderType;
using unrealdisk::FatVolumeReader;
