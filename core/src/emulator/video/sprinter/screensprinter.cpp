#include "stdafx.h"

#include "screensprinter.h"

#include <algorithm>
#include <cstring>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/audio.h"
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
    // _prevTstate here is the next base T-state to draw: [_prevTstate, now)
    // were not drawn yet (frame start: 0, so the frame's first T is drawn too)
    const uint32_t frameEnd = _rasterState.maxFrameTiming;
    uint32_t now = GetCurrentTstate();
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

    // A summary of the squares on screen (tdd-video §7): 40 x 32 picture squares
    unsigned text = 0, graphics320 = 0, graphics640 = 0, border = 0;
    if (in.vram)
    {
        for (uint8_t b = 0; b < 32; b++)
        {
            for (uint8_t a = 0; a < 40; a++)
            {
                const uint8_t m0 = in.vram[SprinterVideoRam::ModeAddress(a, b, in.modePage)];
                if ((m0 >> 4) == 0x0F)
                    border++;
                else if (m0 & 0x10)
                    text++;
                else if (m0 & 0x20)
                    graphics320++;
                else
                    graphics640++;
            }
        }
    }
    s.videoMode = StringHelper::Format("Sprinter %u lines, mode page %u: text %u, graphics 320 %u, graphics 640 %u, "
                                       "border %u squares",
                                       static_cast<unsigned>(_frameLines), static_cast<unsigned>(in.modePage), text,
                                       graphics320, graphics640, border);
    return s;
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
