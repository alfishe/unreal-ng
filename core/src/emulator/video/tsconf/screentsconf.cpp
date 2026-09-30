#include "screentsconf.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
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
        {
            _ts = &decoder->GetState();
            _engine = &decoder->GetEngine();
        }
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

ScreenState ScreenTSConf::DescribeScreenState() const
{
    ScreenState s = Screen::DescribeScreenState();
    auto* decoder = _context ? dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder) : nullptr;
    if (!decoder)
        return s;

    const TsConfState& ts = decoder->GetState();
    const uint8_t vConfig = ts.regs[TsConfReg::VConfig];
    const uint8_t vPage = ts.regs[TsConfReg::VPage];
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    const uint8_t mode = vConfig & 0x03;

    s.width = win.w;
    s.height = win.h;
    s.videoMode = GetVideoModeName(ModeOf(vConfig)) + " " + std::to_string(win.w) + "x" + std::to_string(win.h);
    s.activeRamPage = vPage;
    s.activeScreen = vPage == 7 ? 1 : 0;
    s.activeRamPages.clear();

    VideoModeInfo f;
    switch (mode)
    {
        case 0:  // ZX layout at V_PAGE
            f = GetVideoModeInfo(M_ZX48);
            s.activeRamPages = {vPage};
            break;
        case 1:  // 16C: 512 x 512 at 4 bpp = 128 KB from V_PAGE & #F8
            f.colorDepth = "4 bpp (16 colors per pixel, PAL_SEL bank)";
            f.colors = 16;
            f.bpp = 4;
            f.attributeSize = "per pixel";
            f.pixelDataBytes = 512u * 512u / 2u;
            f.totalBytes = f.pixelDataBytes;
            for (uint16_t p = 0; p < 8; p++)
                s.activeRamPages.push_back(static_cast<uint16_t>((vPage & 0xF8) + p));
            break;
        case 2:  // 256C: 512 x 512 at 8 bpp = 256 KB from V_PAGE & #F0
            f.colorDepth = "8 bpp (256 colors per pixel)";
            f.colors = 256;
            f.bpp = 8;
            f.attributeSize = "per pixel";
            f.pixelDataBytes = 512u * 512u;
            f.totalBytes = f.pixelDataBytes;
            for (uint16_t p = 0; p < 16; p++)
                s.activeRamPages.push_back(static_cast<uint16_t>((vPage & 0xF0) + p));
            break;
        default:  // TXT: character / attribute rows at V_PAGE, font at V_PAGE ^ 1, 14 MHz pixels
            f.colorDepth = "text, 16-color ink/paper per character (PAL_SEL bank)";
            f.colors = 16;
            f.attributeSize = "8x8 pixels (1 character cell)";
            f.textColumns = static_cast<uint8_t>(win.w * 2 / 8);
            f.textRows = static_cast<uint8_t>(win.h / 8);
            s.width = static_cast<uint16_t>(win.w * 2);
            s.activeRamPages = {vPage, static_cast<uint16_t>(vPage ^ 1)};
            break;
    }
    s.format = f;
    return s;
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

uint8_t ScreenTSConf::DotIndex(const TsConfState& ts, const TsConfLine& set, uint32_t dot, uint32_t line, uint32_t sub) const
{
    const uint8_t vConfig = set.vConfig;
    const bool text = (vConfig & 0x03) == 3;
    const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    const uint8_t border = ts.regs[TsConfReg::Border];
    const uint8_t tsu = _engine->TsuPixel(line, dot);
    const bool tsuVisible = (tsu & 0x0F) && !(vConfig & 0x10);  // NOTSU

    // Video plex ([V] video_render.v:75-82): inside the graphics window the TSU
    // wins over graphics unless GFXOVR and the graphics dot is "visible";
    // outside it the TSU shows inside the TS window, else the border
    uint8_t index;
    const bool inWindow = dot >= win.x0 && dot < static_cast<uint32_t>(win.x0 + win.w) && line >= win.y0 &&
                          line < static_cast<uint32_t>(win.y0 + win.h);
    if (!inWindow)
    {
        index = tsuVisible ? tsu : border;
    }
    else
    {
        const bool noGfx = vConfig & 0x20;
        bool gfxVisible = false;
        const uint8_t gfx = noGfx ? border : GraphicsIndex(set, dot - win.x0, sub, gfxVisible);
        gfxVisible = gfxVisible && !noGfx;
        if (vConfig & 0x08)  // GFXOVR
            index = gfxVisible ? gfx : (tsuVisible ? tsu : border);
        else
            index = tsuVisible ? tsu : (noGfx ? border : gfx);
    }

    // TXT is hires: every source flattens to 4 bits in the PAL_SEL bank (hs §4.2)
    return text ? static_cast<uint8_t>(palBank | (index & 0x0F)) : index;
}

uint8_t ScreenTSConf::GraphicsIndex(const TsConfLine& set, uint32_t wx, uint32_t sub, bool& visible) const
{
    const uint8_t mode = set.vConfig & 0x03;
    const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
    const uint32_t gx = (wx + set.gxOffs) & 0x1FF;
    const uint32_t gy = set.cntRow & 0x1FF;
    const uint8_t* ram = _context->pMemory->RAMBase();
    const uint32_t vPage = set.vPage;

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
            visible = ink;
            const uint8_t bright = (attr & 0x40) ? 0x08 : 0x00;
            return static_cast<uint8_t>(palBank | bright | (ink ? (attr & 0x07) : ((attr >> 3) & 0x07)));
        }
        case 1:  // 16C: 4 bpp, high nibble = left pixel
        {
            const uint8_t byte = ram[((vPage & 0xF8) << 14) | (gy << 8) | (gx >> 1)];
            const uint8_t nibble = (gx & 1) ? (byte & 0x0F) : (byte >> 4);
            visible = nibble != 0;
            return static_cast<uint8_t>(palBank | nibble);
        }
        case 2:  // 256C: 8 bpp
        {
            const uint8_t pixel = ram[((vPage & 0xF0) << 14) | (gy << 9) | gx];
            visible = pixel != 0;
            return pixel;
        }
        default:  // TXT: 256-byte rows (128 characters, 128 attributes) at V_PAGE, font at V_PAGE ^ 1
        {
            const uint32_t px = (gx * 2 + sub) & 0x3FF;
            const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
            const uint8_t code = row[(px >> 3) & 0x7F];
            const uint8_t attr = row[128 + ((px >> 3) & 0x7F)];
            const uint8_t font = ram[((vPage ^ 1) << 14) + code * 8 + (gy & 7)];
            const bool on = (font >> (7 - (px & 7))) & 1;
            visible = on;
            return static_cast<uint8_t>(palBank | (on ? (attr & 0x0F) : (attr >> 4)));
        }
    }
}

void ScreenTSConf::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    const TsConfState* ts = State();
    if (!ts || !_engine || !_framebuffer.memoryBuffer || _framebuffer.width != kVisibleDots * 2 ||
        _framebuffer.height != kVisibleLines)
        return;

    const uint32_t frameEnd = kLineTacts * kLines;
    if (fromTstate >= frameEnd)
        return;
    if (toTstate >= frameEnd)
        toTstate = frameEnd - 1;

    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);

    // SIMD-CANDIDATE(TS-O2): per-dot calls; a per-line span renderer (border /
    // window spans, one loop per mode, 8-pixel character spans in TXT) with the
    // CRAM colors from a LUT (TS-O1) is the planned speed-up (BENCH-1)
    for (uint32_t t = fromTstate; t <= toTstate; t++)
    {
        const uint32_t line = t / kLineTacts;
        const uint32_t tact = t % kLineTacts;
        if (line < kFirstVisibleLine || tact < kFirstVisibleTact)
            continue;

        const TsConfLine& set = _engine->Line(line);
        const bool text = (set.vConfig & 0x03) == 3;
        uint32_t* out = fb + (line - kFirstVisibleLine) * (kVisibleDots * 2) + (tact - kFirstVisibleTact) * 4;
        for (uint32_t half = 0; half < 2; half++)
        {
            const uint32_t dot = tact * 2 + half;
            if (text)
            {
                out[0] = CramToRgba(ts->cram[DotIndex(*ts, set, dot, line, 0)]);
                out[1] = CramToRgba(ts->cram[DotIndex(*ts, set, dot, line, 1)]);
            }
            else
            {
                out[0] = out[1] = CramToRgba(ts->cram[DotIndex(*ts, set, dot, line, 0)]);
            }
            out += 2;
        }
    }
}
