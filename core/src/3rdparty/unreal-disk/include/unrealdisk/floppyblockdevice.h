#pragma once

/// @file floppyblockdevice.h
/// @brief A floppy of uniform 512-byte sectors (a PC / MS-DOS format: Sprinter DSS, 720K / 1.44M) as a BlockDevice,
/// so the FAT reader works on it as on a hard disk: LBA = (cylinder * sides + side) * sectors per track + the sector's
/// position, sector ids counted from the lowest id of track 0. A write changes the sector in the image (its CRC and
/// the track's dirty flag follow, as after a guest write); `writable` false refuses writes. The image must outlive the
/// device.

#include <cstdint>
#include <string>

#include "unrealdisk/blockdevice.h"

namespace unrealdisk
{
class DiskImage;

class FloppyBlockDevice : public BlockDevice
{
public:
    explicit FloppyBlockDevice(DiskImage& image, bool writable = false);

    /// Track 0 holds 512-byte sectors with consecutive ids (the device has sectors)
    bool IsUniform() const { return _sectorsPerTrack != 0; }
    uint32_t SectorsPerTrack() const { return _sectorsPerTrack; }

    uint64_t SectorCount() const override { return _sectorCount; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return _writable; }
    std::string Describe() const override { return "floppy as 512-byte sectors"; }
    uint64_t ContentId() const override { return 0; }

private:
    DiskImage& _image;
    bool _writable = false;
    uint32_t _sides = 1;
    uint32_t _sectorsPerTrack = 0;
    uint8_t _firstId = 1;
    uint64_t _sectorCount = 0;
};
}  // namespace unrealdisk
