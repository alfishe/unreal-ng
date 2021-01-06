#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_spectrum3.h"

#include "common/collectionhelper.h"

/// region <Constructors / Destructors>

PortDecoder_Spectrum3::PortDecoder_Spectrum3(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Spectrum3::~PortDecoder_Spectrum3()
{
    LOGDEBUG("PortDecoder_Spectrum3::~PortDecoder_Spectrum3()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum3::Reset()
{
    // Reset memory paging lock latch
    _7FFD_Locked = false;
}

uint8_t PortDecoder_Spectrum3::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;

    /// region <Debug logging>

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

void PortDecoder_Spectrum3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //    ZX Spectrum 128 +2A/+2B/+3
    //    port: #7FFD
    //    port: #1FFD

    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(value, pc);
    }

    bool isPort_1FFD = IsPort_1FFD(port);
    if (isPort_1FFD)
    {
        Port_1FFD(value, pc);
    }

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
    }
    /// endregion </Debug logging>
}

void PortDecoder_Spectrum3::SetRAMPage(uint8_t page)
{

}

void PortDecoder_Spectrum3::SetROMPage(uint8_t page)
{

}

/// endregion </Interface methods>

/// region <Helper methods>
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

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}
/// endregion <Helper methods>


/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Spectrum3::Port_7FFD(uint8_t value, uint16_t pc)
{
    static const uint16_t port = 0x7FFD;
    Memory& memory = *_context->pMemory;
    Screen& screen = *_context->pScreen;

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

    screen.SetActiveScreen(screenNumber);


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
void PortDecoder_Spectrum3::Port_1FFD(uint8_t value, uint16_t pc)
{

}
