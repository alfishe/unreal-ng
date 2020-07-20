#include "stdafx.h"

#include "common/logger.h"

#include "portdecoder_spectrum128.h"

#include "common/stringhelper.h"

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

    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Set default memory paging state: RAM bank: 0; Screen: Normal (bank 5); ROM bank: 0; Disable paging: No
    Port_7FFD(0x7FFD, 0x00);
}

uint8_t PortDecoder_Spectrum128::DecodePortIn(uint16_t addr)
{
    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Spectrum128::DecodePortOut(uint16_t addr, uint8_t value)
{
    //    ZX Spectrum 128 / +2
    //    port: #7FFD

    bool isPort_7FFD = IsPort_7FFD(addr);
    if (isPort_7FFD)
    {
        Port_7FFD(addr, value);
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
bool PortDecoder_Spectrum128::IsPort_7FFD(uint16_t addr)
{
    //    ZX Spectrum 128 / +2
    //    port: #7FFD
    //    Match pattern: 0xxxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the 128K/+2 are controlled to by writes to port 0x7ffd.
    //    As normal on Sinclair hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    //    Bits:
    //      D0 = RAM - bit0 ;128 kB memory
    //      D1 = RAM - bit1 ;128 kB memory
    //      D2 = RAM - bit2 ;128 kB memory
    //      D3 = Screen (Normal (Bank5) | Shadow (Bank 7))
    //      D4 = ROM (ROM0 = 128k ROM | ROM1 = 48k ROM)
    //      D5 = Disable memory paging (both ROM and RAM) until reset
    //      D6 = unused
    //      D7 = unused
    static const uint16_t port_7FFD_full_mask = 0b0111111111111101;
    static const uint16_t port_7FFD_mask_inv = 0b1000000000000010;

    bool result = !(addr & port_7FFD_mask_inv);

    return result;
}

/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Spectrum128::Port_7FFD(uint16_t port, uint8_t value)
{
    static COMPUTER& state = _context->state;
    static Memory& memory = *_context->pMemory;
    static Screen& screen = *_context->pScreen;

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

    // Cache out port value in state
    state.p7FFD = value;

    LOGDEBUG(DumpPortValue(0x7FFD, port, value, Dump_7FFD_value(value).c_str()));
    LOGDEBUG(memory.DumpMemoryBankInfo());
}

std::string PortDecoder_Spectrum128::Dump_7FFD_value(uint8_t value)
{
    uint8_t bankRAM = value & 0b00000111;
    uint8_t screenNumber = (value & 0b00001000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    bool isROM0 = value & 0b00010000;
    bool isPagingDisabled = value & 0b00100000;

    std::string result = StringHelper::Format("RAM bank2 page: %d; Screen: %d; ROM: %d; #7FFD lock: %d", bankRAM, screenNumber, isROM0, isPagingDisabled);

    return result;
}