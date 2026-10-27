#include "unrealdisk/cpmfs.h"

#include <algorithm>
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
    return CpmFileSystem(
        [disk](uint64_t offset, uint8_t* dst, uint32_t size) {
            uint64_t at = kFloppyDirectory + offset;
            while (size > 0)
            {
                const uint64_t track = at / kFloppyTrack;
                const uint32_t inTrack = static_cast<uint32_t>(at % kFloppyTrack);
                if (track / 2 > 0xFF)
                    return false;
                DiskImage::Track* t =
                    disk->getTrackForCylinderAndSide(static_cast<uint8_t>(track / 2), static_cast<uint8_t>(track % 2));
                const DiskImage::Sector* s = t ? t->findSector(static_cast<uint8_t>(inTrack / 256 + 1)) : nullptr;
                if (!s || !s->data || s->dataSize < 256)
                    return false;
                const uint32_t n = std::min<uint32_t>(size, 256 - inTrack % 256);
                std::memcpy(dst, s->data + inTrack % 256, n);
                dst += n;
                at += n;
                size -= n;
            }
            return true;
        },
        geometry);
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
    std::map<std::vector<uint8_t>, size_t> index;
    std::vector<Last> last;
    for (uint32_t i = 0; i < _geometry.entries; i++)
    {
        const uint8_t* e = dir.data() + i * kEntry;
        if (e[0] > 15)
            continue;
        std::vector<uint8_t> key = Key(e);
        auto [it, fresh] = index.emplace(key, out.size());
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
}  // namespace unrealdisk
