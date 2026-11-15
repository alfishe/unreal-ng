#include "unrealdisk/loaders/loader_hfe.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/detail/helpers.h"
// (emulator include dropped: emulator/emulator.h)
// (emulator include dropped: emulator/emulatorcontext.h)
#include "unrealdisk/flux/mfm_decoder.h"
#include "unrealdisk/flux/mfm_encoder.h"
// (emulator include dropped: emulator/notifications.h)

namespace unrealdisk
{

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

void LoaderHFE::deinterleave(const uint8_t* src, size_t available, size_t trackLength, std::vector<uint8_t>& side0,
                             std::vector<uint8_t>& side1)
{
    side0.clear();
    side1.clear();

    const size_t perSide = trackLength / 2;
    for (size_t block = 0; block * INTERLEAVE_SIZE < perSide; block++)
    {
        const size_t count = std::min(INTERLEAVE_SIZE, perSide - block * INTERLEAVE_SIZE);
        for (int side = 0; side < 2; side++)
        {
            const size_t start = block * BLOCK_SIZE + static_cast<size_t>(side) * INTERLEAVE_SIZE;
            if (start >= available) continue;
            const size_t n = std::min(count, available - start);
            std::vector<uint8_t>& out = side == 0 ? side0 : side1;
            out.insert(out.end(), src + start, src + start + n);
        }
    }
}

void LoaderHFE::interleave(const std::vector<uint8_t>& side0, const std::vector<uint8_t>& side1, uint8_t pad,
                           std::vector<uint8_t>& out)
{
    out.clear();

    const size_t maxLen = std::max(side0.size(), side1.size());
    const size_t blocks = (maxLen + INTERLEAVE_SIZE - 1) / INTERLEAVE_SIZE;
    out.reserve(blocks * BLOCK_SIZE);

    for (size_t b = 0; b < blocks; b++)
    {
        for (const std::vector<uint8_t>* side : {&side0, &side1})
        {
            for (size_t i = 0; i < INTERLEAVE_SIZE; i++)
            {
                const size_t at = b * INTERLEAVE_SIZE + i;
                out.push_back(at < side->size() ? (*side)[at] : pad);
            }
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
        const uint8_t b = data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            bits.push_back(static_cast<uint8_t>((b >> (lsbFirst ? bit : 7 - bit)) & 1));
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
                b |= static_cast<uint8_t>(lsbFirst ? (1 << bit) : (1 << (7 - bit)));
            }
        }
        out.push_back(b);
    }
}

void LoaderHFE::processV3Opcodes(const std::vector<uint8_t>& raw, std::vector<uint8_t>& bits,
                                 std::vector<uint8_t>& weakCells)
{
    bits.clear();
    weakCells.clear();

    auto pushCells = [&](uint8_t value, int count, bool weak) {
        for (int bit = count - 1; bit >= 0; bit--)
        {
            bits.push_back(static_cast<uint8_t>((value >> bit) & 1));
            weakCells.push_back(weak ? 1 : 0);
        }
    };

    size_t i = 0;
    while (i < raw.size())
    {
        const uint8_t b = raw[i++];
        switch (b)
        {
            case OP_NOP:
            case OP_SETINDEX:
                break;

            case OP_SETBITRATE:
                i++;  // the rate byte: one cell length for the whole track here
                break;

            case OP_SKIPBITS:
            {
                if (i + 2 > raw.size())
                {
                    i = raw.size();
                    break;
                }
                const int skip = raw[i] & 7;
                const uint8_t value = raw[i + 1];
                i += 2;
                if (value == OP_RAND)
                    pushCells(0, 8 - skip, true);
                else
                    pushCells(value, 8 - skip, false);
                break;
            }

            case OP_RAND:
                pushCells(0, 8, true);
                break;

            default:
                pushCells(b, 8, false);  // F5..FF never stand for cells in a valid stream; taken as data
                break;
        }
    }
}

void LoaderHFE::encodeSide(DiskImage::Track& track, std::vector<uint8_t>& cells, std::vector<uint8_t>& weakCells)
{
    const bool isFM = track.encoding() == DiskImage::Encoding::FM;
    std::vector<uint8_t> encoded;
    if (isFM)
    {
        fdc::flux::FmEncoder encoder;
        encoder.encode(track.rawData(), track.rawSize(), track.clockBitmap().data());
        encoded = encoder.bitCells();
    }
    else
    {
        fdc::flux::MfmEncoder encoder;
        encoder.encode(track.rawData(), track.rawSize(), track.clockBitmap().data());
        encoded = encoder.bitCells();
    }

    // 16 cells per byte in either encoding (an MFM A1 sync too)
    const size_t factor = isFM ? 2 : 1;
    cells.assign(encoded.size() * factor, 0);
    weakCells.assign(cells.size(), 0);
    for (size_t i = 0; i < encoded.size(); i++)
    {
        cells[i * factor + factor - 1] = encoded[i];  // FM cell c -> "0c"
    }
    if (track.hasWeakBits())
    {
        for (size_t b = 0; b < track.rawSize(); b++)
        {
            if (!track.weakByte(b)) continue;
            const size_t from = b * 16 * factor;
            const size_t to = std::min(cells.size(), from + 16 * factor);
            for (size_t c = from; c < to; c++) weakCells[c] = 1;
        }
    }
}

void LoaderHFE::emitV3(const std::vector<uint8_t>& cells, const std::vector<uint8_t>& weakCells,
                       std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(cells.size() / 8 + 16);
    out.push_back(OP_SETBITRATE);
    out.push_back(WRITE_RATE_OPCODE_VALUE);

    size_t pos = 0;
    while (pos < cells.size())
    {
        size_t n = std::min<size_t>(8, cells.size() - pos);
        // A group is weak or not as a whole: cut it where the weak run starts or ends
        const bool weak = weakCells[pos] != 0;
        for (size_t k = 1; k < n; k++)
        {
            if ((weakCells[pos + k] != 0) != weak)
            {
                n = k;
                break;
            }
        }

        if (weak)
        {
            if (n < 8)
            {
                out.push_back(OP_SKIPBITS);
                out.push_back(static_cast<uint8_t>(8 - n));
            }
            out.push_back(OP_RAND);
            pos += n;
            continue;
        }

        uint8_t value = 0;
        for (size_t k = 0; k < n; k++) value = static_cast<uint8_t>((value << 1) | (cells[pos + k] & 1));
        if (n == 8 && (value & 0xF0) == 0xF0)
        {
            n = 7;  // would read as an opcode: send seven cells
            value = static_cast<uint8_t>(value >> 1);
        }
        if (n < 8)
        {
            out.push_back(OP_SKIPBITS);
            out.push_back(static_cast<uint8_t>(8 - n));
        }
        out.push_back(value);
        pos += n;
    }
}

void LoaderHFE::decodeSide(const std::vector<uint8_t>& cells, const std::vector<uint8_t>& weakCells, bool headerFm,
                           bool singleRateFm, DiskImage::Track& track)
{
    if (cells.empty())
    {
        track.resizeRaw(headerFm ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM
                                 : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM,
                        headerFm ? DiskImage::Encoding::FM : DiskImage::Encoding::MFM);
        return;
    }

    struct Candidate
    {
        std::vector<uint8_t> bytes, clock, weak;
        DiskImage::Encoding encoding = DiskImage::Encoding::MFM;
    };
    auto decodeMfm = [&]() {
        Candidate c;
        fdc::flux::MfmDecoder decoder;
        decoder.decode(cells.data(), cells.size(), weakCells.data());
        c.bytes = decoder.bytes();
        c.clock = decoder.clockBitmap();
        c.weak = decoder.weakBytes();
        return c;
    };
    // FM: every cell (single rate), or every second one of the doubled stream at phase 0 or 1
    auto decodeFm = [&](size_t step, size_t phase) {
        std::vector<uint8_t> fmCells, fmWeak;
        fmCells.reserve(cells.size() / step + 1);
        fmWeak.reserve(cells.size() / step + 1);
        for (size_t i = phase; i < cells.size(); i += step)
        {
            fmCells.push_back(cells[i]);
            bool weak = weakCells[i] != 0;
            if (step == 2 && i >= 1) weak = weak || weakCells[i - 1] != 0;
            fmWeak.push_back(weak ? 1 : 0);
        }
        Candidate c;
        c.encoding = DiskImage::Encoding::FM;
        fdc::flux::FmDecoder decoder;
        decoder.decode(fmCells.data(), fmCells.size(), fmWeak.data());
        c.bytes = decoder.bytes();
        c.clock = decoder.clockBitmap();
        c.weak = decoder.weakBytes();
        return c;
    };
    auto apply = [&](const Candidate& c) {
        track.setRaw(c.bytes.data(), c.bytes.size(), c.encoding);
        track.setClockBitmap(c.clock.data(), c.clock.size());
        for (size_t b = 0; b < c.weak.size() && b < track.rawSize(); b++)
        {
            if (c.weak[b]) track.setWeakByte(b, true);
        }
        track.reindex();
        return track.sectorCount();
    };

    std::vector<Candidate> candidates;
    if (singleRateFm)
    {
        candidates.push_back(decodeFm(1, 0));
        candidates.push_back(decodeMfm());
    }
    else if (headerFm)
    {
        candidates.push_back(decodeFm(2, 1));
        candidates.push_back(decodeFm(2, 0));
        candidates.push_back(decodeMfm());
    }
    else
    {
        candidates.push_back(decodeMfm());
        candidates.push_back(decodeFm(2, 1));
        candidates.push_back(decodeFm(2, 0));
    }

    size_t best = 0;
    size_t bestSectors = 0;
    for (size_t i = 0; i < candidates.size(); i++)
    {
        const size_t sectors = apply(candidates[i]);
        if (i == 0 || sectors > bestSectors)
        {
            best = i;
            bestSectors = sectors;
        }
    }
    if (best != candidates.size() - 1) apply(candidates[best]);
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

    bool headerFm = false;
    if (encoding == ENCODING_ISOIBM_FM || encoding == ENCODING_EMU_FM)
    {
        headerFm = true;
    }
    else if (encoding != ENCODING_ISOIBM_MFM && encoding != ENCODING_AMIGA_MFM && encoding != ENCODING_UNKNOWN)
    {
        warnings.push_back(StringHelper::Format("HFE encoding %d is not supported, treated as MFM", encoding));
    }
    // FM recorded at its own cell rate (125 kbit/s and below) rather than doubled at 250
    const bool singleRateFm = headerFm && _bitRate != 0 && _bitRate <= 150;
    /// endregion </Header>

    DiskImage* image = new DiskImage(tracks, sides);

    /// region <Tracks>
    const bool isV3 = (version == 3);

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
        deinterleave(data + trackStart, len - trackStart, trackLength, side0Data, side1Data);

        for (uint8_t side = 0; side < sides; side++)
        {
            const std::vector<uint8_t>& sideData = (side == 0) ? side0Data : side1Data;
            DiskImage::Track* track = image->getTrackForCylinderAndSide(cylinder, side);

            std::vector<uint8_t> bitCells;
            std::vector<uint8_t> weakCells;

            if (isV3)
            {
                std::vector<uint8_t> reversedData(sideData.size());
                for (size_t i = 0; i < sideData.size(); i++)
                {
                    reversedData[i] = bitReverse(sideData[i]);
                }
                processV3Opcodes(reversedData, bitCells, weakCells);
            }
            else
            {
                unpackBits(sideData.data(), sideData.size(), bitCells, true);
                weakCells.assign(bitCells.size(), 0);
            }

            decodeSide(bitCells, weakCells, headerFm, singleRateFm, *track);
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
    return serializeVersion(diskImage, out, warnings, 3);
}

bool LoaderHFE::serializeV1(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    return serializeVersion(diskImage, out, warnings, 1);
}

bool LoaderHFE::serializeVersion(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings,
                                 int version) const
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

    bool allFM = true;
    bool anyWeak = false;
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t s = 0; s < sides; s++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, s);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("HFE save refused: track cylinder %d side %d is missing", c, s));
                return false;
            }
            allFM = allFM && track->encoding() == DiskImage::Encoding::FM;
            anyWeak = anyWeak || track->hasWeakBits();
        }
    }
    if (version == 1 && anyWeak)
    {
        warnings.push_back("HFE v1 cannot store weak bits: saved without them (use HFE v3 for weak bit support)");
    }

    /// region <Header>
    // One encoding byte for the disk, one cell rate: every track goes at 250 kbit/s (FM doubled), so a mixed disk
    // keeps its FM tracks; the byte only says FM when all tracks are
    out.resize(HEADER_SIZE, 0xFF);
    std::memcpy(out.data(), version == 3 ? SIGNATURE_V3 : SIGNATURE_V1, SIGNATURE_LEN);
    out[0x08] = 0;
    out[0x09] = cylinders;
    out[0x0A] = sides;
    out[0x0B] = allFM ? ENCODING_ISOIBM_FM : ENCODING_ISOIBM_MFM;
    out[0x0C] = static_cast<uint8_t>(WRITE_BIT_RATE & 0xFF);
    out[0x0D] = static_cast<uint8_t>(WRITE_BIT_RATE >> 8);
    out[0x0E] = static_cast<uint8_t>(_rpm & 0xFF);
    out[0x0F] = static_cast<uint8_t>(_rpm >> 8);
    out[0x10] = _interfaceMode;
    out[0x11] = 1;
    out[0x12] = 1;
    out[0x13] = 0;
    out[0x14] = _writeAllowed ? 0xFF : 0;
    out[0x15] = _singleStep ? 0xFF : 0;
    /// endregion </Header>

    /// region <LUT placeholder>
    const size_t lutStart = HEADER_SIZE;
    const size_t lutSize = static_cast<size_t>(cylinders) * 4;
    out.resize(lutStart + ((lutSize + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE, 0xFF);
    /// endregion </LUT placeholder>

    /// region <Tracks>
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        std::vector<uint8_t> sideBytes[2];

        for (uint8_t side = 0; side < sides; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            std::vector<uint8_t> cells, weakCells;
            encodeSide(*track, cells, weakCells);

            if (version == 3)
            {
                std::vector<uint8_t> stream;
                emitV3(cells, weakCells, stream);
                for (uint8_t& b : stream) b = bitReverse(b);  // stored first cell in bit 0, as v1
                sideBytes[side] = std::move(stream);
            }
            else
            {
                packBits(cells, sideBytes[side], true);
            }
        }

        const size_t longest = std::max(sideBytes[0].size(), sideBytes[1].size());
        if (longest * 2 > 0xFFFF)
        {
            warnings.push_back(StringHelper::Format("HFE save refused: cylinder %d needs %zu bytes, the LUT holds 65535",
                                                    cylinder, longest * 2));
            return false;
        }

        // Padding past the LUT length: NOP in v3 (bit-reversed), anything in v1
        std::vector<uint8_t> interleaved;
        interleave(sideBytes[0], sideBytes[1], version == 3 ? bitReverse(OP_NOP) : 0, interleaved);

        const size_t trackOffset = out.size() / BLOCK_SIZE;
        const size_t trackLength = longest * 2;

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


    _diskImage->setFilePath(path);
    return true;
}

/// endregion </Basic methods>
}  // namespace unrealdisk
