#pragma once

/// @file iblockdevice.h
/// @brief A disk as a numbered list of 512-byte sectors: the one storage seam
/// shared by every emulated drive that reads raw sectors (SD cards, IDE hard
/// disks). Image formats, host folders and in-memory disks implement it;
/// drives never know which one they talk to.
///
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §7.1,
/// shared with the SD card by docs/inprogress/2026-09-15-atm-baseconf-highres-ports/
/// tdd-storage-sd-ide-cd.md §1 (S1).

#include <cstdint>
#include <optional>
#include <string>

/// Cylinders / heads / sectors carried by an image format's header (IDE only)
struct BlockGeometry
{
    uint32_t cylinders = 0;
    uint32_t heads = 0;
    uint32_t sectors = 0;
};

class IBlockDevice
{
public:
    static constexpr size_t kSectorSize = 512;

    virtual ~IBlockDevice() = default;

    virtual uint64_t SectorCount() const = 0;

    /// Read sector `lba` into `dst` (512 bytes). False beyond the end or on a
    /// host I/O error; `dst` is then undefined
    virtual bool ReadSector(uint64_t lba, uint8_t* dst) = 0;

    /// Write 512 bytes from `src` to sector `lba`. False beyond the end, on a
    /// read-only device or on a host I/O error
    virtual bool WriteSector(uint64_t lba, const uint8_t* src) = 0;

    virtual bool IsWritable() const = 0;

    /// How many sectors from `lba` on are known to read as zeros without reading them (0: not known, read it).
    /// Exports and the CHD writer skip such runs (docs/inprogress/2026-10-05-media-multisource/phases/c10-sparse-memory.md §2)
    virtual uint64_t ZeroRun(uint64_t lba)
    {
        (void)lba;
        return 0;
    }

    /// Geometry from the image header, if the format has one
    virtual std::optional<BlockGeometry> NativeGeometry() const { return std::nullopt; }

    /// Short human-readable description (file path, "memory disk", ...)
    virtual std::string Describe() const = 0;

    /// Identity of the medium for snapshots and TTD: equal ids mean the same
    /// medium with the same contents at attach time
    virtual uint64_t ContentId() const = 0;
};
