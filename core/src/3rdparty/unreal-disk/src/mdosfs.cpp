#include "unrealdisk/mdosfs.h"

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
    return MdosFileSystem([disk, geometry](uint32_t lsn, uint8_t* dst) {
        auto sector = [disk](uint32_t cylinder, uint8_t side, uint8_t id) -> const uint8_t* {
            DiskImage::Track* track = cylinder < disk->getCylinders() && side < disk->getSides()
                                          ? disk->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), side)
                                          : nullptr;
            DiskImage::Sector* s = track ? track->findSector(id) : nullptr;
            return s && s->hasData && s->dataSize == kSector ? s->data : nullptr;
        };
        if (geometry->sectors == 0)
        {
            const uint8_t* boot = sector(0, 0, 1);
            uint8_t cylinders = 0;
            if (!boot || !LoaderMDOS::bootGeometry(boot, cylinders, geometry->sides, geometry->sectors, kMaxCylinders))
                return false;
        }
        const uint32_t track = lsn / geometry->sectors;
        const uint8_t* data = sector(track / geometry->sides, static_cast<uint8_t>(track % geometry->sides),
                                     static_cast<uint8_t>(lsn % geometry->sectors + 1));
        if (!data)
            return false;
        std::memcpy(dst, data, kSector);
        return true;
    });
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
}  // namespace unrealdisk
