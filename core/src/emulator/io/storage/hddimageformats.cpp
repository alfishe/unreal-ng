#include "stdafx.h"

#include "hddimageformats.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/chd/chdimage.h"

namespace
{
    uint32_t Le32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }

    uint64_t Be64(const uint8_t* p)
    {
        uint64_t value = 0;
        for (int i = 0; i < 8; i++)
            value = (value << 8) | p[i];
        return value;
    }

    bool ReadAt(std::ifstream& in, uint64_t offset, uint8_t* out, size_t length)
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(length));
        return static_cast<size_t>(in.gcount()) == length;
    }

    struct Header
    {
        uint64_t size = 0;
        uint8_t head[512] = {};
        bool headRead = false;
        uint8_t tail[512] = {};
        bool tailRead = false;
        uint8_t iso[6] = {};
        bool isoRead = false;
    };

    bool ReadHeader(const std::string& path, Header& h, std::string* error)
    {
        const std::filesystem::path fsPath = FileHelper::ToFsPath(path);
        std::error_code ec;
        h.size = std::filesystem::file_size(fsPath, ec);
        std::ifstream in(fsPath, std::ios::binary);
        if (ec || !in)
        {
            if (error)
                *error = "cannot read " + path;
            return false;
        }
        h.headRead = ReadAt(in, 0, h.head, std::min<uint64_t>(sizeof(h.head), h.size));
        h.tailRead = h.size >= 512 && ReadAt(in, h.size - 512, h.tail, 512);
        h.isoRead = h.size >= 0x8006 && ReadAt(in, 0x8000, h.iso, 6);
        return true;
    }

    bool Fail(std::string* error, const std::string& reason)
    {
        if (error)
            *error = reason;
        return false;
    }

    bool Layout(const std::string& path, const std::string& format, RawImage::Layout& layout, std::string* error)
    {
        layout = RawImage::Layout{};
        if (format == "raw" || format == "iso")
            return true;

        Header h;
        if (!ReadHeader(path, h, error))
            return false;
        if (format == "hdf")
        {
            if (!h.headRead || h.size < 0x16 + 14)
                return Fail(error, path + ": HDF header cut short");
            layout.dataOffset = static_cast<uint64_t>(h.head[9] | (h.head[10] << 8));
            layout.halved = h.head[8] & 0x01;
            const uint8_t* identify = h.head + 0x16;  // IDENTIFY words 1, 3, 6
            BlockGeometry g;
            g.cylinders = static_cast<uint32_t>(identify[2] | (identify[3] << 8));
            g.heads = static_cast<uint32_t>(identify[6] | (identify[7] << 8));
            g.sectors = static_cast<uint32_t>(identify[12] | (identify[13] << 8));
            if (g.heads && g.sectors)
                layout.geometry = g;
            return true;
        }
        if (format == "vhd")
        {
            const uint8_t* f = h.tail;
            const uint32_t type = (static_cast<uint32_t>(f[0x3C]) << 24) | (static_cast<uint32_t>(f[0x3D]) << 16) |
                                  (static_cast<uint32_t>(f[0x3E]) << 8) | f[0x3F];
            if (type != 2)
                return Fail(error, path + ": only fixed VHD images are supported (this one is type " + std::to_string(type) + ")");
            layout.dataBytes = Be64(f + 0x30);  // current size
            if (!layout.dataBytes || layout.dataBytes > h.size - 512)
                layout.dataBytes = h.size - 512;
            BlockGeometry g;
            g.cylinders = static_cast<uint32_t>((f[0x38] << 8) | f[0x39]);
            g.heads = f[0x3A];
            g.sectors = f[0x3B];
            if (g.heads && g.sectors)
                layout.geometry = g;
            return true;
        }
        if (format == "hdi")
        {
            if (!h.headRead || h.size < 32)
                return Fail(error, path + ": HDI header cut short");
            layout.dataOffset = Le32(h.head + 8);
            layout.dataBytes = Le32(h.head + 12);
            if (Le32(h.head + 16) != 512)
                return Fail(error, path + ": HDI with " + std::to_string(Le32(h.head + 16)) + "-byte sectors (512 expected)");
            BlockGeometry g;
            g.sectors = Le32(h.head + 20);
            g.heads = Le32(h.head + 24);
            g.cylinders = Le32(h.head + 28);
            if (g.heads && g.sectors)
                layout.geometry = g;
            return true;
        }
        return Fail(error, "unknown image format '" + format + "'");
    }
}  // namespace

std::string HddImageFormats::Probe(const std::string& path, std::string* error)
{
    Header h;
    if (!ReadHeader(path, h, error))
        return {};
    if (h.headRead && h.size >= 16 && std::memcmp(h.head, "MComprHD", 8) == 0)
        return "chd";
    if (h.isoRead && std::memcmp(h.iso + 1, "CD001", 5) == 0)
        return "iso";
    if (h.headRead && h.size >= 16 && std::memcmp(h.head, "RS-IDE", 6) == 0)
        return "hdf";
    if (h.tailRead && std::memcmp(h.tail, "conectix", 8) == 0)
        return "vhd";
    if (StringHelper::ToLower(FileHelper::GetFileExtension(path)) == "hdi")
        return "hdi";
    return "raw";
}

std::unique_ptr<RawImage> HddImageFormats::Open(const std::string& path, const std::string& format, RawImage::Access access,
                                                std::string* error)
{
    RawImage::Layout layout;
    if (!Layout(path, format, layout, error))
        return nullptr;
    return RawImage::Open(path, format == "iso" ? RawImage::Access::ReadOnly : access, layout, error);
}

std::unique_ptr<IBlockDevice> HddImageFormats::OpenBlock(const std::string& path, const std::string& format,
                                                         RawImage::Access access, std::string* error)
{
    if (format == "chd")
        return ChdImage::Open(path, error);
    return Open(path, format, access, error);
}

bool HddImageFormats::IsHardDiskExtension(const std::string& extension)
{
    const std::string ext = StringHelper::ToLower(extension);
    return ext == "hdd" || ext == "hd" || ext == "hdf" || ext == "hdi" || ext == "vhd" || ext == "chd";
}
