#include "unrealdisk/trdosfs.h"

#include <cstring>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
constexpr size_t kSector = TrdosCatalog::SECTOR_SIZE;
constexpr size_t kSectorsPerTrack = TrdosCatalog::SECTORS_PER_TRACK;
constexpr size_t kInfoSector = 8;
constexpr size_t kInfoDeletedCount = 0xF4;   // the disk info sector's count of deleted files

std::string Padded(const std::string& name)
{
    std::string out = name.substr(0, 8);
    out.resize(8, ' ');
    return out;
}

const uint8_t* SectorData(DiskImage& image, size_t logical)
{
    DiskImage::Track* track = image.getTrack(static_cast<uint8_t>(logical / kSectorsPerTrack));
    return track ? track->getDataForSector(static_cast<uint8_t>(logical % kSectorsPerTrack)) : nullptr;
}

bool WriteSector(DiskImage& image, size_t logical, const uint8_t* data)
{
    DiskImage::Track* track = image.getTrack(static_cast<uint8_t>(logical / kSectorsPerTrack));
    if (!track || !track->getDataForSector(static_cast<uint8_t>(logical % kSectorsPerTrack)))
        return false;
    track->writeSectorData(static_cast<uint8_t>(logical % kSectorsPerTrack), data, kSector);
    return true;
}

std::string Describe(const std::string& name, char type)
{
    return name + (type ? std::string(".") + type : std::string());
}
}  // namespace

bool TrdosFileSystem::IsTrdos() const
{
    return TrdosCatalog::IsTrdos(_image);
}

bool TrdosFileSystem::List(std::vector<TrdosFile>& out, std::string& error) const
{
    out.clear();
    TrdosCatalog catalog;
    if (!catalog.Parse(_image))
    {
        error = "the disk has no TR-DOS file system";
        return false;
    }
    for (const TrdosFile& f : catalog.Files())
        if (!f.deleted)
            out.push_back(f);
    return true;
}

bool TrdosFileSystem::Find(const std::string& name, char type, TrdosFile& out, std::string& error) const
{
    std::vector<TrdosFile> files;
    if (!List(files, error))
        return false;
    std::string wanted = name;
    while (!wanted.empty() && wanted.back() == ' ')
        wanted.pop_back();
    for (auto it = files.rbegin(); it != files.rend(); ++it)
        if (it->TrimmedName() == wanted && (type == 0 || it->type == type))
        {
            out = *it;
            return true;
        }
    error = "no file " + Describe(wanted, type) + " on the disk";
    return false;
}

bool TrdosFileSystem::Read(const TrdosFile& file, std::vector<uint8_t>& data, std::vector<uint8_t>* tail, std::string& error) const
{
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(file.sectors) * kSector);
    const size_t first = static_cast<size_t>(file.firstTrack) * kSectorsPerTrack + file.firstSector;
    for (size_t i = 0; i < file.sectors; ++i)
    {
        const uint8_t* sector = SectorData(_image, first + i);
        if (!sector)
        {
            error = "file " + file.TrimmedName() + " runs past the disk's last track";
            return false;
        }
        bytes.insert(bytes.end(), sector, sector + kSector);
    }
    // A length beyond its sectors is kept as far as the sectors go (XAS writes 0 or more than it holds)
    const size_t length = std::min<size_t>(file.length, bytes.size());
    data.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    if (tail)
        tail->assign(bytes.begin() + static_cast<std::ptrdiff_t>(length), bytes.end());
    return true;
}

bool TrdosFileSystem::Delete(const std::string& name, char type, std::string& error)
{
    TrdosCatalog catalog;
    if (!catalog.Parse(_image))
    {
        error = "the disk has no TR-DOS file system";
        return false;
    }
    DiskImage::Track* track0 = _image.getTrack(0);
    uint8_t info[kSector];
    std::memcpy(info, track0->getDataForSector(kInfoSector), kSector);
    const std::string padded = Padded(name);
    bool found = false;
    for (const TrdosFile& f : catalog.Files())
        if (!f.deleted && f.name == padded && (type == 0 || f.type == type))
        {
            uint8_t dir[kSector];
            std::memcpy(dir, track0->getDataForSector(static_cast<uint8_t>(f.slot / 16)), kSector);
            dir[(f.slot % 16) * 16] = 0x01;
            track0->writeSectorData(static_cast<uint8_t>(f.slot / 16), dir, kSector);
            ++info[kInfoDeletedCount];
            found = true;
        }
    if (found)
        track0->writeSectorData(kInfoSector, info, kSector);
    else
        error = "no file " + Describe(name, type) + " on the disk";
    return found;
}

bool TrdosFileSystem::Write(const std::string& name, char type, const std::vector<uint8_t>& bytes, const WriteOptions& options,
                            std::string& error)
{
    if (type == 0)
    {
        error = "a TR-DOS file needs its type letter";
        return false;
    }
    if (name.empty() || name.size() > 8)
    {
        error = "a TR-DOS name has 1 to 8 characters: '" + name + "'";
        return false;
    }
    if (bytes.size() > 0xFF * kSector)
    {
        error = "a TR-DOS file holds at most 255 sectors";
        return false;
    }
    TrdosCatalog catalog;
    if (!catalog.Parse(_image))
    {
        error = "the disk has no TR-DOS file system";
        return false;
    }
    const size_t sectors = (bytes.size() + kSector - 1) / kSector;
    if (catalog.FreeSectors() < sectors)
    {
        error = "the disk has " + std::to_string(catalog.FreeSectors()) + " free sectors, the file needs " + std::to_string(sectors);
        return false;
    }
    if (catalog.UsedSlots() >= TrdosCatalog::MAX_FILES)
    {
        error = "the catalog is full (128 entries)";
        return false;
    }
    DiskImage::Track* track0 = _image.getTrack(0);
    uint8_t info[kSector];
    std::memcpy(info, track0->getDataForSector(kInfoSector), kSector);
    const size_t first = info[TrdosCatalog::INFO_FIRST_FREE_SECTOR] + kSectorsPerTrack * info[TrdosCatalog::INFO_FIRST_FREE_TRACK];
    for (size_t i = 0; i < sectors; ++i)
        if (!SectorData(_image, first + i))
        {
            error = "the disk's free space runs past its last track";
            return false;
        }

    // An older live file of this name and type: deleted as TR-DOS deletes it
    const std::string padded = Padded(name);
    for (const TrdosFile& f : catalog.Files())
        if (!f.deleted && f.name == padded && f.type == type)
        {
            uint8_t dir[kSector];
            std::memcpy(dir, track0->getDataForSector(static_cast<uint8_t>(f.slot / 16)), kSector);
            dir[(f.slot % 16) * 16] = 0x01;
            track0->writeSectorData(static_cast<uint8_t>(f.slot / 16), dir, kSector);
            ++info[kInfoDeletedCount];
        }

    // The data, the catalog entry, the disk info
    std::vector<uint8_t> body(bytes);
    body.resize(sectors * kSector, 0);
    for (size_t i = 0; i < sectors; ++i)
        WriteSector(_image, first + i, body.data() + i * kSector);
    const size_t slot = catalog.UsedSlots();
    uint8_t dir[kSector];
    std::memcpy(dir, track0->getDataForSector(static_cast<uint8_t>(slot / 16)), kSector);
    uint8_t* entry = dir + (slot % 16) * 16;
    std::memcpy(entry, padded.data(), 8);
    const uint16_t length = options.lengthField ? static_cast<uint16_t>(std::min<size_t>(bytes.size(), 0xFFFF)) : 0;
    entry[8] = static_cast<uint8_t>(type);
    entry[9] = static_cast<uint8_t>(options.start & 0xFF);
    entry[10] = static_cast<uint8_t>(options.start >> 8);
    entry[11] = static_cast<uint8_t>(length & 0xFF);
    entry[12] = static_cast<uint8_t>(length >> 8);
    entry[13] = static_cast<uint8_t>(sectors);
    entry[14] = info[TrdosCatalog::INFO_FIRST_FREE_SECTOR];
    entry[15] = info[TrdosCatalog::INFO_FIRST_FREE_TRACK];
    track0->writeSectorData(static_cast<uint8_t>(slot / 16), dir, kSector);
    const size_t next = first + sectors;
    info[TrdosCatalog::INFO_FIRST_FREE_SECTOR] = static_cast<uint8_t>(next % kSectorsPerTrack);
    info[TrdosCatalog::INFO_FIRST_FREE_TRACK] = static_cast<uint8_t>(next / kSectorsPerTrack);
    info[TrdosCatalog::INFO_FILE_COUNT] = static_cast<uint8_t>(info[TrdosCatalog::INFO_FILE_COUNT] + 1);
    const uint16_t free = static_cast<uint16_t>(catalog.FreeSectors() - sectors);
    info[TrdosCatalog::INFO_FREE_SECTORS] = static_cast<uint8_t>(free & 0xFF);
    info[TrdosCatalog::INFO_FREE_SECTORS + 1] = static_cast<uint8_t>(free >> 8);
    track0->writeSectorData(kInfoSector, info, kSector);
    return true;
}
}  // namespace unrealdisk
