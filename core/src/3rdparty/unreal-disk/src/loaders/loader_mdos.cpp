#include "unrealdisk/loaders/loader_mdos.h"

#include <cstring>

#include "unrealdisk/detail/helpers.h"

namespace unrealdisk
{

/// region <Static helpers>

bool LoaderMDOS::bootGeometry(const uint8_t* boot, uint8_t& cylinders, uint8_t& sides, uint8_t& sectors,
                              uint8_t maxCylinders)
{
    if (std::memcmp(boot + MARK_OFFSET, "SDOS", 4) != 0)
        return false;
    const uint8_t c = boot[0xB2];
    const uint8_t s = boot[0xB3];
    if (c == 0 || c > maxCylinders || s == 0 || s > 18)
        return false;
    cylinders = c;
    sides = (boot[0xB1] & 0x10) ? 2 : 1;
    sectors = s;
    return true;
}

bool LoaderMDOS::detect(const uint8_t* data, size_t length, uint64_t fileSize)
{
    uint8_t cylinders = 0, sides = 0, sectors = 0;
    return data && length >= SECTOR_SIZE && bootGeometry(data, cylinders, sides, sectors) &&
           fileSize == static_cast<uint64_t>(cylinders) * sides * sectors * SECTOR_SIZE;
}

DiskImage::TrackFormatSpec LoaderMDOS::trackSpec(uint8_t sectors)
{
    const bool hd = sectors > 10;
    DiskImage::TrackFormatSpec spec;
    // The widest gap 3 that fits
    for (uint8_t gap3 = 84; gap3 >= 8; gap3 -= 4)
    {
        spec = DiskImage::TrackFormatSpec::ibm(sectors, DiskImage::SECTOR_SIZE_512, 1, gap3, 0xE5);
        spec.trackLength = DiskImage::RawTrack::NominalTrackSize(
            DiskImage::Encoding::MFM, hd ? FdcDataRate::Rate500Kbps : FdcDataRate::Rate250Kbps);
        if (spec.fits())
            break;
    }
    return spec;
}

bool LoaderMDOS::isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack, std::string* reason)
{
    auto fail = [reason](const std::string& text) {
        if (reason)
            *reason = text;
        return false;
    };
    if (!diskImage)
        return fail("Disk image is not set");
    DiskImage::Track* first = diskImage->getTrackForCylinderAndSide(0, 0);
    const size_t expected = first ? first->sectorCount() : 0;
    if (expected == 0 || expected > 18)
        return fail(StringHelper::Format("Track cylinder 0 side 0 has %zu sectors", expected));
    for (uint8_t cylinder = 0; cylinder < diskImage->getCylinders(); cylinder++)
        for (uint8_t side = 0; side < diskImage->getSides(); side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track || track->sectorCount() != expected)
                return fail(StringHelper::Format("Track cylinder %d side %d differs from the rest of the disk",
                                                 cylinder, side));
            for (uint8_t number = 1; number <= expected; number++)
            {
                DiskImage::Sector* sector = track->findSector(number);
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                    return fail(StringHelper::Format("Track cylinder %d side %d: sector %d is missing or not 512 bytes",
                                                     cylinder, side, number));
            }
        }
    if (sectorsPerTrack)
        *sectorsPerTrack = static_cast<uint8_t>(expected);
    return true;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderMDOS::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    uint8_t cylinders = 0, sides = 0, sectors = 0;
    if (!data || len < SECTOR_SIZE || !bootGeometry(data, cylinders, sides, sectors))
    {
        warnings.push_back("MDOS: no MDOS boot sector (\"SDOS\" at 204 and a geometry)");
        return nullptr;
    }
    const size_t expected = static_cast<size_t>(cylinders) * sides * sectors * SECTOR_SIZE;
    if (len != expected)
    {
        warnings.push_back(
            StringHelper::Format("MDOS: the boot sector gives %d x %d x %d sectors (%zu bytes), the file has %zu",
                                 cylinders, sides, sectors, expected, len));
        return nullptr;
    }
    const DiskImage::TrackFormatSpec spec = trackSpec(sectors);
    if (!spec.fits())
    {
        warnings.push_back(StringHelper::Format("MDOS: %d sectors of 512 bytes do not fit into a track", sectors));
        return nullptr;
    }

    DiskImage* image = new DiskImage(cylinders, sides);
    const uint8_t* src = data;
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
        for (uint8_t side = 0; side < sides; side++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, spec);
            for (uint8_t index = 0; index < sectors; index++, src += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->getSector(index);
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                {
                    warnings.push_back(StringHelper::Format(
                        "MDOS: formatter did not produce sector %d on cylinder %d side %d", index + 1, cylinder, side));
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

bool LoaderMDOS::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();
    uint8_t sectors = 0;
    std::string reason;
    if (!isRegularGeometry(diskImage, &sectors, &reason))
    {
        warnings.push_back("MDOS save refused: " + reason + " (export the disk as UDI to keep it)");
        return false;
    }
    out.resize(static_cast<size_t>(diskImage->getCylinders()) * diskImage->getSides() * sectors * SECTOR_SIZE);
    uint8_t* dst = out.data();
    for (uint8_t cylinder = 0; cylinder < diskImage->getCylinders(); cylinder++)
        for (uint8_t side = 0; side < diskImage->getSides(); side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            for (uint8_t number = 1; number <= sectors; number++, dst += SECTOR_SIZE)
                std::memcpy(dst, track->findSector(number)->data, SECTOR_SIZE);
        }
    return true;
}

/// endregion </Serialization>

/// region <Basic methods>

bool LoaderMDOS::loadImage()
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

bool LoaderMDOS::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderMDOS::writeImage(const std::string& path)
{
    _warnings.clear();
    if (!_diskImage || path.empty())
    {
        _warnings.push_back("MDOS save refused: no image or empty path");
        return false;
    }
    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, buffer, _warnings))
        return false;
    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("MDOS save failed: cannot open " + path);
        return false;
    }
    const bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);
    if (!saved)
    {
        _warnings.push_back("MDOS save failed: cannot write " + path);
        return false;
    }
    _diskImage->markClean();
    _diskImage->setFilePath(path);
    return true;
}

/// endregion </Basic methods>
}  // namespace unrealdisk
