#include "emulator/io/joystick/joystick.h"
#include "stdafx.h"

#include "common/modulelogger.h"
#include "debugger/ttd/engine/ttdconfigfingerprint.h"

#include "portdecoder_profi.h"

#include "debugger/ttd/profi/ttdprofipaging.h"
#include "debugger/ttd/profi/ttdprofixtkbc.h"
#include "debugger/ttd/ttdds12887.h"
#include "debugger/ttd/ttdpit8253.h"
#include "debugger/ttd/ttdppi8255.h"
#include "debugger/ttd/ttdusart8251.h"
#include "emulator/cpu/core.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/profi/profigeometry.h"
#include "emulator/video/screen.h"

namespace
{
    // ZX Profi's own Covox DAC lives on ports #5F (Left), #3F (Right) - distinct
    // physical ports from the Pentagon/Scorpion Soundrive set (#F1/#F3/#F9/#FB).
    // Writes are forwarded into the shared Covox device via its standard
    // Left/Right ports, so the device itself stays model-agnostic.
    constexpr uint8_t kProfiCovoxLeftPort  = 0x5F;
    constexpr uint8_t kProfiCovoxRightPort = 0x3F;

    // CP/M-extended-mode Covox aliases (UnrealSpeccy io.cpp): #1F..#7F belong to the FDC
    // while CP/M mode (IsExtMode()) is active, so the DAC moves here instead. Decode is
    // (port & 0x9F) == 0x87 (covers #87/#A7/#C7/#E7) then (port & 0x60): 0x40 -> Left
    // (#C7), 0x20 -> Right (#A7); 0x00 (#87) and 0x60 (#E7) are unused 8255 control-
    // register addresses - not implemented, matching every reference emulator.
    constexpr uint8_t kProfiCovoxExtMask  = 0x9F;
    constexpr uint8_t kProfiCovoxExtMatch = 0x87;
    constexpr uint8_t kProfiCovoxExtLRMask = 0x60;
    constexpr uint8_t kProfiCovoxExtLeftBits  = 0x40;  // #C7
    constexpr uint8_t kProfiCovoxExtRightBits = 0x20;  // #A7
}

/// region <Constructors / Destructors>

PortDecoder_Profi::PortDecoder_Profi(EmulatorContext* context)
    : PortDecoder(context), _board(ProfiBoard::For(context->config.mem_model))
{
    _rtc.SetEmulatedClock([this]() { return EmulatedMicroseconds(); });
    _rtc.SetSessionWall([this]() { return SessionWallMicros(); });
    // Port A's lines are the Kempston joystick (MAN v3.2 sheet: PA0-PA4 + PB0); nothing else drives the 8255's inputs
    _ppi.SetInputA([this]() { return IsKempstonJoystickFitted() ? Default_Port_KempstonJoystick_In() : uint8_t{0xFF}; });
    FitKeyboard();

    // The COM port (v5): the 8251's TxC / RxC is the 8253's counter 0 output. The 8253 has no RESET pin: its
    // counters start idle at power-on and keep counting through a reset
    _pit.PowerOn(NowBase());
    _usart.Reset(NowBase());
    _usart.SetClockSource([this]() { return Usart8251::ClockRate{_pit.ClockHz(), _pit.OutputPeriod(Pit8253::kCounter0)}; });

    // The VG93 and the tape keep their own clocks while the CPU clock changes (turbo x2, hi-res 6/7 on v3 and
    // ZQ3/14 on v5): without the base-clock time base their time is t_states + CPU T in the frame, which steps
    // back at every frame boundary when the CPU runs faster than 3.5 MHz (by 29952 T in v5 hi-res) and forward
    // when slower (v3 hi-res). The VG93 then reports Lost Data on reads polled through #BF: SP-DOS's boot loader
    // (UNICOPY, KLUG's BBS) retried every sector on a v5 forever
    if (_context->pBetaDisk)
        _context->pBetaDisk->SetBaseClockTimeBase(true);
    if (_context->pTape)
        _context->pTape->SetBaseClockTimeBase(true);
}

void PortDecoder_Profi::FitKeyboard()
{
    const CONFIG& config = _context->config;
    _keyboardKind = ProfiResolveKeyboard(static_cast<ProfiKeyboard>(config.profi_keyboard), config.mem_model);
    if (_keyboardKind == ProfiKeyboard::Matrix)
        return;

    // The PROFI-XT controller (research-profi-keyboard.md sections 2, 5). The firmware image is a reconstruction:
    // the only known dump (CRC 9A8E2686) has no EN I and never calls its get-byte routine, so it receives no key;
    // rom/profixt/profi-xt-v1.27.rom has the 5 bytes at 02Eh..032h replaced by 05 14 5F 00 00 (EN I; CALL 05Fh;
    // NOP; NOP) - data/rom/profixt/README.md. [ROM] PROFIXT= takes a clean re-dump when one turns up
    _xtKbc = std::make_unique<ProfiXtKbc>(_context);
    std::string error;
    const ProfiXtKbc::Engine engine =
        _keyboardKind == ProfiKeyboard::XtTable ? ProfiXtKbc::Engine::Table : ProfiXtKbc::Engine::Firmware;
    if (!_xtKbc->Load(engine, config.profi_xt_rom_path, error))
    {
        // No image: the controller's key table keeps the extra keys working
        MLOGWARNING("PortDecoder_Profi: %s - the PROFI-XT key table is used instead", error.c_str());
        _keyboardKind = ProfiKeyboard::XtTable;
        _xtKbc->Load(ProfiXtKbc::Engine::Table, "", error);
    }
    if (!_xtKbc->ImageNote().empty())
        MLOGWARNING("PortDecoder_Profi: %s", _xtKbc->ImageNote().c_str());
    // X9 takes one keyboard: the host's keys reach the controller alone (Auto route, IPs2KeySink::ReplacesMatrix)
    if (_context->pKeyboard)
        _context->pKeyboard->SetPs2Sink(_xtKbc.get());
}

void PortDecoder_Profi::OnFrameEnd()
{
    if (_xtKbc)
        _xtKbc->OnFrameEnd();
    // The COM line: what the ZX sent reaches the peer, what the peer sent arrives, without waiting for an access
    if (_usart.Peer())
        _usart.Advance(NowBase());
}

uint64_t PortDecoder_Profi::NowBase() const
{
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!z80)
        return _state->t_states;
    return _state->t_states + _state->CpuToBaseT(z80->t);
}

void PortDecoder_Profi::SetSerialPeer(ISerialPeer* peer)
{
    _usart.Advance(NowBase());
    ISerialPeer* old = _usart.Peer();
    if (old && old != peer)
    {
        old->onReceive = nullptr;
        old->SetClock(nullptr, 0);
    }
    if (peer)
    {
        peer->SetClock([this]() { return NowBase(); }, kProfiBaseClockHz);
        // Bytes from outside arrive at their emulated time: the 8251 catches up first
        peer->onReceive = [this]() { _usart.Advance(NowBase()); };
    }
    _usart.SetPeer(peer);
}

PortDecoder::NetworkCapabilities PortDecoder_Profi::DescribeNetwork()
{
    // The v5 board's COM port (an 8251, not a 16550 on #xxEF): the machine's own serial port, as on the ATM Turbo 2+.
    // The v3 board has none
    NetworkCapabilities caps;
    if (!_board.extendedPorts)
        return caps;
    caps.serialPort = NetworkCapabilities::SerialPort::Profi8251;
    caps.attachSerialPeer = [this](ISerialPeer* peer) { SetSerialPeer(peer); };
    caps.serialBaud = [this]() { return _usart.Baud(); };
    return caps;
}

PortDecoder_Profi::~PortDecoder_Profi()
{
    if (_xtKbc && _context->pKeyboard && _context->pKeyboard->GetPs2Sink() == _xtKbc.get())
        _context->pKeyboard->SetPs2Sink(nullptr);
    // The peer outlives the decoder (the network manager owns it): it must not call back into this one
    if (ISerialPeer* peer = _usart.Peer())
    {
        peer->onReceive = nullptr;
        peer->SetClock(nullptr, 0);
    }

    Core* core = _context->pCore;
    if (core && core->GetZ80() && core->GetZ80()->GetMachineStepHook() == this)
        core->GetZ80()->SetMachineStepHook(nullptr);
    if (_waitsInstalled && core)
        core->RemoveBusOverlay(_waitOverlay.get());

    // The next model's decoder decides the time base again
    if (_context->pBetaDisk)
        _context->pBetaDisk->SetBaseClockTimeBase(false);
    if (_context->pTape)
        _context->pTape->SetBaseClockTimeBase(false);

    // Battery-backed state outlives the machine ([PROFI] NvramFile); the v3 board has no clock (_nvramLoaded stays false)
    const char* nvramPath = _context->config.profi_nvram_path;
    if (_nvramLoaded && nvramPath[0] != '\0' && !_rtc.SaveNvram(nvramPath))
        MLOGWARNING("PortDecoder_Profi: cannot save the RTC NVRAM to '%s'", nvramPath);

    MLOGDEBUG("PortDecoder_Profi::~PortDecoder_Profi()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Profi::reset()
{
    EmulatorState& state = _context->emulatorState;

    state.p7FFD = 0x00;     // RAM 0, screen 5, ROM14 = 0, unlocked
    state.pDFFD = 0x00;     // no extended RAM, SCO/SCR/CPM/DS80 off, ROM at #0000
    state.pBFFD = 0x00;     // AY register select
    state.pFFFD = 0x00;     // AY data
    state.pFE = 0xFF;       // border white, no sound
    state.border_attr = 0x07;

    ResetPalette();
    _ppi.Reset();   // RESET reaches the 8255: every port an input
    // RESET reaches the 8251 and clears the COM control register (interrupts off); the 8253 has no RESET input
    _usart.Reset(NowBase());
    _usart.SetBoardLatch(0);

    // The RTC's battery-backed cells come from [PROFI] NvramFile once, at
    // power-on; a Z80 reset does not touch the chip. Only the v5 board has the clock
    if (_board.extendedPorts && !_nvramLoaded)
    {
        _nvramLoaded = true;
        const char* nvramPath = _context->config.profi_nvram_path;
        if (nvramPath[0] != '\0' && !_rtc.LoadNvram(nvramPath))
            MLOGINFO("PortDecoder_Profi: no RTC NVRAM at '%s' yet, starting blank", nvramPath);
    }

    _screen->SetBorderColor(COLOR_WHITE);
    _screen->SetActiveScreen(SCREEN_NORMAL);
    _covoxWasReachable = false;  // DOS latch is on right after reset (see below): a plain TR-DOS
                                  // session, not NORMAL mode and not CP/M-extended mode either

    // Profi boots into the SYS (service / menu) ROM: DOS latch on, ROM14 = 0.
    // SetROMMode raises CF_TRDOS and rebuilds the banks through UpdateZ80Banks(),
    // which calls back UpdateModelMemoryBanks() for the RAM windows.
    Memory& memory = *_context->pMemory;
    memory.SetROMMode(RM_SYS);

    // The TURBO switch is a physical switch: [PROFI] Turbo sets it at power-on, a reset leaves it where it is
    if (!_switchFromConfig)
    {
        _switchFromConfig = true;
        state.profi_turbo_switch = _context->config.profi_turbo ? 1 : 0;
        state.profi_cpm_switch = (_board.palette && _context->config.profi_cpm) ? 1 : 0;
    }
    SyncTurbo();
    SyncWaits();
}

bool PortDecoder_Profi::GetFrontPanelSwitch(FrontPanelSwitch sw) const
{
    if (sw == FrontPanelSwitch::Cpm)
        return _board.palette && _state->profi_cpm_switch != 0;
    return sw == FrontPanelSwitch::Turbo && _state->profi_turbo_switch != 0;
}

bool PortDecoder_Profi::SetFrontPanelSwitch(FrontPanelSwitch sw, bool on)
{
    if (sw == FrontPanelSwitch::Cpm && _board.palette)
    {
        // research-profi-v5-open-items.md Q6: the switch drives the clear input of the #DFFD latches (/ONOFF)
        _state->profi_cpm_switch = on ? 1 : 0;
        if (on && _state->pDFFD != 0)
            ApplyDffd(0);
        return true;
    }
    if (sw != FrontPanelSwitch::Turbo)
        return false;
    _state->profi_turbo_switch = on ? 1 : 0;
    SyncTurbo();
    SyncWaits();
    return true;
}

void PortDecoder_Profi::SyncTurbo()
{
    Core* core = _context->pCore;
    Z80* z80 = core ? core->GetZ80() : nullptr;
    const bool pressed = _state->profi_turbo_switch != 0;

    // v3: the VG93's HLD pin is the board's /TURBO, so a loaded head holds 3.5 MHz; follow it while the switch is
    // pressed (the machine step hook costs nothing otherwise). The v5 board has no such link in its drawings
    const bool followHld = pressed && !_board.palette;
    if (z80)
    {
        if (followHld && z80->GetMachineStepHook() != this)
            z80->SetMachineStepHook(this);
        else if (!followHld && z80->GetMachineStepHook() == this)
            z80->SetMachineStepHook(nullptr);
    }

    const bool headLoaded = followHld && _context->pBetaDisk && _context->pBetaDisk->IsHeadLoaded();
    const bool turbo = pressed && !headLoaded;

    // Hi-res (#DFFD bit 7) switches the sync PROM to its upper half and the CPU to its other crystal at once
    // (design-hires.md): the frame and INT first, so the clock change below rescales against the new geometry
    const bool hires = (_state->pDFFD & 0x80) != 0;
    SyncFrame(hires);
    // The AY clock comes from the same video divider (CLCAY): 1.5 MHz in hi-res unless the v5's SB7 says "new"
    if (_context->pSoundManager)
        _context->pSoundManager->SetPsgClock(ProfiAyClockHz(_board.palette, _context->config.profi_ay_clock_new != 0, hires));

    uint8_t ratio = turbo ? 2 : 1;
    uint8_t den = 1;
    if (hires)
    {
        ratio = ProfiHiresClockNum(_board.palette, ProfiClampZq3(_context->config.profi_zq3_mhz), turbo);
        den = kProfiHiresClockDen;
    }
    const uint8_t currentDen = _state->hw_clock_den > 1 ? _state->hw_clock_den : 1;
    if (_state->hw_turbo_ratio == ratio && currentDen == den)
        return;
    _state->hw_turbo_ratio = ratio;
    _state->hw_clock_den = den;
    if (z80)
        z80->ApplyHardwareTurboNow();
}

void PortDecoder_Profi::SyncFrame(bool hires)
{
    // The sync PROM's lower half in Spectrum mode, its upper half in hi-res (ProfiSyncPromFrameHires): the v3's
    // 0a1d PROM gives 320 lines there, so the frame length itself changes, not only the INT position
    CONFIG& config = _context->config;
    const ProfiSyncProm prom = static_cast<ProfiSyncProm>(config.profi_sync_prom);
    const ProfiFrame f = hires ? ProfiSyncPromFrameHires(prom, config.mem_model) : ProfiSyncPromFrame(prom, config.mem_model);
    const uint32_t intstart = hires ? ProfiHiresIntStart(f) : ProfiIntStart(f);
    if (config.frame == f.frame && config.intstart == intstart && config.intlen == f.intLength)
        return;
    config.frame = f.frame;
    config.t_line = f.tLine;
    config.intstart = intstart;
    config.intlen = f.intLength;
    config.frame_duration_us = CalculateFrameDurationUs(f.frame);
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->RecomputeFrameTiming();
}

uint8_t PortDecoder_Profi::TtdClockUnits() const
{
    return ProfiTtdClockUnits(_board.palette, ProfiClampZq3(_context->config.profi_zq3_mhz));
}

void PortDecoder_Profi::OnMachineStep([[maybe_unused]] uint32_t t)
{
    SyncTurbo();
}

void PortDecoder_Profi::SyncWaits()
{
    Core* core = _context->pCore;
    if (!core || !core->GetZ80() || !_context->pMemory)
        return;

    // v5: the video WAIT at 3.5 MHz (unless SB8 is in its PENTAGON position) and the turbo waits; v3: turbo only
    const CONFIG& config = _context->config;
    const bool pressed = _state->profi_turbo_switch != 0;
    // In hi-res the v5 arbiter waits whatever SB8 says (design-hires.md H3)
    const bool hires = (_state->pDFFD & 0x80) != 0;
    const bool wanted = _board.palette ? (!config.profi_wait_pentagon || pressed || hires) : pressed;
    if (wanted == _waitsInstalled)
        return;

    if (wanted)
    {
        ProfiWaitOverlay::Setup setup;
        setup.v5 = _board.palette;
        setup.phase = config.profi_wait_phase & 0x03;
        setup.pentagonJumper = config.profi_wait_pentagon != 0;
        setup.romWait = config.profi_rom_wait != 0;
        setup.paperStartT = kProfiPaperStartT;
        setup.zq3MHz = ProfiClampZq3(config.profi_zq3_mhz);
        _waitOverlay = std::make_unique<ProfiWaitOverlay>(core, core->GetZ80(), _context->pMemory, _state, setup);
        _waitsInstalled = core->AddBusOverlay(_waitOverlay.get());
        if (!_waitsInstalled)
            MLOGWARNING("PortDecoder_Profi: no room for the wait-state overlay; the board runs without waits");
    }
    else
    {
        core->RemoveBusOverlay(_waitOverlay.get());
        _waitsInstalled = false;
    }
}

uint8_t PortDecoder_Profi::FloatingBusV3Hires(double t3Ns) const
{
    // research-profi-hires-timing.md 4.1: in hi-res U9 and U10 take turns on the bus within each tick of the fetch
    // window (FLD1), which leads the displayed dots by one tick: the first half of a tick U10 (the cell's second
    // fetch), the second half U9 (its first fetch); #FF outside FLD1. Which page each latch holds is not traced (O):
    // this returns the pixel bytes of the cell the window is fetching (M)
    const double rel0 = t3Ns - ProfiHiresWindowStartNs(0);
    if (rel0 < 0)
        return 0xFF;
    const uint32_t line = static_cast<uint32_t>(rel0 / kProfiLineNs);
    if (line >= kProfiHiresPaperLines)
        return 0xFF;
    const double rel = t3Ns - ProfiHiresWindowStartNs(line);
    const uint32_t tick = static_cast<uint32_t>(rel / kProfiHiresTickNs);
    if (tick >= kProfiHiresTicksPerWindow)
        return 0xFF;
    const bool firstHalf = (rel - tick * kProfiHiresTickNs) < kProfiHiresRequestNs;
    const uint32_t byteIndex = tick * 2 + (firstHalf ? 1u : 0u);   // ByteOffset: even = the cell's first byte
    const uint16_t offset = ProfiGeometry::ByteOffset(line, byteIndex);
    return _context->pMemory->RAMPageAddress(ProfiGeometry::PixelPage(_state->p7FFD))[offset];
}

uint8_t PortDecoder_Profi::FloatingBusV3(uint32_t t3) const
{
    // research-profi-v3-turbo-floatbus.md B3: the pixel latch U9 drives the data bus through 820R while FLD1 is
    // high, which leads the displayed paper by one 4-T tick. d = T3 - first displayed pixel of the line:
    //   -4 .. -1: byte 0 (at -4 the latch still holds byte 31 of the previous line)
    //    0 .. 123: byte k = d / 4 at the tick's first T, byte k + 1 after it
    //   otherwise (border, blank, the last tick): #FF from the pull-ups. The attribute latch never reaches the bus
    const int32_t frame = static_cast<int32_t>(_context->config.frame);
    if (frame <= 0)
        return 0xFF;
    const int32_t t = static_cast<int32_t>(t3 % static_cast<uint32_t>(frame));
    const int32_t fromPaper = t - static_cast<int32_t>(kProfiPaperStartT) + 4;   // 0 at the window's first T
    if (fromPaper < 0)
        return 0xFF;
    int32_t line = fromPaper / 224;
    const int32_t inLine = fromPaper % 224;
    if (line >= 192 || inLine >= 128)
        return 0xFF;
    const int32_t d = inLine - 4;   // -4 .. 123
    int32_t byte;
    if (d < 0)
    {
        if (d == -4)
        {
            // The latch still holds byte 31 of the previous paper line (line 191 of the previous frame for line 0)
            line = line == 0 ? 191 : line - 1;
            byte = 31;
        }
        else
            byte = 0;
    }
    else
        byte = (d & 3) == 0 ? d / 4 : d / 4 + 1;
    if (byte > 31)
        return 0xFF;

    const uint8_t page = (_state->p7FFD & 0x08) ? 7 : 5;
    const uint16_t y = static_cast<uint16_t>(line);
    const uint16_t offset = static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | byte);
    return _context->pMemory->RAMPageAddress(page)[offset];
}

bool PortDecoder_Profi::IdeShadowedBySysRegister(uint16_t port) const
{
    // V0.03, TR-DOS with ROM14 = 1: #EB is the system register's alias there, not the IDE's data / command port
    return !IsExtMode() && IsLongBesideShort() && (static_cast<uint8_t>(port) & 0xE3) == 0xE3;
}

IdeAdapter::Gate PortDecoder_Profi::IdeGate()
{
    IdeAdapter::Gate gate = PortDecoder::IdeGate();
    gate.profiExt = IsExtMode() || IsLongBesideShort();
    return gate;
}

uint8_t PortDecoder_Profi::DecodePortIn(uint16_t port, uint16_t pc)
{
    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (uint8_t ideValue = 0xFF; !IdeShadowedBySysRegister(port) && TryIdePortIn(port, pc, ideValue))
        return ideValue;

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

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

    const bool dosPorts = (_state->flags & CF_DOSPORTS) != 0;
    const uint16_t fdcPort = dosPorts ? DecodeFDCPort(port) : 0;

    // AY #FFFD: A15=1, A14=1, A13=1, A1=0. The Profi's AY select decodes A13 too (the v3.2 controller sheet 2:
    // ADR13 and ADR15 into U10:D; the Black_Cat port table, Profi-1 column), so #DFFD (A13=0) never reads the
    // AY. Mirrors with A13=1 select it on IN; resolve them to the canonical port BEFORE the weak FE (A0-only) check.
    if ((port & 0xE002) == 0xE000)
    {
        result = PeripheralPortIn(0xFFFD);
        disp.decodedPort = 0xFFFD;
    }
    // AY #BFFD: A15=1, A14=0, A13=1, A1=0
    else if ((port & 0xE002) == 0xA000)
    {
        result = PeripheralPortIn(0xBFFD);
        disp.decodedPort = 0xBFFD;
    }
    // RTC/CMOS: #9F/#BF/#DF/#FF, EXT mode only - takes priority over the FDC/system-port
    // decode below, since #BF/#FF alias to the Beta128 system port outside EXT mode.
    else if ((port & 0x9F) == 0x9F && LongPortOpen(port))
    {
        // Only the data ports (#9F/#DF, bit 5 = 0) return real data; the address
        // strobe (#BF/#FF) is write-only and reads as floating bus.
        if ((port & 0x20) == 0)
            result = _rtc.ReadData();
        _lastPortDecoded = true;
        disp.decodedPort = port & 0xFF;
    }
    // The COM port (v5): 8253 #8F..#EF, 8251 #D3 / #F3, control register #93 / #B3 - extended map only
    else if (const ComDevice com = ComPortDevice(port); com != ComDevice::None)
    {
        result = ComIn(com, port);
        _lastPortDecoded = true;
        disp.decodedPort = port & 0xFF;
    }
    // WD1793 (Beta128) registers and system port: only while the disk interface is
    // on the bus (DOS latch or CP/M mode, CF_DOSPORTS)
    else if (fdcPort != 0)
    {
        result = PeripheralPortIn(fdcPort);
        disp.decodedPort = fdcPort;
        _lastPortDecoded = true;
    }
    else if (IsFEPort(port))
    {
        result = Default_Port_FE_In(port, pc);
        // The PROFI-XT controller answers on the keyboard lines (and holds the Z80 while it does)
        if (_xtKbc)
        {
            const uint8_t lines = _xtKbc->ReadPort(port);
            if (_board.palette)
                result &= static_cast<uint8_t>(lines | 0xC0);   // v5 X9: KD0..KD5, KD5 = bit 5 (pull-up R10)
            else
            {
                // v3 KEYB: KD0..KD4; no KD5 line (bit 5 reads 1), the controller's DK5 lands on pin 2 = bit 7
                result &= static_cast<uint8_t>(lines | 0xE0);
                if (!(lines & 0x20))
                    result &= 0x7F;
            }
        }
        // GX0 (bit 7): "palette exists" detector read by Profi 5.xx software (UniCopy).
        // Default_Port_FE_In leaves bit 7 = 1 (keyboard/tape never touch it), so only
        // override it in DS80, and only on the v5 board: the v3 has no palette, bit 7 reads 1
        if (_board.fePaletteBit7 && (_state->pDFFD & 0x80))
            result = static_cast<uint8_t>((result & 0x7F) | Port_FE_In_GX0());
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    else if (!dosPorts && (port & 0x00FF) == 0x001F && IsKempstonJoystickFitted())
    {
        // Kempston joystick: #1F in the NORMAL port set (no DOS latch, no CP/M), as the Karabas Pro board decodes
        // it (dos_act = 0, cpm = 0); ahead of the mouse, whose standard decode also matches #xx1F with A9 set
        result = Default_Port_KempstonJoystick_In();
        _lastPortDecoded = true;
        disp.decodedPort = 0x001F;
        disp.wasHandledInline = true;
    }
    // The 8255: #3F (B), #5F (C), #7F (control) outside the DOS / CP/M port set; #87 / #A7 / #C7 / #E7 in the
    // extended map. Port A at #1F is the joystick arm above when one is fitted
    else if (const uint8_t ppiReg = PpiRegister(port, dosPorts); ppiReg != 0xFF)
    {
        result = _ppi.Read(ppiReg);
        _lastPortDecoded = true;
        disp.decodedPort = port & 0xFF;
        disp.wasHandledInline = true;
    }
    else if (uint8_t mouseReg = 0; !dosPorts && Default_IsPort_KempstonMouse(port, mouseReg))
    {
        // Kempston mouse and joystick exist only outside the CP/M / DOS port sets
        result = Default_Port_KempstonMouse_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = port;
        disp.wasHandledInline = true;
    }
    // The VG93's registered keys (#1F..#FF) must not answer outside the DOS / CP/M port set: the gated FDC
    // arm above already declined them, so they stay undecoded (floating bus) instead of reaching the controller
    else if (!IsBeta128Port(port))
    {
        result = PeripheralPortIn(port);
        // Identity decode: mark decoded only when a device actually responded
        if (_lastPortDecoded)
            disp.decodedPort = port;
    }
    // The v3 board's floating bus: an IN (A0 = 1) that no device answers reads the video's pixel latch in the
    // Spectrum raster. The lookup runs at T2 of the I/O cycle; the Z80 takes the data at T3, one T later at 3.5 MHz
    if (!_lastPortDecoded && !_board.palette && (port & 0x0001))
    {
        // Z80::t counts CPU clocks of the scaled frame (x the clock multiplier): T3 starts 2 clocks after T2
        const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
        if (z80 && !(_state->pDFFD & 0x80))
            result = FloatingBusV3(_state->CpuToBaseT(z80->t + 2u));
        else if (z80)
        {
            // Hi-res: the instant in ns at the hi-res clock (num / 7 of 3.5 MHz, host speed stripped)
            const uint32_t num = _state->hw_turbo_ratio_applied ? _state->hw_turbo_ratio_applied : 1u;
            const uint32_t host = _state->HostSpeedMultiplier() ? _state->HostSpeedMultiplier() : 1u;
            result = FloatingBusV3Hires(static_cast<double>((z80->t + 2u) / host) * ProfiHiresCpuPeriodNs(num));
        }
    }

    disp.wasDecoded = _lastPortDecoded;

    OnPortInComplete(port, result, pc, disp);

    return result;
}

void PortDecoder_Profi::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    // The IDE board decodes first (UnrealSpeccy io.cpp order)
    if (!IdeShadowedBySysRegister(port) && TryIdePortOut(port, value, pc))
        return;

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

    const bool dosPorts = (_state->flags & CF_DOSPORTS) != 0;

    // ULA port and Profi palette. Deliberately NOT an else-chain with the paging ports:
    // UnrealSpeccy documents titles that OUT #FC to both #FE and #7FFD.
    if (IsFEPort(port))
    {
        // Palette write uses the PREVIOUS #FE value as index, so it must precede the latch update. The v5 board only
        if (_board.palette && (port & 0x0080) == 0 && (_state->pDFFD & 0x80))
            Port_Palette_Out(port);

        Default_Port_FE_Out(port, value, pc);
        disp.decodedPort = 0x00FE;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    const bool dffd = DffdAnswers(port);
    if (IsPort_7FFD(port))
    {
        Port_7FFD(value, pc);
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        // DffdDecode=v50: the 5.0 board decodes #DFFD from A13 and A1 alone, so #1FFD-style ports write both
        if (dffd)
            Port_DFFD(value, pc);
    }
    else if (dffd)
    {
        Port_DFFD(value, pc);
        disp.decodedPort = 0xDFFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    // AY #FFFD: A15=1, A14=1, A13=1, A1=0 (register select / TurboSound chip select); A13=0 is #DFFD, above
    else if ((port & 0xE002) == 0xE000)
    {
        _state->pFFFD = value;
        PeripheralPortOut(0xFFFD, value);
        disp.decodedPort = 0xFFFD;
        disp.wasDecoded = true;
    }
    // AY #BFFD: A15=1, A14=0, A13=1, A1=0 (data write)
    else if ((port & 0xE002) == 0xA000)
    {
        _state->pBFFD = value;
        PeripheralPortOut(0xBFFD, value);
        disp.decodedPort = 0xBFFD;
        disp.wasDecoded = true;
    }
    // RTC/CMOS: #9F/#BF/#DF/#FF, EXT mode only - takes priority over the FDC/system-port
    // decode below, since #BF/#FF alias to the Beta128 system port outside EXT mode.
    else if ((port & 0x9F) == 0x9F && LongPortOpen(port))
    {
        // Bit 5 set (#BF/#FF) latches the register address; clear (#9F/#DF) writes data.
        if (port & 0x20)
            _rtc.WriteAddress(value);
        else
            _rtc.WriteData(value);
        disp.decodedPort = port & 0xFF;
        disp.wasDecoded = true;
    }
    else if (const ComDevice com = ComPortDevice(port); com != ComDevice::None)
    {
        ComOut(com, port, value);
        disp.decodedPort = port & 0xFF;
        disp.wasDecoded = true;
    }
    else if (dosPorts)
    {
        const uint16_t fdcPort = DecodeFDCPort(port);
        if (fdcPort != 0)
        {
            PeripheralPortOut(fdcPort, value);
            disp.decodedPort = fdcPort;
            disp.wasDecoded = true;
        }
        // Covox/SoundRive DAC, CP/M-extended-mode aliases (#C7 Left, #A7 Right): real
        // Profi hardware moves the DAC here because the FDC has taken #1F..#7F away from
        // it. See the kProfiCovoxExt* constants above for the decode.
        else if (LongPortOpen(port))
        {
            const uint8_t lowByte = static_cast<uint8_t>(port);
            const uint8_t lrBits = lowByte & kProfiCovoxExtLRMask;
            if (const uint8_t ppiReg = PpiRegister(port, dosPorts); ppiReg != 0xFF)
            {
                _ppi.Write(ppiReg, value);
                disp.decodedPort = lowByte;
                disp.wasDecoded = true;
            }
            if ((lowByte & kProfiCovoxExtMask) == kProfiCovoxExtMatch &&
                (lrBits == kProfiCovoxExtLeftBits || lrBits == kProfiCovoxExtRightBits))
            {
                if (_context->pSoundManager && _context->pSoundManager->hasCovox())
                {
                    // Use the A ports, not B - see the NORMAL-mode branch below for why.
                    uint16_t canonicalPort = (lrBits == kProfiCovoxExtLeftBits) ? Covox::PORT_LEFT_A : Covox::PORT_RIGHT_A;
                    _context->pSoundManager->getCovox()->portDeviceOutMethod(canonicalPort, value);
                }
                disp.decodedPort = lowByte;
                disp.wasDecoded = true;
            }
        }
    }
    // Covox/SoundRive DAC: #5F (Left), #3F (Right). NORMAL mode only - the FDC/CP'M
    // port set (dosPorts above) takes priority when the disk interface is on the bus;
    // the CP/M-extended-mode aliases (#C7/#A7) are handled inside that branch instead.
    else if (const uint8_t lowByte = port & 0xFF; PpiRegister(port, dosPorts) != 0xFF)
    {
        _ppi.Write(PpiRegister(port, dosPorts), value);
        // The Covox DAC follows every write to B / C, whatever the 8255's mode (as Karabas Pro's covox.vhd latches
        // it): the Profi Covox software never programs the 8255, and no BIOS sets it to output
        if ((lowByte == kProfiCovoxLeftPort || lowByte == kProfiCovoxRightPort) && _context->pSoundManager &&
            _context->pSoundManager->hasCovox())
        {
            // Use the A ports, not B: computeStereoAmplitudes()'s mono-compatibility
            // fallback keys on LeftA/LeftB/RightA==0 and then substitutes RightB into
            // BOTH channels. Profi never touches LeftB/RightB, so routing through them
            // left the fallback armed on LeftA/RightA alone - every time Left passed
            // through exact silence while Right was active, it leaked Right into Left.
            uint16_t canonicalPort = (lowByte == kProfiCovoxLeftPort) ? Covox::PORT_LEFT_A : Covox::PORT_RIGHT_A;
            _context->pSoundManager->getCovox()->portDeviceOutMethod(canonicalPort, value);
        }
        disp.decodedPort = lowByte;
        disp.wasDecoded = true;
    }

    // Silence Covox exactly once when it becomes completely unreachable: a TR-DOS/Beta128
    // FDC session with no CP/M-extended-mode alias available (dosPorts above may be stale
    // if THIS instruction is the 7FFD/DFFD write that caused the transition, so re-read
    // both flags fresh). Entering CP/M-extended mode (IsExtMode()) does NOT silence it -
    // the DAC just moves from #5F/#3F to #C7/#A7, it never loses the bus.
    // Covox has no idle timeout: with the bus taken away, its DAC latches would otherwise
    // hold their last written level forever - inaudible on real hardware (AC-coupled
    // output stage) but a permanent stuck tone through a digital audio pipeline.
    const bool covoxReachableNow = !(_state->flags & CF_DOSPORTS) || IsExtMode() || IsLongBesideShort();
    if (!covoxReachableNow && _covoxWasReachable)
    {
        if (_context->pSoundManager && _context->pSoundManager->hasCovox())
        {
            Covox* covox = _context->pSoundManager->getCovox();
            covox->portDeviceOutMethod(Covox::PORT_LEFT_A, 0x80);
            covox->portDeviceOutMethod(Covox::PORT_RIGHT_A, 0x80);
        }
        _covoxWasReachable = false;
    }
    else if (covoxReachableNow)
    {
        _covoxWasReachable = true;
    }

    // Universal handler for breakpoints, tracking, analyzers
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_Profi::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Profi::SetROMPage(uint8_t page)
{
    (void)page;
}

/// Latch-to-bank translation. Port of the UnrealSpeccy set_banks() MM_PROFI case:
///   page = (DFFD[2:0] << 3 | 7FFD[2:0]) & ramMask
///   SCO=0: #4000 = 5, #C000 = page      SCO=1: #4000 = page, #C000 = 7
///   SCR=1: #8000 = 6 (else 2)
///   WOROM: RAM page 0 at #0000
///   CPM:   disk interface ports on the bus (CF_DOSPORTS)
/// The ROM slot at #0000 has already been chosen by Memory::UpdateZ80Banks() from CF_TRDOS and 7FFD.4.
void PortDecoder_Profi::UpdateModelMemoryBanks()
{
    if (!_memory)
        return;

    const CONFIG& config = _context->config;

    const uint16_t ramPages = config.ramsize ? static_cast<uint16_t>(config.ramsize / 16) : MAX_RAM_PAGES;
    const uint16_t ramMask = static_cast<uint16_t>(ramPages - 1);

    const uint8_t p7FFD = _state->p7FFD;
    const uint8_t pDFFD = _state->pDFFD;

    uint16_t page = static_cast<uint16_t>(((pDFFD & 0x07) << 3) | (p7FFD & 0x07)) & ramMask;
    uint16_t bank1 = 5;
    uint16_t bank2 = (pDFFD & 0x40) ? 6 : 2;
    uint16_t bank3 = page;

    if (pDFFD & 0x08)   // SCO: swap the roles of #4000 and #C000
    {
        bank1 = page;
        bank3 = 7;
    }

    _memory->SetRAMPageToBank1(bank1);
    _memory->SetRAMPageToBank2(bank2);
    _memory->SetRAMPageToBank3(bank3);

    if (pDFFD & 0x10)   // WOROM: RAM instead of ROM at #0000
        _memory->SetRAMPageToBank0(0);

    if (pDFFD & 0x20)   // CPM: disk interface on the bus regardless of the DOS latch
        _state->flags |= CF_DOSPORTS;

    // A TTD restore re-runs the paging decode after loading the switch: bring the clock and the waits in line
    SyncTurbo();
    SyncWaits();
}

PortDecoder::RtcBinding PortDecoder_Profi::GetRtcBinding()
{
    if (!_board.extendedPorts)
        return {nullptr, "", "", "The Profi v3 board has no CMOS clock (it came with controller v4.0)"};

    RtcBinding binding;
    binding.chip = &_rtc;
    binding.ports = "#BF / #FF address, #9F / #DF data, extended mode only (CP/M + ROM14)";
    binding.nvramFile = _context->config.profi_nvram_path;
    return binding;
}

std::vector<ttd::PeripheralId> PortDecoder_Profi::GetTTDModelStateIds() const
{
    // The clock is on the v5 board only; the PROFI-XT controller when fitted
    std::vector<ttd::PeripheralId> ids{ttd::PeripheralId::ProfiPaging, ttd::PeripheralId::Ppi8255};
    if (_board.extendedPorts)
    {
        ids.push_back(ttd::PeripheralId::Ds12887);
        ids.push_back(ttd::PeripheralId::Pit8253);     // the COM port's baud timer
        ids.push_back(ttd::PeripheralId::Usart8251);   // the COM port (its peer: MachineSerialPeer)
    }
    if (_xtKbc)
        ids.push_back(ttd::PeripheralId::ProfiXtKbc);
    return ids;
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Profi::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDProfiPaging>(_context));
    serializers.push_back(std::make_unique<ttd::TTDPpi8255>(const_cast<Ppi8255&>(_ppi)));
    if (_board.extendedPorts)
    {
        serializers.push_back(std::make_unique<ttd::TTDDs12887>(const_cast<Ds12887&>(_rtc)));
        serializers.push_back(std::make_unique<ttd::TTDPit8253>(const_cast<Pit8253&>(_pit)));
        serializers.push_back(std::make_unique<ttd::TTDUsart8251>(const_cast<Usart8251&>(_usart)));
    }
    if (_xtKbc)
        serializers.push_back(std::make_unique<ttd::TTDProfiXtKbc>(*_xtKbc));
    return serializers;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Profi::IsPort_7FFD(uint16_t port)
{
    // Profi 7FFD: /IORQ /WR /A15 /A1 (A2 not decoded; UnrealSpeccy, ZXMAK2 0x8002/0x7FFD, Xpeccy 0x8002)
    static const uint16_t mask = 0b1000'0000'0000'0010;
    static const uint16_t match = 0b0000'0000'0000'0000;
    return (port & mask) == match;
}

bool PortDecoder_Profi::IsPort_DFFD(uint16_t port)
{
    // Profi DFFD: A15=1, A13=0, A1=0. UnrealSpeccy reaches it only after the 7FFD
    // block (A15=0) has returned, so A15=1 is implied there; making it explicit
    // here prevents #5FFD / #1FFD from hitting both handlers.
    static const uint16_t mask = 0b1010'0000'0000'0010;
    static const uint16_t match = 0b1000'0000'0000'0000;
    return (port & mask) == match;
}

uint8_t PortDecoder_Profi::PpiRegister(uint16_t port, bool dosPorts) const
{
    // The 8255's register (A6 A5 = A1 A0 of the chip) for an address it answers, else #FF. Outside the DOS / CP/M
    // port set: #1F / #3F / #5F / #7F (the joystick read of #1F stays its own arm). In the extended map: the same
    // registers at #87 / #A7 / #C7 / #E7 (A7 = 1, A2 = 1: decoder-prom.md, the v5 PROM)
    const uint8_t low = static_cast<uint8_t>(port);
    if (!dosPorts && (low & 0x9F) == 0x1F)
        return static_cast<uint8_t>((low >> 5) & 0x03);
    if (dosPorts && (low & 0x9F) == 0x87 && LongPortOpen(port))
        return static_cast<uint8_t>((low >> 5) & 0x03);
    return 0xFF;
}

PortDecoder_Profi::ComDevice PortDecoder_Profi::ComPortDevice(uint16_t port) const
{
    // The extended map's COM group (Concurrent BIOS port list, PLUSDOC bios2.txt pp. 13-14): A7 = 1, A4 = 0, A3..A0 =
    // 1111 the 8253 (A6 A5 its register), 0011 the 8251 (A6 = 1, A5 = C/D) or the control register (A6 = 0)
    const uint8_t low = static_cast<uint8_t>(port);
    const uint8_t group = static_cast<uint8_t>(low & 0x9F);
    if (group != 0x8F && group != 0x93)
        return ComDevice::None;
    if (!LongPortOpen(port))
        return ComDevice::None;
    if (group == 0x8F)
        return ComDevice::Pit;
    return (low & 0x40) ? ComDevice::Usart : ComDevice::Control;
}

uint8_t PortDecoder_Profi::ComIn(ComDevice device, uint16_t port)
{
    const uint64_t now = NowBase();
    switch (device)
    {
        case ComDevice::Pit:
            return _pit.Read(static_cast<uint8_t>((port >> 5) & 0x03), now);
        case ComDevice::Usart:
            return _usart.Read((port & 0x20) ? Usart8251::kControl : Usart8251::kData, now);
        case ComDevice::Control:
        {
            // D0 RI, D7 DCD of the connector (1 = asserted; the polarity is not documented), the rest float
            _usart.Advance(now);
            uint8_t value = 0x7E;
            if (_usart.RiIn())
                value |= 0x01;
            if (_usart.DcdIn())
                value |= 0x80;
            return value;
        }
        default:
            return 0xFF;
    }
}

void PortDecoder_Profi::ComOut(ComDevice device, uint16_t port, uint8_t value)
{
    const uint64_t now = NowBase();
    switch (device)
    {
        case ComDevice::Pit:
            // The 8251's clock changes with counter 0: the line moves up to now at the old rate first
            _usart.Advance(now);
            _pit.Write(static_cast<uint8_t>((port >> 5) & 0x03), value, now);
            break;
        case ComDevice::Usart:
            _usart.Write((port & 0x20) ? Usart8251::kControl : Usart8251::kData, value, now);
            break;
        case ComDevice::Control:
            // D0: the COM interrupt enable. The interrupt controller behind it (RST #20 receive, RST #28 transmit
            // on the bus: PLUSDOC comport.txt) is not modeled: the latch is kept, no INT is raised
            _usart.SetBoardLatch(static_cast<uint8_t>(value & 0x01));
            break;
        default:
            break;
    }
}

bool PortDecoder_Profi::IsLongBesideShort() const
{
    // Djoni's V0.03 PROM (docs/inprogress/2026-10-04-profi-plus/design.md, tools/machines/profi/profidecoder): in the
    // TR-DOS state with the 48 ROM page (DOS latch on, CP/M off, ROM14 = 1) the stock VG93 stays at #1F..#7F and
    // the extended group answers at ADR7 = 1 (VG93 #83 #A3 #C3, 8255 #87 #A7 #C7, IDE #8B #AB #CB, RTC #9F #BF
    // #DF); #E3.. stay the system register
    if (_context->config.profi_ext_ports != 2 || !_board.extendedPorts)
        return false;
    return (_state->flags & CF_TRDOS) && !(_state->pDFFD & 0x20) && (_state->p7FFD & 0x10);
}

bool PortDecoder_Profi::LongPortOpen(uint16_t port) const
{
    if (IsExtMode())
        return true;
    return IsLongBesideShort() && (static_cast<uint8_t>(port) & 0xE3) != 0xE3;
}

bool PortDecoder_Profi::IsExtMode() const
{
    // The extended port map: CP/M and ROM14, on the v5 board only. The v5 port decoder PROM confirms it
    // (docs/inprogress/2026-10-01-profi-v3-v5/decoder-prom.md): it needs CP/M = 1 and ROM14 = 1, whatever the DOS
    // latch says. Karabas also opens it to the SYS ROM (DOS latch on, ROM14 = 0); the PROM does not, so that is a
    // clone extension. The v3.2 PROM has ADR15 where v5 has ROM14, so v3 has no extended map at all
    if (!_board.extendedPorts)
        return false;
    const bool cpm = (_state->pDFFD & 0x20) != 0;
    const bool rom14 = (_state->p7FFD & 0x10) != 0;
    // [PROFI] ExtPorts=sys: Karabas Pro's decode (karabas-pro.vhd: "(cpm='1' and rom14='1') or (dos_act='1' and
    // rom14='0')") also opens the map while the SYS ROM runs (DOS latch on, ROM14 = 0). ROM BIOS Plus and PQ-DOS
    // (Vadim / Star Software) probe the FDC, RTC and IDE there; BIOS 1.0 / 2.0 instead use the VG93 at #1F..#7F
    // from the SYS ROM and cannot boot a disk with it (docs/inprogress/2026-10-01-profi-v3-v5/software-zoo.md)
    if (_context->config.profi_ext_ports == 1 && (_state->flags & CF_TRDOS) && !rom14)
        return true;
    // ExtPorts=v003 (Djoni's PROM): the same SYS ROM rule, but the PROM forces A2 = 1 in CP/M mode, so the DOS latch
    // only counts with CP/M off (in CP/M mode the table is the stock CP/M map)
    if (_context->config.profi_ext_ports == 2 && (_state->flags & CF_TRDOS) && !cpm && !rom14)
        return true;
    return cpm && rom14;
}

uint16_t PortDecoder_Profi::DecodeFDCPort(uint16_t port) const
{
    const uint8_t p1 = static_cast<uint8_t>(port);
    const bool cpm = (_state->pDFFD & 0x20) != 0;

    if (IsExtMode())
    {
        // "Modified" (extended) ports: #83/#A3/#C3/#E3 and #3F (system) - UnrealSpeccy io.cpp
        if ((p1 & 0x9F) == 0x83)
            return static_cast<uint16_t>((p1 & 0x60) | 0x1F);
        if ((p1 & 0xE3) == 0x23)
            return 0x00FF;
        return 0;
    }

    // BDI ports: #1F/#3F/#5F/#7F (A7=0, A1:0=11) and the system port #FF (#BF in CP/M mode). On v3 this is the
    // only map: CP/M with ROM14 = 1 decodes the same ports (the v3.2 decoder PROM has no ROM14 input)
    if ((p1 & 0x83) == 0x03)
        return static_cast<uint16_t>((p1 & 0x60) | 0x1F);
    if ((p1 & 0xE3) == (cpm ? 0xA3 : 0xE3))
        return 0x00FF;
    if ((p1 & 0x9F) == 0x83 && LongPortOpen(port))
        return static_cast<uint16_t>((p1 & 0x60) | 0x1F);

    return 0;
}

void PortDecoder_Profi::Port_Palette_Out(uint16_t port)
{
    // colour = ~A15..A8 (data bus is not used), index = (previous #FE value ^ 0xF) & 0xF.
    // Karabas video.vhd:205-208: palette[idx] <= (not A15..A8) & BORDER(7), where BORDER is the
    // #FE register latched by the PRECEDING #FE OUT. That gives a 9-bit entry: bits 8:6 G, 5:3 R,
    // 2:1 B from the address bus (the old 3-3-2 GGGRRRBB byte, shifted up by one), bit 0 the extra
    // blue LSB carried in #FE.D7 - so blue ends up 3-bit like G/R (GGGRRRBBB, VID:62-65/231-233).
    const uint8_t index = static_cast<uint8_t>((_state->pFE ^ 0x0F) & 0x0F);
    const uint16_t colour = static_cast<uint16_t>(static_cast<uint8_t>(~(port >> 8)));
    const uint16_t blueLsb = (_state->pFE & 0x80) ? 0x01 : 0x00;
    _state->profiPalette[index] = static_cast<uint16_t>((colour << 1) | blueLsb);
    if (_context->pScreen)
        _context->pScreen->NoteVideoTableWrite(videomap::VideoTable::Palette, index);  // the video change log
}

uint8_t PortDecoder_Profi::Port_FE_In_GX0() const
{
    // Karabas video.vhd:219: GX0 = palette(idx)(6) xor palette(idx)(0) in DS80, else 1.
    // idx is the same (previous #FE value ^ 0xF) index the palette write uses; bit 6 of our
    // 9-bit entry is G's LSB, bit 0 is the extra blue LSB (see Port_Palette_Out).
    const uint8_t index = static_cast<uint8_t>((_state->pFE ^ 0x0F) & 0x0F);
    const uint16_t entry = _state->profiPalette[index];
    const bool gx0 = ((entry >> 6) ^ entry) & 0x01;
    return gx0 ? 0x80 : 0x00;
}

void PortDecoder_Profi::ResetPalette()
{
    // Standard 16 Spectrum colours, index = {bright, G, R, B}. Karabas reset defaults
    // (video.vhd:199-203): each active channel is level 4/7 non-bright, 6/7 bright, out of a
    // 3-bit (0..7) range - now that blue is 3-bit too (see Port_Palette_Out) all three channels
    // share the same levels.
    for (uint8_t i = 0; i < 16; i++)
    {
        const bool bright = (i & 0x08) != 0;
        const uint16_t level = bright ? 0x06 : 0x04;
        const uint16_t green = (i & 0x04) ? level : 0x00;
        const uint16_t red = (i & 0x02) ? level : 0x00;
        const uint16_t blue = (i & 0x01) ? level : 0x00;
        _state->profiPalette[i] = static_cast<uint16_t>((green << 6) | (red << 3) | blue);
    }
}

/// endregion <Helper methods>

/// Port #7FFD (128K paging) handler
void PortDecoder_Profi::Port_7FFD(uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // Lock (bit 5) blocks every bit of the write, including the screen bit,
    // unless DFFD.4 (WOROM) lifts it (UnrealSpeccy io.cpp; all reviewed emulators agree)
    if (IsPagingLocked())
        return;

    _state->p7FFD = value;

    _screen->SetActiveScreen((value & 0x08) ? SCREEN_SHADOW : SCREEN_NORMAL);

    // ROM slot and RAM windows are derived from the latches
    _memory->UpdateZ80Banks();

    MLOGDEBUG(_memory->DumpMemoryBankInfo());
}

/// Port #DFFD (Profi extended paging / mode) handler
bool PortDecoder_Profi::DffdAnswers(uint16_t port) const
{
    // research-profi-v5-open-items.md, "Emulator rule (palette)": the boards differ from the emulators' decode
    switch (_context->config.profi_dffd_decode)
    {
        case 1:   // v5.0: A13=0, A1=0; A15 is not decoded
            return (port & 0x2002) == 0;
        case 2:   // v5.06: high byte #DF, A1=0, and an OUT (n),A never writes it (DD75 /BLOCK)
        {
            if ((port & 0xFF02) != 0xDF00)
                return false;
            const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
            return !z80 || z80->opcode != 0xD3;
        }
        default:
            return IsPort_DFFD(port);
    }
}

void PortDecoder_Profi::Port_DFFD(uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // The CP/M switch holds the latches cleared: the write is lost
    if (_state->profi_cpm_switch)
        return;
    ApplyDffd(value);
}

void PortDecoder_Profi::ApplyDffd(uint8_t value)
{
    const uint8_t changed = _state->pDFFD ^ value;
    _state->pDFFD = value;

    _memory->UpdateZ80Banks();

    // DS80 selects the 512x240 hi-res raster (Screen::DetectModeProfi)
    if (changed & 0x80)
        _context->pScreen->InitRaster();
}

void PortDecoder_Profi::AddTTDBoardSettings(ttd::TTDConfigFingerprint& fp) const
{
    const CONFIG& c = _context->config;
    fp.Add("profi.sync_prom", c.profi_sync_prom);
    fp.Add("profi.wait_phase", c.profi_wait_phase);
    fp.Add("profi.wait_pentagon", c.profi_wait_pentagon);
    fp.Add("profi.rom_wait", c.profi_rom_wait);
    fp.Add("profi.turbo", c.profi_turbo);
    fp.Add("profi.cpm", c.profi_cpm);
    fp.Add("profi.dffd_decode", c.profi_dffd_decode);
    fp.Add("profi.ext_ports", c.profi_ext_ports);   // cpm / sys / v003: a recording made under another decode is refused
}
ProfiKeyboard ProfiKeyboardInForce(const EmulatorContext* context)
{
    const auto* decoder = context ? dynamic_cast<const PortDecoder_Profi*>(context->pPortDecoder) : nullptr;
    return decoder ? decoder->GetKeyboardKind() : ProfiKeyboard::Default;
}

