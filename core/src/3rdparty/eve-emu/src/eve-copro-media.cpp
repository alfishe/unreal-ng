// eve-emu - media FIFO, CMD_LOADIMAGE, CMD_PLAYVIDEO, CMD_VIDEOSTART, CMD_VIDEOFRAME
// (spec §7.5, §8).
//
// CMD_LOADIMAGE collects the whole image file into the INFLIGHT region (it is consumed
// from the ring or the media FIFO as it arrives, so the record must hold it, arch §7.3),
// finds the file's end by its own structure (PNG IEND, JPEG EOI), applies the chip's
// rules, decodes it with the configured decoder, writes the converted pixels into RAM_G
// as their cost elapses, and finally emits the bitmap's display list words.
#include "eve-copro.h"

namespace EveLib
{

namespace
{

// Options [PG §5.19].
constexpr uint32_t kOptMono = 1;
constexpr uint32_t kOptNodl = 2;
constexpr uint32_t kOptFullscreen = 8;
constexpr uint32_t kOptMediafifo = 16;

// Bitmap formats [PG §4.7 Table 7].
constexpr uint32_t kFormatL8 = 3, kFormatArgb4 = 6, kFormatRgb565 = 7, kFormatPaletted565 = 14,
                   kFormatPaletted4444 = 15;

// Display list words [PG §4].
constexpr uint32_t kOpcodeShiftDl = 24;
constexpr uint32_t kOpBitmapSource = 0x01, kOpBitmapLayout = 0x07, kOpBitmapSize = 0x08,
                   kOpBitmapTransformA = 0x15, kOpBitmapTransformE = 0x19, kOpBitmapLayoutH = 0x28,
                   kOpBitmapSizeH = 0x29, kOpPaletteSource = 0x2A;
constexpr uint32_t kLayoutFormatShift = 19, kLayoutStrideShift = 9, kSizeWidthShift = 9;
constexpr uint32_t kLowStrideBits = 10, kLowSizeBits = 9, kHighFieldShift = 2;
constexpr uint32_t kLowMask9 = (1u << kLowSizeBits) - 1;
constexpr uint32_t kLowMask10 = (1u << kLowStrideBits) - 1;
constexpr uint32_t kAddressMask22 = 0x3FFFFF;
constexpr uint32_t kTransformOne = 256; // 1.0 in 8.8
constexpr uint32_t kDlswapFrameRequest = 2;

// File structure.
constexpr uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint32_t kPngSignatureSize = sizeof(kPngSignature);
constexpr uint32_t kPngChunkHeader = 8;    // length, type
constexpr uint32_t kPngChunkOverhead = 12; // length, type, CRC
constexpr uint32_t kPngColorTypeOffset = 25;
constexpr uint8_t kPngGrayAlpha = 4;
constexpr uint8_t kJpegMarker = 0xFF;
constexpr uint8_t kJpegSoi = 0xD8, kJpegEoi = 0xD9, kJpegSos = 0xDA, kJpegStuffing = 0x00, kJpegTem = 0x01;
constexpr uint8_t kJpegRst0 = 0xD0, kJpegRst7 = 0xD7;
constexpr uint32_t kMarkerBytes = 2;
constexpr uint32_t kBitsPerByte = 8;

enum Stage : uint8_t { kCollect = 0, kWritePixels = 1, kEmit = 2 };

enum class FileKind { Unknown, Incomplete, Png, Jpeg };

uint32_t LoadBe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t LoadBe16(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << kBitsPerByte) | p[1];
}

uint32_t Dl(uint32_t opcode, uint32_t fields)
{
    return (opcode << kOpcodeShiftDl) | fields;
}

// --- The data source: the ring or the media FIFO ---------------------------------------------

bool FromFifo(const EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdLoadimage: return (c.params[1] & kOptMediafifo) != 0;
    case kCmdPlayvideo: return (c.params[0] & kOptMediafifo) != 0;
    default: return true; // CMD_VIDEOSTART / CMD_VIDEOFRAME read the media FIFO
    }
}

uint32_t FifoAvailable(const EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    if (c.mediaFifoSize == 0)
        return 0;
    const uint32_t read = RegGet(chip, Reg::MediafifoRead);
    const uint32_t write = RegGet(chip, Reg::MediafifoWrite);
    return (write - read + c.mediaFifoSize) % c.mediaFifoSize;
}

uint32_t SourceAvailable(const EveChip& chip)
{
    return FromFifo(chip) ? FifoAvailable(chip) : RingDataAvailable(chip);
}

uint32_t SourceCopy(const EveChip& chip, uint8_t* out, uint32_t size)
{
    if (!FromFifo(chip))
        return RingDataCopy(chip, out, size);
    const CoproState& c = chip.state.copro;
    const uint32_t count = size < FifoAvailable(chip) ? size : FifoAvailable(chip);
    uint32_t offset = RegGet(chip, Reg::MediafifoRead) % c.mediaFifoSize;
    for (uint32_t i = 0; i < count; ++i)
    {
        out[i] = BusPeek(chip, c.mediaFifoBase + offset);
        offset = (offset + 1) % c.mediaFifoSize;
    }
    return count;
}

void SourceConsume(EveChip& chip, uint32_t bytes)
{
    if (!FromFifo(chip))
    {
        ConsumeRingData(chip, bytes);
        return;
    }
    const CoproState& c = chip.state.copro;
    const uint32_t offset = RegGet(chip, Reg::MediafifoRead);
    RegSet(chip, Reg::MediafifoRead, (offset + bytes) % c.mediaFifoSize);
}

// --- File end detection -------------------------------------------------------------------------

FileKind Kind(const uint8_t* data, uint32_t size)
{
    if (size >= kMarkerBytes && data[0] == kJpegMarker && data[1] == kJpegSoi)
        return FileKind::Jpeg;
    if (size < kPngSignatureSize)
        return size >= kMarkerBytes && data[0] != kPngSignature[0] ? FileKind::Unknown : FileKind::Incomplete;
    return std::memcmp(data, kPngSignature, kPngSignatureSize) == 0 ? FileKind::Png : FileKind::Unknown;
}

// Advance the parser over the collected bytes; true with `end` when the file is complete.
bool FindEnd(CoproState& c, const uint8_t* data, uint32_t size, FileKind kind, uint32_t& end)
{
    if (kind == FileKind::Png)
    {
        if (c.scanPos < kPngSignatureSize)
            c.scanPos = kPngSignatureSize;
        while (c.scanPos + kPngChunkHeader <= size)
        {
            const uint32_t length = LoadBe32(data + c.scanPos);
            const bool last = std::memcmp(data + c.scanPos + 4, "IEND", 4) == 0;
            if (last && c.scanPos + kPngChunkOverhead + length > size)
                return false; // the end chunk has not arrived whole yet
            c.scanPos += kPngChunkOverhead + length;
            if (last)
            {
                end = c.scanPos;
                return true;
            }
        }
        return false;
    }
    // JPEG: marker segments, then entropy-coded data after SOS until the next marker.
    if (c.scanPos < kMarkerBytes)
        c.scanPos = kMarkerBytes;
    while (c.scanPos + kMarkerBytes <= size)
    {
        const uint8_t a = data[c.scanPos];
        const uint8_t b = data[c.scanPos + 1];
        if (c.scanEntropy)
        {
            if (a == kJpegMarker && b != kJpegStuffing && !(b >= kJpegRst0 && b <= kJpegRst7))
                c.scanEntropy = 0; // a marker ends the scan
            else
            {
                c.scanPos += (a == kJpegMarker) ? kMarkerBytes : 1;
                continue;
            }
        }
        if (a != kJpegMarker)
            return false; // not a marker: the parser waits; the decoder will reject the file
        if (b == kJpegMarker)
        {
            ++c.scanPos; // fill byte
            continue;
        }
        if (b == kJpegEoi)
        {
            end = c.scanPos + kMarkerBytes;
            return true;
        }
        if (b == kJpegSoi || b == kJpegTem || (b >= kJpegRst0 && b <= kJpegRst7))
        {
            c.scanPos += kMarkerBytes;
            continue;
        }
        if (c.scanPos + 2 * kMarkerBytes > size)
            return false;
        const uint32_t length = LoadBe16(data + c.scanPos + kMarkerBytes);
        c.scanPos += kMarkerBytes + length;
        if (b == kJpegSos)
            c.scanEntropy = 1;
    }
    return false;
}

// --- Decoding and conversion ----------------------------------------------------------------------

uint32_t BytesPerPixel(uint32_t format)
{
    return (format == kFormatRgb565 || format == kFormatArgb4) ? 2 : 1;
}

// PLTE entries of a PNG file (0 without PLTE).
uint32_t PngPaletteEntries(const uint8_t* data, uint32_t size)
{
    constexpr uint32_t kRgbBytes = 3;
    uint32_t pos = kPngSignatureSize;
    while (pos + kPngChunkOverhead <= size)
    {
        const uint32_t length = LoadBe32(data + pos);
        if (std::memcmp(data + pos + 4, "PLTE", 4) == 0)
            return length / kRgbBytes;
        pos += kPngChunkOverhead + length;
    }
    return 0;
}

bool Indexed(uint32_t format)
{
    return format == kFormatPaletted565 || format == kFormatPaletted4444;
}

uint32_t Channels(EveImageLayout layout)
{
    switch (layout)
    {
    case EVE_IMAGE_GRAY8:
    case EVE_IMAGE_INDEXED8: return 1;
    case EVE_IMAGE_RGB888: return 3;
    default: return 4;
    }
}

uint32_t Narrow(uint32_t value, uint32_t bits)
{
    constexpr uint32_t kMax = 255;
    if (kLoadImageTruncates)
        return value >> (kBitsPerByte - bits);
    return (value * ((1u << bits) - 1) + kMax / 2) / kMax;
}

uint32_t ToRgb565(uint32_t r, uint32_t g, uint32_t b)
{
    constexpr uint32_t kFive = 5, kSix = 6;
    return (Narrow(r, kFive) << (kFive + kSix)) | (Narrow(g, kSix) << kFive) | Narrow(b, kFive);
}

uint32_t ToArgb4(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    constexpr uint32_t kFour = 4;
    return (Narrow(a, kFour) << (3 * kFour)) | (Narrow(r, kFour) << (2 * kFour)) | (Narrow(g, kFour) << kFour) |
           Narrow(b, kFour);
}

uint32_t Luma(uint32_t r, uint32_t g, uint32_t b)
{
    // BT.601 weights in 1/256 (OPT_MONO on a color JPEG; TO VERIFY).
    constexpr uint32_t kR = 77, kG = 150, kB = 29, kRound = 128;
    return (kR * r + kG * g + kB * b + kRound) >> kBitsPerByte;
}

// Probe, check the chip's rules, decode into the image buffer; false = invalid image.
bool DecodeImage(EveChip& chip, bool remember, bool mono)
{
    CoproState& c = chip.state.copro;
    const uint8_t* file = chip.regions[RegionInflight].base;
    const uint32_t size = c.inputBytes;
    const FileKind kind = Kind(file, size);
    const EveImageDecoder* decoder = kind == FileKind::Png ? chip.png : chip.jpeg;
    EveImageInfo info{};
    if (decoder->Probe(decoder->user, file, size, &info) != EVE_DECODE_OK)
        return false;
    const uint32_t pixels = info.width * info.height;
    if (info.width == 0 || info.height == 0 || info.width > kImageMaxWidth || info.height > kImageMaxHeight)
        return false;
    uint32_t format = kFormatRgb565;
    if (kind == FileKind::Png)
    {
        // PNG: bit depth 8, no Adam-7, gray + alpha unsupported [PG §5.19].
        if (info.bitDepth != kBitsPerByte || info.interlaced || pixels > kPngMaxPixels ||
            size <= kPngColorTypeOffset || file[kPngColorTypeOffset] == kPngGrayAlpha)
            return false;
        switch (info.layout)
        {
        case EVE_IMAGE_GRAY8: format = kFormatL8; break;
        case EVE_IMAGE_RGB888: format = kFormatRgb565; break;
        case EVE_IMAGE_RGBA8888: format = kFormatArgb4; break;
        case EVE_IMAGE_INDEXED8: format = info.paletteHasAlpha ? kFormatPaletted4444 : kFormatPaletted565; break;
        }
        if (remember)
            c.paletteEntries = PngPaletteEntries(file, size);
    }
    else
    {
        // Baseline JFIF only: progressive and CMYK fail (spec §7.5).
        if (info.progressive || info.cmyk || info.bitDepth != kBitsPerByte || pixels > kJpegMaxPixels)
            return false;
        format = (info.layout == EVE_IMAGE_GRAY8 || mono) ? kFormatL8 : kFormatRgb565;
    }
    const size_t bytes = static_cast<size_t>(pixels) * Channels(info.layout);
    if (decoder->Decode(decoder->user, file, size, chip.imageBuffer.get(), bytes, chip.imagePalette,
                        kPaletteEntries) != EVE_DECODE_OK)
        return false;
    chip.imageDecoded = true;
    if (remember)
    {
        c.imageFormat = format | (static_cast<uint32_t>(info.layout) << kBitsPerByte);
        c.flightWidth = info.width;
        c.flightHeight = info.height;
    }
    return true;
}

uint32_t ImageFormat(const CoproState& c)
{
    return c.imageFormat & ((1u << kBitsPerByte) - 1);
}

EveImageLayout ImageLayout(const CoproState& c)
{
    return static_cast<EveImageLayout>(c.imageFormat >> kBitsPerByte);
}

// An indexed PNG loads its palette (2 bytes per PLTE entry) at ptr and the indices after
// it (BT8XX, golden case loadimage-png-indexed; ftview's offset 512 is a 256-color palette).
uint32_t PaletteBytes(const CoproState& c)
{
    constexpr uint32_t kEntryBytes = 2;
    return Indexed(ImageFormat(c)) ? c.paletteEntries * kEntryBytes : 0;
}

// Byte `offset` of the converted output (palette first for indexed images).
uint8_t OutputByte(const EveChip& chip, uint32_t offset)
{
    const CoproState& c = chip.state.copro;
    const uint32_t format = ImageFormat(c);
    const uint32_t paletteBytes = PaletteBytes(c);
    if (offset < paletteBytes)
    {
        // Palette entries come as 0xAARRGGBB (decoders.h); two bytes each in RAM_G.
        constexpr uint32_t kEntryBytes = 2;
        const uint32_t entry = chip.imagePalette[offset / kEntryBytes];
        const auto channel = [entry](uint32_t index) { return (entry >> (kBitsPerByte * index)) & 0xFFu; };
        const uint32_t value = format == kFormatPaletted4444 ? ToArgb4(channel(3), channel(2), channel(1), channel(0))
                                                             : ToRgb565(channel(2), channel(1), channel(0));
        return static_cast<uint8_t>(value >> (kBitsPerByte * (offset % kEntryBytes)));
    }
    offset -= paletteBytes;
    const uint32_t bpp = BytesPerPixel(format);
    const uint32_t pixel = offset / bpp;
    const uint32_t channels = Channels(ImageLayout(c));
    const uint8_t* p = chip.imageBuffer.get() + static_cast<size_t>(pixel) * channels;
    switch (format)
    {
    case kFormatL8:
        return static_cast<uint8_t>(channels == 1 ? p[0] : Luma(p[0], p[1], p[2]));
    case kFormatRgb565:
        return static_cast<uint8_t>(ToRgb565(p[0], p[1], p[2]) >> (kBitsPerByte * (offset % bpp)));
    case kFormatArgb4:
        return static_cast<uint8_t>(ToArgb4(p[3], p[0], p[1], p[2]) >> (kBitsPerByte * (offset % bpp)));
    default:
        return p[0]; // palette index
    }
}

bool QueueImageWords(EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    const uint32_t ptr = c.params[0];
    const uint32_t format = ImageFormat(c);
    const uint32_t stride = c.flightWidth * BytesPerPixel(format);
    uint32_t width = c.flightWidth;
    uint32_t height = c.flightHeight;
    uint32_t scale = 1;
    if ((c.params[1] & kOptFullscreen) && kLoadImageFullscreenScales)
    {
        const uint32_t sx = RegGet(chip, Reg::Hsize) / width;
        const uint32_t sy = RegGet(chip, Reg::Vsize) / height;
        scale = sx < sy ? sx : sy;
        scale = scale == 0 ? 1 : scale;
    }
    constexpr uint32_t kMaxWords = 8;
    uint32_t words[kMaxWords];
    uint32_t count = 0;
    if (Indexed(format))
        words[count++] = Dl(kOpPaletteSource, ptr & kAddressMask22);
    // SOURCE, LAYOUT_H, LAYOUT, SIZE_H, SIZE (golden case loadimage-png-rgb).
    words[count++] = Dl(kOpBitmapSource, (ptr + PaletteBytes(c)) & kAddressMask22);
    words[count++] = Dl(kOpBitmapLayoutH, ((stride >> kLowStrideBits) << kHighFieldShift) | (height >> kLowSizeBits));
    words[count++] = Dl(kOpBitmapLayout, (format << kLayoutFormatShift) |
                                             ((stride & kLowMask10) << kLayoutStrideShift) | (height & kLowMask9));
    if (scale > 1)
    {
        width *= scale;
        height *= scale;
        words[count++] = Dl(kOpBitmapTransformA, kTransformOne / scale);
        words[count++] = Dl(kOpBitmapTransformE, kTransformOne / scale);
    }
    words[count++] = Dl(kOpBitmapSizeH, ((width >> kLowSizeBits) << kHighFieldShift) | (height >> kLowSizeBits));
    words[count++] = Dl(kOpBitmapSize, ((width & kLowMask9) << kSizeWidthShift) | (height & kLowMask9));
    for (uint32_t i = 0; i < count; ++i)
        if (!QueueDlWord(chip, words[i]))
            return false;
    return true;
}

// --- CMD_LOADIMAGE steps -----------------------------------------------------------------------------

StepPlan WritePlan(const EveChip& chip);
bool WritePixels(EveChip& chip, uint32_t units);
bool StartWriting(EveChip& chip, uint32_t address, uint8_t nextStage);

StepPlan LoadImagePlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.stage)
    {
    case kCollect:
    {
        const uint32_t available = SourceAvailable(chip);
        if (available == 0)
        {
            c.phase = EVE_COPRO_WAITING_DATA;
            return NotReady();
        }
        return StepPlan{true, available < kWorkBufferSize ? available : kWorkBufferSize, 0};
    }
    case kWritePixels:
        return WritePlan(chip);
    default:
        return EmitPlan(chip);
    }
}

bool MonoOption(const CoproState& c)
{
    return c.command == kCmdLoadimage && (c.params[1] & kOptMono) != 0;
}

// Decode the collected file and prepare to write its pixels at `address`; false: faulted.
bool StartWriting(EveChip& chip, uint32_t address, uint8_t nextStage)
{
    CoproState& c = chip.state.copro;
    if (!DecodeImage(chip, true, MonoOption(c)))
    {
        CoproFaultNow(chip, CoproFault::InvalidImage);
        return false;
    }
    const uint32_t bytes = PaletteBytes(c) + c.flightWidth * c.flightHeight * BytesPerPixel(ImageFormat(c));
    // Output beyond RAM_G is "unpredictable" [PG §5.19]: the emulator faults.
    if (static_cast<uint64_t>(address) + bytes > kRamGSize)
    {
        CoproFaultNow(chip, CoproFault::InvalidImage);
        return false;
    }
    c.writeAddress = address;
    c.stage = nextStage;
    c.total = bytes;
    c.done = 0;
    return true;
}

StepPlan WritePlan(const EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    const uint32_t bpp = BytesPerPixel(ImageFormat(c));
    const uint32_t units = StepUnits(c.total - c.done, chip.costs.loadImagePerPixel);
    return StepPlan{true, units, Cost((units + bpp - 1) / bpp, chip.costs.loadImagePerPixel)};
}

// Write the next converted bytes; true when the whole image is in memory.
bool WritePixels(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    if (!chip.imageDecoded && !DecodeImage(chip, false, MonoOption(c)))
    {
        CoproFaultNow(chip, CoproFault::InvalidImage);
        return false;
    }
    for (uint32_t i = 0; i < units; ++i)
        BusWrite(chip, c.writeAddress + c.done + i, OutputByte(chip, c.done + i));
    c.done += units;
    return c.done >= c.total;
}

void LoadImageApply(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    if (c.stage == kCollect)
    {
        Region& inflight = chip.regions[RegionInflight];
        const uint32_t room = inflight.size - c.inputBytes;
        const uint32_t take = units < room ? units : room;
        if (take == 0)
        {
            CoproFaultNow(chip, CoproFault::InflightOverflow);
            return;
        }
        SourceCopy(chip, inflight.base + c.inputBytes, take);
        const uint32_t before = c.inputBytes;
        const uint32_t size = before + take;
        const FileKind kind = Kind(inflight.base, size);
        if (kind == FileKind::Unknown)
        {
            CoproFaultNow(chip, CoproFault::InvalidImage);
            return;
        }
        uint32_t end = 0;
        const bool complete = kind != FileKind::Incomplete && FindEnd(c, inflight.base, size, kind, end);
        // Consume only the file's bytes: what follows in the ring is the next command.
        const uint32_t used = complete ? end - before : take;
        inflight.MarkDirtyRange(before, used);
        c.inputBytes = before + used;
        c.inflightUsed = c.inputBytes;
        SourceConsume(chip, used);
        if (complete)
        {
            if (!FromFifo(chip))
                AlignRing(chip);
            StartWriting(chip, c.params[0], kWritePixels);
        }
        return;
    }
    if (c.stage == kWritePixels)
    {
        if (!WritePixels(chip, units))
            return;
        c.imageAddress = c.params[0] + PaletteBytes(c);
        c.imageWidth = c.flightWidth;
        c.imageHeight = c.flightHeight;
        chip.imageDecoded = false;
        c.stage = kEmit;
        c.total = 0;
        c.done = 0;
        if (c.params[1] & kOptNodl)
        {
            CompleteCommand(chip);
            return;
        }
        QueueImageWords(chip);
        return;
    }
    EmitApply(chip, units);
}

// --- M-JPEG: the standard Huffman tables (ITU T.81 Annex K.3) -----------------------------------
//
// AVI M-JPEG frames often leave out their Huffman tables and rely on these; a frame
// without DHT gets them inserted after SOI before it is decoded.

constexpr uint8_t kJpegDht = 0xC4;
constexpr uint32_t kHuffmanCodeLengths = 16;
constexpr uint8_t kDcLuminanceBits[kHuffmanCodeLengths] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kDcChrominanceBits[kHuffmanCodeLengths] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr uint8_t kDcValues[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr uint8_t kAcLuminanceBits[kHuffmanCodeLengths] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
constexpr uint8_t kAcLuminanceValues[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
constexpr uint8_t kAcChrominanceBits[kHuffmanCodeLengths] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr uint8_t kAcChrominanceValues[] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
    0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
    0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

struct HuffmanTable
{
    uint8_t classAndId; // high nibble: 0 DC, 1 AC; low nibble: table id
    const uint8_t* bits;
    const uint8_t* values;
    uint32_t count;
};

constexpr HuffmanTable kStandardTables[] = {
    {0x00, kDcLuminanceBits, kDcValues, sizeof(kDcValues)},
    {0x10, kAcLuminanceBits, kAcLuminanceValues, sizeof(kAcLuminanceValues)},
    {0x01, kDcChrominanceBits, kDcValues, sizeof(kDcValues)},
    {0x11, kAcChrominanceBits, kAcChrominanceValues, sizeof(kAcChrominanceValues)},
};

uint32_t DhtSegmentSize()
{
    uint32_t size = 2 * kMarkerBytes; // marker and length
    for (const HuffmanTable& t : kStandardTables)
        size += 1 + kHuffmanCodeLengths + t.count;
    return size;
}

bool HasDht(const uint8_t* data, uint32_t size)
{
    // Marker segments up to SOS.
    uint32_t pos = kMarkerBytes;
    while (pos + 2 * kMarkerBytes <= size && data[pos] == kJpegMarker)
    {
        const uint8_t marker = data[pos + 1];
        if (marker == kJpegDht)
            return true;
        if (marker == kJpegSos)
            return false;
        pos += kMarkerBytes + LoadBe16(data + pos + kMarkerBytes);
    }
    return false;
}

// Insert the standard tables after SOI of the frame in INFLIGHT [0, size); false: no room.
bool InsertStandardTables(Region& inflight, uint32_t& size)
{
    const uint32_t extra = DhtSegmentSize();
    if (size + extra > inflight.size)
        return false;
    uint8_t* data = inflight.base;
    std::memmove(data + kMarkerBytes + extra, data + kMarkerBytes, size - kMarkerBytes);
    uint8_t* p = data + kMarkerBytes;
    *p++ = kJpegMarker;
    *p++ = kJpegDht;
    *p++ = static_cast<uint8_t>((extra - kMarkerBytes) >> kBitsPerByte);
    *p++ = static_cast<uint8_t>(extra - kMarkerBytes);
    for (const HuffmanTable& t : kStandardTables)
    {
        *p++ = t.classAndId;
        std::memcpy(p, t.bits, kHuffmanCodeLengths);
        p += kHuffmanCodeLengths;
        std::memcpy(p, t.values, t.count);
        p += t.count;
    }
    size += extra;
    inflight.MarkDirtyRange(0, size);
    return true;
}

// --- AVI (RIFF) parsing ----------------------------------------------------------------------------

constexpr uint32_t kFourCc = 4;
constexpr uint32_t kChunkHeader = 8;        // fourcc, size
constexpr uint32_t kRiffHeader = 12;        // "RIFF", size, "AVI "
constexpr uint32_t kListType = 4;           // the type after a LIST header
// avih fields [MainAVIHeader]: microseconds per frame at 0, total frames at 16, size at 32.
constexpr uint32_t kAvihMicroSecPerFrame = 0, kAvihTotalFrames = 16, kAvihWidth = 32, kAvihHeight = 36;
constexpr uint32_t kAvihMinimumSize = 40;
constexpr uint32_t kStreamDigits = 2;       // "00dc": stream number in two decimal digits
constexpr uint32_t kDecimal = 10;
constexpr uint64_t kMicrosecondsPerSecond = 1000000;

enum AviStage : uint32_t
{
    kAviRiff = 0,       // need the RIFF header
    kAviChunk = 1,      // need a chunk header (top level or inside movi)
    kAviListType = 2,   // need the type of a LIST
    kAviHeaderList = 3, // collect the hdrl list
    kAviSkip = 4,       // skip skipRemaining bytes
    kAviFrame = 5,      // collect a video chunk
    kAviEnd = 6         // no more frames
};

enum class AviEvent { None, Header, Frame, End };

uint32_t LoadLe32At(const uint8_t* p)
{
    return LoadLe32(p);
}

bool FourCc(const uint8_t* p, const char* text)
{
    return std::memcmp(p, text, kFourCc) == 0;
}

uint32_t Padded(uint32_t size)
{
    return size + (size & 1);
}

// Parse the collected hdrl list: avih and the first 'vids' stream's index.
bool ParseHeaderList(VideoState& v, const uint8_t* data, uint32_t size)
{
    uint32_t pos = 0;
    uint32_t stream = 0;
    bool haveAvih = false;
    bool haveVideo = false;
    while (pos + kChunkHeader <= size)
    {
        const uint8_t* chunk = data + pos;
        const uint32_t length = LoadLe32At(chunk + kFourCc);
        if (FourCc(chunk, "LIST"))
        {
            pos += kChunkHeader + kListType; // descend into strl
            continue;
        }
        if (pos + kChunkHeader + length > size)
            break;
        const uint8_t* body = chunk + kChunkHeader;
        if (FourCc(chunk, "avih") && length >= kAvihMinimumSize)
        {
            v.framePeriodUs = LoadLe32At(body + kAvihMicroSecPerFrame);
            v.totalFrames = LoadLe32At(body + kAvihTotalFrames);
            v.width = LoadLe32At(body + kAvihWidth);
            v.height = LoadLe32At(body + kAvihHeight);
            haveAvih = true;
        }
        else if (FourCc(chunk, "strh") && length >= kFourCc)
        {
            if (!haveVideo && FourCc(body, "vids"))
            {
                v.videoStream = stream;
                haveVideo = true;
            }
            ++stream;
        }
        pos += kChunkHeader + Padded(length);
    }
    return haveAvih && haveVideo;
}

bool IsVideoChunk(const VideoState& v, const uint8_t* fourcc)
{
    const uint32_t tens = v.videoStream / kDecimal % kDecimal;
    const uint32_t ones = v.videoStream % kDecimal;
    return fourcc[0] == '0' + tens && fourcc[1] == '0' + ones && fourcc[kStreamDigits] == 'd' &&
           (fourcc[kStreamDigits + 1] == 'c' || fourcc[kStreamDigits + 1] == 'b');
}

// Bytes the parser needs before its next step (0: none, the stream is over).
uint32_t AviNeed(const VideoState& v)
{
    switch (v.parserStage)
    {
    case kAviRiff: return kRiffHeader;
    case kAviChunk: return kChunkHeader;
    case kAviListType: return kListType;
    case kAviHeaderList:
    case kAviSkip:
    case kAviFrame: return 1; // collected or skipped in pieces
    default: return 0;
    }
}

void EnterChunk(VideoState& v, uint32_t length)
{
    if (v.inMovi)
        v.moviRemaining -= v.moviRemaining < kChunkHeader + Padded(length) ? v.moviRemaining : kChunkHeader + Padded(length);
}

// Consume source bytes of the AVI file, counting what is left of it.
void VideoConsume(EveChip& chip, uint32_t bytes)
{
    VideoState& v = chip.state.copro.video;
    SourceConsume(chip, bytes);
    v.riffRemaining -= v.riffRemaining < bytes ? v.riffRemaining : bytes;
}

// One parser step over the source; returns what it found. Faults on a broken stream.
AviEvent AviStep(EveChip& chip, uint32_t available)
{
    CoproState& c = chip.state.copro;
    VideoState& v = c.video;
    Region& inflight = chip.regions[RegionInflight];
    uint8_t header[kRiffHeader];
    switch (v.parserStage)
    {
    case kAviRiff:
        SourceCopy(chip, header, kRiffHeader);
        VideoConsume(chip, kRiffHeader);
        if (!FourCc(header, "RIFF") || !FourCc(header + kChunkHeader, "AVI "))
        {
            CoproFaultNow(chip, CoproFault::InvalidImage);
            return AviEvent::None;
        }
        v.riffRemaining = LoadLe32At(header + kFourCc) - kFourCc;
        v.parserStage = kAviChunk;
        return AviEvent::None;
    case kAviChunk:
    {
        if (v.inMovi && v.moviRemaining < kChunkHeader)
        {
            v.parserStage = kAviEnd;
            return AviEvent::End;
        }
        SourceCopy(chip, header, kChunkHeader);
        VideoConsume(chip, kChunkHeader);
        const uint32_t length = LoadLe32At(header + kFourCc);
        v.chunkSize = length;
        if (FourCc(header, "LIST"))
        {
            v.parserStage = kAviListType;
            if (v.inMovi)
                v.moviRemaining -= kChunkHeader;
            return AviEvent::None;
        }
        if (v.inMovi && IsVideoChunk(v, header))
        {
            EnterChunk(v, length);
            c.inputBytes = 0;
            v.parserStage = kAviFrame;
            return AviEvent::None;
        }
        if (v.inMovi && FourCc(header, "idx1"))
        {
            v.parserStage = kAviEnd;
            return AviEvent::End;
        }
        EnterChunk(v, length);
        v.skipRemaining = Padded(length);
        v.parserStage = kAviSkip;
        return AviEvent::None;
    }
    case kAviListType:
    {
        SourceCopy(chip, header, kListType);
        VideoConsume(chip, kListType);
        if (v.inMovi)
            v.moviRemaining -= v.moviRemaining < kListType ? v.moviRemaining : kListType;
        if (FourCc(header, "movi"))
        {
            v.inMovi = 1;
            v.moviRemaining = v.chunkSize - kListType;
            v.parserStage = kAviChunk;
            return AviEvent::Header;
        }
        if (FourCc(header, "hdrl"))
        {
            c.inputBytes = 0;
            v.chunkSize -= kListType;
            v.parserStage = kAviHeaderList;
            return AviEvent::None;
        }
        if (FourCc(header, "rec "))
        {
            v.parserStage = kAviChunk; // its chunks follow as if in movi
            return AviEvent::None;
        }
        v.skipRemaining = Padded(v.chunkSize) - kListType;
        v.parserStage = kAviSkip;
        return AviEvent::None;
    }
    case kAviHeaderList:
    case kAviFrame:
    {
        const uint32_t want = Padded(v.chunkSize) - c.inputBytes;
        const uint32_t take = available < want ? available : want;
        if (c.inputBytes + take > inflight.size)
        {
            CoproFaultNow(chip, CoproFault::InflightOverflow);
            return AviEvent::None;
        }
        SourceCopy(chip, inflight.base + c.inputBytes, take);
        VideoConsume(chip, take);
        inflight.MarkDirtyRange(c.inputBytes, take);
        c.inputBytes += take;
        c.inflightUsed = c.inputBytes;
        if (c.inputBytes < Padded(v.chunkSize))
            return AviEvent::None;
        const bool headerList = v.parserStage == kAviHeaderList;
        c.inputBytes = v.chunkSize; // without the pad byte
        v.parserStage = kAviChunk;
        if (!headerList)
            return AviEvent::Frame;
        if (!ParseHeaderList(v, inflight.base, c.inputBytes))
            CoproFaultNow(chip, CoproFault::InvalidImage);
        c.inputBytes = 0;
        return AviEvent::None;
    }
    case kAviSkip:
    {
        const uint32_t take = available < v.skipRemaining ? available : v.skipRemaining;
        VideoConsume(chip, take);
        v.skipRemaining -= take;
        if (v.skipRemaining == 0)
            v.parserStage = kAviChunk;
        return AviEvent::None;
    }
    default:
        return AviEvent::End;
    }
}

// --- CMD_VIDEOSTART, CMD_VIDEOFRAME, CMD_PLAYVIDEO ---------------------------------------------------

enum VideoStage : uint8_t
{
    kVidParse = 0,    // AVI parser steps
    kVidPace = 1,     // wait until the frame's time (PLAYVIDEO)
    kVidSwapWait = 2, // OPT_NOTEAR: wait for the previous frame's swap
    kVidWrite = 3,    // write the decoded frame
    kVidList = 4,     // PLAYVIDEO: write the display list and swap
    kVidDrain = 5     // ring source: consume the rest of the file (idx1)
};

constexpr uint32_t kOptNotear = 4;
constexpr uint32_t kMaxVideoListWords = 16;
constexpr uint32_t kOpClear = 0x26, kOpBegin = 0x1F, kOpEnd = 0x21, kOpDisplay = 0x00, kOpVertexFormat = 0x27;
constexpr uint32_t kClearAll = 7;            // CLEAR(1, 1, 1)
constexpr uint32_t kBeginBitmaps = 1;
constexpr uint32_t kVertex2fTag = 1u << 30;
constexpr uint32_t kVertex2fXShift = 15;
constexpr uint32_t kVertex2fMask = 0x7FFF;

uint64_t FramePeriodClocks(const EveChip& chip)
{
    const uint64_t hz = kVideoPacedByFrequency ? RegGet(chip, Reg::Frequency) : chip.state.power.systemClockHz;
    return static_cast<uint64_t>(chip.state.copro.video.framePeriodUs) * hz / kMicrosecondsPerSecond;
}

// The display list showing the decoded frame (design, spec V18).
uint32_t VideoList(const EveChip& chip, uint32_t* words)
{
    const CoproState& c = chip.state.copro;
    const uint32_t format = ImageFormat(c);
    const uint32_t stride = c.flightWidth * BytesPerPixel(format);
    const uint32_t hsize = RegGet(chip, Reg::Hsize);
    const uint32_t vsize = RegGet(chip, Reg::Vsize);
    uint32_t scale = kTransformOne; // 8.8: texels per screen pixel
    if ((c.params[0] & kOptFullscreen) && hsize != 0 && vsize != 0)
    {
        const uint32_t sx = (c.flightWidth * kTransformOne + hsize - 1) / hsize;
        const uint32_t sy = (c.flightHeight * kTransformOne + vsize - 1) / vsize;
        scale = sx > sy ? sx : sy;
        scale = scale == 0 ? 1 : scale;
    }
    const uint32_t width = c.flightWidth * kTransformOne / scale;
    const uint32_t height = c.flightHeight * kTransformOne / scale;
    const int32_t x = (static_cast<int32_t>(hsize) - static_cast<int32_t>(width)) / 2;
    const int32_t y = (static_cast<int32_t>(vsize) - static_cast<int32_t>(height)) / 2;
    uint32_t n = 0;
    words[n++] = Dl(kOpClear, kClearAll);
    words[n++] = Dl(kOpBitmapSource, c.writeAddress & kAddressMask22);
    words[n++] = Dl(kOpBitmapLayout, (format << kLayoutFormatShift) | ((stride & kLowMask10) << kLayoutStrideShift) |
                                         (c.flightHeight & kLowMask9));
    words[n++] = Dl(kOpBitmapLayoutH, ((stride >> kLowStrideBits) << kHighFieldShift) | (c.flightHeight >> kLowSizeBits));
    words[n++] = Dl(kOpBitmapSize, ((width & kLowMask9) << kSizeWidthShift) | (height & kLowMask9));
    words[n++] = Dl(kOpBitmapSizeH, ((width >> kLowSizeBits) << kHighFieldShift) | (height >> kLowSizeBits));
    if (scale != kTransformOne)
    {
        words[n++] = Dl(kOpBitmapTransformA, scale);
        words[n++] = Dl(kOpBitmapTransformE, scale);
    }
    words[n++] = Dl(kOpBegin, kBeginBitmaps);
    words[n++] = Dl(kOpVertexFormat, 0);
    words[n++] = kVertex2fTag | ((static_cast<uint32_t>(x) & kVertex2fMask) << kVertex2fXShift) |
                 (static_cast<uint32_t>(y) & kVertex2fMask);
    words[n++] = Dl(kOpEnd, 0);
    words[n++] = Dl(kOpDisplay, 0);
    return n;
}

void WriteMemory32(EveChip& chip, uint32_t address, uint32_t value)
{
    for (uint32_t i = 0; i < kFourCc; ++i)
        BusWrite(chip, address + i, static_cast<uint8_t>(value >> (kBitsPerByte * i)));
    FlushPendingRegister(chip);
}

void FinishVideo(EveChip& chip, bool frameLoaded)
{
    CoproState& c = chip.state.copro;
    if (c.command == kCmdVideoframe)
    {
        // The completion pointer: 1 while more frames follow, 0 at the last [PG §5.23].
        const bool more = frameLoaded && c.video.frameIndex < c.video.totalFrames;
        WriteMemory32(chip, c.params[1], more ? 1 : 0);
    }
    if (c.command == kCmdPlayvideo && !FromFifo(chip) && c.video.riffRemaining != 0)
    {
        c.stage = kVidDrain;
        return;
    }
    chip.imageDecoded = false;
    CompleteCommand(chip);
}

StepPlan VideoPlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    VideoState& v = c.video;
    switch (c.stage)
    {
    case kVidParse:
    {
        if (c.command == kCmdVideoframe && !v.ready)
            return StepPlan{true, 0, 0}; // no CMD_VIDEOSTART: finishes with "no more frames"
        const uint32_t need = AviNeed(v);
        if (need == 0)
            return StepPlan{true, 0, 0};
        const uint32_t available = SourceAvailable(chip);
        if (available < need)
        {
            c.phase = EVE_COPRO_WAITING_DATA;
            return NotReady();
        }
        return StepPlan{true, available, 0};
    }
    case kVidPace:
    {
        const uint64_t deadline = v.startClock + static_cast<uint64_t>(v.frameIndex) * FramePeriodClocks(chip);
        const uint64_t now = chip.state.scan.clocksSinceReset;
        return StepPlan{true, 0, deadline > now ? deadline - now : 0};
    }
    case kVidSwapWait:
        if (RegGet(chip, Reg::Dlswap) != 0)
        {
            c.phase = EVE_COPRO_WAITING_SWAP;
            return NotReady();
        }
        return StepPlan{true, 0, 0};
    case kVidWrite:
        return WritePlan(chip);
    case kVidList:
    {
        uint32_t words[kMaxVideoListWords];
        const uint32_t n = VideoList(chip, words);
        return StepPlan{true, n, Cost(n, chip.costs.displayListWord)};
    }
    default:
    {
        const uint32_t available = SourceAvailable(chip);
        if (available == 0)
        {
            c.phase = EVE_COPRO_WAITING_DATA;
            return NotReady();
        }
        return StepPlan{true, available < v.riffRemaining ? available : v.riffRemaining, 0};
    }
    }
}

void VideoApply(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    VideoState& v = c.video;
    switch (c.stage)
    {
    case kVidParse:
    {
        if (c.command == kCmdVideoframe && !v.ready)
        {
            FinishVideo(chip, false);
            return;
        }
        const AviEvent event = v.parserStage == kAviEnd ? AviEvent::End : AviStep(chip, units);
        if (c.phase == EVE_COPRO_FAULT)
            return;
        if (event == AviEvent::Header)
        {
            v.ready = 1;
            v.frameIndex = 0;
            v.startClock = chip.state.scan.clocksSinceReset;
            if (c.command == kCmdVideostart)
                CompleteCommand(chip);
            return;
        }
        if (event == AviEvent::End)
        {
            FinishVideo(chip, false);
            return;
        }
        if (event == AviEvent::Frame)
        {
            Region& inflight = chip.regions[RegionInflight];
            uint32_t size = c.inputBytes;
            if (!HasDht(inflight.base, size) && !InsertStandardTables(inflight, size))
            {
                CoproFaultNow(chip, CoproFault::InflightOverflow);
                return;
            }
            c.inputBytes = size;
            c.inflightUsed = size;
            if (c.command == kCmdVideoframe)
                StartWriting(chip, c.params[0], kVidWrite);
            else
                StartWriting(chip, kVideoFrameAddress, kVidPace);
        }
        return;
    }
    case kVidPace:
        c.stage = (c.params[0] & kOptNotear) ? kVidSwapWait : kVidWrite;
        return;
    case kVidSwapWait:
        c.stage = kVidWrite;
        return;
    case kVidWrite:
        if (!WritePixels(chip, units))
            return;
        ++v.frameIndex;
        if (c.command == kCmdVideoframe)
        {
            FinishVideo(chip, true);
            return;
        }
        c.stage = kVidList;
        return;
    case kVidList:
    {
        uint32_t words[kMaxVideoListWords];
        const uint32_t n = VideoList(chip, words);
        RegSet(chip, Reg::CmdDl, 0);
        c.displayListFull = 0;
        for (uint32_t i = 0; i < n; ++i)
            if (!WriteDlWord(chip, words[i]))
                return;
        WriteRegister32(chip, Reg::Dlswap, kDlswapFrameRequest);
        chip.imageDecoded = false;
        c.stage = kVidParse;
        return;
    }
    default:
        VideoConsume(chip, units);
        if (v.riffRemaining == 0)
        {
            if (!FromFifo(chip))
                AlignRing(chip);
            CompleteCommand(chip);
        }
        return;
    }
}

} // namespace

bool MediaBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdMediafifo:
        c.mediaFifoBase = c.params[0];
        c.mediaFifoSize = c.params[1];
        // The pointers are offsets in the FIFO, both 0 after CMD_MEDIAFIFO (BT8XX, golden
        // case mediafifo-pointers).
        RegSet(chip, Reg::MediafifoRead, 0);
        RegSet(chip, Reg::MediafifoWrite, 0);
        return true;
    case kCmdLoadimage:
        c.stage = kCollect;
        c.scanPos = 0;
        c.scanEntropy = 0;
        c.inputBytes = 0;
        chip.imageDecoded = false;
        return false;
    case kCmdVideostart:
    case kCmdPlayvideo:
        c.video = VideoState{};
        c.stage = kVidParse;
        chip.imageDecoded = false;
        return false;
    case kCmdVideoframe:
        c.stage = kVidParse;
        chip.imageDecoded = false;
        return false;
    default:
        return true;
    }
}

StepPlan MediaPlan(EveChip& chip)
{
    switch (chip.state.copro.command)
    {
    case kCmdLoadimage: return LoadImagePlan(chip);
    case kCmdVideostart:
    case kCmdVideoframe:
    case kCmdPlayvideo: return VideoPlan(chip);
    default: return StepPlan{true, 0, 0};
    }
}

void MediaApply(EveChip& chip, uint32_t units)
{
    switch (chip.state.copro.command)
    {
    case kCmdLoadimage:
        LoadImageApply(chip, units);
        break;
    case kCmdVideostart:
    case kCmdVideoframe:
    case kCmdPlayvideo:
        VideoApply(chip, units);
        break;
    default:
        CompleteCommand(chip);
        break;
    }
}

void MediaStateLoaded(EveChip& chip)
{
    chip.imageDecoded = false; // the decoded image is derived: decoded again when needed
}

} // namespace EveLib
