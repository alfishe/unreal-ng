#include "stdafx.h"

#include "screensprinter.h"

#include <algorithm>
#include <cstring>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/audio.h"
#include "emulator/video/screendigest.h"
#include "emulator/video/sprinter/sprintervideoram.h"

ScreenSprinter::ScreenSprinter(EmulatorContext* context) : Screen(context)
{
    _vid.mode = M_SPRINTER;
    _vid.raster = raster[R_736_288];
    SetVideoMode(M_SPRINTER);
}

/// region <Machine state>

PortDecoder_Sprinter* ScreenSprinter::Decoder() const
{
    // The decoder is created after the screen (Core::Init) and replaced on a model switch
    if (_context && (!_decoder || static_cast<PortDecoder*>(_decoder) != _context->pPortDecoder))
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
    return _decoder;
}

SprinterVideoInputs ScreenSprinter::CurrentInputs() const
{
    SprinterVideoInputs in;
    in.lines = _frameLines;
    in.border = static_cast<uint8_t>(_borderColor & 7);
    in.flash = _state && ((_state->frame_counter >> 4) & 1);  // the PLD's frame counter bit 4 (MAME :409)
    in.SetHold(0x77);
    if (PortDecoder_Sprinter* decoder = Decoder())
    {
        const SprinterPldState& pld = decoder->GetPldState();
        const SprinterVideoRam& vram = decoder->GetVideoRam();
        in.vram = vram.Data();
        in.palette = vram.Palette();
        in.modePage = static_cast<uint8_t>(pld.rgMod & 1);
        in.textPage = static_cast<uint8_t>((pld.pn >> 3) & 1);
        in.SetHold(pld.hold);
    }
    if (_state && _fontLatchFrame == _state->frame_counter)
        in.fontLatch = _fontLatch;
    return in;
}

/// endregion </Machine state>

/// region <Raster>

void ScreenSprinter::InitRaster()
{
    _vid.mode = M_SPRINTER;
    _vid.raster = raster[R_736_288];
    if (_mode != M_SPRINTER)
        SetVideoMode(M_SPRINTER);
    ApplyFrameLines();
}

void ScreenSprinter::SetVideoMode([[maybe_unused]] VideoModeEnum mode)
{
    // One mode for every picture: the squares decide (tdd-video §1)
    Screen::SetVideoMode(M_SPRINTER);
    SetRasterZones();
}

void ScreenSprinter::ApplyFrameLines()
{
    const PortDecoder_Sprinter* decoder = Decoder();
    const uint16_t lines = (decoder && decoder->GetPldState().frameLines) ? 312 : 320;
    const uint32_t frame = static_cast<uint32_t>(lines) * kLineTStates;
    CONFIG& config = _context->config;
    if (lines == _frameLines && config.frame == frame)
        return;

    // A frame start: the CPU's frame (Z80::BeginFrame) and the pacing follow from here
    _frameLines = lines;
    config.frame = frame;
    config.frame_duration_us = CalculateFrameDurationUs(frame);
    _rasterState.configFrameDuration = frame;
    SetRasterZones();
}

void ScreenSprinter::RestoreFrameLines(uint16_t lines)
{
    lines = lines == 312 ? 312 : 320;
    const uint32_t frame = static_cast<uint32_t>(lines) * kLineTStates;
    CONFIG& config = _context->config;
    _frameLines = lines;
    if (config.frame != frame)
    {
        config.frame = frame;
        config.frame_duration_us = CalculateFrameDurationUs(frame);
    }
    _rasterState.configFrameDuration = frame;
    SetRasterZones();
}

void ScreenSprinter::SetRasterZones()
{
    // Visible-first raster (tdd-video §2): lines 0-15 top border, 16-271 the
    // picture, 272-287 bottom border, 288.. blanking; in a line T 0-11 left
    // border, 12-171 the picture (640 pixels at 4 per T), 172-183 right
    // border, 184-223 blanking. The squares cover the border too: these zones
    // only describe where the 640x256 picture of a plain screen is
    RasterState& rs = _rasterState;
    rs.pixelsPerLine = SprinterVideoRenderer::kLinePixels;
    rs.tstatesPerLine = kLineTStates;
    rs.maxFrameTiming = static_cast<uint32_t>(_frameLines) * kLineTStates;
    rs.configFrameDuration = _context ? _context->config.frame : rs.maxFrameTiming;

    rs.topBorderAreaStart = 0;
    rs.topBorderAreaEnd = SprinterVideoRenderer::kBorderTop * kLineTStates - 1;
    rs.screenAreaStart = rs.topBorderAreaEnd + 1;
    rs.screenAreaEnd = rs.screenAreaStart + 256 * kLineTStates - 1;
    rs.bottomBorderAreaStart = rs.screenAreaEnd + 1;
    rs.bottomBorderAreaEnd = kVisibleLines * kLineTStates - 1;
    rs.blankAreaStart = kVisibleLines * kLineTStates;
    rs.blankAreaEnd = rs.maxFrameTiming - 1;

    rs.leftBorderAreaStart = 0;
    rs.leftBorderAreaEnd = SprinterVideoRenderer::kBorderLeft / 4 - 1;
    rs.screenLineAreaStart = SprinterVideoRenderer::kBorderLeft / 4;
    rs.screenLineAreaEnd = static_cast<uint8_t>(rs.screenLineAreaStart + 640 / 4 - 1);
    rs.rightBorderAreaStart = static_cast<uint8_t>(rs.screenLineAreaEnd + 1);
    rs.rightBorderAreaEnd = static_cast<uint8_t>(kVisibleTStates - 1);
    rs.blankLineAreaStart = static_cast<uint8_t>(kVisibleTStates);
    rs.blankLineAreaEnd = static_cast<uint8_t>(kLineTStates - 1);
    rs.paperDotsPerT = 4;
}

BeamPosition ScreenSprinter::DescribeBeam(uint32_t tInFrame) const
{
    BeamPosition b;
    const RasterState& rs = _rasterState;
    b.valid = true;
    b.tInFrame = tInFrame;
    b.line = tInFrame / kLineTStates;
    b.tInLine = tInFrame % kLineTStates;
    b.beamX = b.tInLine * 4;  // 14 MHz pixels from the line origin

    if (tInFrame >= rs.maxFrameTiming)
        return b;  // beyond_raster
    if (tInFrame <= rs.topBorderAreaEnd)
        b.verticalZone = "top_border";
    else if (tInFrame <= rs.screenAreaEnd)
        b.verticalZone = "screen";
    else if (tInFrame <= rs.bottomBorderAreaEnd)
        b.verticalZone = "bottom_border";
    else
        b.verticalZone = "vblank";

    const bool visibleRow = b.line < kVisibleLines;
    const bool screenRows = std::strcmp(b.verticalZone, "screen") == 0;
    if (screenRows)
    {
        if (b.tInLine <= rs.leftBorderAreaEnd)
            b.horizontalZone = "left_border";
        else if (b.tInLine <= rs.screenLineAreaEnd)
            b.horizontalZone = "paper";
        else if (b.tInLine <= rs.rightBorderAreaEnd)
            b.horizontalZone = "right_border";
        else
            b.horizontalZone = "hblank";
    }
    b.inPaper = screenRows && std::strcmp(b.horizontalZone, "paper") == 0;
    if (!screenRows)
        b.zone = (visibleRow && b.tInLine >= kVisibleTStates) ? "hblank" : b.verticalZone;
    else
        b.zone = b.inPaper ? "paper" : (std::strcmp(b.horizontalZone, "hblank") == 0 ? "hblank" : "border");
    b.inVisibleArea = screenRows && std::strcmp(b.horizontalZone, "hblank") != 0;
    if (b.inPaper)
    {
        b.paperX = (b.tInLine - rs.screenLineAreaStart) * 4;
        b.paperXEnd = b.paperX + 3;
        b.paperY = b.line - SprinterVideoRenderer::kBorderTop;
    }
    return b;
}

/// endregion </Raster>

/// region <Drawing>

void ScreenSprinter::UpdateScreen()
{
    DrawTo(GetCurrentTstate());
}

void ScreenSprinter::CatchUpToWrite()
{
    // In base T-states: at a raised clock the 3 CPU T of the cycle are a fraction of one
    const Z80* cpu = _context->pCore->GetZ80();
    const uint32_t multiplier = std::max<uint32_t>(_context->emulatorState.current_z80_frequency_multiplier, 1u);
    const uint32_t landed = (cpu->t > kWriteLandsBeforeEndT ? cpu->t - kWriteLandsBeforeEndT : 0) / multiplier;
    DrawTo(landed);
    if (!_turboRenderSkip && _feature_screenhq_enabled)
        LatchFont(landed);
}

void ScreenSprinter::LatchFont(uint32_t t)
{
    const uint32_t line = t / kLineTStates;
    const uint32_t x = (t % kLineTStates) * 4;
    if (line >= kVisibleLines || x >= SprinterVideoRenderer::kVisibleWidth || !_state)
        return;
    const uint64_t frame = _state->frame_counter;
    if (_fontLatchFrame == frame && _fontLatch.line == line && x >= _fontLatch.x0 && x < _fontLatch.x1)
        return;  // latched by an earlier write in this square: the byte of the square's start
    if (_fontLatchCheckedT == t && _fontLatchCheckedFrame == frame)
        return;  // this moment was looked at (an accelerator burst writes many bytes at one T)
    _fontLatchCheckedT = t;
    _fontLatchCheckedFrame = frame;

    SprinterVideoInputs in = CurrentInputs();
    if (!in.vram)
        return;
    const uint32_t a16 = SprinterVideoRenderer::A16(in, x);
    const uint32_t b8 = SprinterVideoRenderer::B8(in, line);
    const uint8_t* line1 =
        in.vram + SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
    if (!SprinterSquare::IsSymbol(line1[0]) || SprinterSquare::IsBlank(line1[0]) || SprinterSquare::IsBorder(line1[0]))
        return;

    // The latch unit: the square (320: one font byte per 16 pixels) or its 8-pixel half (640)
    const uint32_t unit = (line1[0] & 0x20) ? 16u : 8u;
    const uint32_t sub = a16 & 15;
    const uint32_t intoUnit = sub & (unit - 1);
    if (intoUnit == 0)
        return;  // the unit starts with this write: it latches the new byte
    // A16 is x shifted by whole pixels: the unit started intoUnit pixels earlier (left of the window: from 0)
    const int32_t start = static_cast<int32_t>(x) - static_cast<int32_t>(intoUnit);
    _fontLatch.line = line;
    _fontLatch.x0 = static_cast<uint32_t>(std::max(start, 0));
    _fontLatch.x1 = static_cast<uint32_t>(start + static_cast<int32_t>(unit));
    _fontLatch.font = in.vram[SprinterVideoRenderer::FontAddress(in, SprinterVideoRenderer::SymbolMode(line1, sub), b8 & 7)];
    _fontLatchFrame = frame;
}

void ScreenSprinter::DrawTo(uint32_t now)
{
    // _prevTstate here is the next base T-state to draw: [_prevTstate, now)
    // were not drawn yet (frame start: 0, so the frame's first T is drawn too)
    const uint32_t frameEnd = _rasterState.maxFrameTiming;
    if (now > frameEnd)
        now = frameEnd;  // the frame's last instruction ends past the frame
    if (now <= _prevTstate)
    {
        // A frame wrap without ResetPrevTstate starts over; a T-state behind
        // (a clock ratio switch rounding) is not a wrap
        if (_prevTstate - now > frameEnd / 2)
            _prevTstate = 0;
        else
            return;
    }
    if (!_turboRenderSkip && _feature_screenhq_enabled)
        DrawRange(_prevTstate, now - 1);
    _prevTstate = now;
}

void ScreenSprinter::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    PortDecoder_Sprinter* decoder = Decoder();
    if (!decoder || !_framebuffer.memoryBuffer || _framebuffer.width != SprinterVideoRenderer::kVisibleWidth ||
        _framebuffer.height != kVisibleLines)
        return;

    const uint32_t visibleEnd = kVisibleLines * kLineTStates;
    if (fromTstate >= visibleEnd || fromTstate > toTstate)
        return;
    toTstate = std::min(toTstate, visibleEnd - 1);

    const SprinterVideoInputs in = CurrentInputs();
    const SprinterVideoRenderer& renderer = decoder->VideoRenderer();
    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);

    // One span per raster line of the range
    for (uint32_t t = fromTstate; t <= toTstate;)
    {
        const uint32_t line = t / kLineTStates;
        const uint32_t lineStart = line * kLineTStates;
        const uint32_t first = t - lineStart;
        const uint32_t last = std::min(toTstate - lineStart, kVisibleTStates - 1);
        t = lineStart + kLineTStates;
        if (first > last)
            continue;
        uint32_t* out = fb + line * SprinterVideoRenderer::kVisibleWidth + first * 4;
        renderer.DrawSpan(in, line, first * 4, (last + 1) * 4, out);
    }
}

void ScreenSprinter::RenderFrameBatch()
{
    // ScreenHQ off: the whole frame at its end, with the state of that moment
    DrawRange(0, _rasterState.maxFrameTiming - 1);
}

void ScreenSprinter::RenderOnlyMainScreen()
{
    DrawRange(0, _rasterState.maxFrameTiming - 1);
}

void ScreenSprinter::FillBorderWithColor([[maybe_unused]] uint8_t color)
{
    // The border is drawn by the border squares of every frame
}

/// endregion </Drawing>

/// region <Description>

ScreenState ScreenSprinter::DescribeScreenState() const
{
    ScreenState s = Screen::DescribeScreenState();
    const SprinterVideoInputs in = CurrentInputs();
    s.width = 640;
    s.height = 256;
    s.shadowScreenCapable = false;
    s.activeRamPages.clear();
    s.contention = false;

    // A summary of the squares on screen (tdd-video §7): the 40 x 32 picture squares, classified by
    // SprinterSquare through SprinterPicture (the summary DeviceState::Sprinter's video report uses too).
    // The Spectrum mode is no mode of its own: the ZX screen is the ZX-40 squares the launcher writes
    const SprinterPicture picture = SprinterPicture::Of(in.vram, in.modePage);
    using Kind = SprinterSquare::Kind;
    s.videoMode = StringHelper::Format("Sprinter %u lines, mode page %u: text 40 %d, text 80 %d, graphics 320 %d, "
                                       "graphics 640 %d, spectrum %d, border %d, blank %d squares",
                                       static_cast<unsigned>(_frameLines), static_cast<unsigned>(in.modePage),
                                       picture.Count(Kind::Text40), picture.Count(Kind::Text80),
                                       picture.Count(Kind::Graphics320), picture.Count(Kind::Graphics640),
                                       picture.Count(Kind::Spectrum), picture.Count(Kind::Border), picture.Count(Kind::Blank));
    // A few words for a status line ("Spectrum 256x192, screen 5", "text 80", "320x256 256c (mixed)")
    s.videoModeBrief = picture.Brief(in.textPage);
    return s;
}

bool ScreenSprinter::IndexedFrame(std::vector<uint16_t>& pens, uint16_t& width, uint16_t& height, std::string& encoding) const
{
    // The pen of every visible pixel by the renderer's rules (SprinterVideoRenderer::PenAt), the state now
    const SprinterVideoInputs in = CurrentInputs();
    if (!in.vram)
        return false;
    width = static_cast<uint16_t>(SprinterVideoRenderer::kVisibleWidth);
    height = static_cast<uint16_t>(SprinterVideoRenderer::kVisibleLines);
    pens.resize(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++)
            pens[static_cast<size_t>(y) * width + x] =
                static_cast<uint16_t>(SprinterVideoRenderer::PenAt(in, x, y) & (SprinterVideoRam::kPens - 1));
    encoding = "u16le pen per pixel: k x 256 + n (0-#3FF graphics palettes 0-3, #400-#7FF text paper / ink / flash); "
               "the colors: /state/sprinter/palette";
    return true;
}

bool ScreenSprinter::DigestSurface(ScreenDigestSurface& out) const
{
    // The whole 256 KB video RAM (pictures, fonts, the mode table, the palettes) and the latches that
    // place the picture: mode page, HOLD, border, frame height. Pages 5 / 7 say nothing about a native screen
    PortDecoder_Sprinter* decoder = Decoder();
    if (!decoder)
        return false;
    const SprinterPldState& pld = decoder->GetPldState();
    const SprinterVideoRam& vram = decoder->GetVideoRam();
    uint64_t digest = ScreenDigest::DigestBytes(vram.Data(), SprinterVideoRam::kSize);
    digest = ScreenDigest::MixValue(digest, static_cast<uint8_t>(pld.rgMod & 1));
    digest = ScreenDigest::MixValue(digest, pld.hold);
    digest = ScreenDigest::MixValue(digest, static_cast<uint8_t>(_frameLines == 312 ? 1 : 0));
    out.name = "vram";
    out.description = "Sprinter video RAM (256 KB: pictures, fonts, mode table, palettes) + RGMOD page, HOLD, frame height";
    out.digest = digest;
    out.bytes = SprinterVideoRam::kSize;
    return true;
}

void ScreenSprinter::CaptureFamilyLatches(videomap::VideoLatches& latches) const
{
    // The video change log's Sprinter latches (videowritelog.h): what the PLD holds now
    if (PortDecoder_Sprinter* decoder = Decoder())
    {
        const SprinterPldState& pld = decoder->GetPldState();
        latches.rgMod = pld.rgMod;
        latches.hold = pld.hold;
        latches.portY = pld.portY;
        latches.allMode = pld.allMode;
        latches.frameLines = pld.frameLines ? 312 : 320;
    }
}

const void* ScreenSprinter::VideoFamilyView() const
{
    PortDecoder_Sprinter* decoder = Decoder();
    if (!decoder)
        return nullptr;
    _view.vram = &decoder->GetVideoRam();
    _view.inputs = CurrentInputs();
    _view.hold = decoder->GetPldState().hold;
    return &_view;
}

/// endregion </Description>
