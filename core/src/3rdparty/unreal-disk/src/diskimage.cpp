#include "unrealdisk/diskimage.h"

namespace unrealdisk
{

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
        _tracks.reserve(static_cast<size_t>(MAX_CYLINDERS) * sides);  // growCylinders() never moves the tracks
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
    RawCopy raw = CopyRaw();
    raw.dirty = DirtyState{};  // the copy is clean
    std::unique_ptr<DiskImage> copy = FromRaw(std::move(raw));
    copy->_filePath = filePath;
    return copy;
}

DiskImage::RawCopy DiskImage::CopyRaw() const
{
    LayoutLock lock(this);
    RawCopy copy;
    CopyRawHeader(copy);
    copy.tracks.reserve(_tracks.size());
    for (size_t i = 0; CopyRawTrack(i, copy); i++)
    {
    }
    return copy;
}

void DiskImage::CopyRawHeader(RawCopy& copy) const
{
    copy.cylinders = _cylinders;
    copy.sides = _sides;
    copy.loaded = _loaded;
    copy.fortyTrack = _fortyTrack;
    copy.tracks.clear();
    copy.dirty = DirtyState{};
    copy.dirty.image = _dirty;
    copy.dirty.filePath = _filePath;
}

bool DiskImage::CopyRawTrack(size_t index, RawCopy& copy) const
{
    if (index >= _tracks.size())
        return false;
    const Track& track = _tracks[index];
    copy.tracks.push_back(static_cast<const RawTrack&>(track));
    copy.dirty.tracks.emplace_back(track._dirty, track._rawTrackDirty);
    std::vector<bool> sectors;
    sectors.reserve(track._sectors.size());
    for (const Sector& sector : track._sectors)
        sectors.push_back(sector.dirty);
    copy.dirty.sectors.push_back(std::move(sectors));
    // Cylinders added since the header (WRITE TRACK past the end): the copy covers what it took
    copy.cylinders = std::max<uint8_t>(copy.cylinders, static_cast<uint8_t>((index / std::max<uint8_t>(_sides, 1)) + 1));
    return true;
}

std::unique_ptr<DiskImage> DiskImage::FromRaw(RawCopy&& raw)
{
    auto copy = std::make_unique<DiskImage>(raw.cylinders, raw.sides);
    for (size_t i = 0; i < raw.tracks.size() && i < copy->_tracks.size(); i++)
    {
        Track& to = copy->_tracks[i];
        static_cast<RawTrack&>(to) = std::move(raw.tracks[i]);
        to.reindex();
        to.markClean();
    }
    copy->_loaded = raw.loaded;
    copy->_fortyTrack = raw.fortyTrack;
    copy->_dirty = false;
    copy->restoreDirtyState(raw.dirty);  // same streams, same sector index: the flags line up
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
}  // namespace unrealdisk
