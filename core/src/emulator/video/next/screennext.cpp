#include "stdafx.h"

#include "screennext.h"

#include <algorithm>
#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"

ScreenNext::ScreenNext(EmulatorContext* context) : Screen(context)
{
    _vid.mode = M_NEXT;
    SetVideoMode(M_NEXT);
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
}

void ScreenNext::SetVideoMode([[maybe_unused]] VideoModeEnum mode)
{
    Screen::SetVideoMode(M_NEXT);
    _lastTimingClass = _state ? _state->ula_timing_class : 2;
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
    // The copper's lines count from the first paper line; its horizontal 0 is 11 pixels before the first paper pixel
    const uint32_t perLine = _rasterState.tstatesPerLine;
    const uint64_t frame7 = static_cast<uint64_t>(_rasterState.maxFrameTiming) * 2;
    const uint64_t origin7 = (static_cast<uint64_t>(_rasterState.screenAreaStart / perLine) * perLine + _rasterState.screenLineAreaStart) * 2;
    const uint64_t t7 = (static_cast<uint64_t>(tstate) * 2 + frame7 + 11 - origin7 % frame7) % frame7;
    return t7 * 4;
}

void ScreenNext::RenderLinesUpTo(unsigned lineExclusive)
{
    PortDecoder_Next* decoder = Decoder();
    if (!decoder || !FramebufferReady())
        return;
    lineExclusive = std::min<unsigned>(lineExclusive, NextVideoRenderer::kHeight);
    if (_nextLine >= lineExclusive)
        return;

    NextBoard& board = decoder->Board();
    if (!_copperBound)
    {
        board.Copper().SetNow([this]() { return CopperClock(GetCurrentTstate()); });
        _copperBound = true;
    }
    board.Copper().SetGeometry(_rasterState.tstatesPerLine * 2, _rasterState.maxFrameTiming / _rasterState.tstatesPerLine);
    NextVideoInputs in;
    in.ram = _context->pMemory->RAMPageAddress(0);
    in.ramPages = MAX_RAM_PAGES;
    in.regs = &board.Video();
    in.sprites = &board.Sprites();
    for (unsigned r = 0; r < 256; r++)
        in.nr[r] = board.Stored(static_cast<uint8_t>(r));
    in.border = _state ? static_cast<uint8_t>(_state->pFE & 7) : 0;
    in.portFf = board.Video().PortFf();
    in.shadowScreen = _state && (_state->p7FFD & 0x08);
    in.layer2Enable = board.Video().Layer2Enabled();
    in.flash = _state && (_state->frame_counter & 0x10);
    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);
    auto refresh = [&]() {
        for (unsigned r = 0; r < 256; r++)
            in.nr[r] = board.Stored(static_cast<uint8_t>(r));
        // what the copper (or a port write) may have changed since the line before
        in.portFf = board.Video().PortFf();
        in.shadowScreen = _state && (_state->p7FFD & 0x08);
        in.layer2Enable = board.Video().Layer2Enabled();
        in.border = _state ? static_cast<uint8_t>(_state->pFE & 7) : 0;
    };
    uint32_t segment[NextVideoRenderer::kWidth];
    for (; _nextLine < lineExclusive; _nextLine++)
    {
        uint32_t* row = fb + static_cast<size_t>(_nextLine) * 2 * NextVideoRenderer::kWidth;
        // x = 0 of the grid line, in copper time: the paper's left edge minus the 32 pixels of border
        const uint32_t lineStart = LineEndT(_nextLine) - _rasterState.tstatesPerLine;
        const uint64_t t0 = CopperClock(lineStart + _rasterState.screenLineAreaStart - 16);
        NextCopper& copper = board.Copper();
        copper.RunTo(t0);  // everything before the line
        // The copper writes inside the line change the picture from the pixel it reaches: the pixels before are drawn with
        // the state before the write
        unsigned fromX = 0;
        auto draw = [&](unsigned toX) {
            if (toX <= fromX)
                return;
            refresh();
            NextVideoRenderer::RenderLine(in, _nextLine, segment);
            std::memcpy(row + fromX * 2, segment + fromX * 2, (toX - fromX) * 2 * sizeof(uint32_t));
            fromX = toX;
        };
        if (copper.Mode() != 0)
        {
            copper.SetBeforeWrite([&](uint64_t t) { draw(static_cast<unsigned>(std::min<uint64_t>((t > t0 ? t - t0 : 0) / 4, 320))); });
            copper.RunTo(t0 + 320 * 4);
            copper.SetBeforeWrite(nullptr);
        }
        draw(320);
        std::memcpy(row + NextVideoRenderer::kWidth, row, NextVideoRenderer::kWidth * sizeof(uint32_t));  // every line twice
    }
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
