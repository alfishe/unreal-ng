#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_profi.h"

#include "debugger/ttd/profi/ttdprofipaging.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"

namespace
{
    // ZX Profi's own Covox DAC lives on ports #5F (Left), #3F (Right) - distinct
    // physical ports from the Pentagon/Scorpion Soundrive set (#F1/#F3/#F9/#FB).
    // Writes are forwarded into the shared Covox device via its standard
    // Left/Right ports, so the device itself stays model-agnostic.
    constexpr uint8_t kProfiCovoxLeftPort  = 0x5F;
    constexpr uint8_t kProfiCovoxRightPort = 0x3F;
}

/// region <Constructors / Destructors>

PortDecoder_Profi::PortDecoder_Profi(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
    _cmos.SetCMOSType(Dallas);
}

PortDecoder_Profi::~PortDecoder_Profi()
{
    MLOGDEBUG("PortDecoder_Profi::~PortDecoder_Profi()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Profi::reset()
{
    EmulatorState& state = _context->emulatorState;

    state.p7FFD = 0x00;     // RAM 0, screen 5, ROM14 = 0, unlocked
    state.pDFFD = 0x00;     // no extended RAM, SCO/SCR/CPM/DS80 off, ROM at #0000
    state.pBFFD = 0x00;     // AY register select
    state.pFFFD = 0x00;     // AY data
    state.pFE = 0xFF;       // border white, no sound
    state.border_attr = 0x07;

    ResetPalette();

    _screen->SetBorderColor(COLOR_WHITE);
    _screen->SetActiveScreen(SCREEN_NORMAL);
    _7FFD_Locked = false;

    // Profi boots into the SYS (service / menu) ROM: DOS latch on, ROM14 = 0.
    // SetROMMode raises CF_TRDOS and rebuilds the banks through UpdateZ80Banks(),
    // which calls back UpdateModelMemoryBanks() for the RAM windows.
    Memory& memory = *_context->pMemory;
    memory.SetROMMode(RM_SYS);
}

uint8_t PortDecoder_Profi::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    const bool dosPorts = (_state->flags & CF_DOSPORTS) != 0;
    const uint16_t fdcPort = dosPorts ? DecodeFDCPort(port) : 0;

    // AY #FFFD: A15=1, A14=1, A1=0. The AY-3-8910 does not decode the other
    // address bits, so mirrored ports select it on IN too. Resolve mirrors to the
    // canonical port BEFORE the weak FE (A0-only) check.
    if ((port & 0xC002) == 0xC000)
    {
        result = PeripheralPortIn(0xFFFD);
        disp.decodedPort = 0xFFFD;
    }
    // AY #BFFD: A15=1, A14=0, A1=0
    else if ((port & 0xC002) == 0x8000)
    {
        result = PeripheralPortIn(0xBFFD);
        disp.decodedPort = 0xBFFD;
    }
    // RTC/CMOS: #9F/#BF/#DF/#FF, EXT mode only - takes priority over the FDC/system-port
    // decode below, since #BF/#FF alias to the Beta128 system port outside EXT mode.
    else if ((port & 0x9F) == 0x9F && IsExtMode())
    {
        // Only the data ports (#9F/#DF, bit 5 = 0) return real data; the address
        // strobe (#BF/#FF) is write-only and reads as floating bus.
        if ((port & 0x20) == 0)
            result = _cmos.ReadCMOS();
        _lastPortDecoded = true;
        disp.decodedPort = port & 0xFF;
    }
    // WD1793 (Beta128) registers and system port: only while the disk interface is
    // on the bus (DOS latch or CP/M mode, CF_DOSPORTS)
    else if (fdcPort != 0)
    {
        result = PeripheralPortIn(fdcPort);
        disp.decodedPort = fdcPort;
        _lastPortDecoded = true;
    }
    else if (IsFEPort(port))
    {
        result = Default_Port_FE_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    else if (uint8_t mouseReg = 0; !dosPorts && Default_IsPort_KempstonMouse(port, mouseReg))
    {
        // Kempston mouse and joystick exist only outside the CP/M / DOS port sets
        result = Default_Port_KempstonMouse_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = port;
        disp.wasHandledInline = true;
    }
    else
    {
        result = PeripheralPortIn(port);
        // Identity decode: mark decoded only when a device actually responded
        if (_lastPortDecoded)
            disp.decodedPort = port;
    }
    disp.wasDecoded = _lastPortDecoded;

    OnPortInComplete(port, result, pc, disp);

    return result;
}

void PortDecoder_Profi::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    const bool dosPorts = (_state->flags & CF_DOSPORTS) != 0;

    // ULA port and Profi palette. Deliberately NOT an else-chain with the paging ports:
    // UnrealSpeccy documents titles that OUT #FC to both #FE and #7FFD.
    if (IsFEPort(port))
    {
        // Palette write uses the PREVIOUS #FE value as index, so it must precede the latch update
        if ((port & 0x0080) == 0 && (_state->pDFFD & 0x80))
            Port_Palette_Out(port);

        Default_Port_FE_Out(port, value, pc);
        disp.decodedPort = 0x00FE;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    if (IsPort_7FFD(port))
    {
        Port_7FFD(value, pc);
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    else if (IsPort_DFFD(port))
    {
        Port_DFFD(value, pc);
        disp.decodedPort = 0xDFFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    // AY #FFFD: A15=1, A14=1, A1=0 (register select / TurboSound chip select)
    else if ((port & 0xC002) == 0xC000)
    {
        _state->pFFFD = value;
        PeripheralPortOut(0xFFFD, value);
        disp.decodedPort = 0xFFFD;
        disp.wasDecoded = true;
    }
    // AY #BFFD: A15=1, A14=0, A1=0 (data write)
    else if ((port & 0xC002) == 0x8000)
    {
        _state->pBFFD = value;
        PeripheralPortOut(0xBFFD, value);
        disp.decodedPort = 0xBFFD;
        disp.wasDecoded = true;
    }
    // RTC/CMOS: #9F/#BF/#DF/#FF, EXT mode only - takes priority over the FDC/system-port
    // decode below, since #BF/#FF alias to the Beta128 system port outside EXT mode.
    else if ((port & 0x9F) == 0x9F && IsExtMode())
    {
        // Bit 5 set (#BF/#FF) latches the register address; clear (#9F/#DF) writes data.
        if (port & 0x20)
            _cmos.SetCMOSAddress(value);
        else
            _cmos.WriteCMOS(value);
        disp.decodedPort = port & 0xFF;
        disp.wasDecoded = true;
    }
    else if (dosPorts)
    {
        const uint16_t fdcPort = DecodeFDCPort(port);
        if (fdcPort != 0)
        {
            PeripheralPortOut(fdcPort, value);
            disp.decodedPort = fdcPort;
            disp.wasDecoded = true;
        }
    }
    // Covox/SoundRive DAC: #5F (Left), #3F (Right). NORMAL mode only - the FDC/CP'M
    // port set (dosPorts above) takes priority when the disk interface is on the bus.
    else if (const uint8_t lowByte = port & 0xFF;
             lowByte == kProfiCovoxLeftPort || lowByte == kProfiCovoxRightPort)
    {
        if (_context->pSoundManager && _context->pSoundManager->hasCovox())
        {
            // Use the A ports, not B: computeStereoAmplitudes()'s mono-compatibility
            // fallback keys on LeftA/LeftB/RightA==0 and then substitutes RightB into
            // BOTH channels. Profi never touches LeftB/RightB, so routing through them
            // left the fallback armed on LeftA/RightA alone - every time Left passed
            // through exact silence while Right was active, it leaked Right into Left.
            uint16_t canonicalPort = (lowByte == kProfiCovoxLeftPort) ? Covox::PORT_LEFT_A : Covox::PORT_RIGHT_A;
            _context->pSoundManager->getCovox()->portDeviceOutMethod(canonicalPort, value);
        }
        disp.decodedPort = lowByte;
        disp.wasDecoded = true;
    }

    // Universal handler for breakpoints, tracking, analyzers
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_Profi::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Profi::SetROMPage(uint8_t page)
{
    (void)page;
}

/// Latch-to-bank translation. Port of the UnrealSpeccy set_banks() MM_PROFI case:
///   page = (DFFD[2:0] << 3 | 7FFD[2:0]) & ramMask
///   SCO=0: #4000 = 5, #C000 = page      SCO=1: #4000 = page, #C000 = 7
///   SCR=1: #8000 = 6 (else 2)
///   WOROM: RAM page 0 at #0000
///   CPM:   disk interface ports on the bus (CF_DOSPORTS)
/// The ROM slot at #0000 has already been chosen by Memory::UpdateZ80Banks() from CF_TRDOS and 7FFD.4.
void PortDecoder_Profi::UpdateModelMemoryBanks()
{
    if (!_memory)
        return;

    const CONFIG& config = _context->config;

    const uint16_t ramPages = config.ramsize ? static_cast<uint16_t>(config.ramsize / 16) : MAX_RAM_PAGES;
    const uint16_t ramMask = static_cast<uint16_t>(ramPages - 1);

    const uint8_t p7FFD = _state->p7FFD;
    const uint8_t pDFFD = _state->pDFFD;

    uint16_t page = static_cast<uint16_t>(((pDFFD & 0x07) << 3) | (p7FFD & 0x07)) & ramMask;
    uint16_t bank1 = 5;
    uint16_t bank2 = (pDFFD & 0x40) ? 6 : 2;
    uint16_t bank3 = page;

    if (pDFFD & 0x08)   // SCO: swap the roles of #4000 and #C000
    {
        bank1 = page;
        bank3 = 7;
    }

    _memory->SetRAMPageToBank1(bank1);
    _memory->SetRAMPageToBank2(bank2);
    _memory->SetRAMPageToBank3(bank3);

    if (pDFFD & 0x10)   // WOROM: RAM instead of ROM at #0000
        _memory->SetRAMPageToBank0(0);

    if (pDFFD & 0x20)   // CPM: disk interface on the bus regardless of the DOS latch
        _state->flags |= CF_DOSPORTS;
}

std::vector<ttd::PeripheralId> PortDecoder_Profi::GetTTDModelStateIds() const
{
    return {ttd::PeripheralId::ProfiPaging};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Profi::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDProfiPaging>(_context));
    return serializers;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Profi::IsPort_7FFD(uint16_t port)
{
    // Profi 7FFD: /IORQ /WR /A15 /A1 (A2 not decoded; UnrealSpeccy, ZXMAK2 0x8002/0x7FFD, Xpeccy 0x8002)
    static const uint16_t mask = 0b1000'0000'0000'0010;
    static const uint16_t match = 0b0000'0000'0000'0000;
    return (port & mask) == match;
}

bool PortDecoder_Profi::IsPort_DFFD(uint16_t port)
{
    // Profi DFFD: A15=1, A13=0, A1=0. UnrealSpeccy reaches it only after the 7FFD
    // block (A15=0) has returned, so A15=1 is implied there; making it explicit
    // here prevents #5FFD / #1FFD from hitting both handlers.
    static const uint16_t mask = 0b1010'0000'0000'0010;
    static const uint16_t match = 0b1000'0000'0000'0000;
    return (port & mask) == match;
}

bool PortDecoder_Profi::IsExtMode() const
{
    // UnrealSpeccy default: EXT = cpm && rom14. Karabas additionally treats
    // dosAct && !rom14 as EXT (exposing IDE/RTC to the SYS ROM); not implemented
    // here since it is a clone extension, not proven by UnrealSpeccy sources.
    const bool cpm = (_state->pDFFD & 0x20) != 0;
    const bool rom14 = (_state->p7FFD & 0x10) != 0;
    return cpm && rom14;
}

uint16_t PortDecoder_Profi::DecodeFDCPort(uint16_t port) const
{
    const uint8_t p1 = static_cast<uint8_t>(port);
    const bool cpm = (_state->pDFFD & 0x20) != 0;
    const bool rom14 = (_state->p7FFD & 0x10) != 0;

    if (rom14 && cpm)
    {
        // "Modified" (extended) ports: #83/#A3/#C3/#E3 and #3F (system) - UnrealSpeccy io.cpp
        if ((p1 & 0x9F) == 0x83)
            return static_cast<uint16_t>((p1 & 0x60) | 0x1F);
        if ((p1 & 0xE3) == 0x23)
            return 0x00FF;
        return 0;
    }

    // BDI ports: #1F/#3F/#5F/#7F (A7=0, A1:0=11) and the system port #FF (#BF in CP/M mode)
    if ((p1 & 0x83) == 0x03)
        return static_cast<uint16_t>((p1 & 0x60) | 0x1F);
    if ((p1 & 0xE3) == (cpm ? 0xA3 : 0xE3))
        return 0x00FF;

    return 0;
}

void PortDecoder_Profi::Port_Palette_Out(uint16_t port)
{
    // colour = ~A15..A8 (data bus is not used), index = (previous #FE value ^ 0xF) & 0xF.
    // Format: bits 7:5 G, 4:2 R, 1:0 B (UnrealSpeccy draw path; 3-3-2 superset of its Gg0Rr0Bb)
    const uint8_t index = static_cast<uint8_t>((_state->pFE ^ 0x0F) & 0x0F);
    _state->profiPalette[index] = static_cast<uint8_t>(~(port >> 8));
}

void PortDecoder_Profi::ResetPalette()
{
    // Standard 16 Spectrum colours: index = {bright, G, R, B}
    for (uint8_t i = 0; i < 16; i++)
    {
        const uint8_t blue = (i & 0x01) ? ((i & 0x08) ? 0x03 : 0x02) : 0x00;
        const uint8_t red = (i & 0x02) ? ((i & 0x08) ? 0x07 : 0x05) : 0x00;
        const uint8_t green = (i & 0x04) ? ((i & 0x08) ? 0x07 : 0x05) : 0x00;
        _state->profiPalette[i] = static_cast<uint8_t>((green << 5) | (red << 2) | blue);
    }
}

/// endregion <Helper methods>

/// Port #7FFD (128K paging) handler
void PortDecoder_Profi::Port_7FFD(uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // Lock (bit 5) blocks every bit of the write, including the screen bit,
    // unless DFFD.4 (WOROM) lifts it (UnrealSpeccy io.cpp; all reviewed emulators agree)
    if ((_state->p7FFD & 0x20) && !(_state->pDFFD & 0x10))
        return;

    _state->p7FFD = value;
    _7FFD_Locked = (value & 0x20) != 0;

    _screen->SetActiveScreen((value & 0x08) ? SCREEN_SHADOW : SCREEN_NORMAL);

    // ROM slot and RAM windows are derived from the latches
    _memory->UpdateZ80Banks();

    MLOGDEBUG(_memory->DumpMemoryBankInfo());
}

/// Port #DFFD (Profi extended paging / mode) handler
void PortDecoder_Profi::Port_DFFD(uint8_t value, [[maybe_unused]] uint16_t pc)
{
    const uint8_t changed = _state->pDFFD ^ value;
    _state->pDFFD = value;

    _memory->UpdateZ80Banks();

    // DS80 selects the 512x240 hi-res raster (Screen::DetectModeProfi)
    if (changed & 0x80)
        _context->pScreen->InitRaster();
}
