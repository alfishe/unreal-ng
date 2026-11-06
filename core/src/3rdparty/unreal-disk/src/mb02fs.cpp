#include "unrealdisk/mb02fs.h"

#include <algorithm>
#include <array>
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
constexpr uint32_t kBackup = 1;  ///< the backup sector: system sectors go here first
constexpr uint32_t kEntry = 32;
constexpr uint16_t kProgramAddress = 23755;  ///< a program's body address as BS-DOS saves one (PROG of a 48K machine)

void SetWord(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

/// `name` in `size` bytes, blanks after it
void PutName(uint8_t* p, const std::string& name, size_t size = 10)
{
    std::memset(p, ' ', size);
    std::memcpy(p, name.data(), std::min(name.size(), size));
}

uint8_t NameXor(const uint8_t* name)
{
    uint8_t x = 0;
    for (int k = 0; k < 10; k++)
        x ^= name[k];
    return x;
}
}  // namespace

Mb02FileSystem Mb02FileSystem::OnDisk(DiskImage& image)
{
    auto spt = std::make_shared<uint32_t>(0);
    DiskImage* disk = &image;
    // The track and the sector id of `lsn` (the geometry from the boot sector the first time)
    auto locate = [disk, spt](uint32_t lsn, DiskImage::Track*& track, uint8_t& id) -> DiskImage::Sector* {
        auto find = [disk](uint32_t cylinder, uint8_t side, uint8_t sectorId, DiskImage::Track*& t) {
            t = cylinder < disk->getCylinders() && side < disk->getSides()
                    ? disk->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), side)
                    : nullptr;
            DiskImage::Sector* s = t ? t->findSector(sectorId) : nullptr;
            return s && s->hasData && s->dataSize == kSector ? s : nullptr;
        };
        if (*spt == 0)
        {
            const DiskImage::Sector* boot = find(0, 0, 1, track);
            uint8_t tracks = 0, sectors = 0;
            if (!boot || !LoaderMB02::bootGeometry(boot->data, tracks, sectors))
                return nullptr;
            *spt = sectors;
        }
        const uint32_t cylinder = lsn / (2 * *spt);
        id = static_cast<uint8_t>(lsn % *spt + 1);
        return cylinder > 0xFF ? nullptr : find(cylinder, static_cast<uint8_t>(lsn / *spt % 2), id, track);
    };
    Mb02FileSystem fs([locate](uint32_t lsn, uint8_t* dst) {
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
    if (!(dirs[0] & 0x80) || static_cast<uint32_t>(Word(dirs + 2) & kLsn) >= total)
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
        d.time = Word(header + 0x01);
        d.date = Word(header + 0x03);
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

bool Mb02FileSystem::Writable(std::string& error) const
{
    if (_writer && !_fat.empty())
        return true;
    error = "the MB-02 volume is read only";
    return false;
}

bool Mb02FileSystem::ValidName(const std::string& name, std::string& error)
{
    if (name.empty() || name.size() > 10 || name.back() == ' ')
    {
        error = "an MB-02 name has 1-10 bytes (no blank at its end): " + name;
        return false;
    }
    return true;
}

void Mb02FileSystem::SetFat(uint32_t lsn, uint16_t value)
{
    if (2 * static_cast<size_t>(lsn) + 1 < _fat.size())
        SetWord(_fat.data() + 2 * static_cast<size_t>(lsn), value);
}

bool Mb02FileSystem::WriteFat(std::string& error)
{
    uint8_t boot[kSector];
    if (!_reader(0, boot))
    {
        error = "the boot sector does not read";
        return false;
    }
    for (uint16_t k = 0; k < _boot.fatSectors; k++)
    {
        const uint8_t* src = _fat.data() + static_cast<size_t>(k) * kSector;
        const uint16_t one = k == 0 ? _boot.fat1 : boot[0x1A + 2 * (k - 1)];
        const uint16_t two = k == 0 ? _boot.fat2 : boot[0x1B + 2 * (k - 1)];
        if (!_writer(one, src) || !_writer(two, src))
        {
            error = "FAT sector " + std::to_string(k + 1) + " does not write";
            return false;
        }
    }
    return true;
}

bool Mb02FileSystem::WriteSystem(uint32_t lsn, const uint8_t* data, std::string& error)
{
    if (_writer(kBackup, data) && _writer(lsn, data))
        return true;
    error = "sector " + std::to_string(lsn) + " does not write";
    return false;
}

bool Mb02FileSystem::Allocate(uint32_t count, uint32_t last, std::vector<uint16_t>& out, std::string& error)
{
    out.clear();
    // LSN 0 is the boot sector whatever its FAT word holds, 1 the backup
    for (uint32_t lsn = 2; lsn < _boot.Sectors() && out.size() < count; lsn++)
        if (!(Fat(lsn) & kUsed))
            out.push_back(static_cast<uint16_t>(lsn));
    if (out.size() < count)
    {
        error =
            "not enough free sectors: " + std::to_string(count) + " needed, " + std::to_string(out.size()) + " free";
        return false;
    }
    for (size_t k = 0; k < out.size(); k++)
        SetFat(out[k],
               k + 1 < out.size() ? static_cast<uint16_t>(kNext | out[k + 1]) : static_cast<uint16_t>(kUsed | last));
    return true;
}

void Mb02FileSystem::FreeChain(uint16_t first)
{
    // Along the chain while it goes on; a loop is cut after as many steps as the disk has sectors
    uint32_t lsn = first;
    for (uint32_t steps = 0; lsn >= 2 && lsn < _boot.Sectors() && steps < _boot.Sectors(); steps++)
    {
        const uint16_t word = Fat(lsn);
        if ((word >> 8) == 0xFF || !(word & kUsed))
            break;
        SetFat(lsn, 0);
        if ((word & kNext) != kNext)
            break;
        lsn = word & kLsn;
    }
}

bool Mb02FileSystem::DirectoryBytes(uint8_t number, std::vector<uint16_t>& chain, std::vector<uint8_t>& bytes,
                                    std::string& error) const
{
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
    uint32_t last = 0;
    return Chain(Word(slot + 2) & kLsn, chain, last, error) && Sectors(chain, bytes, error);
}

bool Mb02FileSystem::FreeEntry(std::vector<uint16_t>& chain, std::vector<uint8_t>& bytes, uint32_t& entry,
                               std::string& error)
{
    for (entry = 1; entry < bytes.size() / kEntry; entry++)
        if (!(bytes[entry * kEntry] & Mb02File::kValid))
            return true;
    std::vector<uint16_t> added;
    if (!Allocate(1, kSector, added, error))
        return false;
    SetFat(chain.back(), static_cast<uint16_t>(kNext | added[0]));
    chain.push_back(added[0]);
    bytes.resize(bytes.size() + kSector, 0);
    return true;
}

bool Mb02FileSystem::WriteEntry(const std::vector<uint16_t>& chain, const std::vector<uint8_t>& bytes, uint32_t entry,
                                std::string& error)
{
    const size_t sector = entry * kEntry / kSector;
    return WriteSystem(chain[sector], bytes.data() + sector * kSector, error);
}

bool Mb02FileSystem::Write(uint8_t dir, const std::string& name, const std::vector<uint8_t>& bytes,
                           const WriteOptions& options, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    std::vector<uint16_t> chain;
    std::vector<uint8_t> dirBytes;
    if (!DirectoryBytes(dir, chain, dirBytes, error))
        return false;
    const std::vector<uint8_t> fat = _fat;
    auto fail = [this, &fat]() {
        _fat = fat;
        return false;
    };

    // The older file of the name goes: its entry is the new one's
    std::vector<Mb02File> files;
    if (!List(dir, files, error))
        return false;
    uint32_t entry = 0;
    for (const Mb02File& f : files)
        if ((f.flags & Mb02File::kHeader) && f.name == name)
        {
            if (f.flags & Mb02File::kBody)
                FreeChain(f.first);
            entry = f.entry;
            dirBytes[entry * kEntry] &= static_cast<uint8_t>(~Mb02File::kValid);
        }
    if (entry == 0 && !FreeEntry(chain, dirBytes, entry, error))
        return fail();

    // The body, the FAT (both copies), then the entry
    std::vector<uint16_t> body;
    const uint32_t count = static_cast<uint32_t>((bytes.size() + kSector - 1) / kSector);
    const uint32_t last = bytes.empty() ? 0 : static_cast<uint32_t>(bytes.size() - (count - 1) * kSector);
    if (count && !Allocate(count, last, body, error))
        return fail();
    uint8_t s[kSector];
    for (size_t k = 0; k < body.size(); k++)
    {
        std::memset(s, 0, kSector);
        std::memcpy(s, bytes.data() + k * kSector, std::min<size_t>(kSector, bytes.size() - k * kSector));
        if (!_writer(body[k], s))
        {
            error = "sector " + std::to_string(body[k]) + " does not write";
            return fail();
        }
    }
    if (!WriteFat(error))
        return false;
    uint8_t* e = dirBytes.data() + entry * kEntry;
    std::memset(e, 0, kEntry);
    e[0x00] = static_cast<uint8_t>(Mb02File::kValid | Mb02File::kHeader | (bytes.empty() ? 0 : Mb02File::kBody));
    SetWord(e + 0x01, options.time);
    SetWord(e + 0x03, options.date);
    e[0x05] = options.type;
    PutName(e + 0x06, name);
    SetWord(e + 0x10, static_cast<uint32_t>(std::min<size_t>(bytes.size(), 0xFFFF)));
    SetWord(e + 0x12, options.start);
    SetWord(e + 0x14, options.param2);
    SetWord(e + 0x16, options.type == 0 ? kProgramAddress : options.start);
    const uint32_t length = static_cast<uint32_t>(bytes.size());
    for (int k = 0; k < 4; k++)
        e[0x18 + k] = static_cast<uint8_t>(length >> (8 * k));
    e[0x1C] = 0xFF;
    SetWord(e + 0x1E, body.empty() ? 0 : body.front());
    return WriteEntry(chain, dirBytes, entry, error);
}

bool Mb02FileSystem::Delete(const Mb02File& file, std::string& error)
{
    std::vector<uint16_t> chain;
    std::vector<uint8_t> bytes;
    if (!Writable(error) || !DirectoryBytes(file.directory, chain, bytes, error))
        return false;
    bytes[file.entry * kEntry] &= static_cast<uint8_t>(~Mb02File::kValid);
    if (!WriteEntry(chain, bytes, file.entry, error))
        return false;
    if (file.flags & Mb02File::kBody)
        FreeChain(file.first);
    return WriteFat(error);
}

bool Mb02FileSystem::Rename(const Mb02File& file, uint8_t dir, const std::string& name, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    if (!(file.flags & Mb02File::kHeader))
    {
        error = "a file without a header has no name";
        return false;
    }
    std::vector<Mb02File> files;
    if (!List(dir, files, error))
        return false;
    for (const Mb02File& f : files)
        if ((f.flags & Mb02File::kHeader) && f.name == name && !(dir == file.directory && f.entry == file.entry))
        {
            error = name + " is there already";
            return false;
        }
    std::vector<uint16_t> chain;
    std::vector<uint8_t> bytes;
    if (!DirectoryBytes(file.directory, chain, bytes, error))
        return false;
    std::array<uint8_t, kEntry> entry;
    std::copy_n(bytes.begin() + file.entry * kEntry, kEntry, entry.begin());
    PutName(entry.data() + 0x06, name);
    if (dir == file.directory)
    {
        std::copy(entry.begin(), entry.end(), bytes.begin() + file.entry * kEntry);
        return WriteEntry(chain, bytes, file.entry, error);
    }
    // Into the other directory first, then out of this one
    std::vector<uint16_t> toChain;
    std::vector<uint8_t> toBytes;
    uint32_t at = 0;
    const std::vector<uint8_t> fat = _fat;
    if (!DirectoryBytes(dir, toChain, toBytes, error) || !FreeEntry(toChain, toBytes, at, error))
    {
        _fat = fat;
        return false;
    }
    std::copy(entry.begin(), entry.end(), toBytes.begin() + at * kEntry);
    if (_fat != fat && !WriteFat(error))
        return false;
    if (!WriteEntry(toChain, toBytes, at, error))
        return false;
    bytes[file.entry * kEntry] &= static_cast<uint8_t>(~Mb02File::kValid);
    return WriteEntry(chain, bytes, file.entry, error);
}

bool Mb02FileSystem::SetTime(const Mb02File& file, uint16_t date, uint16_t time, std::string& error)
{
    std::vector<uint16_t> chain;
    std::vector<uint8_t> bytes;
    if (!Writable(error) || !DirectoryBytes(file.directory, chain, bytes, error))
        return false;
    SetWord(bytes.data() + file.entry * kEntry + 0x01, time);
    SetWord(bytes.data() + file.entry * kEntry + 0x03, date);
    return WriteEntry(chain, bytes, file.entry, error);
}

bool Mb02FileSystem::MakeDirectory(uint8_t parent, const std::string& name, uint16_t date, uint16_t time,
                                   std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    std::vector<Mb02Directory> dirs;
    if (!Directories(dirs, error))
        return false;
    for (const Mb02Directory& d : dirs)
        if (d.number && d.parent == parent && d.name == name)
        {
            error = name + " is there already";
            return false;
        }
    uint8_t list[kSector];
    if (!_reader(_boot.dirs, list))
    {
        error = "the DIRS sector does not read";
        return false;
    }
    uint32_t number = 1;
    while (number < 256 && (list[number * 4] & 0x80))
        number++;
    if (number == 256)
    {
        error = "the DIRS sector is full (255 directories)";
        return false;
    }
    const std::vector<uint8_t> fat = _fat;
    std::vector<uint16_t> sector;
    if (!Allocate(1, kSector, sector, error))
        return false;
    uint8_t s[kSector] = {};
    s[0x00] = 0x80;
    SetWord(s + 0x01, time);
    SetWord(s + 0x03, date);
    s[0x05] = parent;
    PutName(s + 0x06, name);
    std::memset(s + 0x10, ' ', 0x10);
    if (!WriteSystem(sector[0], s, error))
    {
        _fat = fat;
        return false;
    }
    if (!WriteFat(error))
        return false;
    uint8_t* slot = list + number * 4;
    slot[0] = 0x80;
    slot[1] = NameXor(s + 0x06);
    SetWord(slot + 2, kUsed | sector[0]);
    return WriteSystem(_boot.dirs, list, error);
}

bool Mb02FileSystem::DeleteDirectory(uint8_t number, std::string& error)
{
    if (!Writable(error))
        return false;
    if (number == 0)
    {
        error = "the root directory stays";
        return false;
    }
    std::vector<Mb02File> files;
    std::vector<Mb02Directory> dirs;
    if (!List(number, files, error) || !Directories(dirs, error))
        return false;
    bool children = false;
    for (const Mb02Directory& d : dirs)
        children = children || (d.number != number && d.parent == number);
    if (!files.empty() || children)
    {
        error = "the directory is not empty";
        return false;
    }
    uint8_t list[kSector];
    if (!_reader(_boot.dirs, list))
    {
        error = "the DIRS sector does not read";
        return false;
    }
    const uint16_t first = Word(list + number * 4 + 2) & kLsn;
    std::memset(list + number * 4, 0, 4);
    if (!WriteSystem(_boot.dirs, list, error))
        return false;
    FreeChain(first);
    return WriteFat(error);
}

bool Mb02FileSystem::RenameDirectory(uint8_t number, uint8_t parent, const std::string& name, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    std::vector<Mb02Directory> dirs;
    if (!Directories(dirs, error))
        return false;
    for (const Mb02Directory& d : dirs)
        if (d.number && d.number != number && d.parent == parent && d.name == name)
        {
            error = name + " is there already";
            return false;
        }
    // Not into itself: the new parent's ancestors do not meet the directory
    std::vector<int> up(256, -1);
    for (const Mb02Directory& d : dirs)
        up[d.number] = d.number ? d.parent : -1;
    for (int p = parent, steps = 0; p > 0 && steps < 256; p = up[p], steps++)
        if (p == number)
        {
            error = "a directory does not go into itself";
            return false;
        }
    std::vector<uint16_t> chain;
    std::vector<uint8_t> bytes;
    if (!DirectoryBytes(number, chain, bytes, error))
        return false;
    bytes[0x05] = parent;
    PutName(bytes.data() + 0x06, name);
    uint8_t list[kSector];
    if (!WriteEntry(chain, bytes, 0, error) || !_reader(_boot.dirs, list))
        return false;
    list[number * 4 + 1] = NameXor(bytes.data() + 0x06);
    return WriteSystem(_boot.dirs, list, error);
}

bool Mb02FileSystem::SetDirectoryTime(uint8_t number, uint16_t date, uint16_t time, std::string& error)
{
    std::vector<uint16_t> chain;
    std::vector<uint8_t> bytes;
    if (!Writable(error) || !DirectoryBytes(number, chain, bytes, error))
        return false;
    SetWord(bytes.data() + 0x01, time);
    SetWord(bytes.data() + 0x03, date);
    return WriteEntry(chain, bytes, 0, error);
}

bool Mb02FileSystem::SetName(const std::string& name, std::string& error)
{
    if (!Writable(error))
        return false;
    if (name.size() > 10)
    {
        error = "an MB-02 disk name has at most 10 bytes";
        return false;
    }
    uint8_t s[kSector];
    if (!_reader(0, s))
    {
        error = "the boot sector does not read";
        return false;
    }
    PutName(s + 0x26, name);
    if (!WriteSystem(0, s, error))
        return false;
    _boot.name = name;
    return true;
}
}  // namespace unrealdisk
