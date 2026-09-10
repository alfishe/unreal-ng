#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_scorpion256.h"

#include "common/collectionhelper.h"
#include "emulator/video/ulacontention.h"

#include <cstdint>

/// Beta128 register select is wired to A7-A0 only: IN A,(#1F) drives A onto
/// A15-A8 and TR-DOS's OUT (#nnFF),A system-port writes carry data-dependent
/// bits in the high byte, so the FDC must match on the LOW address byte and
/// dispatch through the canonical #001F/#003F/#005F/#007F/#00FF device keys -
/// the same normalization the Pentagon decode table performs before handing
/// a port to PeripheralPortIn/Out()
/// \param port Raw 16-bit port address from the Z80 bus
/// \param canonicalPort Out: registered device key for the matched register
/// \return True when the low byte selects a Beta128 register
static bool TryBeta128MirrorPort(uint16_t port, uint16_t& canonicalPort)
{
    switch (port & 0x00FF)
    {
        case 0x001F:
        case 0x003F:
        case 0x005F:
        case 0x007F:
        case 0x00FF:
            canonicalPort = static_cast<uint16_t>(port & 0x00FF);
            return true;
        default:
            return false;
    }
}

/// region <Constructors / Destructors>

PortDecoder_Scorpion256::PortDecoder_Scorpion256(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Scorpion256::~PortDecoder_Scorpion256()
{
    MLOGDEBUG("PortDecoder_Scorpion256::~PortDecoder_Scorpion256()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Scorpion256::reset()
{
    // Scorpion 256 ROM setup
    // Similar to Spectrum 128K
    // Bit 4 of port 0x7FFD: 0 = SOS ROM, 1 = 128K ROM

    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;     // Reset port 0x7FFD to default (Screen 0, RAM bank 0, SOS ROM, paging enabled)
    state.p1FFD = 0x00;     // Reset port 0x1FFD (no RAM at #0000, Shadow Monitor off, extended bank bits clear)
    state.p7EFD = 0x00;     // Reset ProfROM window latch
    state.profrom_bank = 0x00;  // ProfROM quadrant 0 at power-on (design §4.2)
    state.scorpionDosTrigger = 0x00;  // Magic-button DOS trigger cleared by RESET (hardware-reference §9)
    state.scorpion_turbo = 0x00;    // Turbo flip-flop cleared by RESET - 3.5 MHz (hardware-reference 13)
    state.pBFFD = 0x00;     // Reset AY register select port
    state.pFFFD = 0x00;     // Reset AY data port
    state.pFE = 0xF8;       // Reset ULA port (border black, no sound; keys released)
    state.border_attr = 0x00;  // Sync border_attr with pFE bits 0-2 (black)

    // Set default 128K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMMode(RM_SOS);      // SOS ROM at reset
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);

    // Set default border color to black: the Scorpion power-on latch value is 0 and
    // v2.9x ROMs never write #FF during boot, so the machine shows a black border
    // until software sets one (hardware-reference 6, MISTer-verified)
    _screen->SetBorderColor(COLOR_BLACK);

    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Explicitly force screen to SCREEN_NORMAL
    _screen->SetActiveScreen(SCREEN_NORMAL);

    // Set default memory paging state
    Port_7FFD(0x00, 0x0000);
}

uint8_t PortDecoder_Scorpion256::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // Scorpion ZS-256 Turbo+ hardware turbo flip-flop (hardware-reference 13):
    // the turbo GAL clocks its mode latch on IORQ READS whose address matches
    // the #7FFD (set, 01xxxxxxxx1xxx01) or #1FFD (reset, 00xxxxxxxx1xxx01)
    // paging-register decode - A15:A14, A5, A1, A0 only, so every address mirror
    // clocks it too (including the #7EFD ProfROM window port, whose A15:A14
    // pattern matches the set family). The strobe is a pure address decode: it
    // fires whether or not a device claims the read, and the data bus stays
    // undriven, so the returned value is meaningless and is served below as
    // usual. Applies to every Scorpion configuration (MM_SCORP base ZS-256 and
    // MM_PROFSCORP alike). The effective speed changes at the next frame
    // boundary (Z80::Z80FrameCycle composes host speed x turbo)
    switch (port & 0xC023)
    {
        case 0x4021:
            _state->scorpion_turbo = 1;     // IN #7FFD family - 7 MHz
            break;
        case 0x0021:
            _state->scorpion_turbo = 0;     // IN #1FFD family - 3.5 MHz
            break;
        default:
            break;
    }

    // AY #FFFD: A15=1, A14=1, A1=0. The AY-3-8910 does not decode the other
    // address bits, so mirrored ports (#FF05, #FF00, #C000...) select it on IN
    // too. Resolve mirrors to the canonical port BEFORE the weak FE (A0-only)
    // check - otherwise register readback via a mirror reaches the keyboard or
    // returns 0xFF and TurboSound players cannot detect the second chip
    // (same order as the OUT dispatch and the Pentagon decode table)
    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // Resolve Beta128 mirrors once: the FDC decodes A7-A0 only
    uint16_t beta128Port = 0;
    const bool isBeta128 = TryBeta128MirrorPort(port, beta128Port);

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
    else if (IsPort_FE(port))
    {
        // Call default implementation
        result = Default_Port_FE_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    else if (IsPort_1FFD(port))
    {
        // Write-only memory register: the register itself never drives the
        // bus, reads return #FF (hardware-reference 4.3) - marked decoded so
        // the access is attributed instead of hitting the unmapped-port path
        result = 0xFF;
        _lastPortDecoded = true;
        disp.decodedPort = 0x1FFD;
        disp.wasHandledInline = true;
    }
    else if (_context->config.mem_model == MM_PROFSCORP && IsPort_7EFD(port))
    {
        // Write-only ProfROM window latch (#7FFD pattern with A8 low): open
        // bus on read
        result = 0xFF;
        _lastPortDecoded = true;
        disp.decodedPort = 0x7EFD;
        disp.wasHandledInline = true;
    }
    else if (isBeta128 && !(_state->flags & CF_TRDOS) && !(_state->p1FFD & 0x02)
             && !_state->scorpionDosTrigger)
    {
        // Beta128 FDC off the bus: with no TR-DOS session, the Shadow Monitor
        // unpaged and the magic-button DOS trigger disarmed, #1F/#3F/#5F/#7F/#FF
        // stay undecoded so Z80::in() serves the floating bus. While the monitor
        // is paged (#1FFD bit1) or the trigger is armed the FDC keeps answering -
        // the monitor polls it right after unpaging (hardware-reference 12.3,
        // MISTer bug 2) and MAME selects the same DOS I/O view on the trigger
        disp.wasBeta128Gated = true;
    }
    else
    {
        // Beta128 mirrors dispatch through the canonical registered device
        // key; everything else keeps its identity (Covox etc.)
        const uint16_t dispatchPort = isBeta128 ? beta128Port : port;
        result = PeripheralPortIn(dispatchPort);
        // Identity decode: mark decoded only when a device actually responded
        if (_lastPortDecoded)
            disp.decodedPort = dispatchPort;
    }

    // Scorpion floating bus (programmer's manual, port #FF): a read from ANY
    // non-existent port returns the attribute byte of the screen cell being
    // fetched right now - the attribute latch keeps driving the data bus.
    // Border/blanking reads return #FF. The ProfROM monitor times its raster
    // waits on this stream, so it must be live for both even and odd ports
    // (unlike the ZX-model floating-bus fallback in Z80::in which only serves
    // odd ports behind the FloatBus config toggle)
    if (!_lastPortDecoded && _context->pUlaContention)
        result = _context->pUlaContention->GetFloatingBusAttribute();

    disp.wasDecoded = _lastPortDecoded;

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);
    }
    /// endregion </Debug logging>

    // Universal handler for breakpoints, tracking, analyzers
    OnPortInComplete(port, result, pc, disp);

    return result;
}

void PortDecoder_Scorpion256::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //    Scorpion ZS256
    //    port: #7FFD, #1FFD - memory registers
    //    port: #7EFD        - ProfROM window latch (MM_PROFSCORP)
    //    port: #FE           - ULA (keyboard, border, beeper, tape)
    //    port: #xxFF         - border latch (while the FDC system port is off the bus)

    EmulatorState& state = *_state;

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // Resolve Beta128 mirrors once: the FDC decodes A7-A0 only
    uint16_t beta128Port = 0;
    const bool isBeta128 = TryBeta128MirrorPort(port, beta128Port);

    // The ProfROM window latch must be checked first: its pattern is a subset
    // of the #7FFD decode (the base-machine GAL equation ignores A8), so on a
    // ProfROM machine the window write must not fall through to the memory
    // register. On the base model #7EFD stays a plain #7FFD mirror
    if (_context->config.mem_model == MM_PROFSCORP && IsPort_7EFD(port))
    {
        // Window select (design §4.2): the latch is stored verbatim, then the
        // quadrant's window bits above the GAL state are refreshed and the ROM
        // bases rebuilt - inert for images <= 256 KB (window mask 0)
        Memory& memory = *_context->pMemory;
        memory.GetScorpionRomWindow().OnWindowPortWrite(state, _context->temporary, value);
        memory.UpdateZ80Banks();

        disp.decodedPort = 0x7EFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    else if (IsPort_7FFD(port))
    {
        Port_7FFD(value, pc);
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    else if (IsPort_1FFD(port))
    {
        Port_1FFD(value, pc);
        disp.decodedPort = 0x1FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    else if (IsPort_FE(port))
    {
        Default_Port_FE_Out(port, value, pc);
        disp.decodedPort = 0x00FE;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    // AY #FFFD: A15=1, A14=1, A1=0 (register select / TurboSound chip select)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1100'0000'0000'0000
    else if ((port & 0xC002) == 0xC000)
    {
        _state->pFFFD = value;
        PeripheralPortOut(0xFFFD, value);
        disp.decodedPort = 0xFFFD;
        disp.wasDecoded = true;
    }
    // AY #BFFD: A15=1, A14=0, A1=0 (data write)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1000'0000'0000'0000
    else if ((port & 0xC002) == 0x8000)
    {
        _state->pBFFD = value;
        PeripheralPortOut(0xBFFD, value);
        disp.decodedPort = 0xBFFD;
        disp.wasDecoded = true;
    }

    // Beta128 FDC ports are on the bus only while a TR-DOS session is open,
    // the Shadow Monitor is paged (#1FFD bit1 - the monitor talks to the FDC
    // with the session closed, hardware-reference 12.3) or the magic-button
    // DOS trigger is armed (MAME selects the same DOS I/O shadow view on it)
    else if (isBeta128 && ((state.flags & CF_TRDOS) || (state.p1FFD & 0x02)
                           || state.scorpionDosTrigger))
    {
        // Mirrors (e.g. OUT (#3CFF),A drive-select with A on A15-A8) dispatch
        // through the canonical device key - raw addresses miss the exact-key
        // peripheral map and used to fall through to the border arm below
        PeripheralPortOut(beta128Port, value);
        disp.decodedPort = beta128Port;
        disp.wasDecoded = true;
    }

    // Border port: match the LOW BYTE only - OUT (#FF),A puts A on lines
    // A15-A8, so the decoder sees #nnFF for whatever A holds and an exact
    // 16-bit compare would miss every such write. No collision with the arms
    // above: #FFFD/#7FFD/#1FFD end in #FD, both AY masks need A1=0, the #FE
    // pattern needs A0=0
    else if ((port & 0x00FF) == 0x00FF)
    {
        // Reached only when the gating arm above left the FDC system port
        // undecoded; inside a session the byte goes to the FDC instead
        state.border_attr = value & 0b111;
        _screen->SetBorderColor(value & 0b111);
        disp.decodedPort = 0x00FF;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        disp.wasBeta128Gated = isBeta128;  // trace attribution: FDC off the bus
    }

    // Gated FDC writes without a border collision: hardware ignores them
    else if (isBeta128)
    {
        disp.wasBeta128Gated = true;
    }

    // Everything else: registered peripherals (Covox etc.)
    else
    {
        PeripheralPortOut(port, value);
        disp.wasDecoded = true;
    }

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (_logger && _logger->GetLevel() <= LoggerLevel::LogInfo)
    {
        if (!key_exists(_loggingMutePorts, port))
        {
            // Determine RAM/ROM page where code executed from
            std::string currentMemoryPage = GetPCAddressLocator(pc);
            MLOGINFO("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
        }
    }
    /// endregion </Debug logging>

    // Universal handler for breakpoints, tracking, analyzers
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_Scorpion256::SetRAMPage(uint8_t page)
{
    // Debugger-forced page: translate back into the latch pair and reapply,
    // so the debug view and the latch state stay coherent (design 5)
    EmulatorState& state = *_state;

    state.p7FFD = static_cast<uint8_t>((state.p7FFD & ~0b111) | (page & 0b111));
    state.p1FFD = static_cast<uint8_t>((state.p1FFD & ~0x10) | ((page & 0x08) ? 0x10 : 0));
    state.p1FFD = static_cast<uint8_t>((state.p1FFD & ~0xC0) | ((page & 0x30) << 2));

    _context->pMemory->UpdateZ80Banks();
}

void PortDecoder_Scorpion256::SetROMPage(uint8_t page)
{
    // Debugger-forced ROM page: set the select bits the #0000 priority chain
    // reads (CF_TRDOS / #1FFD bit1 / #7FFD bit4) for the role the page belongs
    // to, then reapply through the single translation point
    Memory& memory = *_context->pMemory;
    EmulatorState& state = *_state;

    if (page == static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_sys_rom)))
    {
        state.p1FFD |= 0x02;      // Shadow Monitor
    }
    else if (page == static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_dos_rom)))
    {
        state.flags |= CF_TRDOS;  // open a TR-DOS session
    }
    else if (page == static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_sos_rom)))
    {
        state.p1FFD &= static_cast<uint8_t>(~0x02);
        state.flags &= ~CF_TRDOS;
        state.p7FFD |= 0x10;      // ROM1: 48K BASIC
    }
    else
    {
        state.p1FFD &= static_cast<uint8_t>(~0x02);
        state.flags &= ~CF_TRDOS;
        state.p7FFD &= static_cast<uint8_t>(~0x10);  // ROM0: BASIC 128
    }

    memory.UpdateZ80Banks();
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Scorpion256::IsPort_FE(uint16_t port)
{
    //    Scorpion ZS256
    //    Port: #FE
    //    Match pattern: xxxxxxxx xx1xxx10
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0010'0011;
    static const uint16_t port_FE_match     = 0b0000'0000'0010'0010;

    // Compile-time check
    static_assert((port_FE_full & port_FE_mask) == port_FE_match && "Mask pattern incorrect");

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

bool PortDecoder_Scorpion256::IsPort_7FFD(uint16_t port)
{
    //    Scorpion ZS256
    //    port: #7FFD
    //    Full match  :  01111111 11111101
    //    Match pattern: 01x1xxxx xx1xx101
    //    Equation: /IORQ /WR M1 /A15 A14 A12 A5 A2 /A1 A0
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1101'0000'0010'0111;
    static const uint16_t port_7FFD_match   = 0b0101'0000'0010'0101;

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Scorpion256::IsPort_1FFD(uint16_t port)
{
    //    Scorpion ZS256
    //    port: #1FFD
    //    Full match:    00011111 11111101
    //    Match pattern: 00x1xxxx xx1xx101
    //    /IORQ /WR M1 /A15 /A14 A12 A5 A2 /A1 A0
    static const uint16_t port_1FFD_full    = 0b0001'1111'1111'1101;
    static const uint16_t port_1FFD_mask    = 0b1101'0000'0010'0111;
    static const uint16_t port_1FFD_match   = 0b0001'0000'0010'0101;

    // Compile-time check
    static_assert((port_1FFD_full & port_1FFD_mask) == port_1FFD_match && "Mask pattern incorrect");

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}

bool PortDecoder_Scorpion256::IsPort_7EFD(uint16_t port)
{
    //    Scorpion ZS256 ProfROM
    //    port: #7EFD
    //    Full match:    01111110 11111101
    //    Match pattern: 01x1xxxx 0x1xx101 (A8 low separates it from #7FFD)
    static const uint16_t port_7EFD_full    = 0b0111'1110'1111'1101;
    static const uint16_t port_7EFD_mask    = 0b1101'0001'0010'0111;
    static const uint16_t port_7EFD_match   = 0b0101'0000'0010'0101;

    // Compile-time check
    static_assert((port_7EFD_full & port_7EFD_mask) == port_7EFD_match && "Mask pattern incorrect");

    bool result = (port & port_7EFD_mask) == port_7EFD_match;

    return result;
}
/// endregion </Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_7FFD(uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //  Port: #7FFD
    //  Bits:
    //      D0 = RAM - bit0 ;128 kB memory
    //      D1 = RAM - bit1 ;128 kB memory
    //      D2 = RAM - bit2 ;128 kB memory
    //      D3 = Screen (Normal (Bank5) | Shadow (Bank 7))
    //      D4 = ROM (ROM0 = 128k ROM | ROM1 = 48k ROM)
    //      D5 = Disable memory paging (both ROM and RAM) until reset
    //      D6 = unused
    //      D7 = unused

    static const uint16_t port = 0x7FFD;
    Memory& memory = *_context->pMemory;

    // The disabling latch is kept until reset. Once D5 has been written the
    // whole port is frozen (screen bit included), but the locking write itself
    // applies: latch first, then derive the lock bit. All bank math — the
    // #7FFD low bits plus the #1FFD D4/D6/D7 extensions and the #0000 priority
    // chain — lives in Memory::UpdateScorpionBanks() behind the single
    // UpdateZ80Banks() call (design §3/§5), so the latch that snapshots and
    // the debugger read back is exactly the byte that was written
    if (!_7FFD_Locked)
    {
        _state->p7FFD = value;
        memory.UpdateZ80Banks();

        uint8_t screenNumber = (value & 0b00001000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
        SpectrumScreenEnum screen = screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL;
        _screen->SetActiveScreen(screen);

        _7FFD_Locked = value & 0b00100000;
    }

    MLOGDEBUG(memory.DumpMemoryBankInfo());

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x7FFD, port, value, pc));
        MLOGDEBUG(memory.DumpMemoryBankInfo());
    }

    /// endregion </Debug logging>
}

/// Port #1FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_1FFD(uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //  Port: #1FFD
    //  Bits:
    //      D0     RAM bank 0 at #0000 (priority over every ROM select)
    //      D1     Shadow Monitor (service ROM) at #0000
    //      D2     RS-232 output line - no memory effect (HW ref. 12.9)
    //      D4     RAM bank bit 3 at #C000
    //      D6-D7  RAM bank bits 5:4 at #C000 (1 MB machines)
    //  Not gated by the #7FFD lock latch - the hardware register is independent

    static const uint16_t port = 0x1FFD;
    Memory& memory = *_context->pMemory;

    _state->p1FFD = value;
    memory.UpdateZ80Banks();

    MLOGDEBUG(memory.DumpMemoryBankInfo());

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x1FFD, port, value, pc));
        MLOGDEBUG(memory.DumpMemoryBankInfo());
    }

    /// endregion </Debug logging>
}
