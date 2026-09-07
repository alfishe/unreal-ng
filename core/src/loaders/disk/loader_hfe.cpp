#include "loader_hfe.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/flux/mfm_decoder.h"
#include "emulator/io/fdc/flux/mfm_encoder.h"
#include "emulator/notifications.h"

namespace
{
    inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}

/// region <Static helpers>

int LoaderHFE::detect(const uint8_t* data, size_t len)
{
    if (!data || len < SIGNATURE_LEN) return 0;

    if (std::memcmp(data, SIGNATURE_V3, SIGNATURE_LEN) == 0) return 3;
    if (std::memcmp(data, SIGNATURE_V1, SIGNATURE_LEN) == 0) return 1;
    return 0;
}

void LoaderHFE::deinterleave(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& side0, std::vector<uint8_t>& side1)
{
    side0.clear();
    side1.clear();

    size_t pos = 0;
    while (pos + BLOCK_SIZE <= srcLen)
    {
        side0.insert(side0.end(), src + pos, src + pos + INTERLEAVE_SIZE);
        side1.insert(side1.end(), src + pos + INTERLEAVE_SIZE, src + pos + BLOCK_SIZE);
        pos += BLOCK_SIZE;
    }

    if (pos < srcLen)
    {
        const size_t remaining = srcLen - pos;
        if (remaining <= INTERLEAVE_SIZE)
        {
            side0.insert(side0.end(), src + pos, src + pos + remaining);
        }
        else
        {
            side0.insert(side0.end(), src + pos, src + pos + INTERLEAVE_SIZE);
            side1.insert(side1.end(), src + pos + INTERLEAVE_SIZE, src + srcLen);
        }
    }
}

void LoaderHFE::interleave(const std::vector<uint8_t>& side0, const std::vector<uint8_t>& side1, std::vector<uint8_t>& out)
{
    out.clear();

    const size_t maxLen = std::max(side0.size(), side1.size());
    const size_t blocks = (maxLen + INTERLEAVE_SIZE - 1) / INTERLEAVE_SIZE;

    for (size_t b = 0; b < blocks; b++)
    {
        const size_t s0Start = b * INTERLEAVE_SIZE;
        const size_t s1Start = b * INTERLEAVE_SIZE;

        for (size_t i = 0; i < INTERLEAVE_SIZE; i++)
        {
            out.push_back(s0Start + i < side0.size() ? side0[s0Start + i] : 0);
        }
        for (size_t i = 0; i < INTERLEAVE_SIZE; i++)
        {
            out.push_back(s1Start + i < side1.size() ? side1[s1Start + i] : 0);
        }
    }
}

uint8_t LoaderHFE::bitReverse(uint8_t b)
{
    b = static_cast<uint8_t>(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

void LoaderHFE::unpackBits(const uint8_t* data, size_t byteCount, std::vector<uint8_t>& bits, bool lsbFirst)
{
    bits.clear();
    bits.reserve(byteCount * 8);

    for (size_t i = 0; i < byteCount; i++)
    {
        uint8_t b = data[i];
        if (lsbFirst)
        {
            for (int bit = 0; bit < 8; bit++)
            {
                bits.push_back((b >> bit) & 1);
            }
        }
        else
        {
            for (int bit = 7; bit >= 0; bit--)
            {
                bits.push_back((b >> bit) & 1);
            }
        }
    }
}

void LoaderHFE::packBits(const std::vector<uint8_t>& bits, std::vector<uint8_t>& out, bool lsbFirst)
{
    out.clear();
    out.reserve((bits.size() + 7) / 8);

    size_t i = 0;
    while (i < bits.size())
    {
        uint8_t b = 0;
        for (int bit = 0; bit < 8 && i < bits.size(); bit++, i++)
        {
            if (bits[i])
            {
                b |= lsbFirst ? (1 << bit) : (1 << (7 - bit));
            }
        }
        out.push_back(b);
    }
}

void LoaderHFE::processV3Opcodes(const std::vector<uint8_t>& raw, std::vector<uint8_t>& bits,
                                  std::vector<std::pair<size_t, size_t>>& weakRanges)
{
    bits.clear();
    weakRanges.clear();

    size_t i = 0;
    while (i < raw.size())
    {
        uint8_t b = raw[i];

        if ((b & 0xF0) == 0xF0)
        {
            switch (b)
            {
                case OP_NOP:
                    i++;
                    break;

                case OP_SETINDEX:
                    i++;
                    break;

                case OP_SETBITRATE:
                    i += 3;
                    break;

                case OP_SKIPBITS:
                    if (i + 1 < raw.size())
                    {
                        const uint8_t count = raw[i + 1];
                        for (uint8_t n = 0; n < count; n++) bits.push_back(0);
                    }
                    i += 2;
                    break;

                case OP_RAND:
                    if (i + 1 < raw.size())
                    {
                        const uint8_t count = raw[i + 1];
                        const size_t start = bits.size();
                        for (uint8_t n = 0; n < count; n++) bits.push_back(0);
                        weakRanges.emplace_back(start, count);
                    }
                    i += 2;
                    break;

                default:
                    for (int bit = 7; bit >= 0; bit--)
                    {
                        bits.push_back((b >> bit) & 1);
                    }
                    i++;
                    break;
            }
        }
        else
        {
            for (int bit = 7; bit >= 0; bit--)
            {
                bits.push_back((b >> bit) & 1);
            }
            i++;
        }
    }
}

/// endregion </Static helpers>

/// region <Parsing>

DiskImage* LoaderHFE::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    const int version = detect(data, len);
    if (version == 0)
    {
        warnings.push_back("Not an HFE file: signature 'HXCPICFE' or 'HXCHFEV3' missing");
        return nullptr;
    }
    if (len < HEADER_SIZE)
    {
        warnings.push_back("HFE file is truncated (shorter than the header)");
        return nullptr;
    }

    /// region <Header>
    _formatRevision = data[0x08];
    const uint8_t tracks = data[0x09];
    const uint8_t sides = data[0x0A];
    const uint8_t encoding = data[0x0B];
    _bitRate = readU16(data + 0x0C);
    _rpm = readU16(data + 0x0E);
    _interfaceMode = data[0x10];
    const uint16_t lutOffset = readU16(data + 0x12);
    _writeAllowed = data[0x14] != 0;
    _singleStep = data[0x15] != 0;

    if (tracks == 0 || tracks > MAX_CYLINDERS)
    {
        warnings.push_back(StringHelper::Format("HFE declares %d tracks, supported range is 1..%d", tracks, MAX_CYLINDERS));
        return nullptr;
    }
    if (sides == 0 || sides > 2)
    {
        warnings.push_back(StringHelper::Format("HFE declares %d sides, only 1 or 2 are supported", sides));
        return nullptr;
    }
    if (lutOffset == 0)
    {
        warnings.push_back("HFE track LUT offset is zero");
        return nullptr;
    }

    const size_t lutStart = static_cast<size_t>(lutOffset) * BLOCK_SIZE;
    const size_t lutSize = static_cast<size_t>(tracks) * 4;
    if (lutStart + lutSize > len)
    {
        warnings.push_back("HFE track LUT runs past the end of the file");
        return nullptr;
    }

    DiskImage::Encoding diskEncoding = DiskImage::Encoding::MFM;
    if (encoding == ENCODING_ISOIBM_FM || encoding == ENCODING_EMU_FM)
    {
        diskEncoding = DiskImage::Encoding::FM;
    }
    else if (encoding != ENCODING_ISOIBM_MFM && encoding != ENCODING_AMIGA_MFM && encoding != ENCODING_UNKNOWN)
    {
        warnings.push_back(StringHelper::Format("HFE encoding %d is not supported, treated as MFM", encoding));
    }
    /// endregion </Header>

    DiskImage* image = new DiskImage(tracks, sides);

    /// region <Tracks>
    const bool isV3 = (version == 3);
    const bool isFM = (diskEncoding == DiskImage::Encoding::FM);

    for (uint8_t cylinder = 0; cylinder < tracks; cylinder++)
    {
        const uint8_t* lutEntry = data + lutStart + cylinder * 4;
        const uint16_t trackOffset = readU16(lutEntry);
        const uint16_t trackLength = readU16(lutEntry + 2);

        const size_t trackStart = static_cast<size_t>(trackOffset) * BLOCK_SIZE;
        if (trackStart + trackLength > len)
        {
            warnings.push_back(StringHelper::Format("HFE track %d data runs past the end of the file", cylinder));
            delete image;
            return nullptr;
        }

        std::vector<uint8_t> side0Data, side1Data;
        deinterleave(data + trackStart, trackLength, side0Data, side1Data);

        for (uint8_t side = 0; side < sides; side++)
        {
            const std::vector<uint8_t>& sideData = (side == 0) ? side0Data : side1Data;
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);

            std::vector<uint8_t> bitCells;
            std::vector<std::pair<size_t, size_t>> weakRanges;

            if (isV3)
            {
                std::vector<uint8_t> reversedData(sideData.size());
                for (size_t i = 0; i < sideData.size(); i++)
                {
                    reversedData[i] = bitReverse(sideData[i]);
                }
                processV3Opcodes(reversedData, bitCells, weakRanges);
            }
            else
            {
                unpackBits(sideData.data(), sideData.size(), bitCells, true);
            }

            if (bitCells.empty())
            {
                track->resizeRaw(isFM ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM
                                      : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM,
                                 diskEncoding);
                track->markClean();
                continue;
            }

            if (isFM)
            {
                fdc::flux::FmDecoder decoder;
                decoder.decode(bitCells);
                track->setRaw(decoder.bytes().data(), decoder.bytes().size(), DiskImage::Encoding::FM);
                track->setClockBitmap(decoder.clockBitmap().data(), decoder.clockBitmap().size());
            }
            else
            {
                fdc::flux::MfmDecoder decoder;
                decoder.decode(bitCells);
                track->setRaw(decoder.bytes().data(), decoder.bytes().size(), DiskImage::Encoding::MFM);
                track->setClockBitmap(decoder.clockBitmap().data(), decoder.clockBitmap().size());
            }

            for (const auto& [start, count] : weakRanges)
            {
                const size_t byteStart = start / 16;
                const size_t byteEnd = (start + count + 15) / 16;
                for (size_t b = byteStart; b < byteEnd && b < track->rawSize(); b++)
                {
                    track->setWeakByte(b, true);
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

bool LoaderHFE::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("HFE save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();
    if (cylinders == 0 || cylinders > MAX_CYLINDERS || sides == 0 || sides > 2)
    {
        warnings.push_back("HFE save refused: invalid disk geometry");
        return false;
    }

    bool anyFM = false;
    for (uint8_t c = 0; c < cylinders && !anyFM; c++)
    {
        for (uint8_t s = 0; s < sides && !anyFM; s++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, s);
            if (track && track->encoding() == DiskImage::Encoding::FM) anyFM = true;
        }
    }

    /// region <Header>
    out.resize(HEADER_SIZE, 0);
    std::memcpy(out.data(), SIGNATURE_V3, SIGNATURE_LEN);
    out[0x08] = 0;
    out[0x09] = cylinders;
    out[0x0A] = sides;
    out[0x0B] = anyFM ? ENCODING_ISOIBM_FM : ENCODING_ISOIBM_MFM;
    out[0x0C] = static_cast<uint8_t>(_bitRate & 0xFF);
    out[0x0D] = static_cast<uint8_t>(_bitRate >> 8);
    out[0x0E] = static_cast<uint8_t>(_rpm & 0xFF);
    out[0x0F] = static_cast<uint8_t>(_rpm >> 8);
    out[0x10] = _interfaceMode;
    out[0x12] = 1;
    out[0x14] = _writeAllowed ? 1 : 0;
    out[0x15] = _singleStep ? 1 : 0;
    /// endregion </Header>

    /// region <LUT placeholder>
    const size_t lutStart = HEADER_SIZE;
    const size_t lutSize = static_cast<size_t>(cylinders) * 4;
    out.resize(lutStart + ((lutSize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE, 0);
    /// endregion </LUT placeholder>

    /// region <Tracks>
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        std::vector<uint8_t> side0Bits, side1Bits;

        for (uint8_t side = 0; side < sides; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("HFE save refused: track cylinder %d side %d is missing", cylinder, side));
                return false;
            }

            std::vector<uint8_t>& sideBits = (side == 0) ? side0Bits : side1Bits;
            const bool isFM = (track->encoding() == DiskImage::Encoding::FM);

            if (isFM)
            {
                fdc::flux::FmEncoder encoder;
                encoder.encode(track->rawData(), track->rawSize(), track->clockBitmap().data());
                sideBits = encoder.bitCells();
            }
            else
            {
                fdc::flux::MfmEncoder encoder;
                encoder.encode(track->rawData(), track->rawSize(), track->clockBitmap().data());
                sideBits = encoder.bitCells();
            }
        }

        if (sides == 1)
        {
            side1Bits.resize(side0Bits.size(), 0);
        }

        std::vector<uint8_t> side0Packed, side1Packed;
        packBits(side0Bits, side0Packed, false);
        packBits(side1Bits, side1Packed, false);

        for (auto& b : side0Packed) b = bitReverse(b);
        for (auto& b : side1Packed) b = bitReverse(b);

        std::vector<uint8_t> interleaved;
        interleave(side0Packed, side1Packed, interleaved);

        while (interleaved.size() % BLOCK_SIZE != 0)
        {
            interleaved.push_back(0);
        }

        const size_t trackOffset = out.size() / BLOCK_SIZE;
        const size_t trackLength = interleaved.size();

        uint8_t* lutEntry = out.data() + lutStart + cylinder * 4;
        lutEntry[0] = static_cast<uint8_t>(trackOffset & 0xFF);
        lutEntry[1] = static_cast<uint8_t>(trackOffset >> 8);
        lutEntry[2] = static_cast<uint8_t>(trackLength & 0xFF);
        lutEntry[3] = static_cast<uint8_t>(trackLength >> 8);

        out.insert(out.end(), interleaved.begin(), interleaved.end());
    }
    /// endregion </Tracks>

    return true;
}

bool LoaderHFE::serializeV1(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("HFE save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();
    if (cylinders == 0 || cylinders > MAX_CYLINDERS || sides == 0 || sides > 2)
    {
        warnings.push_back("HFE save refused: invalid disk geometry");
        return false;
    }

    bool anyFM = false;
    bool anyWeak = false;
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t s = 0; s < sides; s++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, s);
            if (track)
            {
                if (track->encoding() == DiskImage::Encoding::FM) anyFM = true;
                if (track->hasWeakBits()) anyWeak = true;
            }
        }
    }

    if (anyWeak)
    {
        warnings.push_back("HFE v1 cannot store weak bits: saved without them (use HFE v3 for weak bit support)");
    }

    /// region <Header>
    out.resize(HEADER_SIZE, 0);
    std::memcpy(out.data(), SIGNATURE_V1, SIGNATURE_LEN);
    out[0x08] = 0;
    out[0x09] = cylinders;
    out[0x0A] = sides;
    out[0x0B] = anyFM ? ENCODING_ISOIBM_FM : ENCODING_ISOIBM_MFM;
    out[0x0C] = static_cast<uint8_t>(_bitRate & 0xFF);
    out[0x0D] = static_cast<uint8_t>(_bitRate >> 8);
    out[0x0E] = static_cast<uint8_t>(_rpm & 0xFF);
    out[0x0F] = static_cast<uint8_t>(_rpm >> 8);
    out[0x10] = _interfaceMode;
    out[0x12] = 1;
    out[0x14] = _writeAllowed ? 1 : 0;
    out[0x15] = _singleStep ? 1 : 0;
    /// endregion </Header>

    /// region <LUT placeholder>
    const size_t lutStart = HEADER_SIZE;
    const size_t lutSize = static_cast<size_t>(cylinders) * 4;
    out.resize(lutStart + ((lutSize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE, 0);
    /// endregion </LUT placeholder>

    /// region <Tracks>
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        std::vector<uint8_t> side0Bits, side1Bits;

        for (uint8_t side = 0; side < sides; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("HFE save refused: track cylinder %d side %d is missing", cylinder, side));
                return false;
            }

            std::vector<uint8_t>& sideBits = (side == 0) ? side0Bits : side1Bits;
            const bool isFM = (track->encoding() == DiskImage::Encoding::FM);

            if (isFM)
            {
                fdc::flux::FmEncoder encoder;
                encoder.encode(track->rawData(), track->rawSize(), track->clockBitmap().data());
                sideBits = encoder.bitCells();
            }
            else
            {
                fdc::flux::MfmEncoder encoder;
                encoder.encode(track->rawData(), track->rawSize(), track->clockBitmap().data());
                sideBits = encoder.bitCells();
            }
        }

        if (sides == 1)
        {
            side1Bits.resize(side0Bits.size(), 0);
        }

        std::vector<uint8_t> side0Packed, side1Packed;
        packBits(side0Bits, side0Packed, true);
        packBits(side1Bits, side1Packed, true);

        std::vector<uint8_t> interleaved;
        interleave(side0Packed, side1Packed, interleaved);

        while (interleaved.size() % BLOCK_SIZE != 0)
        {
            interleaved.push_back(0);
        }

        const size_t trackOffset = out.size() / BLOCK_SIZE;
        const size_t trackLength = interleaved.size();

        uint8_t* lutEntry = out.data() + lutStart + cylinder * 4;
        lutEntry[0] = static_cast<uint8_t>(trackOffset & 0xFF);
        lutEntry[1] = static_cast<uint8_t>(trackOffset >> 8);
        lutEntry[2] = static_cast<uint8_t>(trackLength & 0xFF);
        lutEntry[3] = static_cast<uint8_t>(trackLength >> 8);

        out.insert(out.end(), interleaved.begin(), interleaved.end());
    }
    /// endregion </Tracks>

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderHFE::loadImage()
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

bool LoaderHFE::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderHFE::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("HFE save refused: no image or empty path");
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
        _warnings.push_back("HFE save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("HFE save failed: cannot write " + path);
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
