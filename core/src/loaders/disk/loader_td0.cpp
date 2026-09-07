#include "loader_td0.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/notifications.h"

namespace
{
    inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    inline void putU16(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }

    // Datasheet minimum gap 3: 16 bytes MFM, 10 bytes FM; the TR-DOS layout uses 60
    constexpr uint8_t MIN_GAP_POST_DATA_MFM = 16;
    constexpr uint8_t MIN_GAP_POST_DATA_FM = 10;

    /// region <LZHUF decoder>

    /// LZSS (4 KB ring buffer, matches of 3..60 bytes) + adaptive Huffman coding of 314 symbols
    /// (256 literals + 58 match lengths), match positions coded as a fixed 3..8-bit prefix code for the upper
    /// 6 bits plus 6 raw bits. This is the scheme Teledisk uses for "td" images; Teledisk streams carry no
    /// length prefix, so decoding runs until the input is exhausted.
    class LzhufDecoder
    {
    public:
        static constexpr int N = 4096;
        static constexpr int F = 60;
        static constexpr int THRESHOLD = 2;
        static constexpr int N_CHAR = 256 - THRESHOLD + F;   // 314
        static constexpr int T = N_CHAR * 2 - 1;             // 627 tree nodes
        static constexpr int R = T - 1;                      // root
        static constexpr int MAX_FREQ = 0x8000;

    private:
        const uint8_t* _in;
        size_t _len;
        size_t _pos = 0;
        uint32_t _bitBuf = 0;
        int _bitCount = 0;

        uint16_t _freq[T + 1];
        uint16_t _prnt[T + N_CHAR];
        uint16_t _son[T];
        uint8_t _text[N];

        uint8_t _dCode[256];
        uint8_t _dLen[256];

    public:
        LzhufDecoder(const uint8_t* in, size_t len) : _in(in), _len(len)
        {
            buildPositionTables();
            startHuff();
            std::memset(_text, 0x20, N - F);
            std::memset(_text + (N - F), 0, F);
        }

        bool decode(std::vector<uint8_t>& out, size_t maxOut)
        {
            int r = N - F;

            while (hasInput() && out.size() < maxOut)
            {
                int c = decodeChar();
                if (c < 256)
                {
                    _text[r] = static_cast<uint8_t>(c);
                    r = (r + 1) & (N - 1);
                    out.push_back(static_cast<uint8_t>(c));
                }
                else
                {
                    const int i = (r - decodePosition() - 1) & (N - 1);
                    const int j = c - 255 + THRESHOLD;
                    for (int k = 0; k < j && out.size() < maxOut; k++)
                    {
                        const uint8_t b = _text[(i + k) & (N - 1)];
                        _text[r] = b;
                        r = (r + 1) & (N - 1);
                        out.push_back(b);
                    }
                }
            }

            return true;
        }

    private:
        bool hasInput() const { return _pos < _len || _bitCount > 0; }

        int getBit()
        {
            if (_bitCount == 0)
            {
                _bitBuf = (_pos < _len) ? _in[_pos] : 0;   // Past the end: zero bits (as the original getc() -> 0)
                _pos++;
                _bitCount = 8;
            }
            _bitCount--;
            return (_bitBuf >> _bitCount) & 1;
        }

        int getByte()
        {
            int v = 0;
            for (int i = 0; i < 8; i++) v = (v << 1) | getBit();
            return v;
        }

        /// Canonical prefix code with 1/3/8/12/24/16 codes of lengths 3..8 for the upper 6 bits of a position
        void buildPositionTables()
        {
            static const uint8_t counts[6] = {1, 3, 8, 12, 24, 16};
            int index = 0;
            int code = 0;
            int prevLen = 3;
            for (int li = 0; li < 6; li++)
            {
                const int length = 3 + li;
                for (int n = 0; n < counts[li]; n++, index++)
                {
                    if (index > 0) code = (code + 1) << (length - prevLen);
                    prevLen = length;
                    const int first = code << (8 - length);
                    const int span = 1 << (8 - length);
                    for (int b = first; b < first + span; b++)
                    {
                        _dCode[b] = static_cast<uint8_t>(index);
                        _dLen[b] = static_cast<uint8_t>(length);
                    }
                }
            }
        }

        void startHuff()
        {
            for (int i = 0; i < N_CHAR; i++)
            {
                _freq[i] = 1;
                _son[i] = static_cast<uint16_t>(i + T);
                _prnt[i + T] = static_cast<uint16_t>(i);
            }
            int i = 0;
            int j = N_CHAR;
            while (j <= R)
            {
                _freq[j] = static_cast<uint16_t>(_freq[i] + _freq[i + 1]);
                _son[j] = static_cast<uint16_t>(i);
                _prnt[i] = _prnt[i + 1] = static_cast<uint16_t>(j);
                i += 2;
                j++;
            }
            _freq[T] = 0xFFFF;
            _prnt[R] = 0;
        }

        /// Halve all leaf frequencies and rebuild the tree (keeps the sibling property)
        void reconstruct()
        {
            int j = 0;
            for (int i = 0; i < T; i++)
            {
                if (_son[i] >= T)
                {
                    _freq[j] = static_cast<uint16_t>((_freq[i] + 1) / 2);
                    _son[j] = _son[i];
                    j++;
                }
            }

            int i = 0;
            j = N_CHAR;
            while (j < T)
            {
                int k = i + 1;
                const uint16_t f = static_cast<uint16_t>(_freq[i] + _freq[k]);
                _freq[j] = f;
                k = j - 1;
                while (f < _freq[k]) k--;
                k++;
                std::memmove(&_freq[k + 1], &_freq[k], static_cast<size_t>(j - k) * sizeof(_freq[0]));
                _freq[k] = f;
                std::memmove(&_son[k + 1], &_son[k], static_cast<size_t>(j - k) * sizeof(_son[0]));
                _son[k] = static_cast<uint16_t>(i);
                i += 2;
                j++;
            }

            for (int n = 0; n < T; n++)
            {
                const int k = _son[n];
                _prnt[k] = static_cast<uint16_t>(n);
                if (k < T) _prnt[k + 1] = static_cast<uint16_t>(n);
            }
        }

        /// Increment the frequency of symbol c and restore the sibling property by swapping nodes
        void update(int c)
        {
            if (_freq[R] == MAX_FREQ) reconstruct();

            c = _prnt[c + T];
            do
            {
                const uint16_t k = ++_freq[c];
                int l = c + 1;
                if (k > _freq[l])
                {
                    while (k > _freq[++l]) {}
                    l--;
                    _freq[c] = _freq[l];
                    _freq[l] = k;

                    const int i = _son[c];
                    _prnt[i] = static_cast<uint16_t>(l);
                    if (i < T) _prnt[i + 1] = static_cast<uint16_t>(l);

                    const int j = _son[l];
                    _son[l] = static_cast<uint16_t>(i);
                    _prnt[j] = static_cast<uint16_t>(c);
                    if (j < T) _prnt[j + 1] = static_cast<uint16_t>(c);
                    _son[c] = static_cast<uint16_t>(j);

                    c = l;
                }
            } while ((c = _prnt[c]) != 0);
        }

        int decodeChar()
        {
            int c = _son[R];
            while (c < T)
            {
                c = _son[c + getBit()];
            }
            c -= T;
            update(c);
            return c;
        }

        int decodePosition()
        {
            int i = getByte();
            const int c = static_cast<int>(_dCode[i]) << 6;
            int j = _dLen[i] - 2;   // 8 bits read: prefix (dLen) + (8 - dLen) of the 6 low bits; dLen - 2 remain
            while (j-- > 0)
            {
                i = (i << 1) | getBit();
            }
            return c | (i & 0x3F);
        }
    };

    /// endregion </LZHUF decoder>
}

/// region <Static helpers>

bool LoaderTD0::detect(const uint8_t* data, size_t len)
{
    return data && len >= 2 && ((data[0] == 'T' && data[1] == 'D') || (data[0] == 't' && data[1] == 'd'));
}

uint16_t LoaderTD0::crc16(const uint8_t* data, size_t len)
{
    // CRCHelper::crc16 (fdc.h) implements the same polynomial with a byte-swapped table; it takes a non-const
    // pointer and a 16-bit length, so the plain bitwise form is used here for arbitrary block sizes
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= static_cast<uint16_t>(data[i] << 8);
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0xA097) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool LoaderTD0::decodeDataBlock(uint8_t encoding, const uint8_t* payload, size_t payloadLen, size_t sectorSize,
                                std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(sectorSize);
    bool ok = true;

    switch (encoding)
    {
        case ENCODING_RAW:
            out.assign(payload, payload + std::min(payloadLen, sectorSize));
            ok = payloadLen >= sectorSize;
            break;

        case ENCODING_PATTERN:
        {
            if (payloadLen < 4)
            {
                ok = false;
                break;
            }
            const size_t count = readU16(payload);
            for (size_t i = 0; i < count && out.size() < sectorSize; i++)
            {
                out.push_back(payload[2]);
                if (out.size() < sectorSize) out.push_back(payload[3]);
            }
            break;
        }

        case ENCODING_RLE:
        {
            size_t p = 0;
            while (p + 2 <= payloadLen && out.size() < sectorSize)
            {
                const uint8_t lengthCode = payload[p];
                const size_t count = payload[p + 1];
                p += 2;
                if (lengthCode == 0)
                {
                    if (p + count > payloadLen)
                    {
                        ok = false;
                        break;
                    }
                    const size_t take = std::min(count, sectorSize - out.size());
                    out.insert(out.end(), payload + p, payload + p + take);
                    p += count;
                }
                else
                {
                    const size_t patternLen = static_cast<size_t>(lengthCode) * 2;
                    if (p + patternLen > payloadLen)
                    {
                        ok = false;
                        break;
                    }
                    for (size_t i = 0; i < count && out.size() < sectorSize; i++)
                    {
                        const size_t take = std::min(patternLen, sectorSize - out.size());
                        out.insert(out.end(), payload + p, payload + p + take);
                    }
                    p += patternLen;
                }
            }
            break;
        }

        default:
            ok = false;
            break;
    }

    if (out.size() < sectorSize)
    {
        ok = false;
        out.resize(sectorSize, 0);
    }
    return ok;
}

bool LoaderTD0::decompressLzhuf(const uint8_t* in, size_t len, std::vector<uint8_t>& out, size_t maxOut)
{
    out.clear();
    if (!in && len > 0) return false;

    LzhufDecoder decoder(in, len);
    return decoder.decode(out, maxOut);
}

bool LoaderTD0::buildTrackSpec(const std::vector<uint8_t>& numbers, const std::vector<uint8_t>& sizeCodes,
                               const std::vector<uint8_t>& cylinders, const std::vector<uint8_t>& heads,
                               const std::vector<uint8_t>& idOnly, bool fm, size_t trackLength,
                               DiskImage::TrackFormatSpec& spec, std::string* note)
{
    spec = fm ? DiskImage::TrackFormatSpec::ibm3740() : DiskImage::TrackFormatSpec::trdos(nullptr, 0);
    spec.trackLength = trackLength;
    spec.sectorNumbers = numbers;
    spec.sectorSizeCodes = sizeCodes;
    spec.sectorCylinders = cylinders;
    spec.sectorHeads = heads;
    spec.idOnly = idOnly;

    if (spec.fits()) return true;

    const uint8_t minGap = fm ? MIN_GAP_POST_DATA_FM : MIN_GAP_POST_DATA_MFM;

    // Shrink the post-data gap first (keeps the nominal revolution length)
    for (uint8_t gap = spec.gapPostData; gap >= minGap; gap--)
    {
        spec.gapPostData = gap;
        if (spec.fits())
        {
            if (note) *note = StringHelper::Format("gap3 reduced to %d bytes to fit %zu sectors", gap, numbers.size());
            return true;
        }
    }

    // Then grow the track up to the model maximum
    spec.gapPostData = minGap;
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

bool LoaderTD0::parseBody(const uint8_t* data, size_t len, bool headerFm, std::vector<TrackEntry>& tracks, std::vector<std::string>& warnings)
{
    size_t offset = 0;

    /// region <Comment block>
    _description.clear();
    _commentDate = CommentDate();
    _hasComment = false;

    if (_stepping & STEPPING_COMMENT)
    {
        if (offset + COMMENT_HEADER_SIZE > len)
        {
            warnings.push_back("TD0 file is truncated inside the comment block header");
            return false;
        }
        const uint16_t commentCrc = readU16(data + offset);
        const uint16_t commentLength = readU16(data + offset + 2);
        if (offset + COMMENT_HEADER_SIZE + commentLength > len)
        {
            warnings.push_back("TD0 file is truncated inside the comment text");
            return false;
        }
        const uint16_t computed = crc16(data + offset + 2, COMMENT_HEADER_SIZE - 2 + commentLength);
        if (computed != commentCrc)
        {
            warnings.push_back(StringHelper::Format("TD0 comment block CRC mismatch (stored %04X, computed %04X)", commentCrc, computed));
        }

        const uint8_t* d = data + offset + 4;
        _commentDate.year = static_cast<uint16_t>(1900 + d[0]);
        _commentDate.month = static_cast<uint8_t>(d[1] + 1);
        _commentDate.day = d[2];
        _commentDate.hour = d[3];
        _commentDate.minute = d[4];
        _commentDate.second = d[5];

        const uint8_t* text = data + offset + COMMENT_HEADER_SIZE;
        _description.reserve(commentLength);
        for (size_t i = 0; i < commentLength; i++)
        {
            _description.push_back(text[i] == 0 ? '\n' : static_cast<char>(text[i]));
        }
        while (!_description.empty() && _description.back() == '\n') _description.pop_back();
        _hasComment = true;

        offset += COMMENT_HEADER_SIZE + commentLength;
    }
    /// endregion </Comment block>

    /// region <Tracks>
    while (true)
    {
        if (offset >= len)
        {
            warnings.push_back("TD0 image has no end-of-image marker (0xFF track header)");
            break;
        }
        if (data[offset] == END_OF_IMAGE) break;

        if (offset + TRACK_HEADER_SIZE > len)
        {
            warnings.push_back("TD0 file is truncated inside a track header");
            return false;
        }

        TrackEntry track;
        const uint8_t sectorCount = data[offset];
        track.cylinder = data[offset + 1];
        track.head = static_cast<uint8_t>(data[offset + 2] & 0x7F);
        track.fm = headerFm || (data[offset + 2] & HEAD_FM) != 0;
        const uint8_t trackCrc = data[offset + 3];
        const uint8_t computedTrackCrc = static_cast<uint8_t>(crc16(data + offset, 3) & 0xFF);
        if (trackCrc != computedTrackCrc)
        {
            warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d: track header CRC mismatch (stored %02X, computed %02X)",
                                                    track.cylinder, track.head, trackCrc, computedTrackCrc));
        }
        offset += TRACK_HEADER_SIZE;

        for (uint8_t s = 0; s < sectorCount; s++)
        {
            if (offset + SECTOR_HEADER_SIZE > len)
            {
                warnings.push_back(StringHelper::Format("TD0 file is truncated inside the sector list of cylinder %d head %d", track.cylinder, track.head));
                return false;
            }

            SectorEntry sector;
            sector.cylinder = data[offset];
            sector.head = data[offset + 1];
            sector.number = data[offset + 2];
            sector.sizeCode = data[offset + 3];
            sector.flags = data[offset + 4];
            const uint8_t sectorCrc = data[offset + 5];
            offset += SECTOR_HEADER_SIZE;

            const bool hasDataBlock = !(sector.flags & FLAGS_NO_DATA_BLOCK);
            if (hasDataBlock)
            {
                if (offset + 2 > len)
                {
                    warnings.push_back(StringHelper::Format("TD0 file is truncated at the data block of cylinder %d head %d sector %d",
                                                            track.cylinder, track.head, sector.number));
                    return false;
                }
                const uint16_t blockLength = readU16(data + offset);
                offset += 2;
                if (blockLength == 0 || offset + blockLength > len)
                {
                    warnings.push_back(StringHelper::Format("TD0 file is truncated inside the data block of cylinder %d head %d sector %d",
                                                            track.cylinder, track.head, sector.number));
                    return false;
                }

                if (sector.sizeCode > 6)
                {
                    warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d sector %d: invalid size code %d, sector skipped",
                                                            track.cylinder, track.head, sector.number, sector.sizeCode));
                    offset += blockLength;
                    continue;
                }

                const size_t sectorSize = 128u << sector.sizeCode;
                const uint8_t encoding = data[offset];
                if (!decodeDataBlock(encoding, data + offset + 1, blockLength - 1, sectorSize, sector.data))
                {
                    warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d sector %d: malformed data block (encoding %d)",
                                                            track.cylinder, track.head, sector.number, encoding));
                }
                offset += blockLength;

                const uint8_t computedSectorCrc = static_cast<uint8_t>(crc16(sector.data.data(), sector.data.size()) & 0xFF);
                if (computedSectorCrc != sectorCrc)
                {
                    warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d sector %d: data CRC mismatch (stored %02X, computed %02X)",
                                                            track.cylinder, track.head, sector.number, sectorCrc, computedSectorCrc));
                }
            }

            if (sector.flags & FLAG_NO_ID)
            {
                warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d: sector %d has data but no ID field, skipped",
                                                        track.cylinder, track.head, sector.number));
                continue;
            }

            track.sectors.push_back(std::move(sector));
        }

        tracks.push_back(std::move(track));
    }
    /// endregion </Tracks>

    return true;
}

DiskImage* LoaderTD0::buildImage(const std::vector<TrackEntry>& tracks, uint8_t sides, size_t trackLengthMfm, size_t trackLengthFm,
                                 std::vector<std::string>& warnings)
{
    uint8_t cylinders = 1;
    for (const TrackEntry& t : tracks)
    {
        if (t.head > 1)
        {
            warnings.push_back(StringHelper::Format("TD0 cylinder %d: head %d is outside the supported range 0..1", t.cylinder, t.head));
            return nullptr;
        }
        if (t.cylinder >= MAX_CYLINDERS)
        {
            warnings.push_back(StringHelper::Format("TD0 cylinder %d exceeds the supported maximum of %d cylinders", t.cylinder, MAX_CYLINDERS));
            return nullptr;
        }
        cylinders = std::max<uint8_t>(cylinders, static_cast<uint8_t>(t.cylinder + 1));
        if (t.head == 1 && sides < 2)
        {
            warnings.push_back("TD0 header declares a single-sided disk but head 1 tracks are present; loaded as double-sided");
            sides = 2;
        }
    }

    DiskImage* image = new DiskImage(cylinders, sides);

    // Tracks not present in the file stay unformatted
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t h = 0; h < sides; h++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(c, h);
            track->resizeRaw(DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM);
            track->markClean();
        }
    }

    for (const TrackEntry& t : tracks)
    {
        DiskImage::Track* track = image->getTrackForCylinderAndSide(t.cylinder, t.head);
        if (!track) continue;

        if (t.sectors.empty())
        {
            track->resizeRaw(t.fm ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM,
                             t.fm ? DiskImage::Encoding::FM : DiskImage::Encoding::MFM, t.fm ? 0xFF : 0x4E);
            track->markClean();
            continue;
        }

        const size_t count = t.sectors.size();
        std::vector<uint8_t> numbers(count), sizeCodes(count), cyls(count), heads(count), idOnly(count, 0);
        for (size_t s = 0; s < count; s++)
        {
            const SectorEntry& e = t.sectors[s];
            numbers[s] = e.number;
            sizeCodes[s] = static_cast<uint8_t>(e.sizeCode & 3);
            cyls[s] = e.cylinder;
            heads[s] = e.head;
            idOnly[s] = (e.flags & FLAGS_NO_DATA_BLOCK) ? 1 : 0;
            if (e.sizeCode > 3)
            {
                warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d sector %d: size code %d masked to %d (WD1793 uses 2 bits)",
                                                        t.cylinder, t.head, e.number, e.sizeCode, e.sizeCode & 3));
            }
        }

        DiskImage::TrackFormatSpec spec;
        std::string note;
        if (!buildTrackSpec(numbers, sizeCodes, cyls, heads, idOnly, t.fm, t.fm ? trackLengthFm : trackLengthMfm, spec, &note))
        {
            warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d: %s", t.cylinder, t.head, note.c_str()));
            delete image;
            return nullptr;
        }
        if (!note.empty())
        {
            warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d: %s", t.cylinder, t.head, note.c_str()));
        }

        track->formatTrack(t.cylinder, t.head, spec);

        for (size_t s = 0; s < count; s++)
        {
            const SectorEntry& e = t.sectors[s];
            DiskImage::Sector* sector = track->getRawSector(s);
            if (!sector || !sector->hasData) continue;

            const size_t copy = std::min<size_t>(sector->dataSize, e.data.size());
            std::memcpy(sector->data, e.data.data(), copy);

            if (e.flags & FLAG_DELETED)
            {
                sector->setDataAddressMark(0xF8);
            }
            sector->recalculateDataCRC();

            if (e.flags & FLAG_DATA_CRC_ERROR)
            {
                sector->setDataCRC(static_cast<uint16_t>(sector->dataCRC() ^ 0xFFFF));
            }
        }

        track->reindex();
        track->markClean();
    }

    image->markClean();
    return image;
}

DiskImage* LoaderTD0::parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings)
{
    if (!detect(data, len))
    {
        warnings.push_back("Not a TD0 file: signature 'TD' / 'td' missing");
        return nullptr;
    }
    if (len < HEADER_SIZE)
    {
        warnings.push_back("TD0 file is truncated (shorter than the header)");
        return nullptr;
    }

    /// region <Header>
    const uint16_t storedCrc = readU16(data + 10);
    const uint16_t headerCrc = crc16(data, 10);
    if (storedCrc != headerCrc)
    {
        warnings.push_back(StringHelper::Format("TD0 header CRC mismatch (stored %04X, computed %04X)", storedCrc, headerCrc));
        return nullptr;
    }

    _advanced = data[0] == 't';
    _version = data[4];
    _dataRate = data[5];
    _driveType = data[6];
    _stepping = data[7];
    _dosAllocation = data[8];
    const uint8_t sidesByte = data[9];
    const uint8_t sides = (sidesByte == 1) ? 1 : 2;

    const bool headerFm = (_dataRate & RATE_FM) != 0;
    size_t trackLengthMfm = DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM;
    size_t trackLengthFm = DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM;
    switch (_dataRate & RATE_MASK)
    {
        case 1:
            warnings.push_back("TD0 data rate 300 kbps is outside the WD1793 / Beta Disk envelope; track length scaled to 7500 bytes");
            trackLengthMfm = trackLengthMfm * 300 / 250;
            trackLengthFm = trackLengthFm * 300 / 250;
            break;
        case 2:
            warnings.push_back("TD0 data rate 500 kbps is outside the WD1793 / Beta Disk envelope; track length scaled to 12500 bytes");
            trackLengthMfm = trackLengthMfm * 2;
            trackLengthFm = trackLengthFm * 2;
            break;
        case 3:
            warnings.push_back("TD0 data rate code 3 is reserved; treated as 250 kbps");
            break;
        default:
            break;
    }
    trackLengthMfm = std::min(trackLengthMfm, DiskImage::RawTrack::MAX_TRACK_SIZE);
    trackLengthFm = std::min(trackLengthFm, DiskImage::RawTrack::MAX_TRACK_SIZE);
    if (_dosAllocation)
    {
        warnings.push_back("TD0 image was saved with DOS allocation: unallocated sectors carry no data (stored as ID-only)");
    }
    /// endregion </Header>

    /// region <Body>
    const uint8_t* body = data + HEADER_SIZE;
    size_t bodyLen = len - HEADER_SIZE;
    std::vector<uint8_t> decompressed;
    if (_advanced)
    {
        if (!decompressLzhuf(body, bodyLen, decompressed))
        {
            warnings.push_back("TD0 advanced compression stream could not be decoded");
            return nullptr;
        }
        body = decompressed.data();
        bodyLen = decompressed.size();
    }

    std::vector<TrackEntry> tracks;
    if (!parseBody(body, bodyLen, headerFm, tracks, warnings))
    {
        return nullptr;
    }
    /// endregion </Body>

    return buildImage(tracks, sides, trackLengthMfm, trackLengthFm, warnings);
}

/// endregion </Parsing>

/// region <Serialisation>

bool LoaderTD0::serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const
{
    out.clear();

    if (!diskImage)
    {
        warnings.push_back("TD0 save refused: disk image is not set");
        return false;
    }

    const uint8_t cylinders = diskImage->getCylinders();
    const uint8_t sides = diskImage->getSides();

    bool allFm = true;
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t h = 0; h < sides; h++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, h);
            if (!track)
            {
                warnings.push_back(StringHelper::Format("TD0 save refused: track cylinder %d head %d is missing", c, h));
                return false;
            }
            if (track->encoding() != DiskImage::Encoding::FM) allFm = false;
        }
    }

    /// region <Header>
    out.push_back('T');
    out.push_back('D');
    out.push_back(0);   // sequence
    out.push_back(0);   // check sequence
    out.push_back(_version);
    out.push_back(static_cast<uint8_t>((_dataRate & RATE_MASK) | (allFm ? RATE_FM : 0)));
    out.push_back(_driveType);
    out.push_back(static_cast<uint8_t>((_stepping & 0x7F) | (_description.empty() ? 0 : STEPPING_COMMENT)));
    out.push_back(0);   // DOS allocation: every sector is stored
    out.push_back(sides == 1 ? 1 : 2);
    putU16(out, crc16(out.data(), 10));
    /// endregion </Header>

    /// region <Comment block>
    if (!_description.empty())
    {
        std::vector<uint8_t> block;
        putU16(block, static_cast<uint16_t>(std::min<size_t>(_description.size(), 0xFFFF)));
        block.push_back(static_cast<uint8_t>(_commentDate.year >= 1900 ? _commentDate.year - 1900 : 0));
        block.push_back(static_cast<uint8_t>(_commentDate.month > 0 ? _commentDate.month - 1 : 0));
        block.push_back(_commentDate.day);
        block.push_back(_commentDate.hour);
        block.push_back(_commentDate.minute);
        block.push_back(_commentDate.second);
        const size_t textLen = std::min<size_t>(_description.size(), 0xFFFF);
        for (size_t i = 0; i < textLen; i++)
        {
            const char ch = _description[i];
            block.push_back(ch == '\n' ? 0 : static_cast<uint8_t>(ch));
        }
        if (_description.size() > 0xFFFF)
        {
            warnings.push_back("TD0 comment truncated to 65535 bytes");
        }
        putU16(out, crc16(block.data(), block.size()));
        out.insert(out.end(), block.begin(), block.end());
    }
    /// endregion </Comment block>

    /// region <Tracks>
    bool lossy = false;
    for (uint8_t c = 0; c < cylinders; c++)
    {
        for (uint8_t h = 0; h < sides; h++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(c, h);
            const bool fm = track->encoding() == DiskImage::Encoding::FM;

            const size_t sectorCount = std::min<size_t>(track->sectorCount(), 254);
            if (track->sectorCount() > 254)
            {
                warnings.push_back(StringHelper::Format("TD0 cylinder %d head %d: only the first 254 of %zu sectors stored", c, h, track->sectorCount()));
            }
            const size_t nominal = fm ? DiskImage::RawTrack::DEFAULT_TRACK_SIZE_FM : DiskImage::RawTrack::DEFAULT_TRACK_SIZE_MFM;
            if (track->rawSize() != nominal) lossy = true;

            const size_t trackHeaderAt = out.size();
            out.push_back(static_cast<uint8_t>(sectorCount));
            out.push_back(c);
            out.push_back(static_cast<uint8_t>(h | (fm ? HEAD_FM : 0)));
            out.push_back(static_cast<uint8_t>(crc16(out.data() + trackHeaderAt, 3) & 0xFF));

            for (size_t s = 0; s < sectorCount; s++)
            {
                const DiskImage::Sector* sector = track->getRawSector(s);

                uint8_t flags = 0;
                if (!sector->hasData)
                {
                    flags |= FLAG_NO_DATA;
                }
                else
                {
                    if (!sector->dataCrcValid) flags |= FLAG_DATA_CRC_ERROR;
                    if (sector->deleted) flags |= FLAG_DELETED;
                }
                for (size_t prev = 0; prev < s; prev++)
                {
                    if (track->getRawSector(prev)->number() == sector->number())
                    {
                        flags |= FLAG_DUPLICATE;
                        break;
                    }
                }
                if (!sector->idCrcValid) lossy = true;   // TD0 cannot say "ID CRC wrong"

                out.push_back(sector->cylinder());
                out.push_back(sector->head());
                out.push_back(sector->number());
                out.push_back(sector->sizeCode());
                out.push_back(flags);

                if (!sector->hasData)
                {
                    out.push_back(0);
                    continue;
                }

                const size_t dataSize = sector->dataSize;
                const uint8_t* d = sector->data;
                out.push_back(static_cast<uint8_t>(crc16(d, dataSize) & 0xFF));

                bool isPattern = dataSize >= 2 && (dataSize % 2) == 0;
                for (size_t i = 2; isPattern && i < dataSize; i += 2)
                {
                    if (d[i] != d[0] || d[i + 1] != d[1]) isPattern = false;
                }

                if (isPattern)
                {
                    putU16(out, 5);   // encoding byte + count (2) + pattern (2)
                    out.push_back(ENCODING_PATTERN);
                    putU16(out, static_cast<uint16_t>(dataSize / 2));
                    out.push_back(d[0]);
                    out.push_back(d[1]);
                }
                else
                {
                    putU16(out, static_cast<uint16_t>(dataSize + 1));
                    out.push_back(ENCODING_RAW);
                    out.insert(out.end(), d, d + dataSize);
                }
            }
        }
    }
    out.push_back(END_OF_IMAGE);
    /// endregion </Tracks>

    if (lossy)
    {
        warnings.push_back("TD0 stores sector lists only: gaps, clock marks, exact CRC values and track length are not preserved (use UDI for a lossless copy)");
    }

    return true;
}

/// endregion </Serialisation>

/// region <Basic methods>

bool LoaderTD0::loadImage()
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

bool LoaderTD0::writeImage()
{
    return writeImage(_filepath);
}

bool LoaderTD0::writeImage(const std::string& path)
{
    _warnings.clear();

    if (!_diskImage || path.empty())
    {
        _warnings.push_back("TD0 save refused: no image or empty path");
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
        _warnings.push_back("TD0 save failed: cannot open " + path);
        return false;
    }

    bool saved = FileHelper::SaveBufferToFile(file, buffer.data(), buffer.size());
    FileHelper::CloseFile(file);

    if (!saved)
    {
        _warnings.push_back("TD0 save failed: cannot write " + path);
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
