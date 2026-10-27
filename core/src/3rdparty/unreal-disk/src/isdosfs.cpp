#include "unrealdisk/isdosfs.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/unicode.h"

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
            UnicodeHelper::AppendUtf8(out, UnicodeHelper::FromCodePage(CodePage::Cp866, c));
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

        /// The sector holding `block`, its track and the block's place in it
        DiskImage::Sector* Find(uint32_t block, DiskImage::Track*& track, uint32_t& sub, uint8_t& position) const
        {
            if (perTrack == 0)
                return nullptr;
            const uint32_t t = block / perTrack;
            const uint32_t at = (block % perTrack) / blocksPerSector;
            const uint32_t cylinder = t / heads;
            if (at >= table.size() || cylinder > 0xFF)
                return nullptr;
            track = image->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), static_cast<uint8_t>(t % heads));
            DiskImage::Sector* s = track ? track->findSector(static_cast<uint8_t>(table[at] + 1)) : nullptr;
            sub = block % blocksPerSector;
            position = table[at];
            return s && s->data && s->dataSize >= blocksPerSector * kBlock ? s : nullptr;
        }
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
    IsdosFileSystem fs([map](uint32_t block, uint8_t* dst) {
        DiskImage::Track* track = nullptr;
        uint32_t sub = 0;
        uint8_t position = 0;
        const DiskImage::Sector* s = map->Find(block, track, sub, position);
        if (!s)
            return false;
        std::memcpy(dst, s->data + sub * kBlock, kBlock);
        return true;
    });
    // A write changes the sector in the image (its CRC and the track's dirty flag follow)
    fs.SetWriter([map](uint32_t block, const uint8_t* src) {
        DiskImage::Track* track = nullptr;
        uint32_t sub = 0;
        uint8_t position = 0;
        const DiskImage::Sector* s = map->Find(block, track, sub, position);
        if (!s)
            return false;
        std::vector<uint8_t> sector(s->data, s->data + map->blocksPerSector * kBlock);
        std::memcpy(sector.data() + sub * kBlock, src, kBlock);
        track->writeSectorData(position, sector.data(), sector.size());
        return true;
    });
    return fs;
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

/// region <Writing>

namespace unrealdisk
{
namespace
{
constexpr uint8_t kSystem = 0xFF;
constexpr uint32_t kMaxLiveEntries = 127;   // NEFIL: the kernel refuses a 128th file (error 84)
constexpr uint32_t kMaxContinuousBlocks = 255;   // the kernel frees a continuous file's low 16 length bits' blocks
constexpr uint32_t kMaxSegmentBlocks = 255;
constexpr uint32_t kMaxCatalogueBlocks = kMaxEntries * 32 / kBlock;

void PutWord(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

bool Used(const std::vector<uint8_t>& bits, uint32_t block)
{
    return (bits[block >> 3] & (0x80 >> (block & 7))) != 0;
}

void Mark(std::vector<uint8_t>& bits, uint32_t block, bool used)
{
    if (used)
        bits[block >> 3] = static_cast<uint8_t>(bits[block >> 3] | (0x80 >> (block & 7)));
    else
        bits[block >> 3] = static_cast<uint8_t>(bits[block >> 3] & ~(0x80 >> (block & 7)));
}

/// The first run of `count` free blocks from block 0 (as the kernel's $distr looks), UINT32_MAX when there is none
uint32_t FirstFit(const std::vector<uint8_t>& bits, uint32_t blocks, uint32_t count)
{
    uint32_t run = 0;
    for (uint32_t b = 0; b < blocks; b++)
    {
        run = Used(bits, b) ? 0 : run + 1;
        if (run == count)
            return b + 1 - count;
    }
    return UINT32_MAX;
}

/// The name a listing gives for 11 descriptor bytes
std::string Listed(const uint8_t* name11)
{
    uint8_t e[32] = {};
    std::memcpy(e, name11, 11);
    return Entry(e).name;
}

/// The live entry of `c` (from slot 1 to NFILE - 1) named like `name11` (ASCII case ignored), other than `skip`
uint32_t Named(const std::vector<uint8_t>& raw, const uint8_t* name11, uint32_t skip)
{
    const std::string wanted = Listed(name11);
    const uint32_t nfile = std::min<uint32_t>(raw[21], static_cast<uint32_t>(raw.size() / 32));
    for (uint32_t i = 1; i < nfile; i++)
        if (i != skip && (raw[i * 32 + 11] & kExists) && SameName(Listed(raw.data() + i * 32), wanted))
            return i;
    return 0;
}

std::string Join(const std::vector<std::string>& parts, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; i++)
        out += (i ? "/" : "") + parts[i];
    return out;
}
}  // namespace

bool IsdosFileSystem::EncodeName(const std::string& name, bool catalogue, uint8_t out[11])
{
    std::u32string text = UnicodeHelper::DecodeUtf8(name);
    const size_t dot = text.rfind(U'.');
    const std::u32string base = text.substr(0, dot);
    const std::u32string ext = dot == std::u32string::npos ? std::u32string() : text.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3 || (dot != std::u32string::npos && ext.empty()) || (catalogue && !ext.empty()))
        return false;
    std::memset(out, ' ', 11);
    auto put = [catalogue](const std::u32string& part, uint8_t* to) {
        for (size_t i = 0; i < part.size(); i++)
        {
            const char32_t c = catalogue ? UnicodeHelper::ToUpper(part[i]) : part[i];
            uint8_t byte = 0;
            if (c < 0x80)
            {
                // $fncor: letters, digits and these signs (the second group since 21.IX.1999)
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      std::strchr("#$&+-=_`!@%'(){|}~", static_cast<char>(c))) || c == 0)
                    return false;
                byte = static_cast<uint8_t>(c);
            }
            else if (!UnicodeHelper::ToCodePage(CodePage::Cp866, c, byte) || !((byte >= 0x80 && byte <= 0xAF) || (byte >= 0xE0 && byte <= 0xF1)))
                return false;   // Cyrillic letters of CP866 only
            to[i] = byte;
        }
        return true;
    };
    return put(base, out) && put(ext, out + 8);
}

bool IsdosFileSystem::Put(uint32_t block, const uint8_t* src)
{
    return _writer && block < _boot.blocks && _writer(block, src);
}

bool IsdosFileSystem::LoadCatalogue(uint16_t first, CatalogueImage& out, std::string& error) const
{
    out = {};
    out.first = first;
    uint8_t b[kBlock];
    if (first == 0 || !Block(first, b) || !(b[11] & kCatalogue))
    {
        error = "catalogue block " + std::to_string(first) + " does not read as a catalogue";
        return false;
    }
    const uint32_t csize = Word(b + 14);
    const uint32_t nfile = b[21];
    out.continuous = (b[11] & kContinuous) != 0;
    if (out.continuous)
    {
        const uint32_t count = std::max<uint32_t>(1, (std::max(csize, nfile * 32) + kBlock - 1) / kBlock);
        for (uint32_t k = 0; k < count && k < kMaxCatalogueBlocks; k++)
            out.blocks.push_back(first + k);
    }
    else
    {
        out.table = Word(b + 17);
        uint8_t t[kBlock];
        if (!Block(out.table, t) || t[0] > kMaxSegments)
        {
            error = "catalogue block " + std::to_string(first) + ": its segment table does not read";
            return false;
        }
        for (uint32_t k = 0; k < t[0]; k++)
            for (uint32_t n = 0; n < t[3 + 3 * k] && out.blocks.size() < kMaxCatalogueBlocks; n++)
                out.blocks.push_back(Word(t + 1 + 3 * k) + n);
    }
    if (out.blocks.empty() || out.blocks.front() != first || nfile < 1 || nfile * 32 > out.blocks.size() * kBlock)
    {
        error = "catalogue block " + std::to_string(first) + " is damaged (its blocks do not hold its " + std::to_string(nfile) + " entries)";
        return false;
    }
    out.raw.resize(out.blocks.size() * kBlock);
    for (size_t k = 0; k < out.blocks.size(); k++)
        if (!Block(out.blocks[k], out.raw.data() + k * kBlock))
        {
            error = "catalogue block " + std::to_string(out.blocks[k]) + " unreadable";
            return false;
        }
    return true;
}

bool IsdosFileSystem::StoreEntry(const CatalogueImage& c, uint32_t index)
{
    const size_t k = index * 32 / kBlock;
    return k < c.blocks.size() && Put(c.blocks[k], c.raw.data() + k * kBlock);
}

bool IsdosFileSystem::ParentOf(const std::vector<std::string>& parts, uint16_t& first, std::string& error) const
{
    first = _boot.root;
    if (parts.size() < 2)
        return true;
    IsdosFile dir;
    if (!Find(Join(parts, parts.size() - 1), dir, error))
        return false;
    if (!dir.directory)
    {
        error = dir.path + " is no catalogue";
        return false;
    }
    first = dir.first;
    return true;
}

bool IsdosFileSystem::Locate(const std::string& path, Slot& out, std::string& error) const
{
    const std::vector<std::string> parts = SplitPath(path);
    uint16_t first = 0;
    if (parts.empty())
    {
        error = "name a file";
        return false;
    }
    if (!ParentOf(parts, first, error) || !LoadCatalogue(first, out.catalogue, error))
        return false;
    const uint32_t nfile = out.catalogue.raw[21];
    for (uint32_t i = 1; i < nfile; i++)
    {
        const uint8_t* e = out.catalogue.raw.data() + i * 32;
        if ((e[11] & kExists) && SameName(Entry(e).name, parts.back()))
        {
            out.index = i;
            out.file = Entry(e);
            out.file.path = Join(parts, parts.size() - 1) + (parts.size() > 1 ? "/" : "") + out.file.name;
            return true;
        }
    }
    error = "no file " + path + " on the iS-DOS device";
    return false;
}

bool IsdosFileSystem::Fixed(const uint8_t* entry) const
{
    if (entry[11] == kSystem && entry[19] == kSystem)
        return true;
    // The kernel file (????_dos.sys): the descriptor keeps a copy of its entry at +32, checked by a XOR at +27
    uint8_t b[kBlock];
    return Block(0, b) && std::memcmp(entry, b + 32, 32) == 0;
}

bool IsdosFileSystem::LoadBitmap(std::vector<uint8_t>& bits, std::string& error) const
{
    const uint32_t bytes = (_boot.blocks + 7u) / 8;
    bits.assign(((bytes + kBlock - 1) / kBlock) * kBlock, 0);
    for (uint32_t k = 0; k * kBlock < bytes; k++)
        if (!Block(1 + k, bits.data() + k * kBlock))
        {
            error = "the allocation bitmap (block " + std::to_string(1 + k) + ") does not read";
            return false;
        }
    return true;
}

bool IsdosFileSystem::StoreBitmap(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after)
{
    for (uint32_t k = 0; k * kBlock < after.size(); k++)
        if (std::memcmp(before.data() + k * kBlock, after.data() + k * kBlock, kBlock) != 0 && !Put(1 + k, after.data() + k * kBlock))
            return false;
    return true;
}

bool IsdosFileSystem::Held(const uint8_t* entry, std::vector<uint32_t>& blocks, std::string& error) const
{
    blocks.clear();
    const IsdosFile f = Entry(entry);
    if (f.directory)
    {
        CatalogueImage c;
        if (!LoadCatalogue(f.first, c, error))
            return false;
        blocks = c.blocks;
        if (!c.continuous)
            blocks.push_back(c.table);
        return true;
    }
    if (f.status & kContinuous)
    {
        for (uint32_t k = 0; k < (f.length + kBlock - 1) / kBlock; k++)
            blocks.push_back(f.first + k);
        return true;
    }
    uint8_t t[kBlock];
    if (!Block(f.first, t) || t[0] > kMaxSegments)
    {
        error = f.name + ": its segment table (block " + std::to_string(f.first) + ") does not read";
        return false;
    }
    blocks.push_back(f.first);
    for (uint32_t k = 0; k < t[0]; k++)
        for (uint32_t n = 0; n < t[3 + 3 * k]; n++)
            blocks.push_back(Word(t + 1 + 3 * k) + n);
    return true;
}

bool IsdosFileSystem::TakeSlot(CatalogueImage& c, std::vector<uint8_t>& bits, uint32_t& index, std::string& error)
{
    uint8_t* head = c.raw.data();
    const uint32_t nfile = head[21];
    for (uint32_t i = 1; i < nfile; i++)
        if (!(head[i * 32 + 11] & kExists))
        {
            index = i;
            return true;
        }
    if (head[22] >= kMaxLiveEntries || nfile >= kMaxEntries)
    {
        error = "the catalogue holds " + std::to_string(kMaxLiveEntries) + " files already";
        return false;
    }
    const uint32_t csize = Word(head + 14);
    if (nfile * 32 >= csize)
    {
        // Full: a segmented catalogue grows by 32 bytes, by a block of its own when its blocks are full
        if (c.continuous)
        {
            error = "the catalogue is full (a continuous catalogue does not grow)";
            return false;
        }
        if (csize + 32 > c.blocks.size() * kBlock)
        {
            uint8_t t[kBlock];
            const uint32_t block = FirstFit(bits, _boot.blocks, 1);
            if (!Block(c.table, t) || t[0] >= kMaxSegments || block == UINT32_MAX || c.blocks.size() >= kMaxCatalogueBlocks)
            {
                error = "the catalogue cannot grow (no free block, or its segment table is full)";
                return false;
            }
            PutWord(t + 1 + 3 * t[0], block);
            t[3 + 3 * t[0]] = 1;
            t[0]++;
            const uint8_t zero[kBlock] = {};
            if (!Put(block, zero) || !Put(c.table, t))
            {
                error = "the catalogue's new block " + std::to_string(block) + " cannot be written";
                return false;
            }
            Mark(bits, block, true);
            c.blocks.push_back(block);
            c.raw.resize(c.blocks.size() * kBlock, 0);
            head = c.raw.data();
        }
        PutWord(head + 14, csize + 32);
    }
    index = nfile;
    head[21] = static_cast<uint8_t>(nfile + 1);
    return true;
}

bool IsdosFileSystem::Write(const std::string& path, const std::vector<uint8_t>& bytes, const WriteOptions& options, std::string& error)
{
    if (!_writer)
    {
        error = "the iS-DOS device is read only";
        return false;
    }
    if (!RootOk())
    {
        error = _descriptor ? "the iS-DOS main catalogue does not open" : "no iS-DOS device descriptor in block 0";
        return false;
    }
    const std::vector<std::string> parts = SplitPath(path);
    uint8_t name[11];
    if (parts.empty() || !EncodeName(parts.back(), false, name))
    {
        error = "an iS-DOS file is name.ext: up to 8 and 3 letters, digits or # $ & + - = _ ` ! @ % ' ( ) { | } ~ (" + path + ")";
        return false;
    }
    if (bytes.size() > 0xFFFFFF)
    {
        error = "an iS-DOS file holds at most 16 MiB";
        return false;
    }
    uint16_t first = 0;
    CatalogueImage c;
    std::vector<uint8_t> bits;
    if (!ParentOf(parts, first, error) || !LoadCatalogue(first, c, error) || !LoadBitmap(bits, error))
        return false;
    const std::vector<uint8_t> before = bits;

    // An older file of the name gives its blocks back and keeps its slot
    const uint32_t old = Named(c.raw, name, 0);
    if (old)
    {
        const uint8_t* e = c.raw.data() + old * 32;
        if ((e[11] & kCatalogue) || Fixed(e) || (e[11] & (kWriteProtected | kDeleteProtected)))
        {
            error = path + (((e[11] & kCatalogue) && !Fixed(e)) ? " is a catalogue" : " is protected (a system file, write or delete protected)");
            return false;
        }
        std::vector<uint32_t> held;
        if (!Held(e, held, error))
            return false;
        for (const uint32_t b : held)
            if (b < _boot.blocks)
                Mark(bits, b, false);
    }

    // The blocks: one free run as a continuous file, else a segment table and runs of free blocks in order
    const uint32_t count = static_cast<uint32_t>((bytes.size() + kBlock - 1) / kBlock);
    std::vector<Run> runs;
    uint32_t table = 0;
    const uint32_t fit = count && count <= kMaxContinuousBlocks ? FirstFit(bits, _boot.blocks, count) : UINT32_MAX;
    const bool continuous = fit != UINT32_MAX;
    if (continuous)
        runs.push_back({fit, count});
    else
    {
        table = FirstFit(bits, _boot.blocks, 1);
        if (table != UINT32_MAX)
            Mark(bits, table, true);
        uint32_t have = 0;
        for (uint32_t b = 0; b < _boot.blocks && have < count && table != UINT32_MAX; b++)
        {
            if (Used(bits, b))
                continue;
            if (!runs.empty() && runs.back().start + runs.back().count == b && runs.back().count < kMaxSegmentBlocks)
                runs.back().count++;
            else
                runs.push_back({b, 1});
            have++;
        }
        if (table == UINT32_MAX || have < count)
        {
            error = "the device has no room: " + std::to_string(count + 1) + " blocks needed";
            return false;
        }
        if (runs.size() > kMaxSegments)
        {
            error = "the free space is in more than " + std::to_string(kMaxSegments) + " pieces (a segment table holds no more)";
            return false;
        }
    }
    for (const Run& run : runs)
        for (uint32_t k = 0; k < run.count; k++)
            Mark(bits, run.start + k, true);

    uint32_t index = old;
    if (!index && !TakeSlot(c, bits, index, error))
        return false;

    // The data, the segment table, the bitmap, the descriptor and the catalogue's header
    size_t offset = 0;
    for (const Run& run : runs)
        for (uint32_t k = 0; k < run.count; k++, offset += kBlock)
        {
            uint8_t block[kBlock] = {};
            std::memcpy(block, bytes.data() + offset, std::min<size_t>(kBlock, bytes.size() - offset));
            if (!Put(run.start + k, block))
            {
                error = "block " + std::to_string(run.start + k) + " cannot be written";
                return false;
            }
        }
    if (!continuous)
    {
        uint8_t t[kBlock] = {};
        t[0] = static_cast<uint8_t>(runs.size());
        for (size_t k = 0; k < runs.size(); k++)
        {
            PutWord(t + 1 + 3 * k, runs[k].start);
            t[3 + 3 * k] = static_cast<uint8_t>(runs[k].count);
        }
        if (!Put(table, t))
        {
            error = "the segment table (block " + std::to_string(table) + ") cannot be written";
            return false;
        }
    }
    uint16_t sum = 0;
    for (const uint8_t b : bytes)
        sum = static_cast<uint16_t>(sum + b);
    uint8_t* e = c.raw.data() + index * 32;
    std::memset(e, 0, 32);
    std::memcpy(e, name, 11);
    e[11] = static_cast<uint8_t>(kExists | (continuous ? kContinuous : 0));
    PutWord(e + 12, options.load);
    e[14] = static_cast<uint8_t>(bytes.size());
    e[15] = static_cast<uint8_t>(bytes.size() >> 8);
    e[16] = static_cast<uint8_t>(bytes.size() >> 16);
    PutWord(e + 17, continuous ? runs.front().start : table);
    PutWord(e + 26, static_cast<uint16_t>(0 - sum));
    PutWord(e + 28, options.time);
    PutWord(e + 30, options.date);
    if (!old)
        c.raw[22]++;
    if (!StoreBitmap(before, bits) || !StoreEntry(c, index) || !StoreEntry(c, 0))
    {
        error = "the catalogue or the bitmap cannot be written";
        return false;
    }
    return true;
}

bool IsdosFileSystem::Delete(const std::string& path, std::string& error)
{
    if (!_writer)
    {
        error = "the iS-DOS device is read only";
        return false;
    }
    Slot slot;
    std::vector<uint8_t> bits;
    if (!Locate(path, slot, error) || !LoadBitmap(bits, error))
        return false;
    uint8_t* e = slot.catalogue.raw.data() + slot.index * 32;
    if (Fixed(e) || (e[11] & kDeleteProtected))
    {
        error = slot.file.path + " is protected (a system file or delete protected)";
        return false;
    }
    if (slot.file.directory)
    {
        CatalogueImage sub;
        if (!LoadCatalogue(slot.file.first, sub, error))
            return false;
        for (uint32_t i = 1; i < sub.raw[21]; i++)
            if (sub.raw[i * 32 + 11] & kExists)
            {
                error = slot.file.path + " is not empty";
                return false;
            }
    }
    std::vector<uint32_t> held;
    if (!Held(e, held, error))
        return false;
    const std::vector<uint8_t> before = bits;
    for (const uint32_t b : held)
        if (b < _boot.blocks)
            Mark(bits, b, false);
    e[11] = static_cast<uint8_t>(e[11] & ~kExists);
    if (slot.catalogue.raw[22])
        slot.catalogue.raw[22]--;
    if (!StoreEntry(slot.catalogue, slot.index) || !StoreEntry(slot.catalogue, 0) || !StoreBitmap(before, bits))
    {
        error = "the catalogue or the bitmap cannot be written";
        return false;
    }
    return true;
}

bool IsdosFileSystem::Rename(const std::string& path, const std::string& to, std::string& error)
{
    if (!_writer)
    {
        error = "the iS-DOS device is read only";
        return false;
    }
    Slot slot;
    if (!Locate(path, slot, error))
        return false;
    uint8_t* e = slot.catalogue.raw.data() + slot.index * 32;
    if (Fixed(e))
    {
        error = slot.file.path + " is a system file";
        return false;
    }
    const std::vector<std::string> parts = SplitPath(to);
    uint8_t name[11];
    if (parts.empty() || !EncodeName(parts.back(), slot.file.directory, name))
    {
        error = slot.file.directory ? "an iS-DOS catalogue is a name of up to 8 letters, digits or signs, no type (" + to + ")"
                                    : "an iS-DOS file is name.ext: up to 8 and 3 letters, digits or # $ & + - = _ ` ! @ % ' ( ) { | } ~ (" + to + ")";
        return false;
    }
    // A bare name stays in the catalogue; a path ("/top.txt" too) goes from the root
    uint16_t first = slot.catalogue.first;
    if ((parts.size() > 1 || to.front() == '/') && !ParentOf(parts, first, error))
        return false;

    if (first == slot.catalogue.first)
    {
        if (Named(slot.catalogue.raw, name, slot.index))
        {
            error = "the catalogue has a " + Listed(name) + " already";
            return false;
        }
        std::memcpy(e, name, 11);
        if (!StoreEntry(slot.catalogue, slot.index))
        {
            error = "the catalogue cannot be written";
            return false;
        }
        if (slot.file.directory)
        {
            // Its own entry 0 carries the name too
            CatalogueImage sub;
            if (!LoadCatalogue(slot.file.first, sub, error))
                return false;
            std::memcpy(sub.raw.data(), name, 11);
            if (!StoreEntry(sub, 0))
            {
                error = "the catalogue's header cannot be written";
                return false;
            }
        }
        return true;
    }

    // Into another catalogue: a file only (a catalogue's level and its whole tree would change)
    if (slot.file.directory)
    {
        error = "a catalogue is renamed in place only: give a bare name";
        return false;
    }
    CatalogueImage target;
    std::vector<uint8_t> bits;
    if (!LoadCatalogue(first, target, error) || !LoadBitmap(bits, error))
        return false;
    if (Named(target.raw, name, 0))
    {
        error = "the catalogue has a " + Listed(name) + " already";
        return false;
    }
    const std::vector<uint8_t> before = bits;
    uint32_t index = 0;
    if (!TakeSlot(target, bits, index, error))
        return false;
    std::memcpy(target.raw.data() + index * 32, e, 32);
    std::memcpy(target.raw.data() + index * 32, name, 11);
    target.raw[22]++;
    e[11] = static_cast<uint8_t>(e[11] & ~kExists);
    if (slot.catalogue.raw[22])
        slot.catalogue.raw[22]--;
    if (!StoreBitmap(before, bits) || !StoreEntry(target, index) || !StoreEntry(target, 0) || !StoreEntry(slot.catalogue, slot.index) ||
        !StoreEntry(slot.catalogue, 0))
    {
        error = "the catalogues cannot be written";
        return false;
    }
    return true;
}
}  // namespace unrealdisk

/// endregion </Writing>
