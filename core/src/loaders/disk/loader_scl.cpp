#include "loader_scl.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

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
    return writeImage(_filepath);
}

bool LoaderSCL::writeImage(const std::string& path)
{
    bool result = false;

    if (!_diskImage || path.empty())
    {
        return result;
    }

    _warnings.clear();

    // Read TR-DOS catalog to get file list
    DiskImage::Track* track0 = _diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track0)
    {
        _warnings.push_back("SCL save refused: track 0 is missing");
        return result;
    }

    // Track 0 must carry the TR-DOS catalog (sectors 1..8) and the volume sector (9) as 256-byte sectors
    for (uint8_t sectorNo = 0; sectorNo <= TRD_VOLUME_SECTOR; sectorNo++)
    {
        DiskImage::Sector* sector = track0->getSector(sectorNo);
        if (!sector || !sector->hasData || sector->dataSize != TRD_SECTORS_SIZE_BYTES)
        {
            _warnings.push_back(StringHelper::Format("SCL save refused: track 0 sector %d is not a 256-byte TR-DOS sector", sectorNo + 1));
            return result;
        }
    }

    DiskImage::Sector* volumeSector = track0->getSector(TRD_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)volumeSector->data;

    if (volumeInfo->trDOSSignature != TRD_SIGNATURE)
    {
        _warnings.push_back("SCL save refused: no TR-DOS signature in the volume sector");
        return result;
    }

    uint8_t fileCount = volumeInfo->fileCount;   // Includes deleted entries
    if (fileCount > TRD_MAX_FILES)
    {
        _warnings.push_back("SCL save refused: invalid file count in the volume sector");
        return result;
    }

    // Walk the catalog: export non-deleted files only; sectors not owned by an exported file are not stored
    size_t totalDataSize = 0;
    std::vector<TRDOSDirectoryEntry> fileDescriptors;
    fileDescriptors.reserve(fileCount);

    for (uint8_t i = 0; i < fileCount; i++)
    {
        uint8_t sectorNo = i / 16;  // 16 file descriptors per sector
        uint8_t entryNo = i % 16;

        DiskImage::Sector* catalogSector = track0->getSector(sectorNo);
        TRDOSDirectoryEntry* entry = (TRDOSDirectoryEntry*)(catalogSector->data + entryNo * sizeof(TRDOSDirectoryEntry));

        uint8_t marker = static_cast<uint8_t>(entry->Name[0]);
        if (marker == 0x00)
        {
            break;      // End of catalog
        }
        if (marker == 0x01)
        {
            continue;   // Deleted file
        }

        // Validate the sector chain before committing to export this file
        bool chainValid = true;
        uint16_t locator = entry->StartTrack * TRD_SECTORS_PER_TRACK + entry->StartSector;
        for (size_t n = 0; n < entry->SizeInSectors; n++, locator++)
        {
            DiskImage::Track* fileTrack = _diskImage->getTrack(static_cast<uint8_t>(locator / TRD_SECTORS_PER_TRACK));
            DiskImage::Sector* fileSector = fileTrack ? fileTrack->getSector(locator % TRD_SECTORS_PER_TRACK) : nullptr;
            if (!fileSector || !fileSector->hasData || fileSector->dataSize != TRD_SECTORS_SIZE_BYTES)
            {
                chainValid = false;
                break;
            }
        }

        if (!chainValid)
        {
            std::string name(entry->Name, 8);
            _warnings.push_back(StringHelper::Format("SCL save: file '%s' skipped - its sector chain points outside the TR-DOS geometry", name.c_str()));
            continue;
        }

        fileDescriptors.push_back(*entry);
        totalDataSize += entry->SizeInSectors * TRD_SECTORS_SIZE_BYTES;
    }

    uint8_t exportedCount = static_cast<uint8_t>(fileDescriptors.size());

    // Calculate total size: header + descriptors + all file data + CRC
    size_t headerSize = 8 + 1;  // "SINCLAIR" + file count
    size_t descriptorsSize = exportedCount * sizeof(TRDOSDirectoryEntryBase);

    size_t totalSize = headerSize + descriptorsSize + totalDataSize + 4;  // +4 for CRC

    // Allocate buffer for SCL file
    std::vector<uint8_t> buffer(totalSize);
    size_t offset = 0;

    // Write signature "SINCLAIR"
    std::memcpy(buffer.data() + offset, "SINCLAIR", 8);
    offset += 8;

    // Write exported (non-deleted) file count
    buffer[offset++] = exportedCount;

    // Write file descriptors (14 bytes each - without start sector/track)
    for (const auto& desc : fileDescriptors)
    {
        std::memcpy(buffer.data() + offset, &desc, sizeof(TRDOSDirectoryEntryBase));
        offset += sizeof(TRDOSDirectoryEntryBase);
    }

    // Write file data
    for (const auto& desc : fileDescriptors)
    {
        uint16_t fileSectorLocator = desc.StartTrack * TRD_SECTORS_PER_TRACK + desc.StartSector;

        for (size_t i = 0; i < desc.SizeInSectors; i++, fileSectorLocator++)
        {
            uint8_t fileTrackNo = fileSectorLocator / TRD_SECTORS_PER_TRACK;
            uint8_t fileSectorNo = fileSectorLocator % TRD_SECTORS_PER_TRACK;

            DiskImage::Track* fileTrack = _diskImage->getTrack(fileTrackNo);
            if (!fileTrack)
            {
                return result;
            }

            DiskImage::Sector* fileSector = fileTrack->getSector(fileSectorNo);
            if (!fileSector)
            {
                return result;
            }

            std::memcpy(buffer.data() + offset, fileSector->data, TRD_SECTORS_SIZE_BYTES);
            offset += TRD_SECTORS_SIZE_BYTES;
        }
    }

    // Calculate and write CRC (sum of all preceding bytes)
    uint32_t crc = 0;
    for (size_t i = 0; i < offset; i++)
    {
        crc += buffer[i];
    }
    std::memcpy(buffer.data() + offset, &crc, 4);

    // Write to file
    FILE* file = FileHelper::OpenFile(path, "wb");
    if (file)
    {
        if (FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size()))
        {
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
        FileHelper::CloseFile(file);
    }

    return result;
}

void LoaderSCL::setImage(DiskImage* diskImage)
{
    // Note: Does not take ownership - caller must manage diskImage lifetime
    _diskImage = diskImage;
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

#ifdef _DEBUG
    //std::string message = _diskImage->DumpSectorHex(0, 1);
    //std::cout << "BEFORE - Track: 0 Sector: 1 " << std::endl << message << std::endl;
#endif

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
                    if (totalSizeSectors <= TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK)
                    {
                        // Add files to the image one by one
                        for (size_t i = 0; i < header->FileCount; i++)
                        {
                            TRDOSDirectoryEntryBase fileDescriptor = header->Files[i];

                            // Add file to DiskImage and update TR-DOS catalog
                            addFile(&fileDescriptor, currentFileData);

                            // Move pointer to next file data
                            currentFileData += fileDescriptor.SizeInSectors * TRD_SECTORS_SIZE_BYTES;
                        }

                        _diskImage->setLoaded(true);
                        result = true;

#ifdef _DEBUG
                        //std::string messageAfter = _diskImage->DumpSectorHex(0, 1);
                        //std::cout << "AFTER - Track: 0 Sector: 1 " << std::endl << messageAfter << std::endl;
#endif
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
    DiskImage::Sector* systemSector = track->getSector(TRD_VOLUME_SECTOR);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)systemSector->data;

    if (volumeInfo != nullptr && volumeInfo->fileCount < TRD_MAX_FILES)
    {
        /// region <Locate next empty file record in TR-DOS catalog>
        size_t fileLengthSectors = fileDescriptor->SizeInSectors;
        uint16_t catalogOffset = volumeInfo->fileCount * sizeof(TRDOSDirectoryEntry);
        /// endregion </Locate next empty file record in TR-DOS catalog>

        if (volumeInfo->freeSectorCount >= fileLengthSectors)
        {
            /// region <Create new file descriptor>
            uint8_t dirSectorNo = ((catalogOffset / TRD_SECTORS_SIZE_BYTES) & 0x0F);
            DiskImage::Sector* dirSector = track->getSector(dirSectorNo);

            TRDOSDirectoryEntry* dstFileDescriptor = (TRDOSDirectoryEntry*)(dirSector->data + (catalogOffset & 0x00FF));
            memcpy(dstFileDescriptor, fileDescriptor, sizeof(TRDOSDirectoryEntryBase));
            dstFileDescriptor->StartTrack = volumeInfo->firstFreeTrack;
            dstFileDescriptor->StartSector = volumeInfo->firstFreeSector;

            // Update sector data CRC
            dirSector->recalculateDataCRC();
            /// endregion </Create new file descriptor>

            /// region <Recalculate free TR-DOS disk values>
            uint16_t freeSectorLocator = volumeInfo->firstFreeTrack * TRD_SECTORS_PER_TRACK + volumeInfo->firstFreeSector;
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
                uint8_t fileTrackNo = fileSectorLocator / TRD_SECTORS_PER_TRACK;
                uint8_t fileSectorNo = (fileSectorLocator % TRD_SECTORS_PER_TRACK);

                DiskImage::Track* fileTrack = _diskImage->getTrack(fileTrackNo);
                DiskImage::Sector* fileSector = fileTrack->getSector(fileSectorNo);

                uint8_t* srcSectorData = fileData + i * TRD_SECTORS_SIZE_BYTES;
                uint8_t* dstSectorData = fileSector->data;

                // Transfer sector content
                memcpy(dstSectorData, srcSectorData, TRD_SECTORS_SIZE_BYTES);

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

        // Checksum should cover all bytes EXCEPT the last 4 (which contain the CRC itself)
        // This means bytes 0 to (length - 5) inclusive, which is (length - 4) iterations
        for (size_t i = 0; i < length - 4; i++)
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