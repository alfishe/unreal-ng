#pragma once

/// @file rawimage.h
/// @brief A plain disk image file (.img, .hdd, .ima): sector n at byte n * 512.
///
/// A size that is not a multiple of 512 reads its last partial sector padded
/// with zeros. Writes go straight to the file (write-through); a write to that
/// padded last sector extends the file to a whole sector. Opened read-only,
/// every write fails, and the file is never modified.

#include <fstream>
#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

class RawImage : public IBlockDevice
{
public:
    enum class Access : uint8_t
    {
        ReadOnly,
        ReadWrite
    };

    /// Open an existing image. Returns nullptr (and a reason in `error`) when
    /// the file is missing, is not a regular file, or cannot be opened for `access`
    static std::unique_ptr<RawImage> Open(const std::string& path, Access access, std::string* error = nullptr);

    ~RawImage() override;

    RawImage(const RawImage&) = delete;
    RawImage& operator=(const RawImage&) = delete;

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return _access == Access::ReadWrite; }
    std::string Describe() const override { return _path; }
    uint64_t ContentId() const override { return _contentId; }

    const std::string& Path() const { return _path; }
    uint64_t SizeBytes() const { return _sizeBytes; }

    /// Push buffered writes to the file (also done on every write and on destruction)
    void Flush();

private:
    RawImage(std::string path, Access access, std::fstream file, uint64_t sizeBytes);

    std::string _path;
    Access _access;
    std::fstream _file;
    uint64_t _sizeBytes = 0;
    uint64_t _sectors = 0;
    uint64_t _contentId = 0;
};
