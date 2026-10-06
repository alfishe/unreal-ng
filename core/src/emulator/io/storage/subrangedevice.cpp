#include "stdafx.h"

#include "subrangedevice.h"

#include <algorithm>
#include <cstring>

SubRangeDevice::SubRangeDevice(std::shared_ptr<IBlockDevice> base, uint64_t first, uint64_t count)
    : _base(std::move(base)), _first(first), _count(count)
{
    // The base's identity and the window (FNV-1a)
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint64_t v : {_base->ContentId(), _first, _count})
    {
        for (int i = 0; i < 8; i++)
        {
            h ^= static_cast<uint8_t>(v >> (8 * i));
            h *= 0x100000001b3ULL;
        }
    }
    _contentId = h;
}

uint64_t SubRangeDevice::ZeroRun(uint64_t lba)
{
    if (lba >= _count)
        return 0;
    const uint64_t base = _base->SectorCount();
    if (_first + lba >= base)
        return _count - lba;  // past a cut-down image's end
    return std::min<uint64_t>(_base->ZeroRun(_first + lba), _count - lba);
}

bool SubRangeDevice::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _count)
        return false;
    if (_first + lba >= _base->SectorCount())
    {
        std::memset(dst, 0, kSectorSize);  // a cut-down image: the window goes on past the file's end
        return true;
    }
    return _base->ReadSector(_first + lba, dst);
}

bool SubRangeDevice::WriteSector(uint64_t lba, const uint8_t* src)
{
    return lba < _count && _base->IsWritable() && _base->WriteSector(_first + lba, src);
}

std::string SubRangeDevice::Describe() const
{
    return _base->Describe() + ", sectors " + std::to_string(_first) + "-" + std::to_string(_first + _count - 1);
}
