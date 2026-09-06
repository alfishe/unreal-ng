#include "loader_hobeta.h"

#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "loaders/disk/loader_trd.h"

namespace
{
    inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    inline void putU16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>(v >> 8);
    }

    /// Printable copy of a TR-DOS name for messages
    std::string displayName(const char* name)
    {
        std::string result(name, 8);
        while (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    }
}

/// region <Static helpers>

uint16_t LoaderHobeta::computeChecksum(const uint8_t* header)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < CHECKSUM_OFFSET; i++)
    {
        sum = static_cast<uint16_t>(sum + header[i] * 257 + i);
    }
    return sum;
}

bool LoaderHobeta::parseHeader(const uint8_t* data, size_t len, Header& header, std::vector<std::string>& warnings)
{
    if (!data || len < HEADER_SIZE)
    {
        warnings.push_back(StringHelper::Format("Hobeta: file is %zu bytes, shorter than the 17-byte header", len));
        return false;
    }

    Header parsed;
    std::memcpy(parsed.name, data, NAME_SIZE);
    parsed.type = data[8];
    parsed.start = readU16(data + 9);
    parsed.length = readU16(data + 11);
    parsed.reserved = data[13];
    parsed.sectors = data[14];
    parsed.checksum = readU16(data + CHECKSUM_OFFSET);

    const uint16_t expected = computeChecksum(data);
    if (parsed.checksum != expected)
    {
        warnings.push_back(StringHelper::Format("Hobeta: header checksum mismatch: stored 0x%04X, computed 0x%04X", parsed.checksum, expected));
        return false;
    }

    const size_t expectedSize = HEADER_SIZE + static_cast<size_t>(parsed.sectors) * TRD_SECTORS_SIZE_BYTES;
    if (len != expectedSize)
    {
        warnings.push_back(StringHelper::Format("Hobeta: header declares %d sectors (%zu bytes with header), file is %zu bytes",
                                                parsed.sectors, expectedSize, len));
        return false;
    }

    header = parsed;
    return true;
}

bool LoaderHobeta::detect(const uint8_t* data, size_t len)
{
    Header header;
    std::vector<std::string> warnings;
    return parseHeader(data, len, header, warnings);
}

void LoaderHobeta::serializeHeader(const Header& header, uint8_t* out)
{
    std::memcpy(out, header.name, NAME_SIZE);
    out[8] = header.type;
    putU16(out + 9, header.start);
    putU16(out + 11, header.length);
    out[13] = header.reserved;
    out[14] = header.sectors;
    putU16(out + CHECKSUM_OFFSET, computeChecksum(out));
}

bool LoaderHobeta::hasTrdosSystemTrack(DiskImage* diskImage, std::string* reason)
{
    if (!diskImage)
    {
        if (reason) *reason = "disk image is not set";
        return false;
    }

    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track0)
    {
        if (reason) *reason = "track 0 is missing";
        return false;
    }

    // Catalog (sectors 1..8) and volume sector (9) as 256-byte sectors with data fields
    for (uint8_t sectorNo = 0; sectorNo <= TRD_VOLUME_SECTOR; sectorNo++)
    {
        DiskImage::Sector* sector = track0->getSector(sectorNo);
        if (!sector || !sector->hasData || sector->dataSize != TRD_SECTORS_SIZE_BYTES)
        {
            if (reason) *reason = StringHelper::Format("track 0 sector %d is not a 256-byte TR-DOS sector", sectorNo + 1);
            return false;
        }
    }

    const TRDVolumeInfo* volumeInfo = reinterpret_cast<const TRDVolumeInfo*>(track0->getSector(TRD_VOLUME_SECTOR)->data);
    if (volumeInfo->trDOSSignature != TRD_SIGNATURE)
    {
        if (reason) *reason = "no TR-DOS signature in the volume sector";
        return false;
    }

    return true;
}

DiskImage::Sector* LoaderHobeta::sectorForLocator(DiskImage* diskImage, uint16_t locator)
{
    const uint16_t trackNo = static_cast<uint16_t>(locator / TRD_SECTORS_PER_TRACK);
    const size_t trackCount = static_cast<size_t>(diskImage->getCylinders()) * diskImage->getSides();
    if (trackNo >= trackCount) return nullptr;

    DiskImage::Track* track = diskImage->getTrack(static_cast<uint8_t>(trackNo));
    DiskImage::Sector* sector = track ? track->getSector(static_cast<uint8_t>(locator % TRD_SECTORS_PER_TRACK)) : nullptr;
    if (!sector || !sector->hasData || sector->dataSize != TRD_SECTORS_SIZE_BYTES) return nullptr;

    return sector;
}

std::vector<LoaderHobeta::CatalogEntry> LoaderHobeta::listFiles(DiskImage* diskImage)
{
    std::vector<CatalogEntry> result;

    if (!hasTrdosSystemTrack(diskImage, nullptr)) return result;

    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    const TRDVolumeInfo* volumeInfo = reinterpret_cast<const TRDVolumeInfo*>(track0->getSector(TRD_VOLUME_SECTOR)->data);

    const uint8_t fileCount = volumeInfo->fileCount > TRD_MAX_FILES ? TRD_MAX_FILES : volumeInfo->fileCount;
    for (uint8_t i = 0; i < fileCount; i++)
    {
        DiskImage::Sector* catalogSector = track0->getSector(static_cast<uint8_t>(i / 16));
        const TRDOSDirectoryEntry* entry = reinterpret_cast<const TRDOSDirectoryEntry*>(catalogSector->data + (i % 16) * sizeof(TRDOSDirectoryEntry));

        const uint8_t marker = static_cast<uint8_t>(entry->Name[0]);
        if (marker == 0x00) break;      // End of catalog
        if (marker == 0x01) continue;   // Deleted file

        CatalogEntry item;
        item.index = i;
        item.entry = *entry;
        result.push_back(item);
    }

    return result;
}

bool LoaderHobeta::addFile(DiskImage* diskImage, const TRDOSDirectoryEntryBase& descriptor, const uint8_t* fileData,
                           std::vector<std::string>& warnings, TRDOSDirectoryEntry* placed)
{
    /// region <Validate everything before touching the image>
    std::string reason;
    if (!hasTrdosSystemTrack(diskImage, &reason))
    {
        warnings.push_back("Hobeta inject refused: " + reason);
        return false;
    }

    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    DiskImage::Sector* volumeSector = track0->getSector(TRD_VOLUME_SECTOR);
    const TRDVolumeInfo* volumeInfo = reinterpret_cast<const TRDVolumeInfo*>(volumeSector->data);

    const size_t sectors = descriptor.SizeInSectors;

    if (volumeInfo->fileCount >= TRD_MAX_FILES)
    {
        warnings.push_back(StringHelper::Format("Hobeta inject refused: TR-DOS catalog is full (%d files)", TRD_MAX_FILES));
        return false;
    }

    if (volumeInfo->freeSectorCount < sectors)
    {
        warnings.push_back(StringHelper::Format("Hobeta inject refused: '%s' needs %zu sectors, only %d free",
                                                displayName(descriptor.Name).c_str(), sectors, volumeInfo->freeSectorCount));
        return false;
    }

    const uint16_t firstLocator = static_cast<uint16_t>(volumeInfo->firstFreeTrack * TRD_SECTORS_PER_TRACK + volumeInfo->firstFreeSector);
    const uint32_t endLocator = static_cast<uint32_t>(firstLocator) + sectors;
    if (endLocator > 0xFFFF)
    {
        warnings.push_back("Hobeta inject refused: first free sector in the volume sector is out of range");
        return false;
    }

    for (size_t n = 0; n < sectors; n++)
    {
        const uint16_t locator = static_cast<uint16_t>(firstLocator + n);
        if (!sectorForLocator(diskImage, locator))
        {
            warnings.push_back(StringHelper::Format("Hobeta inject refused: track %d sector %d is not a 256-byte TR-DOS sector",
                                                    locator / TRD_SECTORS_PER_TRACK, locator % TRD_SECTORS_PER_TRACK + 1));
            return false;
        }
    }
    /// endregion </Validate everything before touching the image>

    /// region <Catalog entry>
    const uint8_t entryIndex = volumeInfo->fileCount;
    const uint8_t dirSectorNo = static_cast<uint8_t>(entryIndex / 16);
    DiskImage::Sector* dirSector = track0->getSector(dirSectorNo);

    uint8_t dirData[TRD_SECTORS_SIZE_BYTES];
    std::memcpy(dirData, dirSector->data, TRD_SECTORS_SIZE_BYTES);

    TRDOSDirectoryEntry entry;
    std::memcpy(&entry, &descriptor, sizeof(TRDOSDirectoryEntryBase));
    entry.StartTrack = volumeInfo->firstFreeTrack;
    entry.StartSector = volumeInfo->firstFreeSector;
    std::memcpy(dirData + (entryIndex % 16) * sizeof(TRDOSDirectoryEntry), &entry, sizeof(TRDOSDirectoryEntry));

    track0->writeSectorData(dirSectorNo, dirData, TRD_SECTORS_SIZE_BYTES);
    dirSector->recalculateDataCRC();
    /// endregion </Catalog entry>

    /// region <Volume sector>
    uint8_t volumeData[TRD_SECTORS_SIZE_BYTES];
    std::memcpy(volumeData, volumeSector->data, TRD_SECTORS_SIZE_BYTES);
    TRDVolumeInfo* newVolume = reinterpret_cast<TRDVolumeInfo*>(volumeData);

    newVolume->firstFreeSector = static_cast<uint8_t>(endLocator % TRD_SECTORS_PER_TRACK);
    newVolume->firstFreeTrack = static_cast<uint8_t>(endLocator / TRD_SECTORS_PER_TRACK);
    newVolume->fileCount = static_cast<uint8_t>(entryIndex + 1);
    newVolume->freeSectorCount = static_cast<uint16_t>(newVolume->freeSectorCount - sectors);

    track0->writeSectorData(TRD_VOLUME_SECTOR, volumeData, TRD_SECTORS_SIZE_BYTES);
    volumeSector->recalculateDataCRC();
    /// endregion </Volume sector>

    /// region <File body through the TR-DOS sector chain>
    for (size_t n = 0; n < sectors; n++)
    {
        const uint16_t locator = static_cast<uint16_t>(firstLocator + n);
        DiskImage::Track* fileTrack = diskImage->getTrack(static_cast<uint8_t>(locator / TRD_SECTORS_PER_TRACK));
        const uint8_t fileSectorNo = static_cast<uint8_t>(locator % TRD_SECTORS_PER_TRACK);

        fileTrack->writeSectorData(fileSectorNo, fileData + n * TRD_SECTORS_SIZE_BYTES, TRD_SECTORS_SIZE_BYTES);
        fileTrack->getSector(fileSectorNo)->recalculateDataCRC();
    }
    /// endregion </File body through the TR-DOS sector chain>

    if (placed) *placed = entry;
    return true;
}

bool LoaderHobeta::exportFile(DiskImage* diskImage, const TRDOSDirectoryEntry& entry, uint8_t reserved,
                              std::vector<uint8_t>& out, std::vector<std::string>& warnings)
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("Hobeta export refused: disk image is not set");
        return false;
    }

    const size_t sectors = entry.SizeInSectors;
    const uint16_t firstLocator = static_cast<uint16_t>(entry.StartTrack * TRD_SECTORS_PER_TRACK + entry.StartSector);

    // Validate the chain before writing anything
    for (size_t n = 0; n < sectors; n++)
    {
        const uint32_t locator = static_cast<uint32_t>(firstLocator) + n;
        if (locator > 0xFFFF || !sectorForLocator(diskImage, static_cast<uint16_t>(locator)))
        {
            warnings.push_back(StringHelper::Format("Hobeta export refused: file '%s' sector chain leaves the TR-DOS geometry at track %u sector %u",
                                                    displayName(entry.Name).c_str(), locator / TRD_SECTORS_PER_TRACK, locator % TRD_SECTORS_PER_TRACK + 1));
            return false;
        }
    }

    Header header;
    std::memcpy(header.name, entry.Name, NAME_SIZE);
    header.type = entry.Type;
    header.start = entry.Start;
    header.length = entry.Length;
    header.reserved = reserved;
    header.sectors = entry.SizeInSectors;

    out.resize(HEADER_SIZE + sectors * TRD_SECTORS_SIZE_BYTES);
    serializeHeader(header, out.data());

    uint8_t* dst = out.data() + HEADER_SIZE;
    for (size_t n = 0; n < sectors; n++, dst += TRD_SECTORS_SIZE_BYTES)
    {
        DiskImage::Sector* sector = sectorForLocator(diskImage, static_cast<uint16_t>(firstLocator + n));
        std::memcpy(dst, sector->data, TRD_SECTORS_SIZE_BYTES);
    }

    return true;
}

/// endregion </Static helpers>

/// region <Properties>

void LoaderHobeta::setExportName(const std::string& name)
{
    _exportName = name.substr(0, NAME_SIZE);
    if (!_exportName.empty())
    {
        _exportName.resize(NAME_SIZE, ' ');
    }
}

/// endregion </Properties>

/// region <Basic methods>

bool LoaderHobeta::loadFile()
{
    _fileLoaded = false;
    _fileData.clear();

    if (!FileHelper::FileExists(_filepath))
    {
        _warnings.push_back("File not found: " + _filepath);
        return false;
    }

    const size_t fileSize = FileHelper::GetFileSize(_filepath);
    if (fileSize < HEADER_SIZE)
    {
        _warnings.push_back(StringHelper::Format("Hobeta: %s is %zu bytes, shorter than the 17-byte header", _filepath.c_str(), fileSize));
        return false;
    }

    std::vector<uint8_t> buffer(fileSize);
    if (FileHelper::ReadFileToBuffer(_filepath, buffer.data(), fileSize) != fileSize)
    {
        _warnings.push_back("Unable to read: " + _filepath);
        return false;
    }

    if (!parseHeader(buffer.data(), buffer.size(), _header, _warnings))
    {
        return false;
    }

    _fileData.assign(buffer.begin() + HEADER_SIZE, buffer.end());
    _fileLoaded = true;
    return true;
}

bool LoaderHobeta::loadImage()
{
    _warnings.clear();

    if (!loadFile())
    {
        return false;
    }

    DiskImage* image = new DiskImage(80, 2);

    LoaderTRD loaderTrd(_context, _filepath);
    if (!loaderTrd.format(image))
    {
        _warnings.push_back("Hobeta: unable to format a blank TR-DOS disk");
        delete image;
        return false;
    }

    if (!injectInto(image))
    {
        delete image;
        return false;
    }

    image->markClean();     // A freshly built disk has nothing to save back
    image->setLoaded(true);
    _diskImage = image;
    return true;
}

bool LoaderHobeta::injectInto(DiskImage* diskImage, TRDOSDirectoryEntry* placed)
{
    if (!_fileLoaded)
    {
        _warnings.clear();
        if (!loadFile())
        {
            return false;
        }
    }

    TRDOSDirectoryEntryBase descriptor;
    std::memcpy(descriptor.Name, _header.name, NAME_SIZE);
    descriptor.Type = _header.type;
    descriptor.Start = _header.start;
    descriptor.Length = _header.length;
    descriptor.SizeInSectors = _header.sectors;

    return addFile(diskImage, descriptor, _fileData.data(), _warnings, placed);
}

bool LoaderHobeta::selectExportEntry(CatalogEntry& selected)
{
    std::string reason;
    if (!hasTrdosSystemTrack(_diskImage, &reason))
    {
        _warnings.push_back("Hobeta export refused: " + reason);
        return false;
    }

    std::vector<CatalogEntry> files = listFiles(_diskImage);

    if (_exportIndex >= 0)
    {
        for (const CatalogEntry& item : files)
        {
            if (item.index == _exportIndex)
            {
                selected = item;
                return true;
            }
        }

        _warnings.push_back(StringHelper::Format("Hobeta export refused: catalog slot %d is empty or deleted", _exportIndex));
        return false;
    }

    if (_exportName.empty())
    {
        if (files.empty())
        {
            _warnings.push_back("Hobeta export refused: the disk has no files");
            return false;
        }
        selected = files.front();
        return true;
    }

    for (const CatalogEntry& item : files)
    {
        if (std::memcmp(item.entry.Name, _exportName.data(), NAME_SIZE) == 0)
        {
            selected = item;
            return true;
        }
    }

    _warnings.push_back(StringHelper::Format("Hobeta export refused: file '%s' not found in the catalog", displayName(_exportName.data()).c_str()));
    return false;
}

bool LoaderHobeta::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderHobeta::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("Hobeta export refused: no image or empty path");
        return false;
    }

    CatalogEntry selected;
    if (!selectExportEntry(selected))
    {
        return false;
    }

    // Byte 0x0D comes back verbatim when the exported file is the one this loader read
    uint8_t reserved = 0;
    if (_fileLoaded && std::memcmp(selected.entry.Name, _header.name, NAME_SIZE) == 0 && selected.entry.Type == _header.type)
    {
        reserved = _header.reserved;
    }

    std::vector<uint8_t> buffer;
    if (!exportFile(_diskImage, selected.entry, reserved, buffer, _warnings))
    {
        return false;
    }

    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("Hobeta export failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("Hobeta export failed: cannot write " + path);
        return false;
    }

    // Exporting one file does not save the disk: dirty flags, file path and the disk-written notification are
    // left to the disk image writers (TRD / SCL / UDI)
    return true;
}

/// endregion </Basic methods>
