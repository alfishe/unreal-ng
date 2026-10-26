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

void ScreenNext::DetectRaster()
{
    // One mode: the family of NR #03 only changes the beam (GetTimingDescriptor), which SetVideoMode reads again
    if (_mode != M_NEXT)
        SetVideoMode(M_NEXT);
    else if (_state && (_lastTimingClass != _state->ula_timing_class || _lastSixty != _state->ula_sixty_hz))
        SetVideoMode(M_NEXT);
}

void ScreenNext::InitFrame()
{
    // A frame start right after the frame counter moved on is the previous frame's natural end (the CPU's clock is back at the frame's
    // start already): the passes it has not run yet (a turbo frame with nothing drawn) run with the state at its end, and its late events
    // carry over. Another start in the same frame (the geometry set again) keeps what has run; any other start (a reset, a state load) has
    // no passes behind it: the ones before now are what the state holds
    const uint64_t counter = _state ? _state->frame_counter : 0;
    const bool natural = _passCounterValid && counter == _passCounter + 1;
    const bool again = _passCounterValid && counter == _passCounter && _passNext == _passes.first;
    if (natural)
        FinishSpriteFrame();
    Screen::InitFrame();
    _nextLine = 0;
    _nextX = 0;
    const bool closed = _passFrameClosed;
    PortDecoder_Next* decoder = Decoder();
    if (closed && decoder)
        decoder->Board().Sprites().ShiftClock(-_passes.frame);
    _passFrameClosed = false;
    if (!again)
        _frameSeq++;
    SetupSpritePasses();
    _passNext = _passes.first;
    _passCursorValid = false;
    if (!closed && !again)
    {
        const int64_t now = NowClock();
        while (_passNext < PassLimit() && PassStart(_passNext) <= now)
            _passNext++;
    }
    _passEndValid = false;
    _passCounter = counter;
    _passCounterValid = true;
    PrepareLayerFrame();
}

void ScreenNext::SetupSpritePasses()
{
    // paper row R is filled during row R - 1, from about raw hc -21 of it (28 MHz clocks: 1 a sprite, 17 more for a drawn one); raw hc 0
    // is 16 T-states before the grid's x = 0 (POC 028)
    const int64_t perLine = _rasterState.tstatesPerLine;
    _passes.line = perLine * 8;
    _passes.frame = static_cast<int64_t>(_rasterState.maxFrameTiming) * 8;
    _passes.lines = perLine ? _rasterState.maxFrameTiming / static_cast<uint32_t>(perLine) : 0;
    _passes.base = (static_cast<int64_t>(GridFirstT()) + static_cast<int64_t>(_rasterState.screenLineAreaStart) - 32) * 8 - 84;
    _passes.first = 0;
    if (_passes.line)
    {
        // the first g with base + g * line >= 0
        int64_t g = -(_passes.base / _passes.line);
        while (PassStart(static_cast<int>(g)) < 0)
            g++;
        while (PassStart(static_cast<int>(g - 1)) >= 0)
            g--;
        _passes.first = static_cast<int>(g);
    }
}

unsigned ScreenNext::PassVcount(int g, bool& nextFrame) const
{
    // zxula_timing.vhd 494-507: wvc is 0 on the grid's line 0, restarts at 510 two lines above it and counts on through the frame's end
    // into the next frame's top lines. A pass at the frame's end (the 60 Hz grid starts at the frame's top) belongs to the next frame's
    const int lines = static_cast<int>(_passes.lines);
    int w = g;
    nextFrame = false;
    if (w < -2)
        w += lines;
    else if (w >= lines - 2)
    {
        w -= lines;
        nextFrame = true;
    }
    const unsigned wvc = w < 0 ? static_cast<unsigned>(512 + w) : static_cast<unsigned>(w);
    return (wvc + 1) & 0x1FF;
}

int64_t ScreenNext::PassEnd(int g)
{
    PortDecoder_Next* decoder = Decoder();
    const NextSprites& sprites = decoder->Board().Sprites();
    if (!_passEndValid || _passEndOf != g || _passEndEpoch != sprites.Epoch())
    {
        bool next;
        const unsigned vcount = PassVcount(g, next);
        _passEnd = PassStart(g) + (sprites.AnyVisible() ? static_cast<int64_t>(sprites.FillClocks(vcount)) : 0);
        _passEndOf = g;
        _passEndEpoch = sprites.Epoch();
        _passEndValid = true;
    }
    return _passEnd;
}

void ScreenNext::RunSpritePass(int g, int64_t now)
{
    PortDecoder_Next* decoder = Decoder();
    NextBoard& board = decoder->Board();
    NextSprites& sprites = board.Sprites();
    bool next;
    const unsigned vcount = PassVcount(g, next);
    NextSprites::LineBuffer& line = vcount < NextVideoRenderer::kHeight ? _rowLine[vcount] : _passScratch;
    const unsigned lineClocks = static_cast<unsigned>(_passes.line);
    // a pass a write split goes on from where the engine stood
    NextSprites::FillCursor fresh;
    NextSprites::FillCursor& cursor = _passCursorValid && _passCursorOf == g ? _passCursor : fresh;
    _passCursorValid = false;
    const bool overtime = sprites.FillUntil(cursor, ~0u, vcount, board.Stored(0x15), board.Stored(0x4B), lineClocks, line, _passTicks);
    if (vcount < NextVideoRenderer::kHeight)
        _rowSeq[vcount] = _frameSeq + (next ? 1 : 0);
    if (overtime || !_passTicks.empty())
        sprites.PostFill(PassStart(g), _passTicks, overtime, lineClocks, now);
}

void ScreenNext::AdvanceSpritePass(int g, int64_t now)
{
    NextBoard& board = Decoder()->Board();
    bool next;
    const unsigned vcount = PassVcount(g, next);
    NextSprites::LineBuffer& line = vcount < NextVideoRenderer::kHeight ? _rowLine[vcount] : _passScratch;
    if (!_passCursorValid || _passCursorOf != g)
    {
        _passCursor = NextSprites::FillCursor();
        _passCursorOf = g;
        _passCursorValid = true;
    }
    const int64_t stop = now - PassStart(g);
    board.Sprites().FillUntil(_passCursor, static_cast<unsigned>(stop), vcount, board.Stored(0x15), board.Stored(0x4B),
                              static_cast<unsigned>(_passes.line), line, _passTicks);
}

void ScreenNext::RunSpritePassesStartedBy(int64_t now)
{
    if (!Decoder() || !_passes.line)
        return;
    while (_passNext < PassLimit() && PassStart(_passNext) <= now)
        RunSpritePass(_passNext++, now);
}

void ScreenNext::RunSpritePassesEndedBy(int64_t now)
{
    if (!Decoder() || !_passes.line)
        return;
    while (_passNext < PassLimit() && PassEnd(_passNext) <= now)
        RunSpritePass(_passNext++, now);
}

uint8_t ScreenNext::ReadSpriteStatus()
{
    PortDecoder_Next* decoder = Decoder();
    if (!decoder)
        return 0;
    const int64_t now = NowClock();
    RunSpritePassesStartedBy(now);
    return decoder->Board().Sprites().ReadStatus(now);
}

void ScreenNext::RestartSpriteFrame()
{
    _passFrameClosed = false;
    _passNext = _passes.first;
    _passCursorValid = false;
    const int64_t now = NowClock();
    while (_passNext < PassLimit() && PassStart(_passNext) <= now)
        _passNext++;
    _passEndValid = false;
    _passCounter = _state ? _state->frame_counter : 0;
    _passCounterValid = true;
}

void ScreenNext::FinishSpriteFrame()
{
    if (!Decoder() || !_passes.line)
        return;
    while (_passNext < PassLimit())
        RunSpritePass(_passNext++, _passes.frame);
    _passFrameClosed = true;
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
    _lastSixty = _state ? _state->ula_sixty_hz : 0;
    if (_copperBound)
        UpdateCopperGeometry();  // the raster of the new family
}

bool ScreenNext::FramebufferReady() const
{
    return _framebuffer.memoryBuffer && _framebuffer.width == NextVideoRenderer::kWidth && _framebuffer.height == 2 * NextVideoRenderer::kHeight;
}

uint32_t ScreenNext::GridFirstT() const
{
    // Visible line 0 is 16 lines below the ZX window's first (its 48-line border keeps 32 here). The 60 Hz frame is cut to the grid:
    // its first line after the blank ones is the grid's
    return _rasterState.topBorderAreaStart + (_lastSixty && _lastTimingClass != 4 ? 0 : 16) * static_cast<uint32_t>(_rasterState.tstatesPerLine);
}

uint32_t ScreenNext::LineEndT(unsigned y) const
{
    return GridFirstT() + (y + 1) * _rasterState.tstatesPerLine;
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

void ScreenNext::SpriteStateWillChange(int64_t tstate)
{
    if (!Decoder() || _rasterState.tstatesPerLine == 0)
        return;
    // the passes whose fill has ended by the write saw the old state, so did the sprites the engine has reached in the one it is in
    const int64_t now = tstate >= 0 ? tstate * 8 : NowClock();
    RunSpritePassesEndedBy(now);
    if (_passNext < PassLimit() && PassStart(_passNext) <= now)
        AdvanceSpritePass(_passNext, now);
}

void ScreenNext::UpdateCopperGeometry()
{
    if (PortDecoder_Next* decoder = Decoder())
        decoder->Board().Copper().SetGeometry(_rasterState.tstatesPerLine * 2, _rasterState.maxFrameTiming / _rasterState.tstatesPerLine);
}

void ScreenNext::BindCopper(NextCopper& copper)
{
    copper.SetNow([this]() { return CopperClockNow(); });
    copper.SetBeforeWrite([this](uint64_t clock, uint8_t reg, uint8_t value) { RenderBeforeCopperClock(clock, reg, value); });
    copper.SetGeometry(_rasterState.tstatesPerLine * 2, _rasterState.maxFrameTiming / _rasterState.tstatesPerLine);
    _copperBound = true;
}

ScreenNext::PictureLatency ScreenNext::PictureLatencyOf(uint8_t reg, uint8_t previous, uint8_t value,
                                                         uint8_t paletteWriteSelect)
{
    // Measured on the official RTL simulated whole (tools/poc/028-next-video-pipeline-alignment: GHDL, the zxnext core with the top level's
    // clocks and SRAM arbiter, one register changed at a known clock, the first changed output pixel found against the line before).
    // `clocks` is from the clock the copper sees the WAIT true (the register is written 4 clocks later) to the first changed pixel; a
    // register read where a pixel group is fetched takes effect from the next group boundary (`quantum` clocks, the lattice anchored at
    // the grid's x = 0).
    //  - 22 clocks (14 MHz registers, half pixels): NR #4A, #14, the layer enables (NR #68 bit 7, NR #15 bits 7 / 0, NR #6B bit 7), the
    //    layer order (NR #15 bits 4:2), the palette selects (NR #43 bits 3:1, NR #6B bit 4)
    //  - 22 clocks, 4-clock pixels: the ULA clip (NR #1A), the tilemap clip (NR #1B), Layer 2's palette offset (NR #70 bits 3:0)
    //  - 26 clocks, 4-clock pixels: Layer 2's enable (NR #69 bit 7), scroll (NR #16 / #17 / #71) and clip (NR #18): the SRAM's read adds a pixel
    //  - 56 clocks, 32-clock cells: the ULA's scroll (NR #26 / #27); 70 for the fine scroll (NR #68 bit 2)
    //  - 53 clocks, 32-clock cells: the tilemap's scroll (NR #2F / #30 / #31)
    //  - 20 clocks: ULANext (NR #43 bit 0, NR #42)
    //  - 22 clocks, 4-clock pixels: the LoRes scroll (NR #32 / #33), the sprite clip (NR #19: applied where the line buffer is read)
    //  - 53 clocks, 32-clock cells: the tilemap's control bits latched with the cell (NR #6B bits 0 / 1 / 3 / 5); 24 for the 80-column bit
    //    (read at the output stage, latched every 32 clocks); 20 for the transparent index (NR #4C)
    //  - the palette values (NR #41 / #44): the lookup stage of the palette the write select (NR #43 bits 6:4) names: ULA 18, Layer 2 18,
    //    tilemap 17, sprites 16, half pixels
    //  - 23 clocks, 4-clock pixels: the LoRes palette offset and the Radastan bits (NR #6A); 27 for the Layer 2 resolution (NR #70 bits 5:4)
    // Not here (a different mechanism, open): what the sprite fill consumes (pattern bytes, attributes, NR #4B): the engine fills the next
    // line's buffer from the end of the line before, so a write shows a line or two later, not at a pixel (see the POC 028 reports)
    constexpr PictureLatency kChain{22, 2}, kPixel22{22, 4}, kLayer2{26, 4}, kUlaScroll{56, 32}, kUlaFine{70, 32}, kTilemapScroll{53, 32},
        kUlaNext{20, 2}, kTilemapControl{53, 32}, kTilemap80{24, 32}, kTilemapTransparent{20, 2}, kLayer2Resolution{27, 4},
        kLoRes{23, 4}, kNone{0, 1};
    const uint8_t changed = static_cast<uint8_t>(previous ^ value);
    auto earliest = [](PictureLatency a, PictureLatency b) { return a.clocks == 0 ? b : (b.clocks == 0 ? a : (b.clocks < a.clocks ? b : a)); };
    switch (reg)
    {
        case 0x4A:
        case 0x14:
            return kChain;
        case 0x68:
        {
            PictureLatency l = kNone;
            if (changed & 0xE9)  // bit 7 the ULA enable (measured); bits 6:5 blend, bit 3 ULA+ (ulap_en_0), bit 0 stencil travel the same registers
                l = kChain;
            if (changed & 0x04)  // the fine scroll
                l = earliest(l, kUlaFine);
            return l;
        }
        case 0x6B:  // bit 7 tilemap enable, bit 4 palette select
        {
            PictureLatency l = (changed & 0x90) ? kChain : kNone;
            if (changed & 0x40)  // 80 columns
                l = earliest(l, kTilemap80);
            if (changed & 0x2B)  // on top of the ULA (0), 512 tiles (1), text mode (3), no attribute byte (5)
                l = earliest(l, kTilemapControl);
            return l;
        }
        case 0x4C:
            return kTilemapTransparent;
        case 0x19:
        case 0x32:
        case 0x33:
            return kPixel22;
        case 0x15:  // bit 0 sprite_en and bit 7 lores_en (the chain of ula_en), bits 4:2 the layer order (measured)
            return (changed & 0x9D) ? kChain : kNone;
        case 0x43:
        {
            PictureLatency l = kNone;
            if (changed & 0x0E)  // the active palettes (ULA bit 1, Layer 2 bit 2, sprites bit 3)
                l = kChain;
            if (changed & 0x01)  // ULANext
                l = earliest(l, kUlaNext);
            return l;
        }
        case 0x42:
            return kUlaNext;
        case 0x1A:
        case 0x1B:
            return kPixel22;
        case 0x70:
        {
            PictureLatency l = (changed & 0x0F) ? kPixel22 : kNone;
            if (changed & 0x30)  // the Layer 2 resolution: one stage after the other Layer 2 inputs
                l = earliest(l, kLayer2Resolution);
            return l;
        }
        case 0x6A:  // the LoRes palette offset (bits 3:0) and the Radastan mode / XOR (bits 5:4)
            return (changed & 0x3F) ? kLoRes : kNone;
        case 0x69:
            return (changed & 0x80) ? kLayer2 : kNone;
        case 0x12:
        case 0x13:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x71:
            return kLayer2;
        case 0x26:
        case 0x27:
            return kUlaScroll;
        case 0x2F:
        case 0x30:
        case 0x31:
            return kTilemapScroll;
        case 0x41:
        case 0x44:
        {
            // the write select: 0 / 4 the ULA's palettes, 1 / 5 Layer 2's, 2 / 6 the sprites', 3 / 7 the tilemap's
            switch (paletteWriteSelect & 3)
            {
                case 0:
                    return {18, 2};
                case 1:
                    return {18, 2};
                case 2:
                    return {16, 2};
                default:
                    return {17, 2};
            }
        }
        default:
            return kNone;
    }
}

void ScreenNext::RenderBeforeCopperClock(uint64_t clock, uint8_t reg, uint8_t value)
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
    SpriteStateWillChange(at);  // a MOVE of a sprite register: the rows filled before it keep the old state
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint32_t first = GridFirstT();
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
    // the pixels before the write's effect reaches the output keep the old state: the effect is PictureLatencyOf after the write, and the
    // image's pixels are 14 MHz (two 28 MHz clocks each)
    constexpr unsigned kClocksPerImagePixel = 2, kImagePixels = 640;
    const uint8_t previous = Decoder() ? Decoder()->Board().Stored(reg) : value;
    const PictureLatency latency = PictureLatencyOf(reg, previous, value, Decoder() ? (Decoder()->Board().Stored(0x43) >> 4) & 7 : 0);
    // the MOVE's clock is one after the WAIT's (the copper decodes it on the same edge's next clock): the measured count from the WAIT is
    // one less from here. A register read where a pixel group is fetched takes effect at the next group's boundary
    uint64_t effect = delta + latency.clocks - (latency.clocks ? 1 : 0);
    if (latency.quantum > 1)
        effect = (effect + latency.quantum - 1) / latency.quantum * latency.quantum;
    const unsigned half = static_cast<unsigned>(std::min<uint64_t>(effect / kClocksPerImagePixel, kImagePixels));
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
    if (_rasterState.tstatesPerLine >= 100)  // 7 MHz pixels per line = 2 per T-state, 4 clocks of 28 MHz each
        in.lineClocks = _rasterState.tstatesPerLine * 8;
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
            // the engine's fill of this row: the passes whose fill has ended ran with the state they saw, the row's own runs if it has
            // started; a row ahead of the beam (a picture drawn early) is filled from the state as it is, its flags not posted
            if (_rowSeq[_nextLine] != _frameSeq)
            {
                const int64_t now = NowClock();
                RunSpritePassesEndedBy(now);
                while (_rowSeq[_nextLine] != _frameSeq && _passNext < PassLimit() && PassStart(_passNext) <= now)
                    RunSpritePass(_passNext++, now);
                if (_rowSeq[_nextLine] != _frameSeq)
                    board.Sprites().Fill(_nextLine, board.Stored(0x15), board.Stored(0x4B), static_cast<unsigned>(in.lineClocks), _rowLine[_nextLine],
                                         _passTicks);
            }
            in.spriteLine = &_rowLine[_nextLine];
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
    const uint32_t first = GridFirstT();
    if (now > first)
        done = (now - first) / perLine;
    RenderLinesUpTo(done);
}

void ScreenNext::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    (void)fromTstate;
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint32_t first = GridFirstT();
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
