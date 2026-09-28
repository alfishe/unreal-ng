#include "stdafx.h"
#include "portdecoder_atm3.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/atm/cmos.h"
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
    _state->evoFddMask = 0x00;  // fdd_mask resets to "all drives real" (zports.v:521-525)

    // ATM3 (ZX-Evo BaseConf) always has the DS12885-style RTC/CMOS
    // (original Unreal Speccy gates it on conf.cmos, but a real ZX-Evo has it)
    _cmos.SetCMOSType(Dallas);
}

/// @brief One BaseConf decode arm per I/O cycle
/// @details The rules are the FPGA's (fpga/base_trdemu/trunk/z80/zports.v:
///          porthit list :331-359, read mux :424-482, write strobes :484-545).
///          Every mainboard port decodes the full low byte; the shadow line
///          (TR-DOS active or #BF bit 0) swaps the FDC / ATM group in and the
///          joystick / Z-Controller config / EFF7 group out. Anything not a
///          mainboard port belongs to the ZX-Bus cards (GS, MoonSound, ...).
PortDecoder_ATM3::PortArm PortDecoder_ATM3::ClassifyPort(uint16_t port, bool isWrite)
{
    const uint8_t low = static_cast<uint8_t>(port & 0x00FF);
    const bool shadow = IsManagerEnabled();

    switch (low)
    {
        case 0xFE:
        case 0xF6:
            return PortArm::KeyboardBorder;
        case 0xFC:
            return PortArm::BorderAnd7FFD;
        case 0xFD:
            return (port & 0x8000) ? PortArm::Ay : PortArm::Paging7FFD;
        case 0xF7:
            return (shadow && (port & 0x0100)) ? PortArm::Pager : PortArm::Eff7Gluk;
        case 0x77:
            return shadow ? PortArm::Atm77 : PortArm::SdConfig;
        case 0x57:
            return PortArm::SdData;
        case 0x1F:
            return shadow ? PortArm::Fdc : PortArm::Joystick;
        case 0x3F:
        case 0x5F:
        case 0x7F:
        case 0xFF:
            return shadow ? PortArm::Fdc : PortArm::ZxBus;
        case 0xDF:
            return PortArm::Mouse;
        case 0xBF:
            return PortArm::EvoConfig;
        case 0xBE:
            return PortArm::EvoExit;
        case 0xBD:
            return PortArm::EvoReadback;
        case 0xEF:
            return PortArm::ComPort;
        case 0x3B:
            return PortArm::UlaPlus;
        case 0x11:
            return PortArm::NemoIde;
        case 0xFB:
            // The Covox DAC latch is write-only and not a porthit: reads stay on the ZX-Bus
            return isWrite ? PortArm::Covox : PortArm::ZxBus;
        default:
            break;
    }

    // NemoIDE task-file ports and their aliases: `IS_NIDE_REGS(x) = (x[2:0]==0) && (x[3]!=x[4])`
    // (#10, #30 ... #F0 and #08, #28 ... #E8; #C8 is the CS1 control register)
    if ((low & 0x07) == 0 && ((low >> 3) & 1) != ((low >> 4) & 1))
        return PortArm::NemoIde;

    return PortArm::ZxBus;
}

uint8_t PortDecoder_ATM3::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    const PortArm arm = ClassifyPort(port, /*isWrite*/ false);

    // Full-decode low-byte claim override (see portdecoder.h): a registered
    // ZX-Bus card (e.g. ZXM-MoonSound on #C4-#C7) owns the cycle. The FDC
    // arm passes its canonical low byte so the Beta-128 session arbitration
    // (R6) inside the override recognizes it
    PortDecodeDisposition disp;
    uint16_t decodedPort = (arm == PortArm::Fdc) ? static_cast<uint16_t>(port & 0x00FF) : port;
    if (OverrideDecodeForFullDecodeClaim(port, decodedPort, disp, /*isRead*/ true))
    {
        result = GetCachedFullDecodeInValue(port);
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc, disp);
        return result;
    }

    _lastPortDecoded = true;
    switch (arm)
    {
        case PortArm::KeyboardBorder:
            result = Default_Port_FE_In(port, pc);
            break;
        case PortArm::Ay:
            // #FFFD reads the selected AY register; #BFFD is write-only
            result = (port & 0x4000) ? PeripheralPortIn(PORT_FFFD) : 0xFF;
            break;
        case PortArm::Eff7Gluk:
        case PortArm::Pager:
            result = DecodeF7In(port);
            break;
        case PortArm::SdConfig:
            // Z-Controller config read: always "card inserted, writable" (zports.v:449-450).
            // Real presence / write-protect live in the AVR clock register C
            result = 0x00;
            break;
        case PortArm::SdData:
            // No SD card model yet: an idle SPI line reads #FF, so the ERS and
            // NedoOS card probes fail cleanly
            result = 0xFF;
            break;
        case PortArm::Fdc:
            result = PeripheralPortIn(static_cast<uint16_t>(port & 0x00FF));
            break;
        case PortArm::Joystick:
            // Kempston joystick outside shadow; no joystick model is attached, so
            // nothing is pressed (same stub as PortDecoder_Scorpion256)
            result = 0x00;
            break;
        case PortArm::Mouse:
        {
            // #xxDF: A8=0 buttons + wheel, A8=1 & A10=0 X, A10=1 Y (zkbdmus.v:118-120);
            // the AVR answers #FF with no mouse. Not DOS-gated on this board
            const uint8_t reg = (port & 0x0100) ? ((port & 0x0400) ? 2 : 1) : 0;
            result = (_mouse && _mouse->IsPresent()) ? _mouse->ReadRegister(reg) : 0xFF;
            break;
        }
        case PortArm::EvoConfig:
            // The BaseConf service ROM does IN A,(BF) / OR 1 / OUT (BF),A to open
            // the shadow ports - the read must return the latch, not #FF. Only the
            // defined bits read back: bits 5..0 on the current tree (bit 5 = 4:4:4
            // palette), bits 4..0 on the legacy one (zports.v:466-468)
            result = static_cast<uint8_t>(_state->pBF & (IsLegacyFpga() ? 0x1F : 0x3F));
            break;
        case PortArm::EvoExit:
            // The legacy tree reads the Evo registers here; the current tree
            // removed the #xxBE read ports (git 663b8cf2): write-only exit strobe
            result = IsLegacyFpga() ? ReadEvoRegister(static_cast<uint8_t>((port >> 8) & 0x1F)) : 0xFF;
            break;
        case PortArm::EvoReadback:
            // #xxBD: the readback port of the current tree; write-only (breakpoint) on the legacy one
            result = IsLegacyFpga() ? 0xFF : ReadEvoRegister(static_cast<uint8_t>((port >> 8) & 0x1F));
            break;
        case PortArm::BorderAnd7FFD:
        case PortArm::Paging7FFD:
        case PortArm::Atm77:
        case PortArm::ComPort:
        case PortArm::UlaPlus:
        case PortArm::NemoIde:
            // Mainboard ports whose read side is #FF here: no read mux entry
            // (#FC/#FD/#77) or a device that is not emulated yet (plan E6/E8/E9)
            result = 0xFF;
            break;
        case PortArm::Covox:
        case PortArm::ZxBus:
        default:
            _lastPortDecoded = false;
            // General Sound host ports (GS design §6): #B3/#BB by the low byte with
            // bit 3 masked, mirrors normalized to the canonical device keys.
            // PeripheralPortIn marks the port decoded only when a card is fitted
            if ((port & 0x00F7) == 0x00B3)
            {
                const uint16_t gsPort = (port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3;
                result = PeripheralPortIn(gsPort);
            }
            break;
    }

    OnPortInComplete(port, result, pc);
    return result;
}

void PortDecoder_ATM3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    const PortArm arm = ClassifyPort(port, /*isWrite*/ true);

    PortDecodeDisposition disp;
    uint16_t decodedPort = (arm == PortArm::Fdc) ? static_cast<uint16_t>(port & 0x00FF) : port;
    if (OverrideDecodeForFullDecodeClaim(port, decodedPort, disp, /*isRead*/ false))
    {
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    switch (arm)
    {
        case PortArm::KeyboardBorder:
            if ((port & 0x00FF) == 0x00FE)
                Default_Port_FE_Out(port, value, pc);
            else
                BorderOnlyOut(port, value, pc);  // #F6: border 8-15, beeper untouched (zports.v:944)

            // ATM 4-bit border: bit 3 is ~A3, re-latched by every border write
            // (zports.v:538 `border <= {~a[3], din[2:0]}`) - #FE gives colors 0-7, #F6 8-15
            _state->atmBorderBright = (port & 0x0008) ? 0 : 1;
            break;
        case PortArm::BorderAnd7FFD:
            // #FC: border strobe (not beeper) and, with A15=0, a #7FFD write
            // (portfe_wr and portfd_wr both include #FC, zports.v:484,536)
            BorderOnlyOut(port, value, pc);
            _state->atmBorderBright = (port & 0x0008) ? 0 : 1;
            if ((port & 0x8000) == 0)
                Port_7FFD_Out(port, value, pc);
            break;
        case PortArm::Paging7FFD:
            Port_7FFD_Out(port, value, pc);
            break;
        case PortArm::Ay:
            PeripheralPortOut((port & 0x4000) ? PORT_FFFD : PORT_BFFD, value);
            break;
        case PortArm::Eff7Gluk:
        case PortArm::Pager:
            DecodeF7Out(port, value, pc);
            break;
        case PortArm::Atm77:
            Port_FF77_Out_ATM3(port, value, pc);
            break;
        case PortArm::Fdc:
        {
            // The ZX-Evo, like the ATM-Turbo 2+, keeps the VG93 in double density:
            // only drive / side / reset / HLT reach the controller from #FF (vg93.v)
            uint8_t fdcValue = value;
            const uint16_t fdcPort = static_cast<uint16_t>(port & 0x00FF);
            if (fdcPort == 0x00FF)
                fdcValue &= 0b1011'1111;
            PeripheralPortOut(fdcPort, fdcValue);

            // The #FF write also strobes the palette latch while #xx77 A14 was 0
            // (atm_palwr = vg_wrFF & atm_pen2, zports.v:911-917)
            if (fdcPort == 0x00FF && IsPaletteWriteEnabled())
                Port_ATM_Palette_Out(port, value);
            break;
        }
        case PortArm::EvoConfig:
            Port_BF_Out(port, value, pc);
            break;
        case PortArm::EvoExit:
            Port_BE_Out(port, value, pc);
            break;
        case PortArm::Covox:
            // Covox DAC on #FB (zports.v:945); the self-decoding Covox device owns
            // the channel mapping. Other SounDrive addresses do not exist on this board
            DispatchSelfDecodingOut(port, value);
            break;
        case PortArm::EvoReadback:
            Port_BD_Out(port, value);
            break;
        case PortArm::SdConfig:
        case PortArm::SdData:
        case PortArm::Joystick:
        case PortArm::Mouse:
        case PortArm::ComPort:
        case PortArm::UlaPlus:
        case PortArm::NemoIde:
            // Mainboard ports without an emulated write side yet (ZX-Evo plan
            // E5/E6/E8/E9); swallowed so they never reach a ZX-Bus device
            break;
        case PortArm::ZxBus:
        default:
            // General Sound host ports: #B3/#BB (bit 3 masked) and #33
            if ((port & 0x00F7) == 0x00B3)
                PeripheralPortOut((port & 0x00FF) == 0x00BB ? 0x00BB : 0x00B3, value);
            else if ((port & 0x00FF) == 0x0033)
                PeripheralPortOut(0x0033, value);
            break;
    }

    OnPortOutComplete(port, value, pc);
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
    // #x7F7 (8-bit RAM page register): low byte F7, A8=1, A11:A10=01, window by
    // A15:A14 (atm_pager.v:206-210 `case {za[11],za[10]} 2'b01`)
    return (port & 0x0DFF) == 0x05F7;
}

bool PortDecoder_ATM3::IsPort_FFF7(uint16_t port, uint8_t& windowIndex)
{
    // #xFF7 (ATM window register): low byte F7, A8=1, A11:A10=11, window by
    // A15:A14 (atm_pager.v:200-204). A13:A12 are not decoded - in shadow
    // #EFF7 / #DFF7 / #BFF7 are window registers too, which is why the Gluk
    // ports move to the A8=0 aliases #DEF7 / #BEF7 there
    if ((port & 0x0DFF) != 0x0DF7)
        return false;

    windowIndex = (port >> 14) & 0x03;
    return true;
}

bool PortDecoder_ATM3::IsPort_BE(uint16_t port)
{
    // Port #xBE - ATM3 status / window readback
    return (port & 0x00FF) == 0x00BE;
}

bool PortDecoder_ATM3::IsGlukEnabled()
{
    // gluclock_on = EFF7 bit 7 || shadow (zports.v:739): in shadow the clock
    // ports are always reachable, outside only after OUT (#EFF7),#80
    return IsManagerEnabled() || (_state->pEFF7 & ATM_EFF7_GLUK) != 0;
}

bool PortDecoder_ATM3::IsPort_CMOS_Data(uint16_t port)
{
    // Gluk data: low byte F7, A14=0, A8 = !shadow (#BFF7 outside shadow, #BEF7
    // in shadow), clock enabled (zports.v:455-460)
    const bool shadow = IsManagerEnabled();
    return (port & 0x00FF) == 0x00F7 && (port & 0x4000) == 0 &&
           ((port & 0x0100) != 0) != shadow && IsGlukEnabled();
}

bool PortDecoder_ATM3::IsPort_CMOS_Address(uint16_t port)
{
    // Gluk address: as the data port with A13=0 instead of A14=0 (#DFF7 / #DEF7)
    const bool shadow = IsManagerEnabled();
    return (port & 0x00FF) == 0x00F7 && (port & 0x2000) == 0 &&
           ((port & 0x0100) != 0) != shadow && IsGlukEnabled();
}

bool PortDecoder_ATM3::IsPort_BF(uint16_t port)
{
    // Port #xBF - ATM3 control (shaden)
    return (port & 0x00FF) == 0x00BF;
}

bool PortDecoder_ATM3::IsPort_ATM_Palette(uint16_t port)
{
    // ATM3 palette write decode: exact low byte #xFF only (xpeccy evoPortMap
    // `{0x00ff, 0x00ff, 1, 2, 2, evoInBDI, evoOutFF}`). The partially decoded
    // #xx9F / #xxBF / #xxDF aliases the ATM710 DAC also matches belong to the
    // older machine - on the FPGA the palette latch sees one decoded address
    return (port & 0x00FF) == 0x00FF;
}

bool PortDecoder_ATM3::IsPaletteWriteEnabled()
{
    // xpeccy gates the palette entry on the dos line; the unreal-ng ATM3
    // analog for that shadow-port group is the manager/shaden gate
    return IsManagerEnabled();
}

/// endregion </Port detection>

/// region <Port handlers>

/// @brief ZX Evo baseconf / Pentevo clock select - three states, unlike the
///        two-state ATM 7.10 base implementation
/// @details Reference: Xpeccy pentevo.c evoOut77d,
///          `compSetHwTurbo(comp, (val & 0x08) ? 4 : ((comp->pEFF7 & 0x10) ? 1 : 2))`
///          - pFF77 bit 3 selects 14 MHz outright, otherwise pEFF7 bit 4 picks
///          3.5 MHz over the 7 MHz default.
///
///          Only hw_turbo_shift is written: next_z80_frequency_multiplier is the
///          HOST speed control and Z80::ApplyQueuedFrequencyMultiplier composes
///          current = next << hw_turbo_shift, so writing both would double-count
///          the clock and discard the user's speed setting.
void PortDecoder_ATM3::updateTurboMode()
{
    uint8_t turboShift;
    if (_state->pFF77 & ATM_FF77_TURBO)
        turboShift = 2;                                     // 14 MHz
    else if (_state->pEFF7 & ATM_EFF7_TURBO_3_5)
        turboShift = 0;                                     // 3.5 MHz compatibility
    else
        turboShift = 1;                                     // 7 MHz default

    _state->hw_turbo_shift = turboShift;

    MLOGDEBUG("ATM3 updateTurboMode: hw_turbo_shift=%d (pFF77=0x%02X pEFF7=0x%02X)",
              turboShift, _state->pFF77, _state->pEFF7);
}

void PortDecoder_ATM3::Port_FF77_Out_ATM3(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    uint8_t oldValue = _state->pFF77;

    // No atm_memswap() on the pFF77 bit0 transition - same as the ATM 7.10
    // base handler (the original gates the physical RAM permutation behind
    // the default-off "AtmMemSwap" ini option)

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

bool PortDecoder_ATM3::IsLegacyFpga() const
{
    return _context->config.atm.evo_legacy_fpga != 0;
}

/// @brief Evo readback register (index = A12..A8)
/// @details Same table on both FPGA trees (current portbdmux, legacy portbemux),
///          only the port differs. Index #13 (virtual-drive mask) exists on the
///          current tree only; undefined indices float (#FF here).
uint8_t PortDecoder_ATM3::ReadEvoRegister(uint8_t index)
{
    index &= 0x1F;

    if (index < 0x08)
    {
        // Window register page as written to #x7F7 (the board reads back ~page,
        // top.v `.pages(~{...})`; the register stores the page non-inverted)
        return static_cast<uint8_t>((_state->pFFF7[index] & 0xFF) ^ 0xFF);
    }

    switch (index)
    {
        case 0x08:  // ramnrom: bit i = window i is RAM (map 0 in bits 0-3, map 1 in 4-7)
        {
            uint8_t romMask = 0;
            for (unsigned i = 0; i < 8; i++)
                romMask |= static_cast<uint8_t>(((_state->pFFF7[i] >> 8) & 1) << i);
            return static_cast<uint8_t>(~romMask);
        }
        case 0x09:  // dos7ffd: bit i = window i takes page bits from #7FFD / DOS
        {
            uint8_t fixedMask = 0;
            for (unsigned i = 0; i < 8; i++)
                fixedMask |= static_cast<uint8_t>(((_state->pFFF7[i] >> 9) & 1) << i);
            return static_cast<uint8_t>(~fixedMask);
        }
        case 0x0A:  // last #7FFD write
            return _state->p7FFD;
        case 0x0B:  // last #EFF7 write
            return _state->pEFF7;
        case 0x0C:  // #xx77 state: {~pen2 = A14, cpm_n = A9, ~pen = A8, DOS, turbo, video mode}
            return static_cast<uint8_t>(((_state->aFF77 & ATM_AFF77_PEN2) ? 0x80 : 0x00) |
                                        ((_state->aFF77 & ATM_AFF77_CPM) ? 0x40 : 0x00) |
                                        ((_state->aFF77 & ATM_AFF77_PEN) ? 0x20 : 0x00) |
                                        ((_state->flags & CF_TRDOS) ? 0x10 : 0x00) |
                                        (_state->pFF77 & 0x0F));
        case 0x0D:  // palette entry of the border cell in the #FF write format,
                    // bits 3:2 read back as 1 (xpeccy evoInCfg; RTL round trip
                    // `{g,r,b,G,1,1,R,B}` of the displayed color)
        {
            const uint8_t cell = static_cast<uint8_t>((_state->border_attr & 0x07) | ((_state->atmBorderBright & 1) << 3));
            return static_cast<uint8_t>((_state->atmPaletteRegs[cell] & 0xF3) | 0x0C);
        }
        case 0x0F:  // border color incl. the bright half (0..15)
            return static_cast<uint8_t>((_state->border_attr & 0x07) | ((_state->atmBorderBright & 1) << 3));
        case 0x10:  // breakpoint address low / high
            return _state->pBDl;
        case 0x11:
            return _state->pBDh;
        case 0x12:  // #xBF7 write-protect bits: per-window write protect is not emulated yet (plan E8)
            return 0x00;
        case 0x13:  // virtual-drive mask, current tree only
            return IsLegacyFpga() ? 0xFF : static_cast<uint8_t>(_state->evoFddMask & 0x0F);
        case 0x0E:  // font byte under the beam: font RAM is not emulated yet (plan E8)
        default:
            return 0xFF;
    }
}

/// @brief #xxBD writes
/// @details Current tree: #10BD / #11BD breakpoint address low / high (decode
///          A12..A9 = 8, byte by A8), #13BD virtual-drive mask (zports.v:504-525).
///          Legacy tree: any #xxBD, A8 picks the breakpoint byte (baseconf zports.v:473-487).
void PortDecoder_ATM3::Port_BD_Out(uint16_t port, uint8_t value)
{
    const uint8_t index = static_cast<uint8_t>((port >> 8) & 0x1F);

    if (IsLegacyFpga() || (index >> 1) == (0x10 >> 1))
    {
        if (port & 0x0100)
            _state->pBDh = value;
        else
            _state->pBDl = value;
        return;
    }

    if (index == 0x13)
        _state->evoFddMask = static_cast<uint8_t>(value & 0x0F);
}

void PortDecoder_ATM3::Port_7FFD_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // ZX-Evo BaseConf: the 7FFD lock bit only counts while EFF7 bit 2 (lockmem)
    // holds the memory manager in 128K mode. With lockmem clear (P1024 mode)
    // 7FFD stays writable - bits 5..7 then extend the RAM page number, so a
    // sticky latch would brick the machine after the first P1024 lock write
    // (xpeccy pentevo.c evoOut7FFD: `if ((pEFF7 & 4) && (p7FFD & 0x20)) return;`)
    if ((_state->pEFF7 & ATM_EFF7_LOCKMEM) && (_state->p7FFD & PORT_7FFD_LOCK))
    {
        MLOGWARNING("Port_7FFD_Out(ATM3): Paging locked (EFF7 lockmem + 7FFD.5), ignoring write of 0x%02X", value);
        return;
    }

    Apply7FFDWrite(port, value, pc);
}

void PortDecoder_ATM3::Port_EFF7_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // On ATM3 the EFF7 z-bits feed the video mode decode (xpeccy evoOutEFF7 ->
    // evoSetVideoMode), so a z-bit change needs the same raster re-detection
    // a #xx77 mode change gets. Other EFF7 bits are control-only (turbo /
    // lockmem / rocache) exactly as on the base machine
    constexpr uint8_t VIDEO_BITS = EFF7_4BPP | EFF7_HWMC;
    const uint8_t oldVideoBits = _state->pEFF7 & VIDEO_BITS;

    PortDecoder_ATM710::Port_EFF7_Out(port, value, pc);

    // Bit 2 (128K / 1 MB page mode) and bit 3 (RAM 0 at #0000) change the window map
    if (_memory)
        _memory->UpdateZ80Banks();

    if (((_state->pEFF7 ^ oldVideoBits) & VIDEO_BITS) != 0)
    {
        if (_context->pScreen)
        {
            _context->pScreen->InitRaster();
            MLOGINFO("Port_EFF7_Out(ATM3): video bits changed (0x%02X -> 0x%02X), InitRaster() called",
                     oldVideoBits, _state->pEFF7 & VIDEO_BITS);
        }
    }
}

/// @brief #F7 writes outside the pager: #EFF7 and the Gluk clock ports
/// @details zports.v:490-491, 714-750: the group decodes A8 against the shadow
///          line (A8=1 outside shadow, A8=0 in shadow, so it never collides
///          with the ATM window registers), then A12/A13/A14 independently:
///          A12=0 -> #EFF7 (outside shadow only), A13=0 -> clock address,
///          A14=0 -> clock data. Inside shadow with A8=1 the pager owns the
///          port: A11:A10 = 11 #xFF7, 01 #x7F7, 10 #xBF7 (write protect)
void PortDecoder_ATM3::DecodeF7Out(uint16_t port, uint8_t value, uint16_t pc)
{
    const bool shadow = IsManagerEnabled();
    const bool a8 = (port & 0x0100) != 0;

    if (shadow && a8)
    {
        uint8_t windowIndex = 0;
        if (IsPort_FFF7(port, windowIndex))
            Port_FFF7_Out(port, value, windowIndex, pc);
        else if (IsPort_37F7(port))
            Port_37F7_Out(port, value, pc);
        else
            MLOGDEBUG("PortDecoder_ATM3: #%04X write-protect / unused pager function ignored", port);
        return;
    }

    if (a8 == shadow)
        return;  // A8=0 outside shadow: no function on this board

    // Clock enable as latched before this cycle (gluclock_on uses the registered EFF7)
    const bool gluk = IsGlukEnabled();

    if (!shadow && (port & 0x1000) == 0)
        Port_EFF7_Out(port, value, pc);

    if (gluk && (port & 0x2000) == 0)
        _cmos.SetCMOSAddress(value);
    if (gluk && (port & 0x4000) == 0)
        _cmos.WriteCMOS(value);
}

/// @brief #F7 reads: only the clock data port drives the bus (zports.v:455-460);
///        #EFF7 and the ATM window registers are write-only
uint8_t PortDecoder_ATM3::DecodeF7In(uint16_t port)
{
    if (IsPort_CMOS_Data(port))
        return _cmos.ReadCMOS();
    return 0xFF;
}

/// @brief Border strobe without the beeper / tape bits (#F6, #FC)
/// @details beeper_wr is `loa == #FE` only (zports.v:944): #F6 and #FC set the
///          border and leave MIC/EAR where the last #FE write put them
void PortDecoder_ATM3::BorderOnlyOut(uint16_t port, uint8_t value, uint16_t pc)
{
    const uint8_t keep = static_cast<uint8_t>(_state->pFE & 0xF8);
    Default_Port_FE_Out(port, static_cast<uint8_t>((value & 0x07) | keep), pc);
}

/// @brief BaseConf window mapping (fpga/base_trdemu/trunk/mem/atm_pager.v:114-168)
/// @details Priority for window 0: pager off (all windows ROM 31) > NMI (RAM
///          #FF) > #EFF7 bit 3 (RAM page 0) > the page register. A register
///          whose "dos7ffd" bit is set takes its low page bits from #7FFD:
///          128K mode (#EFF7 bit 2 = 1) `{reg[7:3], 7FFD[2:0]}`, Pentagon-1024
///          mode (bit 2 = 0, the reset state) `{reg[7:6], 7FFD[7:5], 7FFD[2:0]}`.
///          A ROM register with the bit set swaps its page LSB for the DOS signal.
void PortDecoder_ATM3::updateMemoryBanks()
{
    if (!_memory)
        return;

    const CONFIG& config = _context->config;

    const uint16_t ramPages = config.ramsize ? (config.ramsize / 16) : MAX_RAM_PAGES;
    const uint8_t ramMask = static_cast<uint8_t>(ramPages - 1);

    const uint8_t romBanks = (_context->pCore && _context->pCore->GetROM()) ? _context->pCore->GetROM()->GetROMBanksLoaded() : 0;
    const uint8_t romMask = romBanks ? static_cast<uint8_t>(romBanks - 1) : 0;

    // #xx77 A9=0 (cpm_n) forces the DOS signal (zdos.v:68-69)
    if (!(_state->aFF77 & ATM_AFF77_CPM))
        _state->flags |= CF_TRDOS;
    const bool dos = (_state->flags & CF_TRDOS) != 0;

    // #xx77 A8=0: pager off, every window reads the last ROM page
    if (!(_state->aFF77 & ATM_AFF77_PEN))
    {
        for (uint8_t bank = 0; bank < 4; bank++)
            _memory->SetROMPageToBank(bank, romMask);
        return;
    }

    const unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    const bool oneMegMode = (_state->pEFF7 & ATM_EFF7_LOCKMEM) == 0;
    const uint8_t p7FFD = _state->p7FFD;

    for (uint8_t bank = 0; bank < 4; bank++)
    {
        const unsigned reg = _state->pFFF7[regSet + bank];

        switch (reg & 0x300)
        {
            case 0x000:  // RAM, low page bits from #7FFD
            {
                uint16_t page = oneMegMode
                                    ? static_cast<uint16_t>((reg & 0xC0) | (((p7FFD >> 5) & 0x07) << 3) | (p7FFD & 0x07))
                                    : static_cast<uint16_t>((reg & 0xF8) | (p7FFD & 0x07));
                page &= ramMask;
                if (bank == 0) _memory->SetRAMPageToBank0(page);
                else if (bank == 1) _memory->SetRAMPageToBank1(page);
                else if (bank == 2) _memory->SetRAMPageToBank2(page);
                else _memory->SetRAMPageToBank3(page);
                break;
            }
            case 0x100:  // ROM, page LSB = DOS signal
                _memory->SetROMPageToBank(bank, static_cast<uint16_t>(((reg & 0xFE) | (dos ? 1 : 0)) & romMask));
                break;
            case 0x200:  // RAM, page from the register
            {
                const uint16_t page = static_cast<uint16_t>(reg & 0xFF & ramMask);
                if (bank == 0) _memory->SetRAMPageToBank0(page);
                else if (bank == 1) _memory->SetRAMPageToBank1(page);
                else if (bank == 2) _memory->SetRAMPageToBank2(page);
                else _memory->SetRAMPageToBank3(page);
                break;
            }
            case 0x300:  // ROM, page from the register
            default:
                _memory->SetROMPageToBank(bank, static_cast<uint16_t>(reg & 0xFF & romMask));
                break;
        }
    }

    // Window 0 overrides
    if (_state->nmi_in_progress)
        _memory->SetRAMPageToBank0(0xFF & ramMask);
    else if (_state->pEFF7 & ATM_EFF7_ROCACHE)
        _memory->SetRAMPageToBank0(0);
}

/// endregion </Port handlers>
