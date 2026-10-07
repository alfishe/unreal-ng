#include "stdafx.h"

#include "vhdimage.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "common/filehelper.h"

namespace
{
    constexpr uint32_t kSector = 512;
    constexpr uint32_t kUnused = 0xFFFFFFFF;

    void Be16(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    }
    void Be32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[3 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void Be64(uint8_t* p, uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint32_t Get32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }
    uint64_t Get64(const uint8_t* p) { return (static_cast<uint64_t>(Get32(p)) << 32) | Get32(p + 4); }

    /// One's complement of the byte sum, the checksum field counted as zero
    uint32_t Checksum(const uint8_t* data, size_t size, size_t field)
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < size; i++)
            sum += (i >= field && i < field + 4) ? 0 : data[i];
        return ~sum;
    }

    bool Fail(std::string* error, const std::string& text)
    {
        if (error)
            *error = text;
        return false;
    }

    std::array<uint8_t, 1024> DynamicHeader(uint64_t tableOffset, uint32_t entries)
    {
        std::array<uint8_t, 1024> h{};
        std::memcpy(h.data(), "cxsparse", 8);
        Be64(h.data() + 8, ~0ULL);                // data offset: unused
        Be64(h.data() + 16, tableOffset);
        Be32(h.data() + 24, 0x00010000);          // header version 1.0
        Be32(h.data() + 28, entries);
        Be32(h.data() + 32, vhd::kBlockBytes);
        Be32(h.data() + 36, Checksum(h.data(), h.size(), 36));
        return h;
    }
}  // namespace

namespace vhd
{
    void Geometry(const IBlockDevice& device, uint32_t& cylinders, uint32_t& heads, uint32_t& sectors)
    {
        if (const auto native = device.NativeGeometry();
            native && native->cylinders && native->cylinders <= 65535 && native->heads && native->heads <= 255 && native->sectors &&
            native->sectors <= 255)
        {
            cylinders = native->cylinders;
            heads = native->heads;
            sectors = native->sectors;
            return;
        }
        const uint64_t total = std::min<uint64_t>(device.SectorCount(), 65535ull * 16 * 255);
        uint64_t cylinderTimesHeads = 0;
        if (total >= 65535ull * 16 * 63)
        {
            sectors = 255;
            heads = 16;
            cylinderTimesHeads = total / sectors;
        }
        else
        {
            sectors = 17;
            cylinderTimesHeads = total / sectors;
            heads = static_cast<uint32_t>((cylinderTimesHeads + 1023) / 1024);
            if (heads < 4)
                heads = 4;
            if (cylinderTimesHeads >= heads * 1024ull || heads > 16)
            {
                sectors = 31;
                heads = 16;
                cylinderTimesHeads = total / sectors;
            }
            if (cylinderTimesHeads >= heads * 1024ull)
            {
                sectors = 63;
                heads = 16;
                cylinderTimesHeads = total / sectors;
            }
        }
        cylinders = static_cast<uint32_t>(cylinderTimesHeads / heads);
    }

    std::array<uint8_t, 512> Footer(uint64_t size, uint32_t cylinders, uint32_t heads, uint32_t sectors, uint32_t type,
                                    uint64_t dataOffset, uint64_t identity)
    {
        std::array<uint8_t, 512> f{};
        std::memcpy(f.data(), "conectix", 8);
        Be32(f.data() + 8, 2);               // features: reserved bit set
        Be32(f.data() + 12, 0x00010000);     // version 1.0
        Be64(f.data() + 16, dataOffset);
        Be32(f.data() + 24, 0);              // timestamp: 0, so the same disk gives the same file
        std::memcpy(f.data() + 28, "ung ", 4);
        Be32(f.data() + 32, 0x00010000);
        Be32(f.data() + 36, 0x5769326B);     // "Wi2k"
        Be64(f.data() + 40, size);           // original size
        Be64(f.data() + 48, size);           // current size
        Be16(f.data() + 56, cylinders);
        f[58] = static_cast<uint8_t>(heads);
        f[59] = static_cast<uint8_t>(sectors);
        Be32(f.data() + 60, type);
        uint64_t id = identity;
        for (int i = 0; i < 16; i++)
        {
            id ^= id >> 29;
            id *= 0xBF58476D1CE4E5B9ULL;
            f[68 + i] = static_cast<uint8_t>(id >> 56);
        }
        Be32(f.data() + 64, Checksum(f.data(), f.size(), 64));
        return f;
    }

    uint32_t FooterType(const uint8_t* footer)
    {
        return std::memcmp(footer, "conectix", 8) == 0 ? Get32(footer + 60) : 0;
    }

    bool WriteDynamic(IBlockDevice& device, const std::string& path, std::string* error)
    {
        const uint64_t size = device.SectorCount() * kSector;
        const uint32_t perBlock = kBlockBytes / kSector;
        const uint32_t entries = static_cast<uint32_t>((size + kBlockBytes - 1) / kBlockBytes);
        const uint64_t tableOffset = 512 + 1024;
        const uint64_t tableBytes = (static_cast<uint64_t>(entries) * 4 + kSector - 1) / kSector * kSector;
        uint32_t cylinders = 0, heads = 0, sectors = 0;
        Geometry(device, cylinders, heads, sectors);
        const std::array<uint8_t, 512> footer = Footer(size, cylinders, heads, sectors, kDynamic, 512, device.ContentId());

        std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
        if (!out)
            return Fail(error, "cannot write " + path);
        out.write(reinterpret_cast<const char*>(footer.data()), 512);
        const std::array<uint8_t, 1024> header = DynamicHeader(tableOffset, entries);
        out.write(reinterpret_cast<const char*>(header.data()), 1024);
        std::vector<uint8_t> bat(static_cast<size_t>(tableBytes), 0xFF);
        out.write(reinterpret_cast<const char*>(bat.data()), static_cast<std::streamsize>(bat.size()));

        // Blocks with data, in order; each a full bitmap (every sector present) and its 2 MiB
        uint64_t position = tableOffset + tableBytes;
        std::vector<uint8_t> block(kBlockBytes);
        std::vector<uint8_t> bitmap(kSector, 0xFF);
        for (uint32_t b = 0; b < entries; b++)
        {
            const uint64_t first = static_cast<uint64_t>(b) * perBlock;
            const uint64_t count = std::min<uint64_t>(perBlock, device.SectorCount() - first);
            bool data = false;
            std::fill(block.begin(), block.end(), 0);
            for (uint64_t s = 0; s < count; s++)
            {
                if (const uint64_t run = device.ZeroRun(first + s))
                {
                    s += std::min(run, count - s) - 1;
                    continue;
                }
                uint8_t* sector = block.data() + s * kSector;
                if (!device.ReadSector(first + s, sector))
                    return Fail(error, "cannot read sector " + std::to_string(first + s));
                data = data || std::any_of(sector, sector + kSector, [](uint8_t v) { return v != 0; });
            }
            if (!data)
                continue;
            Be32(bat.data() + static_cast<size_t>(b) * 4, static_cast<uint32_t>(position / kSector));
            out.write(reinterpret_cast<const char*>(bitmap.data()), kSector);
            out.write(reinterpret_cast<const char*>(block.data()), kBlockBytes);
            position += kSector + kBlockBytes;
        }
        out.write(reinterpret_cast<const char*>(footer.data()), 512);
        out.seekp(static_cast<std::streamoff>(tableOffset));
        out.write(reinterpret_cast<const char*>(bat.data()), static_cast<std::streamsize>(bat.size()));
        out.flush();
        if (!out)
            return Fail(error, "write error on " + path);
        return true;
    }
}  // namespace vhd

std::unique_ptr<VhdDynamicImage> VhdDynamicImage::Open(const std::string& path, Access access, std::string* error)
{
    std::unique_ptr<VhdDynamicImage> image(new VhdDynamicImage());
    image->_path = path;
    image->_access = access;
    const std::filesystem::path fsPath = FileHelper::ToFsPath(path);
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(fsPath, ec);
    const auto mode = std::ios::binary | std::ios::in | (access == Access::ReadWrite ? std::ios::out : std::ios::openmode{});
    image->_file.open(fsPath, mode);
    if (ec || !image->_file || size < 512 + 1024)
    {
        Fail(error, "cannot open " + path);
        return nullptr;
    }

    auto readAt = [&image](uint64_t offset, uint8_t* dst, size_t length) {
        image->_file.clear();
        image->_file.seekg(static_cast<std::streamoff>(offset));
        image->_file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(length));
        return static_cast<size_t>(image->_file.gcount()) == length;
    };
    image->_footerOffset = size - 512;
    if (!readAt(image->_footerOffset, image->_footer.data(), 512) || vhd::FooterType(image->_footer.data()) != vhd::kDynamic)
    {
        Fail(error, path + ": not a dynamic VHD");
        return nullptr;
    }
    uint8_t header[1024];
    const uint64_t headerOffset = Get64(image->_footer.data() + 16);
    if (!readAt(headerOffset, header, sizeof header) || std::memcmp(header, "cxsparse", 8) != 0)
    {
        Fail(error, path + ": the dynamic header is missing");
        return nullptr;
    }
    image->_tableOffset = Get64(header + 16);
    const uint32_t entries = Get32(header + 28);
    image->_blockBytes = Get32(header + 32);
    if (image->_blockBytes < kSector || image->_blockBytes % kSector != 0 || image->_blockBytes > 256u * 1024 * 1024)
    {
        Fail(error, path + ": block size " + std::to_string(image->_blockBytes));
        return nullptr;
    }
    const uint32_t perBlock = image->_blockBytes / kSector;
    image->_bitmapBytes = ((perBlock + 7) / 8 + kSector - 1) / kSector * kSector;
    image->_sectors = Get64(image->_footer.data() + 48) / kSector;
    if (static_cast<uint64_t>(entries) * perBlock < image->_sectors || static_cast<uint64_t>(entries) * 4 > size)
    {
        Fail(error, path + ": the block table is shorter than the disk");
        return nullptr;
    }
    std::vector<uint8_t> bat(static_cast<size_t>(entries) * 4);
    if (!readAt(image->_tableOffset, bat.data(), bat.size()))
    {
        Fail(error, path + ": the block table is cut short");
        return nullptr;
    }
    image->_bat.resize(entries);
    for (uint32_t i = 0; i < entries; i++)
        image->_bat[i] = Get32(bat.data() + static_cast<size_t>(i) * 4);
    const uint8_t* f = image->_footer.data();
    BlockGeometry g;
    g.cylinders = static_cast<uint32_t>((f[0x38] << 8) | f[0x39]);
    g.heads = f[0x3A];
    g.sectors = f[0x3B];
    if (g.heads && g.sectors)
        image->_geometry = g;
    uint64_t id = 0xcbf29ce484222325ULL;
    for (char c : path)
    {
        id ^= static_cast<uint8_t>(c);
        id *= 0x100000001b3ULL;
    }
    image->_contentId = id ^ size;
    return image;
}

bool VhdDynamicImage::BitmapBit(uint32_t block, uint32_t sector)
{
    if (_bitmapBlock != block)
    {
        _bitmap.assign(_bitmapBytes, 0);
        _file.clear();
        _file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(_bat[block]) * kSector));
        _file.read(reinterpret_cast<char*>(_bitmap.data()), static_cast<std::streamsize>(_bitmapBytes));
        _file.clear();
        _bitmapBlock = block;
    }
    return (_bitmap[sector / 8] >> (7 - sector % 8)) & 1;
}

bool VhdDynamicImage::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    const uint32_t perBlock = _blockBytes / kSector;
    const uint32_t block = static_cast<uint32_t>(lba / perBlock);
    const uint32_t sector = static_cast<uint32_t>(lba % perBlock);
    if (_bat[block] == kUnused || !BitmapBit(block, sector))
    {
        std::memset(dst, 0, kSector);
        return true;
    }
    _file.clear();
    _file.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(_bat[block]) * kSector + _bitmapBytes + static_cast<uint64_t>(sector) * kSector));
    _file.read(reinterpret_cast<char*>(dst), kSector);
    const bool ok = _file.gcount() == kSector;
    _file.clear();
    return ok;
}

bool VhdDynamicImage::Allocate(uint32_t block)
{
    // The block goes where the footer is: a full bitmap, zeros; then the footer after it, then the table entry
    const uint64_t at = _footerOffset;
    std::vector<uint8_t> bytes(_bitmapBytes + _blockBytes, 0);
    std::fill(bytes.begin(), bytes.begin() + _bitmapBytes, 0xFF);
    _file.clear();
    _file.seekp(static_cast<std::streamoff>(at));
    _file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    _footerOffset = at + bytes.size();
    _file.write(reinterpret_cast<const char*>(_footer.data()), 512);
    uint8_t entry[4];
    Be32(entry, static_cast<uint32_t>(at / kSector));
    _file.seekp(static_cast<std::streamoff>(_tableOffset + static_cast<uint64_t>(block) * 4));
    _file.write(reinterpret_cast<const char*>(entry), 4);
    _file.flush();
    if (!_file)
        return false;
    _bat[block] = static_cast<uint32_t>(at / kSector);
    _bitmapBlock = 0xFFFFFFFF;
    return true;
}

bool VhdDynamicImage::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (_access != Access::ReadWrite || lba >= _sectors)
        return false;
    const uint32_t perBlock = _blockBytes / kSector;
    const uint32_t block = static_cast<uint32_t>(lba / perBlock);
    const uint32_t sector = static_cast<uint32_t>(lba % perBlock);
    if (_bat[block] == kUnused)
    {
        if (std::all_of(src, src + kSector, [](uint8_t v) { return v == 0; }))
            return true;  // it reads zeros already
        if (!Allocate(block))
            return false;
    }
    if (!BitmapBit(block, sector))
    {
        // Another writer's partial bitmap: mark the sector present
        _bitmap[sector / 8] = static_cast<uint8_t>(_bitmap[sector / 8] | (0x80 >> (sector % 8)));
        _file.clear();
        _file.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(_bat[block]) * kSector + sector / 8));
        _file.write(reinterpret_cast<const char*>(&_bitmap[sector / 8]), 1);
    }
    _file.clear();
    _file.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(_bat[block]) * kSector + _bitmapBytes + static_cast<uint64_t>(sector) * kSector));
    _file.write(reinterpret_cast<const char*>(src), kSector);
    _file.flush();
    const bool ok = static_cast<bool>(_file);
    _file.clear();
    return ok;
}

uint64_t VhdDynamicImage::ZeroRun(uint64_t lba)
{
    if (lba >= _sectors)
        return 0;
    const uint32_t perBlock = _blockBytes / kSector;
    uint32_t block = static_cast<uint32_t>(lba / perBlock);
    if (_bat[block] != kUnused)
        return 0;
    while (block < _bat.size() && _bat[block] == kUnused)
        block++;
    return std::min<uint64_t>(static_cast<uint64_t>(block) * perBlock, _sectors) - lba;
}

size_t VhdDynamicImage::AllocatedBlocks() const
{
    return static_cast<size_t>(std::count_if(_bat.begin(), _bat.end(), [](uint32_t e) { return e != kUnused; }));
}
