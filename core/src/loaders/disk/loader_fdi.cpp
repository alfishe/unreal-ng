#include "loader_fdi.h"

#include <algorithm>
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
    inline void patchU16(std::vector<uint8_t>& out, size_t at, uint16_t v)
    {
        out[at] = static_cast<uint8_t>(v & 0xFF);
        out[at + 1] = static_cast<uint8_t>(v >> 8);
    }

    // Datasheet minimum gap 3 for MFM is 16 bytes; the TR-DOS layout uses 60
    constexpr uint8_t MIN_GAP_POST_DATA = 16;
}

/// region <Static helpers>

bool LoaderFDI::detect(const uint8_t* data, size_t len)
{
    return data && len >= 3 && std::memcmp(data, SIGNATURE, 3) == 0;
}

bool LoaderFDI::buildTrackSpec(const std::vector<uint8_t>& numbers, const std::vector<uint8_t>& sizeCodes,
                               const std::vector<uint8_t>& cylinders, const std::vector<uint8_t>& heads,
                               const std::vector<uint8_t>& idOnly, DiskImage::TrackFormatSpec& spec, std::string* note)
{
    spec = DiskImage::TrackFormatSpec::trdos(numbers.empty() ? nullptr : numbers.data(), numbers.size());
    spec.sectorSizeCodes = sizeCodes;
    spec.sectorCylinders = cylinders;
    spec.sectorHeads = heads;
    spec.idOnly = idOnly;

    if (spec.fits()) return true;

    // Shrink the post-data gap first (keeps the nominal 6250-byte revolution)
    for (uint8_t gap = spec.gapPostData; gap >= MIN_GAP_POST_DATA; gap--)
    {
        spec.gapPostData = gap;
        if (spec.fits())
        {
            if (note) *note = StringHelper::Format("gap3 reduced to %d bytes to fit %zu sectors", gap, numbers.size());
            return true;
        }
    }

    // Then grow the track (real drives / images allow up to ~6464; the model allows up to MAX_TRACK_SIZE)
    spec.gapPostData = MIN_GAP_POST_DATA;
    const size_t needed = spec.totalBytes();
    if (needed <= DiskImage::RawTrack::MAX_TRACK_SIZE)
    {
        spec.trackLength = needed;
        if (note) *note = StringHelper::Format("track length grown to %zu bytes to fit %zu sectors", needed, numbers.size());
        return true;
    }

    if (note) *note = StringHelper::Format("%zu sectors (%zu bytes) exceed the maximum track length %zu", numbers.size(), needed, DiskImage::RawTrack::MAX_TRACK_SIZE);
    return false;
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderFDI::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    if (!detect(data, len))
    {
        warnings.push_back("Not an FDI file: signature 'FDI' missing");
        return nullptr;
    }
    if (len < HEADER_SIZE)
    {
        warnings.push_back("FDI file is truncated (shorter than the header)");
        return nullptr;
    }

    /// region <Header>
    _writeProtected = data[3] != 0;
    const uint16_t cylinders = readU16(data + 4);
    const uint16_t heads = readU16(data + 6);
    const uint16_t descriptionOffset = readU16(data + 8);
    const uint16_t dataOffset = readU16(data + 10);
    const uint16_t extraLength = readU16(data + 12);

    if (cylinders == 0 || cylinders > MAX_CYLINDERS)
    {
        warnings.push_back(StringHelper::Format("FDI declares %d cylinders, supported range is 1..%d", cylinders, MAX_CYLINDERS));
        return nullptr;
    }
    if (heads == 0 || heads > 2)
    {
        warnings.push_back(StringHelper::Format("FDI declares %d heads, supported range is 1..2", heads));
        return nullptr;
    }
    if (HEADER_SIZE + extraLength > len)
    {
        warnings.push_back("FDI extra header runs past the end of the file");
        return nullptr;
    }
    if (dataOffset > len)
    {
        warnings.push_back("FDI data offset is outside the file");
        return nullptr;
    }

    _extraHeader.assign(data + HEADER_SIZE, data + HEADER_SIZE + extraLength);

    _description.clear();
    if (descriptionOffset != 0 && descriptionOffset < len)
    {
        const uint8_t* text = data + descriptionOffset;
        size_t maxLen = len - descriptionOffset;
        size_t n = 0;
        while (n < maxLen && text[n] != 0) n++;
        _description.assign(reinterpret_cast<const char*>(text), n);
    }
    /// endregion </Header>

    DiskImage* image = new DiskImage(static_cast<uint8_t>(cylinders), static_cast<uint8_t>(heads));

    /// region <Tracks>
    size_t offset = HEADER_SIZE + extraLength;

    for (uint16_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint16_t head = 0; head < heads; head++)
        {
            if (offset + TRACK_HEADER_SIZE > len)
            {
                warnings.push_back(StringHelper::Format("FDI truncated at the track header of cylinder %d head %d", cylinder, head));
                delete image;
                return nullptr;
            }

            const uint32_t trackDataOffset = readU32(data + offset);
            const uint8_t sectorCount = data[offset + 6];
            offset += TRACK_HEADER_SIZE;

            if (offset + static_cast<size_t>(sectorCount) * SECTOR_HEADER_SIZE > len)
            {
                warnings.push_back(StringHelper::Format("FDI truncated inside the sector list of cylinder %d head %d", cylinder, head));
                delete image;
                return nullptr;
            }

            std::vector<uint8_t> numbers(sectorCount), sizeCodes(sectorCount), cyls(sectorCount), hds(sectorCount), idOnly(sectorCount, 0);
            std::vector<uint8_t> flags(sectorCount);
            std::vector<uint16_t> dataOffsets(sectorCount);

            for (uint8_t s = 0; s < sectorCount; s++)
            {
                const uint8_t* sh = data + offset + s * SECTOR_HEADER_SIZE;
                cyls[s] = sh[0];
                hds[s] = sh[1];
                numbers[s] = sh[2];
                sizeCodes[s] = sh[3];
                flags[s] = sh[4];
                dataOffsets[s] = readU16(sh + 5);

                if (sizeCodes[s] > 3)
                {
                    warnings.push_back(StringHelper::Format("FDI cylinder %d head %d sector %d: size code %d masked to %d (WD1793 uses 2 bits)",
                                                            cylinder, head, numbers[s], sizeCodes[s], sizeCodes[s] & 3));
                }

                const size_t dataSize = 128u << (sizeCodes[s] & 3);
                const size_t sectorDataStart = static_cast<size_t>(dataOffset) + trackDataOffset + dataOffsets[s];
                const bool hasData = !(flags[s] & FLAG_NO_DATA);
                if (hasData && sectorDataStart + dataSize > len)
                {
                    warnings.push_back(StringHelper::Format("FDI cylinder %d head %d sector %d: data lies outside the file, stored as ID-only",
                                                            cylinder, head, numbers[s]));
                    idOnly[s] = 1;
                }
                else if (!hasData)
                {
                    idOnly[s] = 1;
                }
            }
            offset += static_cast<size_t>(sectorCount) * SECTOR_HEADER_SIZE;

            DiskImage::TrackFormatSpec spec;
            std::string note;
            DiskImage::Track* track = image->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), static_cast<uint8_t>(head));

            if (sectorCount == 0)
            {
                track->resizeRaw(DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM);
                track->markClean();
                continue;
            }

            if (!buildTrackSpec(numbers, sizeCodes, cyls, hds, idOnly, spec, &note))
            {
                warnings.push_back(StringHelper::Format("FDI cylinder %d head %d: %s", cylinder, head, note.c_str()));
                delete image;
                return nullptr;
            }
            if (!note.empty())
            {
                warnings.push_back(StringHelper::Format("FDI cylinder %d head %d: %s", cylinder, head, note.c_str()));
            }

            track->formatTrack(static_cast<uint8_t>(cylinder), static_cast<uint8_t>(head), spec);

            for (uint8_t s = 0; s < sectorCount; s++)
            {
                DiskImage::Sector* sector = track->getRawSector(s);
                if (!sector) break;

                if (!sector->hasData) continue;

                const size_t dataSize = sector->dataSize;
                const size_t sectorDataStart = static_cast<size_t>(dataOffset) + trackDataOffset + dataOffsets[s];
                std::memcpy(sector->data, data + sectorDataStart, dataSize);

                if (flags[s] & FLAG_DELETED)
                {
                    sector->setDataAddressMark(0xF8);
                }
                sector->recalculateDataCRC();

                // No "CRC ok" bit for this sector size => the data field was read with a CRC error
                const bool crcOk = (flags[s] & (1u << (sizeCodes[s] & 3))) != 0;
                if (!crcOk)
                {
                    sector->setDataCRC(static_cast<uint16_t>(sector->dataCRC() ^ 0xFFFF));
                }
            }

            track->reindex();
            track->markClean();
        }
    }
    /// endregion </Tracks>

    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialisation>

bool LoaderFDI::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("FDI save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t heads = diskImage->getSides();

    // Header
    out.insert(out.end(), SIGNATURE, SIGNATURE + 3);
    out.push_back(_writeProtected ? 1 : 0);
    putU16(out, cylinders);
    putU16(out, heads);
    putU16(out, 0);  // description offset, patched below
    putU16(out, 0);  // data offset, patched below
    putU16(out, static_cast<uint16_t>(_extraHeader.size()));
    out.insert(out.end(), _extraHeader.begin(), _extraHeader.end());

    // Track headers and data collected separately, then concatenated
    std::vector<uint8_t> dataArea;
    bool lossy = false;

    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t head = 0; head < heads; head++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, head);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("FDI save refused: track cylinder %d head %d is missing", cylinder, head));
                return false;
            }

            const size_t sectorCount = std::min<size_t>(track->sectorCount(), 255);
            if (track->sectorCount() > 255)
            {
                warnings.push_back(StringHelper::Format("FDI cylinder %d head %d: only the first 255 of %zu sectors stored", cylinder, head, track->sectorCount()));
            }
            if (track->encoding() == DiskImage::Encoding::FM)
            {
                warnings.push_back(StringHelper::Format("FDI cylinder %d head %d: FM track stored as a plain sector list (density lost)", cylinder, head));
                lossy = true;
            }
            if (track->rawSize() != DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM)
            {
                lossy = true;
            }

            putU32(out, static_cast<uint32_t>(dataArea.size()));
            putU16(out, 0);
            out.push_back(static_cast<uint8_t>(sectorCount));

            uint16_t sectorDataOffset = 0;
            for (size_t s = 0; s < sectorCount; s++)
            {
                const DiskImage::Sector* sector = track->getRawSector(s);
                const uint8_t sizeCode = sector->sizeCode();

                uint8_t flags = 0;
                if (!sector->hasData)
                {
                    flags |= FLAG_NO_DATA;
                }
                else
                {
                    if (sector->dataCrcValid) flags |= static_cast<uint8_t>(1u << (sizeCode & 3));
                    if (sector->deleted) flags |= FLAG_DELETED;
                }
                if (!sector->idCrcValid)
                {
                    lossy = true;  // FDI has no way to say "ID CRC wrong"
                }

                out.push_back(sector->cylinder());
                out.push_back(sector->head());
                out.push_back(sector->number());
                out.push_back(sizeCode);
                out.push_back(flags);
                putU16(out, sectorDataOffset);

                if (sector->hasData)
                {
                    dataArea.insert(dataArea.end(), sector->data, sector->data + sector->dataSize);
                    sectorDataOffset = static_cast<uint16_t>(sectorDataOffset + sector->dataSize);
                }
            }
        }
    }

    // Description text (ASCIIZ) directly after the track headers, then the data area
    if (!_description.empty())
    {
        patchU16(out, 8, static_cast<uint16_t>(out.size()));
        out.insert(out.end(), _description.begin(), _description.end());
        out.push_back(0);
    }

    if (out.size() > 0xFFFF)
    {
        warnings.push_back("FDI save refused: header area exceeds 64 KB (too many tracks / sectors)");
        return false;
    }
    patchU16(out, 10, static_cast<uint16_t>(out.size()));
    out.insert(out.end(), dataArea.begin(), dataArea.end());

    if (lossy)
    {
        warnings.push_back("FDI stores sector lists only: gaps, clock marks, exact CRC values, track length and density are not preserved (use UDI for a lossless copy)");
    }

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderFDI::loadImage()
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

bool LoaderFDI::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderFDI::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("FDI save refused: no image or empty path");
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
        _warnings.push_back("FDI save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("FDI save failed: cannot write " + path);
        return false;
    }

    _diskImage->markClean();

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
