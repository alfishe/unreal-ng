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
        _diskImage = diskImage;

        result = loadSCL();
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
bool LoaderSCL::loadSCL()
{
    bool result = false;

    if (FileHelper::FileExists(_filepath))
    {
        size_t fileSize = FileHelper::GetFileSize(_filepath);
        if (fileSize > 0)
        {
            uint8_t* buffer = new uint8_t [fileSize];
            std::fill(buffer, buffer + fileSize, 0xBB);

            // Read whole SCL file to the buffer and check it's CRC
            if (FileHelper::ReadFileToBuffer(_filepath, buffer, fileSize) == fileSize && checkSCLFileCRC(buffer, fileSize))
            {
                SCLHeader* header = (SCLHeader*)buffer;

                // Ensure it is SCL file and signature matches
                if (areUint8ArraysEqual(header->Signature, (uint8_t*)"SINCLAIR", 8))
                {
                    // File data blocks start immediately after all file descriptors
                    uint8_t* dataBlocks = buffer + sizeof(SCLHeader) + header->FileCount * sizeof(TRDOSDirectoryEntryBase);
                    uint8_t* currentFileData = dataBlocks;

                    size_t totalSizeSectors = 0;

                    for (size_t i = 0; i < header->FileCount; i++)
                    {
                        totalSizeSectors += header->Files[i].SizeInSectors;
                    }

                    // Check if all files in SCL image fit into the empty disk
                    if (totalSizeSectors <= FREE_SECTORS_ON_EMPTY_DISK)
                    {
                        // Add files to the image one by one
                        for (size_t i = 0; i < header->FileCount; i++)
                        {
                            TRDOSDirectoryEntryBase fileDescriptor = header->Files[i];

                            // Add file to DiskImage and update TR-DOS catalog
                            addFile(&fileDescriptor, currentFileData);

                            // Move pointer to next file data
                            currentFileData += fileDescriptor.SizeInSectors * SECTORS_SIZE_BYTES;
                        }
                    }
                }
            }

            delete[] buffer;
        }
    }

    return result;
}

bool LoaderSCL::addFile(TRDOSDirectoryEntryBase* fileDescriptor, uint8_t* fileData)
{
    bool result = false;

    DiskImage::Track* track = _diskImage->getTrack(0);
    uint8_t* sector = track->getDataForSector(TRDOS_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)sector;

    if (volumeInfo != nullptr && volumeInfo->numFiles < TRDOS_MAX_FILES)
    {
        /// region <Locate next empty file record in TR-DOS catalog>
        size_t fileLengthSectors  = fileDescriptor->SizeInSectors;
        size_t catalogOffset = volumeInfo->numFiles * sizeof(TRDOSDirectoryEntry);

        uint8_t* catalogSector = track->getDataForSector(1 + catalogOffset / SECTORS_SIZE_BYTES);

        /// endregion </Locate next empty file record in TR-DOS catalog>

        /// region <Create new file descriptor>
        /// endregion </Create new file descriptor>

        /// region <Recalculate free TR-DOS disk values>
        /// endregion </Recalculate free TR-DOS disk values>
    }

    return result;
}

bool LoaderSCL::checkSCLFileCRC(uint8_t* data, size_t length)
{
    bool result = false;

    if (data && length > 4)
    {
        uint8_t* crcData = &data[length - 4];
        uint32_t crc = *((uint32_t*)crcData);
        uint32_t calculatedCRC = 0;

        for (size_t i = 0; i < length - 4 - 1; i++)
        {
            calculatedCRC += data[i];
        }

        if (crc == calculatedCRC)
        {
            result = true;
        }
    }

    return result;
}

/// endregion </Helper methods>