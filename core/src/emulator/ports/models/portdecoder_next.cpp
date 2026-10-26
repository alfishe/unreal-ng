#include "emulator/io/mouse/mouse.h"
#include "stdafx.h"

#include "portdecoder_next.h"

#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"

#include "common/modulelogger.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/next/ttdnextstate.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/io/z80n/nexttiming.h"
#include <filesystem>

#include "common/filehelper.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/audio.h"
#include "emulator/video/next/screennext.h"
#include "emulator/video/ulacontention.h"

PortDecoder_Next::PortDecoder_Next(EmulatorContext* context) : PortDecoder_Spectrum128(context)
{
    _board = std::make_unique<NextBoard>(&Mem());
    _board->SetMachine(this);
    // The Next's keyboard controller: the physical host keys reach the extended keys and the pad's extra buttons (NR #B0-#B2)
    if (_context->pKeyboard)
    {
        _context->pKeyboard->SetPs2Sink(&_extendedInput);
        _context->pKeyboard->SetExtendedKeySink(&_extendedInput);
    }
    _extendedInput.SetCancelMatrixEntries([this]() { return (_board->Stored(0x68) & 0x10) != 0; });
    _extendedInput.SetClock([this]() { return Now28(); });
    // the picture follows the beam: a video register written by the program applies from the line the beam is on
    // (the copper first: its MOVEs that are due come before the program's write, as the bus gives the copper the clock it asks for)
    _board->SetBeforeCpuWrite([this]() {
        _board->Copper().Sync();
        if (_context->pScreen)
            _context->pScreen->UpdateScreen();
        if (_screenNext)
            _screenNext->SpriteStateWillChange();  // the rows the sprite engine has filled keep what they saw
    });
    // The copper is the machine's: it runs after every instruction, its steps are watched for breakpoints, its clock and its picture hook
    // come from the raster
    _board->Copper().SetObserver([this](const NextCopper::Step& step) { return OnCopperStep(step); });
    if (ScreenNext* screen = _context->pScreen ? dynamic_cast<ScreenNext*>(_context->pScreen) : nullptr)
        screen->BindCopper(_board->Copper());
    _screenNext = _context->pScreen ? dynamic_cast<ScreenNext*>(_context->pScreen) : nullptr;
    _interrupts = std::make_unique<NextInterruptSource>(_context);
    _interrupts->ctcEnables = [this]() { return _ctc.InterruptEnables(); };
    _interrupts->setCtcEnables = [this](uint8_t bits) { _ctc.SetInterruptEnables(bits); };
    _interrupts->onStacklessNmi = [this](bool on) {
        if (_engine)
            _engine->SetStacklessNmi(on, _interrupts.get());
    };
    _ctc.onInterrupt = [this](unsigned channel) {
        _interrupts->Raise(static_cast<NextInterruptSource::Source>(NextInterruptSource::kCtc0 + channel));
    };
    _ctc.onZeroCount = [this](unsigned channel) {
        // joy_iomode_pin7, mode 01: ZC/TO(3) toggles the pin; with NR #0B bit 0 clear it only goes from 0 to 1 and stays
        const uint8_t nr0b = _board->Stored(0x0B);
        if (channel == 3 && (nr0b & 0x30) == 0x10 && ((nr0b & 0x01) || !_joyPin7))
            _joyPin7 = !_joyPin7;
    };
    _uart.onInterrupt = [this](unsigned channel, bool tx) {
        const unsigned source = tx ? (channel ? NextInterruptSource::kUart1Tx : NextInterruptSource::kUart0Tx)
                                   : (channel ? NextInterruptSource::kUart1Rx : NextInterruptSource::kUart0Rx);
        _interrupts->Raise(static_cast<NextInterruptSource::Source>(source));
    };
    _board->SetJournalClock([this](uint32_t& frame, uint32_t& t, uint16_t& pc) {
        frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
        if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
        {
            t = z80->t;
            pc = z80->m1_pc;
        }
    });
    // NextREG breakpoints: the board tells every access of the program with its door (the single choke points)
    _board->SetAccessHook([this](bool write, uint8_t reg, uint8_t value, NextRegSource source) { NextRegAccess(write, reg, value, source); });
    _interrupts->onEvent = [this](NextIntKind kind, const char* source, bool hasVector, uint8_t vector) {
        if (!_intJournal.Enabled())
            return;
        NextIntEvent e;
        e.kind = kind;
        e.source = source;
        e.hasVector = hasVector;
        e.vector = vector;
        RecordInt(e);
    };
    _divMmc = std::make_unique<NextDivMmc>(&Mem(), _board.get());
    _divMmc->isBasicRomPaged = [this]() { return Mem().BasicRomVisible(); };
    _divMmc->fillClock = [this](NextDivMmcEvent& e) {
        e.frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
        if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
        {
            e.t = z80->t;
            e.pc = z80->m1_pc;
        }
    };
    // An entry at #0008: the RST has pushed its return address, the call number is the byte there (esxDOS: RST $08 / DEFB call)
    _divMmc->fillCall = [this](NextDivMmcEvent& e) {
        Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
        if (!z80)
            return;
        e.af = z80->af;
        e.bc = z80->bc;
        e.de = z80->de;
        e.hl = z80->hl;
        const uint16_t sp = z80->sp;
        e.returnAddress = static_cast<uint16_t>(Mem().DirectReadFromZ80Memory(sp) | (Mem().DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1)) << 8));
        e.call = Mem().DirectReadFromZ80Memory(e.returnAddress);
        e.basicRom = Mem().BasicRomVisible();
    };
    _multiface = std::make_unique<NextMultiface>(&Mem(), _board.get(), &_context->emulatorState);
    _m1Chain.owner = this;
    _m1Chain.multiface = _multiface.get();
    _m1Chain.divMmc = _divMmc.get();
    _interrupts->SetPoller([this]() {
        const uint64_t now = Now28();
        _ctc.Advance(now);
        if (!_uart.Idle())
        {
            SyncPiLine();
            // a UART with a silent peer (the ESP module is always fitted) only notes the look
            if (_uart.Quiet())
                _uart.Look(now);
            else
                _uart.Advance(now);
        }
        _uart.PollInterrupts(_interrupts->UartEnables());
    });
    _board->SetInterrupts(_interrupts.get());
    // The Next's own sound: three AYs and the DAC, mixed by SoundManager (the Spectrum 128 TurboSound stays silent)
    _audio = std::make_unique<NextAudio>(_context);
    _audio->SetTimeSource([this]() { return Now(); });
    if (_context->pSoundManager)
        _context->pSoundManager->attachModelAudioSource(_audio.get());
    for (unsigned i = 0; i < 2; i++)
    {
        const std::string id = Slot(i).Descriptor().id;
        _sd[i].setWriteListener([this, id](uint64_t) {
            if (_context->pMediaManager)
                _context->pMediaManager->NoteWrite(id);
        });
        _sd[i].setTraceListener([this, i](const SdTraceEvent& event) { RecordSdEvent(i, event); });
        _sd[i].SetBusTap(&_spiTap, static_cast<uint8_t>(i));
        _sd[i].SetClock([this]() { return EmulatedMicroseconds(); });
        _spiTap.SetDevice(static_cast<uint8_t>(i), i ? "sd1" : "sd0");
    }
    // 16 CPU clocks per byte at any speed (spi_master.vhd); the export's scale is the 28 MHz one
    _spiTap.SetByteTimeNs(16.0 * 1000.0 / 28.0);
    _spiTap.Attach(_context);
    if (_context->pMediaManager)
    {
        _context->pMediaManager->RegisterSlot(_sdSlot0);
        _context->pMediaManager->RegisterSlot(_sdSlot1);
    }
}

PortDecoder_Next::~PortDecoder_Next()
{
    // The peers outlive the decoder (the network manager owns them): they must not call back into this one
    for (unsigned i = 0; i < NextUart::kChannels; i++)
        if (ISerialPeer* peer = _uart.Peer(i))
        {
            peer->onReceive = nullptr;
            peer->SetClock(nullptr, 0);
        }
    if (ISerialPeer* peer = _uart.JoystickPeer())
    {
        peer->onReceive = nullptr;
        peer->SetClock(nullptr, 0);
    }
    if (_context->pKeyboard && _context->pKeyboard->GetPs2Sink() == &_extendedInput)
        _context->pKeyboard->SetPs2Sink(nullptr);
    if (_context->pKeyboard && _context->pKeyboard->GetExtendedKeySink() == &_extendedInput)
        _context->pKeyboard->SetExtendedKeySink(nullptr);
    if (_context->pMediaManager)
    {
        _context->pMediaManager->UnregisterSlot(_sdSlot0.Descriptor().id);
        _context->pMediaManager->UnregisterSlot(_sdSlot1.Descriptor().id);
    }
    if (_context->pSoundManager && _audio)
    {
        _context->pSoundManager->detachModelAudioSource(_audio.get());
        _audio->ReleaseBeeperLevels();
    }
    if (_context->pSoundManager)
        _context->pSoundManager->setMachineClockScale(1.0);  // the next machine runs at its T-states' clock
    if (_context->pCore && _context->pCore->GetZ80() && _context->pCore->GetZ80()->machineM1Hook == _divMmc.get())
        _context->pCore->GetZ80()->machineM1Hook = nullptr;
    if (_context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->SetInterruptSource(nullptr);
    _engine.reset();  // gives the CPU back to the native interpreter
}

void PortDecoder_Next::reset()
{
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;
    state.p1FFD = 0x00;
    state.pBFFD = 0x00;
    state.pFFFD = 0x00;
    state.pFE = 0xFF;
    state.border_attr = 0x07;

    state.ula_sixty_hz = 0;
    Mem().ResetMmu();
    Mem().ApplyClassicPaging(0, 0);
    InsertConfiguredCard();
    _board->Reset(true);
    _ctc.Reset();
    _uart.Reset(true);
    _audio->Reset();
    AudioConfigChanged();
    _dma.Reset();
    BindDma();
    _i2c.Reset();
    _divMmc->Reset();
    _multiface->Reset();
    if (Z80* z80 = _context->pCore->GetZ80(); z80 && !z80->machineM1Hook)
        z80->machineM1Hook = &_m1Chain;
    _context->pCore->GetZ80()->SetInterruptSource(_interrupts.get());
    ApplyTiming(_board->Timing());  // a reset starts the frame: nothing to wait for
    _spiSelected = -1;
    _spiRx = 0xFF;
    _spiPendingValid = false;
    _spiBusyUntil = 0;
    _spiBusyRatio = 1.0;
    _speedPending = false;
    _speedChangedAt = 0;

    _screen->SetBorderColor(COLOR_WHITE);
    _screen->SetActiveScreen(SCREEN_NORMAL);

    if (!_engine)
    {
        _engine = std::make_unique<Z80NEngine>(_context, _context->pCore->GetZ80());
        _engine->SetNextRegHost(_board.get());
    }
    if (!_engine->IsInstalled())
        _engine->Install();
    _engine->InvalidateBoundary();
}

void PortDecoder_Next::UpdateModelMemoryBanks()
{
    Mem().ApplyClassicPaging(_context->emulatorState.p7FFD, _context->emulatorState.p1FFD);
}

void PortDecoder_Next::Port_7FFD_Next(uint16_t port, uint8_t value, uint16_t pc)
{
    (void)port;
    (void)pc;
    if (IsPagingLocked() || !PagingAllowed(port))
        return;
    const uint8_t screenNumber = (value & 0x08) >> 3;
    const uint8_t prevScreenNumber = (_state->p7FFD & 0x08) >> 3;
    _state->p7FFD = value;
    Mem().ApplyClassicPaging(_state->p7FFD, _state->p1FFD);
    if (prevScreenNumber != screenNumber)
        _screen->SetActiveScreen(screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL);
}

void PortDecoder_Next::Port_1FFD_Next(uint8_t value)
{
    if (IsPagingLocked() || !PagingAllowed(0x1FFD))
        return;
    _state->p1FFD = value;
    Mem().ApplyClassicPaging(_state->p7FFD, _state->p1FFD);
}

void PortDecoder_Next::Port_DFFD_Next(uint8_t value)
{
    if (IsPagingLocked() || !PagingAllowed(0xDFFD))
        return;
    Mem().SetExtendedBank(value);
    Mem().ApplyClassicPaging(_state->p7FFD, _state->p1FFD);
}

void PortDecoder_Next::BeforeRelease()
{
    if (UlaContention* ula = _context->pUlaContention)
        ula->SetOddPortRule(nullptr, nullptr);  // the contention object may outlive this decoder
}

/// zxnext.vhd port_contend = not A0 | port_7ffd_active | port_bf3b | port_ff3b: the odd ports that wait like the ULA port. #7FFD (only under
/// the 128K timing: the +3 has no I/O contention at all) is any A15 = 0, A1:0 = 01 port that is not the #1FFD decode and is enabled
/// (NR #82 bit 1); the ULA+ pair needs NR #85 bit 0
bool PortDecoder_Next::OddPortContended(uint16_t port) const
{
    const bool xffd = (port & 0xC003) == 0x0001;
    const bool p1ffd = xffd && (port & 0x3000) == 0x1000 && PortEnabled(3);
    if (_state->ula_timing_class == 2 && (port & 0x8003) == 0x0001 && !p1ffd && PortEnabled(1))
        return true;
    return ((port & 0xFFFF) == 0xBF3B || (port & 0xFFFF) == 0xFF3B) && PortEnabled(24);
}

bool PortDecoder_Next::FloatingBusDrives(uint16_t port) const
{
    switch (_state->ula_timing_class)
    {
        case 0:
        case 1:
        case 2: return (port & 0x00FF) == 0x00FF;
        case 3: return (port & 0xF003) == 0x0001 && PortEnabled(4);
        default: return false;  // the Pentagon timing: #FF everywhere
    }
}

bool PortDecoder_Next::PortEnabled(unsigned bit) const
{
    const unsigned reg = 0x82 + bit / 8;
    const unsigned mask = 1u << (bit % 8);
    if (!(_board->Stored(static_cast<uint8_t>(reg)) & mask))
        return false;
    // the expansion bus's own word (NR #86-#89) is ANDed in only while the bus is enabled
    return !(_board->Stored(0x80) & 0x80) || (_board->Stored(static_cast<uint8_t>(reg + 4)) & mask);
}

/// The mouse registers as zxnext.vhd gives them: X, Y, and {wheel, 1, not middle, not left, not right}; with no mouse the inputs idle
/// (zero counts, buttons released). NR #0A bit 3 swaps the buttons (the PS/2 mouse module's control); its DPI bits are the host's
/// UART 1's pins reach the Pi header only with NR #A0 bit 4 (`pi_uart_en`) and in the Pi orientation, bit 5 (`pi_uart_rxtx`): zxnext.vhd 2319-2334
void PortDecoder_Next::SyncPiLine()
{
    _uart.SetLineConnected(1, (_board->Stored(0xA0) & 0x30) == 0x30);
}

/// NR #0A bits 1:0 into the PS/2 module's scaling (ps2_mouse.v xydelta): 00 x2, 01 x1, 10 half, 11 a quarter
void PortDecoder_Next::ApplyMouseDpi(uint8_t nr0a)
{
    if (!_mouse)
        return;
    static constexpr int kShift[4] = {1, 0, -1, -2};
    _mouse->SetDeltaShift(kShift[nr0a & 3]);
}

uint8_t PortDecoder_Next::ReadMouse(uint8_t reg)
{
    if (!_mouse || !_mouse->IsPresent())
        return reg == 0 ? 0x0F : 0x00;
    ApplyMouseDpi(_board->Stored(0x0A));  // follows a reset or a loaded image
    const uint8_t counted = _mouse->ReadRegister(reg);  // the device keeps its read bookkeeping
    if (reg != 0)
        return counted;
    const uint8_t buttons = _mouse->GetButtons() & 0x07;  // active low: D2 middle, D1 right, D0 left
    const bool left = (buttons & 1) != 0, right = (buttons & 2) != 0;
    const bool reverse = (_board->Stored(0x0A) & 0x08) != 0;
    const uint8_t port = static_cast<uint8_t>((buttons & 4) | ((reverse ? right : left) ? 2 : 0) | ((reverse ? left : right) ? 1 : 0));
    return static_cast<uint8_t>(((_mouse->GetWheel() & 0x0F) << 4) | 0x08 | port);
}

bool PortDecoder_Next::IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const
{
    return MouseRegisterOf(port, outRegister) && PortEnabled(13) && _mouse && _mouse->IsPresent();
}

uint8_t PortDecoder_Next::IdleRead(uint16_t port, uint16_t pc)
{
    constexpr uint8_t kIdleBus = 0xFF;
    _lastPortDecoded = false;
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.decodedPort = port;
    disp.wasDecoded = false;
    disp.wasHandledInline = true;
    OnPortInComplete(port, kIdleBus, pc, disp);
    return kIdleBus;
}

uint8_t PortDecoder_Next::DecodePortIn(uint16_t port, uint16_t pc)
{
    IoTrap(port, false, 0);
    const uint8_t low = static_cast<uint8_t>(port);
    if (low == _multiface->EnablePort() || low == _multiface->DisablePort())
    {
        uint8_t mfValue = 0xFF;
        if (_multiface->PortRead(low, port, mfValue))  // the Multiface drives this read
        {
            _lastPortDecoded = true;
            PortDecodeDisposition disp;
            disp.decodeRuleIndex = PortTraceRule::kNoTable;
            disp.decodedPort = port;
            disp.wasDecoded = true;
            disp.wasHandledInline = true;
            OnPortInComplete(port, mfValue, pc, disp);
            return mfValue;
        }
    }
    if (NextUart::Decodes(port))
    {
        if (!PortEnabled(12))
            return IdleRead(port, pc);
        SyncPiLine();
        const uint8_t value = _uart.Read(port, Now28());
        _uart.PollInterrupts(_interrupts->UartEnables());
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (low == NextCtc::kPortLow && (port >> 11) == 0x03)
    {
        // CTC: A15:A11 = 00011, A10:A8 the channel. Channels 4-7 select no channel (zxnext.vhd ctc_mod NUM_CTC = 4) but the
        // port is decoded (port_ctc ignores A10:A8): the read answers 0 from the empty wired-or, not the floating bus
        if (!PortEnabled(27))
            return IdleRead(port, pc);
        const unsigned channel = (port >> 8) & 7;
        const uint8_t value = channel < NextCtc::kChannels ? _ctc.Read(channel, Now28()) : 0x00;
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = _lastPortDecoded;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (port == 0x123B)
    {
        if (!PortEnabled(15))
            return IdleRead(port, pc);
        const uint8_t value = _board->Video().Port123b();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    // The AY ports (zxnext.vhd 2646-2649, 2771): #FFFD = A15:A14 = 11, A2 = 1 and the +3 family's A1:A0 = 01; #BFFD the same with
    // A15:A14 = 10, readable like #FFFD on +3 timing; #BFF5 (#BFFD with A3 = 0) reads the chip's identity and register address
    const bool ayFfFd = (port & 0xC007) == 0xC005 && !IsPort_DFFD_Next(port);
    const bool ayBfFd = (port & 0xC007) == 0x8005;
    if (ayFfFd || (ayBfFd && (_board->Timing() == 3 || (port & 0x08) == 0)))  // #BFF5 on any timing, #BFFD on +3
    {
        if (!PortEnabled(16))
            return IdleRead(port, pc);
        const uint8_t value = (ayBfFd && (port & 0x08) == 0) ? _audio->ReadIdentity() : _audio->ReadData();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    uint8_t joystick = 0;
    if ((low == 0x1F || low == 0x37) && JoystickPortAnswers(low == 0x37, joystick))
    {
        // The joystick ports: built into the board (zxnext.vhd port_1f_lsb / port_37_lsb), 000FUDLR active high, 0 with nothing pressed
        const uint8_t value = joystick;
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (low == 0x6B || low == 0x0B)
    {
        if (!PortEnabled(low == 0x0B ? 25 : 5))
            return IdleRead(port, pc);
        const uint8_t value = _dma.ReadAs(low == 0x0B);
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (port == 0x303B)
    {
        if (!PortEnabled(14))
            return IdleRead(port, pc);
        // the engine runs in machine time: the flags are those of its steps before the read
        const uint8_t value = _screenNext ? _screenNext->ReadSpriteStatus() : _board->Sprites().ReadStatus();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    // Kempston mouse (zxnext.vhd port_fadf / fbdf / ffdf), gated by NR #83 bit 5
    if (uint8_t mouseRegister = 0; MouseRegisterOf(port, mouseRegister))
    {
        if (!PortEnabled(13))
            return IdleRead(port, pc);  // not decoded: the base class's partial Kempston decode must not answer
        const uint8_t value = ReadMouse(mouseRegister);
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    // ULA+: #BF3B selects, #FF3B reads the CLUT entry (mode 0) or the enable bit; gated by NR #85 bit 0
    if (port == 0xBF3B || port == 0xFF3B)
    {
        if (!PortEnabled(24) || port == 0xBF3B)
            return IdleRead(port, pc);  // #BF3B is write only
        const NextVideoRegs& video = _board->Video();
        const uint8_t value = video.UlaPlusMode() == 0 ? video.ReadUlaPlusPalette()
                                                         : static_cast<uint8_t>((_board->Stored(0x68) >> 3) & 1);
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    // NR #08 bit 2: #FF reads the Timex mode (and only with its enable bit: port_ff_rd_dat; else the ULA's floating bus, below)
    if (low == 0xFF && (_board->Stored(NextBoard::kRegPeripheral2) & 0x04) && PortEnabled(0))
    {
        const uint8_t value = _board->Video().PortFf();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (low == NextDivMmc::kPort && _divMmc->PortEnabled())
    {
        const uint8_t value = _divMmc->ReadPort();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (port == NextI2c::kPortScl || port == NextI2c::kPortSda)
    {
        if (!PortEnabled(10))
            return IdleRead(port, pc);
        uint8_t value = _i2c.Read(port);
        if (_board->Stored(0xA0) & 0x08)  // pi_i2c1_sda / scl = i_GPIO(2 / 3), ANDed into the read (zxnext.vhd 2317-2318)
            if (!((_board->GpioPins() >> (port == NextI2c::kPortScl ? 3 : 2)) & 1))
                value = static_cast<uint8_t>(value & 0xFE);
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (port == kPortRegSelect || port == kPortRegData || (low == kPortSpiData && SpiPortEnabled()))
    {
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port == kPortRegSelect || port == kPortRegData ? port : kPortSpiData;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        uint8_t result;
        if (low == kPortSpiData)
            result = SpiRead();
        else
            result = port == kPortRegSelect ? _board->SelectedRegister() : _board->ReadSelectedByProgram();
        _lastPortDecoded = true;
        OnPortInComplete(port, result, pc, disp);
        return result;
    }
    uint8_t value = PortDecoder_Spectrum128::DecodePortIn(port, pc);
    // The pads press membrane keys in the Sinclair, Cursor and user-defined modes, and their extra buttons in the Kempston
    // and MD ones (membrane_stick.vhd), unless NR #0B bit 7 gives the connectors to the I/O mode
    if (IsPort_FE(port) && !(_board->Stored(0x0B) & 0x80))
    {
        // columns 5 and 6 are the extended keys (NR #B0 / #B1), not bits of the port
        const uint8_t columns = _board->KeyJoystick().Columns(static_cast<uint8_t>(port >> 8), PadState(0), JoystickMode(true),
                                                              PadState(1), JoystickMode(false));
        value &= static_cast<uint8_t>(0xE0 | (columns & 0x1F));
    }
    // port #FE's EAR bit is the tape input or the Pi's I2S audio thresholded into it (zxnext.vhd 3459, NR #A2 bit 0)
    if (_board->PiEar() && IsPort_FE(port))
        value |= 0x40;
    if (_inLog && !_lastPortDecoded)
    {
        PortUse& use = (*_inLog)[port];
        use.count++;
        use.last = value;
    }
    return value;
}

uint8_t PortDecoder_Next::ReadExtendedInput(uint8_t reg) const
{
    switch (reg)
    {
        case 0xB0: return _extendedInput.ReadB0();
        case 0xB1: return _extendedInput.ReadB1();
        case 0xB2: return _extendedInput.ReadB2();
        default: return 0;
    }
}

uint16_t PortDecoder_Next::PadState(unsigned connector) const
{
    if (connector == 0)
    {
        // the left connector: the Kempston device is its low byte, the extras are the keyboard controller's
        const uint8_t stick = _context->pJoystick ? _context->pJoystick->State() : 0;
        return static_cast<uint16_t>(stick | NextExtendedInput::ExtrasToPadBits(_extendedInput.Pad(true)));
    }
    return connector == 1 ? _extendedInput.Right() : 0;
}

bool PortDecoder_Next::ApplyPadState(unsigned connector, uint16_t state)
{
    state &= kPadMask;
    if (connector == 0)
    {
        if (_context->pJoystick)
            _context->pJoystick->SetState(static_cast<uint8_t>(state & 0xFF));
        _extendedInput.SetPad(true, NextExtendedInput::PadBitsToExtras(state));
        return true;
    }
    if (connector != 1)
        return false;
    _extendedInput.SetRight(state);
    return true;
}

unsigned PortDecoder_Next::JoystickMode(bool left) const
{
    const uint8_t nr05 = _board->Stored(0x05);
    constexpr unsigned kLeftBit3 = 3, kRightBit1 = 1, kLeftShift = 6, kRightShift = 4, kLow2 = 3;
    return left ? ((((nr05 >> kLeftBit3) & 1u) << 2) | ((nr05 >> kLeftShift) & kLow2))
                : ((((nr05 >> kRightBit1) & 1u) << 2) | ((nr05 >> kRightShift) & kLow2));
}

bool PortDecoder_Next::JoystickPortAnswers(bool port37, uint8_t& value) const
{
    constexpr uint8_t kPortEnable1F = 0x40, kPortEnable37 = 0x80;  // NR #82 bits 6 and 7 (internal_port_enable)
    constexpr uint8_t kPadBits = 0xC0, kStickBits = 0x3F;           // bits 7:6 belong to an MD pad only
    constexpr unsigned kRegPortEnable0 = 0x82;
    const unsigned first = port37 ? kJoyKempston2 : kJoyKempston1, md = port37 ? kJoyMd2 : kJoyMd1;
    const unsigned leftMode = JoystickMode(true), rightMode = JoystickMode(false);
    const bool leftOn = leftMode == first || leftMode == md, rightOn = rightMode == first || rightMode == md;
    value = 0;
    if (!(_board->Stored(kRegPortEnable0) & (port37 ? kPortEnable37 : kPortEnable1F)) || !(leftOn || rightOn))
        return false;
    if (leftOn)
        value = static_cast<uint8_t>(Default_Port_KempstonJoystick_In() & (leftMode == md ? (kPadBits | kStickBits) : kStickBits));
    if (rightOn)  // the right connector: whatever a tool put on it (it has no host keys); the two sides are ORed (port_1f_dat <= joyL_1f or joyR_1f)
        value |= static_cast<uint8_t>(_extendedInput.Right() & (rightMode == md ? (kPadBits | kStickBits) : kStickBits));
    return true;
}

void PortDecoder_Next::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    IoTrap(port, true, value);
    // The magic debug port: the program's console. A tool's own OUT (toolAccessActive) is not the program's
    if (_magicPort.Enabled()) [[unlikely]]
        if (!_context->toolAccessActive)
            _magicPort.Write(port, value, static_cast<uint32_t>(_context->emulatorState.frame_counter),
                             _context->pCore && _context->pCore->GetZ80() ? _context->pCore->GetZ80()->t : 0, pc);
    if (static_cast<uint8_t>(port) == _multiface->EnablePort() || static_cast<uint8_t>(port) == _multiface->DisablePort())
        _multiface->PortWrite(static_cast<uint8_t>(port));  // the other devices on the port see the write too
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;

    if (NextUart::Decodes(port))
    {
        if (PortEnabled(12))
        {
            SyncPiLine();
            _uart.Write(port, value, Now28());
            _uart.PollInterrupts(_interrupts->UartEnables());
        }
    }
    else if (static_cast<uint8_t>(port) == NextCtc::kPortLow && (port >> 11) == 0x03)
    {
        const unsigned channel = (port >> 8) & 7;
        if (channel < NextCtc::kChannels && PortEnabled(27))
            _ctc.Write(channel, value, Now28());
    }
    else if (port == NextI2c::kPortScl || port == NextI2c::kPortSda)
    {
        if (PortEnabled(10))
            _i2c.Write(port, value);
    }
    else if (static_cast<uint8_t>(port) == NextDivMmc::kPort && _divMmc->PortEnabled())
        _divMmc->WritePort(value);
    else if (static_cast<uint8_t>(port) == 0xFF)
    {
        if (PortEnabled(0))
        {
            if (_context->pScreen)
                _context->pScreen->UpdateScreen();
            _board->Video().WritePortFf(value);
        }
    }  // the Timex screen mode (NR #69 bits 5:0 alias it)
    else if (port == 0x123B)
    {
        if (PortEnabled(15))
        {
            if (_context->pScreen)
                _context->pScreen->UpdateScreen();
            _board->Video().WritePort123b(value);
            _board->RefreshLayer2Mapping();
        }
    }
    else if ((port & 0xC007) == 0xC005 && !IsPort_DFFD_Next(port))
    {
        if (PortEnabled(16))
            _audio->WriteSelect(value);  // #FFFD
    }
    else if ((port & 0xC007) == 0x8005)
    {
        if (PortEnabled(16))
            _audio->WriteData(value);    // #BFFD
    }
    else if (port == 0x303B)
    {
        if (PortEnabled(14))
        {
            if (_screenNext)
                _screenNext->SpriteStateWillChange();
            _board->Sprites().WriteSlotSelect(value);
        }
    }
    else if (port == 0xBF3B)
    {
        if (PortEnabled(24))
            _board->Video().WriteUlaPlusSelect(value);
    }
    else if (port == 0xFF3B)
    {
        if (PortEnabled(24))
        {
            const uint8_t mode = _board->Video().UlaPlusMode();
            if (mode <= 1 && _context->pScreen)
                _context->pScreen->UpdateScreen();
            if (mode == 0)
                _board->Video().WriteUlaPlusPalette(value);
            else if (mode == 1)
                _board->SetStoredBits(0x68, 0x08, static_cast<uint8_t>((value & 1) << 3));  // the enable is NR #68 bit 3's flop
        }
    }
    else if (WriteDacPort(port, value))
        ;
    else if (static_cast<uint8_t>(port) == 0x6B || static_cast<uint8_t>(port) == 0x0B)
    {
        const bool z80Compatible = static_cast<uint8_t>(port) == 0x0B;
        if (PortEnabled(z80Compatible ? 25 : 5))
        {
            const bool wasActive = _dma.Active();
            _dma.Write(value, z80Compatible);
            _dmaRequestLate = !wasActive && _dma.Active();
        }
    }
    else if (static_cast<uint8_t>(port) == 0x57)
    {
        if (PortEnabled(14))
        {
            if (_screenNext)
                _screenNext->SpriteStateWillChange();
            _board->Sprites().WriteAttribute(value);
        }
    }
    else if (static_cast<uint8_t>(port) == 0x5B)
    {
        if (PortEnabled(14))
        {
            if (_screenNext)
                _screenNext->SpriteStateWillChange();
            _board->Sprites().WritePattern(value);
        }
    }
    else if (static_cast<uint8_t>(port) == kPortSpiSelect && SpiPortEnabled())
        SpiSelect(value);
    else if (static_cast<uint8_t>(port) == kPortSpiData && SpiPortEnabled())
        SpiWrite(value);
    else if (port == kPortRegSelect)
        _board->SelectRegister(value);
    else if (port == kPortRegData)
        _board->WriteSelected(value);
    else if (IsPort_7FFD_Next(port))
    {
        if (PortEnabled(1))
            Port_7FFD_Next(port, value, pc);
    }
    else if (IsPort_1FFD_Next(port))
    {
        if (PortEnabled(3))
            Port_1FFD_Next(value);
    }
    else if (IsPort_DFFD_Next(port))
    {
        if (PortEnabled(2))
            Port_DFFD_Next(value);
    }
    else
    {
        PortDecoder_Spectrum128::DecodePortOut(port, value, pc);
        if (_outLog && !IsPort_FE(port) && !IsPort_7FFD(port) && (port & 0xC007) != 0xC005 && (port & 0xC007) != 0x8005)
        {
            PortUse& use = (*_outLog)[port];
            use.count++;
            use.last = value;
        }
        return;
    }
    disp.decodedPort = port;
    OnPortOutComplete(port, value, pc, disp);
}

/// The DMA reads and writes through the Z80's view of memory and the port decoder
void PortDecoder_Next::BindDma()
{
    NextDma::Bus bus;
    bus.readMemory = [this](uint16_t address) { return _context->pMemory->DirectReadFromZ80Memory(address); };
    bus.writeMemory = [this](uint16_t address, uint8_t value) { Mem().DmaWrite(address, value); };
    // the DMA's own cycles do not reach its ports ("allow dma to program itself? no", zxnext.vhd port_dma_rd / port_dma_wr)
    // dma_wait_n includes spi_wait_n (zxnext.vhd 1844): a byte to or from the SD card's data port waits until the card's 16 clocks passed
    auto waitSpi = [this](uint8_t low) {
        if (low != kPortSpiData)
            return;
        while (Now() < SpiBusyUntil())
            _context->pCore->GetZ80()->InsertWaitStates(1);
    };
    bus.readIo = [this, waitSpi](uint16_t port) {
        const uint8_t low = static_cast<uint8_t>(port);
        waitSpi(low);
        return low == 0x6B || low == 0x0B ? uint8_t{0xFF} : DecodePortIn(port, 0);
    };
    bus.writeIo = [this, waitSpi](uint16_t port, uint8_t value) {
        const uint8_t low = static_cast<uint8_t>(port);
        waitSpi(low);
        if (low != 0x6B && low != 0x0B)
            DecodePortOut(port, value, 0);
    };
    // the CPU is held: its clock runs on with every access of the transfer, so a port write lands at its own moment
    bus.advance = [this](unsigned clocks) { _context->pCore->GetZ80()->InsertWaitStates(static_cast<uint8_t>(clocks)); };
    // the DMA runs on the CPU's clock (z80dma clk_i = i_CLK_CPU), which the video logic holds on a contended access of the bus whoever
    // drives it (cpu_mreq_n / cpu_iorq_n / cpu_a are the DMA's while it holds the bus); the +3's wait reaches it through dma_wait_n
    // (zxnext.vhd 1844). A cycle waits at its first T when its address maps to contended memory (mem_contend is decoded from cpu_a
    // alone), a port cycle and a T without a strobe too (`io`); the port rule never holds it: the DMA drops IORQ on a falling edge, so
    // the clock's next rising edge already sees ioreqtw3_n low (zxula.vhd 583-592, dma.vhd 186), while the CPU drops it on T2's rising
    // edge. On the +3 only MREQ cycles wait (o_cpu_wait_n), sampled half a T before the CPU does (DmaDelayAt). POC 036 measured all
    // of it on the RTL
    bus.contend = [this](uint16_t address, bool io) {
        const UlaContention* ula = _context->pUlaContention;
        if (!ula || !ula->IsContentionEnabled() || (io && ula->IsGateArray()) || !ula->IsContendedAt(address))
            return;
        Z80* z80 = _context->pCore->GetZ80();
        const uint8_t wait = ula->DmaDelayAt(z80->t);
        if (wait)
            z80->InsertWaitStates(wait);
    };
    _dma.SetBus(std::move(bus));
}

/// NR #06 / #08 / #09: the AY or YM chips, turbosound, the DAC, the stereo mix
void PortDecoder_Next::AudioConfigChanged()
{
    const uint8_t nr06 = _board->Stored(0x06), nr08 = _board->Stored(0x08), nr09 = _board->Stored(0x09);
    // zxnext.vhd 6379-6392: aymode_i is NR #06 bit 0 (modes 00 and 10 are the YM, 01 and 11 the AY), mode 11 holds the chips in reset
    _audio->Configure((nr06 & 1) == 0, (nr08 & 0x02) != 0, (nr08 & 0x08) != 0, (nr08 & 0x20) != 0, static_cast<uint8_t>((nr09 >> 5) & 7),
                      (nr06 & 0x40) && (nr08 & 0x10), (nr06 & 3) == 3);
}

void PortDecoder_Next::PiAudioChanged(unsigned left, unsigned right)
{
    _audio->SetPiAudio(left, right);
}

/// NR #2C (B, left), #2D (A and D, mono), #2E (C, right): the DAC's NextREG mirrors (writes; a read is the Pi's I2S sample)
void PortDecoder_Next::DacMirrorWrite(uint8_t reg, uint8_t value)
{
    if (reg == 0x2C)
        _audio->WriteDac(1, value);
    else if (reg == 0x2D)
    {
        _audio->WriteDac(0, value);
        _audio->WriteDac(3, value);
    }
    else if (reg == 0x2E)
        _audio->WriteDac(2, value);
}

/// The DAC ports of the VHDL (zxnext.vhd 2429-2435, 2658-2664), gated by NR #84
bool PortDecoder_Next::WriteDacPort(uint16_t port, uint8_t value)
{
    const uint8_t low = static_cast<uint8_t>(port), en = _board->Stored(0x84);
    const bool sd1 = en & 0x02, sd2 = en & 0x04, stereoAD = en & 0x08, stereoBC = en & 0x10, monoADfb = (en & 0x20) && !sd2, monoBC = en & 0x40, monoADdf = en & 0x80;
    const bool mAD = (low == 0xFB && monoADfb) || (low == 0xDF && monoADdf);
    const bool mBC = low == 0xB3 && monoBC;
    const bool a = mAD || (low == 0x1F && sd1) || (low == 0xF1 && sd2) || (low == 0x3F && stereoAD);
    const bool b = mBC || (low == 0x0F && (sd1 || stereoBC)) || (low == 0xF3 && sd2);
    const bool c = mBC || (low == 0x4F && (sd1 || stereoBC)) || (low == 0xF9 && sd2);
    const bool d = mAD || (low == 0x5F && (sd1 || stereoAD)) || (low == 0xFB && sd2);
    if (a)
        _audio->WriteDac(0, value);
    if (b)
        _audio->WriteDac(1, value);
    if (c)
        _audio->WriteDac(2, value);
    if (d)
        _audio->WriteDac(3, value);
    return a || b || c || d;
}

/// Between two instructions: a DMA that holds the bus runs until it is done or waits for its prescaler (burst mode
/// releases the bus then); the CPU is held for two of its clocks per byte
void PortDecoder_Next::NextRegAccess(bool write, uint8_t reg, uint8_t value, NextRegSource source)
{
    if (write && reg == 0x0A)
        ApplyMouseDpi(value);
    if (write && reg == 0x0B)
        UpdateJoystickPin7(value);
    uint8_t door;
    switch (source)
    {
        case NextRegSource::NextReg: door = BRK_NR_DOOR_NEXTREG; break;
        case NextRegSource::Port: door = BRK_NR_DOOR_PORT; break;
        default: return;  // the copper is watched on its own (OnCopperStep); the board itself (reset, a loader) has no one to stop
    }
    if (_breakpointActive || !_context->pDebugManager || !_context->pEmulator)
        return;
    BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
    const uint16_t id = brk.HandleNextReg(write ? BRK_NR_WRITE : BRK_NR_READ, reg, value, door);
    if (id != BRK_INVALID)
        FireBreakpoint(id, reg, write ? BreakpointHitKind::NextRegWrite : BreakpointHitKind::NextRegRead);
}

void PortDecoder_Next::FireBreakpoint(uint16_t id, uint16_t address, BreakpointHitKind kind)
{
    _breakpointActive = true;
    if (_context->pDebugManager)
        _context->pDebugManager->GetBreakpointsManager()->NoteTriggered(id);
    _context->pEmulator->OnBreakpointHit(id, address, kind);
    _breakpointActive = false;
}

void PortDecoder_Next::OnInstructionDone()
{
    if (_nr08Pending) [[unlikely]]
        SettleContentionDisabled();
    if (!_context->pDebugManager) [[unlikely]]
        return;
    BreakpointManager& manager = *_context->pDebugManager->GetBreakpointsManager();
    if (manager.GetHotState()->hasMagic) [[unlikely]]
        WatchMagic(manager);
    if (!manager.GetHotState()->hasBeam) [[likely]]
    {
        _watchValid = false;  // the next look starts from the beam of that moment
        return;
    }
    WatchCopper(manager);
}

/// The instruction that just ran was a magic opcode (ED FF: a NOP of two bytes; DD 01: LD BC,nn of four bytes with the prefix ignored, the
/// hardware does what the table says and so do we): the stop is after it, the address reported is where it stood. The CPU library reports
/// the opcode DD 01 runs as (01, no prefix), so the opcode is read from where the instruction started (m1_pc), and only a whole
/// instruction counts (the PC moved by its length: a lone prefix step does not)
void PortDecoder_Next::WatchMagic(BreakpointManager& manager)
{
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (_breakpointActive || !z80 || !_context->pEmulator || !_context->pMemory)
        return;
    constexpr uint16_t kLengthEdFf = 2;  // prefix + opcode
    constexpr uint16_t kLengthDd01 = 4;  // prefix + opcode + 16-bit operand
    const uint16_t start = z80->m1_pc;
    const uint16_t word = static_cast<uint16_t>((_context->pMemory->DirectReadFromZ80Memory(start) << 8) |
                                                _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(start + 1)));
    if ((word != BRK_MAGIC_ED_FF || z80->pc != static_cast<uint16_t>(start + kLengthEdFf)) &&
        (word != BRK_MAGIC_DD_01 || z80->pc != static_cast<uint16_t>(start + kLengthDd01)))
        return;
    const uint16_t id = manager.HandleMagic(word);
    if (id != BRK_INVALID)
        FireBreakpoint(id, start, BreakpointHitKind::Magic);
}

/// The copper is about to carry out an instruction: a copper-instruction breakpoint, or a NextREG write breakpoint that takes the copper's
/// door, stops the machine BEFORE the MOVE writes (true: the copper holds the instruction until it runs on)
bool PortDecoder_Next::OnCopperStep(const NextCopper::Step& step)
{
    if (_breakpointActive || !_context->pDebugManager || !_context->pEmulator)
        return false;
    BreakpointManager& manager = *_context->pDebugManager->GetBreakpointsManager();
    const BreakpointHotState* hot = manager.GetHotState();
    if (!hot->hasCopperPc && !hot->hasNextRegWrite)
        return false;
    uint16_t found = BRK_INVALID;
    uint16_t address = 0;
    BreakpointHitKind kind = BreakpointHitKind::CopperInstruction;
    if (hot->hasCopperPc)
    {
        found = manager.HandleCopperPc(step.pc);
        address = step.pc;
    }
    if (step.isMove && step.reg != 0 && hot->hasNextRegWrite)
    {
        const uint16_t id = manager.HandleNextReg(BRK_NR_WRITE, step.reg, step.value, BRK_NR_DOOR_COPPER);  // counts its hits as well
        if (found == BRK_INVALID && id != BRK_INVALID)
        {
            found = id;
            address = step.reg;
            kind = BreakpointHitKind::NextRegWrite;
        }
    }
    if (found == BRK_INVALID)
        return false;
    FireBreakpoint(found, address, kind);
    return true;
}

/// Beam breakpoints: the beam passed the place since the last look (the copper's own steps are OnCopperStep's)
void PortDecoder_Next::WatchCopper(BreakpointManager& manager)
{
    NextCopper& copper = _board->Copper();
    uint64_t now;
    if (_breakpointActive || !_context->pEmulator || !copper.BeamNow(now))
        return;
    if (!_watchValid)
    {
        _watchValid = true;
        _watchClock = now;
        return;
    }
    const uint64_t from = _watchClock;
    const bool wrapped = now < from;  // the frame rolled over
    _watchClock = now;
    auto inSpan = [&](uint64_t at) { return wrapped ? (at > from || at <= now) : (at > from && at <= now); };
    const uint16_t found = manager.HandleBeam([&](uint16_t line, uint16_t hc) {
        const uint64_t at = copper.ClockOf(line, hc);
        return at != UINT64_MAX && inSpan(at);
    });
    if (found != BRK_INVALID)
        FireBreakpoint(found, 0, BreakpointHitKind::Beam);
}

void PortDecoder_Next::StepDma()
{
    if (!_dma.Active())
        return;
    if (_dmaRequestLate)
    {
        // the enable is written in the instruction's last cycle, after the T80 sampled BUSRQ for it: the bus goes over at the end
        // of the next cycle, the next instruction's M1 (POC 036; ours runs that whole instruction first)
        _dmaRequestLate = false;
        return;
    }
    // im2_dma_delay: an interrupt the program chose in NR #CC-#CE (or an NMI with #CC bit 7) pending or in service holds the DMA off
    if (_interrupts->DmaDelay(_divMmc->NmiHold() || _multiface->NmiHold()))
        return;
    while (_dma.Active())
    {
        const unsigned moved = _dma.Run(256, Now28());
        if (_dma.Waiting() && _dma.HoldsBus())
        {
            // continuous mode with a prescaler: the DMA keeps the bus through the wait - the CPU does not run
            const uint64_t end = _dma.WaitEnd();
            while (Now28() < end)
            {
                const double missing28 = static_cast<double>(end) - Now28();
                const double perCpuClock = 8.0 / _ratio;  // 28 MHz clocks of one CPU clock
                const unsigned clocks = static_cast<unsigned>(missing28 / perCpuClock) + 1;
                _context->pCore->GetZ80()->InsertWaitStates(static_cast<uint8_t>(clocks > 255 ? 255 : clocks));
            }
            continue;
        }
        if (moved == 0 || _dma.Waiting())
            break;
    }
}

/// region <Reset>

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

/// [NEXT] SdCard: the configured card on every reset while slot 0 is empty (a card the user put in stays), through the media manager's slot "sd.next0" (a folder becomes a FAT16 card with an MBR,
/// writes kept in the session; a file is a raw image). Inserts, swaps and ejects by the user are the manager's
void PortDecoder_Next::InsertConfiguredCard()
{
    const std::string path = _context->config.next_sd_path;
    if (path.empty() || _sd[0].present() || !_context->pMediaManager)
        return;  // nothing configured, or a card is in the slot (the user's, inserted by the manager): it stays
    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved) && !std::filesystem::exists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    MediaSource source;
    source.path = resolved;
    InsertOptions options;
    options.access = AccessMode::Session;
    options.disposition = Disposition::Discard;
    options.immediate = true;
    const MediaResult result = _context->pMediaManager->Insert(_sdSlot0.Descriptor().id, source, options);
    if (!result.Ok())
        MLOGERROR("PortDecoder_Next: SD card '%s' not inserted: %s", path.c_str(), result.message.c_str());
}

/// joy_iomode_pin7 follows NR #0B bit 0 in mode 00 and the UART's TX (idle high) in modes 1x every clock; in mode 01 it keeps what it had
void PortDecoder_Next::UpdateJoystickPin7(uint8_t nr0b)
{
    switch ((nr0b >> 4) & 3)
    {
        case 0: _joyPin7 = (nr0b & 0x01) != 0; break;
        case 1: break;
        default: _joyPin7 = true; break;
    }
}

void PortDecoder_Next::PerformReset(bool hard)
{
    _joyPin7 = true;  // joy_iomode_pin7 resets to 1; NR #0B resets to 0, so mode 00 then follows its bit 0 = 0 at once
    Z80* z80 = _context->pCore->GetZ80();
    // The reset restarts the CPU, not the frame: the video and interrupt timing keep running
    const auto t = z80->t;
    const auto tt = z80->tt;
    z80->Reset();
    z80->t = t;
    z80->tt = tt;

    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;
    state.p1FFD = 0x00;
    Mem().ResetMmu();
    _board->Reset(hard);  // config mode and the boot ROM first: the slot table follows them
    if (hard)
        ApplySystemClock();  // NR #11 is back to VGA 0 with the configuration load
    // the same net pulses on a hard reset, and on a soft one while the expansion bus is enabled (zxnext_top bus_rst_n_io: reset_hard or
    // reset_soft and zxn_bus_en): the ESP restarts with it
    if (hard || (_board->Stored(0x80) & 0x80))
        if (auto* esp = dynamic_cast<EspModule*>(_uart.Peer(0)))
        {
            esp->SetResetPin(true);
            esp->SetResetPin((_board->Stored(0x02) & 0x80) != 0);
        }
    UpdateJoystickPin7(_board->Stored(0x0B));
    _audio->Reset();      // turbosound.vhd / soundrive.vhd: reset_i is `reset` (the chips, the pan, the selected chip, the DACs)
    AudioConfigChanged();
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::Reset;
        e.source = hard ? "hard" : "soft";
        e.value = _board->ResetBits();
        RecordInt(e);
    }
    _dmaRequestLate = false;
    _dma.Reset();         // zxnext.vhd: the DMA's reset_i is the hard or the soft reset
    _uart.Reset(hard);    // the FIFOs on any reset, the prescalers and frames on the hard one (uart.vhd)
    UpdateUartRoute(false);  // NR #0B went back to its reset value: the connector carries no UART
    _divMmc->Reset();     // E3 and the automap latches (zxnext.vhd 4175, divmmc.vhd 108-139) and the Multiface's session are `reset` too
    _multiface->Reset();
    Mem().ApplyClassicPaging(0, 0);
    _spiSelected = -1;
    if (_engine)
        _engine->InvalidateBoundary();
}

/// endregion

/// region <SPI and the SD cards>

uint64_t PortDecoder_Next::Now28() const
{
    return static_cast<uint64_t>(Now() * 8.0);
}

double PortDecoder_Next::Now() const
{
    const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1u;
    // a peer fitted while the machine is being built has no CPU to ask yet
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    return static_cast<double>(_state->t_states) + (z80 ? static_cast<double>(z80->t) / multiplier : 0.0);
}

bool PortDecoder_Next::InsertSdCard(unsigned index, std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode)
{
    // a Next's card is an SDHC one (games such as Atic Atac refuse an SDSC answer: "SDHC OR BETTER REQUIRED"); a small folder-backed image
    // would be taken for SDSC by its size alone
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.type = MediaSourceType::Blank;
        InsertOptions options;
        options.access = AccessOf(mode);
        options.disposition = Disposition::Discard;
        options.immediate = true;
        auto medium = MediaFormatRegistry::WrapBlock(source, AccessOf(mode), "memory", std::move(media));
        return manager->Insert(Slot(index).Descriptor().id, std::move(medium), options).Ok();
    }
    return _sd[index & 1].insert(std::move(media), mode, SdCardSpi::Type::SDHC);
}

bool PortDecoder_Next::InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode)
{
    if (MediaManager* manager = _context->pMediaManager)
    {
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.access = AccessOf(mode);
        options.disposition = Disposition::Discard;
        options.immediate = true;
        const MediaResult result = manager->Insert(Slot(index).Descriptor().id, source, options);
        if (!result.Ok())
            MLOGWARNING("PortDecoder_Next: SD card '%s' not inserted: %s", path.c_str(), result.message.c_str());
        return result.Ok();
    }
    return _sd[index & 1].open(path, mode, SdCardSpi::Type::SDHC);
}

PortDecoder_Next::SdSlot::SdSlot(PortDecoder_Next& owner, unsigned index) : _owner(owner), _index(index)
{
    _descriptor.id = index == 0 ? "sd.next0" : "sd.next1";
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = index == 0 ? "SD card 0 (ZX Next)" : "SD card 1 (ZX Next)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    // NextZXOS, TBBLUE.FW and the boot ROM read FAT16 and FAT32 from an MBR partition
    _descriptor.defaultFs = FatType::Fat16;
    _descriptor.fsCompatibility = {FatType::Fat16, FatType::Fat32};
    _descriptor.folderMbr = true;
    _descriptor.tags = index == 0 ? std::vector<std::string>{"sd", "next", "primary", "boot"}
                                  : std::vector<std::string>{"sd", "next", "secondary"};
    if (index == 0)
        _descriptor.aliases = {"sd"};
}

void PortDecoder_Next::SdSlot::Attach(Medium& medium)
{
    // a Next's card is an SDHC one (games such as Atic Atac refuse an SDSC answer: "SDHC OR BETTER REQUIRED"); a small folder-backed image
    // would be taken for SDSC by its size alone
    _owner._sd[_index].attach(*medium.Block(), SdCardSpi::Type::SDHC);
}

void PortDecoder_Next::SdSlot::Detach()
{
    _owner._sd[_index].detach();
}

bool PortDecoder_Next::SdSlot::IsBusy() const
{
    return _owner._sd[_index].busy();
}

void PortDecoder_Next::RecordSdEvent(unsigned card, const SdTraceEvent& event)
{
    if (!_sdJournal.Enabled()) [[likely]]
        return;
    NextSdEvent e;
    e.frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        e.t = z80->t;
        e.pc = z80->m1_pc;
    }
    e.card = static_cast<uint8_t>(card);
    e.kind = event.kind == SdTraceEvent::Kind::Command ? NextSdKind::Command
             : event.kind == SdTraceEvent::Kind::ReadBlock ? NextSdKind::ReadBlock
                                                           : NextSdKind::WriteBlock;
    e.index = event.index;
    e.app = event.app;
    e.crcValid = event.crcValid;
    e.argument = event.argument;
    e.r1 = event.r1;
    e.block = event.block;
    e.ok = event.ok;
    _sdJournal.Record(e);
}

void PortDecoder_Next::SpiSelect(uint8_t value)
{
    // Active low; only these patterns select (esxDOS writes garbage in the upper bits). The swap bit exchanges
    // the two lines. The Pi lines and the flash are decoded and ignored
    int line = -1;
    if (value == 0xFE)
        line = _board->SdSwap() ? 1 : 0;
    else if (value == 0xFD)
        line = _board->SdSwap() ? 0 : 1;
    if (line == _spiSelected)
        return;
    const bool tap = _spiTap.Active();
    // A select change with a byte still shifting: one finding, on the first line that changes
    bool midByte = tap && _spiPendingValid && Now() < SpiBusyUntil();
    if (_spiSelected >= 0)
    {
        _sd[_spiSelected].select(false);
        if (tap) [[unlikely]]
            _spiTap.SpiSelect(static_cast<uint8_t>(_spiSelected), false, midByte);
        midByte = false;
    }
    _spiSelected = line;
    if (line >= 0)
    {
        _sd[line].select(true);
        if (tap) [[unlikely]]
            _spiTap.SpiSelect(static_cast<uint8_t>(line), true, midByte);
    }
}

void PortDecoder_Next::SpiWrite(uint8_t value)
{
    const double now = Now();
    if (now < SpiBusyUntil())
    {
        _spiTooFast++;
        if (_spiTap.Active()) [[unlikely]]
            _spiTap.SpiEarly(SpiTapSlot(), true, value);
        return;
    }
    SpiSettle(now);
    _spiPending = _spiSelected >= 0 ? _sd[_spiSelected].exchange(value) : 0xFF;
    if (_spiTap.Active()) [[unlikely]]
        _spiTap.SpiByte(SpiTapSlot(), value, _spiPending, false);
    _spiPendingValid = true;
    _spiBusyRatio = SpeedRatio();
    _spiBusyUntil = now + kSpiByteClocks / _spiBusyRatio;
}

double PortDecoder_Next::SpiBusyUntil()
{
    const double ratio = SpeedRatio();
    if (ratio != _spiBusyRatio)
    {
        // spi_master.vhd counts the CPU clock: the byte is 16 clocks whatever the speed. The new speed was taken over at the instant of
        // the switch (ApplyPendingSpeed) or at the start of this frame (Z80::BeginFrame), so what was left of the byte then is counted in
        // the new clocks. A change older than the byte would have left nothing in flight
        const double boundary = std::max(static_cast<double>(_state->t_states), _speedChangedAt);
        if (_spiBusyUntil > boundary)
            _spiBusyUntil = boundary + (_spiBusyUntil - boundary) * _spiBusyRatio / ratio;
        _spiBusyRatio = ratio;
    }
    return _spiBusyUntil;
}

/// spi_master.vhd latches miso_dat only when the counter is zero: a byte in flight is invisible until it is done
void PortDecoder_Next::SpiSettle(double now)
{
    if (_spiPendingValid && now >= SpiBusyUntil())
    {
        _spiRx = _spiPending;
        _spiPendingValid = false;
    }
}

uint8_t PortDecoder_Next::SpiRead()
{
    const double now = Now();
    if (now < SpiBusyUntil())
    {
        // an early read gets the old miso_dat and starts nothing (spi_master.vhd 108-132)
        _spiTooFast++;
        if (_spiTap.Active()) [[unlikely]]
            _spiTap.SpiEarly(SpiTapSlot(), false, 0xFF);
        return _spiRx;
    }
    SpiSettle(now);
    const uint8_t result = _spiRx;
    // a read starts a transfer of #FF; its answer is what the next read returns
    _spiPending = _spiSelected >= 0 ? _sd[_spiSelected].exchange(0xFF) : 0xFF;
    if (_spiTap.Active()) [[unlikely]]
        _spiTap.SpiByte(SpiTapSlot(), 0xFF, _spiPending, true);
    _spiPendingValid = true;
    _spiBusyRatio = SpeedRatio();
    _spiBusyUntil = now + kSpiByteClocks / _spiBusyRatio;
    return result;
}

/// endregion

/// region <Speed, machine type and frame timing>

bool PortDecoder_Next::PagingAllowed(uint16_t port) const
{
    switch (_board->MachineType())
    {
        case 1:
            return false;  // 48K: no paging ports
        case 2:
        case 4:
            return port != 0x1FFD;  // 128K / Pentagon: no #1FFD
        default:
            return true;
    }
}

void PortDecoder_Next::SetCpuSpeed(uint8_t ratio)
{
    // zxnext.vhd 5810-5818: cpu_speed takes nr_07_cpu_speed at the first clock without MREQ / IORQ / M1 after the write, inside the
    // writing instruction. The emulator switches before the next instruction's fetch (ApplyPendingSpeed); a frame start that comes first
    // takes it over the same way (Z80::ApplyQueuedFrequencyMultiplier composes it with the host speed control)
    _state->hw_turbo_ratio = ratio;
    _ratio = ratio;
    _speedPending = ratio != _state->hw_turbo_ratio_applied;
    UpdateContention();
}

void PortDecoder_Next::ApplyPendingSpeed()
{
    _speedPending = false;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!z80 || _state->hw_turbo_ratio == _state->hw_turbo_ratio_applied)
        return;
    _speedChangedAt = Now();
    z80->ApplyHardwareTurboNow();
    SpiBusyUntil();  // a byte in flight keeps its 16 clocks: the rest is counted in the new ones now, not lazily after a second switch
}

void PortDecoder_Next::SetMachineTiming(uint8_t timing)
{
    _pendingTiming = timing;
    if (!_context->config.frame || timing == _state->ula_timing_class)
        return;
    // Before the first frame (reset) there is nothing to wait for
    if (_context->emulatorState.frame_counter == 0 && _context->pCore->GetZ80()->t == 0)
        ApplyTiming(timing);
}

/// NR #8E (registers.txt): 7 = #DFFD bit 0, 6:4 = #7FFD bits 2:0, 3 = change the RAM bank, 2 = #1FFD bit 0 (special
/// all-RAM paging); normal paging: 1:0 = #1FFD bit 2, #7FFD bit 4; all-RAM: 1:0 = #1FFD bit 2, bit 1. A write acts
/// as if by the port writes, always on the ROM / all-RAM part, on the RAM bank only with bit 3
void PortDecoder_Next::WriteMemoryMapping(uint8_t value)
{
    EmulatorState& state = *_state;
    if (value & 0x08)
    {
        state.p7FFD = static_cast<uint8_t>((state.p7FFD & ~0x07) | ((value >> 4) & 0x07));
        // zxnext.vhd: bit 3 of #DFFD is cleared (the Profi mapping is hard-wired off) and bits 2:0 become 00 & NR #8E bit 7
        Mem().SetExtendedBank(static_cast<uint8_t>(value >> 7));
    }
    uint8_t p1ffd = static_cast<uint8_t>(state.p1FFD & ~0x07);
    if (value & 0x04)
    {
        p1ffd = static_cast<uint8_t>(p1ffd | 0x01 | ((value & 0x02) << 1) | ((value & 0x01) << 1));
    }
    else
    {
        p1ffd = static_cast<uint8_t>(p1ffd | ((value & 0x02) << 1) | ((value & 0x01) << 1));  // #1FFD bit 1 = d0 here too (no effect on the map)
        state.p7FFD = static_cast<uint8_t>((state.p7FFD & ~0x10) | ((value & 0x01) << 4));
    }
    state.p1FFD = p1ffd;
    Mem().ApplyClassicPaging(state.p7FFD, state.p1FFD, (value & 0x08) != 0);
}

uint8_t PortDecoder_Next::ReadMemoryMapping() const
{
    const EmulatorState& state = *_state;
    uint8_t value = static_cast<uint8_t>(((Mem().GetExtendedBank() & 1) << 7) | ((state.p7FFD & 7) << 4) | 0x08 | ((state.p1FFD & 1) << 2));
    value |= static_cast<uint8_t>((state.p1FFD & 0x04) >> 1);
    value |= (state.p1FFD & 1) ? static_cast<uint8_t>((state.p1FFD >> 1) & 1) : static_cast<uint8_t>((state.p7FFD >> 4) & 1);
    return value;
}

void PortDecoder_Next::SetContentionDisabled(bool disabled)
{
    // zxnext.vhd 5822: eff_nr_08_contention_disable follows the register in an idle bus cycle while hc(8) = 1, so a switch inside the
    // line's contention window takes effect when the window ends
    _nr08Pending = false;
    if (disabled == _nr08NoContention)
        return;
    const UlaContention* ula = _context->pUlaContention;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const uint32_t at = ula && z80 ? ula->LineWindowEndFrom(z80->t) : 0;
    if (!z80 || at <= z80->t)
    {
        _nr08NoContention = disabled;
        UpdateContention();
        return;
    }
    _nr08Pending = true;
    _nr08PendingValue = disabled;
    _nr08ApplyAt = at;
    _nr08PendingFrame = _state->frame_counter;
}

void PortDecoder_Next::ResetContentionDisabled()
{
    _nr08Pending = false;
    _nr08NoContention = false;
    UpdateContention();
}

void PortDecoder_Next::SettleContentionDisabled()
{
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    // The window ends inside its line: a new frame (the T count restarted) is past it
    if (z80 && z80->t < _nr08ApplyAt && _state->frame_counter == _nr08PendingFrame)
        return;
    _nr08Pending = false;
    _nr08NoContention = _nr08PendingValue;
    UpdateContention();
}

void PortDecoder_Next::UpdateContention()
{
    const uint8_t timing = _state->ula_timing_class;
    const bool family = timing >= 1 && timing <= 3;  // the Pentagon has none
    const bool off = _ratio > 1 || _nr08NoContention;
    _state->hw_contention_disabled = off ? 1 : 0;
    Mem().SetContentionRule(timing);
    Mem().SetSramWait28(_ratio == 8);
    if (UlaContention* ula = _context->pUlaContention)
    {
        ula->SetContentionEnabled(family && !off);
        ula->SetGateArray(timing == 3);
        ula->SetWindowedWait(true);
        ula->SetOddPortRule([](const void* self, uint16_t port) { return static_cast<const PortDecoder_Next*>(self)->OddPortContended(port); }, this);
    }
    if (Core* core = _context->pCore)
        core->SelectMemoryInterface();
}

void PortDecoder_Next::OnFrameEnd()
{
    _board->LatchVideoFrameSync();  // video_frame_sync: eff_nr_05_5060, eff_nr_05_scandouble_en, eff_nr_09_scanlines
    _board->Copper().Sync();  // a frame no line was drawn for (turbo) still runs the copper
    // changes to the video timing take place at the frame sync: the family of NR #03 and the 50 / 60 Hz bit of NR #05
    const uint8_t timing = _pendingTiming ? _pendingTiming : _state->ula_timing_class;
    if (timing && (timing != _state->ula_timing_class || SixtyHzWanted(timing) != (_state->ula_sixty_hz != 0)))
        ApplyTiming(timing);
}

uint32_t PortDecoder_Next::FrameDurationUs(uint32_t frame) const
{
    // a T-state is 8 clocks of CLK_28; rounded up as CalculateFrameDurationUs (the same at 28 MHz)
    const uint64_t hz = _board->SystemClockHz();
    return static_cast<uint32_t>((static_cast<uint64_t>(frame) * 8'000'000ull + hz - 1) / hz);
}

void PortDecoder_Next::ApplySystemClock()
{
    // NR #11 sets CLK_28 (the PLL): the machine counts the same clocks, the real time they take follows. The frame's pacing, the sound's
    // playback rate and the UARTs' baud rates change with it
    const uint32_t hz = _board->SystemClockHz();
    _uart.SetSystemClock(hz);
    _context->config.frame_duration_us = FrameDurationUs(_context->config.frame);
    if (_context->pSoundManager)
        _context->pSoundManager->setMachineClockScale(static_cast<double>(hz) / 28000000.0);
}

void PortDecoder_Next::SystemClockChanged()
{
    ApplySystemClock();
    // the top level's power-on reset is reset_hard there: the expansion bus reset pulses and the ESP restarts (bus_rst_n_io)
    if (auto* esp = dynamic_cast<EspModule*>(_uart.Peer(0)))
    {
        esp->SetResetPin(true);
        esp->SetResetPin((_board->Stored(0x02) & 0x80) != 0);
    }
}

bool PortDecoder_Next::SixtyHzWanted(uint8_t timing) const
{
    return timing != 4 && _board->EffectiveSixtyHz();  // the Pentagon is always 50 Hz
}

void PortDecoder_Next::ApplyTiming(uint8_t timing)
{
    NextTiming t;
    const bool sixty = SixtyHzWanted(timing);
    if (!NextTimingFor(timing, sixty, t))
        return;
    CONFIG& config = _context->config;
    config.frame = t.frame;
    config.t_line = t.line;
    config.intstart = t.intStart;
    config.intlen = t.intLength;
    config.frame_duration_us = FrameDurationUs(t.frame);
    _state->ula_timing_class = timing;
    _state->ula_sixty_hz = t.sixty ? 1 : 0;
    _interrupts->SetGeometry(t);
    _pendingTiming = 0;
    UpdateContention();
}

/// endregion

void PortDecoder_Next::RecordInt(NextIntEvent event)
{
    event.frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        event.t = z80->t;
        event.pc = z80->m1_pc;
    }
    _intJournal.Record(event);
}

const char* PortDecoder_Next::DriveNmiRefusal() const
{
    // zxnext.vhd: hotkey_drive / nmi_sw_gen_divmmc, and nr_06_button_drive_nmi_en (NR #06 bit 4), with no Multiface session; the
    // state machine pulses /NMI once and the DivMMC's button latch maps its ROM in at the #0066 fetch. A request while the
    // handler is in, or still on its way, is lost (nmi_activated)
    if (!(_board->Stored(0x06) & 0x10))
        return "the DRIVE button is off (NR #06 bit 4)";
    if (_divMmc->NmiHold())
        return "the DivMMC NMI handler is on its way or running";
    if (_multiface->IsActive())
        return "a Multiface session is open";
    return nullptr;
}

const char* PortDecoder_Next::MultifaceNmiRefusal() const
{
    // zxnext.vhd nmi_assert_mf: NR #06 bit 3 (the M1 button's enable), and CONMEM off (port #E3 bit 7) and no DivMMC handler
    if (!(_board->Stored(0x06) & 0x08))
        return "the M1 button is off (NR #06 bit 3)";
    if (_divMmc->ReadPort() & 0x80)
        return "CONMEM is on (port #E3 bit 7)";
    if (_divMmc->NmiHold())
        return "the DivMMC NMI handler is on its way or running";
    if (_multiface->NmiHold())
        return "a Multiface NMI session is open";
    return nullptr;
}

bool PortDecoder_Next::GenerateDriveNmi()
{
    const char* refusal = DriveNmiRefusal();
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::NmiRequest;
        e.source = "drive_button";
        e.accepted = refusal == nullptr;
        e.reason = refusal ? refusal : "";
        RecordInt(e);
    }
    if (refusal)
        return false;
    _divMmc->PressButton();
    _context->pCore->GetZ80()->RequestNonMaskedInterrupt();
    return true;
}

bool PortDecoder_Next::GenerateMultifaceNmi()
{
    return RaiseMultifaceNmi("m1_button");
}

void PortDecoder_Next::IoTrap(uint16_t port, bool write, uint8_t value)
{
    // zxnext.vhd 2578-2602, 2723-2725: port_fd = A1:A0 = 01, port_xffd adds A15:A14 = 00; #2FFD (A13:A12 = 10) read, #3FFD (11) read
    // and write; not a write the DAC's #F1 / #F9 owns (port_fd_conflict_wr). NR #D8 bit 0 qualifies it all
    if (!(_board->Stored(0xD8) & 0x01) || (port & 3) != 1 || (port >> 14) != 0)
        return;
    const unsigned select = (port >> 12) & 3;
    uint8_t cause;
    if (select == 2 && !write)
        cause = 1;
    else if (select == 3)
        cause = write ? 3 : 2;
    else
        return;
    const uint8_t low = static_cast<uint8_t>(port);
    if (write && (low == 0xF1 || low == 0xF9) && PortEnabled(18))
        return;
    // the cause and the byte are taken while no NMI is on its way (nmi_accept_cause: the state machine idle or fetching)
    if (!_multiface->NmiHold() && !_divMmc->NmiHold())
        _board->LatchIoTrap(cause, write, value);
    RaiseMultifaceNmi("io_trap");  // nmi_sw_gen_mf: the same assertion as the M1 button, behind NR #06 bit 3
}

bool PortDecoder_Next::RaiseMultifaceNmi(const char* source)
{
    const char* refusal = MultifaceNmiRefusal();
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::NmiRequest;
        e.source = source;
        e.accepted = refusal == nullptr;
        e.reason = refusal ? refusal : "";
        RecordInt(e);
    }
    if (refusal)
        return false;
    _multiface->PressButton();
    _context->pCore->GetZ80()->RequestNonMaskedInterrupt();
    return true;
}

void PortDecoder_Next::OnRetn()
{
    // divmmc_retn_seen = z80_retn_seen and not mf_is_active (the Multiface's session ends first)
    const bool multifaceActive = _multiface->IsActive();
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::Retn;
        e.source = multifaceActive ? "multiface" : (_divMmc->NmiHold() ? "divmmc" : "none");
        RecordInt(e);
    }
    _multiface->OnRetn();
    if (!multifaceActive)
        _divMmc->OnRetn();
}

bool PortDecoder_Next::RequestBoardNmi()
{
    // The DRIVE button (F10 on the board, the host's NMI action): the board owns the request, no plain pulse from the caller
    GenerateDriveNmi();
    return true;
}

/// region <TTD>

std::vector<ttd::TTDDeviceType> PortDecoder_Next::GetTTDModelStateTypes() const
{
    return {ttd::TTDDeviceType::NextState, ttd::TTDDeviceType::NextSystemArea};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Next::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDNextState>(*const_cast<PortDecoder_Next*>(this)));
    serializers.push_back(std::make_unique<ttd::TTDNextSystemArea>(*_context->pMemory));
    return serializers;
}

/// endregion </TTD>

/// region <UART peers>

void PortDecoder_Next::SetUartPeer(unsigned channel, ISerialPeer* peer)
{
    if (channel >= NextUart::kChannels)
        return;
    SyncPiLine();
    _uart.Advance(Now28());
    if (ISerialPeer* old = _uart.Peer(channel); old && old != peer)
    {
        old->onReceive = nullptr;
        old->SetClock(nullptr, 0);
    }
    if (peer)
    {
        constexpr uint32_t kBaseClockHz = 3500000;
        peer->SetClock([this]() { return static_cast<uint64_t>(Now()); }, kBaseClockHz);
        // Bytes from outside arrive at their emulated time: the UART catches up first
        peer->onReceive = [this]() {
            SyncPiLine();
            _uart.Advance(Now28());
            _uart.PollInterrupts(_interrupts->UartEnables());
        };
    }
    _uart.SetPeer(channel, peer);
    if (channel == 0)
        BoardPinsChanged();  // a module fitted now sees the pin as it is
}

void PortDecoder_Next::SetJoystickUartPeer(ISerialPeer* peer)
{
    _uart.Advance(Now28());
    if (ISerialPeer* old = _uart.JoystickPeer(); old && old != peer)
    {
        old->onReceive = nullptr;
        old->SetClock(nullptr, 0);
    }
    if (peer)
    {
        constexpr uint32_t kBaseClockHz = 3500000;
        peer->SetClock([this]() { return static_cast<uint64_t>(Now()); }, kBaseClockHz);
        peer->onReceive = [this]() {
            _uart.Advance(Now28());
            _uart.PollInterrupts(_interrupts->UartEnables());
        };
    }
    _uart.SetJoystickPeer(peer);
}

void PortDecoder_Next::UpdateUartRoute(bool advance)
{
    // zxnext.vhd: joy_iomode_uart_en = NR #0B bit 7 and bit 5; nr_0b_joy_iomode_0 (bit 0) picks the UART whose line the connector takes
    const uint8_t nr0b = _board->Stored(0x0B);
    const bool on = (nr0b & 0x80) && (nr0b & 0x20);
    if (advance)
        _uart.Advance(Now28());
    _uart.SetJoystickRoute(on, nr0b & 1);
}

void PortDecoder_Next::BoardPinsChanged()
{
    UpdateUartRoute();
    // zxnext.vhd: o_ESP_GPIO_0 = NR #A9 bit 0, driven while NR #A8 bit 0 enables it (else the pad floats, pulled up)
    const bool low = (_board->Stored(0xA8) & 1) && !(_board->Stored(0xA9) & 1);
    if (auto* esp = dynamic_cast<EspModule*>(_uart.Peer(0)))
    {
        esp->SetFlashPin(low);
        // NR #02 bit 7 asserts and holds the expansion bus /RESET net (zxnext_top bus_rst_n_io), the one the ESP's reset is on too
        // (registers.txt: "reset to the expansion bus and the esp wifi"); the chip stops while it is held and boots at the release
        esp->SetResetPin((_board->Stored(0x02) & 0x80) != 0);
    }
}

PortDecoder::NetworkCapabilities PortDecoder_Next::DescribeNetwork()
{
    NetworkCapabilities caps;
    caps.serialPort = NetworkCapabilities::SerialPort::NextUart;
    caps.attachSerialPeer = [this](ISerialPeer* peer) { SetUartPeer(0, peer); };
    caps.attachSecondSerialPeer = [this](ISerialPeer* peer) { SetUartPeer(1, peer); };
    caps.attachJoystickSerialPeer = [this](ISerialPeer* peer) { SetJoystickUartPeer(peer); };
    caps.serialBaud = [this]() { return _uart.GetView(0).baud; };
    return caps;
}

/// endregion
