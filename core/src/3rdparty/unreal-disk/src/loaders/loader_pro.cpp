#include "unrealdisk/loaders/loader_pro.h"

#include <cstring>

#include "unrealdisk/detail/helpers.h"

namespace unrealdisk
{

/// region <Static helpers>

uint8_t LoaderPRO::cylindersForSize(size_t fileSize)
{
    if (fileSize % (TRACK_SIZE * SIDES) != 0)
        return 0;
    const size_t cylinders = fileSize / (TRACK_SIZE * SIDES);
    return cylinders >= MIN_CYLINDERS && cylinders <= MAX_CYLINDERS ? static_cast<uint8_t>(cylinders) : 0;
}

DiskImage::TrackFormatSpec LoaderPRO::trackSpec(bool systemTrack)
{
    DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::ibm(SECTORS, DiskImage::SECTOR_SIZE_1024, 1, GAP3, 0xE5);
    if (systemTrack)
        spec.sectorNumbers.back() = SYSTEM_SECTOR;
    spec.trackLength = DiskImage::RawTrack::NominalTrackSize(DiskImage::Encoding::MFM, FdcDataRate::Rate250Kbps);
    spec.gapIndex = 80;
    spec.gapPostIndex = 50;
    spec.gapPostID = 22;
    return spec;
}

DiskImage::Sector* LoaderPRO::findSector(DiskImage::Track* track, uint8_t cylinder, uint8_t side, uint8_t number)
{
    DiskImage::Sector* sector = track->findSector(number);
    if (!sector && cylinder == 0 && side == 0 && number == SECTORS)
        sector = track->findSector(SYSTEM_SECTOR);
    return sector;
}

bool LoaderPRO::isRegularGeometry(DiskImage* diskImage, std::string* reason)
{
    auto fail = [reason](const std::string& text)
    {
        if (reason) *reason = text;
        return false;
    };

    if (!diskImage)
        return fail("Disk image is not set");
    if (diskImage->getSides() != SIDES)
        return fail(StringHelper::Format("Image has %d side(s), a .pro image requires 2", diskImage->getSides()));
    const uint8_t cylinders = diskImage->getCylinders();
    if (cylinders < MIN_CYLINDERS || cylinders > MAX_CYLINDERS)
        return fail(StringHelper::Format("Image has %d cylinders, a .pro image takes 80 to 83", cylinders));

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
                return fail(StringHelper::Format("Track cylinder %d side %d is missing", cylinder, side));
            if (track->sectorCount() != SECTORS)
                return fail(StringHelper::Format("Track cylinder %d side %d has %zu sectors, a .pro image requires 5",
                                                 cylinder, side, track->sectorCount()));
            for (uint8_t number = 1; number <= SECTORS; number++)
            {
                DiskImage::Sector* sector = findSector(track, cylinder, side, number);
                if (!sector || !sector->hasData)
                    return fail(StringHelper::Format("Track cylinder %d side %d: sector %d is missing or has no data field",
                                                     cylinder, side, number));
                if (sector->dataSize != SECTOR_SIZE)
                    return fail(StringHelper::Format("Track cylinder %d side %d: sector %d is %d bytes, a .pro image requires 1024",
                                                     cylinder, side, number, sector->dataSize));
            }
        }
    }
    return true;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderPRO::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    if (!data)
    {
        warnings.push_back("PRO: no data");
        return nullptr;
    }
    const uint8_t cylinders = cylindersForSize(len);
    if (cylinders == 0)
    {
        warnings.push_back(StringHelper::Format("PRO: file size %zu is not 80 to 83 cylinders of 2 x 5 x 1024 bytes", len));
        return nullptr;
    }
    const DiskImage::TrackFormatSpec spec = trackSpec();
    const DiskImage::TrackFormatSpec system = trackSpec(true);
    if (!spec.fits() || !system.fits())
    {
        warnings.push_back("PRO: the track layout does not fit into a track");
        return nullptr;
    }

    DiskImage* image = new DiskImage(cylinders, SIDES);
    const uint8_t* src = data;
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)  // c0/h0, c0/h1, c1/h0, ...
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, cylinder == 0 && side == 0 ? system : spec);
            for (uint8_t index = 0; index < SECTORS; index++, src += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = findSector(track, cylinder, side, static_cast<uint8_t>(index + 1));
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                {
                    warnings.push_back(StringHelper::Format("PRO: formatter did not produce sector %d on cylinder %d side %d",
                                                            index + 1, cylinder, side));
                    delete image;
                    return nullptr;
                }
                std::memcpy(sector->data, src, SECTOR_SIZE);
                sector->recalculateDataCRC();
            }
            track->markClean();
        }
    }
    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialization>

bool LoaderPRO::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();
    std::string reason;
    if (!isRegularGeometry(diskImage, &reason))
    {
        warnings.push_back("PRO save refused: " + reason + " (export the disk as UDI to keep it)");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    out.resize(static_cast<size_t>(cylinders) * SIDES * TRACK_SIZE);
    uint8_t* dst = out.data();
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            // By sector number, not physical position: a guest re-format may have changed the interleave. Verified by
            // isRegularGeometry
            for (uint8_t number = 1; number <= SECTORS; number++, dst += SECTOR_SIZE)
                std::memcpy(dst, findSector(track, cylinder, side, number)->data, SECTOR_SIZE);
        }
    }
    return true;
}

/// endregion </Serialization>

/// region <Basic methods>

bool LoaderPRO::loadImage()
{
    _warnings.clear();

    if (!FileHelper::FileExists(_filepath))
    {
        _warnings.push_back("File not found: " + _filepath);
        return false;
    }
    const size_t fileSize = FileHelper::GetFileSize(_filepath);
    if (cylindersForSize(fileSize) == 0)
    {
        _warnings.push_back(StringHelper::Format("PRO: %s is %zu bytes, expected 80 to 83 cylinders of 10240", _filepath.c_str(), fileSize));
        return false;
    }
    std::vector<uint8_t> buffer(fileSize);
    if (FileHelper::ReadFileToBuffer(_filepath, buffer.data(), fileSize) != fileSize)
    {
        _warnings.push_back("Unable to read: " + _filepath);
        return false;
    }
    DiskImage* image = parse(buffer.data(), buffer.size(), _warnings);
    if (!image)
        return false;

    image->setFilePath(_filepath);
    image->setLoaded(true);
    _diskImage = image;
    return true;
}

bool LoaderPRO::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderPRO::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("PRO save refused: no image or empty path");
        return false;
    }
    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, buffer, _warnings))
        return false;

    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("PRO save failed: cannot open " + path);
        return false;
    }
    const bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);
    if (!saved)
    {
        _warnings.push_back("PRO save failed: cannot write " + path);
        return false;
    }
    _diskImage->setFilePath(path);
    _diskImage->markClean();
    return true;
}

/// endregion </Basic methods>

}  // namespace unrealdisk
