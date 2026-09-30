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
        const size_t trackNumber = static_cast<size_t>(cylinders) * sides;
        _tracks.resize(trackNumber);

        for (size_t i = 0; i < trackNumber; i++)
        {
            Track& track = _tracks[i];
            track._diskImage = this;
            track._cylinder = static_cast<uint8_t>(i / sides);
            track._side = static_cast<uint8_t>(i % sides);

            // Blank TR-DOS formatted track (16 x 256, sectors 1..16, valid CRCs, clock marks on every A1).
            // The first track is formatted for real; the others are stamped from it (same stream, ID fields
            // re-addressed) - formatting and re-indexing 160 tracks byte by byte dominated image creation
            if (i == 0)
                track.formatTrack(track._cylinder, track._side);
            else
                track.restampFrom(_tracks[0], track._cylinder, track._side);
            track.markClean();
        }
        /// endregion </Allocate objects for new disk>

        _dirty = false;
        result = true;
    }

    return result;
}

std::unique_ptr<DiskImage> DiskImage::Clone(const std::string& filePath) const
{
    auto copy = std::make_unique<DiskImage>(_cylinders, _sides);
    for (size_t i = 0; i < _tracks.size() && i < copy->_tracks.size(); i++)
    {
        Track& to = copy->_tracks[i];
        static_cast<RawTrack&>(to) = static_cast<const RawTrack&>(_tracks[i]);
        to.reindex();
        to.markClean();
    }
    copy->_loaded = _loaded;
    copy->_fortyTrack = _fortyTrack;
    copy->_filePath = filePath;
    copy->_dirty = false;
    return copy;
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
