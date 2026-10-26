#include "unrealdisk/isdosfs.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
constexpr size_t kBlock = 256;
constexpr uint8_t kExists = 0x01;
constexpr uint8_t kReadProtected = 0x04;
constexpr uint8_t kWriteProtected = 0x08;
constexpr uint8_t kHidden = 0x10;
constexpr uint8_t kCatalogue = 0x20;
constexpr uint8_t kContinuous = 0x40;
constexpr uint8_t kDeleteProtected = 0x80;
constexpr uint32_t kMaxEntries = 128;   // a catalogue holds at most 128 descriptors, entry 0 included
constexpr uint32_t kMaxSegments = 85;   // 1 + 85 * 3 = 256 bytes of a segment table
constexpr size_t kMaxDepth = 16;        // iS-DOS nests 6 levels; deeper is damage

uint16_t Word(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

// CP866 #80-#FF as Unicode code points
constexpr uint16_t kCp866[128] = {
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
    0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};

/// CP866 text as UTF-8: control bytes as \xNN, '/' as '_', trailing blanks dropped
std::string Text(const uint8_t* raw, size_t size)
{
    while (size > 0 && raw[size - 1] == ' ')
        size--;
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; i++)
    {
        const uint8_t c = raw[i];
        if (c < 0x20 || c == 0x7F)
        {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
        else if (c == '/')
            out += '_';
        else if (c < 0x80)
            out += static_cast<char>(c);
        else
        {
            const uint16_t u = kCp866[c - 0x80];
            if (u < 0x800)
            {
                out += static_cast<char>(0xC0 | u >> 6);
                out += static_cast<char>(0x80 | (u & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xE0 | u >> 12);
                out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (u & 0x3F));
            }
        }
    }
    return out;
}

bool SameName(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

std::vector<std::string> SplitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string part;
    for (const char c : path)
    {
        if (c == '/' || c == '\\')
        {
            if (!part.empty())
                parts.push_back(part);
            part.clear();
        }
        else
            part += c;
    }
    if (!part.empty())
        parts.push_back(part);
    return parts;
}

IsdosFile Entry(const uint8_t* e)
{
    IsdosFile f;
    std::memcpy(f.rawName, e, 11);
    const std::string name = Text(e, 8);
    const std::string ext = Text(e + 8, 3);
    f.name = (name.empty() ? "_" : name) + (ext.empty() ? "" : "." + ext);
    f.status = e[11];
    f.special = e[19];
    f.system = f.status == 0xFF && f.special == 0xFF;
    f.directory = (f.status & kCatalogue) && !f.system;
    f.date = Word(e + 30);
    f.time = Word(e + 28);
    if (f.directory)
    {
        f.length = Word(e + 14);
        f.first = Word(e + 19);
    }
    else
    {
        f.load = Word(e + 12);
        f.length = static_cast<uint32_t>(e[14] | e[15] << 8 | e[16] << 16);
        f.first = Word(e + 17);
        f.checksum = Word(e + 26);
    }
    return f;
}
}  // namespace

std::string IsdosFile::Attributes() const
{
    std::string a;
    a += (status & kContinuous) ? '.' : 'S';
    a += (status & kReadProtected) ? 'R' : '.';
    a += (status & kWriteProtected) ? 'W' : '.';
    a += (status & kHidden) ? 'H' : '.';
    a += (status & kDeleteProtected) ? 'D' : '.';
    return a;
}

IsdosFileSystem::IsdosFileSystem(BlockReader reader) : _reader(std::move(reader))
{
    uint8_t b[kBlock];
    if (!_reader || !_reader(0, b) || !IsDescriptor(b))
        return;
    _descriptor = true;
    _boot.chic = std::memcmp(b + 13, "DSK", 3) == 0;
    // Chic: an 11-byte name, but many Chic-era disks keep an 8-byte name and carry DSK at both +10 and +13
    const bool longName = _boot.chic && std::memcmp(b + 10, "DSK", 3) != 0;
    _boot.title = Text(b + 2, longName ? 11 : 8);
    _boot.blocks = Word(b + 18);
    _boot.root = Word(b + 20);
    _boot.cylinders = b[22];
    _boot.diskType = b[23];
    _boot.sectorBlocks = b[24];
    _boot.sectors = b[25];
    _boot.date = Word(b + 30);
    _boot.sectorTable.assign(b + 64, b + 64 + (_boot.sectors < 16 ? _boot.sectors : 16));
}

bool IsdosFileSystem::IsDescriptor(const uint8_t* b)
{
    const uint16_t blocks = Word(b + 18);
    const uint16_t root = Word(b + 20);
    if ((b[24] != 1 && b[24] != 2 && b[24] != 4) || b[25] < 1 || b[25] > 18 || blocks < 3 || root == 0 || root >= blocks)
        return false;
    return std::memcmp(b + 13, "DSK", 3) == 0 || std::memcmp(b + 10, "DSK", 3) == 0;
}

IsdosFileSystem IsdosFileSystem::OnDisk(DiskImage& image)
{
    // Geometry from the descriptor in sector id 1 of cylinder 0 head 0 (XiSD Formats/FDI/filer.cpp)
    struct Map
    {
        DiskImage* image = nullptr;
        uint32_t blocksPerSector = 0;
        uint32_t perTrack = 0;
        uint32_t heads = 1;
        std::vector<uint8_t> table;
    };
    auto map = std::make_shared<Map>();
    map->image = &image;
    DiskImage::Track* track0 = image.getTrackForCylinderAndSide(0, 0);
    const DiskImage::Sector* s1 = track0 ? track0->findSector(1) : nullptr;
    if (s1 && s1->data && s1->dataSize >= kBlock && IsDescriptor(s1->data))
    {
        map->blocksPerSector = s1->data[24];
        map->perTrack = map->blocksPerSector * s1->data[25];
        const uint8_t entries = s1->data[25] < 16 ? s1->data[25] : 16;
        map->table.assign(s1->data + 64, s1->data + 64 + entries);
        map->heads = image.getSides() ? image.getSides() : 1;
    }
    return IsdosFileSystem([map](uint32_t block, uint8_t* dst) {
        if (map->perTrack == 0)
            return false;
        const uint32_t track = block / map->perTrack;
        const uint32_t position = (block % map->perTrack) / map->blocksPerSector;
        const uint32_t sub = block % map->blocksPerSector;
        const uint32_t cylinder = track / map->heads;
        if (position >= map->table.size() || cylinder > 0xFF)
            return false;
        DiskImage::Track* t = map->image->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder),
                                                                     static_cast<uint8_t>(track % map->heads));
        const DiskImage::Sector* s = t ? t->findSector(static_cast<uint8_t>(map->table[position] + 1)) : nullptr;
        if (!s || !s->data || s->dataSize < map->blocksPerSector * kBlock)
            return false;
        std::memcpy(dst, s->data + sub * kBlock, kBlock);
        return true;
    });
}

IsdosFileSystem IsdosFileSystem::OnBytes(const uint8_t* data, size_t size)
{
    return IsdosFileSystem([data, size](uint32_t block, uint8_t* dst) {
        const size_t offset = static_cast<size_t>(block) * kBlock;
        if (offset + kBlock > size)
            return false;
        std::memcpy(dst, data + offset, kBlock);
        return true;
    });
}

bool IsdosFileSystem::Block(uint32_t block, uint8_t* dst) const
{
    return block < _boot.blocks && _reader(block, dst);
}

bool IsdosFileSystem::Runs(uint8_t status, uint16_t first, uint16_t segmentTable, uint32_t size, std::vector<Run>& runs,
                           std::string& error) const
{
    runs.clear();
    const uint32_t need = (size + kBlock - 1) / kBlock;
    if (status & kContinuous)
    {
        if (need)
            runs.push_back({first, need});
        return true;
    }
    uint8_t t[kBlock];
    if (!Block(segmentTable, t))
    {
        error = "segment table block " + std::to_string(segmentTable) + " unreadable";
        return false;
    }
    if (t[0] > kMaxSegments)
    {
        error = "segment table block " + std::to_string(segmentTable) + ": " + std::to_string(t[0]) + " segments";
        return false;
    }
    uint32_t have = 0;
    for (uint32_t k = 0; k < t[0] && have < need; k++)
    {
        runs.push_back({Word(t + 1 + 3 * k), t[3 + 3 * k]});
        have += t[3 + 3 * k];
    }
    if (have < need)
    {
        error = "segment table block " + std::to_string(segmentTable) + ": " + std::to_string(have) + " blocks for " +
                std::to_string(size) + " bytes";
        return false;
    }
    return true;
}

bool IsdosFileSystem::ReadRuns(const std::vector<Run>& runs, uint32_t size, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    data.reserve(size);
    uint8_t b[kBlock];
    for (const Run& run : runs)
    {
        for (uint32_t k = 0; k < run.count && data.size() < size; k++)
        {
            const uint32_t block = run.start + k;
            if (!Block(block, b))
            {
                error = "block " + std::to_string(block) + (block >= _boot.blocks ? " outside the device" : " unreadable");
                return false;
            }
            const size_t take = size - data.size() < kBlock ? size - data.size() : kBlock;
            data.insert(data.end(), b, b + take);
        }
    }
    return true;
}

bool IsdosFileSystem::OpenCatalogue(uint16_t first, Catalogue& out, std::string& error) const
{
    out = {};
    uint8_t b[kBlock];
    if (first == 0)
    {
        error = "catalogue at block 0 (the device descriptor): no catalogue blocks";
        return false;
    }
    if (!Block(first, b))
    {
        error = "catalogue block " + std::to_string(first) + (first >= _boot.blocks ? " outside the device" : " unreadable");
        return false;
    }
    const uint8_t status = b[11];
    const uint32_t nfile = b[21];
    if (!(status & kCatalogue))
    {
        error = "catalogue block " + std::to_string(first) + ": status #" + std::to_string(status) + " is no catalogue";
        return false;
    }
    if (nfile < 1 || nfile > kMaxEntries)
    {
        error = "catalogue block " + std::to_string(first) + ": " + std::to_string(nfile) + " entries";
        return false;
    }
    // Entries 1 .. NFILE-1 through the catalogue's runs (XiSD); CSIZE is no limit, but the stale count reads up to it
    const uint32_t size = nfile * 32;
    const uint32_t cs = Word(b + 14);
    const uint32_t span = std::min<uint32_t>(kMaxEntries * 32, std::max(size, cs));
    std::vector<uint8_t> raw(b, b + kBlock);
    if (span > kBlock)
    {
        std::vector<Run> runs;
        std::vector<uint8_t> body;
        std::string why;
        if (Runs(status, Word(b + 19), Word(b + 17), span, runs, why) && ReadRuns(runs, span, body, why))
            raw = std::move(body);
        else if (size > kBlock)
        {
            why.clear();
            if (Runs(status, Word(b + 19), Word(b + 17), size, runs, why) && ReadRuns(runs, size, body, why))
                raw = std::move(body);
            else
            {
                if (body.size() > raw.size())
                    raw = std::move(body);
                out.error = "catalogue block " + std::to_string(first) + ": " + why + "; " + std::to_string(raw.size() / 32) +
                            " of " + std::to_string(nfile) + " entries read";
            }
        }
    }
    const uint32_t held = static_cast<uint32_t>(raw.size() / 32);
    for (uint32_t i = 1; i < nfile && i < held; i++)
        if (raw[i * 32 + 11] & kExists)
            out.entries.push_back(Entry(raw.data() + i * 32));
    for (uint32_t i = nfile; i < held; i++)
    {
        const uint8_t* e = raw.data() + i * 32;
        bool printable = true;
        for (int k = 0; k < 11; k++)
            printable = printable && e[k] >= 0x20;
        if ((e[11] & kExists) && printable)
            out.stale++;
    }
    return true;
}

bool IsdosFileSystem::RootOk() const
{
    if (!_descriptor)
        return false;
    uint8_t b[kBlock];
    Catalogue c;
    std::string error;
    return OpenCatalogue(_boot.root, c, error) && Block(_boot.root, b) && Word(b + 19) == _boot.root &&
           Word(b + 12) == _boot.root;
}

bool IsdosFileSystem::IsIsdos() const
{
    return RootOk();
}

bool IsdosFileSystem::List(std::vector<IsdosFile>& out, std::vector<std::string>* notes, std::string& error) const
{
    out.clear();
    if (!RootOk())
    {
        error = _descriptor ? "the iS-DOS main catalogue does not open" : "no iS-DOS device descriptor in block 0";
        return false;
    }
    auto note = [&](const std::string& text) {
        if (notes)
            notes->push_back(text);
    };
    Catalogue root;
    OpenCatalogue(_boot.root, root, error);
    if (!root.error.empty())
        note(root.error);
    if (root.stale)
        note("/: " + std::to_string(root.stale) + " entries past NFILE still marked as existing (stale, not listed)");

    // Every catalogue opened once: a loop or a cross-link is cut, so the walk ends
    std::map<uint16_t, std::string> opened = {{_boot.root, "/"}};
    struct Level
    {
        std::vector<IsdosFile> entries;
        size_t next = 0;
        std::string prefix;
        std::vector<uint16_t> chain;
    };
    std::vector<Level> stack;
    stack.push_back({std::move(root.entries), 0, "", {_boot.root}});
    while (!stack.empty())
    {
        Level& level = stack.back();
        if (level.next >= level.entries.size())
        {
            stack.pop_back();
            continue;
        }
        IsdosFile f = level.entries[level.next++];
        f.path = level.prefix + f.name;
        if (!f.directory)
        {
            out.push_back(std::move(f));
            continue;
        }
        Catalogue sub;
        bool open = false;
        if (std::find(level.chain.begin(), level.chain.end(), f.first) != level.chain.end())
            f.error = "catalogue loop: block " + std::to_string(f.first) + " is an ancestor";
        else if (opened.count(f.first))
            f.error = "catalogue block " + std::to_string(f.first) + " already listed as " + opened[f.first];
        else if (level.chain.size() > kMaxDepth)
            f.error = "nested deeper than " + std::to_string(kMaxDepth) + " levels";
        else
        {
            opened[f.first] = f.path;
            open = OpenCatalogue(f.first, sub, f.error);
            if (open && !sub.error.empty())
                f.error = sub.error;
            if (sub.stale)
                note(f.path + ": " + std::to_string(sub.stale) + " entries past NFILE still marked as existing (stale, not listed)");
        }
        if (!f.error.empty())
            note(f.path + ": " + f.error);
        std::vector<uint16_t> chain = level.chain;
        chain.push_back(f.first);
        const std::string prefix = f.path + "/";
        out.push_back(std::move(f));
        if (open && !sub.entries.empty())
            stack.push_back({std::move(sub.entries), 0, prefix, std::move(chain)});
    }
    return true;
}

bool IsdosFileSystem::ListDirectory(const std::string& path, std::vector<IsdosFile>& out, std::string& error) const
{
    out.clear();
    uint16_t first = _boot.root;
    std::string prefix;
    if (!SplitPath(path).empty())
    {
        IsdosFile dir;
        if (!Find(path, dir, error))
            return false;
        if (!dir.directory)
        {
            error = dir.path + " is no catalogue";
            return false;
        }
        first = dir.first;
        prefix = dir.path + "/";
    }
    else if (!RootOk())
    {
        error = _descriptor ? "the iS-DOS main catalogue does not open" : "no iS-DOS device descriptor in block 0";
        return false;
    }
    Catalogue c;
    if (!OpenCatalogue(first, c, error))
        return false;
    for (IsdosFile& f : c.entries)
    {
        f.path = prefix + f.name;
        out.push_back(std::move(f));
    }
    return true;
}

bool IsdosFileSystem::Find(const std::string& path, IsdosFile& out, std::string& error) const
{
    const std::vector<std::string> parts = SplitPath(path);
    if (!RootOk())
    {
        error = _descriptor ? "the iS-DOS main catalogue does not open" : "no iS-DOS device descriptor in block 0";
        return false;
    }
    if (parts.empty())
    {
        error = "name a file";
        return false;
    }
    uint16_t first = _boot.root;
    std::set<uint16_t> seen = {first};
    std::string prefix;
    for (size_t i = 0; i < parts.size(); i++)
    {
        Catalogue c;
        if (!OpenCatalogue(first, c, error))
            return false;
        const IsdosFile* hit = nullptr;
        for (const IsdosFile& f : c.entries)
            if (SameName(f.name, parts[i]))
            {
                hit = &f;
                break;
            }
        if (!hit)
        {
            error = "no " + prefix + parts[i] + " on the iS-DOS device";
            return false;
        }
        if (i + 1 == parts.size())
        {
            out = *hit;
            out.path = prefix + hit->name;
            return true;
        }
        if (!hit->directory)
        {
            error = prefix + hit->name + " is no catalogue";
            return false;
        }
        if (!seen.insert(hit->first).second)
        {
            error = "catalogue loop at " + prefix + hit->name;
            return false;
        }
        first = hit->first;
        prefix += hit->name + "/";
    }
    return false;
}

bool IsdosFileSystem::Read(const IsdosFile& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    if (file.directory)
    {
        error = file.path + " is a catalogue";
        return false;
    }
    std::vector<Run> runs;
    return Runs(file.status, file.first, file.first, file.length, runs, error) && ReadRuns(runs, file.length, data, error);
}

bool IsdosFileSystem::FreeBlocks(uint32_t& free) const
{
    free = 0;
    if (!_descriptor)
        return false;
    std::vector<uint8_t> bits;
    uint8_t b[kBlock];
    for (uint32_t k = 1; bits.size() < (_boot.blocks + 7u) / 8; k++)
    {
        if (!Block(k, b))
            return false;
        bits.insert(bits.end(), b, b + kBlock);
    }
    for (uint32_t i = 0; i < _boot.blocks; i++)
        if (!(bits[i >> 3] & (0x80 >> (i & 7))))
            free++;
    return true;
}
}  // namespace unrealdisk
