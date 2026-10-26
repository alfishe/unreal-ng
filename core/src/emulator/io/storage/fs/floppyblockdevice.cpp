#include "emulator/io/storage/fs/floppyblockdevice.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/diskimage.h"

FloppyBlockDevice::FloppyBlockDevice(unrealdisk::DiskImage& image) : _image(image)
{
    unrealdisk::DiskImage::Track* track = image.getTrackForCylinderAndSide(0, 0);
    if (!track || track->sectorCount() == 0)
        return;
    uint8_t low = 0xFF;
    uint8_t high = 0;
    for (const auto& sector : track->sectors())
    {
        if (!sector.data || sector.dataSize != kSectorSize)
            return;
        low = std::min(low, sector.number());
        high = std::max(high, sector.number());
    }
    // Consecutive ids, each once
    if (static_cast<size_t>(high - low + 1) != track->sectorCount())
        return;
    _firstId = low;
    _sectorsPerTrack = static_cast<uint32_t>(track->sectorCount());
    _sides = image.getSides() ? image.getSides() : 1;
    _sectorCount = static_cast<uint64_t>(image.getCylinders()) * _sides * _sectorsPerTrack;
}

bool FloppyBlockDevice::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectorCount)
        return false;
    const uint64_t track = lba / _sectorsPerTrack;
    unrealdisk::DiskImage::Track* t = _image.getTrackForCylinderAndSide(static_cast<uint8_t>(track / _sides),
                                                                         static_cast<uint8_t>(track % _sides));
    const auto* sector = t ? t->findSector(static_cast<uint8_t>(_firstId + lba % _sectorsPerTrack)) : nullptr;
    if (!sector || !sector->data || sector->dataSize < kSectorSize)
        return false;
    std::memcpy(dst, sector->data, kSectorSize);
    return true;
}
