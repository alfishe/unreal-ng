#include "stdafx.h"

#include "rawimage.h"

#include "common/filehelper.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

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
    return Open(path, access, Layout{}, error);
}

std::unique_ptr<RawImage> RawImage::Open(const std::string& path, Access access, const Layout& layout, std::string* error)
{
    auto fail = [error](const std::string& reason) -> std::unique_ptr<RawImage> {
        if (error)
            *error = reason;
        return nullptr;
    };

    // Paths are UTF-8 strings; FileHelper builds the native path (UTF-16 on Windows)
    const std::filesystem::path fsPath = FileHelper::ToFsPath(path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fsPath, ec))
        return fail("not a regular file: " + path);
    const uint64_t size = std::filesystem::file_size(fsPath, ec);
    if (ec)
        return fail("cannot read the size of " + path);

    std::ios::openmode mode = std::ios::binary | std::ios::in;
    if (access == Access::ReadWrite)
        mode |= std::ios::out;
    std::fstream file(fsPath, mode);
    if (!file)
        return fail(access == Access::ReadWrite ? "cannot open for writing: " + path : "cannot open: " + path);

    if (layout.dataOffset > size || (layout.dataBytes && layout.dataOffset + layout.dataBytes > size))
        return fail("the image header points beyond the end of " + path);

    return std::unique_ptr<RawImage>(new RawImage(path, access, std::move(file), size, layout));
}

RawImage::RawImage(std::string path, Access access, std::fstream file, uint64_t sizeBytes, const Layout& layout)
    : _path(std::move(path)), _access(access), _file(std::move(file)), _layout(layout)
{
    // _sizeBytes: the disk's bytes as stored (the region after the header)
    _fixedSize = layout.dataBytes != 0 || layout.halved;
    _sizeBytes = layout.dataBytes ? layout.dataBytes : sizeBytes - layout.dataOffset;
    _sectors = (_sizeBytes + StoredSectorBytes() - 1) / StoredSectorBytes();
    _contentId = HashIdentity(_path, sizeBytes);
}

RawImage::~RawImage()
{
    Flush();
#if !defined(_WIN32)
    if (_holeFd >= 0)
        close(_holeFd);
#endif
}

uint64_t RawImage::ZeroRun(uint64_t lba)
{
#if defined(SEEK_DATA) && !defined(_WIN32)
    if (lba >= _sectors || _layout.halved || _holeFd == -2)
        return 0;
    if (_holeFd < 0)
    {
        _holeFd = open(_path.c_str(), O_RDONLY);
        if (_holeFd < 0)
        {
            _holeFd = -2;
            return 0;
        }
    }
    // Every write is flushed (WriteSector), so the file's allocation is current
    const off_t at = static_cast<off_t>(_layout.dataOffset + lba * kSectorSize);
    const off_t data = lseek(_holeFd, at, SEEK_DATA);
    const uint64_t left = _sectors - lba;
    if (data < 0)
        return errno == ENXIO ? left : 0;  // no data from here to the end of the file
    if (data <= at)
        return 0;
    return std::min<uint64_t>(static_cast<uint64_t>(data - at) / kSectorSize, left);
#else
    (void)lba;
    return 0;
#endif
}

bool RawImage::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;

    std::memset(dst, 0, kSectorSize);
    const uint32_t stored = StoredSectorBytes();
    const uint64_t offset = lba * stored;
    const uint64_t available = std::min<uint64_t>(stored, _sizeBytes - offset);

    uint8_t low[kSectorSize / 2];
    uint8_t* target = _layout.halved ? low : dst;
    _file.clear();
    _file.seekg(static_cast<std::streamoff>(_layout.dataOffset + offset));
    _file.read(reinterpret_cast<char*>(target), static_cast<std::streamsize>(available));
    const bool ok = static_cast<uint64_t>(_file.gcount()) == available;
    _file.clear();
    if (_layout.halved)
    {
        for (uint64_t i = 0; i < available; i++)
            dst[i * 2] = low[i];  // the high byte of each word was never stored
    }
    return ok;
}

bool RawImage::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (_access != Access::ReadWrite || lba >= _sectors)
        return false;

    const uint32_t stored = StoredSectorBytes();
    uint8_t low[kSectorSize / 2];
    const uint8_t* source = src;
    if (_layout.halved)
    {
        for (uint32_t i = 0; i < stored; i++)
            low[i] = src[i * 2];
        source = low;
    }
    const uint64_t offset = lba * stored;
    const uint64_t length = _fixedSize ? std::min<uint64_t>(stored, _sizeBytes - offset) : stored;

    _file.clear();
    _file.seekp(static_cast<std::streamoff>(_layout.dataOffset + offset));
    _file.write(reinterpret_cast<const char*>(source), static_cast<std::streamsize>(length));
    _file.flush();
    const bool ok = static_cast<bool>(_file);
    _file.clear();

    // A write to the padded last sector made the file whole
    if (!_fixedSize)
        _sizeBytes = std::max<uint64_t>(_sizeBytes, (lba + 1) * stored);
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
