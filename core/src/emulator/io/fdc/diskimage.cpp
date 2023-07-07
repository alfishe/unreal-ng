#include "diskimage.h"


/// region <Methods>

bool DiskImage::createNewDisk(uint8_t cylinders, uint8_t sides)
{
    bool result = false;

    if (cylinders > 0 && cylinders <= MAX_CYLINDERS && sides > 0 && sides <= 2)
    {
        _cylinders = cylinders;
        _sides = sides;
        _fullRawImageSize = _cylinders * _sides * _fullTrackSizeInBytes;

        // Release memory from previous image data
        releaseMemory();

        // Allocate memory sufficient to fit disk image with required characteristics
        _loadedDisk = new Disk();
        _loadedDisk->rawData = new uint8_t[_fullRawImageSize];
        _loadedDisk->rawDataSize = _fullRawImageSize;

        /// region <Populate indexing information>

        Disk& disk = *_loadedDisk;
        uint8_t(&trackLengths)[MAX_CYLINDERS][2] = disk.trackLengths;
        uint8_t*(&trackData)[MAX_CYLINDERS][2] = disk.trackData;
        uint8_t*(&trackSynchro)[MAX_CYLINDERS][2] = disk.trackSynchro;
        uint8_t*(&trackBads)[MAX_CYLINDERS][2] = disk.trackBads;

        for (uint8_t cylinder = 0; cylinder < _cylinders; cylinder++)
        {
            for (uint8_t head = 0; head < _sides; head++)
            {
                trackLengths[cylinders][head] = _trackSizeInBytes;
                trackData[cylinders][head] = disk.rawData + _fullTrackSizeInBytes * (cylinder * _sides + head); // Sector data first
                trackSynchro[cylinders][head] = trackData[cylinders][head] + _fullTrackSizeInBytes;             // Synchro bitmap next
                trackBads[cylinders][head] = trackSynchro[cylinders][head] + _surfaceBitmapLen;                 // Bads bitmap is the last
            }
        }

        /// endregion </Populate indexing information>

        result = true;
    }

    return result;
}

bool DiskImage::setRawDiskImageData(uint8_t* buffer, size_t size)
{
    bool result = false;

    releaseMemory();

    _loadedDisk = new Disk();
    _loadedDisk->rawData = buffer;
    _loadedDisk->rawDataSize = size;

    _loaded = true;

    return result;
}

/// endregion </Methods>

/// region <Helper methods>
void DiskImage::releaseMemory()
{
    if (_loadedDisk)
    {
        if (_loadedDisk->rawData)
        {
            delete _loadedDisk->rawData;
            _loadedDisk->rawData = nullptr;
        }

        delete _loadedDisk;
        _loadedDisk = nullptr;
    }
}
/// endregion </Helper methods>