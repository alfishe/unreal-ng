#pragma once

/// @file vhdimage.h
/// @brief Microsoft VHD (specification 1.0): the 512-byte footer shared by fixed and dynamic images, a dynamic
/// (sparse) image as a block device, read and written in place, and the writer of a dynamic image from any device.
/// A dynamic image is a footer copy, a dynamic header (`cxsparse`), a block allocation table and 2 MiB blocks, each a
/// sector bitmap and its data, allocated when first written; then the footer. An unallocated block reads zeros.
/// Differencing images (type 4) are not supported.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c10-sparse-memory.md §4.

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"

namespace vhd
{
    constexpr uint32_t kFixed = 2;
    constexpr uint32_t kDynamic = 3;
    constexpr uint32_t kDifferencing = 4;
    constexpr uint32_t kBlockBytes = 2 * 1024 * 1024;

    /// CHS for the footer: the device's own geometry when it fits, else the specification's algorithm
    void Geometry(const IBlockDevice& device, uint32_t& cylinders, uint32_t& heads, uint32_t& sectors);

    /// A footer: `size` bytes, `type` (fixed / dynamic), `dataOffset` (dynamic: the header's offset; fixed: none),
    /// timestamp 0 and a UUID from `identity`, so the same disk gives the same file
    std::array<uint8_t, 512> Footer(uint64_t size, uint32_t cylinders, uint32_t heads, uint32_t sectors, uint32_t type,
                                    uint64_t dataOffset, uint64_t identity);

    /// The disk type a footer names (0 when it is not a VHD footer)
    uint32_t FooterType(const uint8_t* footer);

    /// Write `device` as a dynamic VHD: only the blocks with a non-zero sector are stored (known-zero runs are not read)
    bool WriteDynamic(IBlockDevice& device, const std::string& path, std::string* error);
}  // namespace vhd

class VhdDynamicImage : public IBlockDevice
{
public:
    enum class Access : uint8_t
    {
        ReadOnly,
        ReadWrite
    };

    static std::unique_ptr<VhdDynamicImage> Open(const std::string& path, Access access, std::string* error = nullptr);

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return _access == Access::ReadWrite; }
    std::optional<BlockGeometry> NativeGeometry() const override { return _geometry; }
    std::string Describe() const override { return _path + " (dynamic VHD)"; }
    uint64_t ContentId() const override { return _contentId; }
    /// Up to the next allocated block
    uint64_t ZeroRun(uint64_t lba) override;

    /// Blocks holding data (tests)
    size_t AllocatedBlocks() const;

private:
    VhdDynamicImage() = default;
    bool Allocate(uint32_t block);
    bool BitmapBit(uint32_t block, uint32_t sector);

    std::string _path;
    Access _access = Access::ReadOnly;
    std::fstream _file;
    uint64_t _sectors = 0;
    uint64_t _contentId = 0;
    std::optional<BlockGeometry> _geometry;
    uint32_t _blockBytes = vhd::kBlockBytes;
    uint32_t _bitmapBytes = 512;     ///< a block's bitmap, padded to whole sectors
    uint64_t _tableOffset = 0;
    std::vector<uint32_t> _bat;      ///< sector offset of each block, 0xFFFFFFFF: not allocated
    uint64_t _footerOffset = 0;      ///< where the trailing footer is (allocation moves it)
    std::array<uint8_t, 512> _footer{};
    uint32_t _bitmapBlock = 0xFFFFFFFF;
    std::vector<uint8_t> _bitmap;    ///< the cached bitmap of _bitmapBlock
};
