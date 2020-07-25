#include "stdafx.h"

#include "common/logger.h"

#include "portdecoder_pentagon128.h"
#include "cassert"

/// region <Constructors / Destructors>

PortDecoder_Pentagon128::PortDecoder_Pentagon128(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Pentagon128::~PortDecoder_Pentagon128()
{
    LOGDEBUG("PortDecoder_Pentagon128::~PortDecoder_Pentagon128()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

// See: https://zx-pk.ru/archive/index.php/t-11295.html - Pentagon 128K ROM pages
void PortDecoder_Pentagon128::Reset()
{
    // Pentagon ROM pages
    // 0 - Service <-- Set after reset
    // 1 - TR_DOS
    // 2 - SOS128
    // 3 - SOS48

    // Set default 120K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMPage(0);
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);


    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Set default memory paging state: RAM bank: 0; Screen: Normal (bank 5); ROM bank: 0; Disable paging: No
    Port_7FFD(0x7FFD, 0x00, 0x0000);
}

uint8_t PortDecoder_Pentagon128::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Pentagon128::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    //    Pentagon 128K
    //    port: #7FFD
    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(port, value, pc);
    }
}

/// Actualize port(s) state according selected RAM page
/// Note: Pentagon 128K has 8 RAM pages (16KiB each)
/// \param page Page number [0..7]
void PortDecoder_Pentagon128::SetRAMPage(uint8_t page)
{
    if (page > 7)
    {
        LOGERROR("PortDecoder_Pentagon128::SetRAMPage - Invalid RAM page number %d", page);
        assert("Invalid RAM page");
    }

    _state->p7FFD = _state->p7FFD & 0b1111'1100 ;
}

void PortDecoder_Pentagon128::SetROMPage(uint8_t page)
{
    if (page > 3)
    {
        LOGERROR("PortDecoder_Pentagon128::SetROMPage - Invalid ROM page number %d", page);
        assert("Invalid ROM page");
    }
}

/// endregion </Interface methods>

/// region <Helper methods>
bool PortDecoder_Pentagon128::IsPort_7FFD(uint16_t port)
{
    //    Pentagon 128K
    //    port: #7FFD
    //    Match pattern: 0xxxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the 128K/+2 are controlled to by writes to port 0x7ffd.
    //    As normal on Sinclair hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    static const uint16_t port_7FFD_full    = 0b0111111111111101;
    static const uint16_t port_7FFD_mask    = 0b1000000000000010;
    static const uint16_t port_7FFD_match   = 0b0000000000000000;

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Pentagon128::Port_7FFD(uint16_t port, uint8_t value, uint16_t pc)
{
    static State& state = _context->state;
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

    LOGDEBUG(DumpPortValue(0x7FFD, port, value, pc));
    LOGDEBUG(memory.DumpMemoryBankInfo());
}
