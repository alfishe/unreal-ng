#include "diskimage.h"


/// region <Methods>
bool DiskImage::setRawDiskImageData(uint8_t* buffer, size_t size)
{
    bool result = false;

    if (!_loadedDisk)
    {
        _loadedDisk = new Disk();
        _loadedDisk->rawData = buffer;
        _loadedDisk->rawDataSize = size;

        _loaded = true;
    }

    return result;
}

/// endregion </Methods>