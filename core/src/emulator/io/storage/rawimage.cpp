#include "stdafx.h"

#include "rawimage.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace
{
    /// FNV-1a over the path and size: the same file at the same size is the same medium
    uint64_t HashIdentity(const std::string& path, uint64_t size)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (const char c : path)
        {
            h ^= static_cast<uint8_t>(c);
            h *= 0x100000001b3ULL;
        }
        for (int i = 0; i < 8; i++)
        {
            h ^= static_cast<uint8_t>(size >> (8 * i));
            h *= 0x100000001b3ULL;
        }
        return h;
    }
}  // namespace

std::unique_ptr<RawImage> RawImage::Open(const std::string& path, Access access, std::string* error)
{
    auto fail = [error](const std::string& reason) -> std::unique_ptr<RawImage> {
        if (error)
            *error = reason;
        return nullptr;
    };

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return fail("not a regular file: " + path);
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec)
        return fail("cannot read the size of " + path);

    std::ios::openmode mode = std::ios::binary | std::ios::in;
    if (access == Access::ReadWrite)
        mode |= std::ios::out;
    std::fstream file(path, mode);
    if (!file)
        return fail(access == Access::ReadWrite ? "cannot open for writing: " + path : "cannot open: " + path);

    return std::unique_ptr<RawImage>(new RawImage(path, access, std::move(file), size));
}

RawImage::RawImage(std::string path, Access access, std::fstream file, uint64_t sizeBytes)
    : _path(std::move(path)), _access(access), _file(std::move(file)), _sizeBytes(sizeBytes)
{
    _sectors = (_sizeBytes + kSectorSize - 1) / kSectorSize;
    _contentId = HashIdentity(_path, _sizeBytes);
}

RawImage::~RawImage()
{
    Flush();
}

bool RawImage::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;

    std::memset(dst, 0, kSectorSize);
    const uint64_t offset = lba * kSectorSize;
    const uint64_t available = std::min<uint64_t>(kSectorSize, _sizeBytes - offset);

    _file.clear();
    _file.seekg(static_cast<std::streamoff>(offset));
    _file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(available));
    const bool ok = static_cast<uint64_t>(_file.gcount()) == available;
    _file.clear();
    return ok;
}

bool RawImage::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (_access != Access::ReadWrite || lba >= _sectors)
        return false;

    _file.clear();
    _file.seekp(static_cast<std::streamoff>(lba * kSectorSize));
    _file.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(kSectorSize));
    _file.flush();
    const bool ok = static_cast<bool>(_file);
    _file.clear();

    // A write to the padded last sector made the file whole
    _sizeBytes = std::max<uint64_t>(_sizeBytes, (lba + 1) * kSectorSize);
    return ok;
}

void RawImage::Flush()
{
    if (_access == Access::ReadWrite && _file.is_open())
    {
        _file.flush();
        _file.clear();
    }
}
