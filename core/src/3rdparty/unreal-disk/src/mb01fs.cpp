#include "unrealdisk/mb01fs.h"

#include <cstring>
#include <memory>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
uint16_t Word(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

uint32_t Long(const uint8_t* p)
{
    return Word(p) | static_cast<uint32_t>(Word(p + 2)) << 16;
}

/// `size` bytes at `p`, trailing blanks and zeroes cut
std::string Trimmed(const uint8_t* p, size_t size)
{
    std::string text(reinterpret_cast<const char*>(p), size);
    while (!text.empty() && (text.back() == ' ' || text.back() == 0))
        text.pop_back();
    return text;
}

bool IsBoot(const uint8_t* s)
{
    return s[2] == 0x80 && s[3] == 0x01;
}

constexpr uint16_t kDeletedSector = 0x8000;
constexpr uint16_t kLast = 0x4000;  ///< bits 0-11 the last sector's bytes
constexpr uint16_t kKind = 0x3000;
constexpr uint16_t kSystem = 0x1000;
constexpr uint16_t kLsn = 0x0FFF;
constexpr uint32_t kDirEntry = 16;
constexpr uint32_t kFileEntry = 32;
constexpr uint8_t kMaxSpt = 22;
}  // namespace

Mb01FileSystem Mb01FileSystem::OnDisk(DiskImage& image)
{
    DiskImage* disk = &image;
    auto find = [disk](uint32_t cylinder, uint8_t side, uint8_t id) -> const DiskImage::Sector* {
        DiskImage::Track* t = cylinder < disk->getCylinders() && side < disk->getSides()
                                  ? disk->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), side)
                                  : nullptr;
        const DiskImage::Sector* s = t ? t->findSector(id) : nullptr;
        return s && s->hasData && s->dataSize == kSector ? s : nullptr;
    };
    uint32_t spt = 0, bootLsn = 0;
    for (uint8_t id = 1; id <= kMaxSpt && !spt; id++)
        if (const DiskImage::Sector* s = find(0, 0, id);
            s && IsBoot(s->data) && s->data[15] >= id && s->data[15] <= kMaxSpt)
        {
            spt = s->data[15];
            bootLsn = id - 1u;
        }
    return Mb01FileSystem(
        [find, spt](uint32_t lsn, uint8_t* dst) {
            if (spt == 0 || lsn / (2 * spt) > 0xFF)
                return false;
            const DiskImage::Sector* s =
                find(lsn / (2 * spt), static_cast<uint8_t>(lsn / spt % 2), static_cast<uint8_t>(lsn % spt + 1));
            if (!s)
                return false;
            std::memcpy(dst, s->data, kSector);
            return true;
        },
        bootLsn);
}

bool Mb01FileSystem::Open(std::string* why)
{
    auto fail = [why](const std::string& text) {
        if (why)
            *why = text;
        return false;
    };
    uint8_t s[kSector];
    if (!_reader(_bootLsn, s))
        return fail("no 1024-byte boot sector");
    if (!IsBoot(s))
        return fail("no MB-01 boot sector");
    _boot = {};
    _boot.lsn = _bootLsn;
    _boot.name = Trimmed(s + 4, 10);
    _boot.tracks = s[14];
    _boot.sectors = s[15];
    _boot.time = Word(s + 16);
    _boot.date = Word(s + 18);
    _boot.dirs = s[24];
    _boot.files = s[25];
    _boot.fatSectors = s[26];
    _boot.fat1 = Long(s + 27);
    _boot.fat2 = Long(s + 31);
    const uint32_t total = _boot.Sectors();
    if ((_boot.tracks != 40 && _boot.tracks != 80) || _boot.sectors < 1 || _boot.sectors > kMaxSpt)
        return fail("the geometry does not read");
    if (_boot.fatSectors < 1 || _boot.fatSectors > 4 || static_cast<uint32_t>(_boot.fatSectors) * kSector / 2 < total)
        return fail("the FAT does not cover the disk");
    if (_boot.dirs >= total || _boot.files >= total || _boot.fat1 >= total || _boot.fat2 >= total)
        return fail("the FAT or a list lies past the disk");

    // FAT sector k + 1 follows sector k in the FAT's own chain, else the next LSN
    _fat.assign(static_cast<size_t>(_boot.fatSectors) * kSector, 0);
    for (uint32_t copy = 0; copy < 2; copy++)
    {
        std::vector<uint8_t> fat(_fat.size(), 0);
        uint32_t lsn = copy ? _boot.fat2 : _boot.fat1;
        bool ok = true;
        for (uint16_t k = 0; k < _boot.fatSectors && ok; k++)
        {
            ok = lsn < total && _reader(lsn, fat.data() + static_cast<size_t>(k) * kSector);
            if (2 * static_cast<size_t>(lsn) + 1 < (k + 1u) * kSector)
            {
                const uint16_t next = Word(fat.data() + 2 * static_cast<size_t>(lsn));
                lsn = (next & kKind) == kSystem && !(next & kLast) ? next & kLsn : lsn + 1;
            }
            else
                lsn++;
        }
        if (ok)
        {
            _fat = std::move(fat);
            break;
        }
        if (copy)
            return fail("the FAT reads in neither copy");
    }
    if ((Fat(_bootLsn) & kKind) != kSystem || (Fat(_bootLsn + 1) & kKind) != kSystem)
        return fail("the FAT does not keep the boot and backup sectors as system ones");
    return true;
}

uint16_t Mb01FileSystem::Fat(uint32_t lsn) const
{
    return 2 * static_cast<size_t>(lsn) + 1 < _fat.size() ? Word(_fat.data() + 2 * static_cast<size_t>(lsn)) : 0x3000;
}

uint32_t Mb01FileSystem::FreeSectors() const
{
    uint32_t free = 0;
    for (uint32_t lsn = 0; lsn < _boot.Sectors(); lsn++)
        free += Fat(lsn) == 0;
    return free;
}

bool Mb01FileSystem::ChainBytes(uint32_t first, std::vector<uint8_t>& out, std::string& error) const
{
    out.clear();
    const uint32_t total = _boot.Sectors();
    uint32_t lsn = first;
    for (uint32_t count = 0;; count++)
    {
        const uint16_t word = Fat(lsn);
        if (lsn >= total || count >= total || word == 0 || (word & kDeletedSector) || (word & kKind) >= 0x2000)
        {
            error = "the FAT chain from sector " + std::to_string(first) +
                    " leaves the disk, loops or meets a free, bad or deleted sector";
            return false;
        }
        const size_t at = out.size();
        out.resize(at + kSector);
        if (!_reader(lsn, out.data() + at))
        {
            error = "sector " + std::to_string(lsn) + " does not read";
            return false;
        }
        if (word & kLast)
        {
            const uint32_t last = word & kLsn;
            if (last == 0 || last > kSector)
            {
                error = "the FAT chain from sector " + std::to_string(first) + " ends with a bad byte count";
                return false;
            }
            out.resize(at + last);
            return true;
        }
        lsn = word & kLsn;
    }
}

bool Mb01FileSystem::Directories(std::vector<Mb02Directory>& out, std::string& error) const
{
    out.clear();
    std::vector<uint8_t> list;
    if (!ChainBytes(_boot.dirs, list, error))
    {
        error = "the directory list: " + error;
        return false;
    }
    Mb02Directory root;
    root.first = _boot.dirs;
    out.push_back(root);
    for (uint32_t number = 1; number < 256 && (number + 1) * kDirEntry <= list.size(); number++)
    {
        const uint8_t* e = list.data() + number * kDirEntry;
        if (e[5] == 0)
            continue;
        Mb02Directory d;
        d.number = static_cast<uint8_t>(number);
        d.parent = e[0];
        d.time = Word(e + 1);
        d.date = Word(e + 3);
        d.name = Trimmed(e + 6, 10);
        out.push_back(std::move(d));
    }
    return true;
}

bool Mb01FileSystem::List(uint8_t number, std::vector<Mb02File>& out, std::string& error) const
{
    out.clear();
    std::vector<uint8_t> list;
    if (!ChainBytes(_boot.files, list, error))
    {
        error = "the file list: " + error;
        return false;
    }
    for (uint32_t entry = 0; (entry + 1) * kFileEntry <= list.size(); entry++)
    {
        const uint8_t* e = list.data() + entry * kFileEntry;
        // Deleted, or neither header nor body: no file
        if (e[0] != number || (e[5] & kDeleted) || !(e[5] & (kBody | kHeader)))
            continue;
        Mb02File f;
        f.directory = number;
        f.entry = entry;
        f.flags = e[5];
        f.time = Word(e + 1);
        f.date = Word(e + 3);
        f.type = e[5] & 0x0F;
        if (e[5] & kHeader)
            f.name = Trimmed(e + 6, 10);
        f.headerLength = Word(e + 16);
        f.param1 = Word(e + 18);
        f.param2 = Word(e + 20);
        f.address = Word(e + 22);
        f.length = (e[5] & kBody) ? Word(e + 24) | static_cast<uint32_t>(e[26]) << 16 : 0;
        f.bodyFlag = e[27];
        f.attributes = e[28];
        f.first = Word(e + 30) & kLsn;
        out.push_back(std::move(f));
    }
    return true;
}

bool Mb01FileSystem::Read(const Mb02File& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    if (file.length == 0)
        return true;
    if (!ChainBytes(file.first, data, error))
        return false;
    if (data.size() < file.length)
    {
        error = "the body's chain holds " + std::to_string(data.size()) + " of its " + std::to_string(file.length) +
                " bytes";
        return false;
    }
    data.resize(file.length);
    return true;
}
}  // namespace unrealdisk
