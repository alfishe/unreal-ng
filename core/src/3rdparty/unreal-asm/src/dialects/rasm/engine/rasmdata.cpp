#include "rasmdata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

// Ports of rasm.c v3.3.3 (MIT, Edouard Berge). The function names in comments are rasm's.

namespace unrealasm::rasm
{
namespace
{
template <typename... Args>
std::string Format(const char* format, Args... args)
{
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    return buffer;
}

// double -> unsigned long as the reference build does (arm64 fcvtzu): truncation, NaN and negatives give 0, values from
// 2^64 up give all ones
std::uint64_t ToU64(double v)
{
    if (std::isnan(v) || v <= 0.0)
        return 0;
    if (v >= 18446744073709551616.0)
        return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(v);
}

// double -> int with arm64 fcvtzs saturation (NaN gives 0)
int ToIntSaturated(double v)
{
    if (std::isnan(v))
        return 0;
    if (v >= 2147483647.0)
        return std::numeric_limits<int>::max();
    if (v <= -2147483648.0)
        return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}

int MinMaxInt(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

// ---------------------------------------------------------------------------------------------------------------------
// Reals
// ---------------------------------------------------------------------------------------------------------------------

// The shared body of __internal_MakeAmsdosREAL and __internal_MakeRosoftREAL: rc[0..3] mantissa little endian, rc[4]
// exponent, before the byte order of each format
Real40 MakeReal40(double v)
{
    Real40 result;
    std::array<std::uint8_t, 5>& rc = result.bytes;
    bool negative = false;
    if (v < 0.0)
    {
        negative = true;
        v = -v;
    }
    int exponent = 0;
    std::uint32_t bits = 0;
    const std::uint64_t integer = ToU64(v);
    if (integer)
    {
        // exponent = number of bits of the low 32 bits of the integer part; rasm loops forever when they are all zero
        if ((integer & 0xFFFFFFFFu) == 0)
        {
            result.status = RealStatus::Hangs;
            return result;
        }
        std::uint64_t mask = 0x80000000u;
        while (!(integer & mask))
            mask >>= 1;
        while (mask)
        {
            ++exponent;
            mask >>= 1;
        }
        std::uint64_t mantissa = ToU64(std::ldexp(v, 32 - exponent) + 0.5);
        if (mantissa & 0xFF00000000ull)
            mantissa = 0xFFFFFFFFu;
        bits = static_cast<std::uint32_t>(mantissa);
    }
    else if (v == 0.0)
    {
        exponent = -128;
    }
    else
    {
        std::uint64_t mantissa = ToU64(v * 4294967296.0 + 0.5);
        if (mantissa & 0xFF00000000ull)
            mantissa = 0xFFFFFFFFu;
        if ((mantissa & 0xFFFFFFFFu) == 0)
        {
            // below 2^-33 the rounded fraction is 0 and rasm's search for its first bit never ends
            result.status = RealStatus::Hangs;
            return result;
        }
        std::uint64_t mask = 0x80000000u;
        while (!(mantissa & mask))
        {
            mask >>= 1;
            --exponent;
        }
        mantissa = ToU64(std::ldexp(v, 32 - exponent) + 0.5);
        if (mantissa & 0xFF00000000ull)
            mantissa = 0xFFFFFFFFu;
        bits = static_cast<std::uint32_t>(mantissa);
    }
    rc[0] = static_cast<std::uint8_t>(bits);
    rc[1] = static_cast<std::uint8_t>(bits >> 8);
    rc[2] = static_cast<std::uint8_t>(bits >> 16);
    rc[3] = static_cast<std::uint8_t>(bits >> 24);
    exponent += 128;
    if (exponent < 0 || exponent > 255)
    {
        result.status = RealStatus::ExponentOverflow;
        exponent = 128;
    }
    rc[4] = static_cast<std::uint8_t>(exponent);
    // the sign replaces the implied most significant bit
    if (negative)
        rc[3] |= 0x80;
    else
        rc[3] &= 0x7F;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// PSG tables (rasm's start-up code)
// ---------------------------------------------------------------------------------------------------------------------

std::array<std::uint8_t, 256> BuildTable(std::initializer_list<std::pair<int, int>> runs)
{
    std::array<std::uint8_t, 256> table{};
    int j = 0;
    for (const auto& [count, value] : runs)
        for (int i = 0; i < count; ++i)
            table[j++] = static_cast<std::uint8_t>(value);
    return table;
}

// ---------------------------------------------------------------------------------------------------------------------
// WAV
// ---------------------------------------------------------------------------------------------------------------------

std::uint32_t Le32(std::span<const std::uint8_t> data, size_t at)
{
    return data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

int Le16(std::span<const std::uint8_t> data, size_t at)
{
    return data[at] | (data[at + 1] << 8);
}

enum class SampleFormat
{
    U8,
    S16Little,
    S16Big,
    S24Little,
    S24Big,
    FloatLittle,
    FloatBig,
};

// The __internal_getsample* readers. rasm reads past the file when the data chunk is short; those bytes read as 0 here.
class SampleReader
{
public:
    SampleReader(std::span<const std::uint8_t> data, size_t index, SampleFormat format)
        : _data(data), _index(index), _format(format)
    {
    }

    int Next()
    {
        switch (_format)
        {
            case SampleFormat::U8:
            {
                const int v = Byte(_index) - 128;
                _index += 1;
                return v;
            }
            case SampleFormat::S16Little:
            {
                const int v = Signed(_index + 1);
                _index += 2;
                return v;
            }
            case SampleFormat::S16Big:
            {
                const int v = Signed(_index);
                _index += 2;
                return v;
            }
            case SampleFormat::S24Little:
            {
                const int v = Signed(_index + 2);
                _index += 3;
                return v;
            }
            case SampleFormat::S24Big:
            {
                const int v = Signed(_index);
                _index += 3;
                return v;
            }
            case SampleFormat::FloatLittle:
            case SampleFormat::FloatBig:
            {
                std::uint32_t raw = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const int at = _format == SampleFormat::FloatLittle ? i : 3 - i;
                    raw |= static_cast<std::uint32_t>(Byte(_index + at)) << (8 * i);
                }
                _index += 4;
                float sample;
                std::memcpy(&sample, &raw, sizeof(sample));
                // floor((fsample+1.0)*127.5+0.5): the reference build fuses the multiply-add
                return ToIntSaturated(std::floor(std::fma(static_cast<double>(sample) + 1.0, 127.5, 0.5)));
            }
        }
        return 0;
    }

private:
    int Byte(size_t at) const { return at < _data.size() ? _data[at] : 0; }
    // rasm reads through a plain char pointer: signed on the reference build
    int Signed(size_t at) const { return static_cast<std::int8_t>(static_cast<std::uint8_t>(Byte(at))); }

    std::span<const std::uint8_t> _data;
    size_t _index;
    SampleFormat _format;
};

// __internal_audio_filter: band-pass 100 Hz / 7800 Hz for 15625 Hz, in the reference build's fused form
float AudioFilter(AudioFilterState& f, float e)
{
    const double s = std::fma(static_cast<double>(f.oldOldS), 0.960513,
                              std::fma(static_cast<double>(f.oldS), -0.039487,
                                       std::fma(static_cast<double>(e), -0.980257,
                                                static_cast<double>(f.oldOldE) * 0.980257)));
    f.s = static_cast<float>(s);
    f.oldOldS = f.oldS;
    f.oldS = f.s;
    f.oldOldE = f.oldE;
    f.oldE = e;
    return f.s;
}

// cursample=MinMaxInt(floor(((accumulator/nbchannel)*normalize)+0.5)+128,0,255) (fused multiply-add in the reference)
int Normalize(double accumulator, int channels, float normalize)
{
    const double v = std::floor(std::fma(accumulator / channels, static_cast<double>(normalize), 0.5)) + 128.0;
    return MinMaxInt(ToIntSaturated(v), 0, 255);
}

// ---------------------------------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------------------------------

// The word as rasm's NUL-terminated buffer: past the end reads 0
char At(std::string_view w, size_t i)
{
    return i < w.size() ? w[i] : '\0';
}

std::uint8_t U8(char c)
{
    return static_cast<std::uint8_t>(c);
}

// \xHH after the 'x' at i: the value, i on the last digit; nullopt = "DEFB invalid escaped hexadecimal value!"
std::optional<std::uint8_t> HexEscape(std::string_view w, size_t& i)
{
    auto digit = [](char c) -> int {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    ++i;
    const int high = digit(At(w, i));
    if (high < 0)
        return std::nullopt;
    ++i;
    const int low = digit(At(w, i));
    if (low < 0)
        return std::nullopt;
    return static_cast<std::uint8_t>((high << 4) | low);
}

constexpr const char* kHexEscapeError = "DEFB invalid escaped hexadecimal value!";

// The UTF8REMAP lookup at w[i]: the entry or nullptr
const Utf8Remap* FindRemap(std::string_view w, size_t i, std::span<const Utf8Remap> remap)
{
    for (const Utf8Remap& entry : remap)
    {
        // an empty code never matches here ('x' gives length 0 - 1 in rasm; '' matches everywhere and hangs rasm)
        if (entry.code.empty())
            continue;
        size_t l = 0;
        for (; l < entry.code.size(); ++l)
            if (entry.code[l] != U8(At(w, i + l)))
                break;
        if (l == entry.code.size())
            return &entry;
    }
    return nullptr;
}

std::string RemapWarning(char c)
{
    return Format("UTF8 mapping was defined but no code were found for this one! [#%02X]", U8(c));
}
}  // namespace

// =====================================================================================================================
// 1. Reals
// =====================================================================================================================

Real40 MakeAmsdosReal(double value)
{
    return MakeReal40(value);
}

Real40 MakeMicrosoftReal(double value)
{
    Real40 result = MakeReal40(value);
    const std::array<std::uint8_t, 5> rc = result.bytes;
    result.bytes = {rc[4], rc[3], rc[2], rc[1], rc[0]};
    return result;
}

std::array<std::uint8_t, 3> MakeFloat24(double value)
{
    const float f = static_cast<float>(value);
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    const int exponent = (static_cast<int>((bits >> 23) & 0xFF) - 64) | static_cast<int>((bits >> 24) & 0x80);
    return {static_cast<std::uint8_t>(exponent & 0xFF), static_cast<std::uint8_t>((bits >> 7) & 0xFF),
            static_cast<std::uint8_t>((bits >> 15) & 0xFF)};
}

// =====================================================================================================================
// 2. INCBIN
// =====================================================================================================================

bool HasAmsdosHeader(std::span<const std::uint8_t> file)
{
    if (file.size() <= 128)
        return false;
    int checksum = 0;
    for (int i = 0; i <= 66; ++i)
        checksum += file[i];
    if (!checksum || file[67] != (checksum & 0xFF) || file[68] != (checksum >> 8))
        return false;
    if (!(file[0] < 16 && file[0x12] <= 2 && file[0x18] == file[0x40] && file[0x19] == file[0x41]))
        return false;
    const int length = file[0x18] + file[0x19] * 256;
    const int raw = static_cast<int>(file.size());
    return length <= raw - 128 && length >= raw - 128 - 512;
}

IncbinResult IncbinTransform(std::span<const std::uint8_t> file, const IncbinOptions& o)
{
    IncbinResult r;
    auto fail = [&r](std::string message) -> IncbinResult {
        r.bytes.clear();
        r.error = std::move(message);
        return r;
    };

    // __HEXBIN option parsing
    if (o.remap && *o.remap < 1)
        return fail("INCBIN REMAP need a proper number of columns (>1)");
    int size = o.size;
    int offset = o.offset;
    if (size > 65536)
        return fail("INCBIN invalid size, must be lower than 65536");
    const bool itiles = o.tiles == IncbinTiles::ITiles;
    const bool gtiles = o.tiles == IncbinTiles::GTiles;
    const int options = (itiles ? 1 : 0) + (gtiles ? 1 : 0) + (o.vtiles ? 1 : 0) + (o.remap ? 1 : 0) + (o.repack ? 1 : 0);
    if (options > 1)
        return fail("INCBIN options ITLES, GTILES, VTILES, REPACK and REMAP are mutually exclusives");

    // delayed load + SKIPHEADER
    if (o.skipHeader && HasAmsdosHeader(file))
    {
        if (offset >= 0)
            offset += 128;
        else
            r.warnings.push_back("SKIPHEADER is useless with negative offset");
    }

    const int datalen = static_cast<int>(file.size());
    if (datalen == 0)
    {
        if (size)
            return fail(Format("INCBIN cannot read %d byte%s as the file is empty", size, size > 1 ? "s" : ""));
        r.warnings.push_back("INCBIN of an empty file");
        return r;
    }

    if (size < 0)
    {
        size = datalen + size;
        if (size < 1)
            return fail("INCBIN negative size is greater or equal to filesize");
    }
    if (offset < 0)
        offset = datalen + offset;
    if (!size)
        size = datalen - offset;
    if (size > datalen)
        return fail(Format("INCBIN size is greater than filesize (%d>%d)", size, datalen));
    if (size + offset > datalen)
        return fail(Format("INCBIN size+offset is greater than filesize (%d+%d>%d)", size, offset, datalen));
    r.selectedSize = size;

    // rasm transforms its copy of the file in place (REVERT, the ISLICE line swap)
    Bytes data(file.begin(), file.end());
    // a negative offset left after the conversion makes rasm read before its buffer (REVERT, ITILES / GTILES, REMAP,
    // VTILES, or a plain copy whose end is negative too); the plain copy of any other negative offset copies nothing
    const bool readsAtOffset = o.revert || itiles || gtiles || o.remap || o.vtiles ||
                               (o.slice == IncbinSlice::None && !o.repack && size + offset < 0);
    if (offset < 0 && readsAtOffset)
        return fail("INCBIN offset before the start of the file (rasm reads outside its buffer)");
    if (o.revert && size > 1 && offset >= 0)
        std::reverse(data.begin() + offset, data.begin() + offset + size);

    Bytes& out = r.bytes;
    if (o.slice != IncbinSlice::None)
    {
        const int tilew = o.sliceWidth;
        const int tileh = o.sliceHeight;
        const int tilesize = tilew * tileh;
        if (tilew < 1 || tilew > 256 || tileh < 1)
            return fail("INCBIN SLICE need a tile width in the range [1-256] and a proper height");
        if (datalen % tilesize != 0)
            return fail("INCBIN SLICE need input data to be a multiple of tiles size");
        if (o.slice == IncbinSlice::ISlice && (tileh & 7))
            return fail("INCBIN ISLICE need tile height to be 8 factor");
        const int ntiles = datalen / tilesize;
        const int tpg = 256 / tilew;
        const int groups = (ntiles + tpg - 1) / tpg;
        const int totalsize = (groups * tileh - 1) * 256 + tilew * (ntiles - (groups - 1) * tpg);
        if (totalsize > 65536)
            return fail("INCBIN SLICE outputdata will be too big (>64K)");
        out.assign(static_cast<size_t>(totalsize), 0);
        if (o.slice == IncbinSlice::ISlice)
        {
            // lines 0,1,3,2,6,7,5,4 of each 8-line group go to 0..7
            const int b8 = ntiles * tileh / 8;
            std::vector<std::uint8_t> line(static_cast<size_t>(tilew));
            for (int it = 0; it < b8; ++it)
            {
                std::uint8_t* g = data.data() + static_cast<size_t>(it) * tilew * 8;
                auto row = [&](int n) { return g + static_cast<size_t>(tilew) * n; };
                std::memcpy(line.data(), row(2), tilew);
                std::memcpy(row(2), row(3), tilew);
                std::memcpy(row(3), line.data(), tilew);
                std::memcpy(line.data(), row(4), tilew);
                std::memcpy(row(4), row(6), tilew);
                std::memcpy(row(6), row(5), tilew);
                std::memcpy(row(5), row(7), tilew);
                std::memcpy(row(7), line.data(), tilew);
            }
        }
        for (int it = 0; it < ntiles; ++it)
        {
            const int g = it / tpg;
            const int ig = (it % tpg) * tilew;
            for (int y = 0; y < tileh; ++y)
                std::memcpy(out.data() + static_cast<size_t>((g * tileh + y) * 256 + ig),
                            data.data() + static_cast<size_t>(it * tilesize + y * tilew), tilew);
        }
        r.alignTo256 = !o.misaligned;
    }
    else if (itiles || gtiles)
    {
        const int tilex = o.tileWidth;
        if (tilex <= 0 || tilex > 256)
            return fail("INCBIN ITILES/GTILES need a tile value in the range [1-256]");
        if (size % (tilex * 8))
            return fail(Format("INCBIN ITILES/GTILES cannot reorder tiles %d bytewidth with file of size %d", tilex, size));
        static constexpr int kGray[8] = {0, 1, 3, 2, 6, 7, 5, 4};
        for (int it = 0; it < size; it += tilex * 8)
        {
            for (int line = 0; line < 8; ++line)
            {
                const int base = it + kGray[line] * tilex + offset;
                const bool mirrored = itiles && (line & 1);
                for (int tx = 0; tx < tilex; ++tx)
                    out.push_back(data[static_cast<size_t>(base + (mirrored ? tilex - 1 - tx : tx))]);
            }
        }
    }
    else if (o.repack)
    {
        if (size & 1)
            r.warnings.push_back("INCBIN REPACK is using an odd length");
        // rasm ORs each even byte with itself shifted (not with its neighbour), from the start of the file
        for (int tx = 0; tx < size; tx += 2)
            out.push_back(static_cast<std::uint8_t>(data[tx] | (data[tx] << 4)));
    }
    else if (o.remap)
    {
        const int remap = *o.remap;
        if (remap <= 0 || remap > 32768)
            return fail("INCBIN REMAP need a remap (column number) value in the range [1-32768]");
        const int height = size / remap;
        if (!height || (size % remap) || (remap * height > size))
            return fail(Format("INCBIN REMAP cannot reorder %d columns%s with file of size %d", remap,
                               remap > 1 ? "s" : "", size));
        for (int it = 0; it < remap; ++it)
            for (int tx = 0; tx < height; ++tx)
                out.push_back(data[static_cast<size_t>(it + tx * remap + offset)]);
    }
    else if (o.vtiles)
    {
        const int vtiles = o.vtiles;
        if (vtiles <= 0 || vtiles > 32768)
            return fail("INCBIN VTILES need a value in the range [1-32768]");
        const int width = size / vtiles;
        if (!width || (size % vtiles) || (vtiles * width > size))
            return fail(Format("INCBIN VTILES cannot reorder %d line%s with file of size %d", vtiles,
                               vtiles > 1 ? "s" : "", size));
        int tilex = 0;
        int tiley = 0;
        for (int idx = 0; idx < size; ++idx)
        {
            out.push_back(data[static_cast<size_t>(tilex + tiley * width + offset)]);
            if (++tiley >= vtiles)
            {
                tiley = 0;
                ++tilex;
            }
        }
    }
    else
    {
        // for (idx=offset;idx<size+offset;idx++) with an unsigned idx: a negative offset copies nothing
        const auto first = static_cast<std::uint32_t>(offset);
        const auto last = static_cast<std::uint32_t>(size + offset);
        for (std::uint32_t idx = first; idx < last; ++idx)
            out.push_back(data[idx]);
    }

    if (!size)
    {
        r.warnings.push_back("wont try to crunch no data at all");
        out.clear();
    }
    return r;
}

// =====================================================================================================================
// WAV import (_AudioLoadSample + the DMA framing of __HEXBIN)
// =====================================================================================================================

const std::array<std::uint8_t, 256>& PsgFineTable()
{
    static const std::array<std::uint8_t, 256> table = BuildTable({{1, 0},
                                                                   {1, 1},
                                                                   {1, 2},
                                                                   {2, 3},
                                                                   {3, 4},
                                                                   {4, 5},
                                                                   {5, 6},
                                                                   {6, 7},
                                                                   {9, 8},
                                                                   {18, 9},
                                                                   {20, 10},
                                                                   {24, 11},
                                                                   {30, 12},
                                                                   {34, 13},
                                                                   {48, 14},
                                                                   {50, 15}});
    return table;
}

const std::array<std::uint8_t, 256>& PsgTable()
{
    static const std::array<std::uint8_t, 256> table = BuildTable({{64, 0}, {64, 10}, {64, 14}, {64, 15}});
    return table;
}

const std::array<std::uint8_t, 256>& DmaTable()
{
    static const std::array<std::uint8_t, 256> table = BuildTable({{51, 0}, {51, 10}, {52, 12}, {51, 14}, {51, 15}});
    return table;
}

DataResult ImportWav(std::span<const std::uint8_t> data, const WavOptions& options, AudioFilterState& filter)
{
    DataResult r;
    auto fail = [&r](std::string message) -> DataResult {
        r.bytes.clear();
        r.error = std::move(message);
        return r;
    };
    constexpr size_t kHeader = 44;  // sizeof(struct s_wav_header)
    const size_t filesize = data.size();
    const bool dma = options.mode == WavMode::Dma;
    const float normalize = dma ? 1.0f : static_cast<float>(options.amplification);
    int psgReg = 0xA;
    if (options.channel == DmaChannel::A)
        psgReg = 0x8;
    else if (options.channel == DmaChannel::B)
        psgReg = 0x9;

    if (dma && options.dmaRepeat && (*options.dmaRepeat < 1 || *options.dmaRepeat > 4095))
        return fail("DMA_REPEAT value out of bounds (1-4095)");

    bool raw = true;
    bool bigEndian = false;
    if (filesize > kHeader)
    {
        if (std::memcmp(data.data(), "RIFF", 4) == 0)
            raw = false;
        else if (std::memcmp(data.data(), "RIFX", 4) == 0)
        {
            raw = false;
            bigEndian = true;
        }
    }

    size_t index = 0;
    int channels = 1;
    int samples = 0;
    SampleFormat format = SampleFormat::U8;
    if (!raw)
    {
        if (std::memcmp(data.data() + 8, "WAVE", 4) != 0)
            return fail("WAV import - unsupported audio sample type (format must be 'WAVE')");
        // header fields are read little endian even in a RIFX file, as rasm does
        if (static_cast<std::int32_t>(Le32(data, 16)) != 16)
            return fail("WAV import - invalid wav chunk size (subchunk1 control)");
        if (std::memcmp(data.data() + 12, "fmt", 3) != 0)
            return fail("WAV import - unsupported audio sample type (subchunk1id must be 'fmt')");
        // chunk walk from offset 36; rasm only checks each chunk size against the file size
        std::int64_t chunk = 36;
        while (true)
        {
            if (chunk < 0 || chunk + 8 > static_cast<std::int64_t>(filesize))
                return fail("WAV import - data subchunk not found");
            if (std::memcmp(data.data() + chunk, "data", 4) == 0)
                break;
            const std::int32_t chunkSize = static_cast<std::int32_t>(8u + Le32(data, static_cast<size_t>(chunk) + 4));
            if (static_cast<std::uint32_t>(chunkSize) >= filesize)
                return fail("WAV import - data subchunk not found");
            chunk += chunkSize;
        }
        const std::int32_t dataSize = static_cast<std::int32_t>(Le32(data, static_cast<size_t>(chunk) + 4));
        channels = Le16(data, 22);
        if (channels < 1)
            return fail("WAV import - invalid number of audio channel");
        const int wFormat = Le16(data, 20);
        if (wFormat != 1 && wFormat != 3)
            return fail(Format("WAV import - invalid or unsupported wFormatTag (%04X)", wFormat));
        const double frequency = static_cast<std::int32_t>(Le32(data, 24));
        if (dma && std::fabs(frequency / 15600.0 - 1.0) > 0.2)
            r.warnings.push_back(Format("WAV sample frequency (%dHz) is very different from 15KHz DMA frequency",
                                        static_cast<int>(frequency)));
        const int bitsPerSample = Le16(data, 34);
        switch (bitsPerSample)
        {
            case 8:
                format = SampleFormat::U8;
                break;
            case 16:
                format = bigEndian ? SampleFormat::S16Big : SampleFormat::S16Little;
                break;
            case 24:
                format = bigEndian ? SampleFormat::S24Big : SampleFormat::S24Little;
                break;
            case 32:
                if (wFormat != 3)
                    return fail("WAV import - unsupported 32bits PCM");
                format = bigEndian ? SampleFormat::FloatBig : SampleFormat::FloatLittle;
                break;
            default:
                return fail(Format("WAV import - unsupported bits per sample (%d)", bitsPerSample));
        }
        samples = dataSize / channels / (bitsPerSample / 8);
        if (dataSize < 0 || static_cast<std::uint64_t>(dataSize) + kHeader > filesize)
            return fail(Format("WAV import - cannot read %d byte%s of audio whereas the file is %d bytes big!", dataSize,
                               dataSize > 1 ? "s" : "", static_cast<int>(filesize)));
        // rasm starts reading at the "data" chunk id, not after its 8-byte header
        index = static_cast<size_t>(chunk);
    }
    else
    {
        samples = static_cast<int>(filesize);
    }

    SampleReader reader(data, index, format);
    auto next = [&]() {
        double accumulator = 0.0;
        for (int n = 0; n < channels; ++n)
            accumulator += reader.Next();
        return accumulator;
    };
    const auto& psgfine = PsgFineTable();
    const auto& psgtab = PsgTable();
    const auto& dmatab = DmaTable();
    Bytes& out = r.bytes;

    switch (options.mode)
    {
        case WavMode::Smp:
            for (int i = 0; i < samples; ++i)
                out.push_back(psgfine[Normalize(next(), channels, normalize)]);
            break;
        case WavMode::Sm2:
        {
            std::uint8_t value = 0;
            for (int i = 0; i < samples + 1; i += 2)
            {
                for (int j = 0; j < 2; ++j)
                    value = static_cast<std::uint8_t>((value << 4) + psgfine[Normalize(next(), channels, normalize)]);
                out.push_back(value);
            }
            break;
        }
        case WavMode::Sm4:
        {
            std::uint8_t value = 0;
            for (int i = 0; i < samples + 3; i += 4)
            {
                for (int j = 0; j < 4; ++j)
                {
                    const int level = (psgtab[Normalize(next(), channels, normalize)] + 1) >> 2;
                    value = static_cast<std::uint8_t>(level ? (value << 2) + level - 1 : (value << 2));
                }
                out.push_back(value);
            }
            break;
        }
        case WavMode::Dma:
        {
            if (options.dmaRepeat)
            {
                out.push_back(static_cast<std::uint8_t>(*options.dmaRepeat & 0xFF));
                out.push_back(static_cast<std::uint8_t>(0x20 | ((*options.dmaRepeat >> 8) & 0xF)));
            }
            std::uint8_t sampleValue = 0;
            std::uint8_t previous = psgtab[Normalize(next(), channels, normalize)];
            int repeat = 0;
            for (int i = 1; i < samples; ++i)
            {
                const double accumulator = next();
                const float filtered = AudioFilter(filter, static_cast<float>(accumulator / channels));
                // the filtered sample times normalize is a float product, then + 0.5 in double
                const double v = std::floor(static_cast<double>(filtered * normalize) + 0.5) + 128.0;
                sampleValue = dmatab[MinMaxInt(ToIntSaturated(v), 0, 255)];
                if (sampleValue == previous && i + 1 < samples)
                {
                    ++repeat;
                }
                else
                {
                    if (!repeat)
                    {
                        out.push_back(sampleValue);
                        out.push_back(static_cast<std::uint8_t>(psgReg));
                    }
                    else
                    {
                        out.push_back(previous);
                        out.push_back(static_cast<std::uint8_t>(psgReg));
                        while (repeat)
                        {
                            const int pause = repeat < 4096 ? repeat : 4095;
                            out.push_back(static_cast<std::uint8_t>(pause & 0xFF));
                            out.push_back(static_cast<std::uint8_t>(0x10 | ((pause >> 8) & 0xF)));
                            repeat -= 4096;
                            if (repeat < 0)
                                repeat = 0;
                        }
                    }
                    previous = sampleValue;
                }
            }
            if (!repeat)
            {
                out.push_back(sampleValue);
                out.push_back(static_cast<std::uint8_t>(psgReg));
            }
            if (options.dmaRepeat)
            {
                out.push_back(0x01);
                out.push_back(0x40);  // LOOP
            }
            out.push_back(0);
            out.push_back(static_cast<std::uint8_t>(psgReg));  // volume to zero
            if (options.dmaInt)
            {
                out.push_back(0x10);
                out.push_back(0x40);  // INT
            }
            out.push_back(0x20);
            out.push_back(0x40);  // STOP
            break;
        }
    }
    return r;
}

// =====================================================================================================================
// 3. Headers, pokers, colours
// =====================================================================================================================

std::array<char, 11> MakeAmsdosName(std::string_view reference, int& user, std::vector<std::string>* warnings)
{
    std::string_view filename = reference;
    const size_t colon = filename.find(':');
    if (colon != std::string_view::npos)
    {
        user = std::atoi(std::string(filename.substr(0, colon)).c_str());
        filename = filename.substr(colon + 1);
    }
    else
    {
        user = 0;
    }
    // a NUL ends the C string
    if (const size_t nul = filename.find('\0'); nul != std::string_view::npos)
        filename = filename.substr(0, nul);
    // remove the path: first every '/', then every '\'
    for (size_t slash; (slash = filename.find('/')) != std::string_view::npos;)
        filename = filename.substr(slash + 1);
    for (size_t slash; (slash = filename.find('\\')) != std::string_view::npos;)
        filename = filename.substr(slash + 1);

    const std::string warning = Format("filename [%s] too long for AMSDOS, will be truncated", std::string(filename).c_str());
    if (filename.size() > 12)
    {
        if (warnings)
            warnings->push_back(warning);
    }
    else if (const size_t dot = filename.find('.'); dot != std::string_view::npos && dot > 8)
    {
        if (warnings)
            warnings->push_back(warning);
    }

    auto upper = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; };
    std::array<char, 11> name;
    name.fill(' ');
    size_t i = 0;
    for (; i < filename.size() && filename[i] != '.' && i < 8; ++i)
        name[i] = upper(filename[i]);
    while (i < filename.size() && filename[i] != '.')
        ++i;
    if (i < filename.size() && filename[i] == '.')
    {
        ++i;
        for (size_t ia = 0; i < filename.size() && ia < 3; ++ia)
            name[8 + ia] = upper(filename[i++]);
    }
    return name;
}

std::array<std::uint8_t, 128> MakeAmsdosHeader(int run, int minmem, int maxmem, std::span<const char, 11> name, int user,
                                               int type, std::string_view version)
{
    std::array<std::uint8_t, 128> h{};
    const int length = maxmem - minmem;
    h[0] = static_cast<std::uint8_t>(user);
    std::memcpy(h.data() + 1, name.data(), 11);
    h[18] = static_cast<std::uint8_t>(type);
    h[19] = static_cast<std::uint8_t>(length & 0xFF);
    h[20] = static_cast<std::uint8_t>(length >> 8);
    h[21] = static_cast<std::uint8_t>(minmem & 0xFF);
    h[22] = static_cast<std::uint8_t>(minmem >> 8);
    h[24] = h[19];
    h[25] = h[20];
    h[26] = static_cast<std::uint8_t>(run & 0xFF);
    h[27] = static_cast<std::uint8_t>(run >> 8);
    h[64] = h[19];
    h[65] = h[20];
    h[66] = 0;
    int checksum = 0;
    for (int i = 0; i <= 66; ++i)
        checksum += h[i];
    h[67] = static_cast<std::uint8_t>(checksum & 0xFF);
    h[68] = static_cast<std::uint8_t>(checksum >> 8);
    // "garbage / shadow values from sector buffer"
    std::memcpy(h.data() + 0x47, name.data(), 8);
    h[0x4F] = 0x24;
    h[0x50] = 0x24;
    h[0x51] = 0x24;
    h[0x52] = 0xFF;
    h[0x54] = 0xFF;
    h[0x57] = 0x02;
    h[0x5A] = h[21];
    h[0x5B] = h[22];
    h[0x5D] = h[24];
    h[0x5E] = h[25];
    // sprintf(+0x47+17," created by %-9.9s ",RASM_SNAP_VERSION): overwrites 0x5A..0x5E, ends with a NUL at 0x6E
    const std::string text = Format(" created by %-9.9s ", std::string(version).c_str());
    std::memcpy(h.data() + 0x58, text.c_str(), std::min<size_t>(text.size() + 1, h.size() - 0x58));
    return h;
}

std::array<std::uint8_t, 17> MakeHobetaHeader(int minmem, int maxmem, std::string_view trdosName)
{
    std::array<std::uint8_t, 17> h{};
    const int length = maxmem - minmem;
    for (size_t i = 0; i < 8 && i < trdosName.size() && trdosName[i] != '\0'; ++i)
        h[i] = static_cast<std::uint8_t>(trdosName[i]);
    h[8] = 'C';
    // rasm writes the length where the start address belongs (and again as the length)
    h[0x9] = static_cast<std::uint8_t>(length & 0xFF);
    h[0xA] = static_cast<std::uint8_t>(length >> 8);
    h[0xB] = static_cast<std::uint8_t>(length & 0xFF);
    h[0xC] = static_cast<std::uint8_t>(length >> 8);
    h[0xD] = static_cast<std::uint8_t>((length + 255) >> 8);
    h[0xE] = 0;
    int checksum = 0;
    for (int i = 0; i < 0xF; ++i)
        checksum += h[i] * 257 + i;
    h[0xF] = static_cast<std::uint8_t>(checksum & 0xFF);
    h[0x10] = static_cast<std::uint8_t>((checksum >> 8) & 0xFF);
    return h;
}

DataResult MakeSaveFile(std::string_view filename, std::span<const std::uint8_t> bank, int offset, int size, int run,
                        SaveFormat format)
{
    DataResult r;
    const std::string name(filename);
    const int savelimit = format == SaveFormat::Plain ? static_cast<int>(bank.size()) : 65536;
    if (size < 1 || size > savelimit)
    {
        r.error = Format("cannot save [%s] as the size is invalid!", name.c_str());
        return r;
    }
    if (offset < 0 || offset > savelimit - 1)
    {
        r.error = Format("cannot save [%s] as the offset is invalid!", name.c_str());
        return r;
    }
    if (offset + size > savelimit)
    {
        r.error = Format("cannot save [%s] as the offset+size will be out of bounds!", name.c_str());
        return r;
    }
    if (offset + size > static_cast<int>(bank.size()))
    {
        r.error = Format("cannot save [%s] as the offset+size will be out of bounds!", name.c_str());
        return r;
    }
    if (run < 0)
        run = offset;
    int user = 0;
    if (format == SaveFormat::Amsdos)
    {
        // rasm calls MakeAMSDOS_name twice; the first call cuts the file name at its ':' in place, so the header name
        // (and the file written) come from the user-number part when there is one
        MakeAmsdosName(filename, user, &r.warnings);
        const std::string_view cut = filename.substr(0, filename.find(':'));
        const std::array<char, 11> amsdosName = MakeAmsdosName(cut, user, &r.warnings);
        const auto header = MakeAmsdosHeader(run, offset, offset + size, amsdosName, user);
        r.bytes.assign(header.begin(), header.end());
    }
    else if (format == SaveFormat::Hobeta)
    {
        const std::array<char, 11> amsdosName = MakeAmsdosName(filename, user, &r.warnings);
        const auto header = MakeHobetaHeader(offset, offset + size, std::string_view(amsdosName.data(), 11));
        r.bytes.assign(header.begin(), header.end());
    }
    r.bytes.insert(r.bytes.end(), bank.begin() + offset, bank.begin() + offset + size);
    return r;
}

namespace
{
template <typename Function>
void ForRange(size_t bankSize, int start, int end, Function function)
{
    const int first = std::max(start, 0);
    const int last = std::min(end, static_cast<int>(bankSize));
    for (int j = first; j < last; ++j)
        function(j);
}
}  // namespace

std::uint8_t SumMem(std::span<const std::uint8_t> bank, int start, int end)
{
    std::uint8_t sum = 0;
    ForRange(bank.size(), start, end, [&](int j) { sum = static_cast<std::uint8_t>(sum + bank[j]); });
    return sum;
}

std::uint16_t Sum16(std::span<const std::uint8_t> bank, int start, int end)
{
    std::uint16_t sum = 0;
    ForRange(bank.size(), start, end, [&](int j) { sum = static_cast<std::uint16_t>(sum + bank[j]); });
    return sum;
}

std::uint8_t XorMem(std::span<const std::uint8_t> bank, int start, int end)
{
    std::uint8_t value = 0;
    ForRange(bank.size(), start, end, [&](int j) { value = static_cast<std::uint8_t>(value ^ bank[j]); });
    return value;
}

std::string CipherMem(std::span<std::uint8_t> bank, int start, int end, int method, std::string_view key)
{
    switch (method)
    {
        default:
        case 0:
        case 1:
        case 2:
        {
            std::uint8_t value;
            if (method == 2)
            {
                value = static_cast<std::uint8_t>(start & 0xFF);  // depends on the memory location
            }
            else
            {
                value = start >= 0 && start < static_cast<int>(bank.size()) ? bank[start] : 0;
                ++start;
            }
            ForRange(bank.size(), start, end, [&](int j) { bank[j] = value = static_cast<std::uint8_t>(bank[j] ^ value); });
            return {};
        }
        case 3:
            ForRange(bank.size(), start, end, [&](int j) { bank[j] = static_cast<std::uint8_t>(bank[j] ^ (j & 0xFF)); });
            return {};
        case 4:
        {
            // strlen of the word without its opening quote, minus the closing quote: a NUL in the key ends it
            const std::string_view text = key.substr(0, key.find('\0'));
            if (text.size() <= 1)
                return "CIPHER needs a string of one char or more to run properly";
            size_t k = 0;
            ForRange(bank.size(), start, end, [&](int j) {
                bank[j] = static_cast<std::uint8_t>(bank[j] ^ static_cast<std::uint8_t>(text[k]));
                if (++k >= text.size())
                    k = 0;
            });
            return {};
        }
    }
}

std::optional<int> Soft2HardInk(double soft)
{
    static constexpr int kHard[27] = {20, 4, 21, 28, 24, 29, 12, 5, 13, 22, 6, 23, 30, 0,
                                      31, 14, 7, 15, 18, 2, 19, 26, 25, 27, 10, 3, 11};
    const int index = ToIntSaturated(soft);
    if (index < 0 || index > 26)
        return std::nullopt;
    return 64 + kHard[index];
}

int Hard2SoftInk(double hard)
{
    static constexpr int kSoft[32] = {13, 13, 19, 25, 1, 7, 10, 16, 7, 25, 24, 26, 6, 8, 15, 17,
                                      1, 19, 18, 20, 0, 2, 9, 11, 4, 22, 21, 23, 3, 5, 12, 14};
    return kSoft[ToIntSaturated(hard) & 31];
}

int GetR(double colour)
{
    return (ToIntSaturated(colour) & 0xF0) >> 4;
}

int GetV(double colour)
{
    return (ToIntSaturated(colour) & 0xF00) >> 8;
}

int GetB(double colour)
{
    return ToIntSaturated(colour) & 0xF;
}

int SetR(double level)
{
    return MinMaxInt(ToIntSaturated(level), 0, 15) << 4;
}

int SetV(double level)
{
    return MinMaxInt(ToIntSaturated(level), 0, 15) << 8;
}

int SetB(double level)
{
    return MinMaxInt(ToIntSaturated(level), 0, 15);
}

// =====================================================================================================================
// 4. BUILDZX
// =====================================================================================================================

Bytes BuildZxSnapshot(const ZxSnapshotInput& input)
{
    constexpr size_t kPage = 16384;
    Bytes bank0(65536, 0);
    std::copy_n(input.bank0.begin(), std::min(input.bank0.size(), bank0.size()), bank0.begin());
    // RUN pc,sp: pc is pushed at bank0[sp] (rasm writes bank0[65536] for sp = #FFFF, outside its buffer: dropped)
    const unsigned sp = input.stack & 0xFFFF;
    bank0[sp] = static_cast<std::uint8_t>(input.run & 0xFF);
    if (sp + 1 < bank0.size())
        bank0[sp + 1] = static_cast<std::uint8_t>((input.run >> 8) & 0xFF);

    // 27-byte .sna header: everything 0 but IFF2 (#13) = 0 (DI), SP (#17), IM (#19) = 1; byte 26 (border) is read past
    // rasm's 26-byte array and is 0 on the reference build
    Bytes out(27, 0);
    out[0x13] = 0;
    out[0x17] = static_cast<std::uint8_t>(input.stack & 0xFF);
    out[0x18] = static_cast<std::uint8_t>((input.stack >> 8) & 0xFF);
    out[0x19] = 1;
    auto page = [&](std::span<const std::uint8_t> bank, size_t from) {
        for (size_t i = 0; i < kPage; ++i)
            out.push_back(from + i < bank.size() ? bank[from + i] : 0);
    };
    if (input.bankset0)
    {
        page(bank0, kPage);
        page(bank0, 2 * kPage);
        page(bank0, 3 * kPage);
    }
    else
    {
        page(input.bank5, 0);
        page(input.bank2, 0);
        page(bank0, 0);
    }
    return out;
}

// =====================================================================================================================
// 5. Strings
// =====================================================================================================================

std::optional<Utf8Remap> ParseUtf8Remap(std::string_view word, int value)
{
    if (word.empty() || (word[0] != '\'' && word[0] != '"'))
        return std::nullopt;
    // strncpy of 15 characters after the opening quote, then the closing quote dropped by length - 1
    std::string_view text = word.substr(1, 15);
    text = text.substr(0, text.find('\0'));
    Utf8Remap remap;
    remap.value = static_cast<std::uint8_t>(value);
    if (!text.empty())
        remap.code.assign(text.begin(), text.end() - 1);
    return remap;
}

StringResult DecodeQuotedString(std::string_view w, const std::array<std::uint8_t, 256>& charset,
                                std::span<const Utf8Remap> remap, const StringOptions& options)
{
    StringResult r;
    Bytes& out = r.bytes;
    if (w.empty())
        return r;
    const char quote = w[0];
    auto fail = [&r](std::string message) -> StringResult {
        r.error = std::move(message);
        return r;
    };
    switch (options.directive)
    {
        case StringDirective::Defb:
        case StringDirective::DefbStruct:
        case StringDirective::StructField:
        {
            const bool field = options.directive == StringDirective::StructField;
            const bool full = options.directive != StringDirective::DefbStruct;
            size_t i = 1;
            while (true)
            {
                if (field && r.count >= options.maxBytes)
                    break;
                if (!At(w, i) || At(w, i) == quote)
                    break;
                if (At(w, i) == '\\')
                {
                    ++i;
                    const char c = At(w, i);
                    if (c != '\\')
                    {
                        switch (c)
                        {
                            case 'e':
                                if (full)
                                    out.push_back(0x1B);
                                else
                                    out.push_back(U8(c));
                                break;
                            case 'a':
                                if (full)
                                    out.push_back(0x07);
                                else
                                    out.push_back(U8(c));
                                break;
                            case 'b':
                                out.push_back('\b');
                                break;
                            case 'v':
                                out.push_back('\v');
                                break;
                            case 'f':
                                out.push_back('\f');
                                break;
                            case '0':
                                out.push_back(0);
                                break;
                            case 'r':
                                out.push_back('\r');
                                break;
                            case 'n':
                                out.push_back('\n');
                                break;
                            case 't':
                                out.push_back('\t');
                                break;
                            case 'x':
                                if (full)
                                {
                                    const std::optional<std::uint8_t> value = HexEscape(w, i);
                                    if (!value)
                                        return fail(kHexEscapeError);
                                    out.push_back(*value);
                                }
                                else
                                {
                                    out.push_back(U8(c));
                                }
                                break;
                            default:
                                out.push_back(U8(c));
                                break;
                        }
                    }
                    else
                    {
                        out.push_back(charset[U8(c)]);
                    }
                }
                else if (!remap.empty() && U8(At(w, i)) > 126)
                {
                    if (const Utf8Remap* entry = FindRemap(w, i, remap))
                    {
                        out.push_back(entry->value);
                        i += entry->code.size() - 1;
                    }
                    else
                    {
                        r.warnings.push_back(RemapWarning(At(w, i)));
                    }
                }
                else
                {
                    out.push_back(charset[U8(At(w, i))]);
                }
                ++i;
                ++r.count;
            }
            if (field && r.count == options.maxBytes && At(w, i) && At(w, i) != quote)
                r.overrun = true;
            return r;
        }
        case StringDirective::DefbAs80:
        {
            size_t i = 1;
            while (At(w, i) && At(w, i) != quote)
            {
                if (At(w, i) == '\\')
                    ++i;
                out.push_back(charset[U8(At(w, i))]);
                ++i;
            }
            return r;
        }
        case StringDirective::Str:
        {
            size_t i = 1;
            while (At(w, i) && At(w, i) != quote)
            {
                std::uint8_t c;
                if (At(w, i) == '\\')
                {
                    ++i;
                    c = U8(At(w, i));
                    switch (At(w, i))
                    {
                        case 'b':
                            c = '\b';
                            break;
                        case 'v':
                            c = '\v';
                            break;
                        case 'f':
                            c = '\f';
                            break;
                        case '0':
                            c = 0;
                            break;
                        case 'r':
                            c = '\r';
                            break;
                        case 'n':
                            c = '\n';
                            break;
                        case 't':
                            c = '\t';
                            break;
                        case 'x':
                        {
                            const std::optional<std::uint8_t> value = HexEscape(w, i);
                            if (!value)
                                return fail(kHexEscapeError);
                            c = *value;
                            break;
                        }
                        default:
                            break;
                    }
                }
                else
                {
                    c = charset[U8(At(w, i))];
                }
                if (At(w, i + 1) == quote && options.markEnd)
                    c |= 0x80;
                out.push_back(c);
                ++i;
            }
            return r;
        }
    }
    return r;
}

int EscapedCharValue(char c)
{
    switch (c)
    {
        case 'b':
            return '\b';
        case 'v':
            return '\v';
        case 'f':
            return '\f';
        case '0':
            return 0;
        case 'r':
            return '\r';
        case 'n':
            return '\n';
        case 't':
            return '\t';
        default:
            return static_cast<unsigned char>(c);
    }
}

std::string ApplyUtf8Option(std::string_view word, std::vector<std::string>* errors)
{
    std::string w(word);
    auto error = [errors]() {
        if (errors)
            errors->push_back("Unsupported UTF8 char for quoted string");
    };
    // w as a C string: stop at a NUL
    for (size_t i = 0; i < w.size() && w[i]; ++i)
    {
        if (U8(w[i]) <= 126)
            continue;
        const std::uint8_t next = i + 1 < w.size() ? U8(w[i + 1]) : 0;
        switch (U8(w[i]))
        {
            case 0xC2:
                if (next == 0xBF)
                    w[i] = static_cast<char>(174);  // inverted question mark
                else if (next == 0xA1)
                    w[i] = static_cast<char>(175);  // inverted exclamation mark
                else
                    error();
                break;
            case 0xC3:
                if (next == 0xB9)
                    w[i] = 124;  // u grave
                else if (next == 0xA9)
                    w[i] = 123;  // e acute
                else if (next == 0xA8)
                    w[i] = 125;  // e grave
                else if (next == 0xA0)
                    w[i] = 64;  // a grave
                else if (next == 0x91)
                    w[i] = static_cast<char>(161);  // N tilde
                else if (next == 0xB1)
                    w[i] = static_cast<char>(171);  // n tilde
                else if (next == 0xA7)
                {
                    // c cedilla becomes "\\"
                    w[i] = 92;
                    w[i + 1] = 92;
                }
                else
                    error();
                break;
            default:
                error();
                break;
        }
        // drop the next byte unless it is a backslash
        if (i + 1 < w.size() && w[i + 1] && w[i + 1] != 92)
            w.erase(i + 1, 1);
    }
    return w;
}

std::vector<std::string> CheckQuotedChars(std::string_view w)
{
    std::vector<std::string> errors;
    w = w.substr(0, w.find('\0'));
    auto ok = [](char c) { return (c >= 32 && c < 127) || c == '\t'; };
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (ok(w[i]))
            continue;
        // rasm prints up to 15 printable characters before the first bad one at or after i - 15, then that character
        // as a promoted (signed) char
        std::string before;
        size_t at = i >= 15 ? i - 15 : 0;
        for (; at < w.size(); ++at)
        {
            if (w[at] >= 32 && w[at] < 127)
                before.push_back(w[at]);
            else
                break;
        }
        const int shown = at < w.size() ? static_cast<int>(static_cast<std::int8_t>(w[at])) : 0;
        errors.push_back(Format("Invalid char for quoted string [%s(%X)] use -fq option to allow UTF8", before.c_str(),
                                static_cast<unsigned>(shown)));
    }
    return errors;
}

std::array<std::uint8_t, 256> DefaultCharset()
{
    std::array<std::uint8_t, 256> charset;
    for (int i = 0; i < 256; ++i)
        charset[i] = static_cast<std::uint8_t>(i);
    return charset;
}

std::string CharsetMapStrings(std::array<std::uint8_t, 256>& charset, std::string_view from, std::string_view to)
{
    if (from.empty() || to.empty())
        return "CHARSET <string>,<string> must use strings of the same size!";
    const char tquote = from[0];
    const char dquote = to[0];
    size_t i = 1;
    size_t j = 1;
    while (At(from, i) && At(to, j) && At(from, i) != tquote && At(to, j) != dquote)
    {
        if (At(from, i) == '\\')
            ++i;
        if (At(to, j) == '\\')
            ++j;
        charset[U8(At(from, i))] = U8(At(to, j));
        ++i;
        ++j;
    }
    if (At(from, i) != tquote || At(to, j) != dquote)
        return "CHARSET <string>,<string> must use strings of the same size!";
    return {};
}

std::string CharsetMapString(std::array<std::uint8_t, 256>& charset, std::string_view from, int value)
{
    if (from.empty())
        return "CHARSET string,value has invalid quote!";
    const char tquote = from[0];
    if (from.back() != tquote)
        return "CHARSET string,value has invalid quote!";
    size_t i = 1;
    while (At(from, i) && At(from, i) != tquote)
    {
        if (At(from, i) == '\\')
            ++i;
        charset[U8(At(from, i))] = static_cast<std::uint8_t>(value++);
        ++i;
    }
    return {};
}

std::string CharsetMapByte(std::array<std::uint8_t, 256>& charset, int code, int value)
{
    if (code < 0 || code >= 256)
        return "CHARSET byte value must be 0-255";
    charset[code] = static_cast<std::uint8_t>(value);
    return {};
}

std::string CharsetMapRange(std::array<std::uint8_t, 256>& charset, int start, int end, int value)
{
    if (!(start <= end && start >= 0 && end < 256))
        return "CHARSET Winape directive wrong interval value";
    for (int i = start; i <= end; ++i)
        charset[i] = static_cast<std::uint8_t>(value++);
    return {};
}
}  // namespace unrealasm::rasm
