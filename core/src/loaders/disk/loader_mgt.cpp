#include "loader_mgt.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

namespace
{
    /// Lower-case extension without the leading dot, from either a bare extension or a full path
    std::string normalizeExtension(const std::string& extensionOrPath)
    {
        size_t lastSep = extensionOrPath.find_last_of("/\\");
        std::string name = (lastSep == std::string::npos) ? extensionOrPath : extensionOrPath.substr(lastSep + 1);

        size_t dot = name.find_last_of('.');
        std::string ext = (dot == std::string::npos) ? name : name.substr(dot + 1);

        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }
}

/// region <Static helpers>

uint8_t LoaderMGT::cylindersForSize(size_t fileSize)
{
    if (fileSize == IMAGE_SIZE_80) return 80;
    if (fileSize == IMAGE_SIZE_40) return 40;
    return 0;
}

bool LoaderMGT::detect(size_t fileSize, const std::string& extension)
{
    if (cylindersForSize(fileSize) == 0) return false;

    const std::string ext = normalizeExtension(extension);
    return ext == "mgt" || ext == "img";
}

LoaderMGT::TrackOrder LoaderMGT::orderForExtension(const std::string& extensionOrPath)
{
    return normalizeExtension(extensionOrPath) == "img" ? TrackOrder::SideMajor : TrackOrder::CylinderMajor;
}

bool LoaderMGT::isPlusDGeometry(DiskImage* diskImage, std::string* reason)
{
    if (!diskImage)
    {
        if (reason) *reason = "Disk image is not set";
        return false;
    }

    if (diskImage->getSides() != SIDES)
    {
        if (reason) *reason = StringHelper::Format("Image has %d side(s), MGT requires exactly 2", diskImage->getSides());
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    if (cylinders != 40 && cylinders != 80)
    {
        if (reason) *reason = StringHelper::Format("Image has %d cylinders, MGT requires 40 or 80", cylinders);
        return false;
    }

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
            {
                if (reason) *reason = StringHelper::Format("Track cylinder %d side %d is missing", cylinder, side);
                return false;
            }

            if (track->sectorCount() != SECTORS_PER_TRACK)
            {
                if (reason)
                    *reason = StringHelper::Format("Track cylinder %d side %d has %zu sectors, MGT requires exactly %zu",
                                                   cylinder, side, track->sectorCount(), SECTORS_PER_TRACK);
                return false;
            }

            for (uint8_t number = 1; number <= SECTORS_PER_TRACK; number++)
            {
                DiskImage::Sector* sector = track->findSector(number);
                if (!sector || !sector->hasData)
                {
                    if (reason)
                        *reason = StringHelper::Format("Track cylinder %d side %d: sector %d is missing or has no data field",
                                                       cylinder, side, number);
                    return false;
                }

                if (sector->dataSize != SECTOR_SIZE)
                {
                    if (reason)
                        *reason = StringHelper::Format("Track cylinder %d side %d: sector %d is %d bytes, MGT requires %zu",
                                                       cylinder, side, number, sector->dataSize, SECTOR_SIZE);
                    return false;
                }
            }
        }
    }

    return true;
}

size_t LoaderMGT::trackOffset(uint8_t cylinders, uint8_t cylinder, uint8_t side, TrackOrder order)
{
    size_t trackIndex;
    if (order == TrackOrder::SideMajor)
    {
        trackIndex = static_cast<size_t>(side) * cylinders + cylinder;          // .img: all side 0, then all side 1
    }
    else
    {
        trackIndex = static_cast<size_t>(cylinder) * SIDES + side;              // .mgt: c0/s0, c0/s1, c1/s0, ...
    }

    return trackIndex * TRACK_SIZE;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderMGT::parse(const uint8_t* data, size_t len, TrackOrder order, std::vector<std::string>& warnings)
{
    if (!data)
    {
        warnings.push_back("MGT: no data");
        return nullptr;
    }

    const uint8_t cylinders = cylindersForSize(len);
    if (cylinders == 0)
    {
        warnings.push_back(StringHelper::Format("MGT: file size %zu is neither 819200 (80 cylinders) nor 409600 (40 cylinders)", len));
        return nullptr;
    }

    if (order == TrackOrder::Auto) order = TrackOrder::CylinderMajor;

    const DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::plusD();
    if (!spec.fits())
    {
        warnings.push_back("MGT: the +D track layout does not fit into a track");
        return nullptr;
    }

    DiskImage* image = new DiskImage(cylinders, SIDES);

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, spec);

            const uint8_t* src = data + trackOffset(cylinders, cylinder, side, order);
            for (uint8_t number = 1; number <= SECTORS_PER_TRACK; number++, src += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->getSector(static_cast<uint8_t>(number - 1));
                if (!sector || !sector->hasData || sector->dataSize != SECTOR_SIZE)
                {
                    // Cannot happen with plusD(): the formatter always produces sectors 1..10 x 512
                    warnings.push_back(StringHelper::Format("MGT: formatter did not produce sector %d on cylinder %d side %d", number, cylinder, side));
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

/// region <Serialisation>

bool LoaderMGT::serialize(DiskImage* diskImage, TrackOrder order, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    std::string reason;
    if (!isPlusDGeometry(diskImage, &reason))
    {
        warnings.push_back("MGT save refused: " + reason);
        return false;
    }

    if (order == TrackOrder::Auto) order = TrackOrder::CylinderMajor;

    const uint8_t cylinders = diskImage->getCylinders();
    out.resize(static_cast<size_t>(cylinders) * SIDES * TRACK_SIZE);

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < SIDES; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            uint8_t* dst = out.data() + trackOffset(cylinders, cylinder, side, order);

            for (uint8_t number = 1; number <= SECTORS_PER_TRACK; number++, dst += SECTOR_SIZE)
            {
                DiskImage::Sector* sector = track->getSector(static_cast<uint8_t>(number - 1));  // Verified by isPlusDGeometry
                std::memcpy(dst, sector->data, SECTOR_SIZE);
            }
        }
    }

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

LoaderMGT::TrackOrder LoaderMGT::effectiveOrder(const std::string& path) const
{
    return (_trackOrder == TrackOrder::Auto) ? orderForExtension(path) : _trackOrder;
}

bool LoaderMGT::loadImage()
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
        _warnings.push_back(StringHelper::Format("MGT: %s is %zu bytes, expected 819200 or 409600", _filepath.c_str(), fileSize));
        return false;
    }

    std::vector<uint8_t> buffer(fileSize);
    if (FileHelper::ReadFileToBuffer(_filepath, buffer.data(), fileSize) != fileSize)
    {
        _warnings.push_back("Unable to read: " + _filepath);
        return false;
    }

    DiskImage* image = parse(buffer.data(), buffer.size(), effectiveOrder(_filepath), _warnings);
    if (!image)
    {
        return false;
    }

    image->setFilePath(_filepath);
    image->setLoaded(true);
    _diskImage = image;
    return true;
}

bool LoaderMGT::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderMGT::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("MGT save refused: no image or empty path");
        return false;
    }

    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, effectiveOrder(path), buffer, _warnings))
    {
        return false;
    }

    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("MGT save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("MGT save failed: cannot write " + path);
        return false;
    }

    // Mark disk as clean after successful save
    _diskImage->markClean();

    // Emit notification that disk was saved
    if (_context && _context->pEmulator)
    {
        std::string emulatorId = _context->pEmulator->GetId();
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_FDD_DISK_WRITTEN, new FDDDiskPayload(emulatorId, 0, path), true);
    }

    _diskImage->setFilePath(path);
    return true;
}

/// endregion </Basic methods>
