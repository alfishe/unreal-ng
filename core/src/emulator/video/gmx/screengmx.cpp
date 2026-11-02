#include "screengmx.h"

#include "emulator/memory/memory.h"
#include "emulator/video/gmx/gmxgeometry.h"
#include "emulator/video/zx/zxgeometry.h"

/// region <Constructors / Destructors>

ScreenGmx::ScreenGmx(EmulatorContext* context, Memory* memory) : _context(context), _memory(memory)
{
}

/// endregion </Constructors / Destructors>

/// @brief GMX 640x200 renderer, one call per T-state.
///
/// Fetch (GMX design 2.5): pixel page #39 (#3B with #7FFD bit 3), attribute page #79 (#7B) at the same offset, 80
/// linear bytes per line, one attribute per 8x1 pixels; the byte of line y, column x is (y * 80 + x + start) mod 16000
/// with the scroller's start address (#7AFD / #7CFD). The attribute byte is the standard one: ink 0-2, paper 3-5,
/// bright 6, flash 7; the 16 fixed ZX colours.
/// Timing: the Scorpion's 312 x 224T beam. The 640 px paper is drawn at 5 px/T inside the 128 T paper window
/// (T 24..151 of the line), the 200 lines centred on the standard 192 (4 lines above it). Border at 2 px/T.
void ScreenGmx::Draw(uint32_t tstate, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer,
                     uint8_t borderColor, bool flashPhase)
{
    using namespace GmxGeometry;

    if (framebuffer.memoryBuffer == nullptr)
        return;

    const uint32_t line = tstate / kTStatesPerLine;
    const uint32_t tInLine = tstate % kTStatesPerLine;

    if (line < kVSyncVBlankLines || line >= kVSyncVBlankLines + kVisibleLines)
        return;
    if (tInLine >= kVisibleEndT)
        return;

    uint32_t* framebufferARGB = reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer);
    const uint32_t fbRow = line - kVSyncVBlankLines;
    const uint32_t rowOffset = fbRow * rd.fullFrameWidth;

    const bool inScreenRow = (fbRow >= rd.screenOffsetTop) && (fbRow < rd.screenOffsetTop + kScreenLines);

    if (!inScreenRow || tInLine < kPaperStartT || tInLine >= kPaperEndT)
    {
        const uint32_t border = ZxGeometry::Colour(static_cast<uint8_t>(borderColor & 0x07));
        uint32_t x;
        uint32_t count;
        if (tInLine < kPaperStartT)
        {
            x = 2 * tInLine;
            count = 2;
        }
        else if (tInLine >= kPaperEndT)
        {
            x = rd.screenOffsetLeft + rd.screenWidth + 2 * (tInLine - kPaperEndT);
            count = 2;
        }
        else
        {
            x = rd.screenOffsetLeft + kPixelsPerT * (tInLine - kPaperStartT);
            count = kPixelsPerT;
        }

        for (uint32_t k = 0; k < count && x + k < rd.fullFrameWidth; ++k)
            framebufferARGB[rowOffset + x + k] = border;
        return;
    }

    const EmulatorState& state = _context->emulatorState;
    const uint8_t* pixelBase = _memory->RAMPageAddress(PixelPage(state.p7FFD));
    const uint8_t* attrBase = _memory->RAMPageAddress(AttrPage(state.p7FFD));
    if (pixelBase == nullptr || attrBase == nullptr)
        return;

    const uint32_t y = fbRow - rd.screenOffsetTop;
    const uint32_t start = ScrollStart(state.gmx.p7AFD, state.gmx.p7CFD);
    const uint32_t firstPixel = kPixelsPerT * (tInLine - kPaperStartT);

    for (uint32_t k = 0; k < kPixelsPerT; ++k)
    {
        const uint32_t px = firstPixel + k;
        const uint16_t offset = ByteOffset(y, px / 8, start);
        const uint8_t attr = attrBase[offset];
        const bool ink = Ink(pixelBase[offset], px % 8, attr, flashPhase);
        framebufferARGB[rowOffset + rd.screenOffsetLeft + px] = ZxGeometry::Colour(AttrColourIndex(attr, ink));
    }
}

void ScreenGmx::DrawRange(uint32_t from, uint32_t to, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer,
                          uint8_t borderColor, bool flashPhase)
{
    for (uint32_t t = from; t <= to; ++t)
        Draw(t, rd, framebuffer, borderColor, flashPhase);
}
