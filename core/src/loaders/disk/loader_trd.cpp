#include "loader_trd.h"

#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

/// region <Properties>
DiskImage* LoaderTRD::getImage()
{
    return _diskImage;
}

void LoaderTRD::setImage(DiskImage* diskImage)
{
    // Note: Does not take ownership - caller must manage diskImage lifetime
    _diskImage = diskImage;
}
/// endregion </Properties>

 /// region <Methods>
bool LoaderTRD::loadImage()
 {
    bool result = false;

    if (FileHelper::FileExists(_filepath))
    {
        size_t fileSize = FileHelper::GetFileSize(_filepath);
        if (fileSize > 0)
        {
            // Allocate buffer for the whole file
            std::vector<uint8_t> buffer(fileSize);

            if (FileHelper::ReadFileToBuffer(_filepath, buffer.data(), fileSize))
            {
                size_t cylinders = getTrackNoFromImageSize(fileSize);
                if (cylinders < MAX_CYLINDERS)
                {
                    // Allocate disk image with required characteristics
                    _diskImage = new DiskImage(cylinders, TRD_SIDES);

                    // Perform low level format since .TRD files do not store any low-level information (gaps, clock sync marks ets)
                    format(_diskImage);

                    // Transfer sector data from .TRD to prepared disk image
                    transferSectorData(_diskImage, buffer.data(), fileSize);

                    // Mark disk image as loaded
                    _diskImage->setLoaded(true);

                    result = true;
                }
            }
        }
    }

    return result;
 }

bool LoaderTRD::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderTRD::writeImage(const std::string& path)
{
    bool result = false;

    if (_diskImage && !path.empty())
    {
        FILE* file = FileHelper::OpenFile(path, "wb");

        if (file)
        {
            size_t trackCount = _diskImage->getCylinders() * _diskImage->getSides();
            for (size_t tracks = 0; tracks < trackCount; tracks++)
            {
                DiskImage::Track *track = _diskImage->getTrack(tracks);

                for (size_t sectors = 0; sectors < TRD_SECTORS_PER_TRACK; sectors++)
                {
                    uint8_t *sectorData = track->getDataForSector(sectors);
                    [[maybe_unused]] bool saveResult = FileHelper::SaveBufferToFile(file, sectorData, TRD_SECTORS_SIZE_BYTES);
                }
            }
            
            FileHelper::CloseFile(file);
            
            // Mark disk as clean after successful save
            _diskImage->markClean();
            
            // Emit notification that disk was saved
            if (_context && _context->pEmulator)
            {
                std::string emulatorId = _context->pEmulator->GetId();
                // Note: We don't know which drive this disk is in from the loader context
                // Use drive 0 as default - the receiver can check all drives if needed
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                messageCenter.Post(NC_FDD_DISK_WRITTEN, 
                    new FDDDiskPayload(emulatorId, 0, path), true);
            }
            
            // Update stored file path
            _diskImage->setFilePath(path);
            
            result = true;
        }
    }

    return result;
}

 bool LoaderTRD::format(DiskImage* diskImage)
 {
     static constexpr uint8_t const INTERLEAVE_PATTERNS[3][16] =
     {
         { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },     // Default TR-DOS 5.03 - slow on real drives
         { 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16 },     // Default TR-DOS 5.04T - fast on real drives
         { 1, 12, 7, 2, 13, 8, 3, 14, 9, 4, 15, 10, 5, 16, 11, 6 }      // Something in between for slower drives
     };

     bool result = false;

     /// region <Sanity checks>
    if (diskImage == nullptr)
    {
        return result;
    }

    uint8_t cylinders = diskImage->getCylinders();
    uint8_t sides = diskImage->getSides();

    // Validate disk type based on cylinders and sides
    TRDDiskType diskType;
    if (cylinders == TRD_80_TRACKS)
    {
        if (sides == 2)
            diskType = DS_80;
        else if (sides == 1)
            diskType = SS_80;
        else
            return false;  // Invalid number of sides
    }
    else if (cylinders == TRD_40_TRACKS)
    {
        if (sides == 2)
            diskType = DS_40;
        else if (sides == 1)
            diskType = SS_40;
        else
            return false;  // Invalid number of sides
    }
    else
    {
        return false;  // Invalid number of tracks
    }

    /// endregion </Sanity checks>

    /// region <Get preferred interleave pattern from config>
    uint8_t interleavePatternIndex = _context->config.trdos_interleave;
    if (interleavePatternIndex >= 3)
    {
        interleavePatternIndex = 1;    // Turbo pattern from TR-DOS 5.04T by default
    }
    /// endregion </Get preferred interleave pattern from config>

    // Initialize all tracks
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            /// region <Step 1: position to the track (cylinder+side) within disk image data>
            DiskImage::Track& track = *diskImage->getTrackForCylinderAndSide(cylinder, side);
            /// endregion </Step 1: position to the track (cylinder+side) within disk image data>

            /// region <Step 2: Fully re-initialize low-level formatting by applying default object state>
            track.reset();

            // Apply the interleaving sector pattern used during formatting and re-index sector information
            track.applyInterleaveTable(INTERLEAVE_PATTERNS[interleavePatternIndex]);

            /// endregion </Step 2: Fully re-initialize low-level formatting by applying default object state>

            /// region <Step 3: format the track on logical level (put valid ID records to each sector)>
            for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; sector++)
            {
                [[maybe_unused]] uint8_t sectorNumber = INTERLEAVE_PATTERNS[interleavePatternIndex][sector];

                // Populate sector ID information and recalculate ID CRC
                DiskImage::AddressMarkRecord& markRecord = *track.getIDForSector(sector);
                markRecord.cylinder = cylinder;
                markRecord.head = side;         // Fix: head should be the current side
                markRecord.sector = sector + 1;
                markRecord.sector_size = 0x01;  // Default TR-DOS: 1 => 256 bytes sector
                markRecord.recalculateCRC();
            }
            /// endregion </Step 3: format the track on logical level (put valid ID records to each sector)>

            result = true;
        }
    }

    // Step 4: write volume information with correct disk type
    populateEmptyVolumeInfo(diskImage, diskType);

    return result;
 }

 bool LoaderTRD::validateTRDOSImage(DiskImage* diskImage)
 {
     TRDValidationReport report;

     return validateTRDOSImage(diskImage, report);
 }

 bool LoaderTRD::validateTRDOSImage(DiskImage* diskImage, TRDValidationReport& report)
 {
     // Check disk image data
     if (!diskImage)
     {
         TRDValidationRecord record =
         {
             .message = "Disk image is not set",
             .type = DISK_IMAGE_NULL
         };
         report.errors.push_back(record);

         report.isValid = false;
         return false;
     }

     // Get track 0
     DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
     if (!track0)
     {
         TRDValidationRecord record =
         {
             .message = "Track 0 data unavailable",
             .type = TRACK_DATA_NULL
         };
         report.errors.push_back(record);

         report.isValid = false;
         return false;
     }

     // Get volume sector (track 0, sector 8)
     uint8_t* volumeSectorData = track0->getDataForSector(TRD_VOLUME_SECTOR);
     if (!volumeSectorData)
     {
         TRDValidationRecord record =
         {
             .message = "Sector 8 (TRD_VOLUME_SECTOR) data unavailable",
             .type = SECTOR_DATA_NULL
         };
         report.errors.push_back(record);

         report.isValid = false;
         return false;
     }

     TRDVolumeInfo* volumeSector = (TRDVolumeInfo*)volumeSectorData;

     // Check for TR-DOS volume sector signature and format
     uint8_t diskType = volumeSector->diskType;

     // Valid disk type
     if (!(diskType == DS_80 || diskType == DS_40 || diskType == SS_80 || diskType == SS_40))
     {
         TRDValidationRecord record =
         {
             .message = StringHelper::Format("Invalid disk type: %d", diskType),
             .type = INVALID_DISK_TYPE
         };
         report.errors.push_back(record);

         report.isValid = false;
     }

     // Check TR-DOS signature
     uint8_t signature = volumeSector->trDOSSignature;
     if (signature != TRD_SIGNATURE)
     {
         TRDValidationRecord record =
         {
             .message = StringHelper::Format("Invalid TR-DOS disk signature: 0x%02X", signature),
             .type = INVALID_TRDOS_SIGNATURE
         };
         report.errors.push_back(record);

         report.isValid = false;
     }

     // Get the number of expected sectors
     [[maybe_unused]] uint16_t expectedFreeSectors = TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK;
     uint8_t expectedTracks = TRD_80_TRACKS;
     switch (volumeSector->diskType)
     {
         case DS_80:
             expectedFreeSectors = TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK;
             expectedTracks = TRD_80_TRACKS;
             break;
         case DS_40:
             expectedFreeSectors = TRD_40_TRACKS * MAX_SIDES - 1;
             expectedTracks = TRD_40_TRACKS;
             break;
         case SS_80:
             expectedFreeSectors = TRD_80_TRACKS - 1;
             expectedTracks = TRD_80_TRACKS;
             break;
         case SS_40:
             expectedFreeSectors = TRD_40_TRACKS - 1;
             expectedTracks = TRD_40_TRACKS;
             break;
         default:
             break;
     }

     // Now check catalog sectors (first 8 sectors excluding volume sector)
     // Each sector can contain 16 file descriptors (16 bytes each), max 128 files

     // We'll validate that file descriptors have valid formats
     // Even empty catalog entries should have a valid format

     const uint8_t maxFileCount = std::min(TRD_MAX_FILES, volumeSector->fileCount);
     uint8_t descriptorsChecked = 0;

     for (uint8_t sectorIdx = 0; sectorIdx < 8 && descriptorsChecked < maxFileCount; sectorIdx++)
     {
         uint8_t* catalogSector = track0->getDataForSector(sectorIdx);
         if (!catalogSector)
             continue;

         // Check each file descriptor in this sector
         for (int entryIdx = 0; entryIdx < 16 && descriptorsChecked < maxFileCount; entryIdx++)
         {
             uint8_t* descriptorData = catalogSector + (entryIdx * sizeof(TRDFile));
             TRDFile* descriptor = (TRDFile*)descriptorData;

             // Filename byte 0 can be any (Bit 7 set for deleted file)
             // Filename (bytes 1-8) should contain printable chars (32-127)
             // or be padded with spaces (32)
             for (int i = 1; i <= 8; i++)
             {
                 uint8_t chr = descriptor->name[i];
                 if (chr != 32 && (chr < 32 || chr > 127))
                 {
                     std::string filename((char*)descriptor->name, 8);
                     TRDValidationRecord record =
                     {
                         .message = StringHelper::Format("Invalid file name: %s at index <%d>", filename, descriptorsChecked),
                         .type = INVALID_FILE_NAME,
                         .track = 0,
                         .sector = sectorIdx,
                         .fileIndex = descriptorsChecked
                     };
                     report.errors.push_back(record);

                     report.isValid = false;
                     break;
                 }
             }

             // Start track should be reasonable
             uint8_t startTrack = descriptor->startTrack;
             if (startTrack >= expectedTracks)
             {
                 std::string filename((char*)descriptor->name, 8);
                 TRDValidationRecord record =
                 {
                     .message = StringHelper::Format("Invalid start track %d for the file: %s at index <%d>", startTrack, filename, descriptorsChecked),
                     .type = INVALID_START_TRACK,
                     .track = 0,
                     .sector = sectorIdx,
                     .fileIndex = descriptorsChecked
                 };
                 report.errors.push_back(record);

                 report.isValid = false;
             }

             // Start sector should be reasonable
             uint16_t startSector = descriptor->startSector;
             if (startSector >= TRD_SECTORS_PER_TRACK)
             {
                 std::string filename((char*)descriptor->name, 8);
                 TRDValidationRecord record =
                 {
                     .message = StringHelper::Format("Invalid start sector %d for the file: %s at index <%d>", startSector, filename, descriptorsChecked),
                     .type = INVALID_START_SECTOR,
                     .track = 0,
                     .sector = sectorIdx,
                     .fileIndex = descriptorsChecked
                 };
                 report.errors.push_back(record);

                 report.isValid = false;
             }

             descriptorsChecked++;
         }
     }

     if (report.errors.size() == 0)
         report.isValid = true;

     return report.isValid;
 }


 bool LoaderTRD::validateEmptyTRDOSImage(DiskImage* diskImage)
 {
     TRDValidationReport report;

     return validateEmptyTRDOSImage(diskImage, report);
 }

 bool LoaderTRD::validateEmptyTRDOSImage(DiskImage* diskImage, TRDValidationReport& report)
 {
     bool result = validateTRDOSImage(diskImage, report);
    if (!result)
    {
        report.isValid = false;
        return false;
    }

    // Get track 0
    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track0)
    {
        TRDValidationRecord record =
        {
            .message = "Track 0 data unavailable",
            .type = TRACK_DATA_NULL
        };
        report.errors.push_back(record);
        report.isValid = false;
        
        return false;
    }

    // Get volume sector (track 0, sector 8)
    uint8_t* volumeSectorData = track0->getDataForSector(TRD_VOLUME_SECTOR);
    if (!volumeSectorData)
    {
        TRDValidationRecord record =
        {
            .message = "Volume sector data unavailable",
            .type = SECTOR_DATA_NULL,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    TRDVolumeInfo* volumeSector = (TRDVolumeInfo*)volumeSectorData;

    // Check files count. Should 0 for empty disk
    uint8_t fileCount = volumeSector->fileCount;
    if (fileCount != 0)
    {
        TRDValidationRecord record =
        {
            .message = "File count is not zero for empty disk",
            .type = INVALID_FILE_COUNT,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    // Check deleted files count
    uint8_t deletedFiles = volumeSector->deletedFileCount;
    if (deletedFiles != 0)
    {
        TRDValidationRecord record =
        {
            .message = "Deleted files count is not zero for empty disk",
            .type = INVALID_DELETED_FILE_COUNT,
            .track = 0,
             .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    // Check free sectors counter (should be reasonable for a disk)
    uint16_t freeSectors = volumeSector->freeSectorCount;

    // Check disk type
    uint8_t diskType = volumeSector->diskType;
    if (diskType != DS_80 && diskType != DS_40 && diskType != SS_80 && diskType != SS_40)
    {
        TRDValidationRecord record =
        {
            .message = "Invalid disk type",
            .type = INVALID_DISK_TYPE,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    uint16_t expectedFreeSectors = getFreeSectorCountForDiskType( volumeSector->diskType );

    if (freeSectors != expectedFreeSectors)
    {
        TRDValidationRecord record =
        {
            .message = "Free sectors count does not match expected value",
            .type = INVALID_FREE_SECTORS_COUNT,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    // Check the first free track and sector
    uint8_t firstFreeTrack = volumeSector->firstFreeTrack;
    if (firstFreeTrack != 1)
    {
        TRDValidationRecord record =
        {
            .message = "First free track is not 1",
            .type = INVALID_FIRST_FREE_TRACK,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    uint8_t firstFreeSector = volumeSector->firstFreeSector;
    if (firstFreeSector != 0)
    {
        TRDValidationRecord record =
        {
            .message = "First free sector is not 0",
            .type = INVALID_FIRST_FREE_SECTOR,
            .track = 0,
            .sector = TRD_VOLUME_SECTOR
        };
        report.errors.push_back(record);
        report.isValid = false;
    }

    if (report.errors.size() == 0)
        report.isValid = true;

     return report.isValid;
 }
 /// endregion </Methods>

/// region <Helper methods>
uint8_t LoaderTRD::getTrackNoFromImageSize(size_t filesize)
{
    uint8_t result = filesize / TRD_FULL_TRACK_SIZE;     // Full cylinders (both sides)
    result += filesize % (TRD_FULL_TRACK_SIZE) ? 1 : 0;  // Partially filled cylinders

    return result;
}

bool LoaderTRD::transferSectorData(DiskImage* diskImage, uint8_t* buffer, size_t fileSize)
{
    bool result = false;
    uint8_t cylinders = getTrackNoFromImageSize(fileSize);
    uint8_t tracks = cylinders * 2;

    /// region <Sanity checks>
    if (diskImage == nullptr || buffer == nullptr || fileSize == 0)
    {
        return result;
    }

    if (cylinders == 0 || cylinders > MAX_CYLINDERS)
    {
        return result;
    }

    /// endregion </Sanity checks>

    for (uint8_t trackNo = 0; trackNo < tracks; trackNo++)
    {
        DiskImage::Track& track = *_diskImage->getTrack(trackNo);

        for (uint8_t sectorNo = 0; sectorNo < TRD_SECTORS_PER_TRACK; sectorNo++)
        {
            size_t offset = trackNo * TRD_TRACK_SIZE + sectorNo * TRD_SECTOR_SIZE;
            uint8_t* srcSector = buffer + offset;

            DiskImage::RawSectorBytes* dstSectorObj = track.getRawSector(sectorNo);
            uint8_t* dstSector = dstSectorObj->data;

            // Transfer sector data
            std::copy(srcSector, srcSector + TRD_SECTOR_SIZE, dstSector);

            // Recalculate CRC for sector data block
            dstSectorObj->recalculateDataCRC();
        }
    }

    return result;
}

void LoaderTRD::populateEmptyVolumeInfo(DiskImage* diskImage, TRDDiskType diskType)
{
    DiskImage::Track* track = diskImage->getTrack(0);
    DiskImage::RawSectorBytes* sector = track->getSector(TRD_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)sector->data;

    uint16_t freeSectorCount = getFreeSectorCountForDiskType(diskType);

    volumeInfo->trDOSSignature = TRD_SIGNATURE;
    volumeInfo->diskType = diskType;
    volumeInfo->freeSectorCount = freeSectorCount;
    volumeInfo->firstFreeTrack = 1;
    volumeInfo->firstFreeSector = 0;
    volumeInfo->deletedFileCount = 0;

    // Similar to: volumeInfo->label = "        ";
    std::fill(std::begin(volumeInfo->label), std::end(volumeInfo->label), 0x20);
    std::fill(std::begin(volumeInfo->reserved2), std::end(volumeInfo->reserved2), 0x20);

    // Update sector data CRC
    sector->recalculateDataCRC();
}

/// endregion </Helper methods>