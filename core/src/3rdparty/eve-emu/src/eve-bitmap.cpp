// eve-emu - bitmap formats, sampling, filters, wrap, text formats (spec §6.5).
#include "eve-profile.h"
#include "eve-render.h"
#include "eve-simd.h"

#include <initializer_list>
#include <memory>

namespace EveLib
{

namespace
{

constexpr uint32_t kBitsPerByte = 8;
constexpr uint32_t kRedShift = 16, kGreenShift = 8;
constexpr uint32_t kTextCell = 8;           // TEXT8X8 / TEXTVGA cell width in pixels
constexpr uint32_t kText8x8Height = 8;
constexpr uint32_t kTextVgaHeight = 16;
constexpr uint32_t kTextVgaCellBytes = 2;   // character, attribute
constexpr uint32_t kHighCharacters = 0x80;  // fonts 17 / 19 hold characters 0x80...0xFF
constexpr uint32_t kFont8x8 = 16;           // ROM font handles used by the text formats
constexpr uint32_t kFont8x16 = 18;
constexpr uint32_t kPalette16Bytes = 2;
constexpr uint32_t kPalette32Bytes = 4;
constexpr uint32_t kBytes16 = 2;           // bytes of a 16-bit pixel
constexpr uint32_t kWhiteRgb = 0xFFFFFF;


struct Rgba
{
    uint32_t r, g, b, a;
};

constexpr Rgba kTransparent{0, 0, 0, 0};

// n-bit channel to 8 bits (spec §6.5, V3).
uint32_t Expand(uint32_t value, uint32_t bits)
{
    const uint32_t shift = kBitsPerByte - bits;
    if (!kExpandByReplication)
        return value << shift;
    uint32_t out = 0;
    for (int32_t pos = static_cast<int32_t>(shift); pos > -static_cast<int32_t>(bits); pos -= static_cast<int32_t>(bits))
        out |= pos >= 0 ? value << pos : value >> -pos;
    return out & kChannelMax;
}

uint32_t ReadByte(const EveChip& chip, uint32_t address)
{
    address &= kAddressMask;
    if (address < kRamGSize)
        return chip.regions[RegionRamG].base[address];
    if (address >= kRomFontBase && address < kRomEnd)
        return RomByte(chip, address);
    return 0;
}

// The bytes [start, end) as ReadByte reads them, contiguous, or nullptr: RAM_G, the ROM
// image, or the ROM area without an image (reads 0 up to the font root word; CMD_GRADIENT
// draws a ramp from there)
const uint8_t* LayoutBytes(const EveChip& chip, uint64_t start, uint64_t end)
{
    static const uint8_t kZeros[64 * 1024] = {};
    if (end <= start)
        return nullptr;
    if (end <= kRamGSize)
        return chip.regions[RegionRamG].base + start;
    if (start < kRomFontBase || end > kRomEnd)
        return nullptr;
    if (chip.romImage != nullptr)
        return start >= chip.romBase ? chip.romImage + (start - chip.romBase) : nullptr;
    if (end <= kRomFontRootAddress && end - start <= sizeof(kZeros))
        return kZeros;
    return nullptr;
}

uint32_t Read16(const EveChip& chip, uint32_t address)
{
    return ReadByte(chip, address) | (ReadByte(chip, address + 1) << kBitsPerByte);
}

uint32_t BitsPerPixel(uint8_t format)
{
    constexpr uint32_t kOne = 1, kTwo = 2, kFour = 4, kSixteen = 16;
    switch (format)
    {
    case kFormatL1: return kOne;
    case kFormatL2: return kTwo;
    case kFormatL4: return kFour;
    case kFormatArgb1555:
    case kFormatArgb4:
    case kFormatRgb565:
    case kFormatTextVga: return kSixteen;
    default: return kBitsPerByte;
    }
}

Rgba Luminance(uint32_t l)
{
    return Rgba{kChannelMax, kChannelMax, kChannelMax, l};
}

// Channel layout of the direct-color formats [PG §4.7 Table 7, Figures 6-9]: shift and
// width of each channel in the pixel; an alpha width of 0 means opaque.
struct Channel
{
    uint32_t shift, bits;
};

struct DirectLayout
{
    Channel red, green, blue, alpha;
};

constexpr DirectLayout kArgb1555Layout{{10, 5}, {5, 5}, {0, 5}, {15, 1}};
constexpr DirectLayout kRgb332Layout{{5, 3}, {2, 3}, {0, 2}, {0, 0}};
constexpr DirectLayout kArgb2Layout{{4, 2}, {2, 2}, {0, 2}, {6, 2}};
constexpr DirectLayout kArgb4Layout{{8, 4}, {4, 4}, {0, 4}, {12, 4}};
constexpr DirectLayout kRgb565Layout{{11, 5}, {5, 6}, {0, 5}, {0, 0}};

uint32_t ChannelValue(uint32_t pixel, Channel c)
{
    return Expand((pixel >> c.shift) & ((1u << c.bits) - 1), c.bits);
}

Rgba Direct(uint32_t pixel, const DirectLayout& layout)
{
    return Rgba{ChannelValue(pixel, layout.red), ChannelValue(pixel, layout.green), ChannelValue(pixel, layout.blue),
                layout.alpha.bits != 0 ? ChannelValue(pixel, layout.alpha) : kChannelMax};
}

// Luminance with 1, 2, 4 or 8 bits per pixel; pixel 0 in the high bits of a byte.
Rgba Packed(const EveChip& chip, uint32_t row, uint32_t x, uint32_t bits)
{
    const uint32_t perByte = kBitsPerByte / bits;
    const uint32_t shift = kBitsPerByte - bits * (1 + x % perByte);
    return Luminance(Expand((ReadByte(chip, row + x / perByte) >> shift) & ((1u << bits) - 1), bits));
}

// A glyph pixel of a ROM text font: white where the bit is set.
Rgba Glyph(const EveChip& chip, uint32_t fontHandle, uint32_t character, uint32_t gx, uint32_t gy, uint32_t height)
{
    const uint32_t handle = character >= kHighCharacters ? fontHandle + 1 : fontHandle;
    const uint32_t index = character >= kHighCharacters ? character - kHighCharacters : character;
    const BitmapHandle& font = chip.state.handles[handle];
    const uint32_t row = ReadByte(chip, font.source + index * height + gy);
    return Luminance(((row >> (kBitsPerByte - 1 - gx)) & 1) ? kChannelMax : 0);
}

// The 16 colors of a TEXTVGA attribute's low nibble: bit 0 blue, 1 green, 2 red at 0xAA,
// bit 3 adds 0x55 to each; color 6 is brown (green 0x55).
Rgba VgaColor(uint32_t index)
{
    constexpr uint32_t kBlueBit = 1, kGreenBit = 2, kRedBit = 4, kBrightBit = 8, kBrown = 6;
    constexpr uint32_t kOn = 0xAA, kBright = 0x55, kBrownGreen = 0x55;
    const uint32_t bright = (index & kBrightBit) ? kBright : 0;
    const uint32_t r = ((index & kRedBit) ? kOn : 0) + bright;
    const uint32_t g = index == kBrown ? kBrownGreen : ((index & kGreenBit) ? kOn : 0) + bright;
    const uint32_t b = ((index & kBlueBit) ? kOn : 0) + bright;
    return Rgba{r, g, b, kChannelMax};
}

// The texel at (tx, ty) of a bitmap, inside its layout.
Rgba Texel(const LineRun& run, const BitmapHandle& h, uint32_t base, uint32_t stride, int32_t tx, int32_t ty)
{
    const EveChip& chip = *run.chip;
    const uint32_t x = static_cast<uint32_t>(tx);
    const uint32_t y = static_cast<uint32_t>(ty);
    const uint32_t row = base + y * stride;
    switch (h.format)
    {
    case kFormatArgb1555:
        return Direct(Read16(chip, row + kBytes16 * x), kArgb1555Layout);
    case kFormatL1:
    case kFormatL2:
    case kFormatL4:
    case kFormatL8:
        return Packed(chip, row, x, BitsPerPixel(h.format));
    case kFormatRgb332:
        return Direct(ReadByte(chip, row + x), kRgb332Layout);
    case kFormatArgb2:
        return Direct(ReadByte(chip, row + x), kArgb2Layout);
    case kFormatArgb4:
        return Direct(Read16(chip, row + kBytes16 * x), kArgb4Layout);
    case kFormatRgb565:
        return Direct(Read16(chip, row + kBytes16 * x), kRgb565Layout);
    case kFormatPaletted565:
        return Direct(Read16(chip, run.ctx.paletteSource + kPalette16Bytes * ReadByte(chip, row + x)), kRgb565Layout);
    case kFormatPaletted4444:
        return Direct(Read16(chip, run.ctx.paletteSource + kPalette16Bytes * ReadByte(chip, row + x)), kArgb4Layout);
    case kFormatPaletted8:
    {
        // One channel per pass: PALETTE_SOURCE's offset picks the byte, COLOR_MASK the
        // channel it goes to [PG §4.7 note].
        const uint32_t v = ReadByte(chip, run.ctx.paletteSource + kPalette32Bytes * ReadByte(chip, row + x));
        return Rgba{v, v, v, v};
    }
    case kFormatText8x8:
    {
        const uint32_t character = ReadByte(chip, base + (y / kText8x8Height) * stride + x / kTextCell);
        return Glyph(chip, kFont8x8, character, x % kTextCell, y % kText8x8Height, kText8x8Height);
    }
    case kFormatTextVga:
    {
        // A set glyph pixel takes the VGA color of the attribute's low nibble, opaque; the
        // rest is transparent; the high nibble has no effect (BT8XX, golden cases
        // format-text*).
        constexpr uint32_t kForegroundMask = 0xF;
        const uint32_t cell = base + (y / kTextVgaHeight) * stride + (x / kTextCell) * kTextVgaCellBytes;
        const uint32_t character = ReadByte(chip, cell);
        const Rgba glyph = Glyph(chip, kFont8x16, character, x % kTextCell, y % kTextVgaHeight, kTextVgaHeight);
        return glyph.a != 0 ? VgaColor(ReadByte(chip, cell + 1) & kForegroundMask) : kTransparent;
    }
    case kFormatBargraph:
        // Opaque if the byte at x is less than y [PG §4.7].
        return ReadByte(chip, base + x) < y ? Luminance(kChannelMax) : kTransparent;
    default:
        return kTransparent;
    }
}

// Texel with the wrap mode applied; BORDER outside the layout is transparent.
Rgba WrappedTexel(const LineRun& run, const BitmapHandle& h, uint32_t base, uint32_t stride, uint32_t layoutWidth,
                  uint32_t layoutHeight, int32_t tx, int32_t ty)
{
    // A BARGRAPH bitmap is 256 texels high whatever its layout says (BT8XX, golden case
    // format-text-bargraph).
    constexpr uint32_t kBargraphHeight = 256;
    if (h.format == kFormatBargraph)
        layoutHeight = kBargraphHeight;
    if (layoutWidth == 0 || layoutHeight == 0)
        return kTransparent;
    const int32_t w = static_cast<int32_t>(layoutWidth);
    const int32_t hgt = static_cast<int32_t>(layoutHeight);
    if (h.wrapX)
        tx = ((tx % w) + w) % w;
    else if (tx < 0 || tx >= w)
        return kTransparent;
    if (h.wrapY)
        ty = ((ty % hgt) + hgt) % hgt;
    else if (ty < 0 || ty >= hgt)
        return kTransparent;
    return Texel(run, h, base, stride, tx, ty);
}

int32_t FloorDiv(int64_t value, int64_t divisor)
{
    int64_t q = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0)))
        --q;
    return static_cast<int32_t>(q);
}

// BILINEAR weights out of 256 for the fractions fx, fy (0...255) of the four texels
// 00, 10, 01, 11: the 11 weight is fx x fy rounded down, the others make up fx, fy and
// 256; each weighted texel is rounded down before the sum, so an opaque bitmap loses a
// little alpha between texels (BT8XX, golden cases bilinear-*, bitmap-format-*).
struct BilinearWeights
{
    uint32_t w00, w10, w01, w11;
};

BilinearWeights Weights(uint32_t fx, uint32_t fy)
{
    const uint32_t w11 = (fx * fy) >> kFixedShift;
    return BilinearWeights{kFixedOneTransform - fx - fy + w11, fx - w11, fy - w11, w11};
}

uint32_t Blend4(const BilinearWeights& w, uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11)
{
    return ((c00 * w.w00) >> kFixedShift) + ((c10 * w.w10) >> kFixedShift) + ((c01 * w.w01) >> kFixedShift) +
           ((c11 * w.w11) >> kFixedShift);
}

// --- Fast path (arch §8.4): identity matrix, NEAREST, BORDER, common formats -------------
//
// The same arithmetic as the general path, reorganized: texels come from a row pointer,
// 16-bit pixels from decode tables, paletted pixels from a palette decoded once per line,
// and the default blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) with its exact shortcuts.

constexpr uint32_t kTableEntries = 1u << 16;
constexpr uint32_t kPackRed = 24, kPackGreen = 16, kPackBlue = 8; // RGBA packed in a uint32

uint32_t Pack(const Rgba& c)
{
    return (c.r << kPackRed) | (c.g << kPackGreen) | (c.b << kPackBlue) | c.a;
}

Rgba Unpack(uint32_t p)
{
    return Rgba{(p >> kPackRed) & kChannelMax, (p >> kPackGreen) & kChannelMax, (p >> kPackBlue) & kChannelMax,
                p & kChannelMax};
}

struct DecodeTable
{
    std::unique_ptr<uint32_t[]> entry;
    explicit DecodeTable(const DirectLayout& layout) : entry(new uint32_t[kTableEntries])
    {
        for (uint32_t v = 0; v < kTableEntries; ++v)
            entry[v] = Pack(Direct(v, layout));
    }
};

const uint32_t* TableFor(uint8_t format)
{
    static const DecodeTable argb4(kArgb4Layout);
    static const DecodeTable rgb565(kRgb565Layout);
    static const DecodeTable argb1555(kArgb1555Layout);
    switch (format)
    {
    case kFormatArgb4: return argb4.entry.get();
    case kFormatRgb565: return rgb565.entry.get();
    case kFormatArgb1555: return argb1555.entry.get();
    default: return nullptr;
    }
}

// The 8-bit direct formats: one table of 256 packed texels each
constexpr uint32_t kByteTableEntries = 256;

struct ByteTable
{
    uint32_t entry[kByteTableEntries];
    explicit ByteTable(const DirectLayout& layout)
    {
        for (uint32_t v = 0; v < kByteTableEntries; ++v)
            entry[v] = Pack(Direct(v, layout));
    }
};

const uint32_t* ByteTableFor(uint8_t format)
{
    static const ByteTable rgb332(kRgb332Layout);
    static const ByteTable argb2(kArgb2Layout);
    switch (format)
    {
    case kFormatRgb332: return rgb332.entry;
    case kFormatArgb2: return argb2.entry;
    default: return nullptr;
    }
}

// Packed luminance (L1, L2, L4, L8): the texel for each of the 2^bits values
struct LuminanceTable
{
    uint32_t entry[kByteTableEntries];
    explicit LuminanceTable(uint32_t bits)
    {
        for (uint32_t v = 0; v < (1u << bits); ++v)
            entry[v] = Pack(Luminance(Expand(v, bits)));
    }
};

const uint32_t* LuminanceTableFor(uint8_t format)
{
    static const LuminanceTable l1(1), l2(2), l4(4), l8(8);
    switch (format)
    {
    case kFormatL1: return l1.entry;
    case kFormatL2: return l2.entry;
    case kFormatL4: return l4.entry;
    case kFormatL8: return l8.entry;
    default: return nullptr;
    }
}

bool FastFormat(uint8_t format)
{
    return format == kFormatArgb4 || format == kFormatRgb565 || format == kFormatArgb1555 ||
           format == kFormatPaletted4444 || format == kFormatPaletted565 || format == kFormatPaletted8 || format == kFormatL1 ||
           format == kFormatL2 || format == kFormatL4 || format == kFormatL8 || format == kFormatRgb332 ||
           format == kFormatArgb2;
}

bool DefaultPipeline(const GraphicsContext& ctx)
{
    constexpr uint8_t kAllChannels = kMaskRed | kMaskGreen | kMaskBlue | kMaskAlpha;
    return ctx.alphaFunc == kFuncAlways && ctx.stencilFunc == kFuncAlways && ctx.stencilPass == kStencilKeep &&
           ctx.blendSrc == kBlendSrcAlpha && ctx.blendDst == kBlendOneMinusSrcAlpha && ctx.colorMask == kAllChannels;
}

// Palette of the line run, decoded once per line (memory does not change during a line).
const uint32_t* LinePalette(LineRun& run, uint8_t format)
{
    EveChip& chip = *run.chip;
    const uint32_t source = run.ctx.paletteSource;
    for (uint32_t k = 0; k < kPaletteCacheEntries; ++k)
    {
        const PaletteCache& c = chip.palettes[k];
        if (c.valid && c.source == source && c.format == format && c.ramGWrites == chip.ramGWrites)
            return c.entry;
    }
    PaletteCache& cache = chip.palettes[chip.paletteNext];
    chip.paletteNext = (chip.paletteNext + 1) % kPaletteCacheEntries;
    if (format == kFormatPaletted8)
    {
        // One byte per entry, 4 bytes apart (PALETTE_SOURCE's offset picks the channel
        // byte), in every channel: Rgba{v, v, v, v} as the general path samples it
        const bool inRamG = static_cast<uint64_t>(source) + kPalette32Bytes * (kPaletteEntries - 1) + 1 <= kRamGSize;
        for (uint32_t i = 0; i < kPaletteEntries; ++i)
        {
            const uint32_t v = inRamG ? chip.regions[RegionRamG].base[source + kPalette32Bytes * i]
                                      : ReadByte(chip, source + kPalette32Bytes * i);
            cache.entry[i] = v * 0x01010101u;
        }
        cache.valid = inRamG;
        cache.source = source;
        cache.format = format;
        cache.ramGWrites = chip.ramGWrites;
        return cache.entry;
    }
    // The 16-bit decode tables hold exactly Pack(Direct(v, layout)) for every v
    const uint32_t* table = TableFor(format == kFormatPaletted565 ? kFormatRgb565 : kFormatArgb4);
    const bool inRamG = static_cast<uint64_t>(source) + kPalette16Bytes * kPaletteEntries <= kRamGSize;
    if (inRamG)
    {
        const uint8_t* p = chip.regions[RegionRamG].base + source;
        for (uint32_t i = 0; i < kPaletteEntries; ++i)
            cache.entry[i] = table[p[2 * i] | (static_cast<uint32_t>(p[2 * i + 1]) << kBitsPerByte)];
    }
    else
    {
        for (uint32_t i = 0; i < kPaletteEntries; ++i)
            cache.entry[i] = table[Read16(chip, source + kPalette16Bytes * i)];
    }
    // Outside RAM_G (registers, the ROM) the palette is not kept across lines
    cache.valid = inRamG;
    cache.source = source;
    cache.format = format;
    cache.ramGWrites = chip.ramGWrites;
    return cache.entry;
}

// Default blend of one pixel; exact shortcuts for alpha 0 and 255.
void BlendDefault(LineRun& run, int32_t x, uint32_t rgba)
{
    const uint32_t a = rgba & kChannelMax;
    if (WritesTag(run))
        run.tag[x] = run.ctx.tag;
    if (a == 0)
        return; // x 0 + dst x 255: the destination stays
    uint8_t* dst = run.color + kChannels * static_cast<uint32_t>(x);
    const uint32_t r = (rgba >> kPackRed) & kChannelMax;
    const uint32_t g = (rgba >> kPackGreen) & kChannelMax;
    const uint32_t b = (rgba >> kPackBlue) & kChannelMax;
    if (a == kChannelMax)
    {
        dst[0] = static_cast<uint8_t>(r);
        dst[1] = static_cast<uint8_t>(g);
        dst[2] = static_cast<uint8_t>(b);
        dst[3] = static_cast<uint8_t>(kChannelMax);
        return;
    }
    const uint32_t inverse = kChannelMax - a;
    const uint32_t source[kChannels] = {r, g, b, a};
    // SIMD-CANDIDATE: four channels of one blend.
    for (uint32_t c = 0; c < kChannels; ++c)
    {
        const uint32_t value = static_cast<uint32_t>(Multiply(source[c], a)) + Multiply(dst[c], inverse);
        dst[c] = static_cast<uint8_t>(value > kChannelMax ? kChannelMax : value);
    }
}

// The texels of a span: x' starts at sx and steps by a (1/256 texel) per pixel; BORDER
// leaves texels outside [0, width) transparent, REPEAT wraps them as the general path does
template <typename Fetch>
void DecodeSpanWith(uint32_t* out, uint32_t count, int64_t sx, int64_t a, int32_t width, bool wrapX, Fetch fetch)
{
    if (wrapX)
    {
        for (uint32_t i = 0; i < count; ++i, sx += a)
        {
            int32_t tx = static_cast<int32_t>(sx >> kFixedShift);
            tx = ((tx % width) + width) % width;
            out[i] = fetch(static_cast<uint32_t>(tx));
        }
        return;
    }
    for (uint32_t i = 0; i < count; ++i, sx += a)
    {
        const int32_t tx = static_cast<int32_t>(sx >> kFixedShift);
        out[i] = (tx >= 0 && tx < width) ? fetch(static_cast<uint32_t>(tx)) : 0;
    }
}

template <typename Fetch>
void DecodeSpan(uint32_t* out, uint32_t count, int64_t sx, int64_t a, int32_t width, bool wrapX, Fetch fetch)
{
    DecodeSpanWith(out, count, sx, a, width, wrapX, fetch);
}

// Returns false when the fast path does not apply; the general path then draws the span.
//
// A layout of zero width or height samples transparent black everywhere (WrappedTexel):
// every pixel of the span goes through the pipeline with a transparent texel. Under the
// default pipeline (alpha test and stencil always passing and keeping, SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA, all channels) that leaves the colour exactly as it was and only
// writes the tag; any other pipeline gets the transparent texels through ShadeSpan.
// Zuma's loading screen draws dozens of such bitmaps on every line.
bool DrawTransparentSpan(LineRun& run, int32_t first, int32_t last)
{
    const uint32_t count = static_cast<uint32_t>(last - first);
    if (DefaultPipeline(run.ctx))
    {
        if (WritesTag(run))
            std::memset(run.tag + first, run.ctx.tag, count);
        return true;
    }
    std::memset(run.texels, 0, count * sizeof(uint32_t));
    ShadeSpan(run, first, run.texels, count);
    return true;
}

// Applies to NEAREST sampling (BORDER or REPEAT on each axis) and an axis-aligned matrix
// (B = D = 0: identity, scaling, mirroring), from a layout inside RAM_G. The texel row is
// then the same for the whole span and x' steps by A per pixel - exactly the general
// path's sequence of sample positions and its wrap arithmetic, with the texels decoded
// from tables.
// Calls sink(fetch) with the decoder of one layout row in the handle's format: fetch(u)
// is the packed texel at column u (16-bit formats through tables, paletted ones through
// the line's palette, the luminance formats through their tables)
template <typename Sink>
void WithRowFetch(LineRun& run, const BitmapHandle& h, const uint8_t* row, Sink sink)
{
    const uint32_t* table16 = TableFor(h.format);
    const uint32_t* table8 = ByteTableFor(h.format);
    const uint32_t* luminance = LuminanceTableFor(h.format);
    const uint32_t* palette =
        (h.format == kFormatPaletted4444 || h.format == kFormatPaletted565 || h.format == kFormatPaletted8)
            ? LinePalette(run, h.format)
            : nullptr;
    const uint32_t bpp = BitsPerPixel(h.format);
    if (table16 != nullptr)
        sink([row, table16](uint32_t u) { return table16[row[2 * u] | (static_cast<uint32_t>(row[2 * u + 1]) << kBitsPerByte)]; });
    else if (palette != nullptr)
        sink([row, palette](uint32_t u) { return palette[row[u]]; });
    else if (table8 != nullptr)
        sink([row, table8](uint32_t u) { return table8[row[u]]; });
    else if (bpp == kBitsPerByte)
        sink([row, luminance](uint32_t u) { return luminance[row[u]]; });
    else
    {
        // Pixel 0 in the high bits of a byte
        const uint32_t lumMask = (1u << bpp) - 1;
        const uint32_t perByteShift = bpp == 1 ? 3 : bpp == 2 ? 2 : 1; // log2(pixels per byte)
        const uint32_t indexMask = (1u << perByteShift) - 1;
        sink([=](uint32_t u) {
            const uint32_t shift = kBitsPerByte - bpp * (1 + (u & indexMask));
            return luminance[(row[u >> perByteShift] >> shift) & lumMask];
        });
    }
}

// Calls sink(fetch) with the decoder of the whole layout: fetch(u, v) is the packed texel at
// column u of row v (rows `stride` bytes apart from `layout`), in the handle's format
template <typename Sink>
void WithLayoutFetch(LineRun& run, const BitmapHandle& h, const uint8_t* layout, uint32_t stride, Sink sink)
{
    const uint32_t* table16 = TableFor(h.format);
    const uint32_t* table8 = ByteTableFor(h.format);
    const uint32_t* luminance = LuminanceTableFor(h.format);
    const uint32_t* palette =
        (h.format == kFormatPaletted4444 || h.format == kFormatPaletted565 || h.format == kFormatPaletted8)
            ? LinePalette(run, h.format)
            : nullptr;
    const uint32_t bpp = BitsPerPixel(h.format);
    if (table16 != nullptr)
        sink([layout, stride, table16](uint32_t u, uint32_t v) {
            const uint8_t* p = layout + static_cast<size_t>(v) * stride + 2 * u;
            return table16[p[0] | (static_cast<uint32_t>(p[1]) << kBitsPerByte)];
        });
    else if (palette != nullptr)
        sink([layout, stride, palette](uint32_t u, uint32_t v) { return palette[layout[static_cast<size_t>(v) * stride + u]]; });
    else if (table8 != nullptr)
        sink([layout, stride, table8](uint32_t u, uint32_t v) { return table8[layout[static_cast<size_t>(v) * stride + u]]; });
    else if (bpp == kBitsPerByte)
        sink([layout, stride, luminance](uint32_t u, uint32_t v) { return luminance[layout[static_cast<size_t>(v) * stride + u]]; });
    else
    {
        const uint32_t lumMask = (1u << bpp) - 1;
        const uint32_t perByteShift = bpp == 1 ? 3 : bpp == 2 ? 2 : 1;
        const uint32_t indexMask = (1u << perByteShift) - 1;
        sink([=](uint32_t u, uint32_t v) {
            const uint32_t shift = kBitsPerByte - bpp * (1 + (u & indexMask));
            return luminance[(layout[static_cast<size_t>(v) * stride + (u >> perByteShift)] >> shift) & lumMask];
        });
    }
}

// The end of every fast span: the texels times COLOR_RGB / COLOR_A, then through the
// pipeline (the default blend's exact shortcuts, or ShadeSpan)
void FinishFastSpan(LineRun& run, int32_t first, uint32_t* texels, uint32_t count)
{
    const GraphicsContext& ctx = run.ctx;
    const bool modulate = ctx.colorRgb != kWhiteRgb || ctx.colorA != kChannelMax;
    const uint32_t colorR = (ctx.colorRgb >> kRedShift) & kChannelMax;
    const uint32_t colorG = (ctx.colorRgb >> kGreenShift) & kChannelMax;
    const uint32_t colorB = ctx.colorRgb & kChannelMax;
#ifdef EVE_PROFILE
    Profile().finishPixels += count;
    Profile().modulatePixels += modulate ? count : 0;
    Profile().simdBlendPixels += DefaultPipeline(ctx) && kMultiplyRoundDiv255 ? count : 0;
#endif
    if (modulate && kMultiplyRoundDiv255)
        Simd::Modulate(texels, count, colorR, colorG, colorB, ctx.colorA);
    else if (modulate)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const Rgba c = Unpack(texels[i]);
            texels[i] = Pack(Rgba{Multiply(c.r, colorR), Multiply(c.g, colorG), Multiply(c.b, colorB),
                                  Multiply(c.a, ctx.colorA)});
        }
    }
    BlendSpan(run, first, texels, count);
}

// One layout row's texels for the columns lo .. lo + n - 1: BORDER leaves columns outside
// [0, width) transparent, REPEAT wraps them, as WrappedTexel does
template <typename Fetch>
void DecodeColumns(uint32_t* out, int64_t lo, uint32_t n, int32_t width, bool wrapX, Fetch fetch)
{
    for (uint32_t c = 0; c < n; ++c)
    {
        int64_t col = lo + c;
        if (wrapX)
            col = ((col % width) + width) % width;
        else if (col < 0 || col >= width)
        {
            out[c] = 0;
            continue;
        }
        out[c] = fetch(static_cast<uint32_t>(col));
    }
}

// BILINEAR on an axis-aligned matrix (B = D = 0): the two texel rows (ty, ty + 1) are the
// same for the whole span, so their columns are decoded once into buffers, and each pixel
// blends four of them with the general path's weights and per-term rounding (Weights,
// Blend4). Rows or columns outside the layout are transparent under BORDER and wrap under
// REPEAT, exactly as WrappedTexel samples them.
bool DrawBitmapBilinearFast(LineRun& run, const BitmapHandle& h, uint32_t base, uint32_t stride, uint32_t layoutWidth,
                            uint32_t layoutHeight, int64_t rely, int32_t first, int32_t last, int64_t vx)
{
    const int32_t* t = run.ctx.transform;
    const int64_t a = t[0];
    const int64_t relFirst = static_cast<int64_t>(first) * kSubpixel + kPixelCenter - vx;
    const int64_t bx0 = ((a * relFirst) >> kSubpixelShift) + t[2] - kBilinearOffset;
    const int64_t by = ((static_cast<int64_t>(t[4]) * rely) >> kSubpixelShift) + t[5] - kBilinearOffset;
    const int32_t ty = static_cast<int32_t>(by >> kFixedShift);
    const uint32_t fy = static_cast<uint32_t>(by - static_cast<int64_t>(ty) * kFixedOneTransform);
    const uint32_t count = static_cast<uint32_t>(last - first);

    // The columns the span samples: tx and tx + 1 of its first and last pixel
    const int64_t txFirst = bx0 >> kFixedShift;
    const int64_t txLast = (bx0 + a * static_cast<int64_t>(count - 1)) >> kFixedShift;
    const int64_t lo = txFirst < txLast ? txFirst : txLast;
    const int64_t hi = (txFirst < txLast ? txLast : txFirst) + 1;
    if (hi - lo + 1 > static_cast<int64_t>(kBilinearColumns))
        return false;
    const uint32_t n = static_cast<uint32_t>(hi - lo + 1);

    // The two rows, as WrappedTexel takes them
    const int32_t rows = static_cast<int32_t>(layoutHeight);
    const int32_t width = static_cast<int32_t>(layoutWidth);
    const bool wrapX = h.wrapX != 0;
    const uint64_t rowBytes = (static_cast<uint64_t>(layoutWidth) * BitsPerPixel(h.format) + kBitsPerByte - 1) / kBitsPerByte;
    uint32_t* columns[2] = {run.bilinear, run.bilinear + kBilinearColumns};
    for (int k = 0; k < 2; ++k)
    {
        int32_t r = ty + k;
        if (h.wrapY)
            r = ((r % rows) + rows) % rows;
        else if (r < 0 || r >= rows)
        {
            std::memset(columns[k], 0, n * sizeof(uint32_t));
            continue;
        }
        const uint64_t rowStart = static_cast<uint64_t>(base) + static_cast<uint64_t>(r) * stride;
        const uint8_t* row = LayoutBytes(*run.chip, rowStart, rowStart + rowBytes);
        if (row == nullptr)
            return false;
        WithRowFetch(run, h, row, [&](auto fetch) { DecodeColumns(columns[k], lo, n, width, wrapX, fetch); });
    }

    // Gather each pixel's four taps and its x fraction, then blend them (Simd::BilinearBlend:
    // the general path's Weights / Blend4 arithmetic, NEON / SSE2 / plain C++)
    uint32_t* texels = run.texels;
    uint32_t* taps = run.bilinear + 2 * kBilinearColumns;
    uint32_t* t00 = taps;
    uint32_t* t10 = taps + kMaxLineWidth;
    uint32_t* t01 = taps + 2 * kMaxLineWidth;
    uint32_t* t11 = taps + 3 * kMaxLineWidth;
    uint8_t* fractions = reinterpret_cast<uint8_t*>(taps + 4 * kMaxLineWidth);
    const uint32_t* r0 = columns[0];
    const uint32_t* r1 = columns[1];
    int64_t bx = bx0;
    for (uint32_t i = 0; i < count; ++i, bx += a)
    {
        const int64_t tx = bx >> kFixedShift;
        fractions[i] = static_cast<uint8_t>(bx - tx * kFixedOneTransform);
        const uint32_t idx = static_cast<uint32_t>(tx - lo);
        t00[i] = r0[idx];
        t10[i] = r0[idx + 1];
        t01[i] = r1[idx];
        t11[i] = r1[idx + 1];
    }
    Simd::BilinearBlend(t00, t10, t01, t11, fractions, fy, texels, count);
    FinishFastSpan(run, first, texels, count);
    return true;
}

// NEAREST under any matrix (rotation, shear): x' and y' step by A and D per pixel, each
// texel is decoded straight from the layout - the general path's sample positions and its
// wrap arithmetic on both axes, without its per-texel format switch. The whole layout must
// lie in RAM_G (else the general path reads it through the bus).
// SIMD-CANDIDATE(EVE-AFFINE): a gather per pixel; the coordinate stepping and the BORDER
// tests vectorize, the fetches do not (NEON / SSE2 have no byte gather)
bool DrawBitmapAffineFast(LineRun& run, const BitmapHandle& h, uint32_t base, uint32_t stride, uint32_t layoutWidth,
                          uint32_t layoutHeight, int64_t rely, int32_t first, int32_t last, int64_t vx)
{
    const int32_t* t = run.ctx.transform;
    const uint64_t rowBytes = (static_cast<uint64_t>(layoutWidth) * BitsPerPixel(h.format) + kBitsPerByte - 1) / kBitsPerByte;
    const uint64_t layoutEnd = static_cast<uint64_t>(base) + static_cast<uint64_t>(layoutHeight - 1) * stride + rowBytes;
    const uint8_t* layout = LayoutBytes(*run.chip, base, layoutEnd);
    if (layout == nullptr)
    {
#ifdef EVE_PROFILE
        Profile().fastRejects["layout not contiguous in RAM_G or ROM"]++;
#endif
        return false;
    }
    const int64_t a = t[0], b = t[1], c = t[2], d = t[3], e = t[4], f = t[5];
    const int64_t relFirst = static_cast<int64_t>(first) * kSubpixel + kPixelCenter - vx;
    int64_t sx = ((a * relFirst + b * rely) >> kSubpixelShift) + c;
    int64_t sy = ((d * relFirst + e * rely) >> kSubpixelShift) + f;
    const uint32_t count = static_cast<uint32_t>(last - first);
    const int32_t width = static_cast<int32_t>(layoutWidth);
    const int32_t rows = static_cast<int32_t>(layoutHeight);
    const bool wrapX = h.wrapX != 0, wrapY = h.wrapY != 0;
    uint32_t* texels = run.texels;
    WithLayoutFetch(run, h, layout, stride, [&](auto fetch) {
        for (uint32_t i = 0; i < count; ++i, sx += a, sy += d)
        {
            int32_t tx = static_cast<int32_t>(sx >> kFixedShift);
            int32_t ty = static_cast<int32_t>(sy >> kFixedShift);
            if (wrapX)
                tx = ((tx % width) + width) % width;
            else if (tx < 0 || tx >= width)
            {
                texels[i] = 0;
                continue;
            }
            if (wrapY)
                ty = ((ty % rows) + rows) % rows;
            else if (ty < 0 || ty >= rows)
            {
                texels[i] = 0;
                continue;
            }
            texels[i] = fetch(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
        }
    });
    FinishFastSpan(run, first, texels, count);
    return true;
}

bool DrawBitmapFast(LineRun& run, const BitmapHandle& h, uint32_t base, uint32_t stride, uint32_t layoutWidth,
                    uint32_t layoutHeight, int64_t rely, int32_t first, int32_t last, int64_t vx)
{
    const GraphicsContext& ctx = run.ctx;
    const int32_t* t = ctx.transform;
    if (!FastFormat(h.format))
    {
#ifdef EVE_PROFILE
        Profile().fastRejects["format"]++;
#endif
        return false;
    }
    if (layoutWidth == 0 || layoutHeight == 0)
    {
#ifdef EVE_PROFILE
        Profile().fastRejects["empty layout, handle " + std::to_string(&h - run.chip->state.handles)]++;
#endif
        return DrawTransparentSpan(run, first, last);
    }
    if (t[1] != 0 || t[3] != 0)
    {
        if (!h.filter)
            return DrawBitmapAffineFast(run, h, base, stride, layoutWidth, layoutHeight, rely, first, last, vx);
#ifdef EVE_PROFILE
        Profile().fastRejects["BILINEAR under a rotated matrix"]++;
#endif
        return false;
    }
    if (h.filter)
        return DrawBitmapBilinearFast(run, h, base, stride, layoutWidth, layoutHeight, rely, first, last, vx);
    const int64_t a = t[0];
    const int64_t relFirst = static_cast<int64_t>(first) * kSubpixel + kPixelCenter - vx;
    int64_t sx = ((a * relFirst) >> kSubpixelShift) + t[2];
    const int64_t sy = ((static_cast<int64_t>(t[4]) * rely) >> kSubpixelShift) + t[5];
    int32_t ty = static_cast<int32_t>(sy >> kFixedShift);
    const int32_t layoutRows = static_cast<int32_t>(layoutHeight);
    if (h.wrapY)
        ty = ((ty % layoutRows) + layoutRows) % layoutRows;
    const uint32_t bpp = BitsPerPixel(h.format);
    const uint64_t rowStart = static_cast<uint64_t>(base) + static_cast<uint64_t>(ty) * stride;
    const uint64_t rowBytes = (static_cast<uint64_t>(layoutWidth) * bpp + kBitsPerByte - 1) / kBitsPerByte;
    if (ty < 0 || ty >= layoutRows)
        return DrawTransparentSpan(run, first, last);  // BORDER: every texel transparent
    const uint8_t* row = LayoutBytes(*run.chip, rowStart, rowStart + rowBytes);
    if (row == nullptr)
    {
#ifdef EVE_PROFILE
        Profile().fastRejects["row not contiguous in RAM_G or ROM"]++;
#endif
        return false;
    }
    const int32_t width = static_cast<int32_t>(layoutWidth);
    const uint32_t count = static_cast<uint32_t>(last - first);
    uint32_t* texels = run.texels;
    const bool wrapX = h.wrapX != 0;
    // Decode the span's texels, one loop per format and wrap mode
#ifdef EVE_PROFILE
    Profile().scaleAPixels[a] += count;
    const uint64_t decodeStart = ProfileNow();
#endif
    WithRowFetch(run, h, row, [&](auto fetch) { DecodeSpan(texels, count, sx, a, width, wrapX, fetch); });
#ifdef EVE_PROFILE
    const uint64_t finishStart = ProfileNow();
    Profile().decodeNanos += finishStart - decodeStart;
#endif
    FinishFastSpan(run, first, texels, count);
#ifdef EVE_PROFILE
    Profile().finishNanos += ProfileNow() - finishStart;
#endif
    return true;
}

} // namespace

void BlendSpan(LineRun& run, int32_t first, const uint32_t* texels, uint32_t count)
{
    const GraphicsContext& ctx = run.ctx;
    const bool simple = DefaultPipeline(ctx);
    if (simple && kMultiplyRoundDiv255)
    {
        if (WritesTag(run))
            std::memset(run.tag + first, ctx.tag, count);
        Simd::BlendSrcAlpha(run.color + kChannels * static_cast<uint32_t>(first), texels, count);
    }
    else if (simple)
    {
        for (uint32_t i = 0; i < count; ++i)
            BlendDefault(run, first + static_cast<int32_t>(i), texels[i]);
    }
    else
        ShadeSpan(run, first, texels, count);
}

void InitBitmapTables()
{
    // Build the shared decode tables now, inside EveCreate, not on the first frame.
    for (uint8_t format : {kFormatArgb4, kFormatRgb565, kFormatArgb1555})
        (void)TableFor(format);
    for (uint8_t format : {kFormatRgb332, kFormatArgb2})
        (void)ByteTableFor(format);
    for (uint8_t format : {kFormatL1, kFormatL2, kFormatL4, kFormatL8})
        (void)LuminanceTableFor(format);
}

uint32_t PixelsPerClock(uint8_t format, uint8_t filter)
{
    // [PG §2.5.7], spec §5.2.
    constexpr uint32_t kNearestPaletted = 8, kNearestOther = 16;
    const bool slow = format == kFormatText8x8 || format == kFormatTextVga || format == kFormatPaletted4444 ||
                      format == kFormatPaletted565;
    if (filter)
        return slow ? kBilinearPalettedPixelsPerClock : kBilinearPixelsPerClock;
    return slow ? kNearestPaletted : kNearestOther;
}

template <LineMode Mode>
void DrawBitmap(LineRun& run, const Vertex& v)
{
    const BitmapHandle& h = run.chip->state.handles[v.handle];
    const int32_t width = static_cast<int32_t>(HandleWidth(h));
    const int32_t height = static_cast<int32_t>(HandleHeight(h));
    // Sample positions are relative to the vertex, at pixel centers (spec §6.5, V3).
    const int64_t rely = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter - v.y;
#ifdef EVE_PROFILE
    if constexpr (Mode == LineMode::Draw)
        Profile().bitmapVertices++;
#endif
    if (rely < 0 || rely >= static_cast<int64_t>(height) * kSubpixel)
    {
#ifdef EVE_PROFILE
        if constexpr (Mode == LineMode::Draw)
            Profile().bitmapMissY++;
#endif
        return;
    }
    // Pixels whose centers lie in [v.x, v.x + width).
    int32_t first = FloorDiv(static_cast<int64_t>(v.x) - kPixelCenter + kSubpixel - 1, kSubpixel);
    int32_t last = FloorDiv(static_cast<int64_t>(v.x) + static_cast<int64_t>(width) * kSubpixel - kPixelCenter +
                                kSubpixel - 1,
                            kSubpixel);
    if (!ScissorSpan(run, first, last))
    {
#ifdef EVE_PROFILE
        if constexpr (Mode == LineMode::Draw)
            Profile().bitmapMissX++;
#endif
        return;
    }
    run.fillCost += static_cast<uint64_t>(last - first) * (kFillCostScale / PixelsPerClock(h.format, h.filter));
#ifdef EVE_PROFILE
    struct SpanProfile
    {
        ProfileBitmapKey key;
        uint32_t pixels;
        uint64_t start = ProfileNow();
        ~SpanProfile()
        {
            ProfileCell& cell = Profile().bitmaps[key];
            ++cell.calls;
            cell.pixels += pixels;
            cell.nanos += ProfileNow() - start;
        }
    } spanProfile{{h.format, static_cast<uint8_t>(h.filter ? 1 : 0), 0,
                   run.ctx.transform[1] != 0 || run.ctx.transform[3] != 0 ? ProfileMatrix::Rotated
                   : run.ctx.transform[0] != kFixedOneTransform || run.ctx.transform[4] != kFixedOneTransform
                       ? ProfileMatrix::Scaled
                       : ProfileMatrix::Identity,
                   static_cast<uint8_t>(DefaultPipeline(run.ctx) ? 1 : 0)},
                  static_cast<uint32_t>(last - first)};
#endif

    const GraphicsContext& ctx = run.ctx;
    const uint32_t stride = HandleStride(h);
    const uint32_t layoutHeight = HandleLayoutHeight(h);
    const uint32_t bpp = BitsPerPixel(h.format);
    const uint32_t layoutWidth = h.format == kFormatText8x8 || h.format == kFormatTextVga
                                     ? stride / (bpp / kBitsPerByte) * kTextCell
                                     : stride * kBitsPerByte / bpp;
    // CELL n offsets the source by n x linestride x height (spec §6.5).
    const uint32_t base = h.source + static_cast<uint32_t>(v.cell) * stride * layoutHeight;
    const int64_t a = ctx.transform[0], b = ctx.transform[1], c = ctx.transform[2];
    const int64_t d = ctx.transform[3], e = ctx.transform[4], f = ctx.transform[5];
    const uint32_t colorR = (ctx.colorRgb >> kRedShift) & kChannelMax;
    const uint32_t colorG = (ctx.colorRgb >> kGreenShift) & kChannelMax;
    const uint32_t colorB = ctx.colorRgb & kChannelMax;
    if constexpr (Mode == LineMode::Draw)
    {
        if (run.chip->bitmapFastPath &&
            DrawBitmapFast(run, h, base, stride, layoutWidth, layoutHeight, rely, first, last, v.x))
        {
#ifdef EVE_PROFILE
            spanProfile.key.fast = 1;
#endif
            return;
        }
    }

    // x' = A x + B y + C, y' = D x + E y + F [PG §2.5.5], in 1/256 texel. Pixel x moves
    // by 16 in 1/16 units, so x' and y' move by exactly A and D.
    const int64_t relFirst = static_cast<int64_t>(first) * kSubpixel + kPixelCenter - v.x;
    int64_t sx = ((a * relFirst + b * rely) >> kSubpixelShift) + c;
    int64_t sy = ((d * relFirst + e * rely) >> kSubpixelShift) + f;
    for (int32_t x = first; x < last; ++x, sx += a, sy += d)
    {
        Rgba t;
        if (!h.filter)
        {
            t = WrappedTexel(run, h, base, stride, layoutWidth, layoutHeight, static_cast<int32_t>(sx >> kFixedShift),
                             static_cast<int32_t>(sy >> kFixedShift));
        }
        else
        {
            const int64_t bx = sx - kBilinearOffset;
            const int64_t by = sy - kBilinearOffset;
            const int32_t tx = static_cast<int32_t>(bx >> kFixedShift);
            const int32_t ty = static_cast<int32_t>(by >> kFixedShift);
            const uint32_t fx = static_cast<uint32_t>(bx - static_cast<int64_t>(tx) * kFixedOneTransform);
            const uint32_t fy = static_cast<uint32_t>(by - static_cast<int64_t>(ty) * kFixedOneTransform);
            const Rgba t00 = WrappedTexel(run, h, base, stride, layoutWidth, layoutHeight, tx, ty);
            const Rgba t10 = WrappedTexel(run, h, base, stride, layoutWidth, layoutHeight, tx + 1, ty);
            const Rgba t01 = WrappedTexel(run, h, base, stride, layoutWidth, layoutHeight, tx, ty + 1);
            const Rgba t11 = WrappedTexel(run, h, base, stride, layoutWidth, layoutHeight, tx + 1, ty + 1);
            const BilinearWeights w = Weights(fx, fy);
            t.r = Blend4(w, t00.r, t10.r, t01.r, t11.r);
            t.g = Blend4(w, t00.g, t10.g, t01.g, t11.g);
            t.b = Blend4(w, t00.b, t10.b, t01.b, t11.b);
            t.a = Blend4(w, t00.a, t10.a, t01.a, t11.a);
        }
        // Every bitmap pixel is multiplied by COLOR_RGB and COLOR_A (spec §6.5, V3).
        Shade<Mode>(run, x, Multiply(t.r, colorR), Multiply(t.g, colorG), Multiply(t.b, colorB),
                    Multiply(t.a, ctx.colorA));
        if constexpr (Mode == LineMode::Probe)
        {
            if (x == run.probeX && run.probe->written)
            {
                run.probe->handle = v.handle;
                run.probe->cell = v.cell;
                run.probe->commandIndex = v.index;
                run.probe->command = v.word;
                run.probe->sampleAddress = base;
            }
        }
    }
}

template void DrawBitmap<LineMode::Draw>(LineRun&, const Vertex&);
template void DrawBitmap<LineMode::Probe>(LineRun&, const Vertex&);

} // namespace EveLib
