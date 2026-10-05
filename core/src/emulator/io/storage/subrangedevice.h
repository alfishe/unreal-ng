#pragma once

/// @file subrangedevice.h
/// @brief A window of another block device: sectors [first, first + count) of
/// the base appear as sectors [0, count). A partition of a disk image read as
/// its own volume (a FAT image layer, a passthrough partition of a composed
/// disk). Reads and writes outside the window fail; it is writable only when
/// the base is. Design: docs/inprogress/2026-10-05-media-multisource/phases/c3-image-sources.md.

#include <memory>

#include "emulator/io/storage/iblockdevice.h"

class SubRangeDevice : public IBlockDevice
{
public:
    SubRangeDevice(std::shared_ptr<IBlockDevice> base, uint64_t first, uint64_t count);

    uint64_t SectorCount() const override { return _count; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return _base->IsWritable(); }
    std::string Describe() const override;
    uint64_t ContentId() const override { return _contentId; }

    uint64_t First() const { return _first; }

private:
    std::shared_ptr<IBlockDevice> _base;
    uint64_t _first = 0;
    uint64_t _count = 0;
    uint64_t _contentId = 0;
};
