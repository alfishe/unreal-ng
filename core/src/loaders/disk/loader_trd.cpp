#include "loader_trd.h"

#include "common/filehelper.h"

/// region <Properties>
DiskImage* LoaderTRD::getImage()
{
    return _diskImage;
}

void LoaderTRD::setImage(DiskImage* diskImage)
{
    if (_diskImage)
    {
        delete _diskImage;
    }

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
            uint8_t buffer[fileSize];

            if (FileHelper::ReadFileToBuffer(_filepath, buffer, fileSize))
            {
                size_t cylinders = getTrackNoFromImageSize(fileSize);
                if (cylinders < MAX_CYLINDERS)
                {
                    // Allocate disk image with required characteristics
                    _diskImage = new DiskImage(cylinders, TRD_SIDES);

                    // Perform low level format since .TRD files do not store any low-level information (gaps, clock sync marks ets)
                    format(_diskImage);

                    // Transfer sector data from .TRD to prepared disk image
                    transferSectorData(_diskImage, buffer, fileSize);

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
    bool result = false;

    if (_diskImage)
    {
        FILE* file = FileHelper::OpenFile("test.trd", "wb");

        if (file)
        {
            size_t trackCount = _diskImage->getCylinders() * _diskImage->getSides();
            for (size_t tracks = 0; tracks < trackCount; tracks++)
            {
                DiskImage::Track *track = _diskImage->getTrack(tracks);

                for (size_t sectors = 0; sectors < SECTORS_PER_TRACK; sectors++)
                {
                    uint8_t *sectorData = track->getDataForSector(sectors);
                    FileHelper::SaveBufferToFile(file, sectorData, SECTORS_SIZE_BYTES);
                }
            }

            FileHelper::CloseFile(file);
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
     /// endregion </Sanity checks>

     /// region <Get preferred interleave pattern from config>
     uint8_t interleavePatternIndex = _context->config.trdos_interleave;
     if (interleavePatternIndex >= 3)
     {
         interleavePatternIndex = 1;    // Turbo pattern from TR-DOS 5.04T by default
     }

     /// endregion </Get preferred interleave pattern from config>

     uint8_t cylinders = diskImage->getCylinders();
     uint8_t sides = diskImage->getSides();
     for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
     {
         for (uint8_t side = 0; side < sides; side++)
         {
             /// region <Step 1: position to the track (cylinder+side) within disk image data>
             DiskImage::Track& track = *diskImage->getTrackForCylinderAndSide(cylinder, side);
             /// endregion </Step 1: position to the track (cylinder+side) within disk image data>

             /// region <Step 2: Fully re-initialize low-level formatting by applying default object state>
             track = DiskImage::Track();

             // Apply the interleave sector pattern used during formatting and re-index sector information
             track.applyInterleaveTable(INTERLEAVE_PATTERNS[interleavePatternIndex]);

             /// endregion </Step 2: Fully re-initialize low-level formatting by applying default object state>

             /// region <Step 3: format the track on logical level (put valid ID records to each sector)>
             for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; sector++)
             {
                 uint8_t sectorNumber = INTERLEAVE_PATTERNS[interleavePatternIndex][sector];

                 // Populate sector ID information and recalculate ID CRC
                 DiskImage::AddressMarkRecord& markRecord = *track.getIDForSector(sector);
                 markRecord.cylinder = cylinder;
                 markRecord.head = 0;
                 markRecord.sector = sector + 1;
                 markRecord.sector_len = 0x01;  // Default TR-DOS: 1 - 256 bytes sector
                 markRecord.recalculateCRC();
             }
             /// endregion </Step 3: format the track on logical level (put valid ID records to each sector)>

             result = true;
         }
     }

     // Step 4: write volume information
     populateEmptyVolumeInfo(diskImage);

    return result;
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

void LoaderTRD::populateEmptyVolumeInfo(DiskImage* diskImage)
{
    DiskImage::Track* track = diskImage->getTrack(0);
    DiskImage::RawSectorBytes* sector = track->getSector(TRDOS_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)sector->data;

    volumeInfo->trDOSSignature = TRD_SIGNATURE;
    volumeInfo->diskType = DS_80;
    volumeInfo->freeSectorCount = FREE_SECTORS_ON_EMPTY_DISK;
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