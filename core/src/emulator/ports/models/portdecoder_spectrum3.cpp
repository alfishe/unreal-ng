#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_spectrum3.h"

#include "common/collectionhelper.h"
#include "debugger/ttd/plus3/ttdplus3fdc.h"
#include "debugger/ttd/plus3/ttdplus3paging.h"
#include "emulator/io/fdc/upd765.h"

/// region <Constructors / Destructors>

PortDecoder_Spectrum3::PortDecoder_Spectrum3(EmulatorContext* context) : PortDecoder(context)
{
}

PortDecoder_Spectrum3::~PortDecoder_Spectrum3()
{
    MLOGDEBUG("PortDecoder_Spectrum3::~PortDecoder_Spectrum3()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum3::reset()
{
    // ZX-Spectrum +2A/+2B/+3 ROM pages (ROM number = #1FFD bit 2 : #7FFD bit 4)
    // 0 - editor / menu <-- set after reset
    // 1 - 128 BASIC syntax
    // 2 - +3DOS
    // 3 - 48 BASIC

    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;     // Reset port 0x7FFD to default (Screen 0, RAM bank 0, ROM 0, paging enabled)
    state.p1FFD = 0x00;     // Reset port 0x1FFD (special paging)
    state.pBFFD = 0x00;     // Reset AY register select port
    state.pFFFD = 0x00;     // Reset AY data port
    state.pFE = 0xFF;       // Reset ULA port (border white, no sound)
    state.border_attr = 0x07;  // Sync border_attr with pFE bits 0-2 (white)

    // Both latches zero: ROM 0 at #0000, RAM 5 / 2 / 0 (UpdateModelMemoryBanks)
    _context->pMemory->UpdateZ80Banks();

    // Set default border color to white
    _screen->SetBorderColor(COLOR_WHITE);

    // Explicitly force screen to SCREEN_NORMAL
    _screen->SetActiveScreen(SCREEN_NORMAL);
}

uint8_t PortDecoder_Spectrum3::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // AY #FFFD: A15=1, A14=1, A1=0. The AY-3-8910 does not decode the other
    // address bits, so mirrored ports (#FF05, #FF00, #C000...) select it on IN
    // too. Resolve mirrors to the canonical port BEFORE the weak FE (A0-only)
    // check - otherwise register readback via a mirror reaches the keyboard or
    // returns 0xFF and TurboSound players cannot detect the second chip
    // (same order as the OUT dispatch and the Pentagon decode table)
    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // Full-decode low-byte claim override (see portdecoder.h): a registered
    // low-byte card owns this cycle, so the motherboard decode chain stands
    // down - the observer was already serviced by the Z80 I/O funnel tap and
    // its cached read value is the bus value. The raw-port placeholder keeps
    // an exact Beta-128 registered key unclaimed (R6)
    {
        uint16_t claimedPort = port; // identity placeholder: no arm below resolved yet
        if (OverrideDecodeForFullDecodeClaim(port, claimedPort, disp, /*isRead*/ true))
        {
            result = GetCachedFullDecodeInValue(port);
            _lastPortDecoded = true; // the card drives the bus: no floating bus
            OnPortInComplete(port, result, pc, disp);
            return result;
        }
    }

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
    // uPD765A: #2FFD main status, #3FFD data
    else if (_context->pUPD765 != nullptr && IsPort_2FFD(port))
    {
        result = _context->pUPD765->readMainStatus();
        _lastPortDecoded = true;
        disp.decodedPort = 0x2FFD;
        disp.wasHandledInline = true;
    }
    else if (_context->pUPD765 != nullptr && IsPort_3FFD(port))
    {
        result = _context->pUPD765->readData();
        _lastPortDecoded = true;
        disp.decodedPort = 0x3FFD;
        disp.wasHandledInline = true;
    }
    else if (IsPort_FE(port))
    {
        // Call default implementation
        result = Default_Port_FE_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    else if (uint8_t mouseReg = 0; Default_IsPort_KempstonMouse(port, mouseReg))
    {
        // Kempston Mouse (design §3.2: standard decode on machines without a documented deviation)
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

void PortDecoder_Spectrum3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //    ZX Spectrum 128 +2A/+2B/+3
    //    port: #7FFD
    //    port: #1FFD

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // Full-decode low-byte claim override (see portdecoder.h): a registered
    // low-byte card owns this cycle, so the motherboard decode chain stands
    // down - the observer was already serviced by the Z80 I/O funnel tap.
    // The raw-port placeholder keeps an exact Beta-128 registered key
    // unclaimed (R6)
    {
        uint16_t claimedPort = port; // identity placeholder: no arm below resolved yet
        if (OverrideDecodeForFullDecodeClaim(port, claimedPort, disp, /*isRead*/ false))
        {
            OnPortOutComplete(port, value, pc, disp);
            return;
        }
    }

    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(value, pc);
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    bool isPort_1FFD = IsPort_1FFD(port);
    if (isPort_1FFD)
    {
        Port_1FFD(value, pc);
        disp.decodedPort = 0x1FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    // uPD765A data register (#2FFD, the status register, is read-only)
    if (_context->pUPD765 != nullptr && IsPort_3FFD(port))
    {
        _context->pUPD765->writeData(value);
        disp.decodedPort = 0x3FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    // AY #FFFD: A15=1, A14=1, A1=0 (register select / TurboSound chip select)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1100'0000'0000'0000
    if ((port & 0xC002) == 0xC000)
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

void PortDecoder_Spectrum3::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Spectrum3::SetROMPage(uint8_t page)
{
    (void)page;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Spectrum3::IsPort_FE(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #FE
    //    Match pattern: xxxxxxxx xxxxxxx0
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match     = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_FE_full & port_FE_mask) == port_FE_match && "Mask pattern incorrect");

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

bool PortDecoder_Spectrum3::IsPort_7FFD(uint16_t port)
{
    //    ZX Spectrum +2A / +3
    //    port: #7FFD
    //    Match pattern: 01xxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the 128K/+2 are controlled to by writes to port 0x7ffd.
    //    As normal on Sinclair hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_7FFD_match   = 0b0100'0000'0000'0000;

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Spectrum3::IsPort_1FFD(uint16_t port)
{
    //    ZX Spectrum +2A / +3
    //    port: #1FFD
    //    Match pattern: 0001xxxx xxxxxx0x
    //    Full pattern:  00011111 11111101
    static const uint16_t port_1FFD_full    = 0b0001'1111'1111'1101;
    static const uint16_t port_1FFD_mask    = 0b1111'0000'0000'0010;
    static const uint16_t port_1FFD_match   = 0b0001'0000'0000'0000;

    // Compile-time check
    static_assert((port_1FFD_full & port_1FFD_mask) == port_1FFD_match && "Mask pattern incorrect");

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}

bool PortDecoder_Spectrum3::IsPort_2FFD(uint16_t port)
{
    //    ZX Spectrum +3: uPD765A main status register (read)
    //    Match pattern: 0010xxxx xxxxxx0x
    //    Full pattern:  00101111 11111101
    static const uint16_t port_2FFD_full    = 0b0010'1111'1111'1101;
    static const uint16_t port_2FFD_mask    = 0b1111'0000'0000'0010;
    static const uint16_t port_2FFD_match   = 0b0010'0000'0000'0000;

    // Compile-time check
    static_assert((port_2FFD_full & port_2FFD_mask) == port_2FFD_match && "Mask pattern incorrect");

    return (port & port_2FFD_mask) == port_2FFD_match;
}

bool PortDecoder_Spectrum3::IsPort_3FFD(uint16_t port)
{
    //    ZX Spectrum +3: uPD765A data register (read / write)
    //    Match pattern: 0011xxxx xxxxxx0x
    //    Full pattern:  00111111 11111101
    static const uint16_t port_3FFD_full    = 0b0011'1111'1111'1101;
    static const uint16_t port_3FFD_mask    = 0b1111'0000'0000'0010;
    static const uint16_t port_3FFD_match   = 0b0011'0000'0000'0000;

    // Compile-time check
    static_assert((port_3FFD_full & port_3FFD_mask) == port_3FFD_match && "Mask pattern incorrect");

    return (port & port_3FFD_mask) == port_3FFD_match;
}
/// endregion <Helper methods>


/// Port #7FFD: RAM page at #C000 (bits 0-2), screen (bit 3), ROM number low
/// bit (bit 4), paging lock until reset (bit 5)
void PortDecoder_Spectrum3::Port_7FFD(uint8_t value, uint16_t pc)
{
    static const uint16_t port = 0x7FFD;
    EmulatorState& state = _context->emulatorState;
    Memory& memory = *_context->pMemory;

    // Locked: the whole port is ignored until reset. The lock is the latch's
    // own bit 5, so a reset into 48 BASIC (RM_SOS sets it) and a TTD restore
    // lock exactly like an OUT does
    if (IsPagingLocked())
        return;

    const uint8_t prevScreenNumber = (state.p7FFD & 0b0000'1000) >> 3;
    const uint8_t screenNumber = (value & 0b0000'1000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)

    state.p7FFD = value;
    memory.UpdateZ80Banks();

    if (prevScreenNumber != screenNumber)
        _screen->SetActiveScreen(screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL);

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x7FFD, port, value, pc));
        MLOGDEBUG(memory.DumpMemoryBankInfo());
    }

    /// endregion </Debug logging>
}

/// Port #1FFD: paging mode (bit 0), ROM number high bit or special RAM layout
/// (bits 1-2), disk motor (bit 3), printer strobe (bit 4)
void PortDecoder_Spectrum3::Port_1FFD(uint8_t value, uint16_t pc)
{
    (void)pc;
    EmulatorState& state = _context->emulatorState;

    // Bit 3 switches the motor of both drives, locked or not
    if (_context->pUPD765 != nullptr)
        _context->pUPD765->setMotor((value & 0b0000'1000) != 0);

    // With #7FFD locked the memory bits stay as they are; motor and strobe
    // still follow the port (MAME specpls3; xpeccy-plus and ZXMAK2 differ)
    if (state.p7FFD & 0b0010'0000)
    {
        state.p1FFD = static_cast<uint8_t>((state.p1FFD & 0b0000'0111) | (value & 0b1111'1000));
        return;
    }

    state.p1FFD = value;
    _context->pMemory->UpdateZ80Banks();
}

/// Bank layout from #7FFD and #1FFD (Memory::UpdateZ80Banks hands MM_PLUS3 here)
void PortDecoder_Spectrum3::UpdateModelMemoryBanks()
{
    const EmulatorState& state = _context->emulatorState;
    Memory& memory = *_context->pMemory;

    if (state.p1FFD & 0b0000'0001)
    {
        // Special paging: all RAM, one of four layouts picked by bits 1-2
        static constexpr uint8_t layouts[4][4] = {
            { 0, 1, 2, 3 },
            { 4, 5, 6, 7 },
            { 4, 5, 6, 3 },
            { 4, 7, 6, 3 },
        };
        const uint8_t* layout = layouts[(state.p1FFD >> 1) & 0b11];
        memory.SetRAMPageToBank0(layout[0]);
        memory.SetRAMPageToBank1(layout[1]);
        memory.SetRAMPageToBank2(layout[2]);
        memory.SetRAMPageToBank3(layout[3]);
        return;
    }

    const uint8_t rom = static_cast<uint8_t>(((state.p1FFD >> 1) & 0b10) | ((state.p7FFD >> 4) & 0b01));
    memory.SetROMPage(rom);
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(state.p7FFD & 0b0000'0111);
}

std::vector<ttd::PeripheralId> PortDecoder_Spectrum3::GetTTDModelStateIds() const
{
    // #1FFD is +2A/+3-specific: not in the model-agnostic TTDChipsetState
    if (_context->pUPD765 != nullptr)
        return { ttd::PeripheralId::Plus3Paging, ttd::PeripheralId::Upd765 };
    return { ttd::PeripheralId::Plus3Paging };
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Spectrum3::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDPlus3Paging>(_context));
    if (_context->pUPD765 != nullptr)
        serializers.push_back(std::make_unique<ttd::TTDPlus3Fdc>(_context));
    return serializers;
}
