#include "stdafx.h"

#include "common/logger.h"

#include "portdecoder_spectrum128.h"

#include "common/stringhelper.h"
#include <map>
#include <vector>

/// region <Constructors / Destructors>

PortDecoder_Spectrum128::PortDecoder_Spectrum128(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Spectrum128::~PortDecoder_Spectrum128()
{
    LOGDEBUG("PortDecoder_Spectrum128::~PortDecoder_Spectrum128()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum128::Reset()
{
    // ZX-Spectrum 128K ROM pages
    // 0 - SOS128 <-- Set after reset
    // 1 - SOS48

    // Set default 120K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMPage(0);
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);

    // Set default border color to white
    _screen->SetBorderColor(COLOR_WHITE);

    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Set default memory paging state: RAM bank: 0; Screen: Normal (bank 5); ROM bank: 0; Disable paging: No
    Port_7FFD(0x7FFD, 0x00, 0x0000);
}

uint8_t PortDecoder_Spectrum128::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Spectrum128::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    //    ZX Spectrum 128 / +2
    //    port: #7FFD
    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(port, value, pc);
    }
    else if (IsPort_BFFD(port))
    {
        Port_BFFD(port, value, pc);
    }
    else if (IsPort_FFFD(port))
    {
        Port_FFFD(port, value, pc);
    }
    else if (IsPort_FE(port))
    {
        Port_FE(port, value, pc);
    }
    else
    {
        LOGWARNING("[Out] [PC:%04X] Port: %02X; Value: %02X", pc, port, value);
    }
}

void PortDecoder_Spectrum128::SetRAMPage(uint8_t page)
{

}

void PortDecoder_Spectrum128::SetROMPage(uint8_t page)
{

}


/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Spectrum128::IsPort_FE(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #FE
    //    Match pattern: xxxxxxxx xxxxxxx0
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match     = 0b0000'0000'0000'0000;

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

bool PortDecoder_Spectrum128::IsPort_7FFD(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #7FFD
    //    Match pattern: 0xxxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the 128K/+2 are controlled to by writes to port 0x7ffd.
    //    As normal on Sinclair hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1000'0000'0000'0010;
    static const uint16_t port_7FFD_match   = 0b0000'0000'0000'0000;

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Spectrum128::IsPort_BFFD(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #BFFD
    //    Match pattern: 10xxxxxx xxxxxx0x
    //    Full pattern:  10111111 11111101
    //    AY music co-processor data register
    static const uint16_t port_BFFD_full    = 0b1011'1111'1111'1101;
    static const uint16_t port_BFFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_BFFD_match   = 0b1000'0000'0000'0000;

    bool result = (port & port_BFFD_mask) == port_BFFD_match;

    return result;
}

bool PortDecoder_Spectrum128::IsPort_FFFD(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #BFFD
    //    Match pattern: 11xxxxxx xxxxxx0x
    //    Full pattern:  11111111 11111101
    //    AY music co-processor control register
    static const uint16_t port_FFFD_full    = 0b1111'1111'1111'1101;
    static const uint16_t port_FFFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_FFFD_match   = 0b1100'0000'0000'0000;

    bool result = (port & port_FFFD_mask) == port_FFFD_match;

    return result;
}

/// endregion <Helper methods>

/// Port #FE (Border, Beeper)
/// \param port
/// \param value
/// \param pc
void PortDecoder_Spectrum128::Port_FE(uint16_t port, uint8_t value, uint16_t pc)
{
    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;

    _screen->SetBorderColor(borderColor);

    LOGDEBUG(DumpPortValue(0xFE, port, value, pc, Dump_FE_value(value).c_str()));
}

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Spectrum128::Port_7FFD(uint16_t port, uint8_t value, uint16_t pc)
{
    static Memory& memory = *_context->pMemory;

    uint8_t bankRAM = value & 0b00000111;
    uint8_t screenNumber = (value & 0b00001000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    bool isROM0 = value & 0b00010000;
    bool isPagingDisabled = value & 0b00100000;

    // Disabling latch is kept until reset
    if (!_7FFD_Locked)
    {
        memory.SetRAMPageToBank3(bankRAM);
        memory.SetROMMode(isROM0 ? RM_128 : RM_SOS);

        _7FFD_Locked = isPagingDisabled;
    }

    _screen->SetActiveScreen(screenNumber);

    // Cache out port value in state
    _state->p7FFD = value;

    LOGDEBUG(DumpPortValue(0x7FFD, port, value, pc, Dump_7FFD_value(value).c_str()));
    LOGDEBUG(memory.DumpMemoryBankInfo());
}

void PortDecoder_Spectrum128::Port_BFFD(uint16_t port, uint8_t value, uint16_t pc)
{
    // See: http://f.rdw.se/AY-3-8910-datasheet.pdf - Seems AY control register enumeration is wrong here
    // See: http://cpctech.cpc-live.com/docs/ay38912/psgspec.htm
    // See: https://softspectrum48.weebly.com/notes/ay-emulation-part-2-about-the-ay-3-8912

    // Cache out port value in state
    _state->pBFFD = value;

    LOGDEBUG(DumpPortValue(0xBFFD, port, value, pc, Dump_BFFD_value(value).c_str()));
}

void PortDecoder_Spectrum128::Port_FFFD(uint16_t port, uint8_t value, uint16_t pc)
{
    // See: http://f.rdw.se/AY-3-8910-datasheet.pdf - Seems AY control register enumeration is wrong here
    // See: http://cpctech.cpc-live.com/docs/ay38912/psgspec.htm
    // See: https://softspectrum48.weebly.com/notes/ay-emulation-part-2-about-the-ay-3-8912

    // Cache out port value in state
    _state->pFFFD = value;

    LOGDEBUG(DumpPortValue(0xFFFD, port, value, pc, Dump_FFFD_value(value).c_str()));
}

std::string PortDecoder_Spectrum128::Dump_FE_value(uint8_t value)
{
    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;
    std::string colorText = Screen::GetColorName(borderColor);

    std::string result = StringHelper::Format("Border color: %d (%s); Beeper: %d", borderColor, colorText.c_str(), beeperBit);

    return result;
}

std::string PortDecoder_Spectrum128::Dump_7FFD_value(uint8_t value)
{
    uint8_t bankRAM = value & 0b00000111;
    uint8_t screenNumber = (value & 0b00001000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    bool isROM0 = value & 0b00010000;
    bool isPagingDisabled = value & 0b00100000;

    std::string result = StringHelper::Format("RAM bank3 page: %d; Screen: %d; ROM: %d; #7FFD lock: %d", bankRAM, screenNumber, isROM0, isPagingDisabled);

    return result;
}

std::string PortDecoder_Spectrum128::Dump_BFFD_value(uint8_t value)
{
    static const char* AYCommandData[] =
    {
        "[Data] Channel A - fine tune (8-bit)",    // R0
        "[Data] Channel A - coarse tune (4-bit)",  // R1
        "[Data] Channel B - fine tune (8-bit)",    // R2
        "[Data] Channel B - coarse tune (4-bit)",  // R3
        "[Data] Channel C - fine tune (8-bit)",    // R4
        "[Data] Channel C - coarse tune (4-bit)",  // R5
        "[Data] Noise period (5-bit)",             // R6
        "[Data] Tone enable flags",                // R7
        "[Data] Channel A - Amplitude (5-bit)",    // R8
        "[Data] Channel B - Amplitude (5-bit)",    // R9
        "[Data] Channel C - Amplitude (5-bit)",    // R10
        "[Data] Envelope period - fine (8-bit)",   // R11
        "[Data] Envelope period - coarse (8-bit)", // R12
        "[Data] Envelope shape/cycle",             // R13
        "[Data] I/O Port A data store (8-bit)",    // R14
        "[Data] I/O Port B data store (8-bit)"     // R15
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
            case 7: // Tone enable flags (Tone, Noise, IN/OUT)
            {
                uint8_t toneValue = value & 0b0000'0111;
                uint8_t noiseValue = (value & 0b0011'1000) >> 3;
                uint8_t inoutValue = (value & 0b1100'0000) >> 6;

                valueString = StringHelper::Format("Tone: 0x%02X; Noise:0x%02X; InOut: 0x%02X", toneValue, noiseValue, inoutValue);
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
            case 254:   // Select AY1 chip
                break;
            case 255:   // Select AY0 chip
                break;
        };
    }

    if (valueString.length() > 0)
        result = StringHelper::Format("%s: %s", dataString.c_str(), valueString.c_str());
    else
        result = dataString;

    return result;
}

std::string PortDecoder_Spectrum128::Dump_FFFD_value(uint8_t value)
{
    static const char* AYRegisterNames[] =
    {
        "[Reg] Channel A - fine tune",        // R0
        "[Reg] Channel A - coarse tune",      // R1
        "[Reg] Channel B - fine tune",        // R2
        "[Reg] Channel B - coarse tune",      // R3
        "[Reg] Channel C - fine tune",        // R4
        "[Reg] Channel C - coarse tune",      // R5
        "[Reg] Noise period",                 // R6
        "[Reg] Tone enable flags",            // R7
        "[Reg] Channel A - Amplitude",        // R8
        "[Reg] Channel B - Amplitude",        // R9
        "[Reg] Channel C - Amplitude",        // R10
        "[Reg] Envelope period - fine",       // R11
        "[Reg] Envelope period - coarse",     // R12
        "[Reg] Envelope shape",               // R13
        "[Reg] I/O Port A data store",        // R14
        "[Reg] I/O Port B data store"         // R15
    };

    std::string result;

    if (value <=15) // Documented AY-8910/8912 command registers
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