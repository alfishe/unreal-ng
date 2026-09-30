#include "loader_rawpc.h"

#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"

/// region <Static helpers>

bool LoaderRawPcFloppy::detect(size_t fileSize)
{
    return sectorsForSize(fileSize) != 0;
}

uint8_t LoaderRawPcFloppy::sectorsForSize(size_t fileSize)
{
    if (fileSize == IMAGE_SIZE_DD) return SECTORS_DD;
    if (fileSize == IMAGE_SIZE_HD) return SECTORS_HD;
    return 0;
}

DiskImage::TrackFormatSpec LoaderRawPcFloppy::trackSpec(uint8_t sectorsPerTrack)
{
    const bool hd = sectorsPerTrack == SECTORS_HD;

    // IBM System 34 layout: GAP4a 80, SYNC 12, IAM, GAP1 50, sectors with GAP2 22 and GAP3 84 (DD) / 108 (HD);
    // the rest of the revolution is GAP4b. The track length sets the recorded data rate
    DiskImage::TrackFormatSpec spec =
        DiskImage::TrackFormatSpec::ibm(sectorsPerTrack, DiskImage::SECTOR_SIZE_512, 1, hd ? GAP3_HD : GAP3_DD, 0xF6);
    spec.trackLength = DiskImage::RawTrack::NominalTrackSize(DiskImage::Encoding::MFM,
                                                             hd ? FdcDataRate::Rate500Kbps : FdcDataRate::Rate250Kbps);
    spec.gapIndex = 80;
    spec.gapPostIndex = 50;
    spec.gapPostID = 22;
    return spec;
}

bool LoaderRawPcFloppy::isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack, std::string* reason)
{
    auto fail = [reason](const std::string& text)
    {
        if (reason) *reason = text;
        return false;
    };

    if (!diskImage)
        return fail("Disk image is not set");
    if (diskImage->getSides() != SIDES)
        return fail(StringHelper::Format("Image has %d side(s), a raw PC floppy requires exactly 2", diskImage->getSides()));
    if (diskImage->getCylinders() != CYLINDERS)
        return fail(StringHelper::Format("Image has %d cylinders, a raw PC floppy requires 80", diskImage->getCylinders()));

    DiskImage::Track* first = diskImage->getTrackForCylinderAndSide(0, 0);
    const size_t expected = first ? first->sectorCount() : 0;
    if (expected != SECTORS_DD && expected != SECTORS_HD)
        return fail(StringHelper::Format("Track cylinder 0 side 0 has %zu sectors, a raw PC floppy requires 9 (720 KB) or 18 (1.44 MB)",
                                         expected));

    for (uint8_t cylinder = 0; cylinder < CYLINDERS; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
                return fail(StringHelper::Format("Track cylinder %d side %d is missing", cylinder, side));
            if (track->sectorCount() != expected)
                return fail(StringHelper::Format("Track cylinder %d side %d has %zu sectors, the rest of the disk has %zu",
                                                 cylinder, side, track->sectorCount(), expected));

            for (uint8_t number = 1; number <= expected; number++)
            {
                DiskImage::Sector* sector = track->findSector(number);
                if (!sector || !sector->hasData)
                    return fail(StringHelper::Format("Track cylinder %d side %d: sector %d is missing or has no data field",
                                                     cylinder, side, number));
                if (sector->dataSize != SECTOR_SIZE)
                    return fail(StringHelper::Format("Track cylinder %d side %d: sector %d is %d bytes, a raw PC floppy requires 512",
                                                     cylinder, side, number, sector->dataSize));
            }
        }
    }

    if (sectorsPerTrack) *sectorsPerTrack = static_cast<uint8_t>(expected);
    return true;
}

size_t LoaderRawPcFloppy::trackOffset(uint8_t sectorsPerTrack, uint8_t cylinder, uint8_t side)
{
    const size_t trackIndex = static_cast<size_t>(cylinder) * SIDES + side;  // c0/h0, c0/h1, c1/h0, ...
    return trackIndex * sectorsPerTrack * SECTOR_SIZE;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderRawPcFloppy::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    if (!data)
    {
        warnings.push_back("Raw PC floppy: no data");
        return nullptr;
    }

    const uint8_t sectors = sectorsForSize(len);
    if (sectors == 0)
    {
        warnings.push_back(StringHelper::Format("Raw PC floppy: file size %zu is neither 737280 (720 KB) nor 1474560 (1.44 MB)", len));
        return nullptr;
    }

    const DiskImage::TrackFormatSpec spec = trackSpec(sectors);
    if (!spec.fits())
    {
        warnings.push_back("Raw PC floppy: the PC track layout does not fit into a track");
        return nullptr;
    }

    DiskImage* image = new DiskImage(CYLINDERS, SIDES);

    for (uint8_t cylinder = 0; cylinder < CYLINDERS; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, spec);

            const uint8_t* src = data + trackOffset(sectors, cylinder, side);
            for (uint8_t index = 0; index < sectors; index++, src += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->getSector(index);  // Physical order is 1..N
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                {
                    warnings.push_back(StringHelper::Format("Raw PC floppy: formatter did not produce sector %d on cylinder %d side %d",
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

bool LoaderRawPcFloppy::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    uint8_t sectors = 0;
    std::string reason;
    if (!isRegularGeometry(diskImage, &sectors, &reason))
    {
        warnings.push_back("Raw PC floppy save refused: " + reason + " (export the disk as UDI to keep it)");
        return false;
    }

    out.resize(static_cast<size_t>(CYLINDERS) * SIDES * sectors * SECTOR_SIZE);

    for (uint8_t cylinder = 0; cylinder < CYLINDERS; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            uint8_t* dst = out.data() + trackOffset(sectors, cylinder, side);

            // By sector number, not physical position: a guest re-format may have changed the interleave
            for (uint8_t number = 1; number <= sectors; number++, dst += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->findSector(number);  // Verified by isRegularGeometry
                std::memcpy(dst, sector->data, SECTOR_SIZE);
            }
        }
    }

    return true;
}

/// endregion </Serialization>

/// region <Basic methods>

bool LoaderRawPcFloppy::loadImage()
{
    _warnings.clear();

    if (!FileHelper::FileExists(_filepath))
    {
        _warnings.push_back("File not found: " + _filepath);
        return false;
    }

    const size_t fileSize = FileHelper::GetFileSize(_filepath);
    if (sectorsForSize(fileSize) == 0)
    {
        _warnings.push_back(StringHelper::Format("Raw PC floppy: %s is %zu bytes, expected 737280 or 1474560", _filepath.c_str(), fileSize));
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

bool LoaderRawPcFloppy::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderRawPcFloppy::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("Raw PC floppy save refused: no image or empty path");
        return false;
    }

    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, buffer, _warnings))
        return false;

    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("Raw PC floppy save failed: cannot open " + path);
        return false;
    }

    const bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);
    if (!saved)
    {
        _warnings.push_back("Raw PC floppy save failed: cannot write " + path);
        return false;
    }

    _diskImage->markClean();
    _diskImage->setFilePath(path);
    return true;
}

/// endregion </Basic methods>
