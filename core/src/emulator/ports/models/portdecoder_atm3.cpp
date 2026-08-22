#include "stdafx.h"
#include "portdecoder_atm3.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoder_ATM3::PortDecoder_ATM3(EmulatorContext* context) : PortDecoder_ATM710(context)
{
}

PortDecoder_ATM3::~PortDecoder_ATM3()
{
    MLOGDEBUG("PortDecoder_ATM3::~PortDecoder_ATM3()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_ATM3::reset()
{
    PortDecoder_ATM710::reset();

    // ATM3-specific reset
    _state->pBDl = 0x00;
    _state->pBDh = 0x00;
    _state->pBE = 0x00;
    _state->pBF = 0x00;
}

uint8_t PortDecoder_ATM3::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // ATM3-specific: EFF7 read returns actual register value
    if (IsPort_EFF7(port))
    {
        result = _state->pEFF7;
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc);
        return result;
    }

    // Delegate to base class for other ports
    return PortDecoder_ATM710::DecodePortIn(port, pc);
}

void PortDecoder_ATM3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    // Port #FF77 - ATM3 has partial decode (different from ATM710)
    if (IsPort_FF77(port))
    {
        Port_FF77_Out_ATM3(port, value, pc);
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Port #37F7 - 4MB memory manager (ATM3 specific)
    if (IsPort_37F7(port))
    {
        Port_37F7_Out(port, value, pc);
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Port #BF - ATM3 control
    if (IsPort_BF(port))
    {
        Port_BF_Out(port, value, pc);
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Delegate to base class for other ports
    PortDecoder_ATM710::DecodePortOut(port, value, pc);
}

/// endregion </Interface methods>

/// region <Port detection>

bool PortDecoder_ATM3::IsPort_FF77(uint16_t port)
{
    // ATM3: Partial decode - matches any x*F77
    // Mask 0x0FFF, match 0x0F77 (allow any high nibble)
    return (port & 0x0FFF) == 0x0F77;
}

bool PortDecoder_ATM3::IsPort_37F7(uint16_t port)
{
    // Port #37F7 - 4MB memory manager
    // Full 14-bit decode
    return (port & 0x3FFF) == 0x37F7;
}

bool PortDecoder_ATM3::IsPort_FFF7(uint16_t port, uint8_t& windowIndex)
{
    // ATM3 uses a narrower decode than ATM710: A13:A12 must be set as well
    // (matches original Unreal Speccy io.cpp: mask 0x3FFF, match 0x3FF7)
    if ((port & 0x3FFF) != 0x3FF7)
        return false;

    windowIndex = (port >> 14) & 0x03;
    return true;
}

bool PortDecoder_ATM3::IsPort_BF(uint16_t port)
{
    // Port #BF - ATM3 control (shaden)
    return (port & 0x00FF) == 0x00BF;
}

/// endregion </Port detection>

/// region <Port handlers>

void PortDecoder_ATM3::Port_FF77_Out_ATM3(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    uint8_t oldValue = _state->pFF77;

    // Check if memory swap bit changed
    if ((oldValue ^ value) & ATM_FF77_MEMSWAP)
    {
        atmMemSwap();
    }

    // Store value and full port address
    _state->pFF77 = value;
    _state->aFF77 = port;

    // ATM3: No INT gate - interrupts always pass
    // (Unlike ATM710 where bit 5 controls INT gate)

    // Update video mode if changed (mode = pFF77 & 7, same decode as
    // Screen::DetectModeATM3 / ATM710)
    if ((oldValue ^ value) & ATM_FF77_VMODE_MASK)
    {
        uint8_t oldMode = oldValue & ATM_FF77_VMODE_MASK;
        uint8_t newMode = value & ATM_FF77_VMODE_MASK;
        MLOGINFO("Port_FF77_Out(ATM3): Video mode changed from %d to %d (pFF77: 0x%02X -> 0x%02X)",
                 oldMode, newMode, oldValue, value);

        // Trigger video mode re-detection and framebuffer reallocation.
        // Without this the new mode only applies at the next frame boundary.
        if (_context->pScreen)
        {
            _context->pScreen->InitRaster();
            MLOGINFO("Port_FF77_Out(ATM3): InitRaster() called, new video mode: %d",
                     _context->pScreen->_vid.mode);
        }
        else
        {
            MLOGWARNING("Port_FF77_Out(ATM3): pScreen is NULL, cannot trigger InitRaster()");
        }
    }

    // Update turbo mode
    updateTurboMode();

    // Update memory banks
    updateMemoryBanks();

    MLOGDEBUG("Port_FF77_Out(ATM3): port=0x%04X value=0x%02X %s", port, value, Dump_FF77_value(value).c_str());
}

void PortDecoder_ATM3::Port_37F7_Out(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // 4MB memory manager, RAM pages only (matches original Unreal Speccy io.cpp):
    // window selected by A15:A14, register index combined with 7FFD.4.
    // Page byte is active-low; the type bits are preserved from the previous value
    // (bit 9 kept, bit 8 cleared - the port always selects RAM).
    unsigned idx = ((_state->p7FFD & 0x10) >> 2) | ((port >> 14) & 3);

    unsigned fullValue = (_state->pFFF7[idx] & ~0x1FFu) | (value ^ 0xFF);
    _state->pFFF7[idx] = fullValue;

    updateMemoryBanks();

    MLOGDEBUG("Port_37F7_Out: idx=%d value=0x%02X fullValue=0x%04X", idx, value, fullValue);
}

void PortDecoder_ATM3::Port_BF_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    _state->pBF = value;

    // Bit 0: shaden (shadow DOS ports mode)
    updateMemoryBanks();

    MLOGDEBUG("Port_BF_Out: value=0x%02X shaden=%d", value, value & 1);
}

// updateMemoryBanks(): fully inherited from PortDecoder_ATM710 - the base class
// port of the original set_banks() MM_ATM710/MM_ATM3 branch already covers the
// ATM3 specifics (register masks and the NMI -> RAM page 0xFF window 0 override)

/// endregion </Port handlers>
