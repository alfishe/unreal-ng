#include "screentsconf.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
    /// Graphics window per V_CONFIG[7:6] geometry, in raster dots / lines (hs §4.2)
    struct Window
    {
        uint16_t x0, y0, w, h;
    };
    constexpr Window kWindows[4] = {
        {140, 80, 256, 192},
        {108, 76, 320, 200},
        {108, 56, 320, 240},
        {88, 32, 360, 288},
    };

    /// No-VDAC build: the top 2 bits of a 5-bit channel drive a 2-bit DAC, the
    /// low 3 bits a PWM that adds one level on average (not above level 3),
    /// shown as the time average (hs §4.3)
    struct PwmLevels
    {
        uint8_t level[32];
        constexpr PwmLevels() : level{}
        {
            for (int v = 0; v < 32; v++)
            {
                const int dac = v >> 3;
                const int boost = dac < 3 ? (v & 7) : 0;  // eighths of a level
                level[v] = static_cast<uint8_t>((dac * 8 + boost) * 255 / 24);
            }
        }
    };
    constexpr PwmLevels kPwm;
}

ScreenTSConf::ScreenTSConf(EmulatorContext* context) : ScreenZX(context)
{
}

uint32_t ScreenTSConf::CramToRgba(uint16_t cram)
{
    const uint32_t r = kPwm.level[(cram >> 10) & 0x1F];
    const uint32_t g = kPwm.level[(cram >> 5) & 0x1F];
    const uint32_t b = kPwm.level[cram & 0x1F];
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

VideoModeEnum ScreenTSConf::ModeOf(uint8_t vConfig)
{
    static constexpr VideoModeEnum kModes[4] = {M_TSZX, M_TS16, M_TS256, M_TSTX};
    return kModes[vConfig & 0x03];
}

const TsConfState* ScreenTSConf::State()
{
    if (!_ts && _context)
    {
        if (auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder))
            _ts = &decoder->GetState();
    }
    return _ts;
}

void ScreenTSConf::InitRaster()
{
    const TsConfState* ts = State();
    const VideoModeEnum mode = ts ? ModeOf(ts->regs[TsConfReg::VConfig]) : M_TSZX;

    _vid.mode = mode;
    _vid.raster = raster[R_360_288];
    if (mode != _mode)
        SetVideoMode(mode);
}

void ScreenTSConf::SetVideoMode(VideoModeEnum mode)
{
    // The raster state and framebuffer come from the descriptor; the ZX
    // renderer's tables are not used by this screen
    Screen::SetVideoMode(mode);
}

void ScreenTSConf::SetBorderColor(uint8_t color)
{
    // The border is TS BORDER (a CRAM index) - drawn from the state; flush
    // the beam up to the write like every other screen
    Screen::SetBorderColor(color);
}

void ScreenTSConf::SetActiveScreen(SpectrumScreenEnum screen)
{
    UpdateScreen();
    Screen::SetActiveScreen(screen);
}

void ScreenTSConf::RenderFrameBatch()
{
    DrawRange(0, kLineTacts * kLines - 1);
}

void ScreenTSConf::RenderOnlyMainScreen()
{
    DrawRange(0, kLineTacts * kLines - 1);
}

void ScreenTSConf::FillBorderWithColor([[maybe_unused]] uint8_t color)
{
    // The border is part of every frame the range renderer draws
}

uint8_t ScreenTSConf::DotIndex(const TsConfState& ts, uint32_t dot, uint32_t line, uint32_t sub) const
{
    const uint8_t vConfig = ts.regs[TsConfReg::VConfig];
    const uint8_t mode = vConfig & 0x03;
    const uint8_t palBank = static_cast<uint8_t>((ts.regs[TsConfReg::PalSel] & 0x0F) << 4);
    const Window& win = kWindows[vConfig >> 6];

    const bool inWindow = dot >= win.x0 && dot < static_cast<uint32_t>(win.x0 + win.w) && line >= win.y0 &&
                          line < static_cast<uint32_t>(win.y0 + win.h) && !(vConfig & 0x20);  // NOGFX
    if (!inWindow)
    {
        const uint8_t border = ts.regs[TsConfReg::Border];
        // TXT flattens everything to 4 bits in the PAL_SEL bank (hs §4.2)
        return mode == 3 ? static_cast<uint8_t>(palBank | (border & 0x0F)) : border;
    }

    const uint32_t gx = (dot - win.x0 + (ts.regs[TsConfReg::GXOffsL] | ((ts.regs[TsConfReg::GXOffsH] & 1u) << 8))) & 0x1FF;
    const uint32_t gy = (line - win.y0 + (ts.regs[TsConfReg::GYOffsL] | ((ts.regs[TsConfReg::GYOffsH] & 1u) << 8))) & 0x1FF;
    const uint8_t* ram = _context->pMemory->RAMBase();
    const uint32_t vPage = ts.regs[TsConfReg::VPage];

    switch (mode)
    {
        case 0:  // ZX: Spectrum layout at V_PAGE, rows wrap at 256, columns at 32 bytes
        {
            const uint32_t y = gy & 0xFF;
            const uint32_t x = gx & 0xFF;
            const uint8_t* page = ram + (vPage << 14);
            const uint8_t pixels = page[((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3)];
            const uint8_t attr = page[0x1800 + (y >> 3) * 32 + (x >> 3)];
            bool ink = (pixels >> (7 - (x & 7))) & 1;
            if ((attr & 0x80) && ((_context->emulatorState.frame_counter >> 4) & 1))
                ink = !ink;  // flash: every 16 frames (hs §4.1)
            const uint8_t bright = (attr & 0x40) ? 0x08 : 0x00;
            return static_cast<uint8_t>(palBank | bright | (ink ? (attr & 0x07) : ((attr >> 3) & 0x07)));
        }
        case 1:  // 16C: 4 bpp, high nibble = left pixel
        {
            const uint8_t byte = ram[((vPage & 0xF8) << 14) | (gy << 8) | (gx >> 1)];
            return static_cast<uint8_t>(palBank | ((gx & 1) ? (byte & 0x0F) : (byte >> 4)));
        }
        case 2:  // 256C: 8 bpp
            return ram[((vPage & 0xF0) << 14) | (gy << 9) | gx];
        default:  // TXT: 256-byte rows (128 characters, 128 attributes) at V_PAGE, font at V_PAGE ^ 1
        {
            const uint32_t px = (gx * 2 + sub) & 0x3FF;
            const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
            const uint8_t code = row[(px >> 3) & 0x7F];
            const uint8_t attr = row[128 + ((px >> 3) & 0x7F)];
            const uint8_t font = ram[((vPage ^ 1) << 14) + code * 8 + (gy & 7)];
            const bool on = (font >> (7 - (px & 7))) & 1;
            return static_cast<uint8_t>(palBank | (on ? (attr & 0x0F) : (attr >> 4)));
        }
    }
}

void ScreenTSConf::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    const TsConfState* ts = State();
    if (!ts || !_framebuffer.memoryBuffer || _framebuffer.width != kVisibleDots * 2 ||
        _framebuffer.height != kVisibleLines)
        return;

    const uint32_t frameEnd = kLineTacts * kLines;
    if (fromTstate >= frameEnd)
        return;
    if (toTstate >= frameEnd)
        toTstate = frameEnd - 1;

    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);
    const bool text = (ts->regs[TsConfReg::VConfig] & 0x03) == 3;

    for (uint32_t t = fromTstate; t <= toTstate; t++)
    {
        const uint32_t line = t / kLineTacts;
        const uint32_t tact = t % kLineTacts;
        if (line < kFirstVisibleLine || tact < kFirstVisibleTact)
            continue;

        uint32_t* out = fb + (line - kFirstVisibleLine) * (kVisibleDots * 2) + (tact - kFirstVisibleTact) * 4;
        for (uint32_t half = 0; half < 2; half++)
        {
            const uint32_t dot = tact * 2 + half;
            if (text)
            {
                out[0] = CramToRgba(ts->cram[DotIndex(*ts, dot, line, 0)]);
                out[1] = CramToRgba(ts->cram[DotIndex(*ts, dot, line, 1)]);
            }
            else
            {
                out[0] = out[1] = CramToRgba(ts->cram[DotIndex(*ts, dot, line, 0)]);
            }
            out += 2;
        }
    }
}
