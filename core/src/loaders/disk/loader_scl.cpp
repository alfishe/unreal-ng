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

        // TODO: remove debug
//        if (result)
//        {
//            loaderTrd.setImage(diskImage);
//            loaderTrd.writeImage();
//        }
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

                        _diskImage->setLoaded(true);
                        result = true;
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
    DiskImage::RawSectorBytes* systemSector = track->getSector(TRDOS_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)systemSector->data;

    if (volumeInfo != nullptr && volumeInfo->fileCount < TRDOS_MAX_FILES)
    {
        /// region <Locate next empty file record in TR-DOS catalog>
        size_t fileLengthSectors = fileDescriptor->SizeInSectors;
        uint16_t catalogOffset = volumeInfo->fileCount * sizeof(TRDOSDirectoryEntry);
        /// endregion </Locate next empty file record in TR-DOS catalog>

        if (volumeInfo->freeSectorCount >= fileLengthSectors)
        {
            /// region <Create new file descriptor>
            uint8_t dirSectorNo = ((catalogOffset / SECTORS_SIZE_BYTES) & 0x0F) + 1;
            DiskImage::RawSectorBytes* dirSector = track->getRawSector(dirSectorNo);

            TRDOSDirectoryEntry* dstFileDescriptor = (TRDOSDirectoryEntry*)(dirSector->data + (catalogOffset & 0x00FF));
            memcpy(dstFileDescriptor, fileDescriptor, sizeof(TRDOSDirectoryEntryBase));
            dstFileDescriptor->StartTrack = volumeInfo->firstFreeTrack;
            dstFileDescriptor->StartSector = volumeInfo->firstFreeSector;

            // Update sector data CRC
            dirSector->recalculateDataCRC();
            /// endregion </Create new file descriptor>

            /// region <Recalculate free TR-DOS disk values>
            uint16_t freeSectorLocator = volumeInfo->firstFreeTrack * SECTORS_PER_TRACK + volumeInfo->firstFreeSector;
            uint16_t newFreeSectorLocator = freeSectorLocator + fileLengthSectors;

            volumeInfo->firstFreeSector = newFreeSectorLocator & 0x0F;
            volumeInfo->firstFreeTrack = newFreeSectorLocator >> 4;
            volumeInfo->fileCount++;
            volumeInfo->freeSectorCount -= fileLengthSectors;

            // Update sector CRC
            systemSector->recalculateDataCRC();
            /// endregion </Recalculate free TR-DOS disk values>

            /// region <Write file content - sector by sector>
            uint16_t fileSectorLocator = freeSectorLocator;

            for (size_t i = 0; i < fileLengthSectors; i++, fileSectorLocator++)
            {
                uint8_t fileTrackNo = fileSectorLocator / SECTORS_PER_TRACK;
                uint8_t fileSectorNo = (fileSectorLocator % SECTORS_PER_TRACK);

                DiskImage::Track* fileTrack = _diskImage->getTrack(fileTrackNo);
                DiskImage::RawSectorBytes* fileSector = fileTrack->getSector(fileSectorNo);

                uint8_t* srcSectorData = fileData + i * SECTORS_SIZE_BYTES;
                uint8_t* dstSectorData = fileSector->data;

                // Transfer sector content
                memcpy(dstSectorData, srcSectorData,SECTORS_SIZE_BYTES);

                // Update sector CRC
                fileSector->recalculateDataCRC();
            }
            /// endregion </Write file content - sector by sector>

            result = true;
        }
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