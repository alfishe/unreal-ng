#include "stdafx.h"
#include "portdecoder_atm3.h"

#include "emulator/io/mouse/mousemanager.h"

#include "common/modulelogger.h"
#include "debugger/ttd/atm/ttdevofontram.h"
#include "debugger/ttd/atm/ttdevoavrvolatile.h"
#include "debugger/ttd/atm/ttdevoflash.h"
#include "debugger/ttd/atm/ttdevomouse.h"
#include "debugger/ttd/atm/ttdevops2.h"
#include "debugger/ttd/atm/ttdevosdcard.h"
#include "debugger/ttd/atm/ttdevoturbocache.h"
#include "debugger/ttd/ttdds12887.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// region <Constructors / Destructors>

PortDecoder_ATM3::PortDecoder_ATM3(EmulatorContext* context) : PortDecoder_ATM710(context, false)
{
    _zc.SetDevice(&_sdCard);
    _evoAvr.SetEmulatedClock([this]() { return EmulatedMicroseconds(); });
    _evoAvr.SetSessionWall([this]() { return SessionWallMicros(); });

    // TTD: the card's protocol state is in the EvoSdCard blob; a guest write
    // changes the medium, so it is a replay barrier (the media manager's rule)
    _sdCard.setWriteListener([this](uint64_t) {
        if (_context->pMediaManager)
            _context->pMediaManager->NoteWrite(_sdSlot.Descriptor().id);
    });

    if (_context->pMediaManager)
        _context->pMediaManager->RegisterSlot(_sdSlot);

    // The AVR is the board's PS/2 keyboard controller: physical host keys reach
    // its scan code log (NedoOS reads only that). Other machines attach nothing
    if (_context->pKeyboard)
        _context->pKeyboard->SetPs2Sink(&_evoAvr);

    // The AVR's PS/2 mouse is the board's mouse: the host mouse reaches it through the
    // emulator's mouse manager, and the Kempston-address ports read its registers
    _evoAvr.Ps2Mouse().SetFrameSource([context = _context] { return context->emulatorState.frame_counter; });
    _evoAvr.Ps2Mouse().SetConnected(_mouse && _mouse->IsPresent());
    if (_context->pMouseManager)
        _context->pMouseManager->AddSink(&_evoAvr.Ps2Mouse());

    // The AVR owns the board's resets: F12 released after a short hold = the
    // reset button, a key pressed with Ctrl+Alt held = the power cycle. The
    // sink is resolved when a reset fires: the decoder is built before
    // Emulator installs it
    _evoAvr.SetResetHandler([context = _context](bool hardReset) {
        if (!context->pSoftResetSink)
            return;
        if (hardReset)
            context->pSoftResetSink->RequestHardReset();
        else
            context->pSoftResetSink->RequestSoftReset();
    });
}

PortDecoder_ATM3::~PortDecoder_ATM3()
{
    if (_context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(_sdSlot.Descriptor().id);

    if (_context->pKeyboard && _context->pKeyboard->GetPs2Sink() == &_evoAvr)
        _context->pKeyboard->SetPs2Sink(nullptr);
    if (_context->pMouseManager)
        _context->pMouseManager->RemoveSink(&_evoAvr.Ps2Mouse());

    if (_context->pCore && _context->pCore->GetZ80() && _context->pCore->GetZ80()->machineM1Hook == this)
        _context->pCore->GetZ80()->machineM1Hook = nullptr;

    if (_turboWaitsInstalled && _context->pCore)
        _context->pCore->RemoveBusOverlay(_turboOverlay.get());

    if (_fontOverlayInstalled && _context->pCore)
        _context->pCore->RemoveBusOverlay(_fontOverlay.get());

    if (_context->pCore)
        _context->pCore->RemoveBusOverlay(&_flash);

    // Battery-backed state outlives the machine ([EVO] NvramFile)
    const char* nvramPath = _context->config.atm.evo_nvram_path;
    if (_nvramLoaded && nvramPath[0] != '\0' && !_evoAvr.SaveNvram(nvramPath))
        MLOGWARNING("PortDecoder_ATM3: cannot save the ZX-Evo NVRAM to '%s'", nvramPath);

    MLOGDEBUG("PortDecoder_ATM3::~PortDecoder_ATM3()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_ATM3::reset()
{
    PortDecoder_ATM710::reset();

    // The AVR keeps running through a Z80 reset, but the emulator's time base (t_states) starts again from 0: its
    // main-loop phase and EEPROM write are re-anchored there (EvoAvrWait counts in that time base)
    _evoAvr.Wait().Reset();
    // The flash chip has no reset pin; the time base restarts, so a running program / erase completes here
    _flash.OnMachineReset();

    // A mouse plugged in or out ([INPUT] Mouse=) re-runs the AVR's mouse reset; the
    // registers themselves outlive a Z80 reset (the AVR keeps running)
    _evoAvr.Ps2Mouse().SetConnected(_mouse && _mouse->IsPresent());

    // ATM3-specific reset
    _state->evo.pBDb.l = 0x00;
    _state->evo.pBDb.h = 0x00;
    _state->evo.pBE = 0x00;
    _state->evo.pBF = 0x00;
    _state->evo.wrProt = 0x00;  // atm_pager.v: wrdisables reset to 0
    SyncFontOverlay();         // pBF.2 is clear now; the font RAM itself keeps its content (altdpram, not reset)
    SyncFlashWindows();        // pBF.1 is clear now: no window writes the flash
    _state->evo.fddMask = 0x00;  // fdd_mask resets to "all drives real" (zports.v:521-525)

    // znmi.v: reset clears pending_nmi, in_nmi, in_nmi_2 (pBE doubles as the
    // NMI exit M1 countdown, 0 = idle)
    _state->evo.inNmi = false;
    _state->evo.nmiEntry = false;
    _state->nmiAtIntStartPending = false;
    _state->evo.trdemu = 0;     // zdos.v: in_trdemu resets to 0
    _state->evo.vgSys = 0;  // vg_res_n resets to 0, the rest reads as 0
    // The reset's 7 MHz select (ATM710::reset ran updateTurboMode) is taken over at the next M1 like any other
    _state->evo.turboPending = (_state->hw_turbo_ratio != _state->hw_turbo_ratio_applied) ? 1 : 0;
    RefreshM1Hook();

    // The battery-backed NVRAM and EEPROM come from [EVO] NvramFile once, at
    // power-on; a Z80 reset does not touch the AVR
    if (!_nvramLoaded)
    {
        _nvramLoaded = true;
        const char* nvramPath = _context->config.atm.evo_nvram_path;
        if (nvramPath[0] != '\0' && !_evoAvr.LoadNvram(nvramPath))
            MLOGINFO("PortDecoder_ATM3: no ZX-Evo NVRAM at '%s' yet, starting blank", nvramPath);
    }

    // The card and its session writes survive a Z80 reset (the media manager
    // inserted the configured card before the first one). The controller
    // itself resets (spihub.v: /CS high)
    _zc.Reset();
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
            // In shadow a write with A15 = 1 is the chip select (#8057, what
            // NedoOS uses); reads are always data (zports.v:812-818)
            return (isWrite && shadow && (port & 0x8000)) ? PortArm::SdConfig : PortArm::SdData;
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
        case 0x2F:
        case 0x4F:
        case 0x6F:
        case 0x8F:
            // Legacy FPGA: four shadow R/W bytes for the patched-DOS RAM disk
            // (baseconf zports.v:186-189); the current tree removed them
            return (shadow && IsLegacyFpga()) ? PortArm::LegacyFddLatch : PortArm::ZxBus;
        default:
            break;
    }

    // NemoIDE task-file ports and their aliases: `IS_NIDE_REGS(x) = (x[2:0]==0) && (x[3]!=x[4])`
    // (#10, #30 ... #F0 and #08, #28 ... #E8; #C8 is the CS1 control register)
    if ((low & 0x07) == 0 && ((low >> 3) & 1) != ((low >> 4) & 1))
        return PortArm::NemoIde;

    return PortArm::ZxBus;
}

/// @brief Internal port codes: the BaseConf decode arms ClassifyPort resolves
///        every I/O cycle to (zports.v porthit list), by PortArm value
std::vector<PortTraceCodeName> PortDecoder_ATM3::GetPortTraceCodeTable() const
{
    return {
        {static_cast<uint16_t>(PortArm::ZxBus), "ZxBus"},
        {static_cast<uint16_t>(PortArm::KeyboardBorder), "KeyboardBorder"},
        {static_cast<uint16_t>(PortArm::BorderAnd7FFD), "BorderAnd7FFD"},
        {static_cast<uint16_t>(PortArm::Paging7FFD), "Paging7FFD"},
        {static_cast<uint16_t>(PortArm::Ay), "Ay"},
        {static_cast<uint16_t>(PortArm::Eff7Gluk), "Eff7Gluk"},
        {static_cast<uint16_t>(PortArm::Pager), "Pager"},
        {static_cast<uint16_t>(PortArm::Atm77), "Atm77"},
        {static_cast<uint16_t>(PortArm::SdConfig), "SdConfig"},
        {static_cast<uint16_t>(PortArm::SdData), "SdData"},
        {static_cast<uint16_t>(PortArm::Fdc), "Fdc"},
        {static_cast<uint16_t>(PortArm::Joystick), "Joystick"},
        {static_cast<uint16_t>(PortArm::Mouse), "Mouse"},
        {static_cast<uint16_t>(PortArm::EvoConfig), "EvoConfig"},
        {static_cast<uint16_t>(PortArm::EvoExit), "EvoExit"},
        {static_cast<uint16_t>(PortArm::EvoReadback), "EvoReadback"},
        {static_cast<uint16_t>(PortArm::ComPort), "ComPort"},
        {static_cast<uint16_t>(PortArm::UlaPlus), "UlaPlus"},
        {static_cast<uint16_t>(PortArm::NemoIde), "NemoIde"},
        {static_cast<uint16_t>(PortArm::Covox), "Covox"},
        {static_cast<uint16_t>(PortArm::LegacyFddLatch), "LegacyFddLatch"},
    };
}

PortDecodeDisposition PortDecoder_ATM3::TraceDisposition(PortArm arm, uint16_t port, bool isWrite)
{
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.wasHandledInline = true;
    disp.internalCode = static_cast<uint16_t>(arm);  // the BaseConf decode arm (GetPortTraceCodeTable)
    const uint16_t low = port & 0x00FF;

    switch (arm)
    {
        case PortArm::KeyboardBorder:
            disp.decodedPort = 0x00FE;
            break;
        case PortArm::BorderAnd7FFD:
            // #FC: a write with A15=0 also pages - the paging is the part that matters
            disp.decodedPort = (isWrite && (port & 0x8000) == 0) ? 0x7FFD : 0x00FE;
            break;
        case PortArm::Paging7FFD:
            disp.decodedPort = 0x7FFD;
            break;
        case PortArm::Ay:
            disp.decodedPort = (port & 0x4000) ? 0xFFFD : 0xBFFD;
            disp.wasHandledInline = false;
            break;
        case PortArm::Eff7Gluk:
            // #EFF7 is A12=0; the other #xxF7 addresses are the Gluk clock
            disp.decodedPort = (port & 0x1000) == 0 ? 0xEFF7 : port;
            disp.device = (port & 0x1000) == 0 ? PortDeviceId::Control_EFF7 : PortDeviceId::Custom;
            break;
        case PortArm::Pager:
            disp.decodedPort = port;
            disp.device = PortDeviceId::Memory_Windows;
            break;
        case PortArm::Atm77:
            disp.decodedPort = 0xFF77;
            disp.device = PortDeviceId::ATM_FF77;
            break;
        case PortArm::SdConfig:
        case PortArm::SdData:
            disp.decodedPort = port;
            disp.device = PortDeviceId::SdCard;
            break;
        case PortArm::Fdc:
            // #FF also strobes the palette latch; the FDC side is the one attributed
            disp.decodedPort = low;
            disp.wasHandledInline = false;
            break;
        case PortArm::EvoConfig:
        case PortArm::EvoExit:
        case PortArm::EvoReadback:
            disp.decodedPort = port;
            disp.device = PortDeviceId::Evo_Config;
            break;
        case PortArm::Covox:
            disp.decodedPort = 0x00FB;
            disp.wasHandledInline = false;
            break;
        case PortArm::Joystick:
        case PortArm::Mouse:
        case PortArm::ComPort:
        case PortArm::UlaPlus:
        case PortArm::NemoIde:
        case PortArm::LegacyFddLatch:
            // Mainboard ports without their own device id (joystick #1F must not
            // read as the FDC, which shares the low byte in shadow)
            disp.decodedPort = port;
            disp.device = PortDeviceId::Custom;
            break;
        case PortArm::ZxBus:
        default:
            // ZX-Bus cards: the General Sound host ports by their canonical keys
            disp.wasHandledInline = false;
            if ((port & 0x00F7) == 0x00B3)
                disp.decodedPort = low == 0x00BB ? 0x00BB : 0x00B3;
            else if (isWrite && low == 0x0033)
                disp.decodedPort = 0x0033;
            break;
    }
    return disp;
}

uint8_t PortDecoder_ATM3::DecodePortIn(uint16_t port, uint16_t pc)
{
    if (_turboWaitsInstalled) [[unlikely]]
        NoteTurboIo(port);

    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (uint8_t ideValue = 0xFF; TryIdePortIn(port, pc, ideValue))
        return ideValue;

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
            // zports.v: {1'b1, tape_read, 1'b0, keys_in} - bit 5 reads 0 here, 1 on a plain ULA
            result = static_cast<uint8_t>(Default_Port_FE_In(port, pc) & ~0x20);
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
            result = _zc.ReadData();
            break;
        case PortArm::Fdc:
        {
            const uint8_t fdcPort = static_cast<uint8_t>(port & 0x00FF);
            // A drive emulated in software leaves the chip deselected: nothing drives the bus
            result = TrdemuFdcAccess(fdcPort, /*isWrite*/ false, 0) ? 0xFF : PeripheralPortIn(fdcPort);
            // #FF reads {intrq, drq, 1, last write D4..D0} on the current tree (zports.v VGSYS, vg93.v:177);
            // the legacy tree reads back all six written bits (vgFF). The chip's own register does not hold them
            // while the write was a reset
            if (fdcPort == 0xFF)
                result = static_cast<uint8_t>((result & 0xC0) |
                                              (IsLegacyFpga() ? (_state->evo.vgSys & 0x3F) : (0x20 | (_state->evo.vgSys & 0x1F))));
            break;
        }
        case PortArm::LegacyFddLatch:
            result = _state->wd_shadow[((port & 0x00FF) >> 5) - 1];
            break;
        case PortArm::Joystick:
            // Kempston joystick outside shadow (zports.v: kj_in, the AVR's SPI register). No device
            // or not fitted: nothing is pressed, 0x00
            result = Default_Port_KempstonJoystick_In();
            break;
        case PortArm::Mouse:
        {
            // #xxDF: A8=0 buttons + wheel, A8=1 & A10=0 X, A10=1 Y (zkbdmus.v:118-120);
            // the AVR answers #FF with no mouse. Not DOS-gated on this board
            const uint8_t reg = (port & 0x0100) ? ((port & 0x0400) ? 2 : 1) : 0;
            result = _evoAvr.Ps2Mouse().ReadRegister(reg);  // #FF / #FF / #FF with no mouse (zx_mouse_reset(0))
            break;
        }
        case PortArm::EvoConfig:
            // The BaseConf service ROM does IN A,(BF) / OR 1 / OUT (BF),A to open
            // the shadow ports - the read must return the latch, not #FF. Only the
            // defined bits read back: bits 5..0 on the current tree (bit 5 = 4:4:4
            // palette), bits 4..0 on the legacy one (zports.v:466-468)
            result = static_cast<uint8_t>(_state->evo.pBF & (IsLegacyFpga() ? 0x1F : 0x3F));
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

    PortDecodeDisposition trace = TraceDisposition(arm, port, /*isWrite*/ false);
    trace.wasDecoded = _lastPortDecoded;
    OnPortInComplete(port, result, pc, trace);
    return result;
}

void PortDecoder_ATM3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    if (_turboWaitsInstalled) [[unlikely]]
        NoteTurboIo(port);

    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (TryIdePortOut(port, value, pc))
        return;

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
            _state->atm.borderBright = (port & 0x0008) ? 0 : 1;
            break;
        case PortArm::BorderAnd7FFD:
            // #FC: border strobe (not beeper) and, with A15=0, a #7FFD write
            // (portfe_wr and portfd_wr both include #FC, zports.v:484,536)
            BorderOnlyOut(port, value, pc);
            _state->atm.borderBright = (port & 0x0008) ? 0 : 1;
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
            if (!TrdemuFdcAccess(static_cast<uint8_t>(fdcPort), /*isWrite*/ true, value))
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
        case PortArm::LegacyFddLatch:
            _state->wd_shadow[((port & 0x00FF) >> 5) - 1] = value;
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
            _zc.WriteConfig(value);
            break;
        case PortArm::SdData:
            _zc.WriteData(value);
            break;
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

    PortDecodeDisposition trace = TraceDisposition(arm, port, /*isWrite*/ true);
    trace.wasDecoded = trace.decodedPort != 0x0000;
    OnPortOutComplete(port, value, pc, trace);
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
    return (_state->evo.pBF & 0x01) != 0 ||
           (_state->atm.aFF77 & ATM_AFF77_CPM) == 0 ||
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
///          Only hw_turbo_ratio is written: next_z80_frequency_multiplier is the
///          HOST speed control and Z80::ApplyQueuedFrequencyMultiplier composes
///          current = next x hw_turbo_ratio, so writing both would double-count
///          the clock and discard the user's speed setting.
void PortDecoder_ATM3::updateTurboMode()
{
    uint8_t turboRatio;
    if (_state->pFF77 & ATM_FF77_TURBO)
        turboRatio = 4;                                     // 14 MHz
    else if (_state->pEFF7 & ATM_EFF7_TURBO_3_5)
        turboRatio = 1;                                     // 3.5 MHz compatibility
    else
        turboRatio = 2;                                     // 7 MHz default

    _state->hw_turbo_ratio = turboRatio;

    // zclock.v: the clock select is taken over on the falling edge of /RFSH, the refresh of the next M1, not at
    // the OUT. The M1 hook (attached only while this is pending) applies it
    _state->evo.turboPending = (turboRatio != _state->hw_turbo_ratio_applied) ? 1 : 0;
    RefreshM1Hook();

    MLOGDEBUG("ATM3 updateTurboMode: hw_turbo_ratio=%d (pFF77=0x%02X pEFF7=0x%02X)",
              turboRatio, _state->pFF77, _state->pEFF7);

    SyncTurboWaits();
}

/// region <Wait states at 14 MHz>

void PortDecoder_ATM3::SyncTurboWaits()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80() || !_memory)
        return;

    const bool wanted = _state->hw_turbo_ratio == 4;
    if (wanted == _turboWaitsInstalled)
        return;

    if (wanted)
    {
        if (!_turboOverlay)
            _turboOverlay = std::make_unique<EvoTurboOverlay>(core, core->GetZ80(), _memory, _state);
        // The words cached before the switch are not tracked (no waits below 14 MHz): start empty
        _turboOverlay->Invalidate();
        _turboWaitsInstalled = core->AddBusOverlay(_turboOverlay.get());
        if (!_turboWaitsInstalled)
            MLOGWARNING("PortDecoder_ATM3: no room for the 14 MHz wait-state overlay; 14 MHz runs without waits");
    }
    else
    {
        core->RemoveBusOverlay(_turboOverlay.get());
        _turboWaitsInstalled = false;
    }
}

void PortDecoder_ATM3::SyncFontOverlay()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80() || !_memory)
        return;

    const bool wanted = (_state->evo.pBF & 0x04) != 0;
    if (wanted == _fontOverlayInstalled)
        return;

    if (wanted)
    {
        if (!_fontOverlay)
            _fontOverlay = std::make_unique<EvoFontOverlay>(_state->atm.fontRam);
        _fontOverlayInstalled = core->AddBusOverlay(_fontOverlay.get());
        if (!_fontOverlayInstalled)
            MLOGWARNING("PortDecoder_ATM3: no room for the font RAM loader overlay; font writes are lost");
    }
    else
    {
        core->RemoveBusOverlay(_fontOverlay.get());
        _fontOverlayInstalled = false;
    }
}

void PortDecoder_ATM3::NoteTurboIo(uint16_t port)
{
    _turboOverlay->Invalidate();

    // External ports (zports.v:361-367): AY (#FD with A15 = 1) and the VG93 ports in shadow
    const uint8_t low = static_cast<uint8_t>(port & 0x00FF);
    const bool external = (low == 0xFD && (port & 0x8000)) ||
                          ((low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F) && IsManagerEnabled());
    if (external && _turboOverlay->WaitsApply())
        _context->pCore->GetZ80()->AddWaitStates(3);
}

EvoTurboOverlay::CacheState PortDecoder_ATM3::GetTurboCacheState() const
{
    return _turboWaitsInstalled ? _turboOverlay->GetCacheState() : EvoTurboOverlay::CacheState{};
}

void PortDecoder_ATM3::SetTurboCacheState(const EvoTurboOverlay::CacheState& state)
{
    if (_turboWaitsInstalled)
        _turboOverlay->SetCacheState(state);
}

/// endregion </Wait states at 14 MHz>

void PortDecoder_ATM3::Port_FF77_Out_ATM3(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    uint8_t oldValue = _state->pFF77;

    // No atm_memswap() on the pFF77 bit0 transition - same as the ATM 7.10
    // base handler (the original gates the physical RAM permutation behind
    // the default-off "AtmMemSwap" ini option)

    // Store value and full port address
    _state->pFF77 = value;
    _state->atm.aFF77 = port;

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

    unsigned fullValue = (_state->atm.pFFF7[idx] & ~0x1FFu) | (value ^ 0xFF);
    _state->atm.pFFF7[idx] = fullValue;

    // Full set_banks() equivalent: window mapping + TR-DOS session flag re-derivation
    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_37F7_Out: idx=%d value=0x%02X fullValue=0x%04X", idx, value, fullValue);
}

void PortDecoder_ATM3::Port_BF7_Out(uint16_t port, uint8_t value)
{
    // atm_pager.v:186 `wrdisables[pent1m_ROM] <= zd[0]`: the window is A15:A14, the map the current #7FFD.4
    const unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    const uint8_t bit = static_cast<uint8_t>(1u << (regSet + (port >> 14)));
    _state->evo.wrProt = static_cast<uint8_t>((value & 1) ? (_state->evo.wrProt | bit) : (_state->evo.wrProt & ~bit));

    if (_memory)
        _memory->UpdateZ80Banks();

    MLOGDEBUG("Port_BF7_Out: port=0x%04X value=0x%02X protect=0x%02X", port, value, _state->evo.wrProt);
}

bool PortDecoder_ATM3::IsWindowWriteProtected(uint8_t bank) const
{
    // Pager off: every window is the ROM, nothing to protect (atm_pager.v:121)
    if (!(_state->atm.aFF77 & ATM_AFF77_PEN))
        return false;

    // Window 0 under the NMI page, the virtual TR-DOS page or RAM 0 answers to trdemu_wr_disable, which the
    // trap order already covers (atm_pager.v:126)
    if (bank == 0 && (_state->evo.inNmi || (_state->evo.trdemu & kTrdemuIn) || (_state->pEFF7 & ATM_EFF7_ROCACHE)))
        return false;

    const unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    return (_state->evo.wrProt >> (regSet + (bank & 3))) & 1;
}

void PortDecoder_ATM3::SyncFlashWindows()
{
    uint8_t mask = 0;
    if (_memory && (_state->evo.pBF & 0x02))
        for (uint8_t bank = 0; bank < 4; bank++)
            if (_memory->IsWindowRom(bank) && !IsWindowWriteProtected(bank))
                mask |= static_cast<uint8_t>(1u << bank);
    _flash.SetWriteWindows(mask);
}

void PortDecoder_ATM3::OnFrameEnd()
{
    PortDecoder_ATM710::OnFrameEnd();
    _flash.OnFrameEnd();
}

void PortDecoder_ATM3::OnDosRomFetch(uint16_t pc)
{
    // atm_pager.v zclk_stall: 4 fclk of the 28 MHz clock (half a 3.5 MHz T = 128 counter ticks) on every fetch from
    // #3Dxx of a window that holds the DOS ROM in map 1 (map 1 current, the register ROM with the dos7ffd bit), so
    // the ROM chip can answer. Like every machine wait it follows the `contention` feature
    if ((_state->p7FFD & 0x10) && (_state->atm.pFFF7[4 + (pc >> 14)] & 0x300) == 0x100 && _context->pCore &&
        _context->pCore->IsContentionSwitchOn())
        _context->pCore->GetZ80()->AddWaitTicks(kDosEntryStallTicks);
}

void PortDecoder_ATM3::Port_BF_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // Bit 3: a 1->0 edge requests a board NMI, released at the next frame INT
    // (znmi.v set_nmi_now -> pending_nmi -> nmi_start at int_start)
    const bool nmiEdge = (_state->evo.pBF & 0x08) && !(value & 0x08);
    _state->evo.pBF = value;
    if (nmiEdge)
        RequestBoardNmi();

    // Bit 2 lets every memory write also write the font RAM (zports.v fnt_wr)
    SyncFontOverlay();

    // Bit 4 enables the M1 breakpoint (zbreak.v)
    RefreshM1Hook();

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
    // NMI exit (znmi.v clr_nmi): the NMI page leaves #0000-#3FFF right after
    // the refresh of the second M1 that follows this write - with the usual
    // OUT (#BE),A : RETN that is the RETN's second opcode byte, so RETN runs
    // from the NMI page and returns through the restored map. pBE counts those
    // M1s down (0 = idle)
    if (_state->evo.inNmi)
    {
        _state->evo.pBE = 2;
        RefreshM1Hook();
    }
    else if (_state->evo.trdemu & kTrdemuIn)
    {
        // Virtual TR-DOS exit: immediate (zdos.v `clr_nmi && !in_nmi`), so the
        // fetch right after this OUT already comes from the TR-DOS ROM
        _state->evo.trdemu &= static_cast<uint8_t>(~kTrdemuIn);
        if (_memory)
            _memory->UpdateZ80Banks();
    }

    MLOGDEBUG("Port_BE_Out: NMI exit armed=%d", _state->evo.pBE != 0);
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
        return static_cast<uint8_t>((_state->atm.pFFF7[index] & 0xFF) ^ 0xFF);
    }

    switch (index)
    {
        case 0x08:  // ramnrom: bit i = window i is RAM (map 0 in bits 0-3, map 1 in 4-7)
        {
            uint8_t romMask = 0;
            for (unsigned i = 0; i < 8; i++)
                romMask |= static_cast<uint8_t>(((_state->atm.pFFF7[i] >> 8) & 1) << i);
            return static_cast<uint8_t>(~romMask);
        }
        case 0x09:  // dos7ffd: bit i = window i takes page bits from #7FFD / DOS
        {
            uint8_t fixedMask = 0;
            for (unsigned i = 0; i < 8; i++)
                fixedMask |= static_cast<uint8_t>(((_state->atm.pFFF7[i] >> 9) & 1) << i);
            return static_cast<uint8_t>(~fixedMask);
        }
        case 0x0A:  // last #7FFD write
            return _state->p7FFD;
        case 0x0B:  // last #EFF7 write
            return _state->pEFF7;
        case 0x0C:  // #xx77 state: {~pen2 = A14, cpm_n = A9, ~pen = A8, DOS, turbo, video mode}
            return static_cast<uint8_t>(((_state->atm.aFF77 & ATM_AFF77_PEN2) ? 0x80 : 0x00) |
                                        ((_state->atm.aFF77 & ATM_AFF77_CPM) ? 0x40 : 0x00) |
                                        ((_state->atm.aFF77 & ATM_AFF77_PEN) ? 0x20 : 0x00) |
                                        ((_state->flags & CF_TRDOS) ? 0x10 : 0x00) |
                                        (_state->pFF77 & 0x0F));
        case 0x0D:  // the displayed color of the border cell, `{g,r,b,G,1,1,R,B}` (RTL palcolor / BD_COLORRD): the
                    // high bit pair of each channel, or the low pair while the 4:4:4 palette is on
        {
            const uint8_t cell = static_cast<uint8_t>((_state->border_attr & 0x07) | ((_state->atm.borderBright & 1) << 3));
            const uint32_t abgr = _state->atm.palette[cell];
            const unsigned shift = PaletteLowBitsFromAddress() ? 4 : 6;  // the nibble's low pair, or the byte's top pair
            const unsigned red = (abgr >> shift) & 0x03;
            const unsigned green = (abgr >> (8 + shift)) & 0x03;
            const unsigned blue = (abgr >> (16 + shift)) & 0x03;
            // The stored color is the positive one; the port shows it inverted, as it was written
            const unsigned low = (~((green & 1) << 2 | (red & 1) << 1 | (blue & 1))) & 0x07;   // g, r, b
            const unsigned high = (~((green >> 1) << 2 | (red >> 1) << 1 | (blue >> 1))) & 0x07;  // G, R, B
            return static_cast<uint8_t>(((low & 0x04) << 5) | ((low & 0x02) << 5) | ((low & 0x01) << 5) |
                                        ((high & 0x04) << 2) | 0x0C | (high & 0x02) | (high & 0x01));
        }
        case 0x0F:  // border color incl. the bright half (0..15)
            return static_cast<uint8_t>((_state->border_attr & 0x07) | ((_state->atm.borderBright & 1) << 3));
        case 0x10:  // breakpoint address low / high
            return _state->evo.pBDb.l;
        case 0x11:
            return _state->evo.pBDb.h;
        case 0x12:  // #xBF7 write-protect bits, the order of 08
            return _state->evo.wrProt;
        case 0x13:  // virtual-drive mask, current tree only
            return IsLegacyFpga() ? 0xFF : static_cast<uint8_t>(_state->evo.fddMask & 0x0F);
        case 0x0E:  // the glyph byte the text renderer fetched last (RTL fontrom_readback)
            return _state->atm.fontByte;
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
            _state->evo.pBDb.h = value;
        else
            _state->evo.pBDb.l = value;
        return;
    }

    if (index == 0x13)
        _state->evo.fddMask = static_cast<uint8_t>(value & 0x0F);
}

void PortDecoder_ATM3::Port_7FFD_Out([[maybe_unused]] uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // ZX-Evo BaseConf: the 7FFD lock bit only counts while EFF7 bit 2 (lockmem)
    // holds the memory manager in 128K mode. With lockmem clear (P1024 mode)
    // 7FFD stays writable - bits 5..7 then extend the RAM page number, so a
    // sticky latch would brick the machine after the first P1024 lock write
    // (xpeccy pentevo.c evoOut7FFD: `if ((pEFF7 & 4) && (p7FFD & 0x20)) return;`)
    if (IsPagingLocked())
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
        else if ((port & 0x0C00) == 0x0800)
            Port_BF7_Out(port, value);
        else
            MLOGDEBUG("PortDecoder_ATM3: #%04X unused pager function ignored", port);
        return;
    }

    if (a8 == shadow)
        return;  // A8=0 outside shadow: no function on this board

    // Clock enable as latched before this cycle (gluclock_on uses the registered EFF7)
    const bool gluk = IsGlukEnabled();

    if (!shadow && (port & 0x1000) == 0)
        Port_EFF7_Out(port, value, pc);

    if (gluk && (port & 0x2000) == 0)
        _evoAvr.WriteAddress(value);
    if (gluk && (port & 0x4000) == 0)
    {
        // wait_start_gluclock = gluclock_on && !a[14] && (portf7_rd || portf7_wr) (fpga/base zports.v:754): the
        // Z80 waits on /WAIT until the AVR answers (zwait.v:57-61), as for the COM port
        ConfigureAvrWait();
        _evoAvr.HoldForGlukAccess(_context, false, value);
        _evoAvr.WriteData(value);
    }
}

/// @brief #F7 reads: only the clock data port drives the bus (zports.v:455-460);
///        #EFF7 and the ATM window registers are write-only
uint8_t PortDecoder_ATM3::DecodeF7In(uint16_t port)
{
    if (IsPort_CMOS_Data(port))
    {
        // The read waits for the AVR's answer (zports.v:754, wait_read on the bus :441-442)
        ConfigureAvrWait();
        _evoAvr.HoldForGlukAccess(_context, true, 0xFF);
        return _evoAvr.ReadData();
    }
    return 0xFF;
}

Uart16550::AvrFirmware PortDecoder_ATM3::ConfigureAvrWait()
{
    // The NedoPC firmwares serve the wait ports with zx_wait_task (status, SPI #41 / #42 for the cell, #40); a
    // TS-Labs firmware on the BaseConf FPGA picks zx_wait_task_old, the same path (main.c setup_prepare_runtime_mode)
    const auto firmware = static_cast<Uart16550::AvrFirmware>(_context ? _context->config.atm.evo_avr : 0);
    const bool ts = firmware == Uart16550::AvrFirmware::Ts2013 || firmware == Uart16550::AvrFirmware::Ts2016Feb ||
                    firmware == Uart16550::AvrFirmware::Ts2016Apr;
    const Uart16550::Params p = Uart16550::EvoAvrParams(firmware);
    _evoAvr.SetWaitFirmware(ts ? EvoAvr::WaitHandler::TsOld : EvoAvr::WaitHandler::BaseConf,
                            EvoAvrWait::Timing{p.avrClockHz, p.isrCycles, p.loopCycles, p.waitChecksPerLoop});
    return firmware;
}

/// @brief Border strobe without the beeper / tape bits (#F6, #FC)
/// @details beeper_wr is `loa == #FE` only (zports.v:944): #F6 and #FC set the
///          border and leave MIC/EAR where the last #FE write put them
void PortDecoder_ATM3::BorderOnlyOut(uint16_t port, uint8_t value, uint16_t pc)
{
    const uint8_t keep = static_cast<uint8_t>(_state->pFE & 0xF8);
    Default_Port_FE_Out(port, static_cast<uint8_t>((value & 0x07) | keep), pc);
}

/// region <Board NMI>

bool PortDecoder_ATM3::RequestBoardNmi()
{
    // pending_nmi: released at the next frame INT (Z80::ProcessInterrupts),
    // where OnFrameIntStartNmi() still vetoes it while the NMI page is in
    _state->nmiAtIntStartPending = true;
    return true;
}

bool PortDecoder_ATM3::OnFrameIntStartNmi()
{
    // nmi_count only starts on `nmi_start && !in_nmi`: no nested board NMI
    if (_state->evo.inNmi)
        return false;

    _state->evo.nmiEntry = true;
    return true;
}

bool PortDecoder_ATM3::OnNmiAccepted()
{
    // Only the board's own NMIs page RAM #FF in (in_nmi_2); an /NMI from
    // elsewhere is a plain Z80 NMI at #0066 of whatever is mapped
    if (!_state->evo.nmiEntry)
        return false;

    _state->evo.nmiEntry = false;
    _state->evo.inNmi = true;
    if (_memory)
        _memory->UpdateZ80Banks();
    return true;
}

bool PortDecoder_ATM3::IsDosLeavingBank(uint8_t bank) const
{
    // Pager off: every window is ROM 31
    if (!(_state->atm.aFF77 & ATM_AFF77_PEN))
        return false;

    const unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    return (_state->atm.pFFF7[regSet + (bank & 3)] & 0x100) == 0;  // bit 8 = programmed ROM
}

void PortDecoder_ATM3::OnMachineM1(uint16_t address)
{
    // The refresh of this M1: a clock select written before it takes effect now (zclock.v int_turbo)
    if (_state->evo.turboPending)
    {
        _state->evo.turboPending = 0;
        if (_context->pCore && _context->pCore->GetZ80())
            _context->pCore->GetZ80()->ApplyHardwareTurboNow();
    }

    // NMI exit countdown (znmi.v clr_count / pending_clr)
    if (_state->evo.pBE > 0 && --_state->evo.pBE == 0)
    {
        _state->evo.inNmi = false;
        if (_memory)
            _memory->UpdateZ80Banks();
    }

    // M1 breakpoint (zbreak.v): an immediate NMI, not synchronized to INT,
    // and like every board NMI only while the NMI page is out
    if ((_state->evo.pBF & 0x10) && address == _state->evo.pBD && !_state->evo.inNmi)
    {
        _state->evo.nmiEntry = true;
        if (_context->pCore && _context->pCore->GetZ80())
            _context->pCore->GetZ80()->RequestNonMaskedInterrupt();
    }

    RefreshM1Hook();
}

void PortDecoder_ATM3::RefreshM1Hook()
{
    if (!_context->pCore || !_context->pCore->GetZ80())
        return;

    const bool needed = _state->evo.pBE > 0 || (_state->evo.pBF & 0x10) || (_state->evo.trdemu & kTrdemuPending) ||
                        _state->evo.turboPending;
    Z80* z80 = _context->pCore->GetZ80();
    if (needed)
        z80->machineM1Hook = this;
    else if (z80->machineM1Hook == this)
        z80->machineM1Hook = nullptr;
}

void PortDecoder_ATM3::BeforeMachineM1([[maybe_unused]] uint16_t address)
{
    // zdos.v: in_trdemu is set by the trapped access and seen by the very next
    // opcode fetch. The trapping instruction itself still ran with the ROM in
    // window 0, so its own memory writes (INI...) could not reach page #FE -
    // the RTL's trdemu_wr_disable window
    if (_state->evo.trdemu & kTrdemuPending)
    {
        _state->evo.trdemu = static_cast<uint8_t>((_state->evo.trdemu & ~kTrdemuPending) | kTrdemuIn);
        if (_memory)
            _memory->UpdateZ80Banks();
        RefreshM1Hook();
    }
}

bool PortDecoder_ATM3::TrdemuFdcAccess(uint8_t fdcPort, bool isWrite, uint8_t value)
{
    // vg93.v:177 / zports.v vgFF: every OUT (#FF) in shadow is latched, drive mask or not; #FF reads it back
    const bool systemWrite = isWrite && fdcPort == 0xFF;
    if (systemWrite)
        _state->evo.vgSys = static_cast<uint8_t>(value & 0x3F);

    if (IsLegacyFpga())
        return false;  // the legacy tree has no drive mask

    // The drive number the FPGA compares: for OUT (#FF) the value being written
    // (vg_rdwr_fclk is registered after the write), otherwise the latched one
    const uint8_t drive = static_cast<uint8_t>(_state->evo.vgSys & 0x03);

    const bool masked = (_state->evo.fddMask >> drive) & 0x01;
    if (!masked)
        return false;

    // Trap (zdos.v:61): TR-DOS executing from ROM in window 0, palette-write
    // mode off (#xx77 A14 = 1 -> atm_pen2 = 0); the FDC arm already implies shadow
    const bool dos = (_state->flags & CF_TRDOS) != 0;
    const bool romInWindow0 = _memory && _memory->GetMemoryBankMode(0) == MemoryBankModeEnum::BANK_ROM;
    const bool paletteWriteOff = (_state->atm.aFF77 & ATM_AFF77_PEN2) != 0;
    if (dos && romInWindow0 && paletteWriteOff)
    {
        _state->evo.trdemu |= kTrdemuPending;
        RefreshM1Hook();
    }

    // #FF is the FPGA's own latch and always answers; #1F-#7F would select the chip
    return fdcPort != 0xFF;
}

/// endregion </Board NMI>

/// region <SD card>

namespace
{
    AccessMode AccessOf(SdCardSpi::WriteMode mode)
    {
        switch (mode)
        {
            case SdCardSpi::WriteMode::Persist: return AccessMode::WriteThrough;
            case SdCardSpi::WriteMode::Off: return AccessMode::ReadOnly;
            default: return AccessMode::Session;
        }
    }
}  // namespace

bool PortDecoder_ATM3::InsertSdCard(const std::string& path, SdCardSpi::WriteMode mode, bool writeProtect)
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.access = AccessOf(mode);
        options.writeProtect = writeProtect;
        options.disposition = Disposition::Discard;  // the legacy call always replaced the card
        const MediaResult result = manager->Insert(_sdSlot.Descriptor().id, source, options);
        if (!result.Ok())
            MLOGWARNING("PortDecoder_ATM3: SD card '%s' not inserted: %s", path.c_str(), result.message.c_str());
        return result.Ok();
    }
    const bool inserted = _sdCard.open(path, mode);
    _sdWriteProtect = writeProtect;
    UpdateSdStatus();
    return inserted;
}

bool PortDecoder_ATM3::InsertSdCard(std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode, bool writeProtect)
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.type = MediaSourceType::Blank;
        InsertOptions options;
        options.writeProtect = writeProtect;
        options.disposition = Disposition::Discard;
        auto medium = MediaFormatRegistry::WrapBlock(source, AccessOf(mode), "memory", std::move(media));
        return manager->Insert(_sdSlot.Descriptor().id, std::move(medium), options).Ok();
    }
    const bool inserted = _sdCard.insert(std::move(media), mode);
    _sdWriteProtect = writeProtect;
    UpdateSdStatus();
    return inserted;
}

void PortDecoder_ATM3::EjectSdCard()
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        EjectOptions options;
        options.disposition = Disposition::Discard;  // the programmatic eject of tests and automation wrappers
        manager->Eject(_sdSlot.Descriptor().id, options);
        return;
    }
    _sdCard.close();
    UpdateSdStatus();
}

void PortDecoder_ATM3::UpdateSdStatus()
{
    // AVR register C: b3 card present, b2 write-protected (rtc.c reads the
    // slot's detect and WP switches)
    _evoAvr.SetSdStatus(_sdCard.present(), _sdCard.present() && _sdWriteProtect);
}

PortDecoder_ATM3::EvoSdSlot::EvoSdSlot(PortDecoder_ATM3& owner) : _owner(owner)
{
    _descriptor.id = "sd.zc";
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = "SD card (Z-Controller)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500;  // the ERS and NedoOS poll the card and re-initialize it
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    _descriptor.defaultFs = FatType::Fat16;
    _descriptor.hasCardDetect = true;          // AVR register C bit 3
    _descriptor.hasWriteProtectSwitch = true;  // AVR register C bit 2
    _descriptor.tags = {"sd", "zcontroller", "primary", "boot"};
    _descriptor.aliases = {"sd"};
    _descriptor.guestName = "E: in the ERS and NedoOS (the card's first FAT partition)";
}

void PortDecoder_ATM3::EvoSdSlot::Attach(Medium& medium)
{
    _owner._sdCard.attach(*medium.Block());
    _owner._sdCard.select(_owner._zc.IsSelected());
    _owner.UpdateSdStatus();
}

void PortDecoder_ATM3::EvoSdSlot::Detach()
{
    _owner._sdCard.detach();
    _owner.UpdateSdStatus();
}

bool PortDecoder_ATM3::EvoSdSlot::IsBusy() const
{
    return _owner._sdCard.busy();
}

void PortDecoder_ATM3::EvoSdSlot::SetWriteProtectSwitch(bool on)
{
    _owner._sdWriteProtect = on;
    _owner.UpdateSdStatus();
}

PortDecoder::RtcBinding PortDecoder_ATM3::GetRtcBinding()
{
    RtcBinding binding;
    binding.chip = &_evoAvr;
    binding.ports = "Gluk: #DFF7 address / #BFF7 data after #EFF7 bit 7; #DEF7 / #BEF7 in shadow";
    binding.nvramFile = _context->config.atm.evo_nvram_path;
    return binding;
}

std::vector<ttd::PeripheralId> PortDecoder_ATM3::GetTTDModelStateIds() const
{
    std::vector<ttd::PeripheralId> ids = PortDecoder_ATM710::GetTTDModelStateIds();
    ids.push_back(ttd::PeripheralId::EvoSdCard);
    ids.push_back(ttd::PeripheralId::Ds12887);
    ids.push_back(ttd::PeripheralId::EvoPs2);
    ids.push_back(ttd::PeripheralId::EvoMouse);
    ids.push_back(ttd::PeripheralId::EvoTurboCache);
    ids.push_back(ttd::PeripheralId::EvoFontRam);
    // The AVR's /WAIT timing (main-loop phase, EEPROM write) rides in EvoAvrVolatile; its first bytes repeat what
    // AtmPaging carries for the ATM3 (restored after it, the same values)
    ids.push_back(ttd::PeripheralId::EvoAvrVolatile);
    ids.push_back(ttd::PeripheralId::EvoFlash);
    return ids;
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_ATM3::CreateTTDSerializers() const
{
    auto serializers = PortDecoder_ATM710::CreateTTDSerializers();
    // The serializer reads and restores the live card; the decoder outlives
    // every TTD session (the manager goes before the core)
    auto& self = const_cast<PortDecoder_ATM3&>(*this);
    serializers.push_back(std::make_unique<ttd::TTDEvoSdCard>(self._sdCard, self._zc));
    serializers.push_back(std::make_unique<ttd::TTDDs12887>(const_cast<EvoAvr&>(_evoAvr)));
    serializers.push_back(std::make_unique<ttd::TTDEvoPs2>(const_cast<EvoAvr&>(_evoAvr)));
    serializers.push_back(std::make_unique<ttd::TTDEvoMouse>(const_cast<EvoAvr&>(_evoAvr).Ps2Mouse()));
    serializers.push_back(std::make_unique<ttd::TTDEvoTurboCache>(const_cast<PortDecoder_ATM3&>(*this)));
    serializers.push_back(std::make_unique<ttd::TTDEvoFontRam>(_context));
    serializers.push_back(std::make_unique<ttd::TTDEvoAvrVolatile>(const_cast<EvoAvr&>(_evoAvr)));
    serializers.push_back(std::make_unique<ttd::TTDEvoFlash>(self._flash));
    return serializers;
}

/// endregion </SD card>

/// @brief BaseConf window mapping (fpga/base_trdemu/trunk/mem/atm_pager.v:114-168)
/// @details Priority for window 0: pager off (all windows ROM 31) > NMI (RAM
///          #FF) > #EFF7 bit 3 (RAM page 0) > the page register. A register
///          whose "dos7ffd" bit is set takes its low page bits from #7FFD:
///          128K mode (#EFF7 bit 2 = 1) `{reg[7:3], 7FFD[2:0]}`, Pentagon-1024
///          mode (bit 2 = 0, the reset state) `{reg[7:6], 7FFD[7:5], 7FFD[2:0]}`.
///          A ROM register with the bit set swaps its page LSB for the DOS signal.
void PortDecoder_ATM3::EnterSpectrum128Paging(uint16_t pc)
{
    _state->evo.wrProt = 0x00;
    _state->evo.inNmi = false;
    _state->evo.nmiEntry = false;
    _state->nmiAtIntStartPending = false;
    _state->evo.trdemu = 0;
    _state->pEFF7 = static_cast<uint8_t>((_state->pEFF7 | ATM_EFF7_LOCKMEM) & ~ATM_EFF7_ROCACHE);
    PortDecoder_ATM710::EnterSpectrum128Paging(pc);   // ends in updateMemoryBanks(): this class's mapping
}

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
    if (!(_state->atm.aFF77 & ATM_AFF77_CPM))
        _state->flags |= CF_TRDOS;
    const bool dos = (_state->flags & CF_TRDOS) != 0;

    // #xx77 A8=0: pager off, every window reads the last ROM page
    if (!(_state->atm.aFF77 & ATM_AFF77_PEN))
    {
        for (uint8_t bank = 0; bank < 4; bank++)
            _memory->SetROMPageToBank(bank, romMask);
        SyncFlashWindows();
        return;
    }

    const unsigned regSet = (_state->p7FFD & 0x10) ? 4 : 0;
    const bool oneMegMode = (_state->pEFF7 & ATM_EFF7_LOCKMEM) == 0;
    const uint8_t p7FFD = _state->p7FFD;

    for (uint8_t bank = 0; bank < 4; bank++)
    {
        const unsigned reg = _state->atm.pFFF7[regSet + bank];

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

    // Window 0 overrides: NMI RAM #FF, virtual-TR-DOS RAM #FE (#FF when both,
    // atm_pager.v `page <= {7'h7F, in_nmi}`)
    if (_state->evo.inNmi || (_state->evo.trdemu & kTrdemuIn))
        _memory->SetRAMPageToBank0((_state->evo.inNmi ? 0xFF : 0xFE) & ramMask);
    else if (_state->pEFF7 & ATM_EFF7_ROCACHE)
        _memory->SetRAMPageToBank0(0);

    // #xBF7: a protected RAM window keeps reading and drops its writes. ROM windows drop them already
    if (_state->evo.wrProt)
        for (uint8_t bank = 0; bank < 4; bank++)
            if (!_memory->IsWindowRom(bank) && IsWindowWriteProtected(bank))
                _memory->SetBankWriteProtected(bank);

    SyncFlashWindows();

    // Every state restore (TTD seek, snapshot) re-runs the decode: re-attach
    // the M1 hook the restored NMI / breakpoint state needs
    RefreshM1Hook();
}

/// endregion </Port handlers>

PortDecoder::NetworkCapabilities PortDecoder_ATM3::DescribeNetwork()
{
    NetworkCapabilities caps;
    caps.serialPort = NetworkCapabilities::SerialPort::EvoAvr;
    const Uart16550::AvrFirmware firmware = ConfigureAvrWait();
    caps.uart = Uart16550::EvoAvrParams(firmware);
    caps.avrWait = &_evoAvr.Wait();   // #xxEF and #BFF7 share the AVR's main loop (zwait.v)

    // The BaseConf FPGA hands the AVR A10..A8 (SPI register #42). The TS-Labs
    // firmware from 2016-02 expects the TS-Conf FPGA's full high byte and
    // reads the COM port wrongly here (reference-evo-com-port.md §9):
    //  - 2016-02 .. 2021-04: index 0..7 from #42 lands in its ZiFi data area
    //  - since 2021-04-28: the index is read from #41 = the Gluk clock address
    //    (the last #DFF7 write): F8..FF reach the 16550, C0..CF the ZiFi
    //    registers, D0..F7 nothing, 00..BF the ZiFi data area
    caps.zifi = firmware == Uart16550::AvrFirmware::Ts2016Feb || firmware == Uart16550::AvrFirmware::Ts2016Apr;
    if (firmware == Uart16550::AvrFirmware::Ts2016Feb)
        caps.serialRegister = [](uint16_t) { return ComPortRegister::kDataRegion; };
    else if (firmware == Uart16550::AvrFirmware::Ts2016Apr)
    {
        caps.serialRegister = [this](uint16_t) {
            const uint8_t index = _evoAvr.GetAddress();
            if (index >= 0xF8)
                return static_cast<int>(index - 0xF8);
            if (index >= 0xC0 && index <= 0xCF)
                return ComPortRegister::kZiFiBase + (index - 0xC0);
            if (index >= 0xD0)
                return ComPortRegister::kNothing;
            return ComPortRegister::kDataRegion;
        };
    }
    return caps;
}

