#include "stdafx.h"
#include "portdecoder_atm450.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/rom.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

// Not the v7.10 board: no keyboard controller (#FE is the plain matrix port), no INTERNAL I/O connector, no
// 7 MHz RAM waits of the v7.10 arbiter; its own #FE decode (A0)
PortDecoder_ATM450::PortDecoder_ATM450(EmulatorContext* context) : PortDecoder_ATM710(context, false) {}

PortDecoder_ATM450::~PortDecoder_ATM450()
{
    MLOGDEBUG("PortDecoder_ATM450::~PortDecoder_ATM450()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_ATM450::reset()
{
    // p7FFD, palette RAM back to the ZX preset, bright border off, base clock
    PortDecoder_ATM710::reset();

    // UnrealSpeccy reset(): comp.pFDFD = 0 (z80.cpp). aFE / aFB are
    // mode-dependent and set by ApplyBootROMDefaults() right after this
    _state->pFDFD = 0x00;
}

void PortDecoder_ATM450::ApplyBootROMDefaults(ROMModeEnum mode)
{
    // Port of the UnrealSpeccy reset(mode) MM_ATM450 block (z80.cpp): RM_DOS
    // boots straight into TR-DOS with the ZX screen; every other mode starts
    // the machine in the system ROM (CPSYS), video mode 0, and leaves the
    // ROM / video choice to it. ZXMAK2 BusReset does the same (AFE |= 0x80,
    // AFB |= 0x80). The CF_TRDOS / 7FFD.4 part of the mode is applied by the
    // generic Memory::SetROMMode table (RM_DOS keeps 7FFD.4 set on 4.50)
    if (mode == RM_DOS)
    {
        Port_FE_AddressLatch(ATM450_AFE_ROM | ATM450_AFE_VMODE);
        _state->aFB = 0x00;
    }
    else
    {
        Port_FE_AddressLatch(ATM450_AFE_ROM);
        _state->aFB = ATM450_AFB_CPSYS;
    }

    if (_memory)
        _memory->UpdateZ80Banks();
}

void PortDecoder_ATM450::EnterSpectrum128Paging([[maybe_unused]] uint16_t pc)
{
    // The reset starts the machine in its system ROM (CPSYS) with video mode 0 and leaves the ROM / video choice to it. A
    // Spectrum program wants what RM_DOS sets up: ROM at #0000, the ZX screen, the 48K / 128K / DOS ROM by #7FFD
    Port_FE_AddressLatch(ATM450_AFE_ROM | ATM450_AFE_VMODE);
    _state->aFB = 0x00;
    _state->pFDFD = 0x00;
    if (_memory)
        _memory->UpdateZ80Banks();
}

IdeAdapter::Gate PortDecoder_ATM450::IdeGate()
{
    IdeAdapter::Gate gate = PortDecoder::IdeGate();
    gate.dosPorts = (_state->flags & CF_DOSPORTS) != 0;
    return gate;
}

uint8_t PortDecoder_ATM450::DecodePortIn(uint16_t port, uint16_t pc)
{
    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (uint8_t ideValue = 0xFF; TryIdePortIn(port, pc, ideValue))
        return ideValue;

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    PortDecodeDisposition disp;

    // Canonical Beta128 aliases first - see PortDecoder_ATM710::DecodePortIn
    uint16_t decodedPort = decodePort(port);

    // A registered full-decode low-byte card owns the cycle (portdecoder.h)
    if (OverrideDecodeForFullDecodeClaim(port, decodedPort, disp, /*isRead*/ true))
    {
        result = GetCachedFullDecodeInValue(port);
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc, disp);
        return result;
    }

    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // Port #FE - keyboard, tape; bit 7 is the PAL marker (UnrealSpeccy
    // `val = (val & 0x7F) | atm450_z(cpu.t)`)
    if (IsPort_FE(port))
    {
        const uint32_t sinceInt = _context->pCore ? TStatesSinceInt(_context->pCore->GetZ80()->t) : 0;
        result = static_cast<uint8_t>((Default_Port_FE_In(port, pc) & 0x7F) | PalMarker(sinceInt));
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    // Port #FFFD - AY register read
    else if (IsPort_FFFD(port))
    {
        result = PeripheralPortIn(PORT_FFFD);
        _lastPortDecoded = true;
        disp.decodedPort = PORT_FFFD;
    }
    // General Sound host ports: claimed before the aFB latch (UnrealSpeccy
    // io.cpp checks `(port & 0xF7) == 0xB3` long before the A2=0 arm), so a
    // GS status read never flips CPSYS
    else if ((port & 0x00F7) == 0x00B3)
    {
        const uint16_t gsPort = (port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3;
        result = PeripheralPortIn(gsPort);
        disp.decodedPort = gsPort;
    }
    // An external ZX-bus Kempston mouse card (PortDecoder_ATM710::IsPort_KempstonMouse)
    else if (uint8_t mouseRegister = 0; IsPort_KempstonMouse(port, mouseRegister))
    {
        result = _mouse->ReadRegister(mouseRegister);
        _lastPortDecoded = true;
        disp.decodedPort = mouseRegister == 0 ? 0xFADF : (mouseRegister == 1 ? 0xFBDF : 0xFFDF);
    }
    // Beta128 FDC ports: only inside the TR-DOS session (UnrealSpeccy
    // CF_DOSPORTS; requirements OQ-7 records ZXMAK2's wider SYSEN gate)
    else if (IsBeta128Port(decodedPort) && (_state->flags & CF_DOSPORTS))
    {
        result = PeripheralPortIn(decodedPort);
        _lastPortDecoded = true;
        disp.decodedPort = decodedPort;
    }
    // aFB: any other read with A2 = 0 latches the low address byte, the bus
    // reads #FF (UnrealSpeccy io.cpp, the wide decode "for MODPLAYi")
    else if (IsPort_FB(port))
    {
        Port_FB_AddressLatch(port);
        result = 0xFF;
        _lastPortDecoded = true;
        disp.decodedPort = port;
        disp.device = PortDeviceId::Memory_Windows;
        disp.wasHandledInline = true;
    }

    disp.wasDecoded = _lastPortDecoded;
    OnPortInComplete(port, result, pc, disp);
    return result;
}

void PortDecoder_ATM450::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (TryIdePortOut(port, value, pc))
        return;

    PortDecodeDisposition disp;
    uint16_t decodedPort = decodePort(port);

    if (OverrideDecodeForFullDecodeClaim(port, decodedPort, disp, /*isRead*/ false))
    {
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.wasDecoded = true;

    // Port #FE - border, beeper, tape, then the ATM side effects of the same
    // cycle: the bright border bit from A3 and the aFE address latch
    if (IsPort_FE(port))
    {
        Default_Port_FE_Out(port, value, pc);
        _state->atmBorderBright = (port & 0x0008) ? 0 : 1;
        Port_FE_AddressLatch(port);

        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }

    // No else: UnrealSpeccy does not return after #FE, so an even port with
    // A1 = 0 (#FC, #7C...) also reaches the A1 = 0 group below
    if ((port & 0x0002) == 0)
    {
        uint16_t armPort = 0x0000;
        PortDeviceId armDevice = PortDeviceId::None;

        if (IsPort_7DFD(port))
        {
            Port_7DFD_PaletteOut(value);
            armPort = 0x7DFD;
            armDevice = PortDeviceId::Palette;
        }
        else if (IsPort_7FFD(port))
        {
            Port_7FFD_Out(port, value, pc);
            armPort = 0x7FFD;
        }
        else if (IsPort_FDFD(port))
        {
            Port_FDFD_Out(value);
            armPort = 0xFDFD;
            armDevice = PortDeviceId::Memory_Windows;
        }
        // AY: the A15 = 1, A9 = 1 group; A14 picks select / data
        else if (IsPort_FFFD(port))
        {
            PeripheralPortOut(PORT_FFFD, value);
            if (disp.decodedPort == 0x0000)
                disp.decodedPort = PORT_FFFD;
        }
        else if (IsPort_BFFD(port))
        {
            PeripheralPortOut(PORT_BFFD, value);
            if (disp.decodedPort == 0x0000)
                disp.decodedPort = PORT_BFFD;
        }

        if (armPort != 0x0000 && disp.decodedPort == 0x0000)
        {
            disp.decodedPort = armPort;
            disp.device = armDevice;
            disp.wasHandledInline = true;
        }
    }
    // General Sound host ports (#B3 / #BB bit3-masked, #33 full)
    else if ((port & 0x00F7) == 0x00B3)
    {
        const uint16_t gsPort = (port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3;
        PeripheralPortOut(gsPort, value);
        disp.decodedPort = gsPort;
    }
    else if ((port & 0x00FF) == 0x0033)
    {
        PeripheralPortOut(0x0033, value);
        disp.decodedPort = 0x0033;
    }
    // Beta128 FDC ports inside the TR-DOS session. #FF bit 6 (DDEN) is masked
    // to MFM as on ATM710 - UnrealSpeccy ignores that bit on every model
    else if (IsBeta128Port(decodedPort) && (_state->flags & CF_DOSPORTS))
    {
        uint8_t fdcValue = value;
        if (decodedPort == 0x00FF)
            fdcValue &= 0b1011'1111;
        PeripheralPortOut(decodedPort, fdcValue);
        disp.decodedPort = decodedPort;
    }

    if (disp.decodedPort == 0x0000)
        disp.wasDecoded = false;  // no arm took it
    OnPortOutComplete(port, value, pc, disp);
}

/// endregion </Interface methods>

/// region <Port detection>

uint32_t PortDecoder_ATM450::TStatesSinceInt(uint32_t z80T) const
{
    // UnrealSpeccy counts cpu.t from the INT edge (INT is active while
    // t < intlen); this core counts from the frame start and raises INT at
    // intstart + 1 (Z80::_intStart, config.cpp ATM timing). The PAL windows are
    // INT-relative: the system ROM samples Z at a fixed delay after HALT and
    // turns the 16 samples into the key that decrypts its CP/M loader
    const CONFIG& config = _context->config;
    const uint32_t frame = config.frame ? config.frame : 1;
    const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1;
    const uint32_t t = (z80T / multiplier) % frame;
    const uint32_t intT = (config.intstart + 1) % frame;
    return (t + frame - intT) % frame;
}

uint8_t PortDecoder_ATM450::PalMarker(uint32_t sinceInt)
{
    // UnrealSpeccy atm450_z(): "PAL hardware gives 3 zeros in secret short
    // time intervals" - normal-speed branch only (the 4.50 has no turbo)
    for (uint32_t start : ATM450_PALZ_WINDOW_STARTS)
    {
        if (sinceInt - start < ATM450_PALZ_WINDOW_LENGTH)
            return 0x00;
    }

    return 0x80;
}

/// endregion </Port detection>

/// region <Port handlers>

void PortDecoder_ATM450::Port_FE_AddressLatch(uint16_t port)
{
    // set_atm_aFE((unsigned char)port): the latch takes the LOW address byte
    const uint8_t oldValue = _state->aFE;
    const uint8_t newValue = static_cast<uint8_t>(port & 0x00FF);
    _state->aFE = newValue;

    const uint8_t changed = oldValue ^ newValue;

    // A6-A5: video mode RG0 / RG1 (Screen::DetectModeATM1 reads aFE). The
    // board has no memory swap on A6 - the UnrealSpeccy atm_memswap() call
    // here is gated behind the default-off AtmMemSwap ini option and is not
    // hardware (ATM manual appendix 2, schematic D50: A6 -> RG0)
    if ((changed & ATM450_AFE_VMODE) && _context->pScreen)
    {
        _context->pScreen->InitRaster();
        MLOGDEBUG("Port_FE_AddressLatch: video mode %d -> %d", (oldValue >> 5) & 3, (newValue >> 5) & 3);
    }

    // A7: ROM / RAM at #0000
    if ((changed & ATM450_AFE_ROM) && _memory)
        _memory->UpdateZ80Banks();
}

void PortDecoder_ATM450::Port_FB_AddressLatch(uint16_t port)
{
    // comp.aFB = (unsigned char)port; set_banks()
    _state->aFB = static_cast<uint8_t>(port & 0x00FF);

    if (_memory)
        _memory->UpdateZ80Banks();
}

void PortDecoder_ATM450::Port_FDFD_Out(uint8_t value)
{
    _state->pFDFD = value;

    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_FDFD_Out: value=0x%02X RAMEXT:%d CPNET:%d", value, value & ATM450_FDFD_RAM_EXT,
              (value & ATM450_FDFD_CPNET) ? 1 : 0);
}

// UnrealSpeccy atm_writepal() with the ATM1 table (draw.cpp, identical in
// ZXMAK2 UlaAtm450): the cell is the 4-bit border color; the data byte is
// --grbGRB, active-low. Each channel has a high (GRB, bits 2-0) and a low
// (grb, bits 5-3) line - the same 2-bit DAC the ATM2 formula drives, so the
// 4-bit component is 0xA * high + 5 * low on the {0x00, 0x11 .. 0xFF} ladder
void PortDecoder_ATM450::Port_7DFD_PaletteOut(uint8_t value)
{
    const uint8_t cell = static_cast<uint8_t>((_state->border_attr & 0x07) | ((_state->atmBorderBright & 1) << 3));
    _state->atmPaletteRegs[cell] = value;

    const uint8_t v = static_cast<uint8_t>(value ^ 0xFF);
    const uint8_t blue = static_cast<uint8_t>(0x0A * ((v >> 0) & 1) + 0x05 * ((v >> 3) & 1));
    const uint8_t red = static_cast<uint8_t>(0x0A * ((v >> 1) & 1) + 0x05 * ((v >> 4) & 1));
    const uint8_t green = static_cast<uint8_t>(0x0A * ((v >> 2) & 1) + 0x05 * ((v >> 5) & 1));

    // 4-bit -> 8-bit is x * 0x11 (the atm2clev ladder); ABGR packing like the ULA tables
    _state->atmPalette[cell] = 0xFF000000u | (static_cast<uint32_t>(blue * 0x11) << 16) |
                               (static_cast<uint32_t>(green * 0x11) << 8) | static_cast<uint32_t>(red * 0x11);

    MLOGDEBUG("Port_7DFD_PaletteOut: value=0x%02X cell=%d -> 0x%08X", value, cell, _state->atmPalette[cell]);
    if (_context->pScreen)
        _context->pScreen->NoteVideoTableWrite(videomap::VideoTable::Palette, cell);  // the video change log
}

void PortDecoder_ATM450::updateMemoryBanks()
{
    if (!_memory)
        return;

    // Port of the UnrealSpeccy set_banks() MM_ATM450 branch (memory.cpp) on
    // top of the generic 128K mapping it starts from, with the #FDFD bit
    // assignment of the board itself (ATM manual, schematic D3: D0 -> EA16,
    // D1 -> EA17, D2 -> RA16, D3 -> CPNET; ZXMAK2 MemoryAtm450 agrees).
    // UnrealSpeccy feeds D2 into the RAM page instead - invisible at the
    // board's 512 KiB, where the mask drops that bit anyway

    // Window 3: #7FFD page extended by pFDFD bits 1-0 (EA16 / EA17)
    const uint16_t page3 =
        static_cast<uint16_t>(((_state->p7FFD & 0x07) | ((_state->pFDFD & ATM450_FDFD_RAM_EXT) << 3)) &
                              _memory->GetRamMask());
    _memory->SetRAMPageToBank3(page3);
    _memory->SetRAMPageToBank2(2);

    // aFE.7 = 0: RAM at #0000 - page 0 in window 0 and page 4 (not 5) in window 1
    if (!(_state->aFE & ATM450_AFE_ROM))
    {
        _memory->SetRAMPageToBank0(0);
        _memory->SetRAMPageToBank1(4);
        return;
    }

    _memory->SetRAMPageToBank1(5);

    const bool trdos = (_state->flags & CF_TRDOS) != 0;

    // The ROM arbitration rewrites the CPSYS latch itself: the 48K lock drops
    // it, CPNET inside a TR-DOS session raises it ("more priority, then 7FFD")
    if (_state->p7FFD & PORT_7FFD_LOCK)
        _state->aFB &= static_cast<uint8_t>(~ATM450_AFB_CPSYS);
    if (trdos && (_state->pFDFD & ATM450_FDFD_CPNET))
        _state->aFB |= ATM450_AFB_CPSYS;

    if (_state->aFB & ATM450_AFB_CPSYS)
        _memory->SetROMSystem();
    else if (trdos)
        _memory->SetROMDOS();  // whatever 7FFD.4 says
    else if (_state->p7FFD & 0x10)
        _memory->SetROM48k();
    else
        _memory->SetROM128k();

    // RA16 (#FDFD D2): ROM A16 - the upper 64 KiB of a 128 KiB "ROM disc"
    // (27C010). The shipped 64 KiB image has nothing there: the bit only
    // selects pages that are actually loaded
    if (_state->pFDFD & ATM450_FDFD_ROM_A16)
    {
        const uint8_t romBanks =
            (_context->pCore && _context->pCore->GetROM()) ? _context->pCore->GetROM()->GetROMBanksLoaded() : 0;
        const uint16_t page = _memory->GetROMPageForBank(0);
        if (romBanks > 4 && page != MEMORY_UNMAPPABLE)
            _memory->SetROMPageToBank(0, static_cast<uint16_t>((page | 4) & (romBanks - 1)));
    }
}

void PortDecoder_ATM450::updateTurboMode()
{
    // No software clock switch on the 4.50 board (requirements R8)
    _state->hw_turbo_ratio = 1;
}

/// endregion </Port handlers>
