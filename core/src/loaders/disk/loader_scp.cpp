#include "loader_scp.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/flux/flux_pll.h"
#include "emulator/io/fdc/flux/mfm_decoder.h"
#include "emulator/io/fdc/flux/mfm_encoder.h"
#include "emulator/notifications.h"

namespace
{
    inline uint16_t readU16BE(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
    inline uint32_t readU32LE(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    inline void putU32LE(std::vector<uint8_t>& out, uint32_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }
    inline void putU16BE(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    uint32_t computeChecksum(const uint8_t* data, size_t len)
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < len; i++)
        {
            sum += data[i];
        }
        return sum;
    }
}

/// region <Static helpers>

bool LoaderSCP::detect(const uint8_t* data, size_t len)
{
    return data && len >= SIGNATURE_LEN && std::memcmp(data, SIGNATURE, SIGNATURE_LEN) == 0;
}

bool LoaderSCP::parseFluxData(const uint8_t* data, size_t offset, uint32_t count,
                               std::vector<uint32_t>& intervalsNs)
{
    intervalsNs.clear();
    intervalsNs.reserve(count);

    uint32_t accumulated = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint16_t ticks = readU16BE(data + offset + i * 2);
        if (ticks == 0)
        {
            accumulated += 65536;
        }
        else
        {
            const uint32_t totalTicks = accumulated + ticks;
            intervalsNs.push_back(totalTicks * NS_PER_TICK);
            accumulated = 0;
        }
    }

    return true;
}

void LoaderSCP::decodeTrack(const std::vector<uint32_t>& intervalsNs, bool fm,
                            std::vector<uint8_t>& bytes, std::vector<uint8_t>& clockBitmap)
{
    bytes.clear();
    clockBitmap.clear();

    if (intervalsNs.empty()) return;

    fdc::flux::FluxPll pll{fm ? fdc::flux::FluxPll::Mode::FM : fdc::flux::FluxPll::Mode::MFM};
    pll.addIntervals(intervalsNs);

    if (fm)
    {
        fdc::flux::FmDecoder decoder;
        decoder.decode(pll.bitCells());
        bytes = decoder.bytes();
        clockBitmap = decoder.clockBitmap();
    }
    else
    {
        fdc::flux::MfmDecoder decoder;
        decoder.decode(pll.bitCells());
        bytes = decoder.bytes();
        clockBitmap = decoder.clockBitmap();
    }
}

void LoaderSCP::mergeRevolutions(const std::vector<std::vector<uint8_t>>& revBytes,
                                  std::vector<uint8_t>& merged,
                                  std::vector<bool>& weakBits)
{
    merged.clear();
    weakBits.clear();

    if (revBytes.empty()) return;

    size_t maxLen = 0;
    for (const auto& rev : revBytes)
    {
        maxLen = std::max(maxLen, rev.size());
    }

    merged.resize(maxLen, 0);
    weakBits.resize(maxLen, false);

    for (size_t i = 0; i < maxLen; i++)
    {
        int histogram[256] = {0};
        int validCount = 0;

        for (const auto& rev : revBytes)
        {
            if (i < rev.size())
            {
                histogram[rev[i]]++;
                validCount++;
            }
        }

        if (validCount == 0)
        {
            merged[i] = 0;
            continue;
        }

        int bestValue = 0;
        int bestCount = 0;
        for (int v = 0; v < 256; v++)
        {
            if (histogram[v] > bestCount)
            {
                bestCount = histogram[v];
                bestValue = v;
            }
        }

        merged[i] = static_cast<uint8_t>(bestValue);

        int different = 0;
        for (int v = 0; v < 256; v++)
        {
            if (v != bestValue && histogram[v] > 0)
            {
                different += histogram[v];
            }
        }
        if (different > 0)
        {
            weakBits[i] = true;
        }
    }
}

void LoaderSCP::encodeTrack(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap,
                            bool fm, std::vector<uint16_t>& fluxTicks)
{
    fluxTicks.clear();

    std::vector<uint8_t> bitCells;
    if (fm)
    {
        fdc::flux::FmEncoder encoder;
        encoder.encode(bytes, count, clockBitmap);
        bitCells = encoder.bitCells();
    }
    else
    {
        fdc::flux::MfmEncoder encoder;
        encoder.encode(bytes, count, clockBitmap);
        bitCells = encoder.bitCells();
    }

    const uint32_t nominalCellNs = fm ? 4000 : 2000;
    const uint32_t ticksPerCell = nominalCellNs / NS_PER_TICK;

    uint32_t cellsSinceTransition = 0;
    for (uint8_t cell : bitCells)
    {
        cellsSinceTransition++;
        if (cell == 1)
        {
            uint32_t totalTicks = cellsSinceTransition * ticksPerCell;
            while (totalTicks >= 65536)
            {
                fluxTicks.push_back(0);
                totalTicks -= 65536;
            }
            if (totalTicks > 0)
            {
                fluxTicks.push_back(static_cast<uint16_t>(totalTicks));
            }
            cellsSinceTransition = 0;
        }
    }

    if (cellsSinceTransition > 0)
    {
        uint32_t totalTicks = cellsSinceTransition * ticksPerCell;
        while (totalTicks >= 65536)
        {
            fluxTicks.push_back(0);
            totalTicks -= 65536;
        }
        if (totalTicks > 0)
        {
            fluxTicks.push_back(static_cast<uint16_t>(totalTicks));
        }
    }
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderSCP::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    if (!detect(data, len))
    {
        warnings.push_back("Not an SCP file: signature 'SCP' missing");
        return nullptr;
    }
    if (len < HEADER_SIZE + TRACK_TABLE_ENTRIES * 4)
    {
        warnings.push_back("SCP file is truncated (shorter than header + track table)");
        return nullptr;
    }

    /// region <Header>
    _version = data[3];
    _diskType = data[4];
    const uint8_t revolutions = data[5];
    const uint8_t startTrack = data[6];
    const uint8_t endTrack = data[7];
    _flags = data[8];
    _bitCellWidth = data[9];
    _heads = data[10];
    _resolution = data[11];

    if (revolutions == 0)
    {
        warnings.push_back("SCP declares 0 revolutions per track");
        return nullptr;
    }
    if (revolutions > MAX_REVOLUTIONS)
    {
        warnings.push_back(StringHelper::Format("SCP declares %d revolutions, only the first %d will be used",
                                                revolutions, MAX_REVOLUTIONS));
    }
    if (endTrack < startTrack)
    {
        warnings.push_back("SCP end track is before start track");
        return nullptr;
    }

    const bool is96tpi = (_flags & FLAG_96TPI) != 0;
    /// endregion </Header>

    uint8_t maxCylinder = 0;
    uint8_t maxSide = 0;
    std::vector<TrackData> tracks;

    /// region <Track parsing>
    for (uint8_t t = startTrack; t <= endTrack; t++)
    {
        const uint32_t trackOffset = readU32LE(data + TRACK_TABLE_OFFSET + t * 4);
        if (trackOffset == 0) continue;

        if (trackOffset + TRACK_HEADER_SIZE > len)
        {
            warnings.push_back(StringHelper::Format("SCP track %d offset is outside the file", t));
            continue;
        }

        if (std::memcmp(data + trackOffset, "TRK", 3) != 0)
        {
            warnings.push_back(StringHelper::Format("SCP track %d missing 'TRK' signature", t));
            continue;
        }

        const uint8_t trackNumber = data[trackOffset + 3];
        const uint8_t cylinder = trackNumber / 2;
        const uint8_t side = trackNumber % 2;

        maxCylinder = std::max(maxCylinder, cylinder);
        maxSide = std::max(maxSide, side);

        TrackData td;
        td.trackNumber = trackNumber;

        const uint8_t revsToRead = std::min(revolutions, MAX_REVOLUTIONS);
        for (uint8_t r = 0; r < revsToRead; r++)
        {
            const size_t revHeaderOffset = trackOffset + TRACK_HEADER_SIZE + r * REV_HEADER_SIZE;
            if (revHeaderOffset + REV_HEADER_SIZE > len)
            {
                warnings.push_back(StringHelper::Format("SCP track %d revolution %d header is outside the file", t, r));
                break;
            }

            const uint32_t indexTime = readU32LE(data + revHeaderOffset);
            const uint32_t fluxCount = readU32LE(data + revHeaderOffset + 4);
            const uint32_t dataOffset = readU32LE(data + revHeaderOffset + 8);

            const size_t fluxStart = trackOffset + dataOffset;
            if (fluxStart + fluxCount * 2 > len)
            {
                warnings.push_back(StringHelper::Format("SCP track %d revolution %d flux data is outside the file", t, r));
                break;
            }

            RevolutionData rd;
            rd.indexTimeNs = indexTime * NS_PER_TICK;
            parseFluxData(data, fluxStart, fluxCount, rd.fluxIntervalsNs);
            td.revolutions.push_back(std::move(rd));
        }

        tracks.push_back(std::move(td));
    }
    /// endregion </Track parsing>

    if (tracks.empty())
    {
        warnings.push_back("SCP file contains no valid tracks");
        return nullptr;
    }

    const uint8_t cylinders = static_cast<uint8_t>(maxCylinder + 1);
    const uint8_t sides = static_cast<uint8_t>(maxSide + 1);

    if (cylinders > MAX_CYLINDERS)
    {
        warnings.push_back(StringHelper::Format("SCP image has %d cylinders, maximum is %d", cylinders, MAX_CYLINDERS));
        return nullptr;
    }

    DiskImage* image = new DiskImage(cylinders, sides);

    /// region <Track conversion>
    for (const TrackData& td : tracks)
    {
        const uint8_t cylinder = td.trackNumber / 2;
        const uint8_t side = td.trackNumber % 2;

        DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);
        if (!track) continue;

        if (td.revolutions.empty())
        {
            track->resizeRaw(DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM);
            track->markClean();
            continue;
        }

        bool isFM = false;
        if (!td.revolutions[0].fluxIntervalsNs.empty())
        {
            isFM = fdc::flux::FluxPll::detectMode(td.revolutions[0].fluxIntervalsNs.data(),
                                                   td.revolutions[0].fluxIntervalsNs.size())
                   == fdc::flux::FluxPll::Mode::FM;
        }

        std::vector<std::vector<uint8_t>> revBytes;
        std::vector<std::vector<uint8_t>> revClocks;

        for (const RevolutionData& rd : td.revolutions)
        {
            std::vector<uint8_t> bytes, clockBitmap;
            decodeTrack(rd.fluxIntervalsNs, isFM, bytes, clockBitmap);
            revBytes.push_back(std::move(bytes));
            revClocks.push_back(std::move(clockBitmap));
        }

        std::vector<uint8_t> merged;
        std::vector<bool> weakBits;

        if (revBytes.size() == 1)
        {
            merged = revBytes[0];
        }
        else
        {
            mergeRevolutions(revBytes, merged, weakBits);
        }

        if (merged.empty())
        {
            track->resizeRaw(isFM ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM
                                  : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM,
                             isFM ? DiskImage::Encoding::FM : DiskImage::Encoding::MFM);
        }
        else
        {
            track->setRaw(merged.data(), merged.size(),
                          isFM ? DiskImage::Encoding::FM : DiskImage::Encoding::MFM);

            if (!revClocks.empty() && !revClocks[0].empty())
            {
                track->setClockBitmap(revClocks[0].data(), revClocks[0].size());
            }

            for (size_t i = 0; i < weakBits.size() && i < merged.size(); i++)
            {
                if (weakBits[i])
                {
                    track->setWeakByte(i, true);
                }
            }
        }

        track->reindex();
        track->markClean();
    }
    /// endregion </Track conversion>

    if (is96tpi && cylinders <= 42)
    {
        warnings.push_back("SCP image has 96 TPI flag set with <= 42 cylinders: may need double-stepping");
    }

    image->markClean();
    return image;
}

/// endregion </Parsing>

/// region <Serialisation>

bool LoaderSCP::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("SCP save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();
    if (cylinders == 0 || cylinders > MAX_CYLINDERS || sides == 0 || sides > 2)
    {
        warnings.push_back("SCP save refused: invalid disk geometry");
        return false;
    }

    const uint8_t startTrack = 0;
    const uint8_t endTrack = static_cast<uint8_t>(cylinders * sides - 1);
    const bool is80track = (cylinders >= 70);

    /// region <Header>
    out.push_back('S');
    out.push_back('C');
    out.push_back('P');
    out.push_back(_version);
    out.push_back(_diskType);
    out.push_back(1);
    out.push_back(startTrack);
    out.push_back(endTrack);
    out.push_back(static_cast<uint8_t>(_flags | (is80track ? FLAG_96TPI : 0)));
    out.push_back(_bitCellWidth);
    out.push_back(sides == 1 ? HEADS_SIDE0 : HEADS_BOTH);
    out.push_back(_resolution);
    putU32LE(out, 0);
    /// endregion </Header>

    /// region <Track table placeholder>
    const size_t trackTableStart = out.size();
    for (size_t i = 0; i < TRACK_TABLE_ENTRIES; i++)
    {
        putU32LE(out, 0);
    }
    /// endregion </Track table placeholder>

    /// region <Tracks>
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t s = 0; s < sides; s++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, s);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("SCP save refused: track cylinder %d side %d is missing", c, s));
                return false;
            }

            const uint8_t trackNumber = c * 2 + s;
            const size_t trackOffset = out.size();

            const size_t trackTableEntry = trackTableStart + trackNumber * 4;
            out[trackTableEntry] = static_cast<uint8_t>(trackOffset & 0xFF);
            out[trackTableEntry + 1] = static_cast<uint8_t>((trackOffset >> 8) & 0xFF);
            out[trackTableEntry + 2] = static_cast<uint8_t>((trackOffset >> 16) & 0xFF);
            out[trackTableEntry + 3] = static_cast<uint8_t>((trackOffset >> 24) & 0xFF);

            out.push_back('T');
            out.push_back('R');
            out.push_back('K');
            out.push_back(trackNumber);

            const bool isFM = (track->encoding() == DiskImage::Encoding::FM);
            std::vector<uint16_t> fluxTicks;
            encodeTrack(track->rawData(), track->rawSize(), track->clockBitmap().data(), isFM, fluxTicks);

            const uint32_t nominalCellNs = isFM ? 4000 : 2000;
            const uint32_t bytesPerRev = isFM ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM
                                               : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM;
            const uint32_t indexTimeNs = bytesPerRev * 8 * nominalCellNs;
            const uint32_t indexTimeTicks = indexTimeNs / NS_PER_TICK;

            putU32LE(out, indexTimeTicks);
            putU32LE(out, static_cast<uint32_t>(fluxTicks.size()));
            putU32LE(out, REV_HEADER_SIZE);

            for (uint16_t tick : fluxTicks)
            {
                putU16BE(out, tick);
            }
        }
    }
    /// endregion </Tracks>

    /// region <Checksum>
    const uint32_t checksum = computeChecksum(out.data() + HEADER_SIZE, out.size() - HEADER_SIZE);
    out[12] = static_cast<uint8_t>(checksum & 0xFF);
    out[13] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    out[14] = static_cast<uint8_t>((checksum >> 16) & 0xFF);
    out[15] = static_cast<uint8_t>((checksum >> 24) & 0xFF);
    /// endregion </Checksum>

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderSCP::loadImage()
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

bool LoaderSCP::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderSCP::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("SCP save refused: no image or empty path");
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
        _warnings.push_back("SCP save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("SCP save failed: cannot write " + path);
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
