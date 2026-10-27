#include "unrealdisk/floppyblockdevice.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/diskimage.h"

namespace unrealdisk
{
namespace
{
DiskImage::Track* TrackOf(DiskImage& image, uint32_t sides, uint32_t sectorsPerTrack, uint64_t count, uint64_t lba)
{
    if (lba >= count)
        return nullptr;
    const uint64_t track = lba / sectorsPerTrack;
    return image.getTrackForCylinderAndSide(static_cast<uint8_t>(track / sides), static_cast<uint8_t>(track % sides));
}
}  // namespace

FloppyBlockDevice::FloppyBlockDevice(DiskImage& image, bool writable) : _image(image), _writable(writable)
{
    DiskImage::Track* track = image.getTrackForCylinderAndSide(0, 0);
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
    DiskImage::Track* t = TrackOf(_image, _sides, _sectorsPerTrack, _sectorCount, lba);
    const auto* sector = t ? t->findSector(static_cast<uint8_t>(_firstId + lba % _sectorsPerTrack)) : nullptr;
    if (!sector || !sector->data || sector->dataSize < kSectorSize)
        return false;
    std::memcpy(dst, sector->data, kSectorSize);
    return true;
}

bool FloppyBlockDevice::WriteSector(uint64_t lba, const uint8_t* src)
{
    DiskImage::Track* t = _writable ? TrackOf(_image, _sides, _sectorsPerTrack, _sectorCount, lba) : nullptr;
    const uint8_t id = static_cast<uint8_t>(_firstId + lba % _sectorsPerTrack);
    const auto* sector = t && id != 0 ? t->findSector(id) : nullptr;
    if (!sector || !sector->data || sector->dataSize < kSectorSize)
        return false;
    t->writeSectorData(static_cast<uint8_t>(id - 1), src, kSectorSize);
    return true;
}
}  // namespace unrealdisk
