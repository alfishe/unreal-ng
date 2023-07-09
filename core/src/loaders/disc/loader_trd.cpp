#include "loader_trd.h"

#include "common/filehelper.h"

 /// region <Methods>
bool LoaderTRD::loadImage()
 {
    bool result = false;

    if (FileHelper::FileExists(_filepath))
    {
        size_t fileSize = FileHelper::GetFileSize(_filepath);
        if (fileSize > 0)
        {
            uint8_t* buffer = new uint8_t[fileSize];
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

    return result;
}

DiskImage* LoaderTRD::getImage()
{
    return _diskImage;
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

             // Apply interleave sector pattern used during formatting and re-index sector information
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
                 markRecord.sector = sectorNumber;
                 markRecord.sector_len = 0x01;  // Default TR-DOS 1 - 256 bytes sector
                 markRecord.recalculateCRC();
             }
             /// endregion </Step 3: format the track on logical level (put valid ID records to each sector)>
         }
     }

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

            DiskImage::RawSectorBytes* dstSectorObj = track.getRawSectorBytes(sectorNo);
            uint8_t* dstSector = dstSectorObj->data;

            // Transfer sector data
            std::copy(srcSector, srcSector + TRD_SECTOR_SIZE, dstSector);

            // Recalculate CRC for sector data block
            dstSectorObj->recalculateDataCRC();
        }
    }

    return result;
}

/// endregion </Helper methods>