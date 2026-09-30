#include "ulacontention.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"  // for VideoModeEnum / M_ZX128
#include "emulator/emulatorcontext.h"

const char* ContentionRuleName(ContentionRule rule)
{
    switch (rule)
    {
        case ContentionRule::Ula48:
            return "ula48";
        case ContentionRule::Ula128:
            return "ula128";
        case ContentionRule::GateArray:
            return "gatearray";
        case ContentionRule::None:
        default:
            return "none";
    }
}

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

    // Contention runs for 128 T, starting kContentionLeadT before the first
    // displayed pixel: 48K first contended T = INT + 14335, pixel at INT + 14340.
    // The +2A/+3 gate array holds the CPU one T longer: its pattern opens with a 1 T hold before the first
    // fetch cell and closes with the same 1 T hold after the last one, so offset 128 still waits 1 (Rak's
    // Timing Test v0.3 "contended NOP" on a real +3 and a real +2A; every emulator surveyed - Fuse, MAME,
    // BizHawk, ZXMAK2, ZEsarUX, Xpeccy - reuses the ULA's 128 T without +3-specific evidence)
    const uint32_t contentionStart = _raster.screenLineAreaStart - kContentionLeadT;
    const uint32_t contentionLast = _raster.screenLineAreaEnd - kContentionLeadT + (_gateArray ? 1 : 0);
    if (tInLine < contentionStart || tInLine > contentionLast)
        return 0;

    // The ULA fetches memory in 8-pixel character blocks, taking 4 T-states per fetch.
    // Because the ULA has priority over the shared memory bus, the CPU is halted (contended)
    // if it tries to access contended memory during these fetches.
    // The delay depends precisely on which T-state within the 8-T-state cell the CPU access falls into.
    // offsetInCell (0..7) maps directly to the contentionPattern array (e.g., 6, 5, 4, 3, 2, 1, 0, 0).
    uint32_t offsetInCell = (tInLine - contentionStart) % 8;
    return _gateArray ? gateArrayContentionPattern[offsetInCell] : contentionPattern[offsetInCell];
}

uint8_t UlaContention::IoWaitBeforeIorq(uint16_t port, uint32_t cycleStartT) const
{
    // The +2A/+3 gate array contends memory cycles only, never I/O
    if (!_contentionEnabled || _gateArray)
        return 0;
    // C:1 when the high byte addresses contended memory, N:1 otherwise
    return _slotContended[port >> 14] ? ComputeContentionDelay(cycleStartT) : 0;
}

uint8_t UlaContention::IoWaitAfterIorq(uint16_t port, uint32_t iorqT) const
{
    if (!_contentionEnabled || _gateArray)
        return 0;

    // ULA port (A0 = 0): C:3 - one wait at the IORQ T, then the cycle's 3 T
    if ((port & 0x0001) == 0)
        return ComputeContentionDelay(iorqT);

    // Only the high byte contended: C:1, C:1, C:1 - a wait before each of the remaining T-states
    if (_slotContended[port >> 14])
    {
        uint32_t t = iorqT;
        uint32_t waits = 0;
        for (int i = 0; i < 3; i++)
        {
            const uint8_t w = ComputeContentionDelay(t);
            waits += w;
            t += w + 1;
        }
        return static_cast<uint8_t>(waits);
    }

    return 0;  // N:4 (or N:1 before IORQ, then N:3)
}

uint8_t UlaContention::GetFloatingBus() const
{
    // Floating bus: the byte the video logic fetches right now, #FF between fetches and in the border
    // (fetch timing per architecture in FetchedByte)
    if (_context && _context->config.floatbus == 0)
        return 0xFF;

    uint8_t value = 0xFF;
    return FetchedByte(value) ? value : 0xFF;
}

uint8_t UlaContention::GetGateArrayFloatingBus(uint16_t port) const
{
    if (_context && _context->config.floatbus == 0)
        return 0xFF;
    if ((port & 0xF003) != 0x0001)
        return 0xFF;
    if (_context && (_context->emulatorState.p7FFD & 0x20))
        return 0xFF;  // paging locked: the gate array stops driving the bus

    uint8_t value = 0xFF;
    if (!FetchedByte(value))
        value = _lastContendedByte;
    return static_cast<uint8_t>(value | 0x01);
}

bool UlaContention::FetchedByte(uint8_t& value) const
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
    // We model this by shifting the effective paper area backward (FetchLead):
    //   fetchAreaStart = screenLineAreaStart - lead
    //   fetchAreaEnd   = screenLineAreaEnd   - lead
    // The lookup runs at IORQ, one T into the I/O cycle. On the Ferranti ULA
    // (lead 6) an I/O cycle that starts on the contention onset (the T with
    // delay 6, 5 T before the first pixel) reads the bitmap byte: FUSE, Zero,
    // ZXMAK2, MAME and pico-spec agree (the "14338" of the floating-bus
    // articles is FUSE's end-of-cycle count of the same T). The discrete-logic
    // clones keep lead 4.
    //
    // ──────────────────────────────────────────────────────────────────────────

    // User can explicitly disable floating bus (e.g. FloatBus=0 for some configs).

    uint32_t y, cellIndex;
    if (!LocateFloatingBusCell(y, cellIndex))
        return false;

    // T-states into the paper area (both architectures derive their fetch
    // phase from it; recomputed here to keep LocateFloatingBusCell minimal)
    uint32_t t = _cpu->t % _raster.configFrameDuration;
    uint32_t tInLine = (t - _raster.screenAreaStart) % _raster.tstatesPerLine;
    uint32_t tInPaper = tInLine - (_raster.screenLineAreaStart - FetchLead());

    // ── Determine which byte is on the bus based on architecture ──
    bool isAttribute;

    if (_fetchType == ULA_FERRANTI)
    {
        // ── Ferranti ULA: 8T pipeline with shift gaps ──
        // Only phases 2-5 have VRAM data; phases 0-1 and 6-7 are 0xFF (shift).
        uint32_t phase8 = tInPaper % 8;
        if (phase8 < 2 || phase8 > 5)
            return false;
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

    // ULA snow: the bytes the ULA really fetched for this cell (SnowOffsets)
    uint16_t pixelOffset = static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | cellIndex);
    uint16_t attrOffset = static_cast<uint16_t>(AttributeCellAddress(y, cellIndex) - 0x4000);
    if (_fetchType == ULA_FERRANTI && HasSnow() && SnowOffsets(y, cellIndex, pixelOffset, attrOffset)) [[unlikely]]
    {
        value = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x4000 + (isAttribute ? attrOffset : pixelOffset)));
        return true;
    }

    if (isAttribute)
    {
        value = _memory->DirectReadFromZ80Memory(AttributeCellAddress(y, cellIndex));
        return true;
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
        value = _memory->DirectReadFromZ80Memory(pixelAddr);
        return true;
    }
}

bool UlaContention::LocatePaperFetch(uint32_t t, uint32_t& y, uint32_t& tInPaper) const
{
    if (t < _raster.screenAreaStart || t > _raster.screenAreaEnd)
        return false;
    const uint32_t tInLine = (t - _raster.screenAreaStart) % _raster.tstatesPerLine;
    const uint32_t fetchAreaStart = _raster.screenLineAreaStart - FetchLead();
    const uint32_t fetchAreaEnd = _raster.screenLineAreaEnd - FetchLead();
    if (tInLine < fetchAreaStart || tInLine >= fetchAreaEnd)
        return false;
    y = (t - _raster.screenAreaStart) / _raster.tstatesPerLine;
    if (y >= 192)
        return false;
    tInPaper = tInLine - fetchAreaStart;
    return true;
}

uint32_t UlaContention::SnowFrameStamp() const
{
    return _context ? static_cast<uint32_t>(_context->emulatorState.frame_counter) : 0u;
}

void UlaContention::NoteRefresh(uint32_t t, uint8_t r)
{
    if (_raster.configFrameDuration == 0)
        return;
    uint32_t y, tInPaper;
    if (!LocatePaperFetch(t % _raster.configFrameDuration, y, tInPaper))
        return;

    const uint32_t phase = tInPaper % 8;
    uint32_t cell = (tInPaper / 8) * 2;
    uint8_t kind;
    if (phase == kSnowPhase)
        kind = SnowMarkSnow;
    else if (phase == kDoublePhase)
    {
        kind = SnowMarkDouble;
        cell += 1;
    }
    else
        return;
    if (cell >= 32)
        return;

    const uint32_t index = y * 32 + cell;
    const uint32_t frame = SnowFrameStamp();
    _snowKind[index] = kind;
    _snowR[index] = static_cast<uint8_t>(r & 0x7F);
    _snowStamp[index] = frame;
    _snowFrame = frame;
}

bool UlaContention::SnowOffsets(uint32_t y, uint32_t cell, uint16_t& pixelOffset, uint16_t& attrOffset) const
{
    if (y >= 192 || cell >= 32)
        return false;
    const uint32_t index = y * 32 + cell;
    const uint32_t frame = SnowFrameStamp();
    if (_snowStamp[index] != frame || _snowKind[index] == SnowMarkNone)
        return false;

    if (_snowKind[index] == SnowMarkDouble)
    {
        // The second cell shows the first cell's bytes, as the first cell was fetched (snowed or not)
        pixelOffset = static_cast<uint16_t>(pixelOffset - 1);
        attrOffset = static_cast<uint16_t>(attrOffset - 1);
        SnowOffsets(y, cell - 1, pixelOffset, attrOffset);
        return true;
    }

    // Snow: bits 6..0 of both addresses from R (bit 7 of R does not take part)
    const uint16_t r = _snowR[index];
    pixelOffset = static_cast<uint16_t>((pixelOffset & ~0x7F) | r);
    attrOffset = static_cast<uint16_t>((attrOffset & ~0x7F) | r);
    return true;
}

bool UlaContention::LocateFloatingBusCell(uint32_t& y, uint32_t& cellIndex) const
{
    uint32_t t = _cpu->t % _raster.configFrameDuration;
    uint32_t tInLine = (t - _raster.screenAreaStart) % _raster.tstatesPerLine;

    // Pipeline offset: the video controller fetches ahead of the beam (FetchedByte)
    uint32_t fetchAreaStart = _raster.screenLineAreaStart - FetchLead();
    uint32_t fetchAreaEnd = _raster.screenLineAreaEnd - FetchLead();

    // Fast area checks: outside the overall screen or outside the fetch area
    if (t < _raster.screenAreaStart || t > _raster.screenAreaEnd)
        return false;
    if (tInLine < fetchAreaStart || tInLine >= fetchAreaEnd)
        return false;

    uint32_t tInPaper = tInLine - fetchAreaStart;
    uint32_t lineInScreen = (t - _raster.screenAreaStart) / _raster.tstatesPerLine;

    if (lineInScreen >= 192)
        return false;
    if (_memory == nullptr)
        return false;

    cellIndex = tInPaper / 4;
    y = lineInScreen;
    return true;
}

uint16_t UlaContention::AttributeCellAddress(uint32_t y, uint32_t cellIndex)
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
    return 0x5800 | (block << 8) | (char_row << 5) | cellIndex;
}

uint8_t UlaContention::GetFloatingBusAttribute() const
{
    uint32_t y, cellIndex;
    if (!LocateFloatingBusCell(y, cellIndex))
        return 0xFF;

    return _memory->DirectReadFromZ80Memory(AttributeCellAddress(y, cellIndex));
}
