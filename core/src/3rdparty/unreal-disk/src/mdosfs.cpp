#include "unrealdisk/mdosfs.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/loaders/loader_mdos.h"

namespace unrealdisk
{
namespace
{
constexpr uint32_t kFatFirst = 1, kFatSectors = 5, kDirFirst = 6, kDirSectors = 8, kDataFirst = 14;
constexpr uint32_t kPerFatSector = 341;
constexpr uint16_t kFree = 0x000, kReserved = 0xDDD, kEmpty = 0xC00, kLast = 0xE00;
constexpr uint8_t kDeleted = 0xE5;
constexpr uint8_t kDeleteAllowed = 0x01;  // D of HSPARWED

/// `size` bytes at `p`, trailing zeroes cut (and blanks when `blanks`)
std::string Trimmed(const uint8_t* p, size_t size, bool blanks)
{
    std::string text(reinterpret_cast<const char*>(p), size);
    while (!text.empty() && (text.back() == 0 || (blanks && text.back() == ' ')))
        text.pop_back();
    return text;
}
}  // namespace

MdosFileSystem MdosFileSystem::OnDisk(DiskImage& image)
{
    struct Geometry
    {
        uint8_t sides = 0, sectors = 0;
    };
    auto geometry = std::make_shared<Geometry>();
    DiskImage* disk = &image;
    // The track and the sector id of `lsn` (the geometry from the boot sector the first time)
    auto locate = [disk, geometry](uint32_t lsn, DiskImage::Track*& track, uint8_t& id) -> DiskImage::Sector* {
        auto find = [disk](uint32_t cylinder, uint8_t side, uint8_t sectorId, DiskImage::Track*& t) {
            t = cylinder < disk->getCylinders() && side < disk->getSides()
                    ? disk->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), side)
                    : nullptr;
            DiskImage::Sector* s = t ? t->findSector(sectorId) : nullptr;
            return s && s->hasData && s->dataSize == kSector ? s : nullptr;
        };
        if (geometry->sectors == 0)
        {
            const DiskImage::Sector* boot = find(0, 0, 1, track);
            uint8_t cylinders = 0;
            if (!boot ||
                !LoaderMDOS::bootGeometry(boot->data, cylinders, geometry->sides, geometry->sectors, kMaxCylinders))
                return nullptr;
        }
        const uint32_t t = lsn / geometry->sectors;
        id = static_cast<uint8_t>(lsn % geometry->sectors + 1);
        return find(t / geometry->sides, static_cast<uint8_t>(t % geometry->sides), id, track);
    };
    MdosFileSystem fs([locate](uint32_t lsn, uint8_t* dst) {
        DiskImage::Track* track = nullptr;
        uint8_t id = 0;
        const DiskImage::Sector* s = locate(lsn, track, id);
        if (!s)
            return false;
        std::memcpy(dst, s->data, kSector);
        return true;
    });
    // A write changes the sector in the image (its CRC and the track's dirty flag follow)
    fs.SetWriter([locate](uint32_t lsn, const uint8_t* src) {
        DiskImage::Track* track = nullptr;
        uint8_t id = 0;
        if (!locate(lsn, track, id))
            return false;
        track->writeSectorData(static_cast<uint8_t>(id - 1), src, kSector);
        return true;
    });
    return fs;
}

std::vector<Mdos3Slot> MdosFileSystem::Slots(const std::function<bool(uint64_t, uint8_t*)>& read, uint64_t sectors)
{
    std::vector<Mdos3Slot> out;
    uint8_t info[kSector];
    for (uint64_t k = 0; (k + 1) * kSlotSectors <= sectors; k++)
    {
        if (!read(k * kSlotSectors, info) || std::memcmp(info, "SDOS", 4) != 0)
            continue;
        Mdos3Slot slot;
        slot.index = static_cast<uint32_t>(k);
        slot.firstSector = k * kSlotSectors + 1;
        slot.name = Trimmed(info + 4, 32, true);
        slot.writeProtected = info[36] == '1';
        out.push_back(std::move(slot));
    }
    return out;
}

bool MdosFileSystem::Open(std::string* why)
{
    auto fail = [why](const std::string& text) {
        if (why)
            *why = text;
        return false;
    };
    uint8_t s[kSector];
    if (!_reader(0, s))
        return fail("no 512-byte boot sector");
    _boot = {};
    if (!LoaderMDOS::bootGeometry(s, _boot.cylinders, _boot.sides, _boot.sectors, kMaxCylinders))
        return fail("no MDOS boot sector");
    if (_boot.Sectors() < kDataFirst)
        return fail("the disk is smaller than its FAT and directory");
    _boot.name = Trimmed(s + 192, 10, false);
    _fat.assign(static_cast<size_t>(kFatSectors) * kSector, 0);
    for (uint32_t k = 0; k < kFatSectors; k++)
        if (!_reader(kFatFirst + k, _fat.data() + static_cast<size_t>(k) * kSector))
            return fail("FAT sector " + std::to_string(k + 1) + " does not read");
    for (uint32_t lsn = 0; lsn < kDataFirst; lsn++)
        if (Fat(lsn) != kReserved)
            return fail("the FAT does not reserve the boot sector, the FAT and the directory");
    return true;
}

uint16_t MdosFileSystem::Fat(uint32_t lsn) const
{
    const uint32_t sector = lsn / kPerFatSector, i = lsn % kPerFatSector;
    if (sector >= kFatSectors || _fat.empty())
        return kReserved;
    const uint8_t* b = _fat.data() + static_cast<size_t>(sector) * kSector + (i / 2) * 3;
    return i % 2 == 0 ? static_cast<uint16_t>(b[0] | (b[1] >> 4) << 8)
                      : static_cast<uint16_t>((b[1] & 0x0F) << 8 | b[2]);
}

bool MdosFileSystem::List(std::vector<MdosFile>& out, std::string& error) const
{
    out.clear();
    uint8_t s[kSector];
    for (uint32_t k = 0; k < kDirSectors; k++)
    {
        if (!_reader(kDirFirst + k, s))
        {
            error = "directory sector " + std::to_string(k + 1) + " does not read";
            return false;
        }
        for (uint32_t i = 0; i < kSector / 32; i++)
        {
            const uint8_t* e = s + i * 32;
            if (e[0] == 0x00 || e[0] == 0xE5)
                continue;
            MdosFile f;
            f.slot = k * (kSector / 32) + i;
            f.type = static_cast<char>(e[0]);
            f.name = Trimmed(e + 1, 10, false);
            f.length = static_cast<uint32_t>(e[11] | e[12] << 8 | e[21] << 16);
            f.start = static_cast<uint16_t>(e[13] | e[14] << 8);
            f.param2 = static_cast<uint16_t>(e[15] | e[16] << 8);
            f.first = static_cast<uint16_t>(e[17] | e[18] << 8);
            f.extra = e[19];
            f.attributes = e[20];
            out.push_back(std::move(f));
        }
    }
    return true;
}

bool MdosFileSystem::Read(const MdosFile& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    if (file.length == 0)
        return true;
    const uint32_t total = _boot.Sectors();
    const std::string from = "the FAT chain of " + file.name + " from sector " + std::to_string(file.first);
    uint32_t lsn = file.first;
    data.reserve(file.length);
    uint8_t s[kSector];
    while (true)
    {
        if (lsn < kDataFirst || lsn >= total || data.size() >= file.length)
        {
            error = from + " leaves the disk or loops";
            return false;
        }
        if (!_reader(lsn, s))
        {
            error = "sector " + std::to_string(lsn) + " does not read";
            return false;
        }
        const uint16_t next = Fat(lsn);
        if (next >= kLast && next < kLast + kSector)
        {
            const uint32_t used = next == kLast ? kSector : next - kLast;
            data.insert(data.end(), s, s + used);
            break;
        }
        if (next == kFree || next >= kEmpty)
        {
            error = from + " meets a free, reserved or bad sector";
            return false;
        }
        data.insert(data.end(), s, s + kSector);
        lsn = next;
    }
    if (data.size() != file.length)
    {
        error =
            from + " holds " + std::to_string(data.size()) + " of the file's " + std::to_string(file.length) + " bytes";
        return false;
    }
    return true;
}

uint32_t MdosFileSystem::FreeSectors() const
{
    uint32_t free = 0;
    for (uint32_t lsn = kDataFirst; lsn < _boot.Sectors(); lsn++)
        if (Fat(lsn) == kFree)
            free++;
    return free;
}

bool MdosFileSystem::Writable(std::string& error) const
{
    if (_writer && !_fat.empty())
        return true;
    error = "the MDOS volume is read only";
    return false;
}

void MdosFileSystem::SetFat(uint32_t lsn, uint16_t value)
{
    const uint32_t sector = lsn / kPerFatSector, i = lsn % kPerFatSector;
    if (sector >= kFatSectors)
        return;
    uint8_t* b = _fat.data() + static_cast<size_t>(sector) * kSector + (i / 2) * 3;
    if (i % 2 == 0)
    {
        b[0] = static_cast<uint8_t>(value);
        b[1] = static_cast<uint8_t>((b[1] & 0x0F) | (value >> 8) << 4);
    }
    else
    {
        b[1] = static_cast<uint8_t>((b[1] & 0xF0) | (value >> 8));
        b[2] = static_cast<uint8_t>(value);
    }
}

void MdosFileSystem::FreeChain(uint32_t first)
{
    // Up to the last sector's word (#E00 + n), a loop cut after as many steps as the disk has sectors
    for (uint32_t lsn = first, steps = 0; lsn >= kDataFirst && lsn < _boot.Sectors() && steps < _boot.Sectors();
         steps++)
    {
        const uint16_t next = Fat(lsn);
        SetFat(lsn, kFree);
        lsn = next;
    }
}

bool MdosFileSystem::WriteFat(std::string& error)
{
    for (uint32_t k = 0; k < kFatSectors; k++)
        if (!_writer(kFatFirst + k, _fat.data() + static_cast<size_t>(k) * kSector))
        {
            error = "FAT sector " + std::to_string(k + 1) + " does not write";
            return false;
        }
    return true;
}

bool MdosFileSystem::PatchEntry(uint32_t slot, const std::function<void(uint8_t*)>& patch, std::string& error)
{
    const uint32_t lsn = kDirFirst + slot / (kSector / 32);
    uint8_t s[kSector];
    if (!_reader(lsn, s))
    {
        error = "directory sector " + std::to_string(lsn - kDirFirst + 1) + " does not read";
        return false;
    }
    patch(s + (slot % (kSector / 32)) * 32);
    if (!_writer(lsn, s))
    {
        error = "directory sector " + std::to_string(lsn - kDirFirst + 1) + " does not write";
        return false;
    }
    return true;
}

bool MdosFileSystem::ValidName(const std::string& name, char type, std::string& error)
{
    if (name.empty() || name.size() > 10 || name.find('\0') != std::string::npos || type == 0 ||
        static_cast<uint8_t>(type) == kDeleted)
    {
        error = "an MDOS file is a name of 1-10 bytes (no zero byte) and a type letter";
        return false;
    }
    return true;
}

bool MdosFileSystem::Write(const std::string& name, char type, const std::vector<uint8_t>& bytes,
                           const WriteOptions& options, std::string& error)
{
    if (!Writable(error) || !ValidName(name, type, error))
        return false;
    if (bytes.size() > 0xFFFFFF)
    {
        error = "an MDOS file holds at most 16 MiB";
        return false;
    }
    std::vector<MdosFile> files;
    if (!List(files, error))
        return false;
    const MdosFile* older = nullptr;
    for (const MdosFile& f : files)
        if (f.name == name && f.type == type)
            older = &f;
    if (older && !(older->attributes & kDeleteAllowed))
    {
        error = name + "." + std::string(1, type) + " is there and may not be deleted (no D attribute)";
        return false;
    }

    // The free entry and sectors, the older file's counted free
    std::vector<bool> slotUsed(kDirSectors * (kSector / 32), false);
    for (const MdosFile& f : files)
        if (&f != older)
            slotUsed[f.slot] = true;
    const std::vector<uint8_t> fat = _fat;
    if (older)
        FreeChain(older->first);
    uint32_t slot = 0;
    while (slot < slotUsed.size() && slotUsed[slot])
        slot++;
    const uint32_t needed = static_cast<uint32_t>((bytes.size() + kSector - 1) / kSector);
    std::vector<uint32_t> chain;
    for (uint32_t lsn = kDataFirst; lsn < _boot.Sectors() && chain.size() < needed; lsn++)
        if (Fat(lsn) == kFree)
            chain.push_back(lsn);
    if (slot == slotUsed.size() || chain.size() < needed)
    {
        _fat = fat;
        error = slot == slotUsed.size() ? "the MDOS directory is full (128 files)"
                                        : "not enough free sectors: " + std::to_string(needed) + " needed, " +
                                              std::to_string(chain.size()) + " free";
        return false;
    }

    // The data, the FAT, then the entry: an interrupted write leaves lost sectors, not a broken file
    uint8_t s[kSector];
    for (size_t k = 0; k < chain.size(); k++)
    {
        std::memset(s, 0, kSector);
        const size_t at = k * kSector, n = std::min<size_t>(kSector, bytes.size() - at);
        std::memcpy(s, bytes.data() + at, n);
        if (!_writer(chain[k], s))
        {
            _fat = fat;
            error = "sector " + std::to_string(chain[k]) + " does not write";
            return false;
        }
        SetFat(chain[k], k + 1 < chain.size() ? static_cast<uint16_t>(chain[k + 1])
                                              : static_cast<uint16_t>(kLast + (n == kSector ? 0 : n)));
    }
    if (!WriteFat(error))
        return false;
    const uint32_t length = static_cast<uint32_t>(bytes.size());
    const uint16_t first = chain.empty() ? kEmpty : static_cast<uint16_t>(chain.front());
    auto entry = [&](uint8_t* e) {
        std::memset(e, 0, 32);
        e[0] = static_cast<uint8_t>(type);
        std::memcpy(e + 1, name.data(), name.size());
        e[11] = static_cast<uint8_t>(length);
        e[12] = static_cast<uint8_t>(length >> 8);
        e[13] = static_cast<uint8_t>(options.start);
        e[14] = static_cast<uint8_t>(options.start >> 8);
        e[15] = static_cast<uint8_t>(options.param2);
        e[16] = static_cast<uint8_t>(options.param2 >> 8);
        e[17] = static_cast<uint8_t>(first);
        e[18] = static_cast<uint8_t>(first >> 8);
        e[20] = options.attributes;
        e[21] = static_cast<uint8_t>(length >> 16);
        std::memset(e + 22, kDeleted, 10);
    };
    if (older && older->slot != slot && !PatchEntry(older->slot, [](uint8_t* e) { e[0] = kDeleted; }, error))
        return false;
    return PatchEntry(slot, entry, error);
}

bool MdosFileSystem::Delete(const MdosFile& file, std::string& error)
{
    if (!Writable(error))
        return false;
    if (!(file.attributes & kDeleteAllowed))
    {
        error = file.name + "." + std::string(1, file.type) + " may not be deleted (no D attribute)";
        return false;
    }
    FreeChain(file.first);
    return PatchEntry(file.slot, [](uint8_t* e) { e[0] = kDeleted; }, error) && WriteFat(error);
}

bool MdosFileSystem::Rename(const MdosFile& file, const std::string& name, char type, std::string& error)
{
    if (!Writable(error) || !ValidName(name, type, error))
        return false;
    std::vector<MdosFile> files;
    if (!List(files, error))
        return false;
    for (const MdosFile& f : files)
        if (f.slot != file.slot && f.name == name && f.type == type)
        {
            error = name + "." + std::string(1, type) + " is there already";
            return false;
        }
    return PatchEntry(
        file.slot,
        [&](uint8_t* e) {
            e[0] = static_cast<uint8_t>(type);
            std::memset(e + 1, 0, 10);
            std::memcpy(e + 1, name.data(), name.size());
        },
        error);
}

bool MdosFileSystem::SetAttributes(const MdosFile& file, uint8_t attributes, std::string& error)
{
    return Writable(error) && PatchEntry(file.slot, [attributes](uint8_t* e) { e[20] = attributes; }, error);
}

bool MdosFileSystem::SetName(const std::string& name, std::string& error)
{
    if (!Writable(error))
        return false;
    if (name.size() > 10 || name.find('\0') != std::string::npos)
    {
        error = "an MDOS disk name has at most 10 bytes";
        return false;
    }
    uint8_t s[kSector];
    if (!_reader(0, s))
    {
        error = "the boot sector does not read";
        return false;
    }
    std::memset(s + 192, 0, 10);
    std::memcpy(s + 192, name.data(), name.size());
    if (!_writer(0, s))
    {
        error = "the boot sector does not write";
        return false;
    }
    _boot.name = name;
    return true;
}
}  // namespace unrealdisk
