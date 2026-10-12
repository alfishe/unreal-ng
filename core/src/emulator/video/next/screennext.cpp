#include "stdafx.h"

#include "screennext.h"

#include <algorithm>
#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

ScreenNext::ScreenNext(EmulatorContext* context) : Screen(context)
{
    _vid.mode = M_NEXT;
    SetVideoMode(M_NEXT);
    _layerWanted.store(context && context->config.next_layer_capture != 0);  // [NEXT] LayerCapture, on unless the ini says off
}

void ScreenNext::SetLayerCapture(bool enabled)
{
    _layerWanted.store(enabled);
    if (_context)
        _context->config.next_layer_capture = enabled ? 1 : 0;  // a screen made later (a model switch) starts the same way
}

PortDecoder_Next* ScreenNext::Decoder() const
{
    // The decoder is created after the screen (Core::Init) and replaced on a model switch
    if (_context && (!_decoder || static_cast<PortDecoder*>(_decoder) != _context->pPortDecoder))
        _decoder = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
    return _decoder;
}

void ScreenNext::InitRaster()
{
    // One mode: the family of NR #03 only changes the beam (GetTimingDescriptor), which SetVideoMode reads again
    if (_mode != M_NEXT)
        SetVideoMode(M_NEXT);
    else if (_state && _lastTimingClass != _state->ula_timing_class)
        SetVideoMode(M_NEXT);
}

void ScreenNext::InitFrame()
{
    Screen::InitFrame();
    _nextLine = 0;
    _nextX = 0;
    PrepareLayerFrame();
}

void ScreenNext::PrepareLayerFrame()
{
    if (!_layerWanted.load())
    {
        if (_layerDraw || _layerLast)
        {
            std::lock_guard<std::mutex> lock(_layerMutex);
            _layerDraw.reset();
            _layerLast.reset();
            _layerBytes.store(0);
        }
        _layerRecording = false;
        return;
    }
    if (!_layerDraw)
    {
        _layerDraw = std::make_shared<NextLayer::Frame>();
        _layerDraw->Allocate();
        _layerBytes.store(NextLayer::Frame::Bytes() * (_layerLast ? 2 : 1));
    }
    _layerDraw->BeginFrame();
    _layerRecording = true;
}

void ScreenNext::CompleteLayerFrame()
{
    _layerRecording = false;
    _layerDraw->sequence = _layerFrames.fetch_add(1) + 1;
    _layerDraw->frameCounter = _state ? _state->frame_counter : 0;
    std::shared_ptr<NextLayer::Frame> spare;
    {
        std::lock_guard<std::mutex> lock(_layerMutex);
        std::swap(_layerLast, _layerDraw);  // draw now holds the set before last (or null at the first frame)
        // a reader still holding it keeps its set alive; the renderer takes a fresh one then
        if (_layerDraw && _layerDraw.use_count() == 1)
            spare = std::move(_layerDraw);
        _layerDraw.reset();
    }
    if (!spare)
    {
        spare = std::make_shared<NextLayer::Frame>();
        spare->Allocate();
    }
    _layerDraw = std::move(spare);
    _layerBytes.store(2 * NextLayer::Frame::Bytes());
}

std::shared_ptr<const NextLayer::Frame> ScreenNext::LastLayerFrame() const
{
    std::lock_guard<std::mutex> lock(_layerMutex);
    return _layerLast;
}

void ScreenNext::SetVideoMode([[maybe_unused]] VideoModeEnum mode)
{
    Screen::SetVideoMode(M_NEXT);
    _lastTimingClass = _state ? _state->ula_timing_class : 2;
    if (_copperBound)
        UpdateCopperGeometry();  // the raster of the new family
}

bool ScreenNext::FramebufferReady() const
{
    return _framebuffer.memoryBuffer && _framebuffer.width == NextVideoRenderer::kWidth && _framebuffer.height == 2 * NextVideoRenderer::kHeight;
}

uint32_t ScreenNext::LineEndT(unsigned y) const
{
    // Visible line 0 is 16 lines below the ZX window's first (its 48-line border keeps 32 here)
    const uint32_t perLine = _rasterState.tstatesPerLine;
    return _rasterState.topBorderAreaStart + (16 + y + 1) * perLine;
}

uint64_t ScreenNext::CopperClock(uint32_t tstate) const
{
    // The copper's lines count from the first paper line; its horizontal 0 is 11 pixels before the first paper pixel. In 7 MHz counts the map
    // is linear modulo the frame: ((2 t + B) mod frame7) * 4, B constant for a raster (cached: this runs after every instruction while the
    // copper works)
    const uint32_t perLine = _rasterState.tstatesPerLine;
    if (_copperKey[0] != _rasterState.maxFrameTiming || _copperKey[1] != perLine || _copperKey[2] != _rasterState.screenAreaStart ||
        _copperKey[3] != _rasterState.screenLineAreaStart)
    {
        _copperKey[0] = _rasterState.maxFrameTiming;
        _copperKey[1] = perLine;
        _copperKey[2] = _rasterState.screenAreaStart;
        _copperKey[3] = _rasterState.screenLineAreaStart;
        _copperFrame7 = static_cast<uint64_t>(_rasterState.maxFrameTiming) * 2;
        const uint64_t origin7 = (static_cast<uint64_t>(_rasterState.screenAreaStart / perLine) * perLine + _rasterState.screenLineAreaStart) * 2;
        _copperB = _copperFrame7 + 11 - origin7 % _copperFrame7;
    }
    uint64_t t7 = static_cast<uint64_t>(tstate) * 2 + _copperB;
    while (t7 >= _copperFrame7)
        t7 -= _copperFrame7;
    return t7 * 4;
}

void ScreenNext::UpdateCopperGeometry()
{
    if (PortDecoder_Next* decoder = Decoder())
        decoder->Board().Copper().SetGeometry(_rasterState.tstatesPerLine * 2, _rasterState.maxFrameTiming / _rasterState.tstatesPerLine);
}

void ScreenNext::BindCopper(NextCopper& copper)
{
    copper.SetNow([this]() { return CopperClockNow(); });
    copper.SetBeforeWrite([this](uint64_t clock, uint8_t reg) { RenderBeforeCopperClock(clock, reg); });
    copper.SetGeometry(_rasterState.tstatesPerLine * 2, _rasterState.maxFrameTiming / _rasterState.tstatesPerLine);
    _copperBound = true;
}

unsigned ScreenNext::PixelLatencyClocks(uint8_t reg)
{
    // Counted against zxnext.vhd (the output of the Next is a chain of CLK_14 registers; the copper's write is E+4 after the WAIT is true):
    //  - NR #4A fallback colour, NR #14 global transparency: fallback_rgb_0 (first CLK_14 edge after the write), _1a, _1, _2, then
    //    rgb_out_3..6 and rgb_out_o: 22 clocks from the clock the copper sees the WAIT true, i.e. 20 from the MOVE; the ULA pixel itself
    //    leaves at its own hc (it is fetched 4 pixels ahead), so this is the delay against it
    //  - the palette values (NR #41, NR #44): the lookup is at stage 1, E+4.5 + 4k for pixel k: 16 clocks
    // Not counted (0): the layer / sprite enables, the scroll and offset registers, the palette selection - they land later than the
    // write, by an amount not worked out
    constexpr unsigned kFallbackClocks = 22, kPaletteClocks = 16;
    switch (reg)
    {
        case 0x4A:
        case 0x14:
            return kFallbackClocks;
        case 0x41:
        case 0x44:
            return kPaletteClocks;
        default:
            return 0;
    }
}

void ScreenNext::RenderBeforeCopperClock(uint64_t clock, uint8_t reg)
{
    if (_turboRenderSkip || !FramebufferReady())
        return;
    // Where the write is in the frame: the copper clock is at most an instruction behind the beam, so the T-state is the beam's minus the
    // difference (this works across the copper's own frame wrap, which does not fall on the frame start)
    const uint64_t frameClocks = _copperFrame7 * 4;
    if (frameClocks == 0)
        return;
    const uint64_t now = CopperClockNow();
    const uint64_t behind = (now + frameClocks - clock) % frameClocks;
    constexpr uint64_t kClocksPerTState = 8;  // 28 MHz clocks in a 3.5 MHz T-state
    const uint32_t t = GetCurrentTstate();
    if (behind / kClocksPerTState > t)
        return;  // the previous frame's tail: that frame is drawn to its end already
    const uint32_t at = t - static_cast<uint32_t>(behind / kClocksPerTState);
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint32_t first = _rasterState.topBorderAreaStart + 16 * perLine;
    if (at < first)
        return;  // the vertical blank before the first visible line
    const unsigned line = (at - first) / perLine;
    if (line >= NextVideoRenderer::kHeight)
    {
        RenderUpTo(NextVideoRenderer::kHeight, 0);
        return;
    }
    // x = 0 of the line in copper time, and the pixel of the write from the copper clock itself (a quarter pixel finer than the T-state)
    const uint32_t lineStart = first + line * perLine;
    const uint64_t lineClocks = static_cast<uint64_t>(perLine) * 8;
    const uint64_t x0 = CopperClock(lineStart + _rasterState.screenLineAreaStart - 16);
    const uint64_t delta = (clock + frameClocks - x0) % frameClocks;
    if (delta >= lineClocks)
    {
        RenderUpTo(line, 0);
        return;
    }
    // the pixels before the write's effect reaches the output keep the old state: the effect is PixelLatencyClocks after the write, and the
    // image's pixels are 14 MHz (two 28 MHz clocks each)
    constexpr unsigned kClocksPerImagePixel = 2, kImagePixels = 640;
    const unsigned half = static_cast<unsigned>(std::min<uint64_t>((delta + PixelLatencyClocks(reg)) / kClocksPerImagePixel, kImagePixels));
    RenderUpTo(line, half);
}

void ScreenNext::RenderLinesUpTo(unsigned lineExclusive)
{
    RenderUpTo(lineExclusive, 0);
}

void ScreenNext::RenderUpTo(unsigned lineLimit, unsigned xLimit)
{
    PortDecoder_Next* decoder = Decoder();
    if (!decoder || !FramebufferReady())
        return;
    lineLimit = std::min<unsigned>(lineLimit, NextVideoRenderer::kHeight);
    constexpr unsigned kW = NextVideoRenderer::kWidth;
    constexpr unsigned kPixels = kW;  // the image line in 14 MHz pixels: 320 grid pixels of 32 border + 256 paper + 32 border, doubled
    if (_nextLine > lineLimit || (_nextLine == lineLimit && _nextX >= xLimit))
        return;
    if (!_copperBound)
        BindCopper(decoder->Board().Copper());

    NextBoard& board = decoder->Board();
    NextVideoInputs in;
    in.ram = _context->pMemory->RAMPageAddress(0);
    in.ramPages = static_cast<NextMemory*>(_context->pMemory)->InstalledRamPages();
    in.regs = &board.Video();
    in.sprites = &board.Sprites();
    in.flash = _state && (_state->frame_counter & 0x10);
    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);
    auto refresh = [&]() {
        // what the copper or the program may have changed since the pixels before
        for (unsigned r = 0; r < 256; r++)
            in.nr[r] = board.Stored(static_cast<uint8_t>(r));
        in.portFf = board.Video().PortFf();
        in.shadowScreen = _state && (_state->p7FFD & 0x08);
        in.layer2Enable = board.Video().Layer2Enabled();
        in.border = _state ? static_cast<uint8_t>(_state->pFE & 7) : 0;
    };
    uint32_t segment[kW];
    // The layer record: a segment that covers the whole line is rendered straight into the frame's rows; a partial one goes through a
    // line of scratch and only its x-range is copied, exactly as the composed row is
    NextLayer::Frame* record = _layerRecording ? _layerDraw.get() : nullptr;
    NextLayer::Pixel scratchPlane[NextLayer::kPlanes][kW];
    uint8_t scratchIds[kW], scratchInfo[kW];
    NextLayer::LineSink scratchSink;
    for (unsigned p = 0; p < NextLayer::kPlanes; p++)
        scratchSink.plane[p] = scratchPlane[p];
    scratchSink.ids = scratchIds;
    scratchSink.info = scratchInfo;
    while (_nextLine < lineLimit || (_nextLine == lineLimit && _nextX < xLimit))
    {
        NextLayer::LineSink lineSink;
        if (record)
            lineSink = record->Line(_nextLine);
        uint32_t* row = fb + static_cast<size_t>(_nextLine) * 2 * kW;
        const unsigned toX = _nextLine < lineLimit ? kPixels : std::min(xLimit, kPixels);
        const unsigned fromX = _nextX;
        if (toX > fromX)
        {
            refresh();
            const bool whole = fromX == 0 && toX >= kPixels;
            in.layerOut = record ? (whole ? &lineSink : &scratchSink) : nullptr;
            if (record)
                NextVideoRenderer::RecordSegment(*record, _nextLine, fromX, toX, in);
            NextVideoRenderer::RenderLine(in, _nextLine, segment);
            std::memcpy(row + fromX, segment + fromX, (toX - fromX) * sizeof(uint32_t));
            if (record && !whole)
            {
                for (unsigned p = 0; p < NextLayer::kPlanes; p++)
                    std::memcpy(lineSink.plane[p] + fromX, scratchPlane[p] + fromX, (toX - fromX) * sizeof(NextLayer::Pixel));
                std::memcpy(lineSink.ids + fromX, scratchIds + fromX, (toX - fromX));
                std::memcpy(lineSink.info + fromX, scratchInfo + fromX, (toX - fromX));
            }
            _nextX = toX;
        }
        if (_nextX < kPixels)
            break;  // the limit is inside this line
        std::memcpy(row + kW, row, kW * sizeof(uint32_t));  // every line twice
        _nextLine++;
        _nextX = 0;
    }
    if (record && _nextLine >= NextVideoRenderer::kHeight)
        CompleteLayerFrame();
}

void ScreenNext::UpdateScreen()
{
    if (_turboRenderSkip || !_feature_screenhq_enabled)
        return;
    const uint32_t now = GetCurrentTstate();
    // lines whose end the beam has passed
    unsigned done = 0;
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint32_t first = _rasterState.topBorderAreaStart + 16 * perLine;
    if (now > first)
        done = (now - first) / perLine;
    RenderLinesUpTo(done);
}

void ScreenNext::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    (void)fromTstate;
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint32_t first = _rasterState.topBorderAreaStart + 16 * perLine;
    unsigned done = 0;
    if (toTstate + 1 > first)
        done = (toTstate + 1 - first) / perLine;
    RenderLinesUpTo(done);
}

void ScreenNext::RenderFrameBatch()
{
    // ScreenHQ off: the whole frame at its end, with the state of that moment
    RenderLinesUpTo(NextVideoRenderer::kHeight);
}

void ScreenNext::RenderOnlyMainScreen()
{
    RenderLinesUpTo(NextVideoRenderer::kHeight);
}

void ScreenNext::FillBorderWithColor([[maybe_unused]] uint8_t color)
{
    // The border is the ULA layer of every line
}
