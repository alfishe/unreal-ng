#include "stdafx.h"
#include "portdecoder_atm3.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/nvram.h"
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

    // ATM3 (ZX-Evo BaseConf) always has the DS12885-style RTC/CMOS
    // (original Unreal Speccy gates it on conf.cmos, but a real ZX-Evo has it)
    _cmos.SetCMOSType(Dallas);
}

uint8_t PortDecoder_ATM3::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // Z-Controller SD interface, data port (any port with low byte 0x57 -
    // original io.cpp checks `conf.zc && (port & 0xFF) == 0x57` before the
    // MM_ATM3 block). The ZX-Evo board always carries the ZC; without an SD
    // image the reference TSdCard::Rd() returns 0xFF ("no card") so the EVO-DOS
    // SD-boot detection fails cleanly instead of reading floating-bus garbage.
    // Full SD image emulation is future work.
    if ((port & 0x00FF) == 0x0057)
    {
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc);
        return result;
    }

    // Port #xBF - read pBF back (original io.cpp: return comp.pBF).
    // The BaseConf service ROM does IN A,(BF) / OR 1 / OUT (BF),A to open the
    // DOS-port gate before any paging - if this read returns 0xFF instead of
    // pBF, the ROM latches spurious bits (e.g. D3) into pBF.
    if (IsPort_BF(port))
    {
        result = _state->pBF;
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc);
        return result;
    }

    // Port #xBE - ATM3 status / window readback (original io.cpp in(), MM_ATM3)
    if (IsPort_BE(port))
    {
        uint8_t portHi = (port >> 8) & 0xFF;
        result = Port_BE_In(portHi);
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc);
        return result;
    }

    // CMOS data port (0xBFF7 with shaden off, 0xBEF7 with shaden on)
    if (IsPort_CMOS_Data(port))
    {
        result = _cmos.ReadCMOS();
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc);
        return result;
    }

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
    // Z-Controller SD data writes: without an SD image the reference
    // TSdCard::Wr() ignores them - swallow so they do not fall through to the
    // FDC / floating-bus handlers (original io.cpp returns right after Zc.Wr)
    if ((port & 0x00FF) == 0x0057)
    {
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Port #xBF - ATM3 control (any port with low byte 0xBF, original io.cpp).
    // Handled unconditionally, before everything else - must not reach the
    // base class (BDI decode) or the memory manager.
    if (IsPort_BF(port))
    {
        Port_BF_Out(port, value, pc);
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Port #xBE - NMI exit counter (any port with low byte 0xBE)
    if (IsPort_BE(port))
    {
        Port_BE_Out(port, value, pc);
        OnPortOutComplete(port, value, pc);
        return;
    }

    // Memory manager ports (x7F7 / xx77 / xFF7) only respond while the
    // DOS-port gate is open. In the original (io.cpp / memory.cpp set_banks)
    // CF_DOSPORTS is raised by pBF.0 (shaden) OR by CF_TRDOS, which for
    // ATM3/ATM710 is forced while ~cpm=0 (aFF77 bit 9 clear). At reset
    // aFF77=0, so the manager is OPEN from the start - the BaseConf service
    // ROM relies on this: OUT (BC77),02 / OUT (FF77),AB and the initial
    // window writes all happen BEFORE the shaden bit is set via IN/OUT (BF).
    //
    // While the gate is open the CMOS shifts off the xFF7 window addresses:
    //   0xDFF7 = CMOS address, 0xBFF7 = CMOS data (gate closed)
    //   0xDEF7 = CMOS address, 0xBEF7 = CMOS data (gate open;
    //            0xBFF7/0xDFF7 then decode as xFF7 windows 2/3)
    if (IsManagerEnabled())
    {
        // Port #37F7 - 4MB memory manager (ATM3 specific)
        if (IsPort_37F7(port))
        {
            Port_37F7_Out(port, value, pc);
            OnPortOutComplete(port, value, pc);
            return;
        }

        // Port #xx77 - ATM control register (any port with low byte 0x77;
        // the BaseConf service ROM enables the manager via port 0xBC77)
        if (IsPort_FF77(port))
        {
            Port_FF77_Out_ATM3(port, value, pc);
            OnPortOutComplete(port, value, pc);
            return;
        }

        // Port #xFF7 - memory manager window registers
        uint8_t windowIndex = 0;
        if (IsPort_FFF7(port, windowIndex))
        {
            Port_FFF7_Out(port, value, windowIndex, pc);
            OnPortOutComplete(port, value, pc);
            return;
        }

        // CMOS address / data (shaden on: DEF7 / BEF7)
        if (IsPort_CMOS_Address(port))
        {
            _cmos.SetCMOSAddress(value);
            OnPortOutComplete(port, value, pc);
            return;
        }
        if (IsPort_CMOS_Data(port))
        {
            _cmos.WriteCMOS(value);
            OnPortOutComplete(port, value, pc);
            return;
        }
    }
    else
    {
        // Manager gate closed - swallow x7F7 / xx77 / xFF7 writes so they do not
        // fall through to the base ATM710 handlers (original ignores them too)
        uint8_t windowIndex = 0;
        if (IsPort_37F7(port) || IsPort_FF77(port) || IsPort_FFF7(port, windowIndex))
        {
            MLOGDEBUG("PortDecoder_ATM3: manager write to 0x%04X ignored (gate closed)", port);
            OnPortOutComplete(port, value, pc);
            return;
        }

        // CMOS address / data (shaden off: DFF7 / BFF7)
        if (IsPort_CMOS_Address(port))
        {
            _cmos.SetCMOSAddress(value);
            OnPortOutComplete(port, value, pc);
            return;
        }
        if (IsPort_CMOS_Data(port))
        {
            _cmos.WriteCMOS(value);
            OnPortOutComplete(port, value, pc);
            return;
        }
    }

    // Delegate to base class for other ports (FE, 7FFD, EFF7, BFFD, FFFD, Beta128...)
    PortDecoder_ATM710::DecodePortOut(port, value, pc);
}

/// endregion </Interface methods>

/// region <Port detection>

bool PortDecoder_ATM3::IsManagerEnabled()
{
    // CF_DOSPORTS equivalent: shaden (pBF.0) OR CF_TRDOS. For ATM3/ATM710
    // set_banks() forces CF_TRDOS while ~cpm=0 (aFF77 bit 9 clear), and
    // CF_TRDOS itself feeds CF_DOSPORTS. Xpeccy models the same rule as
    // `bdiz = 1` when `!(prt2 & 0x80)` (prt2.7 = aFF77 bit 14 latched by xx77)
    // or evoBF & 1.
    return (_state->pBF & 0x01) != 0 ||
           (_state->aFF77 & ATM_AFF77_CPM) == 0 ||
           (_state->flags & CF_TRDOS) != 0;
}

bool PortDecoder_ATM3::IsPort_FF77(uint16_t port)
{
    // ATM3: Partial decode - any port with low byte 0x77 (original io.cpp: `p1 == 0x77`).
    // The BaseConf service ROM enables the memory manager via port 0xBC77,
    // which the previous 0x0FFF/0x0F77 mask missed.
    return (port & 0x00FF) == 0x0077;
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

bool PortDecoder_ATM3::IsPort_BE(uint16_t port)
{
    // Port #xBE - ATM3 status / window readback
    return (port & 0x00FF) == 0x00BE;
}

bool PortDecoder_ATM3::IsPort_CMOS_Data(uint16_t port)
{
    // CMOS data: 0xBFF7 with the manager gate closed, 0xBEF7 with it open
    // (original io.cpp: `port == (0xBFF7 & mask)` with mask = ~0x100 when DOS ports on)
    return port == (IsManagerEnabled() ? 0xBEF7 : 0xBFF7);
}

bool PortDecoder_ATM3::IsPort_CMOS_Address(uint16_t port)
{
    // CMOS address: 0xDFF7 with the manager gate closed, 0xDEF7 with it open
    return port == (IsManagerEnabled() ? 0xDEF7 : 0xDFF7);
}

bool PortDecoder_ATM3::IsPort_BF(uint16_t port)
{
    // Port #xBF - ATM3 control (shaden)
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

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

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

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_37F7_Out: idx=%d value=0x%02X fullValue=0x%04X", idx, value, fullValue);
}

void PortDecoder_ATM3::Port_BF_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // Bit 3: 1->0 edge requests NMI (original io.cpp). NMI serving is not
    // wired in this core yet (see Z80::ProcessInterrupts), so only pBF is
    // latched here.
    _state->pBF = value;

    // Bit 0: shaden (shadow DOS ports mode) - gates the memory manager and
    // CMOS address decode in DecodePortOut/DecodePortIn.
    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_BF_Out: value=0x%02X shaden=%d", value, value & 1);
}

void PortDecoder_ATM3::Port_BE_Out([[maybe_unused]] uint16_t port, [[maybe_unused]] uint8_t value,
                                   [[maybe_unused]] uint16_t pc)
{
    // NMI exit counter - every write resets it to 2 (original io.cpp)
    _state->pBE = 2;

    MLOGDEBUG("Port_BE_Out: pBE=2");
}

uint8_t PortDecoder_ATM3::Port_BE_In(uint8_t portHi)
{
    // Port #xBE readback, selected by A15..A8 (original io.cpp in(), MM_ATM3)
    if ((portHi & ~7) == 0)
    {
        // Non-inverted RAM page number of window register 0-7
        return (_state->pFFF7[portHi & 7] & 0xFF) ^ 0xFF;
    }

    switch (portHi)
    {
        case 0x08:  // ram/rom flags: bit i = window i bit 8, inverted
        {
            uint8_t ramRomMask = 0;
            for (unsigned i = 0; i < 8; i++)
                ramRomMask |= ((_state->pFFF7[i] >> 8) & 1) << i;
            return ~ramRomMask;
        }
        case 0x09:  // dos7ffd: bit i = window i bit 9, inverted
        {
            uint8_t dosMask = 0;
            for (unsigned i = 0; i < 8; i++)
                dosMask |= ((_state->pFFF7[i] >> 9) & 1) << i;
            return ~dosMask;
        }
        case 0x0A:  // p7FFD
            return _state->p7FFD;
        case 0x0C:  // FF77 state: aFF77 bits 14/9/8 + pFF77 low nibble
            return ((_state->aFF77 >> 14) << 7) | ((_state->aFF77 >> 9) << 6) |
                   ((_state->aFF77 >> 8) << 5) | (_state->pFF77 & 0xF);
        default:    // 0x0B, 0x0D (palette) and unmapped - open bus
            return 0xFF;
    }
}

// updateMemoryBanks(): fully inherited from PortDecoder_ATM710 - the base class
// port of the original set_banks() MM_ATM710/MM_ATM3 branch already covers the
// ATM3 specifics (register masks and the NMI -> RAM page 0xFF window 0 override)

/// endregion </Port handlers>
