#include "unrealdisk/plus3fs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
constexpr size_t kSector = 512;
constexpr size_t kEntry = 32;
constexpr size_t kRecord = 128;
constexpr size_t kRecordsPerExtent = 128;   // a logical extent: 16K
constexpr uint8_t kDeleted = 0xE5;
constexpr size_t kHeader = 128;

/// The disk's geometry (the specification of its first sector, else the +3 standard)
struct Geometry
{
    uint8_t sides = 1;
    uint8_t tracks = 40;
    uint8_t sectors = 9;          // per track
    uint8_t firstSector = 1;      // the lowest sector ID of a track
    uint8_t reserved = 1;         // tracks before the directory
    size_t blockSize = 1024;
    uint8_t directoryBlocks = 2;

    size_t Blocks() const { return (static_cast<size_t>(tracks) * sides - reserved) * sectors * kSector / blockSize; }
    bool WideBlocks() const { return Blocks() > 255; }          // 16-bit block numbers
    size_t BlocksPerEntry() const { return WideBlocks() ? 8 : 16; }
    size_t Entries() const { return directoryBlocks * blockSize / kEntry; }
    /// logical extents (16K) one entry covers, less one: the extent number's bits within an entry
    uint8_t ExtentMask() const { return static_cast<uint8_t>(BlocksPerEntry() * blockSize / 16384 - 1); }
};

struct Entry
{
    size_t index = 0;
    uint8_t raw[kEntry] = {};
    uint8_t User() const { return raw[0]; }
    std::string Name() const { return Trimmed(std::string(reinterpret_cast<const char*>(raw + 1), 8)); }
    std::string Type() const
    {
        std::string t;
        for (int k = 9; k < 12; ++k)
            t.push_back(static_cast<char>(raw[k] & 0x7F));
        return Trimmed(t);
    }
    unsigned Extent() const { return static_cast<unsigned>(raw[12] & 0x1F) + 32u * (raw[14] & 0x3F); }
    uint8_t Records() const { return raw[15]; }

    /// the attribute bits cleared, trailing blanks removed
    static std::string Trimmed(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(c & 0x7F);
        while (!s.empty() && (s.back() == ' ' || s.back() == 0))
            s.pop_back();
        return s;
    }
};

class Disk
{
public:
    explicit Disk(DiskImage& image) : _image(image) { Read(); }

    bool Ok() const { return _ok; }
    const Geometry& Shape() const { return _g; }

    uint8_t* Sector(size_t logical)
    {
        DiskImage::Track* track = TrackOf(logical);
        if (!track)
            return nullptr;
        DiskImage::Sector* s = track->findSector(static_cast<uint8_t>(_g.firstSector + logical % _g.sectors));
        return s && s->hasData && s->dataSize >= kSector ? s->data : nullptr;
    }

    bool WriteSector(size_t logical, const uint8_t* data)
    {
        DiskImage::Track* track = TrackOf(logical);
        if (!track || !Sector(logical))
            return false;
        track->writeSectorData(static_cast<uint8_t>(_g.firstSector + logical % _g.sectors - 1), data, kSector);
        return true;
    }

    /// The block's bytes (its sectors in order); false when a sector is missing
    bool ReadBlock(size_t block, std::vector<uint8_t>& out)
    {
        const size_t per = _g.blockSize / kSector;
        for (size_t k = 0; k < per; ++k)
        {
            const uint8_t* data = Sector(block * per + k);
            if (!data)
                return false;
            out.insert(out.end(), data, data + kSector);
        }
        return true;
    }

    bool WriteBlock(size_t block, const uint8_t* bytes)
    {
        const size_t per = _g.blockSize / kSector;
        for (size_t k = 0; k < per; ++k)
            if (!WriteSector(block * per + k, bytes + k * kSector))
                return false;
        return true;
    }

    std::vector<Entry> Entries()
    {
        std::vector<Entry> out;
        std::vector<uint8_t> directory;
        for (size_t b = 0; b < _g.directoryBlocks; ++b)
            if (!ReadBlock(b, directory))
                return {};
        for (size_t k = 0; k < _g.Entries() && (k + 1) * kEntry <= directory.size(); ++k)
        {
            Entry e;
            e.index = k;
            std::memcpy(e.raw, directory.data() + k * kEntry, kEntry);
            out.push_back(e);
        }
        return out;
    }

    bool WriteEntries(const std::vector<Entry>& entries)
    {
        std::vector<uint8_t> directory;
        for (size_t b = 0; b < _g.directoryBlocks; ++b)
            if (!ReadBlock(b, directory))
                return false;
        for (const Entry& e : entries)
            std::memcpy(directory.data() + e.index * kEntry, e.raw, kEntry);
        for (size_t b = 0; b < _g.directoryBlocks; ++b)
            if (!WriteBlock(b, directory.data() + b * _g.blockSize))
                return false;
        return true;
    }

    std::vector<size_t> Blocks(const Entry& e) const
    {
        std::vector<size_t> out;
        for (size_t k = 0; k < _g.BlocksPerEntry(); ++k)
        {
            const size_t b = _g.WideBlocks() ? (e.raw[16 + 2 * k] | (e.raw[17 + 2 * k] << 8)) : e.raw[16 + k];
            if (b != 0)
                out.push_back(b);
        }
        return out;
    }

private:
    DiskImage::Track* TrackOf(size_t logical)
    {
        const size_t track = _g.reserved + logical / _g.sectors;
        const uint8_t cylinder = static_cast<uint8_t>(_g.sides == 2 ? track / 2 : track);
        const uint8_t side = static_cast<uint8_t>(_g.sides == 2 ? track % 2 : 0);
        return _image.getTrackForCylinderAndSide(cylinder, side);
    }

    void Read()
    {
        DiskImage::Track* first = _image.getTrackForCylinderAndSide(0, 0);
        if (!first || first->sectors().empty())
            return;
        uint8_t lowest = 0xFF;
        for (const DiskImage::Sector& s : first->sectors())
        {
            if (!s.hasData || s.dataSize != kSector)
                return;   // +3DOS has 512-byte sectors
            lowest = std::min(lowest, s.number());
        }
        _g.firstSector = lowest;
        _g.sectors = static_cast<uint8_t>(first->sectors().size());
        _g.tracks = std::max<uint8_t>(_image.getCylinders(), 1);
        _g.sides = 1;
        if (lowest == 0xC1)   // the Amstrad CPC's data format: no reserved track
            _g.reserved = 0;
        else if (lowest == 0x41)   // its system format: two
            _g.reserved = 2;
        // the specification in the first sector (+3DOS, PCW): format, sides, tracks, sectors, size, reserved, block
        // size, directory blocks
        const DiskImage::Sector* boot = first->findSector(lowest);
        if (boot && lowest == 1 && boot->data[0] <= 3 && boot->data[4] == 2 && boot->data[3] >= 8 && boot->data[3] <= 10 &&
            boot->data[6] >= 1 && boot->data[6] <= 5 && boot->data[7] >= 1 && boot->data[7] <= 16)
        {
            _g.sides = (boot->data[1] & 3) ? 2 : 1;
            _g.tracks = boot->data[2];
            _g.sectors = boot->data[3];
            _g.reserved = boot->data[5];
            _g.blockSize = size_t(128) << boot->data[6];
            _g.directoryBlocks = boot->data[7];
        }
        if (_g.sides == 1 && _image.getSides() == 2 && _g.tracks > 40 && !_image.getTrackForCylinderAndSide(_g.tracks - 1, 0))
            _g.sides = 2;
        _ok = _g.Blocks() > _g.directoryBlocks;
    }

    DiskImage& _image;
    Geometry _g;
    bool _ok = false;
};

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

/// NAME.TYP -> "NAME    ", "TYP"
void SplitName(const std::string& full, std::string& name, std::string& type)
{
    const size_t dot = full.find('.');
    name = Upper(full.substr(0, dot)).substr(0, 8);
    type = dot == std::string::npos ? std::string() : Upper(full.substr(dot + 1)).substr(0, 3);
}

bool Live(const Entry& e) { return e.User() <= 15; }

/// The files: their entries grouped by user, name and type, each group in extent order
std::vector<std::vector<Entry>> Group(const std::vector<Entry>& entries)
{
    std::map<std::tuple<uint8_t, std::string, std::string>, std::vector<Entry>> files;
    std::vector<std::tuple<uint8_t, std::string, std::string>> order;
    for (const Entry& e : entries)
        if (Live(e))
        {
            const auto key = std::make_tuple(e.User(), e.Name(), e.Type());
            if (!files.count(key))
                order.push_back(key);
            files[key].push_back(e);
        }
    std::vector<std::vector<Entry>> out;
    for (const auto& key : order)
    {
        std::vector<Entry> group = files[key];
        std::sort(group.begin(), group.end(), [](const Entry& a, const Entry& b) { return a.Extent() < b.Extent(); });
        out.push_back(std::move(group));
    }
    return out;
}

Plus3File Describe(Disk& disk, const std::vector<Entry>& group)
{
    const Geometry& g = disk.Shape();
    Plus3File f;
    f.user = group.front().User();
    f.name = group.front().Name();
    f.type = group.front().Type();
    for (int k = 0; k < 3; ++k)
        if (group.front().raw[9 + k] & 0x80)
            f.attributes |= static_cast<uint8_t>(1 << k);
    const unsigned perEntry = g.ExtentMask() + 1u;   // logical extents an entry holds
    const Entry& last = group.back();
    // the records: whole entries before the last one, then the last one's logical extents and its records
    const uint32_t records = static_cast<uint32_t>(last.Extent() / perEntry * perEntry * kRecordsPerExtent + last.Extent() % perEntry * kRecordsPerExtent) +
                             last.Records();
    f.size = records * kRecord;
    std::vector<uint8_t> head;
    const std::vector<size_t> blocks = disk.Blocks(group.front());
    if (!blocks.empty() && disk.ReadBlock(blocks.front(), head) && head.size() >= kHeader && std::memcmp(head.data(), "PLUS3DOS\x1A", 9) == 0)
    {
        uint8_t sum = 0;
        for (size_t k = 0; k < 127; ++k)
            sum = static_cast<uint8_t>(sum + head[k]);
        if (sum == head[127])
        {
            const uint32_t whole = head[11] | (head[12] << 8) | (head[13] << 16) | (static_cast<uint32_t>(head[14]) << 24);
            f.header = true;
            f.length = whole >= kHeader ? whole - kHeader : 0;
            f.basicType = head[15];
            f.param1 = static_cast<uint16_t>(head[18] | (head[19] << 8));
            f.param2 = static_cast<uint16_t>(head[20] | (head[21] << 8));
        }
    }
    if (!f.header)
        f.length = f.size;
    return f;
}
}  // namespace

bool Plus3FileSystem::IsPlus3() const
{
    std::vector<Plus3File> files;
    std::string error;
    return List(files, error);
}

bool Plus3FileSystem::List(std::vector<Plus3File>& out, std::string& error) const
{
    out.clear();
    Disk disk(_image);
    const std::vector<Entry> entries = disk.Ok() ? disk.Entries() : std::vector<Entry>{};
    if (entries.empty())
    {
        error = "the disk has no +3DOS file system";
        return false;
    }
    // a directory: every entry free (#E5) or of a user 0-15 with a name of printable characters
    for (const Entry& e : entries)
        if (Live(e))
            for (int k = 1; k < 12; ++k)
                if ((e.raw[k] & 0x7F) < 0x20)
                {
                    error = "the disk has no +3DOS file system (directory entry " + std::to_string(e.index) + ")";
                    return false;
                }
    for (const std::vector<Entry>& group : Group(entries))
        out.push_back(Describe(disk, group));
    return true;
}

bool Plus3FileSystem::Find(const std::string& name, Plus3File& out, std::string& error) const
{
    std::vector<Plus3File> files;
    if (!List(files, error))
        return false;
    std::string wantedName, wantedType;
    SplitName(name, wantedName, wantedType);
    for (const Plus3File& f : files)
        if (Upper(f.name) == wantedName && Upper(f.type) == wantedType)
        {
            out = f;
            return true;
        }
    error = "no file " + name + " on the disk";
    return false;
}

bool Plus3FileSystem::Read(const Plus3File& file, std::vector<uint8_t>& data, std::string& error) const
{
    data.clear();
    Disk disk(_image);
    for (const std::vector<Entry>& group : Group(disk.Entries()))
    {
        if (group.front().User() != file.user || group.front().Name() != file.name || group.front().Type() != file.type)
            continue;
        std::vector<uint8_t> all;
        for (const Entry& e : group)
            for (const size_t b : disk.Blocks(e))
                if (!disk.ReadBlock(b, all))
                {
                    error = file.FullName() + ": block " + std::to_string(b) + " cannot be read";
                    return false;
                }
        all.resize(std::min<size_t>(all.size(), file.size));
        if (file.header)
        {
            const size_t end = std::min<size_t>(all.size(), kHeader + file.length);
            data.assign(all.begin() + static_cast<std::ptrdiff_t>(std::min(kHeader, end)), all.begin() + static_cast<std::ptrdiff_t>(end));
        }
        else
            data = std::move(all);
        return true;
    }
    error = "no file " + file.FullName() + " on the disk";
    return false;
}

bool Plus3FileSystem::Delete(const std::string& name, std::string& error)
{
    Disk disk(_image);
    std::vector<Entry> entries = disk.Ok() ? disk.Entries() : std::vector<Entry>{};
    if (entries.empty())
    {
        error = "the disk has no +3DOS file system";
        return false;
    }
    std::string wantedName, wantedType;
    SplitName(name, wantedName, wantedType);
    std::vector<Entry> changed;
    for (Entry& e : entries)
        if (Live(e) && Upper(e.Name()) == wantedName && Upper(e.Type()) == wantedType)
        {
            e.raw[0] = kDeleted;
            changed.push_back(e);
        }
    if (changed.empty())
    {
        error = "no file " + name + " on the disk";
        return false;
    }
    if (!disk.WriteEntries(changed))
    {
        error = "the directory cannot be written";
        return false;
    }
    return true;
}

bool Plus3FileSystem::ValidName(const std::string& name)
{
    const size_t dot = name.find('.');
    const std::string base = name.substr(0, dot);
    const std::string type = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    if (base.empty() || base.size() > 8 || type.size() > 3 || (dot != std::string::npos && type.empty()))
        return false;
    // Blanks inside a name stay (a TR-DOS name moved over keeps them), not in front
    if (base[0] == ' ' || (!type.empty() && type[0] == ' '))
        return false;
    for (const char c : base + type)
        if (c < ' ' || c >= 0x7F || std::strchr("<>.,;:=?*[]", c))
            return false;
    return true;
}

bool Plus3FileSystem::Rename(const std::string& name, const std::string& newName, std::string& error)
{
    if (!ValidName(newName))
    {
        error = "a +3DOS name is NAME.TYP: up to 8 and 3 characters, none of < > . , ; : = ? * [ ] (" + newName + ")";
        return false;
    }
    Disk disk(_image);
    std::vector<Entry> entries = disk.Ok() ? disk.Entries() : std::vector<Entry>{};
    if (entries.empty())
    {
        error = "the disk has no +3DOS file system";
        return false;
    }
    std::string wantedName, wantedType, targetName, targetType;
    SplitName(name, wantedName, wantedType);
    SplitName(newName, targetName, targetType);
    const bool same = wantedName == targetName && wantedType == targetType;
    for (const Entry& e : entries)
        if (!same && Live(e) && Upper(e.Name()) == targetName && Upper(e.Type()) == targetType)
        {
            error = "the disk has a file " + newName + " already";
            return false;
        }
    targetName.resize(8, ' ');
    targetType.resize(3, ' ');
    std::vector<Entry> changed;
    for (Entry& e : entries)
        if (Live(e) && Upper(e.Name()) == wantedName && Upper(e.Type()) == wantedType)
        {
            std::memcpy(e.raw + 1, targetName.data(), 8);
            for (int k = 0; k < 3; ++k)
                e.raw[9 + k] = static_cast<uint8_t>((e.raw[9 + k] & 0x80) | targetType[k]);
            changed.push_back(e);
        }
    if (changed.empty())
    {
        error = "no file " + name + " on the disk";
        return false;
    }
    if (!disk.WriteEntries(changed))
    {
        error = "the directory cannot be written";
        return false;
    }
    return true;
}

bool Plus3FileSystem::SetAttributes(const std::string& name, uint8_t attributes, std::string& error)
{
    Disk disk(_image);
    std::vector<Entry> entries = disk.Ok() ? disk.Entries() : std::vector<Entry>{};
    if (entries.empty())
    {
        error = "the disk has no +3DOS file system";
        return false;
    }
    std::string wantedName, wantedType;
    SplitName(name, wantedName, wantedType);
    std::vector<Entry> changed;
    for (Entry& e : entries)
        if (Live(e) && Upper(e.Name()) == wantedName && Upper(e.Type()) == wantedType)
        {
            for (int k = 0; k < 3; ++k)
                e.raw[9 + k] = static_cast<uint8_t>((e.raw[9 + k] & 0x7F) | ((attributes >> k) & 1 ? 0x80 : 0));
            changed.push_back(e);
        }
    if (changed.empty())
    {
        error = "no file " + name + " on the disk";
        return false;
    }
    if (!disk.WriteEntries(changed))
    {
        error = "the directory cannot be written";
        return false;
    }
    return true;
}

bool Plus3FileSystem::Write(const std::string& name, const std::vector<uint8_t>& bytes, const WriteOptions& options, std::string& error)
{
    if (!ValidName(name))
    {
        error = "a +3DOS name is NAME.TYP: up to 8 and 3 characters, none of < > . , ; : = ? * [ ] (" + name + ")";
        return false;
    }
    {
        Plus3File old;
        std::string ignored;
        if (Find(name, old, ignored) && !Delete(name, error))
            return false;
    }
    Disk disk(_image);
    std::vector<Entry> entries = disk.Ok() ? disk.Entries() : std::vector<Entry>{};
    if (entries.empty())
    {
        error = "the disk has no +3DOS file system";
        return false;
    }
    const Geometry& g = disk.Shape();

    // the content: the header, the data, up to whole records
    std::vector<uint8_t> content;
    if (options.header)
    {
        content.assign(kHeader, 0);
        std::memcpy(content.data(), "PLUS3DOS\x1A\x01\x00", 11);
        const uint32_t whole = static_cast<uint32_t>(kHeader + bytes.size());
        for (int k = 0; k < 4; ++k)
            content[11 + k] = static_cast<uint8_t>(whole >> (8 * k));
        content[15] = options.basicType;
        content[16] = static_cast<uint8_t>(bytes.size());
        content[17] = static_cast<uint8_t>(bytes.size() >> 8);
        content[18] = static_cast<uint8_t>(options.param1);
        content[19] = static_cast<uint8_t>(options.param1 >> 8);
        content[20] = static_cast<uint8_t>(options.param2);
        content[21] = static_cast<uint8_t>(options.param2 >> 8);
        uint8_t sum = 0;
        for (size_t k = 0; k < 127; ++k)
            sum = static_cast<uint8_t>(sum + content[k]);
        content[127] = sum;
    }
    content.insert(content.end(), bytes.begin(), bytes.end());
    const size_t records = std::max<size_t>(1, (content.size() + kRecord - 1) / kRecord);
    content.resize(((records * kRecord + g.blockSize - 1) / g.blockSize) * g.blockSize, 0x1A);

    // the free blocks (the directory's and every live entry's are taken) and entries
    std::vector<bool> used(g.Blocks(), false);
    for (size_t b = 0; b < g.directoryBlocks && b < used.size(); ++b)
        used[b] = true;
    for (const Entry& e : entries)
        if (Live(e))
            for (const size_t b : disk.Blocks(e))
                if (b < used.size())
                    used[b] = true;
    const size_t needed = content.size() / g.blockSize;
    std::vector<size_t> blocks;
    for (size_t b = 0; b < used.size() && blocks.size() < needed; ++b)
        if (!used[b])
            blocks.push_back(b);
    if (blocks.size() < needed)
    {
        error = "not enough free blocks: " + std::to_string(needed) + " needed, " + std::to_string(blocks.size()) + " free";
        return false;
    }
    const size_t entriesNeeded = (needed + g.BlocksPerEntry() - 1) / g.BlocksPerEntry();
    std::vector<size_t> free;
    for (const Entry& e : entries)
        if (!Live(e))
            free.push_back(e.index);
    if (free.size() < entriesNeeded)
    {
        error = "the directory is full";
        return false;
    }

    for (size_t k = 0; k < blocks.size(); ++k)
        if (!disk.WriteBlock(blocks[k], content.data() + k * g.blockSize))
        {
            error = "block " + std::to_string(blocks[k]) + " cannot be written";
            return false;
        }
    std::string fileName, fileType;
    SplitName(name, fileName, fileType);
    fileName.resize(8, ' ');
    fileType.resize(3, ' ');
    const size_t recordsPerEntry = g.BlocksPerEntry() * g.blockSize / kRecord;
    std::vector<Entry> written;
    for (size_t k = 0; k < entriesNeeded; ++k)
    {
        Entry e;
        e.index = free[k];
        std::memset(e.raw, 0, kEntry);
        e.raw[0] = options.user;
        std::memcpy(e.raw + 1, fileName.data(), 8);
        std::memcpy(e.raw + 9, fileType.data(), 3);
        const bool last = k + 1 == entriesNeeded;
        const size_t inEntry = last ? records - k * recordsPerEntry : recordsPerEntry;   // records this entry holds
        const size_t extent = k * (g.ExtentMask() + 1u) + (inEntry - 1) / kRecordsPerExtent;   // its last logical extent
        e.raw[12] = static_cast<uint8_t>(extent & 0x1F);
        e.raw[14] = static_cast<uint8_t>(extent >> 5);
        e.raw[15] = static_cast<uint8_t>(inEntry - (inEntry - 1) / kRecordsPerExtent * kRecordsPerExtent);
        for (size_t n = 0; n < g.BlocksPerEntry(); ++n)
        {
            const size_t at = k * g.BlocksPerEntry() + n;
            if (at >= blocks.size())
                break;
            if (g.WideBlocks())
            {
                e.raw[16 + 2 * n] = static_cast<uint8_t>(blocks[at]);
                e.raw[17 + 2 * n] = static_cast<uint8_t>(blocks[at] >> 8);
            }
            else
                e.raw[16 + n] = static_cast<uint8_t>(blocks[at]);
        }
        written.push_back(e);
    }
    if (!disk.WriteEntries(written))
    {
        error = "the directory cannot be written";
        return false;
    }
    return true;
}
}  // namespace unrealdisk
