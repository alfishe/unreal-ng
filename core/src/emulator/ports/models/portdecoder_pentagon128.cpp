#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_pentagon128.h"

#include "common/collectionhelper.h"
#include "common/stringhelper.h"
#include "cassert"

/// region <Constructors / Destructors>

PortDecoder_Pentagon128::PortDecoder_Pentagon128(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
    
    // Initialize screen pointer from context
    if (_context != nullptr)
    {
        _screen = _context->pScreen;
    }
    else
    {
        // If context is null, we can't initialize screen
        _screen = nullptr;
    }
}

PortDecoder_Pentagon128::~PortDecoder_Pentagon128()
{
    MLOGDEBUG("PortDecoder_Pentagon128::~PortDecoder_Pentagon128()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

// See: https://zx-pk.ru/archive/index.php/t-11295.html - Pentagon 128K ROM pages
void PortDecoder_Pentagon128::reset()
{
    // Pentagon ROM pages
    // 0 - Service <-- Set after reset
    // 1 - TR_DOS
    // 2 - SOS128
    // 3 - SOS48

    // Set default 120K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMPage(2);           // Pentagon 128K: ROM0 - service; ROM1 - TR-DOS; ROM2 - 128K; ROM3 - 48K
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);


    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Set default memory paging state: RAM bank: 0; Screen: Normal (bank 5); ROM bank: 0; Disable paging: No
    Port_7FFD_Out(0x7FFD, 0x00, 0x0000);
}

uint8_t PortDecoder_Pentagon128::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;
    uint16_t decodedPort = decodePort(port);

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortIn(port, pc);

    if (decodedPort != 0x0000)
    {
        switch (decodedPort)
        {
            case 0x00FE:
                // FE port must be passed as non-decoded since keyboard handler uses it
                result = Default_Port_FE_In(port, pc);
                break;
            case 0x7FFD:
                result = _context->emulatorState.p7FFD;
                break;
            default:
                // All ports registered with PortDecoder will be handled
                result = PeripheralPortIn(decodedPort);
                break;
        }
    }

    /// region <Debug logging>

    // Treat all FE ports as one for logging purposes
    if ((port & 0x00FE) == 0x00FE)
        port = 0x00FE;

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);
    }
    /// endregion </Debug logging>

    return result;
}

void PortDecoder_Pentagon128::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    uint16_t decodedPort = decodePort(port);

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortOut(port, value, pc);

    if (decodedPort != 0x0000)
    {
        switch (decodedPort)
        {
            case 0x00FE:
                Default_Port_FE_Out(decodedPort, value, pc);
                break;
            case 0x7FFD:
                Port_7FFD_Out(decodedPort, value, pc);
                break;
            default:
                // All ports registered with PortDecoder will be handled
                PeripheralPortOut(decodedPort, value);
                break;
        }
    }

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, decodedPort))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[Out] [PC:%04X%s] Port: %04X; Decoded port: %04X; Value: %02X", pc, currentMemoryPage.c_str(), port, decodedPort, value);
    }
    /// endregion </Debug logging>
}

/// Actualize port(s) state according selected RAM page
/// Note: Pentagon 128K has 8 RAM pages (16KiB each)
/// \param page Page number [0..7]
void PortDecoder_Pentagon128::SetRAMPage(uint8_t page)
{
    if (page > 7)
    {
        MLOGERROR("PortDecoder_Pentagon128::SetRAMPage - Invalid RAM page number %d", page);
        assert("Invalid RAM page");
    }

    uint8_t portValue = _state->p7FFD & 0b1111'1000;
    portValue = portValue | page;
    _state->p7FFD = page;
}

void PortDecoder_Pentagon128::SetROMPage(uint8_t page)
{
    if (page > 3)
    {
        MLOGERROR("PortDecoder_Pentagon128::SetROMPage - Invalid ROM page number %d", page);
        assert("Invalid ROM page");
    }

    _memory->SetROMPage(page, false);
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Pentagon128::IsPort_FE(uint16_t port)
{
    //    Pentagon 128K
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

bool PortDecoder_Pentagon128::IsPort_7FFD(uint16_t port)
{
    //    Pentagon 128K
    //    Port: #7FFD
    //    Match pattern: 0xxxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the Pentagon 128K are controlled to by writes to port 0x7ffd.
    //    As normal on Spectrum-clones hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1000'0000'0000'0010;
    static const uint16_t port_7FFD_match   = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Pentagon128::IsPort_BFFD(uint16_t port)
{
    //    Pentagon 128K
    //    Port: #BFFD
    //    Match pattern: 10xxxxxx xxxxxx0x
    //    Full pattern:  10111111 11111101
    //    AY music co-processor data register
    static const uint16_t port_BFFD_full    = 0b1011'1111'1111'1101;
    static const uint16_t port_BFFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_BFFD_match   = 0b1000'0000'0000'0000;

    // Compile-time check
    static_assert((port_BFFD_full & port_BFFD_mask) == port_BFFD_match && "Mask pattern incorrect");

    bool result = (port & port_BFFD_mask) == port_BFFD_match;

    return result;
}

bool PortDecoder_Pentagon128::IsPort_FFFD(uint16_t port)
{
    //    Pentagon 128K
    //    Port: #BFFD
    //    Match pattern: 11xxxxxx xxxxxx0x
    //    Full pattern:  11111111 11111101
    //    AY music co-processor control register
    static const uint16_t port_FFFD_full    = 0b1111'1111'1111'1101;
    static const uint16_t port_FFFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_FFFD_match   = 0b1100'0000'0000'0000;

    // Compile-time check
    static_assert((port_FFFD_full & port_FFFD_mask) == port_FFFD_match && "Mask pattern incorrect");

    bool result = (port & port_FFFD_mask) == port_FFFD_match;

    return result;
}

uint16_t PortDecoder_Pentagon128::decodePort(uint16_t port)
{
    uint16_t result = 0x0000;

    static constexpr PortMatch const portMasksMatches[] =
    {
        // ------- Mask ------ , ------- Match ------ . - Port -
        { 0b1100'0000'0000'0010, 0b1100'0000'0000'0000, 0xFFFD },   // AY #FFFD         Match value: (0xF000)
        { 0b1100'0000'0000'0010, 0b1000'0000'0000'0000, 0xBFFD },   // AY #BFFD         Match value: (0x8000)
        { 0b1000'0000'0000'0010, 0b0000'0000'0000'0000, 0x7FFD },   // Mem #7FFD        Match value: (0x0000)
        { 0b0000'0000'0000'0001, 0b0000'0000'0000'0000, 0x00FE },   // Sys $00FE        Match value: (0x0000)

        { 0b0000'0000'1000'0011, 0b0000'0000'1000'0011, 0x00FF },   // Beta128 #00FF    Match value: (131, 0x0083)
        //{ 0b0000'0000'1001'1111, 0b0000'0000'0000'0011, 0x001F },   // Beta128 #001F    Match value: (131, 0x0083)
        //{ 0b0000'0000'1001'1111, 0b0000'0000'0000'0011, 0x003F },   // Beta128 #003F    Match value: (131, 0x0083)
        //{ 0b0000'0000'1001'1111, 0b0000'0000'0000'0011, 0x005F },   // Beta128 #005F    Match value: (131, 0x0083)
        //{ 0b0000'0000'1001'1111, 0b0000'0000'0000'0011, 0x007F },   // Beta128 #007F    Match value: (131, 0x0083)
    };
    static constexpr size_t const elements = sizeof(portMasksMatches) / sizeof(PortMatch);

    /// region <Full resolving>
    bool matchResult;
    for (size_t i = 0; i < elements; i++)
    {
        const PortMatch& item = portMasksMatches[i];
        matchResult = (port & item.mask) == item.match;
        if (matchResult)
        {
            result = item.resolvedPort;
            break;
        }
    }
    /// endregion </Full resolving>

    if (result == 0x00FF)
    {
    }

    if (result == 0x0000)
    {
        // Simplified resolving for BDI ports 1F, 3F, 5F, 7F
        static constexpr const uint8_t portsMask  = 0b1000'0011;    // 0x83 (131)
        static constexpr const uint8_t portsMatch = 0b0000'0011;    // 0x03 (3)
        if ((port & portsMask) == portsMatch)
        {
            // result = (port & 0x60) | 0x1F;
            result = (port & 0b0110'0000) | 0b0001'1111;
        }
    }

    return result;
}

/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Pentagon128::Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    EmulatorState& state = _context->emulatorState;
    Memory& memory = *_context->pMemory;

    uint8_t screenNumber = (value & 0b0000'1000) >> 3;  // Bit 3: 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    uint8_t romPage = (value & 0b0001'0000) >> 4;       // Bit 4: 0 = 128K, 1 = 48K
    bool isPagingDisabled = value & 0b0010'0000;        // Bit 5: 0 = none, 1 = blocked

    // Disabling latch is kept until reset
    if (!_7FFD_Locked)
    {
        romPage |= 0b0000'0010; // Pentagon has 128K/48K ROMs in pages 2 and 3, not 0 and 1. So we need make correction (add 2 or set bit 1)

        switchRAMPage(value);   // Separate virtual method is used to unify 128k and 512k behavior
        memory.SetROMPage(romPage);

        _7FFD_Locked = isPagingDisabled;
    }
    else
    {
        // Set 48k ROM and lock RAM page0 at 0xC000...0xFFFF address window
        switchRAMPage(0);
        memory.SetROMPage(3);
    }

    // Detect if screen switch requested. Do not switch screen if state not changed
    uint8_t prevScreenNumber = (_state->p7FFD & 0b00001000) >> 3;
    if (prevScreenNumber != screenNumber && _screen != nullptr)
    {
        SpectrumScreenEnum screen = screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL;
        _screen->SetActiveScreen(screen);
    }
    else if (_screen == nullptr)
    {
        MLOGWARNING("Port_7FFD_Out: Screen pointer is null, cannot switch screen");
    }

    // Cache out port value in state
    state.p7FFD = value;

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x7FFD, port, value, pc, Dump_7FFD_value(value).c_str()));
    }

    /// endregion </Debug logging>
}

/// Pentagon 128k RAM page switching. Uses only 8 pages [0..7] via port #7FFD bits [0..2]
void PortDecoder_Pentagon128::switchRAMPage(uint8_t value)
{
    Memory& memory = *_context->pMemory;

    uint8_t bankRAM = value & 0b0000'0111;              // Bits [0:2]
    memory.SetRAMPageToBank3(bankRAM);
}

/// endregion </Helper methods>

/// region <Debug information>

std::string PortDecoder_Pentagon128::Dump_7FFD_value(uint8_t value)
{
    uint8_t bankRAM = value & 0b0000'0111;
    uint8_t screenNumber = (value & 0b0000'1000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    uint8_t romPage = (value & 0b0001'0000) >> 4;
    bool isPagingDisabled = value & 0b0010'0000;

    std::string result = StringHelper::Format("RAM page3 (C000-FFFF) bank: %d; Screen: %d; ROM: %d; #7FFD lock: %d", bankRAM, screenNumber, romPage, isPagingDisabled);

    return result;
}

std::string PortDecoder_Pentagon128::Dump_BFFD_value(uint8_t value)
{
    // See: http://cpctech.cpc-live.com/docs/ay38912/psgspec.htm
    // See: http://f.rdw.se/AY-3-8910-datasheet.pdf - Incorrect command register enumeration here
    static const char* AYCommandData[] =
    {
        "[Data] R0 - Channel A - fine tune (8-bit)",        // R0
        "[Data] R1 - Channel A - coarse tune (4-bit)",      // R1
        "[Data] R2 - Channel B - fine tune (8-bit)",        // R2
        "[Data] R3 - Channel B - coarse tune (4-bit)",      // R3
        "[Data] R4 - Channel C - fine tune (8-bit)",        // R4
        "[Data] R5 - Channel C - coarse tune (4-bit)",      // R5
        "[Data] R6 - Noise period (5-bit)",                 // R6
        "[Data] R7 - Mixer control Enable",                 // R7
        "[Data] R8 - Channel A - Amplitude (5-bit)",        // R8
        "[Data] R9 - Channel B - Amplitude (5-bit)",        // R9
        "[Data] R10 - Channel C - Amplitude (5-bit)",       // R10
        "[Data] R11 - Envelope period - fine (8-bit)",      // R11
        "[Data] R12 - Envelope period - coarse (8-bit)",    // R12
        "[Data] R13 - Envelope shape/cycle",                // R13
        "[Data] R14 - I/O Port A data store (8-bit)",       // R14
        "[Data] R15 - I/O Port B data store (8-bit)"        // R15
    };

    std::string result;

    uint8_t ayRegister = _state->pFFFD;     // Most recent access to port #FFFD contains selected AY register
    std::string dataString;
    std::string valueString;

    if (ayRegister <= 15)
    {
        dataString = AYCommandData[ayRegister];

        switch (ayRegister)
        {
            case 0: // Channel A - fine tune (8-bit)
                valueString = StringHelper::Format("0x%02X", value);
                break;
            case 1: // Channel A - coarse tune (4-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0000'1111);
                break;
            case 2: // Channel B - fine tune (8-bit)
                valueString = StringHelper::Format("0x%02X", value);
                break;
            case 3: // Channel B - coarse tune (4-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0000'1111);
                break;
            case 4: // Channel C - fine tune (8-bit)
                valueString = StringHelper::Format("0x%02X", value);
                break;
            case 5: // Channel C - coarse tune (4-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0000'1111);
                break;
            case 6: // Noise period (5-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0001'1111);
                break;
            case 7: // Enable control (Tone, Noise, IN/OUT)
            {
                uint8_t toneValue = value & 0b0000'0111;
                bool toneA = toneValue & 0b0000'0001;
                bool toneB = toneValue & 0b0000'0010;
                bool toneC = toneValue & 0b0000'0100;

                uint8_t noiseValue = (value & 0b0011'1000) >> 3;
                bool noiseA = noiseValue & 0b0000'0001;
                bool noiseB = noiseValue & 0b0000'0010;
                bool noiseC = noiseValue & 0b0000'0100;

                uint8_t inoutValue = (value & 0b1100'0000) >> 6;
                bool ioA = inoutValue & 0b0000'0001;
                bool ioB = inoutValue & 0b0000'0010;

                std::string toneString = StringHelper::Format("Tone A: %s; Tone B: %s, Tone C: %s", toneA ? "off" : "on", toneB ? "off" : "on", toneC ? "off" : "on");
                std::string noiseString = StringHelper::Format("Noise A: %s; Noise B: %s, Noise C: %s", noiseA ? "off" : "on", noiseB ? "off" : "on", noiseC ? "off" : "on");
                std::string inoutString = StringHelper::Format("IO A: %s, IO B: %s", ioA ? "out" : "in", ioB ? "out" : "in");

                valueString = StringHelper::Format("[%s] [%s] [%s]", toneString.c_str(), noiseString.c_str(), inoutString.c_str());
                break;
            }
            case 8: // Channel A - Amplitude (5-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0001'1111);
                break;
            case 9: // Channel B - Amplitude (5-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0001'1111);
                break;
            case 10:// Channel C - Amplitude (5-bit)
                valueString = StringHelper::Format("0x%02X", value & 0b0001'1111);
                break;
            case 11:// Envelope period - fine (8-bit)
                break;
            case 12:// Envelope period - coarse (8-bit)
                break;
            case 13:// Envelope shape/cycle
            {
                uint8_t holdValue = value & 0b0000'0001;
                uint8_t altValue = (value & 0b0000'0010) >> 1;
                uint8_t attValue = (value & 0b0000'0100) >> 2;
                uint8_t contValue = (value & 0b0000'1000) >> 3;

                valueString = StringHelper::Format("Hold: 0x%02X; Alt:0x%02X; Att: 0x%02X; Cont: 0x%02X", holdValue, altValue, attValue, contValue);

                break;
            }
            case 14://I/O Port A data store (8-bit)
                break;
            case 15://I/O Port B data store (8-bit)
                break;
        };
    }
    else if (ayRegister == 0xFE || ayRegister == 0xFF) // Non-standard Turbo-Sound card use
    {
        switch (ayRegister)
        {
            case 0xFE:   // Select AY1 chip
                break;
            case 0xFF:   // Select AY0 chip
                break;
        };
    }

    if (valueString.length() > 0)
        result = StringHelper::Format("%s: %s", dataString.c_str(), valueString.c_str());
    else
        result = dataString;

    return result;
}

std::string PortDecoder_Pentagon128::Dump_FFFD_value(uint8_t value)
{
    static const char* AYRegisterNames[] =
    {
        "[Reg]  R0 - Channel A - fine tune",        // R0
        "[Reg]  R1 - Channel A - coarse tune",      // R1
        "[Reg]  R2 - Channel B - fine tune",        // R2
        "[Reg]  R3 - Channel B - coarse tune",      // R3
        "[Reg]  R4 - Channel C - fine tune",        // R4
        "[Reg]  R5 - Channel C - coarse tune",      // R5
        "[Reg]  R6 - Noise period",                 // R6
        "[Reg]  R7 - Mixer Control Enable",         // R7
        "[Reg]  R8 - Channel A - Amplitude",        // R8
        "[Reg]  R9 - Channel B - Amplitude",        // R9
        "[Reg]  R10 - Channel C - Amplitude",       // R10
        "[Reg]  R11 - Envelope period - fine",      // R11
        "[Reg]  R12 - Envelope period - coarse",    // R12
        "[Reg]  R13 - Envelope shape",              // R13
        "[Reg]  R14 - I/O Port A data store",       // R14
        "[Reg]  R15 - I/O Port B data store"        // R15
    };

    std::string result;

    if (value <= 15) // Documented AY-8910/8912 command registers
    {
        result = AYRegisterNames[value];
    }
    else if (value == 0xFE || value == 0xFF) // Non-standard Turbo-Sound card use
    {
        if (value == 0xFE)
            result = "TurboSound AY1 chip select";
        if (value == 0xFF)
            result = "TurboSound AY0 chip select";
    }
    else
    {
        result = StringHelper::Format("Invalid AY control register: %d", value);
    }

    return result;
}

/// endregion </Debug information>