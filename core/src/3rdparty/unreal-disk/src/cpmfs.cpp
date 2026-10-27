#include "unrealdisk/cpmfs.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
constexpr uint32_t kEntry = 32;
constexpr uint32_t kExtent = 16384;
/// The ATM CP/M floppy: cylinder 0 (two tracks of 16 x 256 bytes) reserved
constexpr uint64_t kFloppyDirectory = 0x2000;
constexpr uint32_t kFloppyTrack = 16 * 256;

std::string Trimmed(const uint8_t* bytes, size_t n)
{
    std::string out;
    for (size_t i = 0; i < n; i++)
        out += static_cast<char>(bytes[i] & 0x7F);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/// The logical extent number: EX + 32 * S2
uint32_t Extent(const uint8_t* e)
{
    return (e[12] & 0x1F) + 32u * (e[14] & 0x3F);
}

std::vector<uint8_t> Key(const uint8_t* e)
{
    std::vector<uint8_t> key(12);
    key[0] = e[0];
    for (int i = 0; i < 11; i++)
        key[1 + i] = static_cast<uint8_t>(e[1 + i] & 0x7F);
    return key;
}

std::string Upper(std::string text)
{
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

bool Number(const std::string& text, uint32_t& out)
{
    if (text.empty() || text.size() > 9)
        return false;
    out = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        out = out * 10 + static_cast<uint32_t>(c - '0');
    }
    return true;
}

std::string BadName(const std::string& name)
{
    return "not a CP/M name NAME.TYP (1-8 and 0-3 characters, none of < > . , ; : = ? * [ ] | / \\ or a blank): " +
           name;
}

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
}  // namespace

CpmGeometry AtmCpmChannel::Geometry() const
{
    CpmGeometry g;
    const uint32_t perTrack = sectors * sectorSize / 128;
    if (perTrack == 0 || entries == 0 || records <= perTrack * reserved)
        return g;
    const uint32_t blocks = (records - perTrack * reserved) >> 4;
    if (blocks == 0 || blocks > 65536 ||
        static_cast<uint64_t>(entries) * kEntry > static_cast<uint64_t>(blocks) * CpmGeometry::kBlock)
        return g;
    g.blocks = blocks;
    g.entries = entries;
    return g;
}

CpmFileSystem CpmFileSystem::AtmFloppy(DiskImage& image)
{
    CpmGeometry geometry;
    geometry.blocks = 316;
    geometry.entries = 128;
    DiskImage* disk = &image;
    // The 256-byte sector holding byte `at` of the image and its track
    auto locate = [disk](uint64_t at, DiskImage::Track*& t) -> DiskImage::Sector* {
        const uint64_t track = at / kFloppyTrack;
        if (track / 2 > 0xFF)
            return nullptr;
        t = disk->getTrackForCylinderAndSide(static_cast<uint8_t>(track / 2), static_cast<uint8_t>(track % 2));
        DiskImage::Sector* s = t ? t->findSector(static_cast<uint8_t>(at % kFloppyTrack / 256 + 1)) : nullptr;
        return s && s->data && s->dataSize >= 256 ? s : nullptr;
    };
    CpmFileSystem fs(
        [locate](uint64_t offset, uint8_t* dst, uint32_t size) {
            uint64_t at = kFloppyDirectory + offset;
            while (size > 0)
            {
                DiskImage::Track* t = nullptr;
                const DiskImage::Sector* s = locate(at, t);
                if (!s)
                    return false;
                const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(256 - at % 256));
                std::memcpy(dst, s->data + at % 256, n);
                dst += n;
                at += n;
                size -= n;
            }
            return true;
        },
        geometry);
    // A write changes the sector in the image (its CRC and the track's dirty flag follow)
    fs.SetWriter([locate](uint64_t offset, const uint8_t* src, uint32_t size) {
        uint64_t at = kFloppyDirectory + offset;
        while (size > 0)
        {
            DiskImage::Track* t = nullptr;
            const DiskImage::Sector* s = locate(at, t);
            if (!s)
                return false;
            uint8_t sector[256];
            std::memcpy(sector, s->data, 256);
            const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(256 - at % 256));
            std::memcpy(sector + at % 256, src, n);
            t->writeSectorData(static_cast<uint8_t>(at % kFloppyTrack / 256), sector, 256);
            src += n;
            at += n;
            size -= n;
        }
        return true;
    });
    return fs;
}

bool CpmFileSystem::ParseAtmConfig(const uint8_t* sector, std::vector<AtmCpmChannel>& out)
{
    static const uint8_t signature[] = {0xC9, 0x00, 0x00, 0x55, 0xAA};
    if (std::memcmp(sector, signature, sizeof(signature)) != 0)
        return false;
    // Records up to the trailer at #7E: tag, length (tag and length included), body
    constexpr size_t kEnd = 0x7E;
    for (size_t p = sizeof(signature); p + 2 <= kEnd && sector[p] != 0;)
    {
        const uint8_t tag = sector[p];
        const uint8_t length = sector[p + 1];
        if (length < 2 || p + length > kEnd)
            break;
        // ASSIGN: drive, channel, the descriptor from DTYP on (22 bytes up to DDIRENT)
        if (tag == 3 && length >= 2 + 2 + 22)
        {
            const uint8_t* body = sector + p + 2;
            const uint8_t* d = body + 2;
            AtmCpmChannel c;
            c.drive = static_cast<char>('A' + (body[0] & 0x0F));
            c.driver = d[0];
            c.unit = d[1];
            c.heads = d[3];
            c.cylinders = d[5] | d[6] << 8;
            c.sectors = d[7];
            c.sectorSize = d[8] | d[9] << 8;
            c.reserved = d[10];
            c.startCylinder = d[11] | d[12] << 8;
            c.records = d[13] | d[14] << 8;
            c.entries = d[20] | d[21] << 8;
            out.push_back(c);
        }
        p += length;
    }
    return true;
}

bool CpmFileSystem::ParseCds(const std::string& text, AtmCpmChannel& out)
{
    AtmCpmChannel c;
    c.sectorSize = 512;
    bool driver = false, sectors = false, tracks = false, heads = false, dir = false, start = false;
    const std::string body = text.substr(0, text.find('\x1A'));
    size_t from = 0;
    while (from <= body.size())
    {
        size_t end = body.find_first_of("\r\n", from);
        if (end == std::string::npos)
            end = body.size();
        std::string line = body.substr(from, end - from);
        from = end + 1;
        line = Trim(line.substr(0, line.find(';')));
        if (line.empty())
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return false;
        const std::string field = Upper(Trim(line.substr(0, eq)));
        uint32_t value = 0;
        if (!Number(Trim(line.substr(eq + 1)), value))
            return false;
        // WORD n / BYTE n poke the binary descriptor; nothing of the layout depends on them
        if (field.rfind("WORD", 0) == 0 || field.rfind("BYTE", 0) == 0)
            continue;
        if (field == "DRIVER")
            c.driver = static_cast<uint8_t>(value), driver = true;
        else if (field == "UNIT")
            c.unit = static_cast<uint8_t>(value);
        else if (field == "SECTORS")
            c.sectors = value, sectors = true;
        else if (field == "TRACKS")
            c.cylinders = value, tracks = true;
        else if (field == "SECSIZE")
            c.sectorSize = value;
        else if (field == "HEADSF")
            c.heads = value, heads = true;
        else if (field == "SYS")
            c.reserved = value;
        else if (field == "DIR")
            c.entries = value, dir = true;
        else if (field == "PATBEG")
            c.startCylinder = value, start = true;
    }
    if (!driver || (c.driver == 4 && !(sectors && tracks && heads && dir && start)))
        return false;
    c.records = static_cast<uint32_t>(
        std::min<uint64_t>(static_cast<uint64_t>(c.cylinders) * c.heads * c.sectors * c.sectorSize / 128, UINT32_MAX));
    out = c;
    return true;
}

bool CpmFileSystem::Directory(std::vector<uint8_t>& out) const
{
    out.assign(static_cast<size_t>(_geometry.entries) * kEntry, 0);
    return _geometry.entries > 0 && _reader && _reader(0, out.data(), static_cast<uint32_t>(out.size()));
}

uint32_t CpmFileSystem::UsedPointers(const uint8_t* e) const
{
    const uint32_t mask = _geometry.Wide() ? 0 : 1;  // EXM
    const uint32_t records = (e[12] & mask) * 128 + std::min<uint8_t>(e[15], 128);
    const uint32_t slots = _geometry.Wide() ? 8 : 16;
    return std::min(slots, (records + 15) / 16);
}

uint32_t CpmFileSystem::Pointer(const uint8_t* e, uint32_t k) const
{
    return _geometry.Wide() ? (e[16 + 2 * k] | e[17 + 2 * k] << 8) : e[16 + k];
}

bool CpmFileSystem::IsCpm(std::string* why) const
{
    auto fail = [why](const std::string& reason) {
        if (why)
            *why = reason;
        return false;
    };
    std::vector<uint8_t> dir;
    if (_geometry.blocks <= _geometry.DirectoryBlocks() || !Directory(dir))
        return fail("the directory does not read");
    std::vector<bool> used(_geometry.blocks, false);
    uint32_t files = 0;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint8_t* e = dir.data() + i * kEntry;
        if (e[0] == 0xE5)
            continue;
        const std::string at = "entry " + std::to_string(i);
        if (e[0] > 15)
            return fail(at + ": user " + std::to_string(e[0]));
        // A blank name with a type happens (PASC_MT.TRD: "        .COM"); all eleven blank does not
        bool named = false;
        for (int k = 1; k < 12; k++)
        {
            if ((e[k] & 0x7F) < 0x20 || (e[k] & 0x7F) > 0x7E)
                return fail(at + ": no name");
            named = named || (e[k] & 0x7F) != ' ';
        }
        if (!named)
            return fail(at + ": no name");
        if (e[12] > 31 || e[13] != 0 || e[15] > 128)
            return fail(at + ": no extent");
        const uint32_t pointers = UsedPointers(e);
        for (uint32_t k = 0; k < pointers; k++)
        {
            const uint32_t block = Pointer(e, k);
            if (block < _geometry.DirectoryBlocks() || block >= _geometry.blocks)
                return fail(at + ": block " + std::to_string(block) + " off the volume");
            if (used[block])
                return fail(at + ": block " + std::to_string(block) + " used twice");
            used[block] = true;
        }
        files++;
    }
    return files > 0 ? true : fail("no file");
}

bool CpmFileSystem::List(std::vector<CpmFile>& out, std::string& error) const
{
    std::vector<uint8_t> dir;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    struct Last
    {
        uint32_t extent = 0;
        uint32_t first = UINT32_MAX;
    };
    std::map<std::array<uint8_t, 12>, size_t> index;
    std::vector<Last> last;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint8_t* e = dir.data() + i * kEntry;
        if (e[0] > 15)
            continue;
        std::vector<uint8_t> key = Key(e);
        std::array<uint8_t, 12> sorted;
        std::copy_n(key.begin(), sorted.size(), sorted.begin());
        auto [it, fresh] = index.emplace(sorted, out.size());
        if (fresh)
        {
            CpmFile f;
            f.user = e[0];
            f.name = Trimmed(e + 1, 8);
            f.type = Trimmed(e + 9, 3);
            f.attributes = static_cast<uint8_t>((e[9] >> 7) | (e[10] >> 7) << 1 | (e[11] >> 7) << 2);
            f.key = std::move(key);
            out.push_back(std::move(f));
            last.emplace_back();
        }
        CpmFile& f = out[it->second];
        Last& l = last[it->second];
        const uint32_t extent = Extent(e);
        const uint64_t size = static_cast<uint64_t>(extent) * kExtent + std::min<uint8_t>(e[15], 128) * 128u;
        if (f.extents == 0 || extent >= l.extent)
        {
            l.extent = extent;
            f.size = size;
        }
        if (extent < l.first)
        {
            l.first = extent;
            f.firstBlock = UsedPointers(e) ? Pointer(e, 0) : 0;
        }
        f.extents++;
    }
    return true;
}

bool CpmFileSystem::Read(const CpmFile& file, std::vector<uint8_t>& data, std::string& error) const
{
    std::vector<uint8_t> dir;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    data.assign(file.size, 0);
    const uint32_t mask = _geometry.Wide() ? 0 : 1;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint8_t* e = dir.data() + i * kEntry;
        if (e[0] > 15 || Key(e) != file.key)
            continue;
        // An entry holds extents (EX & ~EXM) on; an extent past the size (a damaged directory) adds nothing
        const uint64_t base = static_cast<uint64_t>(Extent(e) & ~mask) * kExtent;
        const uint32_t pointers = UsedPointers(e);
        for (uint32_t k = 0; k < pointers; k++)
        {
            const uint64_t at = base + static_cast<uint64_t>(k) * CpmGeometry::kBlock;
            if (at >= file.size)
                break;
            const uint32_t block = Pointer(e, k);
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(CpmGeometry::kBlock, file.size - at));
            if (block >= _geometry.blocks ||
                !_reader(static_cast<uint64_t>(block) * CpmGeometry::kBlock, data.data() + at, n))
            {
                error = "block " + std::to_string(block) + " of " + file.FullName() + " does not read";
                return false;
            }
        }
    }
    return true;
}

bool CpmFileSystem::FreeBlocks(uint32_t& out) const
{
    std::vector<uint8_t> dir;
    if (!Directory(dir))
        return false;
    std::vector<bool> used(_geometry.blocks, false);
    for (uint32_t b = 0; b < _geometry.DirectoryBlocks() && b < _geometry.blocks; b++)
        used[b] = true;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint8_t* e = dir.data() + i * kEntry;
        if (e[0] > 15)
            continue;
        for (uint32_t k = 0, n = UsedPointers(e); k < n; k++)
            if (Pointer(e, k) < _geometry.blocks)
                used[Pointer(e, k)] = true;
    }
    out = static_cast<uint32_t>(std::count(used.begin(), used.end(), false));
    return true;
}

bool CpmFileSystem::ValidName(const std::string& name, const std::string& type)
{
    if (name.empty() || name.size() > 8 || type.size() > 3)
        return false;
    for (const char c : name + type)
        if (c <= ' ' || c >= 0x7F || (c >= 'a' && c <= 'z') || std::strchr("<>.,;:=?*[]|/\\", c))
            return false;
    return true;
}

bool CpmFileSystem::Writable(std::string& error) const
{
    if (_writer)
        return true;
    error = "the CP/M volume is read only";
    return false;
}

bool CpmFileSystem::WriteDirectory(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after,
                                   std::string& error)
{
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint64_t at = static_cast<uint64_t>(i) * kEntry;
        if (std::memcmp(before.data() + at, after.data() + at, kEntry) == 0)
            continue;
        // Changed entries next to each other go in one write
        uint32_t n = 1;
        while (i + n < _geometry.entries &&
               std::memcmp(before.data() + at + n * kEntry, after.data() + at + n * kEntry, kEntry) != 0)
            n++;
        if (!_writer(at, after.data() + at, n * kEntry))
        {
            error = "the CP/M directory cannot be written";
            return false;
        }
        i += n - 1;
    }
    return true;
}

bool CpmFileSystem::Entries(const std::vector<uint8_t>& dir, const std::vector<uint8_t>& key,
                            std::vector<uint32_t>& out, std::string& error) const
{
    for (uint32_t i = 0; i < _geometry.entries; i++)
        if (dir[i * kEntry] <= 15 && Key(dir.data() + i * kEntry) == key)
            out.push_back(i);
    if (out.empty())
        error = "the file is not on the CP/M volume";
    return !out.empty();
}

bool CpmFileSystem::Write(uint8_t user, const std::string& name, const std::string& type,
                          const std::vector<uint8_t>& bytes, std::string& error)
{
    if (!Writable(error))
        return false;
    if (user > 15 || !ValidName(name, type))
    {
        error = BadName(name + (type.empty() ? "" : "." + type));
        return false;
    }
    std::vector<uint8_t> dir;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    std::vector<uint8_t> key(12, ' ');
    key[0] = user;
    std::memcpy(key.data() + 1, name.data(), name.size());
    std::memcpy(key.data() + 9, type.data(), type.size());

    // The older file's entries go free in the same directory write; its blocks are not reused for the new data
    std::vector<uint8_t> after = dir;
    std::vector<bool> used(_geometry.blocks, false);
    for (uint32_t b = 0; b < _geometry.DirectoryBlocks() && b < _geometry.blocks; b++)
        used[b] = true;
    std::vector<uint32_t> freeEntries;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        uint8_t* e = after.data() + i * kEntry;
        if (e[0] > 15)
        {
            freeEntries.push_back(i);
            continue;
        }
        if (Key(e) == key)
        {
            if (e[9] & 0x80)
            {
                error = Trimmed(e + 1, 8) + "." + Trimmed(e + 9, 3) + " is read only";
                return false;
            }
            e[0] = 0xE5;
            freeEntries.push_back(i);
        }
        for (uint32_t k = 0, n = UsedPointers(dir.data() + i * kEntry); k < n; k++)
            if (Pointer(e, k) < _geometry.blocks)
                used[Pointer(e, k)] = true;
    }

    const uint32_t records = static_cast<uint32_t>((bytes.size() + 127) / 128);
    const uint32_t needed = (records * 128 + CpmGeometry::kBlock - 1) / CpmGeometry::kBlock;
    const uint32_t perEntry = _geometry.Wide() ? 8 : 16;
    const uint32_t entriesNeeded = std::max<uint32_t>(1, (needed + perEntry - 1) / perEntry);
    std::vector<uint32_t> blocks;
    for (uint32_t b = 0; b < _geometry.blocks && blocks.size() < needed; b++)
        if (!used[b])
            blocks.push_back(b);
    if (blocks.size() < needed)
    {
        error =
            "not enough free blocks: " + std::to_string(needed) + " needed, " + std::to_string(blocks.size()) + " free";
        return false;
    }
    if (freeEntries.size() < entriesNeeded)
    {
        error = "the CP/M directory is full";
        return false;
    }

    std::vector<uint8_t> block(CpmGeometry::kBlock);
    for (uint32_t k = 0; k < needed; k++)
    {
        const size_t at = static_cast<size_t>(k) * CpmGeometry::kBlock;
        const size_t n = std::min<size_t>(CpmGeometry::kBlock, bytes.size() - at);
        std::fill(block.begin(), block.end(), static_cast<uint8_t>(0x1A));
        std::memcpy(block.data(), bytes.data() + at, n);
        if (!_writer(static_cast<uint64_t>(blocks[k]) * CpmGeometry::kBlock, block.data(), CpmGeometry::kBlock))
        {
            error = "block " + std::to_string(blocks[k]) + " cannot be written";
            return false;
        }
    }

    // An entry holds perEntry blocks: one logical extent of 16 KB (16-bit pointers) or two (8-bit); EX is its last
    // logical extent, RC the records in that one
    const uint32_t extentsPerEntry = perEntry * CpmGeometry::kBlock / kExtent;
    const uint32_t recordsPerEntry = perEntry * CpmGeometry::kBlock / 128;
    for (uint32_t k = 0; k < entriesNeeded; k++)
    {
        uint8_t* e = after.data() + freeEntries[k] * kEntry;
        std::memset(e, 0, kEntry);
        std::memcpy(e, key.data(), key.size());
        const uint32_t inEntry = k + 1 == entriesNeeded ? records - k * recordsPerEntry : recordsPerEntry;
        const uint32_t extent = k * extentsPerEntry + (inEntry ? (inEntry - 1) / 128 : 0);
        e[12] = static_cast<uint8_t>(extent & 0x1F);
        e[14] = static_cast<uint8_t>(extent >> 5);
        e[15] = static_cast<uint8_t>(inEntry - extent % extentsPerEntry * 128);
        for (uint32_t n = 0; n < perEntry && k * perEntry + n < blocks.size(); n++)
        {
            const uint32_t b = blocks[k * perEntry + n];
            if (_geometry.Wide())
            {
                e[16 + 2 * n] = static_cast<uint8_t>(b);
                e[17 + 2 * n] = static_cast<uint8_t>(b >> 8);
            }
            else
                e[16 + n] = static_cast<uint8_t>(b);
        }
    }
    return WriteDirectory(dir, after, error);
}

bool CpmFileSystem::Delete(const CpmFile& file, std::string& error)
{
    std::vector<uint8_t> dir;
    std::vector<uint32_t> entries;
    if (!Writable(error))
        return false;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    if (!Entries(dir, file.key, entries, error))
        return false;
    if (file.attributes & CpmFile::kReadOnly)
    {
        error = file.FullName() + " is read only";
        return false;
    }
    std::vector<uint8_t> after = dir;
    for (const uint32_t i : entries)
        after[i * kEntry] = 0xE5;
    return WriteDirectory(dir, after, error);
}

bool CpmFileSystem::Rename(const CpmFile& file, uint8_t user, const std::string& name, const std::string& type,
                           std::string& error)
{
    if (!Writable(error))
        return false;
    if (user > 15 || !ValidName(name, type))
    {
        error = BadName(name + (type.empty() ? "" : "." + type));
        return false;
    }
    std::vector<uint8_t> dir;
    std::vector<uint32_t> entries;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    if (!Entries(dir, file.key, entries, error))
        return false;
    if (file.attributes & CpmFile::kReadOnly)
    {
        error = file.FullName() + " is read only";
        return false;
    }
    std::vector<uint8_t> key(12, ' ');
    key[0] = user;
    std::memcpy(key.data() + 1, name.data(), name.size());
    std::memcpy(key.data() + 9, type.data(), type.size());
    std::vector<uint32_t> taken;
    std::string ignored;
    if (key != file.key && Entries(dir, key, taken, ignored))
    {
        error = name + (type.empty() ? "" : "." + type) + " is there already";
        return false;
    }
    std::vector<uint8_t> after = dir;
    for (const uint32_t i : entries)
    {
        uint8_t* e = after.data() + i * kEntry;
        e[0] = user;
        for (int k = 0; k < 11; k++)
            e[1 + k] = static_cast<uint8_t>((e[1 + k] & 0x80) | key[1 + k]);
    }
    return WriteDirectory(dir, after, error);
}

bool CpmFileSystem::SetAttributes(const CpmFile& file, uint8_t attributes, std::string& error)
{
    if (!Writable(error))
        return false;
    std::vector<uint8_t> dir;
    std::vector<uint32_t> entries;
    if (!Directory(dir))
    {
        error = "the CP/M directory does not read";
        return false;
    }
    if (!Entries(dir, file.key, entries, error))
        return false;
    std::vector<uint8_t> after = dir;
    for (const uint32_t i : entries)
        for (int k = 0; k < 3; k++)
        {
            uint8_t& b = after[i * kEntry + 9 + k];
            b = static_cast<uint8_t>((b & 0x7F) | ((attributes >> k) & 1 ? 0x80 : 0));
        }
    return WriteDirectory(dir, after, error);
}
}  // namespace unrealdisk
