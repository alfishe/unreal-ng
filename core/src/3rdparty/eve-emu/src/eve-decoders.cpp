// eve-emu - selection of built-in / external decoders (arch §5, §6), and the built-in
// decoders' header probes.
#include "eve-internal.h"

#if EVE_HAS_BUILTIN_INFLATE
#include "eve-vendor-tinfl.h"
#endif
#if EVE_HAS_BUILTIN_PNG || EVE_HAS_BUILTIN_JPEG
#include "eve-vendor-stb.h"
#endif

namespace EveLib
{

namespace
{

#if EVE_HAS_BUILTIN_INFLATE

void BuiltinInflateBegin(void* state, void* user)
{
    (void)user;
    EveLibTinflBegin(state, kInflateZlibHeader ? 1 : 0);
}

EveDecodeStatus BuiltinInflateRun(void* state, void* user, const uint8_t* in, size_t inSize, size_t* inUsed,
                                  uint8_t* out, size_t outSize, size_t* outWritten)
{
    (void)user;
    switch (EveLibTinflRun(state, in, inSize, inUsed, out, outSize, outWritten))
    {
    case EVE_LIB_TINFL_DONE:
        return EVE_DECODE_OK;
    case EVE_LIB_TINFL_NEED_INPUT:
        return EVE_DECODE_NEED_INPUT;
    case EVE_LIB_TINFL_OUTPUT_FULL:
        return EVE_DECODE_OUTPUT_FULL;
    default:
        return EVE_DECODE_ERROR;
    }
}

#endif

#if EVE_HAS_BUILTIN_PNG

uint32_t LoadBe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

const uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
// PNG layout: signature, then chunks of length (4), type (4), data, CRC (4); IHDR first.
constexpr size_t kPngSignatureSize = sizeof(kPngSignature);
constexpr size_t kPngChunkOverhead = 12;
constexpr size_t kPngIhdrType = 12;       // offset of the first chunk's type
constexpr size_t kPngIhdrWidth = 16;
constexpr size_t kPngIhdrHeight = 20;
constexpr size_t kPngIhdrBitDepth = 24;
constexpr size_t kPngIhdrColorType = 25;
constexpr size_t kPngIhdrInterlace = 28;
constexpr size_t kPngMinimumSize = 33;    // signature + IHDR chunk
constexpr uint32_t kMaxPaletteEntries = 256;

// PNG color types (IHDR).
constexpr uint8_t kPngGray = 0;
constexpr uint8_t kPngTruecolor = 2;
constexpr uint8_t kPngIndexed = 3;
constexpr uint8_t kPngGrayAlpha = 4;
constexpr uint8_t kPngTruecolorAlpha = 6;

struct PngHeader
{
    uint32_t width = 0, height = 0;
    uint8_t bitDepth = 0, colorType = 0, interlace = 0;
    const uint8_t* palette = nullptr; // PLTE data
    uint32_t paletteSize = 0;         // bytes
    const uint8_t* transparency = nullptr; // tRNS data
    uint32_t transparencySize = 0;
};

bool ParsePng(const uint8_t* data, size_t size, PngHeader& header)
{
    if (size < kPngMinimumSize || std::memcmp(data, kPngSignature, kPngSignatureSize) != 0 ||
        std::memcmp(data + kPngIhdrType, "IHDR", 4) != 0)
        return false;
    header.width = LoadBe32(data + kPngIhdrWidth);
    header.height = LoadBe32(data + kPngIhdrHeight);
    header.bitDepth = data[kPngIhdrBitDepth];
    header.colorType = data[kPngIhdrColorType];
    header.interlace = data[kPngIhdrInterlace];
    size_t pos = kPngSignatureSize;
    while (pos + kPngChunkOverhead <= size)
    {
        const uint32_t length = LoadBe32(data + pos);
        const uint8_t* type = data + pos + 4;
        if (length > size - pos - kPngChunkOverhead)
            return false;
        if (std::memcmp(type, "PLTE", 4) == 0)
        {
            header.palette = data + pos + 8;
            header.paletteSize = length;
        }
        else if (std::memcmp(type, "tRNS", 4) == 0)
        {
            header.transparency = data + pos + 8;
            header.transparencySize = length;
        }
        else if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0)
        {
            break;
        }
        pos += kPngChunkOverhead + length;
    }
    return header.width != 0 && header.height != 0;
}

EveDecodeStatus PngProbe(void* user, const uint8_t* data, size_t size, EveImageInfo* info)
{
    (void)user;
    PngHeader header;
    if (!ParsePng(data, size, header))
        return EVE_DECODE_ERROR;
    *info = EveImageInfo{};
    info->width = header.width;
    info->height = header.height;
    info->bitDepth = header.bitDepth;
    info->interlaced = header.interlace != 0;
    switch (header.colorType)
    {
    case kPngGray:
        info->layout = EVE_IMAGE_GRAY8;
        break;
    case kPngTruecolor:
        info->layout = EVE_IMAGE_RGB888;
        break;
    case kPngIndexed:
        info->layout = EVE_IMAGE_INDEXED8;
        info->paletteHasAlpha = header.transparency != nullptr;
        break;
    case kPngGrayAlpha: // reported as RGBA; the library rejects it by the PNG color type
    case kPngTruecolorAlpha:
        info->layout = EVE_IMAGE_RGBA8888;
        break;
    default:
        return EVE_DECODE_ERROR;
    }
    return EVE_DECODE_OK;
}

EveDecodeStatus PngDecode(void* user, const uint8_t* data, size_t size, uint8_t* pixels, size_t pixelsSize,
                          uint32_t* palette, size_t paletteEntries)
{
    (void)user;
    PngHeader header;
    if (!ParsePng(data, size, header))
        return EVE_DECODE_ERROR;
    const bool indexed = header.colorType == kPngIndexed;
    int channels = 4;
    if (header.colorType == kPngGray)
        channels = 1;
    else if (header.colorType == kPngTruecolor)
        channels = 3;
    const size_t count = static_cast<size_t>(header.width) * header.height;
    if (count * (indexed ? 1u : static_cast<size_t>(channels)) > pixelsSize)
        return EVE_DECODE_ERROR;

    int width = 0;
    int height = 0;
    unsigned char* decoded = StbDecode(data, size, channels, &width, &height);
    if (decoded == nullptr)
        return EVE_DECODE_ERROR;
    if (static_cast<uint32_t>(width) != header.width || static_cast<uint32_t>(height) != header.height)
    {
        StbFree(decoded);
        return EVE_DECODE_ERROR;
    }
    if (!indexed)
    {
        std::memcpy(pixels, decoded, count * static_cast<size_t>(channels));
        StbFree(decoded);
        return EVE_DECODE_OK;
    }

    // stb_image expands the palette; map each pixel back to the first palette entry with
    // the same color (identical picture; duplicate entries resolve to the first one).
    const uint32_t entries = header.paletteSize / 3;
    if (entries == 0 || entries > kMaxPaletteEntries || entries > paletteEntries)
    {
        StbFree(decoded);
        return EVE_DECODE_ERROR;
    }
    for (uint32_t i = 0; i < entries; ++i)
    {
        const uint32_t alpha = i < header.transparencySize ? header.transparency[i] : 0xFF;
        palette[i] = (alpha << 24) | (static_cast<uint32_t>(header.palette[3 * i]) << 16) |
                     (static_cast<uint32_t>(header.palette[3 * i + 1]) << 8) | header.palette[3 * i + 2];
    }
    uint32_t lastColor = 0;
    uint8_t lastIndex = 0;
    bool haveLast = false;
    for (size_t p = 0; p < count; ++p)
    {
        const uint8_t* rgba = decoded + 4 * p;
        const uint32_t color = (static_cast<uint32_t>(rgba[3]) << 24) | (static_cast<uint32_t>(rgba[0]) << 16) |
                               (static_cast<uint32_t>(rgba[1]) << 8) | rgba[2];
        if (!haveLast || color != lastColor)
        {
            uint32_t i = 0;
            while (i < entries && palette[i] != color)
                ++i;
            if (i == entries)
            {
                StbFree(decoded);
                return EVE_DECODE_ERROR;
            }
            lastColor = color;
            lastIndex = static_cast<uint8_t>(i);
            haveLast = true;
        }
        pixels[p] = lastIndex;
    }
    StbFree(decoded);
    return EVE_DECODE_OK;
}

#endif

#if EVE_HAS_BUILTIN_JPEG

// JPEG markers (ITU T.81 Table B.1), the byte after 0xFF.
constexpr uint8_t kJpegMarkerPrefix = 0xFF;
constexpr uint8_t kJpegSoi = 0xD8;
constexpr uint8_t kJpegEoi = 0xD9;
constexpr uint8_t kJpegSos = 0xDA;
constexpr uint8_t kJpegRst0 = 0xD0;
constexpr uint8_t kJpegRst7 = 0xD7;
constexpr uint8_t kJpegTem = 0x01;
constexpr uint8_t kJpegSof0 = 0xC0;
constexpr uint8_t kJpegSof15 = 0xCF;
constexpr uint8_t kJpegDht = 0xC4;
constexpr uint8_t kJpegJpg = 0xC8;
constexpr uint8_t kJpegDac = 0xCC;
constexpr uint8_t kJpegSof2 = 0xC2;  // progressive, Huffman
constexpr uint8_t kJpegSof6 = 0xC6;  // differential progressive, Huffman
constexpr uint8_t kJpegSof10 = 0xCA; // progressive, arithmetic
constexpr uint8_t kJpegSof14 = 0xCE; // differential progressive, arithmetic
constexpr uint32_t kJpegSofMinimumLength = 8;

EveDecodeStatus JpegProbe(void* user, const uint8_t* data, size_t size, EveImageInfo* info)
{
    (void)user;
    if (size < 4 || data[0] != kJpegMarkerPrefix || data[1] != kJpegSoi)
        return EVE_DECODE_ERROR;
    size_t pos = 2;
    while (pos + 4 <= size)
    {
        if (data[pos] != kJpegMarkerPrefix)
            return EVE_DECODE_ERROR;
        const uint8_t marker = data[pos + 1];
        if (marker == kJpegMarkerPrefix)
        {
            ++pos; // fill byte
            continue;
        }
        if (marker == kJpegSoi || (marker >= kJpegRst0 && marker <= kJpegRst7) || marker == kJpegTem)
        {
            pos += 2;
            continue;
        }
        const uint32_t length = (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
        if (length < 2 || pos + 2 + length > size)
            return EVE_DECODE_ERROR;
        const bool frame = marker >= kJpegSof0 && marker <= kJpegSof15 && marker != kJpegDht && marker != kJpegJpg &&
                           marker != kJpegDac;
        if (frame)
        {
            if (length < kJpegSofMinimumLength)
                return EVE_DECODE_ERROR;
            const uint8_t* sof = data + pos + 4;
            *info = EveImageInfo{};
            info->bitDepth = sof[0];
            info->height = (static_cast<uint32_t>(sof[1]) << 8) | sof[2];
            info->width = (static_cast<uint32_t>(sof[3]) << 8) | sof[4];
            const uint8_t components = sof[5];
            info->progressive =
                (marker == kJpegSof2 || marker == kJpegSof6 || marker == kJpegSof10 || marker == kJpegSof14) ? 1 : 0;
            info->cmyk = components == 4 ? 1 : 0;
            info->layout = components == 1 ? EVE_IMAGE_GRAY8 : EVE_IMAGE_RGB888;
            if (components != 1 && components != 3 && components != 4)
                return EVE_DECODE_ERROR;
            return EVE_DECODE_OK;
        }
        if (marker == kJpegSos || marker == kJpegEoi)
            return EVE_DECODE_ERROR; // scan or end before a frame header
        pos += 2 + length;
    }
    return EVE_DECODE_ERROR;
}

EveDecodeStatus JpegDecode(void* user, const uint8_t* data, size_t size, uint8_t* pixels, size_t pixelsSize,
                           uint32_t* palette, size_t paletteEntries)
{
    (void)palette;
    (void)paletteEntries;
    EveImageInfo info;
    if (JpegProbe(user, data, size, &info) != EVE_DECODE_OK)
        return EVE_DECODE_ERROR;
    const int channels = info.layout == EVE_IMAGE_GRAY8 ? 1 : 3;
    const size_t bytes = static_cast<size_t>(info.width) * info.height * static_cast<size_t>(channels);
    if (bytes > pixelsSize)
        return EVE_DECODE_ERROR;
    int width = 0;
    int height = 0;
    unsigned char* decoded = StbDecode(data, size, channels, &width, &height);
    if (decoded == nullptr)
        return EVE_DECODE_ERROR;
    const bool sizeMatches = static_cast<uint32_t>(width) == info.width && static_cast<uint32_t>(height) == info.height;
    if (sizeMatches)
        std::memcpy(pixels, decoded, bytes);
    StbFree(decoded);
    return sizeMatches ? EVE_DECODE_OK : EVE_DECODE_ERROR;
}

#endif

bool ValidInflate(const EveInflateDecoder* d)
{
    return d->Begin != nullptr && d->Run != nullptr && d->stateSize != 0;
}

bool ValidImage(const EveImageDecoder* d)
{
    return d->Probe != nullptr && d->Decode != nullptr;
}

} // namespace

bool ResolveDecoders(EveChip& chip, const EveDecoders* decoders)
{
    const EveInflateDecoder* inflate = nullptr;
    const EveImageDecoder* png = nullptr;
    const EveImageDecoder* jpeg = nullptr;
    if (decoders != nullptr)
    {
        if (decoders->structSize < sizeof(EveDecoders))
        {
            SetLastError("EveCreate: decoders->structSize is smaller than sizeof(EveDecoders)");
            return false;
        }
        inflate = decoders->inflate;
        png = decoders->png;
        jpeg = decoders->jpeg;
    }
    if (inflate == nullptr)
        inflate = EveBuiltinInflate();
    if (png == nullptr)
        png = EveBuiltinPng();
    if (jpeg == nullptr)
        jpeg = EveBuiltinJpeg();
    if (inflate == nullptr || !ValidInflate(inflate))
    {
        SetLastError("EveCreate: no inflate decoder (built with EVE_DECODER_INFLATE=EXTERNAL and none passed)");
        return false;
    }
    if (png == nullptr || !ValidImage(png))
    {
        SetLastError("EveCreate: no PNG decoder (built with EVE_DECODER_PNG=EXTERNAL and none passed)");
        return false;
    }
    if (jpeg == nullptr || !ValidImage(jpeg))
    {
        SetLastError("EveCreate: no JPEG decoder (built with EVE_DECODER_JPEG=EXTERNAL and none passed)");
        return false;
    }
    chip.inflate = inflate;
    chip.png = png;
    chip.jpeg = jpeg;
    return true;
}

} // namespace EveLib

extern "C" {

const EveInflateDecoder* EveBuiltinInflate(void)
{
#if EVE_HAS_BUILTIN_INFLATE
    static const EveInflateDecoder decoder = {EveLibTinflStateSize(), 1, EveLib::BuiltinInflateBegin,
                                              EveLib::BuiltinInflateRun, nullptr};
    return &decoder;
#else
    return nullptr;
#endif
}

const EveImageDecoder* EveBuiltinPng(void)
{
#if EVE_HAS_BUILTIN_PNG
    static const EveImageDecoder decoder = {EveLib::PngProbe, EveLib::PngDecode, nullptr};
    return &decoder;
#else
    return nullptr;
#endif
}

const EveImageDecoder* EveBuiltinJpeg(void)
{
#if EVE_HAS_BUILTIN_JPEG
    static const EveImageDecoder decoder = {EveLib::JpegProbe, EveLib::JpegDecode, nullptr};
    return &decoder;
#else
    return nullptr;
#endif
}

} // extern "C"
