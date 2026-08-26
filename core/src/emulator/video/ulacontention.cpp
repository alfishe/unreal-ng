#include "ulacontention.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"  // for VideoModeEnum / M_ZX128
#include "emulator/emulatorcontext.h"

void UlaContention::SetDependencies(Z80* cpu, Memory* memory, EmulatorContext* context)
{
    _cpu = cpu;
    _memory = memory;
    _context = context;
}

void UlaContention::UpdateRaster(const ContentionRaster& raster)
{
    _raster = raster;
}

uint8_t UlaContention::GetContentionDelay() const
{
    if (!_contentionEnabled)
        return 0;
    return ComputeContentionDelay(_cpu->t);
}


uint8_t UlaContention::ComputeContentionDelay(uint32_t t) const
{
    // The ULA only contends memory during the visible "paper" rendering area.
    // Outside this vertical area (in top/bottom borders or vertical retrace), there is no contention.
    if (t < _raster.screenAreaStart || t > _raster.screenAreaEnd)
        return 0;

    // Calculate T-state position within the current scanline (0 to tstatesPerLine - 1).
    // The Z80 executes instructions varying in length, so 't' can increment by arbitrary amounts.
    // tInLine normalizes the absolute CPU 't' time to the current line's horizontal position.
    uint32_t tInLine = (t - _raster.screenAreaStart) % _raster.tstatesPerLine;

    // Within the scanline, contention only occurs when the ULA is actively fetching video data.
    // If the beam is in the horizontal blanking or left/right border, there is no contention.
    if (tInLine < _raster.screenLineAreaStart || tInLine > _raster.screenLineAreaEnd)
        return 0;

    // The ULA fetches memory in 8-pixel character blocks, taking 4 T-states per fetch.
    // Because the ULA has priority over the shared memory bus, the CPU is halted (contended)
    // if it tries to access contended memory during these fetches.
    // The delay depends precisely on which T-state within the 8-T-state cell the CPU access falls into.
    // offsetInCell (0..7) maps directly to the contentionPattern array (e.g., 6, 5, 4, 3, 2, 1, 0, 0).
    uint32_t offsetInCell = (tInLine - _raster.screenLineAreaStart) % 8;
    return contentionPattern[offsetInCell];
}

uint8_t UlaContention::GetIOContentionDelay(uint16_t port) const
{
    if (!_contentionEnabled)
        return 0;

    uint32_t t = _cpu->t % _raster.configFrameDuration;

    uint8_t delay = ComputeContentionDelay(t);

    // IO contention rules (https://faqwiki.zxnet.co.uk/wiki/Contended_I/O):
    //
    // ZX-48K:
    //   A0=0 (even port): contention pattern delay
    //   A0=1 (odd port): no delay
    //
    // ZX-128K/+2/+3:
    //   A0=0 (even port): contention pattern delay + 1T extra
    //   A0=1, A1=0:       1T delay
    //   A0=1, A1=1:       no delay

    if ((port & 0x0001) == 0)
    {
        // Even port (A0=0) — contended on both 48K and 128K
        // 128K gets +1T extra for all even ports
        // (Screen pushes the model flag via UpdateRaster; we detect 128K
        //  by checking tstatesPerLine since 128K has 228 vs 48K's 224)
        if (_raster.tstatesPerLine >= 228)
            delay++;
    }
    else
    {
        // Odd port (A0=1)
        if (_raster.tstatesPerLine >= 228 && (port & 0x0002) == 0)
            delay = 1;  // 128K: 1T delay for A0=1, A1=0
        else
            delay = 0;
    }

    return delay;
}

uint8_t UlaContention::GetFloatingBus() const
{
    // ──────────────────────────────────────────────────────────────────────────
    // Floating Bus — what VRAM byte is on the shared data bus right now?
    // ──────────────────────────────────────────────────────────────────────────
    //
    // When the Z80 reads an unmapped port, no peripheral drives the data bus.
    // The CPU sees whatever byte the video controller just fetched from VRAM.
    //
    // CONTENTION vs FLOATING BUS are INDEPENDENT:
    //   Contention   = "the video controller halts the CPU" (clock stretching)
    //   Floating bus = "video data appears on the shared bus"
    // Pentagon has NO contention but DOES have a floating bus.
    // We intentionally do NOT check _contentionEnabled here.
    //
    // ──────────────────────────────────────────────────────────────────────────
    // TWO VIDEO CONTROLLER ARCHITECTURES
    // ──────────────────────────────────────────────────────────────────────────
    //
    // 1. ULA_FERRANTI (ZX-48K, ZX-128K, +2, +3)
    //    The Ferranti ULA chip has an internal 8-T-state state machine.
    //    Per 8T cycle: 4T fetch (2 pixel + 2 attribute bytes) + 4T shift
    //    (shift register outputs pixels, bus idle → 0xFF).
    //
    //    phase8 = tInPaper % 8
    //      0,1     → 0xFF (shift, no bus activity)
    //      2       → pixel byte
    //      3       → attribute byte
    //      4       → pixel byte
    //      5       → attribute byte
    //      6,7     → 0xFF (shift, no bus activity)
    //
    //    Reference: ZXMAK2 SpectrumRenderer.cs — CalcTableItem() lines 430-481,
    //    ReadFreeBus() lines 86-101. The per-T-state ULA action table maps:
    //      scrPix%8==0 → Shift1AndFetchB2 (pixel)
    //      scrPix%8==1 → Shift1AndFetchA2 (attribute)
    //      scrPix%8==2 → Shift1 (idle)
    //      scrPix%8==3 → Shift1Last (idle)
    //      scrPix%8==4,5 → Shift2 (idle)
    //      scrPix%8==6 → Shift2AndFetchB1 (pixel)
    //      scrPix%8==7 → Shift2AndFetchA1 (attribute)
    //
    //    Our tInPaper = scrPix + 4 (pipeline offset), so scrPix%8==0 → phase8==4.
    //
    // 2. ULA_DISCRETE_LOGIC (Pentagon, Scorpion, Profi, and other Soviet clones)
    //    Built from discrete TTL chips (counters + multiplexers).
    //    The video address counter runs CONTINUOUSLY — there are NO shift/dead
    //    cycles. Every T-state during paper has VRAM data on the bus.
    //
    //    phase4 = tInPaper % 4
    //      0,1 → pixel byte      (bitmap data from 0x4000-0x57FF)
    //      2,3 → attribute byte  (color data from 0x5800-0x5AFF)
    //
    //    This is why Pentagon floating bus sync works differently from ZX:
    //    the sync instruction IN A,(C) always sees VRAM data on the bus during
    //    paper — never 0xFF. The attribute change point is at phase4==2.
    //
    //    Reference: UnrealSpeccy io.cpp:953-958 uses a simplified model that
    //    returns attribute for ALL 4 phases per cell (no pixel/attr distinction).
    //    Our implementation is more accurate: pixel bytes during phases 0-1,
    //    attribute bytes during phases 2-3.
    //
    //    NOTE: ZXMAK2 uses the same 8T pipeline for Pentagon too (UlaPentagon
    //    does not override ReadFreeBus). This is a known ZXMAK2 simplification.
    //    Real Pentagon hardware uses discrete logic without shift gaps.
    //
    // ──────────────────────────────────────────────────────────────────────────
    // PIPELINE OFFSET
    // ──────────────────────────────────────────────────────────────────────────
    //
    // Both architectures fetch VRAM data ahead of the electron beam because the
    // shift register / attribute latch must be loaded before pixels are drawn.
    // We model this by shifting the effective paper area backward by 4 T-states:
    //   fetchAreaStart = screenLineAreaStart - 4
    //   fetchAreaEnd   = screenLineAreaEnd   - 4
    //
    // ──────────────────────────────────────────────────────────────────────────

    // User can explicitly disable floating bus (e.g. FloatBus=0 for some configs).
    if (_context && _context->config.floatbus == 0)
        return 0xFF;

    uint32_t t = _cpu->t % _raster.configFrameDuration;
    uint32_t tInLine = (t - _raster.screenAreaStart) % _raster.tstatesPerLine;

    // Apply 4T pipeline offset (video controller fetches ahead of beam)
    uint32_t fetchAreaStart = _raster.screenLineAreaStart - 4;
    uint32_t fetchAreaEnd = _raster.screenLineAreaEnd - 4;

    // Fast area checks: outside the overall screen or outside the fetch area
    if (t < _raster.screenAreaStart || t > _raster.screenAreaEnd)
        return 0xFF;
    if (tInLine < fetchAreaStart || tInLine >= fetchAreaEnd)
        return 0xFF;

    uint32_t tInPaper = tInLine - fetchAreaStart;
    uint32_t lineInScreen = (t - _raster.screenAreaStart) / _raster.tstatesPerLine;

    if (lineInScreen >= 192)
        return 0xFF;
    if (_memory == nullptr)
        return 0xFF;

    uint32_t cellIndex = tInPaper / 4;
    uint32_t y = lineInScreen;

    // ── Determine which byte is on the bus based on architecture ──
    bool isAttribute;

    if (_fetchType == ULA_FERRANTI)
    {
        // ── Ferranti ULA: 8T pipeline with shift gaps ──
        // Only phases 2-5 have VRAM data; phases 0-1 and 6-7 are 0xFF (shift).
        uint32_t phase8 = tInPaper % 8;
        if (phase8 < 2 || phase8 > 5)
            return 0xFF;
        // Even phases (2,4) → pixel byte; odd phases (3,5) → attribute byte
        isAttribute = (phase8 & 1) != 0;
    }
    else
    {
        // ── Discrete logic (Pentagon/Scorpion): 4T continuous fetch ──
        // ALL phases have VRAM data — no shift gaps.
        // Phases 0-1 → pixel byte; phases 2-3 → attribute byte.
        uint32_t phase4 = tInPaper % 4;
        isAttribute = (phase4 >= 2);
    }

    if (isAttribute)
    {
        // ── Attribute byte (0x5800–0x5AFF) ──
        //
        // ZX Spectrum VRAM attribute layout (interleaved):
        //   Address = 0x5800 | (block << 8) | (char_row << 5) | cellIndex
        //     block    = (y >> 6) & 3   — screen third (top/mid/bottom)
        //     char_row = (y >> 3) & 7   — character row within third
        //     cellIndex                  — character column 0-31
        //
        // Verified against ZXMAK2 CalcTableAddrAt() (SpectrumRenderer.cs:525-530).
        uint32_t block = (y >> 6) & 0x03;
        uint32_t char_row = (y >> 3) & 0x07;
        uint16_t attrAddr = 0x5800 | (block << 8) | (char_row << 5) | cellIndex;
        return _memory->DirectReadFromZ80Memory(attrAddr);
    }
    else
    {
        // ── Pixel / bitmap byte (0x4000–0x57FF) ──
        //
        // ZX Spectrum VRAM pixel layout (interleaved):
        //   Address = 0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8)
        //                      | ((y & 0x38) << 2) | cellIndex
        //     block    = (y >> 6) & 3   — screen third
        //     pixel_row = y & 7          — scan line within character cell
        //     char_row = (y >> 3) & 7   — character row within third
        //     cellIndex                  — character column 0-31
        //
        // Verified against ZXMAK2 CalcTableAddrBw() (SpectrumRenderer.cs:513-518).
        uint16_t pixelAddr = 0x4000
            | ((y & 0xC0) << 5)
            | ((y & 0x07) << 8)
            | ((y & 0x38) << 2)
            | cellIndex;
        return _memory->DirectReadFromZ80Memory(pixelAddr);
    }
}
