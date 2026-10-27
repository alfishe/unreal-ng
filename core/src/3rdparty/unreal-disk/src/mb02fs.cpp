#include "unrealdisk/mb02fs.h"

#include <cstring>
#include <memory>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/loaders/loader_mb02.h"

namespace unrealdisk
{
namespace
{
uint16_t Word(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

/// `size` bytes at `p`, trailing blanks and zeroes cut
std::string Trimmed(const uint8_t* p, size_t size)
{
    std::string text(reinterpret_cast<const char*>(p), size);
    while (!text.empty() && (text.back() == ' ' || text.back() == 0))
        text.pop_back();
    return text;
}

constexpr uint16_t kNext = 0xC000;  ///< bits 15 + 14: the next LSN follows
constexpr uint16_t kUsed = 0x8000;
constexpr uint16_t kLsn = 0x3FFF;
}  // namespace

Mb02FileSystem Mb02FileSystem::OnDisk(DiskImage& image)
{
    auto spt = std::make_shared<uint32_t>(0);
    DiskImage* disk = &image;
    return Mb02FileSystem([disk, spt](uint32_t lsn, uint8_t* dst) {
        auto sector = [disk](uint8_t cylinder, uint8_t side, uint8_t id) -> const uint8_t* {
            DiskImage::Track* track = cylinder < disk->getCylinders() && side < disk->getSides()
                                          ? disk->getTrackForCylinderAndSide(cylinder, side)
                                          : nullptr;
            DiskImage::Sector* s = track ? track->findSector(id) : nullptr;
            return s && s->hasData && s->dataSize == kSector ? s->data : nullptr;
        };
        if (*spt == 0)
        {
            // The boot sector gives the geometry
            const uint8_t* boot = sector(0, 0, 1);
            uint8_t tracks = 0, sectors = 0;
            if (!boot || !LoaderMB02::bootGeometry(boot, tracks, sectors))
                return false;
            *spt = sectors;
        }
        const uint32_t cylinder = lsn / (2 * *spt);
        if (cylinder > 0xFF)
            return false;
        const uint8_t* data = sector(static_cast<uint8_t>(cylinder), static_cast<uint8_t>(lsn / *spt % 2),
                                     static_cast<uint8_t>(lsn % *spt + 1));
        if (!data)
            return false;
        std::memcpy(dst, data, kSector);
        return true;
    });
}

bool Mb02FileSystem::Open(std::string* why)
{
    auto fail = [why](const std::string& text) {
        if (why)
            *why = text;
        return false;
    };
    uint8_t s[kSector];
    if (!_reader(0, s))
        return fail("no 1024-byte boot sector");
    uint8_t tracks = 0, sectors = 0;
    if (!LoaderMB02::bootGeometry(s, tracks, sectors))
        return fail("no MB-02 boot sector");
    _boot = {};
    _boot.tracks = tracks;
    _boot.sectors = sectors;
    _boot.dirs = Word(s + 0x0C);
    _boot.fatSectors = Word(s + 0x0E);
    _boot.fat1 = Word(s + 0x12);
    _boot.fat2 = Word(s + 0x14);
    _boot.time = Word(s + 0x21);
    _boot.date = Word(s + 0x23);
    _boot.name = Trimmed(s + 0x26, 10);
    const uint32_t total = _boot.Sectors();
    if (_boot.fatSectors < 1 || _boot.fatSectors > 4 || Word(s + 0x10) != _boot.fatSectors * kSector)
        return fail("the FAT size does not read");
    if (static_cast<uint32_t>(_boot.fatSectors) * kSector / 2 < total)
        return fail("the FAT does not cover the disk");
    if (_boot.dirs >= total || _boot.fat1 >= total || _boot.fat2 >= total)
        return fail("the FAT or the DIRS sector lies past the disk");

    // FAT sectors 2..4 of both copies are bytes at #1A..#1F, interleaved FAT1 / FAT2
    _fat.assign(static_cast<size_t>(_boot.fatSectors) * kSector, 0);
    for (uint16_t k = 0; k < _boot.fatSectors; k++)
    {
        const uint16_t one = k == 0 ? _boot.fat1 : s[0x1A + 2 * (k - 1)];
        const uint16_t two = k == 0 ? _boot.fat2 : s[0x1B + 2 * (k - 1)];
        uint8_t* dst = _fat.data() + static_cast<size_t>(k) * kSector;
        if (!_reader(one, dst) && !_reader(two, dst))
            return fail("FAT sector " + std::to_string(k + 1) + " reads in neither copy");
    }
    if (Fat(1) != 0xFF00)
        return fail("the FAT does not reserve the backup boot sector");

    uint8_t dirs[kSector];
    if (!_reader(_boot.dirs, dirs))
        return fail("the DIRS sector does not read");
    if (!(dirs[0] & 0x80) || (Word(dirs + 2) & kLsn) >= total)
        return fail("the DIRS sector has no root directory");
    return true;
}

uint16_t Mb02FileSystem::Fat(uint32_t lsn) const
{
    return 2 * static_cast<size_t>(lsn) + 1 < _fat.size() ? Word(_fat.data() + 2 * static_cast<size_t>(lsn)) : 0xFFFF;
}

bool Mb02FileSystem::Chain(uint16_t first, std::vector<uint16_t>& out, uint32_t& last, std::string& error) const
{
    out.clear();
    const uint32_t total = _boot.Sectors();
    uint16_t lsn = first;
    while (true)
    {
        if (lsn >= total || out.size() >= total)
        {
            error = "the FAT chain from sector " + std::to_string(first) + " leaves the disk or loops";
            return false;
        }
        out.push_back(lsn);
        const uint16_t next = Fat(lsn);
        if ((next >> 8) == 0xFF || !(next & kUsed))
        {
            error = "the FAT chain from sector " + std::to_string(first) + " meets a free or reserved sector";
            return false;
        }
        if ((next & kNext) != kNext)
        {
            last = next & kLsn;
            if (last == 0 || last > kSector)
            {
                error = "the FAT chain from sector " + std::to_string(first) + " ends with a bad byte count";
                return false;
            }
            return true;
        }
        lsn = next & kLsn;
    }
}

bool Mb02FileSystem::Sectors(const std::vector<uint16_t>& chain, std::vector<uint8_t>& out, std::string& error) const
{
    out.resize(chain.size() * kSector);
    for (size_t k = 0; k < chain.size(); k++)
        if (!_reader(chain[k], out.data() + k * kSector))
        {
            error = "sector " + std::to_string(chain[k]) + " does not read";
            return false;
        }
    return true;
}

bool Mb02FileSystem::Directories(std::vector<Mb02Directory>& out, std::string& error) const
{
    out.clear();
    uint8_t dirs[kSector];
    if (!_reader(_boot.dirs, dirs))
    {
        error = "the DIRS sector does not read";
        return false;
    }
    for (uint32_t number = 0; number < 256; number++)
    {
        const uint8_t* slot = dirs + number * 4;
        if (!(slot[0] & 0x80))
            continue;
        Mb02Directory d;
        d.number = static_cast<uint8_t>(number);
        d.first = Word(slot + 2) & kLsn;
        uint8_t header[kSector];
        if (d.first >= _boot.Sectors() || !_reader(d.first, header))
        {
            error = "directory " + std::to_string(number) + " does not read";
            return false;
        }
        d.parent = header[0x05];
        d.name = Trimmed(header + 0x06, 10);
        out.push_back(std::move(d));
    }
    return true;
}

bool Mb02FileSystem::List(uint8_t number, std::vector<Mb02File>& out, std::string& error) const
{
    out.clear();
    uint8_t dirs[kSector];
    if (!_reader(_boot.dirs, dirs))
    {
        error = "the DIRS sector does not read";
        return false;
    }
    const uint8_t* slot = dirs + number * 4;
    if (!(slot[0] & 0x80))
    {
        error = "no directory " + std::to_string(number);
        return false;
    }
    std::vector<uint16_t> chain;
    uint32_t last = 0;
    std::vector<uint8_t> bytes;
    if (!Chain(Word(slot + 2) & kLsn, chain, last, error) || !Sectors(chain, bytes, error))
        return false;
    for (uint32_t entry = 1; entry < bytes.size() / 32; entry++)
    {
        const uint8_t* e = bytes.data() + entry * 32;
        if (!(e[0] & Mb02File::kValid))
            continue;
        Mb02File f;
        f.directory = number;
        f.entry = entry;
        f.flags = e[0x00];
        f.time = Word(e + 0x01);
        f.date = Word(e + 0x03);
        if (f.flags & Mb02File::kHeader)
        {
            f.type = e[0x05];
            f.name = Trimmed(e + 0x06, 10);
            f.headerLength = Word(e + 0x10);
            f.param1 = Word(e + 0x12);
            f.param2 = Word(e + 0x14);
        }
        f.address = Word(e + 0x16);
        f.length = (f.flags & Mb02File::kBody)
                       ? static_cast<uint32_t>(e[0x18] | e[0x19] << 8 | e[0x1A] << 16 | e[0x1B] << 24)
                       : 0;
        f.bodyFlag = e[0x1C];
        f.attributes = e[0x1D];
        f.first = Word(e + 0x1E) & kLsn;
        out.push_back(std::move(f));
    }
    return true;
}

bool Mb02FileSystem::Read(const Mb02File& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    if (file.length == 0)
        return true;
    std::vector<uint16_t> chain;
    uint32_t last = 0;
    if (!Chain(file.first, chain, last, error))
        return false;
    const uint64_t held = (chain.size() - 1) * static_cast<uint64_t>(kSector) + last;
    if (held < file.length)
    {
        error =
            "the FAT chain holds " + std::to_string(held) + " of the file's " + std::to_string(file.length) + " bytes";
        return false;
    }
    if (!Sectors(chain, data, error))
        return false;
    data.resize(file.length);
    return true;
}

uint32_t Mb02FileSystem::FreeSectors() const
{
    uint32_t free = 0;
    // LSN 0 is the boot sector whatever its FAT word holds
    for (uint32_t lsn = 1; lsn < _boot.Sectors(); lsn++)
        if (!(Fat(lsn) & kUsed))
            free++;
    return free;
}
}  // namespace unrealdisk
