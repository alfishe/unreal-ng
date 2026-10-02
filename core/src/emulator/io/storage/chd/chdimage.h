#pragma once

/// @file chdimage.h
/// @brief A hard-disk CHD as a block device: the shared storage seam
/// (IBlockDevice) over a ChdFile, so every IDE unit and SD card reads it like
/// any other image. Read-only: guest writes go to the medium's change layer
/// (SessionWriteMap) and reach a CHD only through an explicit save.
///
/// Decompressed hunks stay in a small cache (64 hunks, 256 KB for chdman's
/// 4 KB hunks): a sector read in a hunk read before costs a copy.
/// The `GDDD` geometry is the native geometry the ATA IDENTIFY reports.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/chd/chdfile.h"
#include "emulator/io/storage/iblockdevice.h"

class ChdImage : public IBlockDevice
{
public:
    /// Open a hard-disk CHD (parents found next to it). nullptr with a reason
    /// for anything not a readable CHD of 512-byte sectors
    static std::unique_ptr<ChdImage> Open(const std::string& path, std::string* error = nullptr);
    explicit ChdImage(std::unique_ptr<chd::ChdFile> file);

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::optional<BlockGeometry> NativeGeometry() const override { return _geometry; }
    std::string Describe() const override { return _file->Path(); }
    uint64_t ContentId() const override { return _contentId; }

    chd::ChdFile& File() { return *_file; }
    const chd::ChdFile& File() const { return *_file; }

    /// Hunk reads that missed the cache (tests, benchmarks)
    uint64_t HunkReads() const { return _hunkReads; }
    /// The reason of the last failed read
    const std::string& LastError() const { return _lastError; }

private:
    static constexpr uint32_t kCacheHunks = 64;

    std::unique_ptr<chd::ChdFile> _file;
    uint64_t _sectors = 0;
    uint64_t _contentId = 0;
    std::optional<BlockGeometry> _geometry;
    std::vector<uint8_t> _cache;
    std::vector<int64_t> _cacheTags;
    uint64_t _hunkReads = 0;
    std::string _lastError;
};
