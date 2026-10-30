#include "unrealdisk/mb01fs.h"

#include <algorithm>
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
constexpr uint16_t kProgramAddress = 23755;  ///< a program's body address as BS-DOS saves one (as on MB-02)
constexpr uint16_t kListEnd = kSystem | kLast | 1024;  ///< a list's last sector

void SetWord(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

/// `name` in 10 bytes, blanks after it
void PutName(uint8_t* p, const std::string& name)
{
    std::memset(p, ' ', 10);
    std::memcpy(p, name.data(), std::min<size_t>(name.size(), 10));
}

bool ValidName(const std::string& name, std::string& error)
{
    if (name.empty() || name.size() > 10 || name.back() == ' ')
    {
        error = "an MB-01 name has 1-10 bytes (no blank at its end): " + name;
        return false;
    }
    return true;
}

/// A file-list slot that holds no file: deleted, or neither header nor body
bool FreeSlot(const uint8_t* e)
{
    return (e[5] & Mb01FileSystem::kDeleted) || !(e[5] & (Mb01FileSystem::kBody | Mb01FileSystem::kHeader));
}
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
    Mb01FileSystem fs(
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
    // A write changes the sector in the image (its CRC and the track's dirty flag follow)
    fs.SetWriter([find, disk, spt](uint32_t lsn, const uint8_t* src) {
        if (spt == 0 || lsn / (2 * spt) > 0xFF)
            return false;
        const uint32_t cylinder = lsn / (2 * spt);
        const auto side = static_cast<uint8_t>(lsn / spt % 2);
        const auto id = static_cast<uint8_t>(lsn % spt + 1);
        if (!find(cylinder, side, id))
            return false;
        disk->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), side)
            ->writeSectorData(static_cast<uint8_t>(id - 1), src, kSector);
        return true;
    });
    return fs;
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
    // Where each copy lies, walked as above through the FAT now read
    for (uint32_t copy = 0; copy < 2; copy++)
    {
        _fatAt[copy].clear();
        uint32_t lsn = copy ? _boot.fat2 : _boot.fat1;
        for (uint16_t k = 0; k < _boot.fatSectors; k++)
        {
            _fatAt[copy].push_back(lsn);
            const uint16_t next = Fat(lsn);
            lsn = 2 * static_cast<size_t>(lsn) + 1 < (k + 1u) * kSector && (next & kKind) == kSystem && !(next & kLast)
                      ? next & kLsn
                      : lsn + 1;
        }
    }
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

bool Mb01FileSystem::ChainBytes(uint32_t first, std::vector<uint8_t>& out, std::string& error,
                                std::vector<uint32_t>* sectors) const
{
    out.clear();
    if (sectors)
        sectors->clear();
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
        if (sectors)
            sectors->push_back(lsn);
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

bool Mb01FileSystem::Writable(std::string& error) const
{
    if (_writer && !_fat.empty())
        return true;
    error = "the MB-01 volume is read only";
    return false;
}

void Mb01FileSystem::SetFat(uint32_t lsn, uint16_t value)
{
    if (2 * static_cast<size_t>(lsn) + 1 < _fat.size())
        SetWord(_fat.data() + 2 * static_cast<size_t>(lsn), value);
}

bool Mb01FileSystem::WriteFat(std::string& error)
{
    for (uint16_t k = 0; k < _boot.fatSectors; k++)
        for (const std::vector<uint32_t>& at : _fatAt)
            if (!_writer(at[k], _fat.data() + static_cast<size_t>(k) * kSector))
            {
                error = "FAT sector " + std::to_string(at[k]) + " does not write";
                return false;
            }
    return true;
}

bool Mb01FileSystem::WriteSystem(uint32_t lsn, const uint8_t* data, std::string& error)
{
    if (_writer(_bootLsn + 1, data) && _writer(lsn, data))
        return true;
    error = "sector " + std::to_string(lsn) + " does not write";
    return false;
}

bool Mb01FileSystem::Allocate(uint32_t count, uint32_t last, std::vector<uint32_t>& out, std::string& error)
{
    out.clear();
    // Not LSN 0: a link to it would read as a free word
    const uint32_t end = std::min<uint32_t>(_boot.Sectors(), kLsn + 1);
    for (uint32_t lsn = 1; lsn < end && out.size() < count; lsn++)
        if (Fat(lsn) == 0)
            out.push_back(lsn);
    if (out.size() < count)
    {
        error =
            "not enough free sectors: " + std::to_string(count) + " needed, " + std::to_string(out.size()) + " free";
        return false;
    }
    for (size_t k = 0; k < out.size(); k++)
        SetFat(out[k], k + 1 < out.size() ? static_cast<uint16_t>(out[k + 1]) : static_cast<uint16_t>(kLast | last));
    return true;
}

void Mb01FileSystem::FreeChain(uint32_t first)
{
    // Along a normal chain while it goes on; a loop is cut after as many steps as the disk has sectors
    uint32_t lsn = first;
    for (uint32_t steps = 0; lsn > 0 && lsn < _boot.Sectors() && steps < _boot.Sectors(); steps++)
    {
        const uint16_t word = Fat(lsn);
        if (word == 0 || (word & (kDeletedSector | kKind)))
            break;
        SetFat(lsn, 0);
        if (word & kLast)
            break;
        lsn = word & kLsn;
    }
}

bool Mb01FileSystem::ListBytes(uint32_t first, std::vector<uint32_t>& chain, std::vector<uint8_t>& bytes,
                               std::string& error) const
{
    return ChainBytes(first, bytes, error, &chain);
}

bool Mb01FileSystem::Grow(std::vector<uint32_t>& chain, std::vector<uint8_t>& bytes, std::string& error)
{
    std::vector<uint32_t> added;
    if (!Allocate(1, kSector, added, error))
        return false;
    SetFat(added[0], kListEnd);
    SetFat(chain.back(), static_cast<uint16_t>(kSystem | added[0]));
    // The old last sector is the list's in full now: its bytes past the count are zeroes
    const bool partial = bytes.size() < chain.size() * kSector;
    chain.push_back(added[0]);
    bytes.resize(chain.size() * kSector, 0);
    return (!partial || WriteAt(chain, bytes, (chain.size() - 2) * kSector, error)) &&
           WriteAt(chain, bytes, (chain.size() - 1) * kSector, error);
}

bool Mb01FileSystem::WriteAt(const std::vector<uint32_t>& chain, const std::vector<uint8_t>& bytes, size_t at,
                             std::string& error)
{
    const size_t sector = at / kSector;
    std::vector<uint8_t> s(kSector, 0);
    std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(sector * kSector),
              bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), (sector + 1) * kSector)), s.begin());
    return WriteSystem(chain[sector], s.data(), error);
}

bool Mb01FileSystem::Write(uint8_t dir, const std::string& name, const std::vector<uint8_t>& bytes,
                           const WriteOptions& options, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    std::vector<Mb02Directory> dirs;
    if (!Directories(dirs, error))
        return false;
    if (std::none_of(dirs.begin(), dirs.end(), [dir](const Mb02Directory& d) { return d.number == dir; }))
    {
        error = "no directory " + std::to_string(dir);
        return false;
    }
    if (bytes.size() > 0xFFFFFF)
    {
        error = "an MB-01 body holds at most 16 MB";
        return false;
    }
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!ListBytes(_boot.files, chain, list, error))
        return false;
    const std::vector<uint8_t> fat = _fat;
    auto fail = [this, &fat]() {
        _fat = fat;
        return false;
    };

    // The older file of the name goes: its slot is the new one's
    std::vector<Mb02File> files;
    if (!List(dir, files, error))
        return false;
    size_t entry = list.size() / kFileEntry;
    std::vector<size_t> older;
    for (const Mb02File& f : files)
        if ((f.flags & kHeader) && f.name == name)
        {
            if (f.flags & kBody)
                FreeChain(f.first);
            entry = f.entry;
            older.push_back(f.entry);
            list[f.entry * kFileEntry + 5] |= kDeleted;
        }
    for (size_t k = 0; k < list.size() / kFileEntry && entry * kFileEntry >= list.size(); k++)
        if (FreeSlot(list.data() + k * kFileEntry))
            entry = k;
    if (entry * kFileEntry >= list.size() && !Grow(chain, list, error))
        return fail();

    // The body, the FAT (both copies), then the entry
    std::vector<uint32_t> body;
    const auto count = static_cast<uint32_t>((bytes.size() + kSector - 1) / kSector);
    const uint32_t last = bytes.empty() ? 0 : static_cast<uint32_t>(bytes.size() - (count - 1) * size_t{kSector});
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
    uint8_t* e = list.data() + entry * kFileEntry;
    std::memset(e, 0, kFileEntry);
    e[0] = dir;
    SetWord(e + 1, options.time);
    SetWord(e + 3, options.date);
    e[5] = static_cast<uint8_t>(kHeader | (bytes.empty() ? 0 : kBody) | (options.type & 0x0F));
    PutName(e + 6, name);
    SetWord(e + 16, static_cast<uint32_t>(bytes.size() & 0xFFFF));
    SetWord(e + 18, options.start);
    SetWord(e + 20, options.param2);
    SetWord(e + 22, options.type == 0 ? kProgramAddress : options.start);
    const auto length = static_cast<uint32_t>(bytes.size());
    SetWord(e + 24, length);
    e[26] = static_cast<uint8_t>(length >> 16);
    e[27] = 0xFF;
    SetWord(e + 30, body.empty() ? 0 : body.front());
    if (!WriteAt(chain, list, entry * kFileEntry, error))
        return false;
    // Another older file of the name, in another list sector, deleted there too
    for (const size_t at : older)
        if (at * kFileEntry / kSector != entry * kFileEntry / kSector && !WriteAt(chain, list, at * kFileEntry, error))
            return false;
    return true;
}

bool Mb01FileSystem::Delete(const Mb02File& file, std::string& error)
{
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!Writable(error) || !ListBytes(_boot.files, chain, list, error))
        return false;
    list[file.entry * kFileEntry + 5] |= kDeleted;
    if (!WriteAt(chain, list, file.entry * kFileEntry, error))
        return false;
    if (file.flags & kBody)
        FreeChain(file.first);
    return WriteFat(error);
}

bool Mb01FileSystem::Rename(const Mb02File& file, uint8_t dir, const std::string& name, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    if (!(file.flags & kHeader))
    {
        error = "a file without a header has no name";
        return false;
    }
    std::vector<Mb02File> files;
    if (!List(dir, files, error))
        return false;
    for (const Mb02File& f : files)
        if ((f.flags & kHeader) && f.name == name && f.entry != file.entry)
        {
            error = name + " is there already";
            return false;
        }
    std::vector<Mb02Directory> dirs;
    if (!Directories(dirs, error))
        return false;
    if (std::none_of(dirs.begin(), dirs.end(), [dir](const Mb02Directory& d) { return d.number == dir; }))
    {
        error = "no directory " + std::to_string(dir);
        return false;
    }
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!ListBytes(_boot.files, chain, list, error))
        return false;
    uint8_t* e = list.data() + file.entry * kFileEntry;
    e[0] = dir;
    PutName(e + 6, name);
    return WriteAt(chain, list, file.entry * kFileEntry, error);
}

bool Mb01FileSystem::SetTime(const Mb02File& file, uint16_t date, uint16_t time, std::string& error)
{
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!Writable(error) || !ListBytes(_boot.files, chain, list, error))
        return false;
    SetWord(list.data() + file.entry * kFileEntry + 1, time);
    SetWord(list.data() + file.entry * kFileEntry + 3, date);
    return WriteAt(chain, list, file.entry * kFileEntry, error);
}

bool Mb01FileSystem::MakeDirectory(uint8_t parent, const std::string& name, uint16_t date, uint16_t time,
                                   std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    std::vector<Mb02Directory> dirs;
    if (!Directories(dirs, error))
        return false;
    if (std::none_of(dirs.begin(), dirs.end(), [parent](const Mb02Directory& d) { return d.number == parent; }))
    {
        error = "no directory " + std::to_string(parent);
        return false;
    }
    for (const Mb02Directory& d : dirs)
        if (d.number && d.parent == parent && d.name == name)
        {
            error = name + " is there already";
            return false;
        }
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!ListBytes(_boot.dirs, chain, list, error))
        return false;
    size_t number = 1;
    while (number < 256 && (number + 1) * kDirEntry <= list.size() && list[number * kDirEntry + 5])
        number++;
    if (number == 256)
    {
        error = "the directory list is full (255 directories)";
        return false;
    }
    const std::vector<uint8_t> fat = _fat;
    if ((number + 1) * kDirEntry > list.size())
    {
        if (!Grow(chain, list, error))
            return false;
        if (!WriteFat(error))
        {
            _fat = fat;
            return false;
        }
    }
    uint8_t* e = list.data() + number * kDirEntry;
    std::memset(e, 0, kDirEntry);
    e[0] = parent;
    SetWord(e + 1, time);
    SetWord(e + 3, date);
    e[5] = 1;
    PutName(e + 6, name);
    return WriteAt(chain, list, number * kDirEntry, error);
}

bool Mb01FileSystem::DeleteDirectory(uint8_t number, std::string& error)
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
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!ListBytes(_boot.dirs, chain, list, error))
        return false;
    std::memset(list.data() + number * kDirEntry, 0, kDirEntry);
    return WriteAt(chain, list, number * kDirEntry, error);
}

bool Mb01FileSystem::RenameDirectory(uint8_t number, uint8_t parent, const std::string& name, std::string& error)
{
    if (!Writable(error) || !ValidName(name, error))
        return false;
    if (number == 0)
    {
        error = "the root directory has no name";
        return false;
    }
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
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!ListBytes(_boot.dirs, chain, list, error))
        return false;
    uint8_t* e = list.data() + number * kDirEntry;
    e[0] = parent;
    PutName(e + 6, name);
    return WriteAt(chain, list, number * kDirEntry, error);
}

bool Mb01FileSystem::SetDirectoryTime(uint8_t number, uint16_t date, uint16_t time, std::string& error)
{
    std::vector<uint32_t> chain;
    std::vector<uint8_t> list;
    if (!Writable(error) || !ListBytes(_boot.dirs, chain, list, error))
        return false;
    if (number == 0)
    {
        error = "the root directory has no time";
        return false;
    }
    SetWord(list.data() + number * kDirEntry + 1, time);
    SetWord(list.data() + number * kDirEntry + 3, date);
    return WriteAt(chain, list, number * kDirEntry, error);
}

bool Mb01FileSystem::SetName(const std::string& name, std::string& error)
{
    if (!Writable(error))
        return false;
    if (name.size() > 10)
    {
        error = "an MB-01 disk name has at most 10 bytes";
        return false;
    }
    uint8_t s[kSector];
    if (!_reader(_bootLsn, s))
    {
        error = "the boot sector does not read";
        return false;
    }
    PutName(s + 4, name);
    if (!WriteSystem(_bootLsn, s, error))
        return false;
    _boot.name = name;
    return true;
}
}  // namespace unrealdisk
