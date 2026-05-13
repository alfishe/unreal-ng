#include "diskimage.h"

/// region <Helper methods>

bool DiskImage::allocateMemory(uint8_t cylinders, uint8_t sides)
{
    bool result = false;

    if (cylinders > 0 && cylinders <= MAX_CYLINDERS && sides > 0 && sides <= 2)
    {
        _cylinders = cylinders;
        _sides = sides;

        // Release memory from previous image data
        releaseMemory();

        /// region <Allocate objects for new disk>
        const size_t trackNumber = cylinders * sides;
        _tracks.resize(trackNumber);
        _tracks.reserve(trackNumber);

        for (Track& track : _tracks)
        {
            track._diskImage = this;
            track.reset();
        }
        /// endregion </Allocate objects for new disk>

        result = true;
    }

    return result;
}

void DiskImage::releaseMemory()
{
    _tracks.clear();
    _tracks.shrink_to_fit();
}

/// endregion </Helper methods>

/// region <Track Change Tracking>

void DiskImage::Track::markDirty()
{
    _dirty = true;
    
    // Auto-propagate to DiskImage
    if (_diskImage)
    {
        _diskImage->markDirty();
    }
}

void DiskImage::Track::markRawTrackDirty()
{
    _rawTrackDirty = true;
    _dirty = true;
    
    // Auto-propagate to DiskImage
    if (_diskImage)
    {
        _diskImage->markDirty();
    }
}

/// endregion </Track Change Tracking>