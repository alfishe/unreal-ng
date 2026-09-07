#include "loader_dsk.h"

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

    /// Sector size in bytes for a uPD765 size code as it is stored in the file (N up to 7 => 16 KB).
    /// Standard DSK images store 128 << N bytes, except that N >= 6 is stored as 0x1800 bytes (CPCEMU convention).
    inline size_t storedSizeStandard(uint8_t n)
    {
        if (n >= 6) return 0x1800;
        return static_cast<size_t>(128u) << n;
    }

    /// Sector size the WD1793 model uses for a size code (N masked to two bits)
    inline size_t modelSize(uint8_t n) { return static_cast<size_t>(128u) << (n & 0x03); }
}

/// region <Static helpers>

bool LoaderDSK::detect(const uint8_t* data, size_t len, bool* extended)
{
    if (extended) *extended = false;
    if (!data || len < SIGNATURE_LEN) return false;

    if (std::memcmp(data, SIGNATURE_EXTENDED, SIGNATURE_LEN) == 0)
    {
        if (extended) *extended = true;
        return true;
    }

    return std::memcmp(data, SIGNATURE_STANDARD, SIGNATURE_LEN) == 0;
}

DiskImage* LoaderDSK::createBlankPlus3Image()
{
    return new DiskImage(PLUS3_CYLINDERS, PLUS3_SIDES, DiskImage::TrackFormatSpec::plus3());
}

uint8_t LoaderDSK::measureGap3(const DiskImage::Track* track)
{
    const uint8_t fallback = 0x4E;
    if (!track || track->sectorCount() == 0) return fallback;

    const DiskImage::Sector& first = track->sectors()[0];
    const size_t len = track->rawSize();
    const uint8_t* raw = track->rawData();
    const bool mfm = (track->encoding() == DiskImage::Encoding::MFM);

    // Gap starts after the data CRC (or after the ID CRC for an ID-only sector)
    size_t pos = first.hasData ? static_cast<size_t>(first.dataOffset) + first.dataSize + 2
                               : static_cast<size_t>(first.idamOffset) + 7;

    // ...and ends at the sync of the next ID field (or at the end of the stream)
    size_t limit = len;
    if (track->sectorCount() > 1)
    {
        const DiskImage::Sector& next = track->sectors()[1];
        limit = std::min(limit, static_cast<size_t>(next.idamOffset) - (mfm ? 3 : 0));
    }

    size_t count = 0;
    while (pos + count < limit && raw[pos + count] != 0x00 && count < 255) count++;

    return static_cast<uint8_t>(count);
}

uint8_t LoaderDSK::mostFrequentDataByte(const DiskImage::Track* track)
{
    const uint8_t fallback = 0xE5;
    if (!track) return fallback;

    size_t histogram[256] = {0};
    bool any = false;
    for (const DiskImage::Sector& sector : track->sectors())
    {
        if (!sector.hasData) continue;
        any = true;
        for (size_t i = 0; i < sector.dataSize; i++) histogram[sector.data[i]]++;
    }
    if (!any) return fallback;

    size_t best = 0;
    for (size_t b = 1; b < 256; b++)
    {
        if (histogram[b] > histogram[best]) best = b;
    }
    return static_cast<uint8_t>(best);
}

/// endregion </Static helpers>

/// region <Parsing>

bool LoaderDSK::parseTrack(DiskImage* image, uint8_t cylinder, uint8_t side, const uint8_t* info, const uint8_t* dataEnd,
                           bool extended, std::vector<std::string>& warnings)
{
    if (std::memcmp(info, TRACK_SIGNATURE, std::strlen(TRACK_SIGNATURE)) != 0)
    {
        warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: 'Track-Info' signature missing", cylinder, side));
        return false;
    }

    const uint8_t mode = extended ? info[OFF_TI_MODE] : MODE_MFM;
    const uint8_t sectorCount = info[OFF_TI_SECTOR_COUNT];
    const uint8_t gap3 = info[OFF_TI_GAP3];
    const uint8_t filler = info[OFF_TI_FILLER];

    if (sectorCount > MAX_SECTORS_PER_TRACK)
    {
        warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d declares %d sectors, at most %zu fit into the track information block",
                                                cylinder, side, sectorCount, MAX_SECTORS_PER_TRACK));
        return false;
    }

    DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
    if (!track)
    {
        warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d is outside the image", cylinder, side));
        return false;
    }

    /// region <Layout>
    DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::ibm(0, DiskImage::SECTOR_SIZE_512, 1, gap3, filler);
    if (mode == MODE_FM)
    {
        spec.encoding = DiskImage::Encoding::FM;
        spec.trackLength = NOMINAL_TRACK_LEN_FM;
        spec.gapFill = 0xFF;
        spec.syncLength = 6;
        spec.gapIndex = 40;
        spec.gapPostIndex = 26;
        spec.gapPostID = 11;
    }

    struct SectorEntry
    {
        uint8_t st1 = 0;
        uint8_t st2 = 0;
        size_t stored = 0;      // Bytes present in the file for this sector
        const uint8_t* src = nullptr;
    };
    std::vector<SectorEntry> entries(sectorCount);

    spec.sectorNumbers.resize(sectorCount);
    spec.sectorSizeCodes.resize(sectorCount);
    spec.sectorCylinders.resize(sectorCount);
    spec.sectorHeads.resize(sectorCount);
    spec.idOnly.assign(sectorCount, 0);

    const uint8_t* src = info + TRACK_INFO_SIZE;
    for (size_t i = 0; i < sectorCount; i++)
    {
        const uint8_t* entry = info + OFF_TI_SECTOR_INFO + i * SECTOR_INFO_SIZE;
        const uint8_t n = entry[3];

        spec.sectorCylinders[i] = entry[0];
        spec.sectorHeads[i] = entry[1];
        spec.sectorNumbers[i] = entry[2];
        spec.sectorSizeCodes[i] = n;

        SectorEntry& se = entries[i];
        se.st1 = entry[4];
        se.st2 = entry[5];
        se.stored = extended ? readU16(entry + 6) : storedSizeStandard(n);
        se.src = src;

        if (src + se.stored > dataEnd)
        {
            warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: sector data truncated (sector %d, %zu bytes expected)",
                                                    cylinder, side, entry[2], se.stored));
            return false;
        }
        src += se.stored;

        const bool idOnly = (se.st1 & (ST1_NO_DATA | ST1_MISSING_ADDRESS_MARK)) != 0 || (se.st2 & ST2_MISSING_DATA_MARK) != 0 ||
                            (extended && se.stored == 0);
        spec.idOnly[i] = idOnly ? 1 : 0;

        if (n > 3)
        {
            warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: sector %d has size code %d, the WD1793 model keeps %zu bytes",
                                                    cylinder, side, entry[2], n, modelSize(n)));
        }
    }

    // A layout that only misses the nominal length by the index mark (e.g. a TR-DOS style 16 x 256 track saved to
    // DSK) is rebuilt without the IAM instead of stretching the track
    if (!spec.fits())
    {
        DiskImage::TrackFormatSpec withoutIam = spec;
        withoutIam.indexMark = false;
        if (withoutIam.fits()) spec = withoutIam;
    }

    // Grow the track when the layout does not fit the nominal length; shrink GAP#3 as a last resort
    if (!spec.fits())
    {
        const size_t needed = spec.totalBytes();
        if (needed <= DiskImage::RawTrack::MAX_TRACK_SIZE)
        {
            spec.trackLength = needed;
            warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: %d sectors with GAP#3 %d need %zu bytes, track grown from %zu",
                                                    cylinder, side, sectorCount, gap3, needed, spec.encoding == DiskImage::Encoding::FM
                                                        ? NOMINAL_TRACK_LEN_FM : MAX_TRACK_LEN));
        }
        else
        {
            spec.trackLength = DiskImage::RawTrack::MAX_TRACK_SIZE;
            while (!spec.fits() && spec.gapPostData > 1) spec.gapPostData--;
            if (!spec.fits())
            {
                warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: %d sectors do not fit into %zu bytes",
                                                        cylinder, side, sectorCount, DiskImage::RawTrack::MAX_TRACK_SIZE));
                return false;
            }
            warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: layout too large, track grown to %zu bytes and GAP#3 reduced to %d",
                                                    cylinder, side, DiskImage::RawTrack::MAX_TRACK_SIZE, spec.gapPostData));
        }
    }

    track->formatTrack(cylinder, side, spec);
    /// endregion </Layout>

    /// region <Data and status>
    std::vector<DiskImage::Sector>& sectors = track->sectors();
    if (sectors.size() != sectorCount)
    {
        warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: %zu of %d sectors indexed after formatting",
                                                cylinder, side, sectors.size(), sectorCount));
        return false;
    }

    for (size_t i = 0; i < sectorCount; i++)
    {
        DiskImage::Sector& sector = sectors[i];
        const SectorEntry& se = entries[i];

        if (sector.hasData)
        {
            const size_t size = sector.dataSize;
            const size_t copies = (se.stored > size && se.stored % size == 0) ? se.stored / size : 1;

            std::memcpy(sector.data, se.src, std::min(se.stored, size));

            // Weak sector: several copies, differing bytes become weak
            for (size_t k = 1; k < copies; k++)
            {
                const uint8_t* copy = se.src + k * size;
                for (size_t b = 0; b < size; b++)
                {
                    if (copy[b] != se.src[b]) track->setWeakByte(sector.dataOffset + b, true);
                }
            }
            if (copies == 1 && se.stored > size)
            {
                warnings.push_back(StringHelper::Format("DSK track cylinder %d side %d: sector %d stores %zu bytes for a %zu-byte sector, extra bytes dropped",
                                                        cylinder, side, sector.number(), se.stored, size));
            }

            if (se.st2 & ST2_CONTROL_MARK) sector.setDataAddressMark(0xF8);
            sector.recalculateDataCRC();

            if (se.st2 & ST2_DATA_ERROR_IN_DATA)
            {
                sector.data[sector.dataSize] ^= 0xFF;  // Corrupt the data CRC
                sector.isDataCRCValid();
            }
        }

        // ST1.DE without ST2.DD: the uPD765 failed the CRC of the ID field
        if ((se.st1 & ST1_DATA_ERROR) && !(se.st2 & ST2_DATA_ERROR_IN_DATA))
        {
            sector.id->id_crc ^= 0x00FF;
            sector.isIDCRCValid();
        }
    }

    track->reindex();
    track->markClean();
    /// endregion </Data and status>

    return true;
}

DiskImage* LoaderDSK::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    bool extended = false;
    if (!detect(data, len, &extended))
    {
        warnings.push_back("Not a DSK file: 'MV - CPC' / 'EXTENDED' signature missing");
        return nullptr;
    }
    if (len < DISK_INFO_SIZE)
    {
        warnings.push_back("DSK file is truncated (shorter than the disk information block)");
        return nullptr;
    }

    /// region <Disk information block>
    const uint8_t tracks = data[OFF_TRACKS];
    const uint8_t sides = data[OFF_SIDES];
    const uint16_t standardTrackSize = readU16(data + OFF_TRACK_SIZE);

    if (tracks == 0)
    {
        warnings.push_back("DSK declares 0 tracks");
        return nullptr;
    }
    if (tracks > MAX_CYLINDERS)
    {
        warnings.push_back(StringHelper::Format("DSK declares %d tracks, the emulator supports at most %d", tracks, MAX_CYLINDERS));
        return nullptr;
    }
    if (sides == 0 || sides > MAX_SIDES)
    {
        warnings.push_back(StringHelper::Format("DSK declares %d sides, only 1 or 2 are supported", sides));
        return nullptr;
    }
    const size_t trackCount = static_cast<size_t>(tracks) * sides;
    if (extended && trackCount > MAX_TRACKS)
    {
        warnings.push_back(StringHelper::Format("EDSK declares %zu tracks, the size table holds at most %zu", trackCount, MAX_TRACKS));
        return nullptr;
    }
    if (!extended && standardTrackSize < TRACK_INFO_SIZE)
    {
        warnings.push_back(StringHelper::Format("DSK track size %d is smaller than the track information block", standardTrackSize));
        return nullptr;
    }
    /// endregion </Disk information block>

    _extended = extended;
    _creator.assign(reinterpret_cast<const char*>(data + OFF_CREATOR), CREATOR_LEN);
    while (!_creator.empty() && (_creator.back() == ' ' || _creator.back() == '\0')) _creator.pop_back();

    DiskImage* image = new DiskImage(tracks, sides);

    /// region <Tracks>
    size_t offset = DISK_INFO_SIZE;

    for (uint8_t cylinder = 0; cylinder < tracks; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            const size_t index = static_cast<size_t>(cylinder) * sides + side;
            const size_t trackSize = extended ? static_cast<size_t>(data[OFF_TRACK_SIZE_TABLE + index]) * 256 : standardTrackSize;

            if (trackSize == 0)
            {
                // Unformatted track: nominal length of gap bytes, no sectors
                DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
                track->resizeRaw(DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM);
                track->markClean();
                continue;
            }

            if (offset + TRACK_INFO_SIZE > len)
            {
                warnings.push_back(StringHelper::Format("DSK truncated at track cylinder %d side %d", cylinder, side));
                delete image;
                return nullptr;
            }

            // Sector data may not run past the declared track block (extended) / file end
            const uint8_t* dataEnd = data + std::min(len, offset + trackSize);

            if (!parseTrack(image, cylinder, side, data + offset, dataEnd, extended, warnings))
            {
                delete image;
                return nullptr;
            }

            offset += trackSize;
        }
    }
    /// endregion </Tracks>

    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialisation>

bool LoaderDSK::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("DSK save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();
    if (cylinders == 0 || sides == 0 || sides > 2)
    {
        warnings.push_back("DSK save refused: invalid disk geometry");
        return false;
    }
    const size_t trackCount = static_cast<size_t>(cylinders) * sides;
    if (trackCount > MAX_TRACKS)
    {
        warnings.push_back(StringHelper::Format("DSK save refused: %zu tracks exceed the EDSK size table (%zu)", trackCount, MAX_TRACKS));
        return false;
    }

    /// region <Disk information block>
    out.assign(DISK_INFO_SIZE, 0);
    std::memcpy(out.data(), SIGNATURE_EXTENDED_FULL, SIGNATURE_FULL_LEN);
    std::memcpy(out.data() + OFF_CREATOR, CREATOR, CREATOR_LEN);
    out[OFF_TRACKS] = cylinders;
    out[OFF_SIDES] = sides;
    /// endregion </Disk information block>

    /// region <Tracks>
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            const size_t index = static_cast<size_t>(cylinder) * sides + side;
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("DSK save refused: track cylinder %d side %d is missing", cylinder, side));
                return false;
            }

            const std::vector<DiskImage::Sector>& sectors = track->sectors();
            if (sectors.empty())
            {
                // Unformatted (no ID fields): size 0, no track block
                out[OFF_TRACK_SIZE_TABLE + index] = 0;
                continue;
            }
            if (sectors.size() > MAX_SECTORS_PER_TRACK)
            {
                warnings.push_back(StringHelper::Format("DSK save refused: track cylinder %d side %d has %zu sectors, EDSK holds at most %zu",
                                                        cylinder, side, sectors.size(), MAX_SECTORS_PER_TRACK));
                return false;
            }

            const bool fm = (track->encoding() == DiskImage::Encoding::FM);
            const size_t blockStart = out.size();
            out.resize(blockStart + TRACK_INFO_SIZE, 0);
            uint8_t* info = out.data() + blockStart;

            std::memcpy(info, "Track-Info\r\n", 12);
            info[OFF_TI_CYLINDER] = cylinder;
            info[OFF_TI_SIDE] = side;
            info[OFF_TI_RATE] = RATE_SD_DD;
            info[OFF_TI_MODE] = fm ? MODE_FM : MODE_MFM;
            info[OFF_TI_SIZE_CODE] = sectors[0].sizeCode();
            info[OFF_TI_SECTOR_COUNT] = static_cast<uint8_t>(sectors.size());
            info[OFF_TI_GAP3] = measureGap3(track);
            info[OFF_TI_FILLER] = mostFrequentDataByte(track);

            for (size_t i = 0; i < sectors.size(); i++)
            {
                const DiskImage::Sector& sector = sectors[i];
                uint8_t st1 = 0;
                uint8_t st2 = 0;
                size_t stored = 0;
                bool weak = false;

                if (sector.hasData)
                {
                    stored = sector.dataSize;
                    if (!sector.dataCrcValid)
                    {
                        st1 |= ST1_DATA_ERROR;
                        st2 |= ST2_DATA_ERROR_IN_DATA;
                    }
                    if (sector.deleted) st2 |= ST2_CONTROL_MARK;

                    for (size_t b = 0; b < sector.dataSize && !weak; b++)
                    {
                        weak = track->weakByte(sector.dataOffset + b);
                    }
                    if (weak) stored *= 2;  // Two copies, the second one has the weak bytes inverted
                }
                else
                {
                    st1 |= ST1_NO_DATA;
                    st2 |= ST2_MISSING_DATA_MARK;
                }
                if (!sector.idCrcValid) st1 |= ST1_DATA_ERROR;

                uint8_t* entry = out.data() + blockStart + OFF_TI_SECTOR_INFO + i * SECTOR_INFO_SIZE;
                entry[0] = sector.cylinder();
                entry[1] = sector.head();
                entry[2] = sector.number();
                entry[3] = sector.sizeCode();
                entry[4] = st1;
                entry[5] = st2;
                entry[6] = static_cast<uint8_t>(stored & 0xFF);
                entry[7] = static_cast<uint8_t>(stored >> 8);

                if (sector.hasData)
                {
                    out.insert(out.end(), sector.data, sector.data + sector.dataSize);
                    if (weak)
                    {
                        for (size_t b = 0; b < sector.dataSize; b++)
                        {
                            const uint8_t value = sector.data[b];
                            out.push_back(track->weakByte(sector.dataOffset + b) ? static_cast<uint8_t>(~value) : value);
                        }
                    }
                }
            }

            // Track blocks are multiples of 256 bytes (size table stores size / 256)
            const size_t blockSize = out.size() - blockStart;
            const size_t padded = (blockSize + 255) & ~static_cast<size_t>(255);
            if (padded / 256 > 255)
            {
                warnings.push_back(StringHelper::Format("DSK save refused: track cylinder %d side %d is larger than 65280 bytes", cylinder, side));
                return false;
            }
            out.resize(blockStart + padded, 0);
            out[OFF_TRACK_SIZE_TABLE + index] = static_cast<uint8_t>(padded / 256);

            if (track->rawSize() != DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM &&
                track->rawSize() != DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM)
            {
                warnings.push_back(StringHelper::Format("DSK cannot store the track length: track cylinder %d side %d (%zu bytes) saved without it",
                                                        cylinder, side, track->rawSize()));
            }
        }
    }
    /// endregion </Tracks>

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderDSK::loadImage()
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

bool LoaderDSK::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderDSK::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("DSK save refused: no image or empty path");
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
        _warnings.push_back("DSK save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("DSK save failed: cannot write " + path);
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
