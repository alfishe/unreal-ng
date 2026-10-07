#pragma once

/// @file partitioneddisk.h
/// @brief A disk made of partitions: a synthesized MBR (and an EBR chain past
/// four partitions) in front of child volumes, each at its own LBA 0. A
/// partition may be a passthrough window of an image or a composed volume.
/// Read-only: guest writes go to the change layer above, as for every
/// composite. Design: docs/inprogress/2026-10-05-media-multisource/phases/c7-partitions.md.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"

class IComposedLayout;

class PartitionedDisk : public IBlockDevice
{
public:
    static constexpr uint64_t kAlign = 2048;  ///< 1 MiB

    struct Part
    {
        std::string name;
        uint8_t type = 0;
        uint64_t sectors = 0;                  ///< the partition's size
        std::shared_ptr<IBlockDevice> device;  ///< its volume at its own LBA 0; may be shorter (zeros after it)
        int fatBits = 0;                       ///< 12 / 16 / 32: the type when `type` is 0 (§3 of the design)
        /// The composed layout of `device` (provenance, `media changes`), and where the device's sector 0 is in it
        const IComposedLayout* layout = nullptr;
        uint64_t layoutOffset = 0;
        uint64_t start = 0;                    ///< set by Build
        bool logical = false;                  ///< set by Build: in the extended partition
    };

    /// Lay `parts` out (1 MiB aligned, in order; past four: three primary, the rest logical).
    /// `totalSectors`: the disk's size, at least the layout's; none: the end of the last partition
    static std::unique_ptr<PartitionedDisk> Build(std::vector<Part> parts, std::optional<uint64_t> totalSectors,
                                                  std::string description, std::string* error);

    /// D-6: the MBR's code area (bytes 0-445, a disk signature at 440 included) from a source disk; empty: no code
    /// and a signature of our own. The first partition is the active one
    void SetMbrCode(std::vector<uint8_t> code) { _mbrCode = std::move(code); }

    /// The type byte for a FAT volume of `sectors` at `start` (12, 16 or 32 bits)
    static uint8_t FatType(int bits, uint64_t start, uint64_t sectors);

    uint64_t SectorCount() const override { return _totalSectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    uint64_t ZeroRun(uint64_t lba) override;
    std::string Describe() const override;
    uint64_t ContentId() const override { return _contentId; }
    /// The composite's id (its descriptor mixed in): the session delta and `media layers` name it
    void SetContentId(uint64_t id) { _contentId = id; }

    const std::vector<Part>& Parts() const { return _parts; }
    /// The MBR and every EBR
    const std::vector<uint64_t>& TableSectors() const { return _tables; }

private:
    PartitionedDisk() = default;

    void BuildTable(uint64_t lba, uint8_t* dst) const;
    static void Entry(uint8_t* e, uint8_t type, uint64_t first, uint64_t count, uint64_t absoluteFirst);

    std::vector<Part> _parts;
    std::vector<uint64_t> _tables;     ///< 0, then each logical partition's EBR
    uint64_t _extendedStart = 0;       ///< the first EBR (0: no extended partition)
    uint64_t _extendedEnd = 0;
    uint64_t _totalSectors = 0;
    uint64_t _contentId = 0;
    std::string _description;
    std::vector<uint8_t> _mbrCode;
};
