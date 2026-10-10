#pragma once

// Forwards to unreal-disk (unrealdisk/trdos.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/trdos.h"

using unrealdisk::TRD_SIGNATURE;
using unrealdisk::TRD_80_TRACKS;
using unrealdisk::TRD_40_TRACKS;
using unrealdisk::TRD_SECTORS_PER_TRACK;
using unrealdisk::TRD_SECTORS_SIZE_BYTES;
using unrealdisk::TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK;
using unrealdisk::TRD_FREE_SECTORS_ON_DS_40_EMPTY_DISK;
using unrealdisk::TRD_FREE_SECTORS_ON_SS_80_EMPTY_DISK;
using unrealdisk::TRD_FREE_SECTORS_ON_SS_40_EMPTY_DISK;
using unrealdisk::TRD_MAX_FILES;
using unrealdisk::TRD_VOLUME_SECTOR;
using unrealdisk::TRDDiskType;
using unrealdisk::DS_80;
using unrealdisk::DS_40;
using unrealdisk::SS_80;
using unrealdisk::SS_40;
using unrealdisk::getTRDDiskTypeName;
using unrealdisk::getFreeSectorCountForDiskType;
using unrealdisk::TRDVolumeInfo;
using unrealdisk::TRDFile;
using unrealdisk::TRDCatalog;
using unrealdisk::TRDValidationErrorType;
using unrealdisk::DISK_IMAGE_NULL;
using unrealdisk::TRACK_DATA_NULL;
using unrealdisk::SECTOR_DATA_NULL;
using unrealdisk::INVALID_DISK_TYPE;
using unrealdisk::INVALID_FILE_COUNT;
using unrealdisk::INVALID_FREE_SECTORS_COUNT;
using unrealdisk::INVALID_FIRST_FREE_TRACK;
using unrealdisk::INVALID_FIRST_FREE_SECTOR;
using unrealdisk::INVALID_TRDOS_SIGNATURE;
using unrealdisk::INVALID_DELETED_FILE_COUNT;
using unrealdisk::INVALID_FILE_NAME;
using unrealdisk::INVALID_START_TRACK;
using unrealdisk::INVALID_START_SECTOR;
using unrealdisk::TRDValidationRecord;
using unrealdisk::TRDValidationReport;
