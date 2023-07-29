#include "loader_scl.h"

#include "common/filehelper.h"

/// region <Basic methods>
bool LoaderSCL::loadImage()
{
    bool result = false;

    DiskImage* diskImage = new DiskImage(80, 2);

    // Probably, format procedure can be extracted to more common class
    LoaderTRD loaderTrd(_context, _filepath);
    result = loaderTrd.format(diskImage);

    if (result)
    {
        result = loadSCL(diskImage);
    }

    return result;
}

bool LoaderSCL::writeImage()
{
    bool result = false;

    return result;
}

DiskImage* LoaderSCL::getImage()
{
    return _diskImage;
}

/// endregion </Basic methods>

/// region <Helper methods>
bool LoaderSCL::loadSCL(DiskImage* diskImage)
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
                SCLHeader* header = (SCLHeader*)buffer;

                if (areUint8ArraysEqual(header->Signature, (uint8_t*)"SINCLAIR", 8))
                {
                    size_t totalSizeSectors = 0;

                    for (size_t i = 0; i < header->FileCount; i++)
                    {
                        totalSizeSectors += header->Files[i].FullSectorsSize;
                    }

                    // Whole track 0 (16 sectors) is used fo TR-DOS catalog info
                    if (totalSizeSectors <= 80 * SECTORS_PER_TRACK * MAX_SIDES - 16)
                    {
                        for (size_t i = 0; i < header->FileCount; i++)
                        {
                            addFile();
                        }
                    }
                }
            }
        }
    }

    return result;
}

bool LoaderSCL::addFile()
{
    bool result = false;

    return result;
}
/// endregion </Helper methods>