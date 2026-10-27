#include "stdafx.h"

#include "isosynthvolume.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <set>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/sourcepool.h"

namespace
{
    constexpr uint32_t kBlock = IsoSynthVolume::kBlock;
    constexpr int kMaxDepth = 8;  ///< ECMA-119 6.8.2.1: the root is level 1

    void Both16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 8);
        p[3] = static_cast<uint8_t>(v);
    }
    void Both32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
        {
            p[i] = static_cast<uint8_t>(v >> (8 * i));
            p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }
    void Le32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void Be32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[3 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void Le16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    void Be16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    }

    /// Civil date of a UTC second count (Howard Hinnant's civil_from_days)
    void Civil(int64_t t, int64_t& year, int64_t& month, int64_t& day, int64_t& hour, int64_t& minute, int64_t& second)
    {
        t = std::clamp<int64_t>(t, -2208988800LL, 4102444799LL);  // 1900-01-01 .. 2099-12-31
        int64_t days = t / 86400;
        int64_t rest = t % 86400;
        if (rest < 0)
        {
            rest += 86400;
            days--;
        }
        const int64_t z = days + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        day = doy - (153 * mp + 2) / 5 + 1;
        month = mp < 10 ? mp + 3 : mp - 9;
        year = yoe + era * 400 + (month <= 2 ? 1 : 0);
        hour = rest / 3600;
        minute = rest / 60 % 60;
        second = rest % 60;
    }

    /// A directory record's 7-byte date (UTC)
    void RecordDate(uint8_t* p, int64_t t)
    {
        int64_t y, mo, d, h, mi, s;
        Civil(t, y, mo, d, h, mi, s);
        p[0] = static_cast<uint8_t>(y - 1900);
        p[1] = static_cast<uint8_t>(mo);
        p[2] = static_cast<uint8_t>(d);
        p[3] = static_cast<uint8_t>(h);
        p[4] = static_cast<uint8_t>(mi);
        p[5] = static_cast<uint8_t>(s);
        p[6] = 0;
    }

    /// A volume descriptor's 17-byte date ("YYYYMMDDHHMMSScc" and the zone)
    void VolumeDate(uint8_t* p, int64_t t)
    {
        int64_t y, mo, d, h, mi, s;
        Civil(t, y, mo, d, h, mi, s);
        char text[48];  // room for any int (gcc checks the widest case)
        std::snprintf(text, sizeof text, "%04d%02d%02d%02d%02d%02d00", static_cast<int>(y), static_cast<int>(mo), static_cast<int>(d),
                      static_cast<int>(h), static_cast<int>(mi), static_cast<int>(s));
        std::memcpy(p, text, 16);
        p[16] = 0;
    }

    /// The d-characters of a name part: upper-case A-Z, 0-9 and "_"; anything else "_"
    std::string DChars(const std::u32string& text)
    {
        std::string out;
        for (char32_t c : text)
        {
            if (c >= U'a' && c <= U'z')
                out.push_back(static_cast<char>(c - U'a' + 'A'));
            else if ((c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c == U'_')
                out.push_back(static_cast<char>(c));
            else
                out.push_back('_');
        }
        return out;
    }

    struct Item
    {
        uint32_t node = 0;
        bool isDirectory = false;
        uint8_t hidden = 0;
        std::string iso;            ///< "NAME.EXT;1" / "DIR"
        std::u16string joliet;      ///< "Long name.txt;1" / "Dir"
        size_t dir = 0;             ///< directories: the index into dirs
        std::vector<std::pair<uint32_t, uint32_t>> sections;  ///< files: (block, bytes)
    };

    struct Dir
    {
        uint32_t node = 0;
        size_t parent = 0;
        int depth = 1;
        std::vector<Item> items;
        std::vector<size_t> isoOrder;
        std::vector<size_t> jolietOrder;
        uint16_t isoNumber = 1;
        uint16_t jolietNumber = 1;
        uint32_t isoBlock = 0;
        uint32_t isoBytes = 0;
        uint32_t jolietBlock = 0;
        uint32_t jolietBytes = 0;
    };

    uint32_t RecordLength(size_t nameBytes)
    {
        return static_cast<uint32_t>(33 + nameBytes + ((nameBytes % 2) == 0 ? 1 : 0));
    }

    std::string JolietBytes(const std::u16string& name)
    {
        std::string out;
        for (char16_t c : name)
        {
            out.push_back(static_cast<char>(c >> 8));
            out.push_back(static_cast<char>(c & 0xFF));
        }
        return out;
    }
}  // namespace

/// The build: names, order, layout and the metadata bytes
class IsoLayout
{
public:
    IsoLayout(IsoSynthVolume& volume, const IsoTargetOptions& options, std::vector<std::string>* report)
        : _volume(volume), _tree(*volume._tree), _options(options), _report(report)
    {
    }

    bool Build(std::string* error);

private:
    bool Collect(std::string* error);
    void Name(Dir& dir);
    std::string IsoName(const std::string& utf8, bool isDirectory) const;
    std::u16string JolietName(const std::string& utf8, bool isDirectory) const;
    uint32_t DirectoryBytes(const Dir& dir, bool joliet) const;
    void WriteDirectory(const Dir& dir, bool joliet);
    uint32_t PathTableBytes(bool joliet) const;
    void WritePathTable(uint8_t* p, bool joliet, bool bigEndian) const;
    void WriteDescriptor(uint8_t* p, bool joliet, uint32_t pathTableBytes, uint32_t lBlock, uint32_t mBlock);
    int64_t Time(int64_t t) const { return _options.fixedTimeUtc.value_or(t); }

    IsoSynthVolume& _volume;
    const FileTree& _tree;
    const IsoTargetOptions& _options;
    std::vector<std::string>* _report;
    std::vector<Dir> _dirs;
    std::vector<size_t> _isoBfs;     ///< dirs in the PVD path table's order
    std::vector<size_t> _jolietBfs;  ///< dirs in the Joliet path table's order
    int64_t _newest = 0;
};

bool IsoLayout::Collect(std::string* error)
{
    _dirs.push_back(Dir{});
    _dirs[0].node = FileTree::kRoot;
    _newest = _tree.Node(FileTree::kRoot).mtimeUtc;
    for (size_t d = 0; d < _dirs.size(); d++)
    {
        const uint32_t node = _dirs[d].node;
        for (uint32_t child : _tree.Node(node).children)
        {
            const TreeNode& c = _tree.Node(child);
            _newest = std::max(_newest, c.mtimeUtc);
            Item item;
            item.node = child;
            item.isDirectory = c.isDirectory;
            item.hidden = (c.attributes & 0x02) ? 1 : 0;
            if (c.isDirectory)
            {
                Dir sub;
                sub.node = child;
                sub.parent = d;
                sub.depth = _dirs[d].depth + 1;
                if (sub.depth > kMaxDepth && !_options.relaxDepth)
                {
                    if (error)
                        *error = _tree.PathOf(child) + ": deeper than ISO 9660's 8 directory levels (iso.relaxDepth for Joliet-only readers)";
                    return false;
                }
                item.dir = _dirs.size();
                _dirs.push_back(sub);
            }
            _dirs[d].items.push_back(item);
        }
    }
    return true;
}

std::string IsoLayout::IsoName(const std::string& utf8, bool isDirectory) const
{
    const std::u32string name = UnicodeHelper::DecodeUtf8(utf8);
    if (isDirectory)
    {
        std::string text = DChars(name);
        text.resize(std::min<size_t>(text.size(), _options.level == 1 ? 8 : 31));
        return text.empty() ? "_" : text;
    }
    const size_t dot = name.rfind(U'.');
    std::string base = DChars(dot == std::u32string::npos ? name : name.substr(0, dot));
    std::string ext = dot == std::u32string::npos ? std::string() : DChars(name.substr(dot + 1));
    if (_options.level == 1)
    {
        base.resize(std::min<size_t>(base.size(), 8));
        ext.resize(std::min<size_t>(ext.size(), 3));
    }
    else
    {
        ext.resize(std::min<size_t>(ext.size(), 8));
        base.resize(std::min<size_t>(base.size(), 30 - 1 - ext.size()));
    }
    if (base.empty())
        base = "_";
    return base + "." + ext;
}

std::u16string IsoLayout::JolietName(const std::string& utf8, bool isDirectory) const
{
    std::u16string name = UnicodeHelper::ToUtf16(UnicodeHelper::DecodeUtf8(utf8));
    for (char16_t& c : name)
    {
        if (c < 0x20 || c == u'*' || c == u'/' || c == u':' || c == u';' || c == u'?' || c == u'\\')
            c = u'_';
    }
    const size_t limit = isDirectory ? 64 : 62;  // a file's ";1" makes 64
    if (name.size() > limit)
        name.resize(limit);
    return name;
}

void IsoLayout::Name(Dir& dir)
{
    std::set<std::string> isoTaken;
    std::set<std::u16string> jolietTaken;
    for (Item& item : dir.items)
    {
        const TreeNode& node = _tree.Node(item.node);

        // ISO: unique within the directory, a tail ~N when taken
        std::string iso = IsoName(node.name, item.isDirectory);
        if (isoTaken.count(iso))
        {
            const size_t dot = item.isDirectory ? std::string::npos : iso.rfind('.');
            const std::string base = dot == std::string::npos ? iso : iso.substr(0, dot);
            const std::string ext = dot == std::string::npos ? std::string() : iso.substr(dot);
            const size_t room = item.isDirectory ? (_options.level == 1 ? 8 : 31) : (_options.level == 1 ? 8 : 30 - ext.size());
            for (int n = 1;; n++)
            {
                const std::string tail = "~" + std::to_string(n);
                const std::string candidate = base.substr(0, std::min(base.size(), room - tail.size())) + tail + ext;
                if (!isoTaken.count(candidate))
                {
                    iso = candidate;
                    break;
                }
            }
            if (_report)
                _report->push_back(_tree.PathOf(item.node) + ": ISO name " + iso + " (the plain one is taken)");
        }
        isoTaken.insert(iso);
        item.iso = item.isDirectory ? iso : iso + ";1";

        // Joliet: the long name, unique too when cut to its 64 units
        std::u16string joliet = JolietName(node.name, item.isDirectory);
        if (jolietTaken.count(joliet))
        {
            for (int n = 1;; n++)
            {
                const std::u16string tail = UnicodeHelper::ToUtf16(UnicodeHelper::DecodeUtf8("~" + std::to_string(n)));
                std::u16string candidate = joliet.substr(0, std::min(joliet.size(), (item.isDirectory ? 64 : 62) - tail.size())) + tail;
                if (!jolietTaken.count(candidate))
                {
                    joliet = candidate;
                    break;
                }
            }
            if (_report)
                _report->push_back(_tree.PathOf(item.node) + ": Joliet name cut to 64 characters and made unique");
        }
        jolietTaken.insert(joliet);
        item.joliet = item.isDirectory ? joliet : joliet + u";1";
    }

    dir.isoOrder.resize(dir.items.size());
    dir.jolietOrder.resize(dir.items.size());
    for (size_t i = 0; i < dir.items.size(); i++)
        dir.isoOrder[i] = dir.jolietOrder[i] = i;
    std::sort(dir.isoOrder.begin(), dir.isoOrder.end(), [&dir](size_t a, size_t b) { return dir.items[a].iso < dir.items[b].iso; });
    std::sort(dir.jolietOrder.begin(), dir.jolietOrder.end(),
              [&dir](size_t a, size_t b) { return dir.items[a].joliet < dir.items[b].joliet; });
}

uint32_t IsoLayout::DirectoryBytes(const Dir& dir, bool joliet) const
{
    uint32_t at = 34 + 34;  // "." and ".."
    auto place = [&at](uint32_t length) {
        if (at % kBlock + length > kBlock)
            at = (at / kBlock + 1) * kBlock;
        at += length;
    };
    for (size_t i : joliet ? dir.jolietOrder : dir.isoOrder)
    {
        const Item& item = dir.items[i];
        const size_t nameBytes = joliet ? item.joliet.size() * 2 : item.iso.size();
        const size_t records = item.isDirectory ? 1 : std::max<size_t>(1, item.sections.size());
        for (size_t r = 0; r < records; r++)
            place(RecordLength(nameBytes));
    }
    return (at + kBlock - 1) / kBlock * kBlock;
}

void IsoLayout::WriteDirectory(const Dir& dir, bool joliet)
{
    uint8_t* base = _volume._metadata.data() + static_cast<size_t>(joliet ? dir.jolietBlock : dir.isoBlock) * kBlock;
    uint32_t at = 0;
    auto record = [&](uint32_t extent, uint32_t size, int64_t mtime, uint8_t flags, const std::string& name) {
        const uint32_t length = RecordLength(name.size());
        if (at % kBlock + length > kBlock)
            at = (at / kBlock + 1) * kBlock;
        uint8_t* p = base + at;
        p[0] = static_cast<uint8_t>(length);
        Both32(p + 2, extent);
        Both32(p + 10, size);
        RecordDate(p + 18, Time(mtime));
        p[25] = flags;
        Both16(p + 28, 1);
        p[32] = static_cast<uint8_t>(name.size());
        std::memcpy(p + 33, name.data(), name.size());
        at += length;
    };
    const Dir& parent = _dirs[dir.parent];
    const int64_t own = _tree.Node(dir.node).mtimeUtc;
    record(joliet ? dir.jolietBlock : dir.isoBlock, joliet ? dir.jolietBytes : dir.isoBytes, own, 0x02, std::string(1, '\0'));
    record(joliet ? parent.jolietBlock : parent.isoBlock, joliet ? parent.jolietBytes : parent.isoBytes,
           _tree.Node(parent.node).mtimeUtc, 0x02, std::string(1, '\1'));
    for (size_t i : joliet ? dir.jolietOrder : dir.isoOrder)
    {
        const Item& item = dir.items[i];
        const TreeNode& node = _tree.Node(item.node);
        const std::string name = joliet ? JolietBytes(item.joliet) : item.iso;
        const uint8_t hidden = item.hidden;
        if (item.isDirectory)
        {
            const Dir& sub = _dirs[item.dir];
            record(joliet ? sub.jolietBlock : sub.isoBlock, joliet ? sub.jolietBytes : sub.isoBytes, node.mtimeUtc,
                   static_cast<uint8_t>(0x02 | hidden), name);
        }
        else if (item.sections.empty())
            record(0, 0, node.mtimeUtc, hidden, name);
        else
        {
            for (size_t s = 0; s < item.sections.size(); s++)
            {
                const bool more = s + 1 < item.sections.size();
                record(item.sections[s].first, item.sections[s].second, node.mtimeUtc, static_cast<uint8_t>(hidden | (more ? 0x80 : 0)),
                       name);
            }
        }
    }
}

uint32_t IsoLayout::PathTableBytes(bool joliet) const
{
    uint32_t bytes = 0;
    for (size_t d : joliet ? _jolietBfs : _isoBfs)
    {
        size_t nameBytes = 1;  // the root's 0x00
        if (d != 0)
        {
            const Dir& parent = _dirs[_dirs[d].parent];
            for (const Item& item : parent.items)
            {
                if (item.isDirectory && item.dir == d)
                    nameBytes = joliet ? item.joliet.size() * 2 : item.iso.size();
            }
        }
        bytes += static_cast<uint32_t>(8 + nameBytes + (nameBytes % 2));
    }
    return bytes;
}

void IsoLayout::WritePathTable(uint8_t* p, bool joliet, bool bigEndian) const
{
    for (size_t d : joliet ? _jolietBfs : _isoBfs)
    {
        const Dir& dir = _dirs[d];
        std::string name(1, '\0');
        if (d != 0)
        {
            for (const Item& item : _dirs[dir.parent].items)
            {
                if (item.isDirectory && item.dir == d)
                    name = joliet ? JolietBytes(item.joliet) : item.iso;
            }
        }
        const Dir& parent = _dirs[dir.parent];
        const uint32_t extent = joliet ? dir.jolietBlock : dir.isoBlock;
        const uint16_t parentNumber = joliet ? parent.jolietNumber : parent.isoNumber;
        p[0] = static_cast<uint8_t>(name.size());
        p[1] = 0;
        if (bigEndian)
        {
            Be32(p + 2, extent);
            Be16(p + 6, parentNumber);
        }
        else
        {
            Le32(p + 2, extent);
            Le16(p + 6, parentNumber);
        }
        std::memcpy(p + 8, name.data(), name.size());
        p += 8 + name.size() + (name.size() % 2);
    }
}

void IsoLayout::WriteDescriptor(uint8_t* p, bool joliet, uint32_t pathTableBytes, uint32_t lBlock, uint32_t mBlock)
{
    p[0] = joliet ? 2 : 1;
    std::memcpy(p + 1, "CD001", 5);
    p[6] = 1;
    // Text fields: spaces (UCS-2 spaces for Joliet)
    auto fill = [p, joliet](size_t from, size_t to) {
        for (size_t i = from; i < to; i++)
            p[i] = joliet ? ((i - from) % 2 ? 0x20 : 0x00) : 0x20;
    };
    fill(8, 72);
    fill(190, 813);
    // Volume identifier
    std::string id = DChars(UnicodeHelper::DecodeUtf8(_options.volumeId));
    if (joliet)
    {
        const std::u16string wide = UnicodeHelper::ToUtf16(UnicodeHelper::DecodeUtf8(_options.volumeId));
        for (size_t i = 0; i < wide.size() && i < 16; i++)
        {
            p[40 + i * 2] = static_cast<uint8_t>(wide[i] >> 8);
            p[41 + i * 2] = static_cast<uint8_t>(wide[i] & 0xFF);
        }
        p[88] = '%';
        p[89] = '/';
        p[90] = 'E';
    }
    else
        std::memcpy(p + 40, id.data(), std::min<size_t>(id.size(), 32));
    Both32(p + 80, _volume._blocks);
    Both16(p + 120, 1);
    Both16(p + 124, 1);
    Both16(p + 128, kBlock);
    Both32(p + 132, pathTableBytes);
    Le32(p + 140, lBlock);
    Be32(p + 148, mBlock);
    // The root directory record
    const Dir& root = _dirs[0];
    uint8_t* r = p + 156;
    r[0] = 34;
    Both32(r + 2, joliet ? root.jolietBlock : root.isoBlock);
    Both32(r + 10, joliet ? root.jolietBytes : root.isoBytes);
    RecordDate(r + 18, Time(_tree.Node(FileTree::kRoot).mtimeUtc));
    r[25] = 0x02;
    Both16(r + 28, 1);
    r[32] = 1;
    r[33] = 0;
    const int64_t created = Time(_newest);
    VolumeDate(p + 813, created);
    VolumeDate(p + 830, created);
    std::memset(p + 847, '0', 16);
    std::memset(p + 864, '0', 16);
    p[881] = 1;
}

bool IsoLayout::Build(std::string* error)
{
    if (!Collect(error))
        return false;
    for (Dir& dir : _dirs)
        Name(dir);

    // Path table orders: breadth first, subdirectories in each tree's name order
    for (int joliet = 0; joliet < 2; joliet++)
    {
        std::vector<size_t>& bfs = joliet ? _jolietBfs : _isoBfs;
        bfs.push_back(0);
        for (size_t i = 0; i < bfs.size(); i++)
        {
            const Dir& dir = _dirs[bfs[i]];
            for (size_t k : joliet ? dir.jolietOrder : dir.isoOrder)
            {
                if (dir.items[k].isDirectory)
                    bfs.push_back(dir.items[k].dir);
            }
        }
        for (size_t i = 0; i < bfs.size(); i++)
        {
            if (bfs.size() > 0xFFFF)
            {
                if (error)
                    *error = "more than 65535 directories: an ISO 9660 path table cannot number them";
                return false;
            }
            (joliet ? _dirs[bfs[i]].jolietNumber : _dirs[bfs[i]].isoNumber) = static_cast<uint16_t>(i + 1);
        }
    }

    // File sections (known before the directory sizes: a big file has several records)
    uint64_t fileBlocks = 0;
    for (size_t d : _isoBfs)
    {
        for (size_t k : _dirs[d].isoOrder)
        {
            Item& item = _dirs[d].items[k];
            if (item.isDirectory)
                continue;
            uint64_t bytes = _tree.Node(item.node).data.bytes;
            while (bytes > 0)
            {
                const uint64_t take = std::min<uint64_t>(bytes, IsoSynthVolume::kMaxSection);
                item.sections.push_back({0, static_cast<uint32_t>(take)});
                bytes -= take;
            }
            fileBlocks += (_tree.Node(item.node).data.bytes + kBlock - 1) / kBlock;
        }
    }

    // Layout
    uint32_t block = 16;
    const uint32_t pvdBlock = block++;
    const bool bootable = !_options.boot.empty();
    const uint32_t bootRecordBlock = bootable ? block++ : 0;
    const uint32_t svdBlock = _options.joliet ? block++ : 0;
    const uint32_t terminatorBlock = block++;
    const uint32_t isoTable = PathTableBytes(false);
    const uint32_t jolietTable = _options.joliet ? PathTableBytes(true) : 0;
    auto blocksOf = [](uint64_t bytes) { return static_cast<uint32_t>((bytes + kBlock - 1) / kBlock); };
    const uint32_t isoL = block;
    block += blocksOf(isoTable);
    const uint32_t isoM = block;
    block += blocksOf(isoTable);
    uint32_t jolietL = 0;
    uint32_t jolietM = 0;
    if (_options.joliet)
    {
        jolietL = block;
        block += blocksOf(jolietTable);
        jolietM = block;
        block += blocksOf(jolietTable);
    }
    for (size_t d : _isoBfs)
    {
        _dirs[d].isoBytes = DirectoryBytes(_dirs[d], false);
        _dirs[d].isoBlock = block;
        block += _dirs[d].isoBytes / kBlock;
    }
    if (_options.joliet)
    {
        for (size_t d : _jolietBfs)
        {
            _dirs[d].jolietBytes = DirectoryBytes(_dirs[d], true);
            _dirs[d].jolietBlock = block;
            block += _dirs[d].jolietBytes / kBlock;
        }
    }
    const uint32_t catalogBlock = bootable ? block++ : 0;
    const uint32_t metadataEnd = block;
    if (bootable && _options.boot.size() * 2 + 2 > 64)
    {
        if (error)
            *error = "boot: more El Torito entries than one catalog block holds";
        return false;
    }
    if (static_cast<uint64_t>(block) + fileBlocks > 0xFFFFFFFFull)
    {
        if (error)
            *error = "the content needs more than 2^32 blocks of 2048 bytes";
        return false;
    }
    for (size_t d : _isoBfs)
    {
        for (size_t k : _dirs[d].isoOrder)
        {
            Item& item = _dirs[d].items[k];
            if (item.isDirectory || item.sections.empty())
                continue;
            uint64_t fileBlock = 0;
            for (auto& section : item.sections)
            {
                const uint32_t blocks = blocksOf(section.second);
                section.first = block;
                _volume._runs.push_back({block, blocks, fileBlock, &_tree.Node(item.node).data});
                fileBlock += blocks;
                block += blocks;
            }
        }
    }
    // Boot images that are not files of the union: after the files, served from where they are
    std::vector<uint32_t> bootBlock(_options.boot.size(), 0);
    _volume._bootData.reserve(_options.boot.size());
    for (size_t i = 0; i < _options.boot.size(); i++)
    {
        const IsoBootImage& image = _options.boot[i];
        if (image.unionNode != FileTree::kNone)
        {
            for (const Dir& dir : _dirs)
            {
                for (const Item& item : dir.items)
                {
                    if (item.node == image.unionNode && !item.sections.empty())
                        bootBlock[i] = item.sections.front().first;
                }
            }
            if (bootBlock[i] == 0)
            {
                if (error)
                    *error = "boot: El Torito image " + _tree.PathOf(image.unionNode) + " is not a file of the volume (or empty)";
                return false;
            }
            continue;
        }
        _volume._bootData.push_back(image.data);
        const uint32_t blocks = blocksOf(image.data.bytes);
        bootBlock[i] = block;
        _volume._runs.push_back({block, blocks, 0, &_volume._bootData.back()});
        block += blocks;
    }
    _volume._blocks = block;
    _volume._bootCatalog = catalogBlock;

    // Metadata bytes
    _volume._metadata.assign(static_cast<size_t>(metadataEnd) * kBlock, 0);
    uint8_t* m = _volume._metadata.data();
    WriteDescriptor(m + static_cast<size_t>(pvdBlock) * kBlock, false, isoTable, isoL, isoM);
    if (_options.joliet)
        WriteDescriptor(m + static_cast<size_t>(svdBlock) * kBlock, true, jolietTable, jolietL, jolietM);
    if (bootable)
    {
        uint8_t* br = m + static_cast<size_t>(bootRecordBlock) * kBlock;
        std::memcpy(br + 1, "CD001", 5);
        br[6] = 1;
        std::memcpy(br + 7, "EL TORITO SPECIFICATION", 23);
        Le32(br + 71, catalogBlock);

        uint8_t* c = m + static_cast<size_t>(catalogBlock) * kBlock;
        c[0] = 0x01;
        c[1] = _options.boot.front().entry.platform;
        std::memcpy(c + 4, "UNREAL-NG", 9);
        c[30] = 0x55;
        c[31] = 0xAA;
        uint16_t sum = 0;
        for (int k = 0; k < 32; k += 2)
            sum = static_cast<uint16_t>(sum + (c[k] | (c[k + 1] << 8)));
        Le16(c + 28, static_cast<uint16_t>(0x10000 - sum));
        auto entry = [](uint8_t* e, const IsoBootEntry& b, uint32_t loadBlock) {
            e[0] = b.bootable ? 0x88 : 0x00;
            e[1] = b.emulation;
            Le16(e + 2, b.loadSegment);
            e[4] = b.systemType;
            Le16(e + 6, b.emulation == 0 ? (b.sectorCount ? b.sectorCount : 4) : 1);
            Le32(e + 8, loadBlock);
        };
        entry(c + 32, _options.boot.front().entry, bootBlock[0]);
        // The other entries: one section per run of the same platform
        uint32_t at = 64;
        for (size_t i = 1; i < _options.boot.size();)
        {
            size_t j = i;
            while (j < _options.boot.size() && _options.boot[j].entry.platform == _options.boot[i].entry.platform)
                j++;
            c[at] = j == _options.boot.size() ? 0x91 : 0x90;
            c[at + 1] = _options.boot[i].entry.platform;
            Le16(c + at + 2, static_cast<uint16_t>(j - i));
            at += 32;
            for (size_t k = i; k < j; k++, at += 32)
                entry(c + at, _options.boot[k].entry, bootBlock[k]);
            i = j;
        }
    }
    uint8_t* t = m + static_cast<size_t>(terminatorBlock) * kBlock;
    t[0] = 255;
    std::memcpy(t + 1, "CD001", 5);
    t[6] = 1;
    WritePathTable(m + static_cast<size_t>(isoL) * kBlock, false, false);
    WritePathTable(m + static_cast<size_t>(isoM) * kBlock, false, true);
    if (_options.joliet)
    {
        WritePathTable(m + static_cast<size_t>(jolietL) * kBlock, true, false);
        WritePathTable(m + static_cast<size_t>(jolietM) * kBlock, true, true);
    }
    for (const Dir& dir : _dirs)
    {
        WriteDirectory(dir, false);
        if (_options.joliet)
            WriteDirectory(dir, true);
    }
    return true;
}

// --- IsoSynthVolume ---

IsoSynthVolume::IsoSynthVolume(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool)
    : _tree(std::move(tree)), _pool(std::move(pool)), _reader(*_pool, _tree->Extents())
{
}

std::unique_ptr<IsoSynthVolume> IsoSynthVolume::Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                      const IsoTargetOptions& options, std::string* error,
                                                      std::vector<std::string>* report)
{
    std::unique_ptr<IsoSynthVolume> volume(new IsoSynthVolume(std::move(tree), std::move(pool)));
    IsoLayout layout(*volume, options, report);
    if (!layout.Build(error))
        return nullptr;
    return volume;
}

std::unique_ptr<CdImage> IsoSynthVolume::MakeDisc(std::unique_ptr<IsoSynthVolume> volume, std::string description, uint64_t contentId)
{
    cd::StoredTrack track;
    track.track.number = 1;
    track.track.mode = cd::TrackMode::Mode1;
    track.track.pregapLba = track.track.startLba = 0;
    track.track.endLba = volume->Blocks();
    track.source = 0;
    track.format = cd::StoredFormat::Cooked2048;
    track.stride = kBlock;
    track.storedFirstLba = 0;
    track.storedEndLba = volume->Blocks();
    std::vector<std::unique_ptr<cd::IFrameSource>> sources;
    sources.push_back(std::move(volume));
    std::vector<cd::StoredTrack> tracks{track};
    return std::make_unique<CdImage>(std::move(sources), std::move(tracks), "compose-iso", std::move(description), contentId);
}

bool IsoSynthVolume::ReadBlock(uint32_t block, uint8_t* dst)
{
    if (static_cast<size_t>(block) * kBlock < _metadata.size())
    {
        std::memcpy(dst, _metadata.data() + static_cast<size_t>(block) * kBlock, kBlock);
        return true;
    }
    const Run* run = nullptr;
    if (!_runs.empty())
    {
        const Run& last = _runs[_lastRun];
        if (block >= last.firstBlock && block < last.firstBlock + last.blocks)
            run = &last;
        else
        {
            auto it = std::upper_bound(_runs.begin(), _runs.end(), block, [](uint32_t b, const Run& r) { return b < r.firstBlock; });
            if (it != _runs.begin())
            {
                --it;
                if (block < it->firstBlock + it->blocks)
                {
                    run = &*it;
                    _lastRun = static_cast<size_t>(it - _runs.begin());
                }
            }
        }
    }
    if (!run)
    {
        std::memset(dst, 0, kBlock);
        return true;
    }
    const FileData& data = *run->data;
    const uint64_t first = (run->fileBlockStart + (block - run->firstBlock)) * 4;
    bool ok = true;
    for (uint32_t i = 0; i < 4; i++)
        ok = _reader.ReadFileSector(data, first + i, dst + i * 512) && ok;
    return ok;
}

bool IsoSynthVolume::Read(uint64_t offset, uint8_t* dst, uint32_t length)
{
    uint8_t block[kBlock];
    bool ok = true;
    while (length > 0)
    {
        const uint32_t index = static_cast<uint32_t>(offset / kBlock);
        const uint32_t within = static_cast<uint32_t>(offset % kBlock);
        const uint32_t take = std::min<uint32_t>(length, kBlock - within);
        if (within == 0 && take == kBlock)
            ok = ReadBlock(index, dst) && ok;
        else
        {
            ok = ReadBlock(index, block) && ok;
            std::memcpy(dst, block + within, take);
        }
        dst += take;
        offset += take;
        length -= take;
    }
    return ok;
}
