#include "unrealdisk/fatfs.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/blockdevice.h"

namespace unrealdisk
{
namespace
{
constexpr uint32_t kSector = 512;
constexpr uint8_t kLongName = 0x0F;
constexpr uint8_t kDeleted = 0xE5;
constexpr uint8_t kBaseLower = 0x08, kExtLower = 0x10;  // the NT case flags of byte 12

uint16_t Get16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t Get32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void Put16(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void Put32(uint8_t* p, uint32_t v)
{
    Put16(p, v);
    Put16(p + 2, v >> 16);
}

uint8_t LongNameChecksum(const uint8_t* shortName)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortName[i]);
    return sum;
}

/// The UTF-16 units a long-name slot holds, in order
constexpr int kSlotUnits[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

bool ShortNameCharacter(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return std::strchr("!#$%&'()-@^_`{}~", c) != nullptr && c != 0;
}

std::string Lower(const std::string& text)
{
    std::string out = text;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

/// Case-insensitive compare: ASCII and the Cyrillic letters UnicodeHelper upper-cases
bool SameName(const std::string& a, const std::string& b)
{
    std::u32string x = UnicodeHelper::DecodeUtf8(a), y = UnicodeHelper::DecodeUtf8(b);
    if (x.size() != y.size())
        return false;
    for (size_t i = 0; i < x.size(); i++)
        if (UnicodeHelper::ToUpper(x[i]) != UnicodeHelper::ToUpper(y[i]))
            return false;
    return true;
}

std::vector<std::string> SplitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : path)
    {
        if (c == '/' || c == '\\')
        {
            if (!part.empty())
                parts.push_back(part);
            part.clear();
        }
        else
            part.push_back(c);
    }
    if (!part.empty())
        parts.push_back(part);
    return parts;
}
}  // namespace

FatFs::FatFs(BlockDevice& device, CodePage page) : _device(device), _page(page) {}

bool FatFs::Open(std::string* error)
{
    FatVolumeReader reader;
    _cacheLba = UINT64_MAX;
    _fsInfoMarked = false;
    if (!reader.Open(_device, _page, error))
        return Fail(Error::Io);
    _type = reader.Type();
    _volumeStart = reader.VolumeStart();
    _sectorsPerCluster = reader.SectorsPerCluster();
    _reservedSectors = reader.ReservedSectors();
    _fats = reader.FatCount();
    _fatSectors = reader.FatSectors();
    _rootDirSectors = reader.RootDirSectors();
    _rootCluster = reader.RootCluster();
    _clusterCount = reader.ClusterCount();
    _dataStart = _volumeStart + reader.DataStart();
    _fsInfoSector = reader.FsInfoSector();
    _error = Error::None;
    return true;
}

bool FatFs::Writable() const
{
    return _device.IsWritable();
}

bool FatFs::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba != _cacheLba)
    {
        if (!_device.ReadSector(lba, _cache))
        {
            _cacheLba = UINT64_MAX;
            return Fail(Error::Io);
        }
        _cacheLba = lba;
    }
    std::memcpy(dst, _cache, kSector);
    return true;
}

bool FatFs::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (!_device.WriteSector(lba, src))
    {
        _cacheLba = UINT64_MAX;
        return Fail(_device.IsWritable() ? Error::Io : Error::ReadOnly);
    }
    std::memcpy(_cache, src, kSector);
    _cacheLba = lba;
    return true;
}

uint64_t FatFs::ClusterLba(uint32_t cluster) const
{
    return _dataStart + static_cast<uint64_t>(cluster - 2) * _sectorsPerCluster;
}

uint32_t FatFs::EndMark() const
{
    return _type == FatReaderType::Fat12 ? 0xFFF : _type == FatReaderType::Fat16 ? 0xFFFF : 0x0FFFFFFF;
}

bool FatFs::IsEnd(uint32_t value) const
{
    return _type == FatReaderType::Fat12 ? value >= 0xFF8 : _type == FatReaderType::Fat16 ? value >= 0xFFF8 : value >= 0x0FFFFFF8;
}

uint32_t FatFs::FatGet(uint32_t cluster)
{
    const uint64_t fat = _volumeStart + _reservedSectors;
    uint8_t s[kSector];
    if (_type == FatReaderType::Fat12)
    {
        const uint32_t offset = cluster + cluster / 2;
        if (!ReadSector(fat + offset / kSector, s))
            return 0;
        const uint8_t low = s[offset % kSector];
        if ((offset + 1) % kSector == 0 && !ReadSector(fat + (offset + 1) / kSector, s))
            return 0;
        const uint16_t v = static_cast<uint16_t>(low | (s[(offset + 1) % kSector] << 8));
        return (cluster & 1) ? v >> 4 : v & 0xFFF;
    }
    const uint32_t width = _type == FatReaderType::Fat16 ? 2 : 4;
    const uint32_t offset = cluster * width;
    if (!ReadSector(fat + offset / kSector, s))
        return 0;
    return width == 2 ? Get16(s + offset % kSector) : Get32(s + offset % kSector) & 0x0FFFFFFF;
}

bool FatFs::FatSet(uint32_t cluster, uint32_t value)
{
    uint8_t s[kSector];
    for (uint32_t copy = 0; copy < _fats; copy++)
    {
        const uint64_t fat = _volumeStart + _reservedSectors + static_cast<uint64_t>(copy) * _fatSectors;
        if (_type == FatReaderType::Fat12)
        {
            const uint32_t offset = cluster + cluster / 2;
            const uint8_t mask0 = (cluster & 1) ? 0x0F : 0x00, mask1 = (cluster & 1) ? 0x00 : 0xF0;
            const uint16_t v = static_cast<uint16_t>((cluster & 1) ? (value & 0xFFF) << 4 : value & 0xFFF);
            for (uint32_t i = 0; i < 2; i++)
            {
                const uint64_t lba = fat + (offset + i) / kSector;
                if (!ReadSector(lba, s))
                    return false;
                uint8_t& b = s[(offset + i) % kSector];
                b = static_cast<uint8_t>((b & (i ? mask1 : mask0)) | (i ? v >> 8 : v & 0xFF));
                if (!WriteSector(lba, s))
                    return false;
            }
            continue;
        }
        const uint32_t width = _type == FatReaderType::Fat16 ? 2 : 4;
        const uint32_t offset = cluster * width;
        const uint64_t lba = fat + offset / kSector;
        if (!ReadSector(lba, s))
            return false;
        if (width == 2)
            Put16(s + offset % kSector, value);
        else
            Put32(s + offset % kSector, (Get32(s + offset % kSector) & 0xF0000000) | (value & 0x0FFFFFFF));
        if (!WriteSector(lba, s))
            return false;
    }
    MarkFsInfo();
    return true;
}

void FatFs::MarkFsInfo()
{
    if (_type != FatReaderType::Fat32 || _fsInfoSector == 0 || _fsInfoMarked)
        return;
    _fsInfoMarked = true;
    uint8_t s[kSector];
    const uint64_t lba = _volumeStart + _fsInfoSector;
    if (!ReadSector(lba, s) || Get32(s) != 0x41615252 || Get32(s + 484) != 0x61417272)
        return;
    Put32(s + 488, 0xFFFFFFFF);  // free count unknown
    Put32(s + 492, 0xFFFFFFFF);  // no hint
    WriteSector(lba, s);
}

bool FatFs::Chain(uint32_t first, std::vector<uint32_t>& clusters)
{
    clusters.clear();
    uint32_t c = first;
    while (c != 0)
    {
        if (c < 2 || c >= _clusterCount + 2 || clusters.size() > _clusterCount)
            return Fail(Error::Damaged);
        clusters.push_back(c);
        const uint32_t next = FatGet(c);
        if (_error == Error::Io)
            return false;
        if (IsEnd(next))
            break;
        if (next == 0)
            return Fail(Error::Damaged);
        c = next;
    }
    return true;
}

bool FatFs::Allocate(uint32_t& cluster)
{
    for (uint32_t i = 0; i < _clusterCount; i++)
    {
        const uint32_t c = 2 + (_allocHint - 2 + i) % _clusterCount;
        if (FatGet(c) == 0)
        {
            if (_error == Error::Io || !FatSet(c, EndMark()))
                return false;
            _allocHint = c + 1 < _clusterCount + 2 ? c + 1 : 2;
            cluster = c;
            return true;
        }
    }
    return Fail(Error::NoSpace);
}

bool FatFs::ZeroCluster(uint32_t cluster)
{
    uint8_t zero[kSector] = {};
    for (uint32_t i = 0; i < _sectorsPerCluster; i++)
        if (!WriteSector(ClusterLba(cluster) + i, zero))
            return false;
    return true;
}

bool FatFs::FreeChain(uint32_t first)
{
    std::vector<uint32_t> clusters;
    if (!Chain(first, clusters))
        return false;
    for (uint32_t c : clusters)
        if (!FatSet(c, 0))
            return false;
    return true;
}

FatNode FatFs::Root() const
{
    FatNode root;
    root.root = true;
    root.attributes = FatNode::kDirectory;
    root.firstCluster = _type == FatReaderType::Fat32 ? _rootCluster : 0;
    return root;
}

bool FatFs::DirectorySectors(const FatNode& dir, std::vector<uint64_t>& sectors)
{
    sectors.clear();
    if (!dir.IsDirectory())
        return Fail(Error::NotDirectory);
    if (dir.root && _type != FatReaderType::Fat32)
    {
        const uint64_t first = _volumeStart + _reservedSectors + static_cast<uint64_t>(_fats) * _fatSectors;
        for (uint32_t i = 0; i < _rootDirSectors; i++)
            sectors.push_back(first + i);
        return true;
    }
    std::vector<uint32_t> clusters;
    if (!Chain(dir.firstCluster, clusters))
        return false;
    for (uint32_t c : clusters)
        for (uint32_t i = 0; i < _sectorsPerCluster; i++)
            sectors.push_back(ClusterLba(c) + i);
    return true;
}

bool FatFs::List(const FatNode& dir, std::vector<FatNode>& out)
{
    _error = Error::None;
    out.clear();
    std::vector<uint64_t> sectors;
    if (!DirectorySectors(dir, sectors))
        return false;
    std::u16string longName;
    std::vector<std::pair<uint64_t, uint16_t>> slots;
    int expected = 0;  // long-name slots still to come (0: none collected)
    uint8_t checksum = 0;
    uint8_t s[kSector];
    for (uint64_t lba : sectors)
    {
        if (!ReadSector(lba, s))
            return false;
        for (uint16_t offset = 0; offset < kSector; offset += 32)
        {
            const uint8_t* e = s + offset;
            if (e[0] == 0)
                return true;
            if (e[0] == kDeleted)
            {
                expected = 0;
                slots.clear();
                continue;
            }
            if (e[11] == kLongName)
            {
                const int ord = e[0] & 0x3F;
                if (e[0] & 0x40)
                {
                    longName.assign(static_cast<size_t>(ord) * 13, u'\0');
                    slots.clear();
                    expected = ord;
                    checksum = e[13];
                }
                else if (ord != expected || e[13] != checksum)
                {
                    expected = 0;
                    slots.clear();
                    continue;
                }
                if (ord == 0 || expected == 0)
                    continue;
                for (int i = 0; i < 13; i++)
                    longName[static_cast<size_t>(ord - 1) * 13 + i] = static_cast<char16_t>(Get16(e + kSlotUnits[i]));
                slots.emplace_back(lba, offset);
                expected = ord - 1;
                continue;
            }
            const bool haveLong = !slots.empty() && expected == 0 && LongNameChecksum(e) == checksum;
            if ((e[11] & FatNode::kVolume) || (e[0] == '.' && (e[1] == ' ' || (e[1] == '.' && e[2] == ' '))))
            {
                slots.clear();
                expected = 0;
                continue;
            }
            FatNode node;
            std::string base, ext;
            for (int i = 0; i < 11; i++)
            {
                uint8_t b = e[i];
                if (i == 0 && b == 0x05)
                    b = kDeleted;
                std::string& part = i < 8 ? base : ext;
                if (b < 0x80)
                    part.push_back(static_cast<char>(b));
                else
                    UnicodeHelper::AppendUtf8(part, UnicodeHelper::FromCodePage(_page, b));
            }
            while (!base.empty() && base.back() == ' ')
                base.pop_back();
            while (!ext.empty() && ext.back() == ' ')
                ext.pop_back();
            if (e[12] & kBaseLower)
                base = Lower(base);
            if (e[12] & kExtLower)
                ext = Lower(ext);
            node.shortName = ext.empty() ? base : base + "." + ext;
            node.name = node.shortName;
            if (haveLong)
            {
                const size_t end = longName.find(u'\0');
                node.name = UnicodeHelper::EncodeUtf8(UnicodeHelper::FromUtf16(longName.substr(0, end)));
                node.longSlots = slots;
            }
            node.attributes = e[11];
            node.firstCluster = Get16(e + 26) | (_type == FatReaderType::Fat32 ? static_cast<uint32_t>(Get16(e + 20)) << 16 : 0);
            node.size = Get32(e + 28);
            node.time = Get16(e + 22);
            node.date = Get16(e + 24);
            node.entryLba = lba;
            node.entryOffset = offset;
            out.push_back(std::move(node));
            slots.clear();
            expected = 0;
        }
    }
    return true;
}

bool FatFs::NodeAt(uint64_t lba, uint16_t offset, FatNode& out)
{
    _error = Error::None;
    uint8_t s[kSector];
    if (offset > kSector - 32 || (offset & 31) != 0)
        return Fail(Error::NotFound);
    if (!ReadSector(lba, s))
        return false;
    const uint8_t* e = s + offset;
    if (e[0] == 0 || e[0] == kDeleted || e[11] == kLongName || (e[11] & FatNode::kVolume))
        return Fail(Error::NotFound);
    out = FatNode();
    out.attributes = e[11];
    out.firstCluster = Get16(e + 26) | (_type == FatReaderType::Fat32 ? static_cast<uint32_t>(Get16(e + 20)) << 16 : 0);
    out.size = Get32(e + 28);
    out.time = Get16(e + 22);
    out.date = Get16(e + 24);
    out.entryLba = lba;
    out.entryOffset = offset;
    return true;
}

bool FatFs::Find(const FatNode& dir, const std::string& name, FatNode& out)
{
    std::vector<FatNode> entries;
    if (!List(dir, entries))
        return false;
    for (FatNode& e : entries)
        if (SameName(e.name, name) || SameName(e.shortName, name))
        {
            out = std::move(e);
            return true;
        }
    return Fail(Error::NotFound);
}

bool FatFs::Resolve(const std::string& path, FatNode& out)
{
    _error = Error::None;
    FatNode node = Root();
    for (const std::string& part : SplitPath(path))
    {
        if (!node.IsDirectory())
            return Fail(Error::NotFound);
        FatNode next;
        if (!Find(node, part, next))
            return false;
        node = std::move(next);
    }
    out = std::move(node);
    return true;
}

size_t FatFs::Read(const FatNode& file, uint32_t offset, uint8_t* dst, size_t count)
{
    _error = Error::None;
    if (file.IsDirectory())
    {
        Fail(Error::IsDirectory);
        return 0;
    }
    if (offset >= file.size || count == 0)
        return 0;
    count = std::min<size_t>(count, file.size - offset);
    const uint32_t clusterBytes = ClusterBytes();
    uint32_t c = file.firstCluster;
    for (uint32_t i = offset / clusterBytes; i > 0; i--)
    {
        c = FatGet(c);
        if (c < 2 || c >= _clusterCount + 2)
        {
            Fail(Error::Damaged);
            return 0;
        }
    }
    size_t done = 0;
    uint32_t pos = offset;
    uint8_t s[kSector];
    while (done < count)
    {
        if (c < 2 || c >= _clusterCount + 2)
        {
            Fail(Error::Damaged);
            break;
        }
        const uint32_t inCluster = pos % clusterBytes;
        if (!ReadSector(ClusterLba(c) + inCluster / kSector, s))
            break;
        const size_t take = std::min<size_t>(kSector - inCluster % kSector, count - done);
        std::memcpy(dst + done, s + inCluster % kSector, take);
        done += take;
        pos += static_cast<uint32_t>(take);
        if (pos % clusterBytes == 0 && done < count)
            c = FatGet(c);
    }
    return done;
}

bool FatFs::StoreEntry(const FatNode& node)
{
    if (node.root)
        return true;
    uint8_t s[kSector];
    if (!ReadSector(node.entryLba, s))
        return false;
    uint8_t* e = s + node.entryOffset;
    e[11] = node.attributes;
    Put16(e + 20, _type == FatReaderType::Fat32 ? node.firstCluster >> 16 : 0);
    Put16(e + 22, node.time);
    Put16(e + 24, node.date);
    Put16(e + 18, node.date);  // the access date
    Put16(e + 26, node.firstCluster & 0xFFFF);
    Put32(e + 28, node.IsDirectory() ? 0 : node.size);
    return WriteSector(node.entryLba, s);
}

bool FatFs::Write(FatNode& file, uint32_t offset, const uint8_t* src, size_t count, size_t& written)
{
    written = 0;
    _error = Error::None;
    if (file.IsDirectory())
        return Fail(Error::IsDirectory);
    if (!Writable() || (file.attributes & FatNode::kReadOnly))
        return Fail(Error::ReadOnly);
    if (offset > file.size && !Resize(file, offset))
        return false;
    if (count == 0)
        return true;
    count = std::min<uint64_t>(count, 0xFFFFFFFFull - offset);
    const uint32_t clusterBytes = ClusterBytes();
    std::vector<uint32_t> clusters;
    if (!Chain(file.firstCluster, clusters))
        return false;
    const uint64_t end = static_cast<uint64_t>(offset) + count;
    const size_t need = static_cast<size_t>((end + clusterBytes - 1) / clusterBytes);
    bool full = false;
    while (clusters.size() < need)
    {
        uint32_t c = 0;
        if (!Allocate(c))
        {
            if (_error != Error::NoSpace)
                return false;
            full = true;
            break;
        }
        if (clusters.empty())
            file.firstCluster = c;
        else if (!FatSet(clusters.back(), c))
            return false;
        clusters.push_back(c);
    }
    const size_t room = static_cast<size_t>(std::min<uint64_t>(end, static_cast<uint64_t>(clusters.size()) * clusterBytes) - offset);
    uint8_t s[kSector];
    uint32_t pos = offset;
    while (written < room)
    {
        const uint32_t c = clusters[pos / clusterBytes];
        const uint32_t inCluster = pos % clusterBytes;
        const uint64_t lba = ClusterLba(c) + inCluster / kSector;
        const size_t at = inCluster % kSector;
        const size_t take = std::min<size_t>(kSector - at, room - written);
        if (take < kSector && !ReadSector(lba, s))
            return false;
        std::memcpy(s + at, src + written, take);
        if (!WriteSector(lba, s))
            return false;
        written += take;
        pos += static_cast<uint32_t>(take);
    }
    file.size = std::max<uint32_t>(file.size, offset + static_cast<uint32_t>(written));
    file.date = _date;
    file.time = _time;
    file.attributes |= FatNode::kArchive;
    if (!StoreEntry(file))
        return false;
    return full ? Fail(Error::NoSpace) : true;
}

bool FatFs::Resize(FatNode& file, uint32_t size)
{
    _error = Error::None;
    if (file.IsDirectory())
        return Fail(Error::IsDirectory);
    if (!Writable() || (file.attributes & FatNode::kReadOnly))
        return Fail(Error::ReadOnly);
    if (size > file.size)
    {
        static const uint8_t zeros[4096] = {};
        while (file.size < size)
        {
            size_t written = 0;
            const size_t chunk = std::min<size_t>(sizeof(zeros), size - file.size);
            if (!Write(file, file.size, zeros, chunk, written))
                return false;
        }
        return true;
    }
    std::vector<uint32_t> clusters;
    if (!Chain(file.firstCluster, clusters))
        return false;
    const size_t keep = (static_cast<uint64_t>(size) + ClusterBytes() - 1) / ClusterBytes();
    if (keep < clusters.size())
    {
        if (keep == 0)
            file.firstCluster = 0;
        else if (!FatSet(clusters[keep - 1], EndMark()))
            return false;
        for (size_t i = keep; i < clusters.size(); i++)
            if (!FatSet(clusters[i], 0))
                return false;
    }
    file.size = size;
    file.date = _date;
    file.time = _time;
    file.attributes |= FatNode::kArchive;
    return StoreEntry(file);
}

bool FatFs::ValidName(const std::string& name)
{
    if (name.empty() || name.find_first_not_of('.') == std::string::npos || name.size() > 255 * 3)
        return false;
    for (unsigned char c : name)
        if (c < 0x20 || std::strchr("\"*/:<>?\\|", c))
            return false;
    return name.back() != ' ';
}

bool FatFs::ShortNameOf(const std::string& name, uint8_t out[11], uint8_t& caseFlags)
{
    caseFlags = 0;
    const size_t dot = name.find('.');
    const std::string base = name.substr(0, dot);
    const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3 || ext.find('.') != std::string::npos ||
        (dot != std::string::npos && ext.empty()))
        return false;
    auto part = [&](const std::string& text, uint8_t* dst, size_t width, uint8_t lowerFlag) {
        bool upper = false, lower = false;
        for (size_t i = 0; i < width; i++)
            dst[i] = ' ';
        for (size_t i = 0; i < text.size(); i++)
        {
            char c = text[i];
            if (c >= 'a' && c <= 'z')
            {
                lower = true;
                c = static_cast<char>(c - 'a' + 'A');
            }
            else if (c >= 'A' && c <= 'Z')
                upper = true;
            if (!ShortNameCharacter(c))
                return false;
            dst[i] = static_cast<uint8_t>(c);
        }
        if (upper && lower)
            return false;
        if (lower)
            caseFlags |= lowerFlag;
        return true;
    };
    return part(base, out, 8, kBaseLower) && part(ext, out + 8, 3, kExtLower);
}

bool FatFs::AddEntry(const FatNode& dir, const std::string& name, const uint8_t* shortEntry, FatNode& out)
{
    uint8_t entry[32];
    std::memcpy(entry, shortEntry, 32);
    uint8_t caseFlags = 0;
    std::u16string longName;
    if (ShortNameOf(name, entry, caseFlags))
        entry[12] = caseFlags;
    else
    {
        // A generated short name, BASE~N.EXT, unique in the directory
        std::vector<FatNode> existing;
        if (!List(dir, existing))
            return false;
        const std::u32string points = UnicodeHelper::DecodeUtf8(name);
        longName = UnicodeHelper::ToUtf16(points);
        if (longName.size() > 255)
            return Fail(Error::InvalidName);
        size_t lastDot = std::u32string::npos;
        for (size_t i = points.size(); i-- > 0;)
            if (points[i] == U'.')
            {
                lastDot = i;
                break;
            }
        auto basis = [&](size_t from, size_t to, size_t width) {
            std::vector<uint8_t> bytes;
            for (size_t i = from; i < to && bytes.size() < width; i++)
            {
                const char32_t c = UnicodeHelper::ToUpper(points[i]);
                if (c == U' ' || c == U'.')
                    continue;
                uint8_t b = '_';
                if (c < 0x80)
                    b = ShortNameCharacter(static_cast<char>(c)) ? static_cast<uint8_t>(c) : '_';
                else if (!UnicodeHelper::ToCodePage(_page, c, b) || b < 0x80)
                    b = '_';
                bytes.push_back(b);
            }
            return bytes;
        };
        std::vector<uint8_t> base = basis(0, lastDot == std::u32string::npos || lastDot == 0 ? points.size() : lastDot, 8);
        const std::vector<uint8_t> ext =
            lastDot == std::u32string::npos || lastDot == 0 ? std::vector<uint8_t>() : basis(lastDot + 1, points.size(), 3);
        if (base.empty())
            base.push_back('_');
        bool unique = false;
        for (uint32_t n = 1; n < 1000000 && !unique; n++)
        {
            const std::string tail = "~" + std::to_string(n);
            const size_t keep = std::min(base.size(), 8 - tail.size());
            std::memset(entry, ' ', 11);
            std::memcpy(entry, base.data(), keep);
            std::memcpy(entry + keep, tail.data(), tail.size());
            std::memcpy(entry + 8, ext.data(), ext.size());
            std::string display;
            for (int i = 0; i < 11; i++)
            {
                if (i == 8)
                {
                    while (!display.empty() && display.back() == ' ')
                        display.pop_back();
                    if (entry[8] != ' ')
                        display.push_back('.');
                }
                if (entry[i] < 0x80)
                    display.push_back(static_cast<char>(entry[i]));
                else
                    UnicodeHelper::AppendUtf8(display, UnicodeHelper::FromCodePage(_page, entry[i]));
            }
            while (!display.empty() && display.back() == ' ')
                display.pop_back();
            unique = std::none_of(existing.begin(), existing.end(), [&](const FatNode& e) { return SameName(e.shortName, display); });
        }
        if (!unique)
            return Fail(Error::Exists);
        entry[12] = 0;
    }
    if (entry[0] == kDeleted)
        entry[0] = 0x05;

    // A run of free entries for the slots and the short entry, the directory grown when it has none
    const size_t slotCount = (longName.size() + 12) / 13;
    const size_t needed = slotCount + 1;
    std::vector<uint64_t> sectors;
    if (!DirectorySectors(dir, sectors))
        return false;
    std::vector<std::pair<uint64_t, uint16_t>> run;
    uint8_t s[kSector];
    for (size_t i = 0; run.size() < needed; i++)
    {
        if (i == sectors.size() && !ExtendDirectory(dir, sectors))
            return false;
        if (!ReadSector(sectors[i], s))
            return false;
        for (uint16_t offset = 0; offset < kSector && run.size() < needed; offset += 32)
        {
            if (s[offset] == 0 || s[offset] == kDeleted)
                run.emplace_back(sectors[i], offset);
            else
                run.clear();
        }
    }

    const uint8_t checksum = LongNameChecksum(entry);
    out = FatNode();
    for (size_t k = 0; k < needed; k++)
    {
        const auto [lba, offset] = run[k];
        if (!ReadSector(lba, s))
            return false;
        uint8_t* e = s + offset;
        if (k < slotCount)
        {
            const size_t ord = slotCount - k;  // the last part first
            std::memset(e, 0, 32);
            e[0] = static_cast<uint8_t>(ord | (k == 0 ? 0x40 : 0));
            e[11] = kLongName;
            e[13] = checksum;
            for (int u = 0; u < 13; u++)
            {
                const size_t at = (ord - 1) * 13 + u;
                const uint16_t unit = at < longName.size() ? longName[at] : at == longName.size() ? 0x0000 : 0xFFFF;
                Put16(e + kSlotUnits[u], unit);
            }
            out.longSlots.emplace_back(lba, offset);
        }
        else
            std::memcpy(e, entry, 32);
        if (!WriteSector(lba, s))
            return false;
    }
    std::vector<FatNode> entries;
    if (!List(dir, entries))
        return false;
    for (FatNode& e : entries)
        if (e.entryLba == run.back().first && e.entryOffset == run.back().second)
        {
            out = std::move(e);
            return true;
        }
    return Fail(Error::Damaged);
}

bool FatFs::ExtendDirectory(const FatNode& dir, std::vector<uint64_t>& sectors)
{
    if (dir.root && _type != FatReaderType::Fat32)
        return Fail(Error::NoSpace);  // the fixed root is full
    std::vector<uint32_t> clusters;
    uint32_t c = 0;
    if (!Chain(dir.firstCluster, clusters) || clusters.empty() || !Allocate(c) || !ZeroCluster(c) ||
        !FatSet(clusters.back(), c))
        return clusters.empty() ? Fail(Error::Damaged) : false;
    for (uint32_t i = 0; i < _sectorsPerCluster; i++)
        sectors.push_back(ClusterLba(c) + i);
    return true;
}

bool FatFs::Create(const FatNode& dir, const std::string& name, bool directory, FatNode& out)
{
    _error = Error::None;
    if (!dir.IsDirectory())
        return Fail(Error::NotDirectory);
    if (!ValidName(name))
        return Fail(Error::InvalidName);
    if (!Writable())
        return Fail(Error::ReadOnly);
    FatNode existing;
    if (Find(dir, name, existing))
        return Fail(Error::Exists);
    if (_error != Error::NotFound)
        return false;
    _error = Error::None;

    uint8_t entry[32] = {};
    entry[11] = directory ? FatNode::kDirectory : FatNode::kArchive;
    Put16(entry + 14, _time);  // created
    Put16(entry + 16, _date);
    Put16(entry + 18, _date);  // accessed
    Put16(entry + 22, _time);  // written
    Put16(entry + 24, _date);
    uint32_t cluster = 0;
    if (directory)
    {
        if (!Allocate(cluster) || !ZeroCluster(cluster))
            return false;
        uint8_t s[kSector] = {};
        const uint32_t parent = dir.root ? 0 : dir.firstCluster;
        for (int i = 0; i < 2; i++)
        {
            uint8_t* e = s + i * 32;
            std::memcpy(e, i == 0 ? ".          " : "..         ", 11);
            e[11] = FatNode::kDirectory;
            Put16(e + 14, _time);
            Put16(e + 16, _date);
            Put16(e + 18, _date);
            Put16(e + 22, _time);
            Put16(e + 24, _date);
            const uint32_t target = i == 0 ? cluster : parent;
            Put16(e + 20, _type == FatReaderType::Fat32 ? target >> 16 : 0);
            Put16(e + 26, target & 0xFFFF);
        }
        if (!WriteSector(ClusterLba(cluster), s))
            return false;
        Put16(entry + 20, _type == FatReaderType::Fat32 ? cluster >> 16 : 0);
        Put16(entry + 26, cluster & 0xFFFF);
    }
    if (!AddEntry(dir, name, entry, out))
    {
        const Error error = _error;
        if (cluster)
            FatSet(cluster, 0);
        return Fail(error);
    }
    return true;
}

bool FatFs::Remove(const FatNode& node)
{
    _error = Error::None;
    if (node.root)
        return Fail(Error::InvalidName);
    if (!Writable())
        return Fail(Error::ReadOnly);
    if (node.attributes & FatNode::kReadOnly)
        return Fail(Error::ReadOnly);
    if (node.IsDirectory())
    {
        std::vector<FatNode> entries;
        if (!List(node, entries))
            return false;
        if (!entries.empty())
            return Fail(Error::NotEmpty);
    }
    std::vector<std::pair<uint64_t, uint16_t>> places = node.longSlots;
    places.emplace_back(node.entryLba, node.entryOffset);
    uint8_t s[kSector];
    for (const auto& [lba, offset] : places)
    {
        if (!ReadSector(lba, s))
            return false;
        s[offset] = kDeleted;
        if (!WriteSector(lba, s))
            return false;
    }
    return node.firstCluster == 0 || FreeChain(node.firstCluster);
}

bool FatFs::Rename(const FatNode& node, const FatNode& dir, const std::string& name, FatNode& out)
{
    _error = Error::None;
    if (node.root)
        return Fail(Error::InvalidName);
    if (!dir.IsDirectory())
        return Fail(Error::NotDirectory);
    if (!ValidName(name))
        return Fail(Error::InvalidName);
    if (!Writable())
        return Fail(Error::ReadOnly);
    FatNode existing;
    bool sameEntry = false;  // a case-only rename: the name is taken by the entry itself
    if (Find(dir, name, existing))
    {
        if (existing.entryLba != node.entryLba || existing.entryOffset != node.entryOffset)
            return Fail(Error::Exists);
        sameEntry = true;
    }
    else if (_error != Error::NotFound)
        return false;
    _error = Error::None;

    uint8_t s[kSector];
    if (!ReadSector(node.entryLba, s))
        return false;
    uint8_t entry[32];
    std::memcpy(entry, s + node.entryOffset, 32);
    std::vector<std::pair<uint64_t, uint16_t>> places = node.longSlots;
    places.emplace_back(node.entryLba, node.entryOffset);
    std::vector<uint8_t> firstBytes;
    auto markOld = [&](bool deleted) {
        for (size_t i = 0; i < places.size(); i++)
        {
            if (!ReadSector(places[i].first, s))
                return false;
            if (deleted)
                firstBytes.push_back(s[places[i].second]);
            s[places[i].second] = deleted ? kDeleted : firstBytes[i];
            if (!WriteSector(places[i].first, s))
                return false;
        }
        return true;
    };
    if (sameEntry)
    {
        // The old entry goes first so the new one may take its place; it comes back when that fails
        if (!markOld(true))
            return false;
        if (!AddEntry(dir, name, entry, out))
        {
            const Error error = _error;
            markOld(false);
            return Fail(error);
        }
    }
    else if (!AddEntry(dir, name, entry, out) || !markOld(true))
        return false;
    if (node.IsDirectory() && node.firstCluster != 0)
    {
        // ".." of a moved directory names its new parent (0: the root)
        const uint32_t parent = dir.root ? 0 : dir.firstCluster;
        const uint64_t lba = ClusterLba(node.firstCluster);
        if (!ReadSector(lba, s))
            return false;
        Put16(s + 32 + 20, _type == FatReaderType::Fat32 ? parent >> 16 : 0);
        Put16(s + 32 + 26, parent & 0xFFFF);
        if (!WriteSector(lba, s))
            return false;
    }
    return true;
}

bool FatFs::SetAttributes(FatNode& node, uint8_t attributes)
{
    _error = Error::None;
    if (node.root)
        return Fail(Error::InvalidName);
    if (!Writable())
        return Fail(Error::ReadOnly);
    constexpr uint8_t kChangeable = FatNode::kReadOnly | FatNode::kHidden | FatNode::kSystem | FatNode::kArchive;
    node.attributes = static_cast<uint8_t>((node.attributes & ~kChangeable) | (attributes & kChangeable));
    return StoreEntry(node);
}

uint32_t FatFs::FreeClusters()
{
    _error = Error::None;
    uint32_t free = 0;
    for (uint32_t c = 2; c < _clusterCount + 2 && _error == Error::None; c++)
        if (FatGet(c) == 0 && _error == Error::None)
            free++;
    return free;
}

bool FatFs::Extents(const FatNode& file, std::vector<std::pair<uint64_t, uint32_t>>& runs)
{
    _error = Error::None;
    runs.clear();
    if (file.IsDirectory())
        return Fail(Error::IsDirectory);
    std::vector<uint32_t> clusters;
    if (!Chain(file.firstCluster, clusters))
        return false;
    for (uint32_t c : clusters)
    {
        const uint64_t lba = ClusterLba(c);
        if (!runs.empty() && runs.back().first + runs.back().second == lba)
            runs.back().second += _sectorsPerCluster;
        else
            runs.emplace_back(lba, _sectorsPerCluster);
    }
    return true;
}
}  // namespace unrealdisk
