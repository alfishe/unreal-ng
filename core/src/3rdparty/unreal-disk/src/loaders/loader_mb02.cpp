#include "unrealdisk/loaders/loader_mb02.h"

#include <cstring>

#include "unrealdisk/detail/helpers.h"

namespace unrealdisk
{

/// region <Static helpers>

bool LoaderMB02::bootGeometry(const uint8_t* boot, uint8_t& tracks, uint8_t& sectors)
{
    if (boot[0x00] != 0x18 || boot[0x03] != 0x02 || boot[0x20] != 0x00 || boot[0x25] != 0x00)
        return false;
    const uint16_t t = boot[0x04] | boot[0x05] << 8;
    const uint16_t s = boot[0x06] | boot[0x07] << 8;
    const uint16_t sides = boot[0x08] | boot[0x09] << 8;
    const uint16_t cluster = boot[0x0A] | boot[0x0B] << 8;
    if (sides != SIDES || cluster != 1 || t == 0 || t > MAX_CYLINDERS || s == 0 || s > 22)
        return false;
    tracks = static_cast<uint8_t>(t);
    sectors = static_cast<uint8_t>(s);
    return true;
}

bool LoaderMB02::detect(const uint8_t* data, size_t length, uint64_t fileSize)
{
    uint8_t tracks = 0, sectors = 0;
    return data && length >= SECTOR_SIZE && bootGeometry(data, tracks, sectors) &&
           fileSize == static_cast<uint64_t>(tracks) * SIDES * sectors * SECTOR_SIZE;
}

DiskImage::TrackFormatSpec LoaderMB02::trackSpec(uint8_t sectors)
{
    const bool hd = sectors > 5;
    DiskImage::TrackFormatSpec spec;
    // The widest gap 3 that fits (an HD track of 11 sectors leaves little room)
    for (uint8_t gap3 = 40; gap3 >= 8; gap3 -= 4)
    {
        spec = DiskImage::TrackFormatSpec::ibm(sectors, DiskImage::SECTOR_SIZE_1024, 1, gap3, 0xE5);
        spec.trackLength = DiskImage::RawTrack::NominalTrackSize(
            DiskImage::Encoding::MFM, hd ? FdcDataRate::Rate500Kbps : FdcDataRate::Rate250Kbps);
        if (spec.fits())
            break;
    }
    return spec;
}

bool LoaderMB02::isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack, std::string* reason)
{
    auto fail = [reason](const std::string& text) {
        if (reason)
            *reason = text;
        return false;
    };
    if (!diskImage)
        return fail("Disk image is not set");
    if (diskImage->getSides() != SIDES)
        return fail(StringHelper::Format("Image has %d side(s), an MB-02 image has 2", diskImage->getSides()));
    DiskImage::Track* first = diskImage->getTrackForCylinderAndSide(0, 0);
    const size_t expected = first ? first->sectorCount() : 0;
    if (expected == 0 || expected > 22)
        return fail(StringHelper::Format("Track cylinder 0 side 0 has %zu sectors", expected));
    for (uint8_t cylinder = 0; cylinder < diskImage->getCylinders(); cylinder++)
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track || track->sectorCount() != expected)
                return fail(StringHelper::Format("Track cylinder %d side %d differs from the rest of the disk",
                                                 cylinder, side));
            for (uint8_t number = 1; number <= expected; number++)
            {
                DiskImage::Sector* sector = track->findSector(number);
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                    return fail(StringHelper::Format(
                        "Track cylinder %d side %d: sector %d is missing or not 1024 bytes", cylinder, side, number));
            }
        }
    if (sectorsPerTrack)
        *sectorsPerTrack = static_cast<uint8_t>(expected);
    return true;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderMB02::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    uint8_t tracks = 0, sectors = 0;
    if (!data || len < SECTOR_SIZE || !bootGeometry(data, tracks, sectors))
    {
        warnings.push_back("MB-02: no MB-02 boot sector");
        return nullptr;
    }
    const size_t expected = static_cast<size_t>(tracks) * SIDES * sectors * SECTOR_SIZE;
    if (len != expected)
    {
        warnings.push_back(
            StringHelper::Format("MB-02: the boot sector gives %d x 2 x %d sectors (%zu bytes), the file has %zu",
                                 tracks, sectors, expected, len));
        return nullptr;
    }
    const DiskImage::TrackFormatSpec spec = trackSpec(sectors);
    if (!spec.fits())
    {
        warnings.push_back(StringHelper::Format("MB-02: %d sectors of 1024 bytes do not fit into a track", sectors));
        return nullptr;
    }

    DiskImage* image = new DiskImage(tracks, SIDES);
    const uint8_t* src = data;
    for (uint8_t cylinder = 0; cylinder < tracks; cylinder++)
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, spec);
            for (uint8_t index = 0; index < sectors; index++, src += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->getSector(index);
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                {
                    warnings.push_back(
                        StringHelper::Format("MB-02: formatter did not produce sector %d on cylinder %d side %d",
                                             index + 1, cylinder, side));
                    delete image;
                    return nullptr;
                }
                std::memcpy(sector->data, src, SECTOR_SIZE);
                sector->recalculateDataCRC();
            }
            track->markClean();
        }
    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialization>

bool LoaderMB02::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();
    uint8_t sectors = 0;
    std::string reason;
    if (!isRegularGeometry(diskImage, &sectors, &reason))
    {
        warnings.push_back("MB-02 save refused: " + reason + " (export the disk as UDI to keep it)");
        return false;
    }
    out.resize(static_cast<size_t>(diskImage->getCylinders()) * SIDES * sectors * SECTOR_SIZE);
    uint8_t* dst = out.data();
    for (uint8_t cylinder = 0; cylinder < diskImage->getCylinders(); cylinder++)
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            for (uint8_t number = 1; number <= sectors; number++, dst += SECTOR_SIZE)
                std::memcpy(dst, track->findSector(number)->data, SECTOR_SIZE);
        }
    return true;
}

/// endregion </Serialization>

/// region <Basic methods>

bool LoaderMB02::loadImage()
{
    _warnings.clear();
    if (!FileHelper::FileExists(_filepath))
    {
        _warnings.push_back("File not found: " + _filepath);
        return false;
    }
    const size_t fileSize = FileHelper::GetFileSize(_filepath);
    std::vector<uint8_t> buffer(fileSize);
    if (fileSize == 0 || FileHelper::ReadFileToBuffer(_filepath, buffer.data(), fileSize) != fileSize)
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

bool LoaderMB02::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderMB02::writeImage(const std::string& path)
{
    _warnings.clear();
    if (!_diskImage || path.empty())
    {
        _warnings.push_back("MB-02 save refused: no image or empty path");
        return false;
    }
    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, buffer, _warnings))
        return false;
    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("MB-02 save failed: cannot open " + path);
        return false;
    }
    const bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);
    if (!saved)
    {
        _warnings.push_back("MB-02 save failed: cannot write " + path);
        return false;
    }
    _diskImage->markClean();
    _diskImage->setFilePath(path);
    return true;
}

/// endregion </Basic methods>
}  // namespace unrealdisk
