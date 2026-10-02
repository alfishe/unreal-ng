// eve-emu - CMD_TEXT, CMD_NUMBER, fonts, CMD_SETBITMAP (spec §7.5).
//
// The display list these commands produce is generated whole into the INFLIGHT region,
// then emitted as its cost elapses. The words are those of the BT8XX reference, which runs
// the real coprocessor ROM (golden cases cmd-text, cmd-number, cmd-fonts, cmd-setfont).
#include "eve-copro.h"

namespace EveLib
{

namespace
{

// Display list words used here [PG §4].
constexpr uint32_t kOpcodeShiftDl = 24;
constexpr uint32_t kOpBitmapSource = 0x01, kOpBitmapHandle = 0x05, kOpCell = 0x06, kOpBitmapLayout = 0x07,
                   kOpBitmapSize = 0x08, kOpBegin = 0x1F, kOpSaveContext = 0x22, kOpRestoreContext = 0x23,
                   kOpVertexFormat = 0x27, kOpBitmapLayoutH = 0x28, kOpBitmapSizeH = 0x29;
constexpr uint32_t kBeginBitmaps = 1;
constexpr uint32_t kVertex2fTag = 1u << 30;
constexpr uint32_t kVertex2fXShift = 15;
constexpr uint32_t kVertex2fMask = 0x7FFF;
constexpr uint32_t kTextVertexFormat = 2;         // CMD_TEXT's VERTEX2F unit: 1/4 pixel
constexpr int32_t kTextVertexScale = 4;
constexpr uint32_t kVertex2iiTag = 2u << 30;
constexpr uint32_t kVertex2iiXShift = 21, kVertex2iiYShift = 12, kVertex2iiHandleShift = 7;
constexpr int32_t kVertex2iiMax = 511;
constexpr uint32_t kCellMask = 0x7F;
constexpr uint32_t kAddressMask22 = 0x3FFFFF;
constexpr uint32_t kLayoutFormatShift = 19, kLayoutStrideShift = 9;
constexpr uint32_t kSizeWidthShift = 9;
constexpr uint32_t kLowStrideBits = 10, kLowSizeBits = 9;
constexpr uint32_t kHighFieldShift = 2;          // _H commands: width / stride in bits 3:2
constexpr uint32_t kLowMask9 = (1u << kLowSizeBits) - 1;
constexpr uint32_t kLowMask10 = (1u << kLowStrideBits) - 1;
constexpr uint32_t kBitsPerByte = 8;

// Options [PG §5.41, §5.43].
constexpr uint32_t kOptCenterX = 512;
constexpr uint32_t kOptCenterY = 1024;
constexpr uint32_t kOptRightX = 2048;
constexpr uint32_t kOptSigned = 256;
constexpr uint32_t kOptWidthMask = 0x1F;          // CMD_NUMBER: zero-padding width 1...9
constexpr uint32_t kMaxNumberWidth = 9;
constexpr uint32_t kMinBase = 2, kMaxBase = 36;
constexpr uint32_t kMaxDigits = 33;               // 32 binary digits and a sign
constexpr uint32_t kDigitsBelowTen = 10;
constexpr uint32_t kFontWidths = 128;             // width table entries of a metric block
constexpr uint32_t kMaxString = kRamCmdSize;      // a string cannot be longer than the ring

// 16-bit fields pack two per word, the first in the low half [PG §5].
constexpr uint32_t kHalfWordBits = 16;

int16_t Low16(uint32_t word)
{
    return static_cast<int16_t>(static_cast<uint16_t>(word));
}

int16_t High16(uint32_t word)
{
    return static_cast<int16_t>(static_cast<uint16_t>(word >> kHalfWordBits));
}

uint32_t Dl(uint32_t opcode, uint32_t fields)
{
    return (opcode << kOpcodeShiftDl) | fields;
}

uint32_t BitsPerPixel(uint32_t format)
{
    // [PG §4.7 Table 7]: L1 1, L2 2, L4 4, 16-bit formats 16, everything else 8.
    constexpr uint32_t kL1 = 1, kL4 = 2, kArgb1555 = 0, kArgb4 = 6, kRgb565 = 7, kTextVga = 10, kL2 = 17;
    switch (format)
    {
    case kL1: return 1;
    case kL2: return 2;
    case kL4: return 4;
    case kArgb1555:
    case kArgb4:
    case kRgb565:
    case kTextVga: return 16;
    default: return kBitsPerByte;
    }
}

// The bitmap parameter words of a font handle: SOURCE, LAYOUT_H, LAYOUT, SIZE_H, SIZE with
// NEAREST / BORDER (the order of the BT8XX reference, golden case cmd-fonts).
uint32_t LayoutWord(uint32_t format, uint32_t stride, uint32_t height)
{
    return Dl(kOpBitmapLayout, (format << kLayoutFormatShift) | ((stride & kLowMask10) << kLayoutStrideShift) |
                                   (height & kLowMask9));
}

uint32_t LayoutHighWord(uint32_t stride, uint32_t height)
{
    return Dl(kOpBitmapLayoutH, ((stride >> kLowStrideBits) << kHighFieldShift) | (height >> kLowSizeBits));
}

uint32_t SizeWord(uint32_t width, uint32_t height)
{
    return Dl(kOpBitmapSize, ((width & kLowMask9) << kSizeWidthShift) | (height & kLowMask9));
}

uint32_t SizeHighWord(uint32_t width, uint32_t height)
{
    return Dl(kOpBitmapSizeH, ((width >> kLowSizeBits) << kHighFieldShift) | (height >> kLowSizeBits));
}

bool QueueWords(EveChip& chip, const uint32_t* words, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!QueueDlWord(chip, words[i]))
            return false;
    return true;
}

bool QueueBitmapWords(EveChip& chip, uint32_t source, uint32_t format, uint32_t stride, uint32_t width,
                      uint32_t height)
{
    const uint32_t words[] = {Dl(kOpBitmapSource, source & kAddressMask22), LayoutHighWord(stride, height),
                              LayoutWord(format, stride, height), SizeHighWord(width, height),
                              SizeWord(width, height)};
    return QueueWords(chip, words, sizeof(words) / sizeof(words[0]));
}

struct FontMetrics
{
    uint32_t block, format, stride, width, height, data;
};

FontMetrics Metrics(const EveChip& chip, uint32_t font)
{
    FontMetrics m{};
    m.block = chip.state.copro.fontPointers[font % kHandleCount];
    m.format = ReadMemory32(chip, m.block + kFontMetricFormat);
    m.stride = ReadMemory32(chip, m.block + kFontMetricStride);
    m.width = ReadMemory32(chip, m.block + kFontMetricWidth);
    m.height = ReadMemory32(chip, m.block + kFontMetricHeight);
    m.data = ReadMemory32(chip, m.block + kFontMetricData);
    return m;
}

uint32_t CharWidth(const EveChip& chip, const FontMetrics& m, uint8_t character)
{
    return character < kFontWidths ? BusPeek(chip, m.block + character) : 0;
}

// The words of a string drawn with a font at (x, y): SAVE_CONTEXT, VERTEX_FORMAT(2),
// BITMAP_HANDLE, BEGIN(BITMAPS), per character VERTEX2II(x, y, font, code) when x and y are
// 0...511, else CELL(code) and VERTEX2F in 1/4 pixel, RESTORE_CONTEXT (golden cmd-text).
bool QueueText(EveChip& chip, int32_t x, int32_t y, uint32_t font, uint32_t options, const uint8_t* text,
               uint32_t length)
{
    font %= kHandleCount;
    const FontMetrics m = Metrics(chip, font);
    int32_t width = 0;
    for (uint32_t i = 0; i < length; ++i)
        width += static_cast<int32_t>(CharWidth(chip, m, text[i]));
    // OPT_RIGHTX: x is the rightmost pixel [PG §5.41].
    if (options & kOptRightX)
        x -= width - 1;
    else if (options & kOptCenterX)
        x -= width / 2;
    if (options & kOptCenterY)
        y -= static_cast<int32_t>(m.height) / 2;

    const uint32_t head[] = {Dl(kOpSaveContext, 0), Dl(kOpVertexFormat, kTextVertexFormat),
                             Dl(kOpBitmapHandle, font), Dl(kOpBegin, kBeginBitmaps)};
    if (!QueueWords(chip, head, sizeof(head) / sizeof(head[0])))
        return false;
    for (uint32_t i = 0; i < length; ++i)
    {
        const int32_t cx = x;
        x += static_cast<int32_t>(CharWidth(chip, m, text[i]));
        const uint32_t cell = text[i] & kCellMask;
        if (cx >= 0 && cx <= kVertex2iiMax && y >= 0 && y <= kVertex2iiMax)
        {
            const uint32_t vertex = kVertex2iiTag | (static_cast<uint32_t>(cx) << kVertex2iiXShift) |
                                    (static_cast<uint32_t>(y) << kVertex2iiYShift) |
                                    (font << kVertex2iiHandleShift) | cell;
            if (!QueueDlWord(chip, vertex))
                return false;
            continue;
        }
        const uint32_t vertex =
            kVertex2fTag | ((static_cast<uint32_t>(cx * kTextVertexScale) & kVertex2fMask) << kVertex2fXShift) |
            (static_cast<uint32_t>(y * kTextVertexScale) & kVertex2fMask);
        if (!QueueDlWord(chip, Dl(kOpCell, cell)) || !QueueDlWord(chip, vertex))
            return false;
    }
    return QueueDlWord(chip, Dl(kOpRestoreContext, 0));
}

// CMD_GRADIENT (golden cases gradient-*): a 512 x 1 L8 ramp in ROM (0 up to texel 128,
// rising to 255 at texel 383) drawn over the whole screen on the scratch handle, with a
// transform that puts t = 0 at texel 128 and t = 1 at texel 384; first the per-channel
// minimum of the two colors with the ramp written to alpha, then the rising channels
// blended by DST_ALPHA and the falling ones by ONE_MINUS_DST_ALPHA.
bool QueueGradient(EveChip& chip)
{
    constexpr uint32_t kRampAddress = 0x200065;
    constexpr uint32_t kRampStride = 512;
    constexpr uint32_t kRampHeight = 1;
    constexpr uint32_t kFormatL8 = 3;
    constexpr int64_t kTexelScale = 65536;      // 256 texels per t, in 1/256 texel
    constexpr int64_t kRampStart = 32768;       // texel 128 in 1/256 texel
    constexpr uint32_t kMask17 = 0x1FFFF, kMask24 = 0xFFFFFF;
    constexpr uint32_t kOpTransformA = 0x15, kOpTransformB = 0x16, kOpTransformC = 0x17, kOpTransformD = 0x18,
                       kOpTransformE = 0x19, kOpTransformF = 0x1A, kOpColorRgb = 0x04, kOpColorA = 0x10,
                       kOpBlendFunc = 0x0B, kOpColorMask = 0x20;
    constexpr uint32_t kBlendOneZero = (1u << 3) | 0, kBlendDstAlphaOne = (3u << 3) | 1,
                       kBlendOneMinusDstAlphaOne = (5u << 3) | 1;
    constexpr uint32_t kMaskRgba = 0xF, kMaskRgb = 0xE, kAlphaFull = 0xFF, kWrapYRepeat = 1u << 18;
    constexpr uint32_t kChannelShifts[] = {16, 8, 0};
    const CoproState& c = chip.state.copro;
    const int64_t x0 = Low16(c.params[0]), y0 = High16(c.params[0]);
    const int64_t x1 = Low16(c.params[2]), y1 = High16(c.params[2]);
    const uint32_t rgb0 = c.params[1] & 0xFFFFFF, rgb1 = c.params[3] & 0xFFFFFF;
    const int64_t dx = x1 - x0, dy = y1 - y0;
    const int64_t length2 = dx * dx + dy * dy;
    int64_t a = -1, b = -1, cc = -1, d = -1, e = -1, ff = -1; // a zero-length gradient
    if (length2 != 0)
    {
        a = kTexelScale * dx / length2;
        b = kTexelScale * dy / length2;
        cc = (kRampStart * length2 - kTexelScale * (dx * x0 + dy * y0)) / length2;
        d = e = ff = 0;
    }
    uint32_t base = 0, rising = 0, falling = 0;
    for (uint32_t shift : kChannelShifts)
    {
        const uint32_t v0 = (rgb0 >> shift) & 0xFF, v1 = (rgb1 >> shift) & 0xFF;
        base |= (v0 < v1 ? v0 : v1) << shift;
        rising |= (v1 > v0 ? v1 - v0 : 0) << shift;
        falling |= (v0 > v1 ? v0 - v1 : 0) << shift;
    }
    const uint32_t scratch = c.scratchHandle % kHandleCount;
    const uint32_t vertex = kVertex2iiTag | (scratch << kVertex2iiHandleShift);
    const uint32_t head[] = {
        Dl(kOpSaveContext, 0),
        Dl(kOpTransformA, static_cast<uint32_t>(a) & kMask17), Dl(kOpTransformB, static_cast<uint32_t>(b) & kMask17),
        Dl(kOpTransformD, static_cast<uint32_t>(d) & kMask17), Dl(kOpTransformE, static_cast<uint32_t>(e) & kMask17),
        Dl(kOpTransformC, static_cast<uint32_t>(cc) & kMask24), Dl(kOpTransformF, static_cast<uint32_t>(ff) & kMask24),
        Dl(kOpBitmapHandle, scratch), Dl(kOpBitmapSource, kRampAddress), LayoutHighWord(kRampStride, kRampHeight),
        LayoutWord(kFormatL8, kRampStride, kRampHeight), SizeHighWord(0, 0), Dl(kOpBitmapSize, kWrapYRepeat),
        Dl(kOpColorRgb, base), Dl(kOpColorA, kAlphaFull), Dl(kOpBlendFunc, kBlendOneZero),
        Dl(kOpColorMask, kMaskRgba), Dl(kOpBegin, kBeginBitmaps), vertex, Dl(kOpColorMask, kMaskRgb)};
    if (!QueueWords(chip, head, sizeof(head) / sizeof(head[0])))
        return false;
    if (rising != 0)
    {
        const uint32_t pass[] = {Dl(kOpColorRgb, rising), Dl(kOpBlendFunc, kBlendDstAlphaOne), vertex};
        if (!QueueWords(chip, pass, sizeof(pass) / sizeof(pass[0])))
            return false;
    }
    if (falling != 0)
    {
        const uint32_t pass[] = {Dl(kOpColorRgb, falling), Dl(kOpBlendFunc, kBlendOneMinusDstAlphaOne), vertex};
        if (!QueueWords(chip, pass, sizeof(pass) / sizeof(pass[0])))
            return false;
    }
    return QueueDlWord(chip, Dl(kOpRestoreContext, 0));
}

// CMD_NUMBER's digits in the current base [PG §5.43].
uint32_t FormatNumber(const EveChip& chip, uint32_t value, uint32_t options, uint8_t* out)
{
    uint32_t base = chip.state.copro.numberBase;
    if (base < kMinBase || base > kMaxBase)
        base = kDefaultNumberBase;
    const bool negative = (options & kOptSigned) && static_cast<int32_t>(value) < 0;
    uint64_t magnitude = negative ? static_cast<uint64_t>(-static_cast<int64_t>(static_cast<int32_t>(value))) : value;
    uint8_t digits[kMaxDigits];
    uint32_t count = 0;
    do
    {
        const uint32_t digit = static_cast<uint32_t>(magnitude % base);
        // Digits above 9 are upper case (golden case cmd-number).
        digits[count++] = static_cast<uint8_t>(digit < kDigitsBelowTen ? '0' + digit : 'A' + digit - kDigitsBelowTen);
        magnitude /= base;
    } while (magnitude != 0 && count < kMaxDigits);
    uint32_t width = options & kOptWidthMask;
    width = width > kMaxNumberWidth ? kMaxNumberWidth : width;
    while (count < width && count < kMaxDigits)
        digits[count++] = '0';
    uint32_t length = 0;
    if (negative)
        out[length++] = '-';
    while (count > 0)
        out[length++] = digits[--count];
    return length;
}

bool QueueSetbitmap(EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    const uint32_t format = static_cast<uint16_t>(Low16(c.params[1]));
    const uint32_t width = static_cast<uint16_t>(High16(c.params[1]));
    const uint32_t height = static_cast<uint16_t>(Low16(c.params[2]));
    const uint32_t stride = (width * BitsPerPixel(format) + kBitsPerByte - 1) / kBitsPerByte;
    // CMD_SETBITMAP's own order: SOURCE, SIZE_H, SIZE, LAYOUT_H, LAYOUT (golden cmd-fonts).
    const uint32_t words[] = {Dl(kOpBitmapSource, c.params[0] & kAddressMask22), SizeHighWord(width, height),
                              SizeWord(width, height), LayoutHighWord(stride, height),
                              LayoutWord(format, stride, height)};
    return QueueWords(chip, words, sizeof(words) / sizeof(words[0]));
}

// Find the NUL that ends the string in the ring; false while it has not arrived.
bool StringLength(const EveChip& chip, uint32_t& length)
{
    const uint32_t available = RingDataAvailable(chip);
    for (uint32_t i = 0; i < available && i < kMaxString; ++i)
        if (RingDataByte(chip, i) == 0)
        {
            length = i;
            return true;
        }
    return false;
}

} // namespace

bool TextBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdText:
        return false; // the string follows
    case kCmdNumber:
    {
        uint8_t text[kMaxDigits + 1];
        const uint32_t options = static_cast<uint16_t>(High16(c.params[1]));
        const uint32_t length = FormatNumber(chip, c.params[2], options, text);
        if (!QueueText(chip, Low16(c.params[0]), High16(c.params[0]), static_cast<uint16_t>(Low16(c.params[1])),
                       options, text, length))
            return false;
        return false; // emitted as its cost elapses
    }
    case kCmdSetfont:
        c.fontPointers[c.params[0] % kHandleCount] = c.params[1];
        c.fontFirstChar[c.params[0] % kHandleCount] = 0;
        return true;
    case kCmdSetfont2:
    {
        const uint32_t font = c.params[0] % kHandleCount;
        c.fontPointers[font] = c.params[1];
        c.fontFirstChar[font] = static_cast<uint8_t>(c.params[2]);
        const FontMetrics m = Metrics(chip, font);
        // The source moves back by firstchar cells, so a character's cell is its code
        // (golden case cmd-setfont).
        const uint32_t source = m.data - c.params[2] * m.stride * m.height;
        if (!QueueDlWord(chip, Dl(kOpBitmapHandle, font)) ||
            !QueueBitmapWords(chip, source, m.format, m.stride, m.width, m.height))
            return false;
        return false;
    }
    case kCmdRomfont:
    {
        const uint32_t font = c.params[0] % kHandleCount;
        const uint32_t slot = c.params[1];
        if (slot < kFirstRomFont || slot > kLastRomFont)
            return true; // TO VERIFY: an invalid slot is ignored
        c.fontPointers[font] = ReadMemory32(chip, kRomFontRootAddress) + kFontMetricSize * (slot - kFirstRomFont);
        c.fontFirstChar[font] = 0;
        const FontMetrics m = Metrics(chip, font);
        if (!QueueDlWord(chip, Dl(kOpBitmapHandle, font)) ||
            !QueueBitmapWords(chip, m.data, m.format, m.stride, m.width, m.height))
            return false;
        return false;
    }
    case kCmdSetbitmap:
        QueueSetbitmap(chip);
        return false;
    case kCmdGradient:
        QueueGradient(chip);
        return false;
    default:
        return true;
    }
}

StepPlan TextPlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    if (c.command == kCmdText && !c.generated)
    {
        uint32_t length = 0;
        if (!StringLength(chip, length))
        {
            c.phase = EVE_COPRO_WAITING_DATA;
            return NotReady();
        }
        return StepPlan{true, length + 1, 0};
    }
    return EmitPlan(chip);
}

void TextApply(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    if (c.command == kCmdText && !c.generated)
    {
        // Read the string (units bytes with its NUL), consume it, generate the words.
        uint8_t text[kMaxString];
        const uint32_t length = units - 1;
        RingDataCopy(chip, text, length);
        ConsumeRingData(chip, units);
        AlignRing(chip);
        c.generated = 1;
        if (!QueueText(chip, Low16(c.params[0]), High16(c.params[0]), static_cast<uint16_t>(Low16(c.params[1])),
                       static_cast<uint16_t>(High16(c.params[1])), text, length))
            return;
        return;
    }
    EmitApply(chip, units);
}

} // namespace EveLib
