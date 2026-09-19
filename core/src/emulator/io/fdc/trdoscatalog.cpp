#include "trdoscatalog.h"

#include "emulator/io/fdc/diskimage.h"

std::string TrdosFile::TrimmedName() const
{
    size_t end = name.size();
    while (end > 0 && name[end - 1] == ' ')
        end--;
    return name.substr(0, end);
}

bool TrdosCatalog::IsTrdos(DiskImage& image)
{
    DiskImage::Track* track0 = image.getTrack(0);
    if (track0 == nullptr)
        return false;

    const uint8_t* info = track0->getDataForSector(8);  // Disk info, sector 9
    if (info == nullptr)
        return false;

    if (info[0] != 0x00 || info[INFO_TRDOS_ID] != 0x10)
        return false;

    const uint8_t diskType = info[INFO_DISK_TYPE];
    if (diskType < 0x16 || diskType > 0x19)
        return false;

    // Catalog sectors must all be present
    for (uint8_t s = 0; s < 8; s++)
    {
        if (track0->getDataForSector(s) == nullptr)
            return false;
    }

    return true;
}

bool TrdosCatalog::Parse(DiskImage& image)
{
    _files.clear();
    _freeSectors = 0;
    _fileCount = 0;

    if (!IsTrdos(image))
        return false;

    DiskImage::Track* track0 = image.getTrack(0);
    const uint8_t* info = track0->getDataForSector(8);
    _freeSectors = static_cast<uint16_t>(info[INFO_FREE_SECTORS] | (info[INFO_FREE_SECTORS + 1] << 8));
    _fileCount = info[INFO_FILE_COUNT];

    for (size_t slot = 0; slot < MAX_FILES; slot++)
    {
        const uint8_t* entry = track0->getDataForSector(static_cast<uint8_t>(slot / 16)) + (slot % 16) * 16;

        if (entry[0] == 0x00)  // End of catalog
            break;

        TrdosFile file;
        file.slot = static_cast<int>(slot);
        file.deleted = entry[0] == 0x01;
        file.name.assign(reinterpret_cast<const char*>(entry), 8);
        file.type = static_cast<char>(entry[8]);
        file.start = static_cast<uint16_t>(entry[9] | (entry[10] << 8));
        file.length = static_cast<uint16_t>(entry[11] | (entry[12] << 8));
        file.sectors = entry[13];
        file.firstSector = entry[14];
        file.firstTrack = entry[15];
        _files.push_back(file);
    }

    return true;
}

std::vector<const TrdosFile*> TrdosCatalog::BasicFiles() const
{
    std::vector<const TrdosFile*> result;
    for (const TrdosFile& file : _files)
    {
        if (!file.deleted && file.type == 'B')
            result.push_back(&file);
    }
    return result;
}

const TrdosFile* TrdosCatalog::FindBoot() const
{
    for (const TrdosFile& file : _files)
    {
        if (!file.deleted && file.type == 'B' && file.name == "boot    ")
            return &file;
    }
    return nullptr;
}
