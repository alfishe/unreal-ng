#include "stdafx.h"

#include "extentreader.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/iblockdevice.h"

bool ExtentReader::ReadFileSector(const FileData& data, uint64_t fileSector, uint8_t* dst)
{
    const uint64_t offset = fileSector * kSectorSize;
    if (offset >= data.bytes)
    {
        std::memset(dst, 0, kSectorSize);
        return true;
    }
    const size_t wanted = static_cast<size_t>(std::min<uint64_t>(kSectorSize, data.bytes - offset));

    size_t got = 0;
    switch (data.storage)
    {
        case FileData::Storage::Zero:
            break;
        case FileData::Storage::HostFile:
            got = _pool.ReadHost(data.hostFile, offset, dst, wanted);
            break;
        case FileData::Storage::DeviceExtents:
        {
            const Extent* extent = FindExtent(data, fileSector);
            if (!extent)
                break;  // a hole between extents: zeros
            if (!_pool.Device(data.source).ReadSector(extent->sourceLba + (fileSector - extent->fileSectorStart), dst))
            {
                std::memset(dst, 0, kSectorSize);
                return false;
            }
            got = wanted;
            break;
        }
    }
    // The rest of the sector: past the file's end (slack) or what a short host file did not give
    std::memset(dst + got, 0, kSectorSize - got);
    return true;
}

const Extent* ExtentReader::FindExtent(const FileData& data, uint64_t fileSector)
{
    const Extent* first = _extents.data() + data.firstExtent;
    const Extent* last = first + data.extentCount;
    auto contains = [fileSector](const Extent* e) {
        return fileSector >= e->fileSectorStart && fileSector < static_cast<uint64_t>(e->fileSectorStart) + e->sectors;
    };

    // Sequential reads: the extent of the previous read, or the next one
    if (_lastData == &data && _lastExtent < data.extentCount)
    {
        const Extent* e = first + _lastExtent;
        if (contains(e))
            return e;
        if (e + 1 < last && contains(e + 1))
        {
            _lastExtent++;
            return e + 1;
        }
    }

    _searches++;
    const Extent* it = std::upper_bound(first, last, fileSector,
                                        [](uint64_t s, const Extent& e) { return s < e.fileSectorStart; });
    if (it == first)
        return nullptr;
    --it;
    if (!contains(it))
        return nullptr;
    _lastData = &data;
    _lastExtent = static_cast<uint32_t>(it - first);
    return it;
}
