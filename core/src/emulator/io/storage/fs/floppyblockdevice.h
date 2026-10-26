#pragma once

/// @file floppyblockdevice.h
/// @brief A floppy of uniform 512-byte sectors (a PC / MS-DOS format: Sprinter DSS, 720K / 1.44M) as an IBlockDevice,
/// so the FAT reader works on it as on a hard disk: LBA = (cylinder * sides + side) * sectors per track + the sector's
/// position, sector ids counted from the lowest id of track 0. Read only; the image must outlive the device.

#include <cstdint>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

namespace unrealdisk
{
class DiskImage;
}

class FloppyBlockDevice : public IBlockDevice
{
public:
    explicit FloppyBlockDevice(unrealdisk::DiskImage& image);

    /// Track 0 holds 512-byte sectors with consecutive ids (the device has sectors)
    bool IsUniform() const { return _sectorsPerTrack != 0; }
    uint32_t SectorsPerTrack() const { return _sectorsPerTrack; }

    uint64_t SectorCount() const override { return _sectorCount; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::string Describe() const override { return "floppy as 512-byte sectors"; }
    uint64_t ContentId() const override { return 0; }

private:
    unrealdisk::DiskImage& _image;
    uint32_t _sides = 1;
    uint32_t _sectorsPerTrack = 0;
    uint8_t _firstId = 1;
    uint64_t _sectorCount = 0;
};
