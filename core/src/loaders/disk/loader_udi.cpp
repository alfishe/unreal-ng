#include "loader_udi.h"

#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

namespace
{
    inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    inline uint32_t readU32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }
    inline void putU16(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    inline void putU32(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

/// region <Static helpers>

bool LoaderUDI::detect(const uint8_t* data, size_t len, bool* compressed)
{
    if (compressed) *compressed = false;
    if (!data || len < 4) return false;

    if (std::memcmp(data, SIGNATURE, 4) == 0) return true;

    if (std::memcmp(data, SIGNATURE_COMPRESSED, 4) == 0)
    {
        if (compressed) *compressed = true;
        return true;
    }

    return false;
}

uint32_t LoaderUDI::computeCrc(const uint8_t* data, size_t len)
{
    int crc = -1;
    CRCHelper::crcUDI(crc, data, len);
    return static_cast<uint32_t>(crc);
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderUDI::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    bool compressed = false;
    if (!detect(data, len, &compressed))
    {
        warnings.push_back("Not a UDI file: signature 'UDI!' missing");
        return nullptr;
    }
    if (compressed)
    {
        warnings.push_back("Compressed UDI ('udi!') is not supported");
        return nullptr;
    }
    if (len < HEADER_SIZE + 4)
    {
        warnings.push_back("UDI file is truncated (shorter than header + CRC)");
        return nullptr;
    }

    /// region <Header>
    const uint32_t declaredSize = readU32(data + 4);
    const uint8_t version = data[8];
    const uint8_t maxCylinder = data[9];
    const uint8_t maxHead = data[10];
    const uint32_t extendedHeaderSize = readU32(data + 12);

    if (version != VERSION)
    {
        warnings.push_back(StringHelper::Format("UDI version %d is not supported (expected 0)", version));
        return nullptr;
    }
    if (maxHead > 1)
    {
        warnings.push_back(StringHelper::Format("UDI declares %d heads, only 1 or 2 are supported", maxHead + 1));
        return nullptr;
    }
    if (maxCylinder + 1 > MAX_CYLINDERS)
    {
        warnings.push_back(StringHelper::Format("UDI declares %d cylinders, the emulator supports at most %d", maxCylinder + 1, MAX_CYLINDERS));
        return nullptr;
    }
    if (declaredSize + 4 != len)
    {
        warnings.push_back(StringHelper::Format("UDI size field (%u) does not match the file size minus CRC (%zu)", declaredSize, len - 4));
        // Continue: the CRC check below decides whether the file is usable
    }
    if (HEADER_SIZE + static_cast<size_t>(extendedHeaderSize) > len - 4)
    {
        warnings.push_back("UDI extended header runs past the end of the file");
        return nullptr;
    }
    /// endregion </Header>

    /// region <CRC>
    const uint32_t storedCrc = readU32(data + len - 4);
    const uint32_t actualCrc = computeCrc(data, len - 4);
    if (storedCrc != actualCrc)
    {
        std::string message = StringHelper::Format("UDI CRC mismatch: stored 0x%08X, computed 0x%08X", storedCrc, actualCrc);
        if (!_ignoreCrc)
        {
            warnings.push_back(message + " (load refused; use setIgnoreCrc(true) to force)");
            return nullptr;
        }
        warnings.push_back(message + " (ignored)");
    }
    /// endregion </CRC>

    _extendedHeader.assign(data + HEADER_SIZE, data + HEADER_SIZE + extendedHeaderSize);
    _reserved = data[11];

    const uint8_t cylinders = static_cast<uint8_t>(maxCylinder + 1);
    const uint8_t sides = static_cast<uint8_t>(maxHead + 1);
    DiskImage* image = new DiskImage(cylinders, sides);

    /// region <Tracks>
    size_t offset = HEADER_SIZE + extendedHeaderSize;
    const size_t end = len - 4;

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            if (offset + 3 > end)
            {
                warnings.push_back(StringHelper::Format("UDI truncated at track cylinder %d side %d", cylinder, side));
                delete image;
                return nullptr;
            }

            const uint8_t type = data[offset];
            const uint16_t trackLength = readU16(data + offset + 1);
            offset += 3;

            if (type & TRACK_TYPE_MULTIREV_FLAG)
            {
                warnings.push_back(StringHelper::Format("UDI track cylinder %d side %d uses the multi-revolution extension, not supported", cylinder, side));
                delete image;
                return nullptr;
            }
            if (type != TRACK_TYPE_MFM && type != TRACK_TYPE_FM && type != TRACK_TYPE_MIXED)
            {
                warnings.push_back(StringHelper::Format("UDI track cylinder %d side %d has unknown type %d", cylinder, side, type));
                delete image;
                return nullptr;
            }

            const size_t bitmapSize = (static_cast<size_t>(trackLength) + 7) / 8;
            const size_t trackBytes = static_cast<size_t>(trackLength) + bitmapSize + (type == TRACK_TYPE_MIXED ? bitmapSize : 0);
            if (offset + trackBytes > end)
            {
                warnings.push_back(StringHelper::Format("UDI track data truncated at cylinder %d side %d", cylinder, side));
                delete image;
                return nullptr;
            }

            if (trackLength > DiskImage::RawTrack::MAX_TRACK_SIZE)
            {
                warnings.push_back(StringHelper::Format("UDI track cylinder %d side %d is %d bytes long, above the supported maximum %zu",
                                                        cylinder, side, trackLength, DiskImage::RawTrack::MAX_TRACK_SIZE));
                delete image;
                return nullptr;
            }
            if (trackLength < DiskImage::RawTrack::MIN_TRACK_SIZE)
            {
                warnings.push_back(StringHelper::Format("UDI track cylinder %d side %d is only %d bytes long, padded to %zu",
                                                        cylinder, side, trackLength, DiskImage::RawTrack::MIN_TRACK_SIZE));
            }

            DiskImage::Encoding encoding = (type == TRACK_TYPE_FM) ? DiskImage::Encoding::FM : DiskImage::Encoding::MFM;
            if (type == TRACK_TYPE_MIXED)
            {
                warnings.push_back(StringHelper::Format("UDI track cylinder %d side %d is a mixed FM/MFM track, loaded as MFM", cylinder, side));
            }

            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
            track->setRaw(data + offset, trackLength, encoding);
            track->setClockBitmap(data + offset + trackLength, bitmapSize);
            track->markClean();

            offset += trackBytes;
        }
    }
    /// endregion </Tracks>

    // Anything between the last track and the CRC is kept verbatim (TRX2X writes an ASCIIZ comment there)
    _trailer.assign(data + offset, data + end);

    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialisation>

bool LoaderUDI::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("UDI save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();
    if (cylinders == 0 || sides == 0 || sides > 2)
    {
        warnings.push_back("UDI save refused: invalid disk geometry");
        return false;
    }

    // Header
    out.insert(out.end(), SIGNATURE, SIGNATURE + 4);
    putU32(out, 0);  // Size, patched below
    out.push_back(VERSION);
    out.push_back(static_cast<uint8_t>(cylinders - 1));
    out.push_back(static_cast<uint8_t>(sides - 1));
    out.push_back(_reserved);
    putU32(out, static_cast<uint32_t>(_extendedHeader.size()));
    out.insert(out.end(), _extendedHeader.begin(), _extendedHeader.end());

    // Tracks
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("UDI save refused: track cylinder %d side %d is missing", cylinder, side));
                return false;
            }

            const size_t length = track->rawSize();
            if (length > 0xFFFF)
            {
                warnings.push_back(StringHelper::Format("UDI save refused: track cylinder %d side %d is longer than 65535 bytes", cylinder, side));
                return false;
            }

            out.push_back(track->encoding() == DiskImage::Encoding::FM ? TRACK_TYPE_FM : TRACK_TYPE_MFM);
            putU16(out, static_cast<uint16_t>(length));
            out.insert(out.end(), track->rawData(), track->rawData() + length);

            const std::vector<uint8_t>& clock = track->clockBitmap();
            const size_t bitmapSize = (length + 7) / 8;
            out.insert(out.end(), clock.begin(), clock.begin() + std::min(bitmapSize, clock.size()));
            if (clock.size() < bitmapSize)
            {
                out.insert(out.end(), bitmapSize - clock.size(), 0);
            }

            if (track->hasWeakBits())
            {
                warnings.push_back(StringHelper::Format("UDI cannot store weak bits: track cylinder %d side %d saved without them", cylinder, side));
            }
        }
    }

    out.insert(out.end(), _trailer.begin(), _trailer.end());

    // Size field = everything before the CRC
    const uint32_t size = static_cast<uint32_t>(out.size());
    out[4] = static_cast<uint8_t>(size & 0xFF);
    out[5] = static_cast<uint8_t>((size >> 8) & 0xFF);
    out[6] = static_cast<uint8_t>((size >> 16) & 0xFF);
    out[7] = static_cast<uint8_t>((size >> 24) & 0xFF);

    putU32(out, computeCrc(out.data(), out.size()));
    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderUDI::loadImage()
{
    _warnings.clear();

    if (!FileHelper::FileExists(_filepath))
    {
        _warnings.push_back("File not found: " + _filepath);
        return false;
    }

    const size_t fileSize = FileHelper::GetFileSize(_filepath);
    if (fileSize == 0)
    {
        _warnings.push_back("File is empty: " + _filepath);
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
    {
        return false;
    }

    image->setFilePath(_filepath);
    image->setLoaded(true);
    _diskImage = image;
    return true;
}

bool LoaderUDI::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderUDI::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("UDI save refused: no image or empty path");
        return false;
    }

    std::vector<uint8_t> buffer;
    if (!serialize(_diskImage, buffer, _warnings))
    {
        return false;
    }

    FILE* file = FileHelper::OpenFile(path, "wb");
    if (!file)
    {
        _warnings.push_back("UDI save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("UDI save failed: cannot write " + path);
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
