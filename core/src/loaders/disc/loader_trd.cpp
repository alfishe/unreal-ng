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
                size_t cylinders = getCylindersFromImageSize(fileSize);
                if (cylinders < MAX_CYLINDERS)
                {
                    _diskImage = new DiskImage(cylinders, TRD_SIDES);



                    _diskImage->setRawDiskImageData(buffer, fileSize);

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

 bool LoaderTRD::format(DiskImage* image)
 {
     static constexpr uint8_t const INTERLEAVE_PATTERNS[3][16] =
     {
         { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },     // Default TR-DOS 5.03 - slow on real drives
         { 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16 },     // Default TR-DOS 5.04T - fast on real drives
         { 1, 12, 7, 2, 13, 8, 3, 14, 9, 4, 15, 10, 5, 16, 11, 6 }      // Something in between for slower drives
     };

     bool result = false;

     /// region <Sanity checks>
     if (image == nullptr)
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

     uint8_t cylinders = image->getCylinders();
     uint8_t sides = image->getSides();
     for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
     {
         for (uint8_t side = 0; side < sides; side++)
         {
             // Step 1: position to the track (cylinder+side) within disk image data
             //DiskImage::Track track = DiskImage::Track::seek();

             // Step 2: format the track
             for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; sector++)
             {
                 uint8_t sectorNumber = INTERLEAVE_PATTERNS[interleavePatternIndex][sector];

                 // Populate sector ID information
                 // Byte[0] - cylinder
                 // Byte[1] - side
                 // Byte[2] - sector
                 // Byte[3] - sector size
                 // Byte[4] - crc1
                 // Byte[5] - crc2
             }
         }
     }

    return result;
 }
 /// endregion </Methods>

/// region <Helper methods>
uint8_t LoaderTRD::getCylindersFromImageSize(size_t filesize)
{
    uint8_t result = filesize / TRD_TRACK_SIZE;     // Full cylinders
    result += filesize % (TRD_TRACK_SIZE) ? 1 : 0;  // Partially filled cylinders

    return result;
}
/// endregion </Helper methods>