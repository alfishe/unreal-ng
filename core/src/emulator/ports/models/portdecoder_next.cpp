#include "stdafx.h"

#include "portdecoder_next.h"

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
#include "emulator/video/ulacontention.h"

PortDecoder_Next::PortDecoder_Next(EmulatorContext* context) : PortDecoder_Spectrum128(context)
{
    _board = std::make_unique<NextBoard>(&Mem());
    _board->SetMachine(this);
    // the picture follows the beam: a video register written by the program applies from the line the beam is on
    _board->SetBeforeCpuWrite([this]() {
        if (_context->pScreen)
            _context->pScreen->UpdateScreen();
    });
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
    _m1Chain.multiface = _multiface.get();
    _m1Chain.divMmc = _divMmc.get();
    _interrupts->SetPoller([this]() { _ctc.Advance(Now28()); });
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
    }
    if (_context->pMediaManager)
    {
        _context->pMediaManager->RegisterSlot(_sdSlot0);
        _context->pMediaManager->RegisterSlot(_sdSlot1);
    }
}

PortDecoder_Next::~PortDecoder_Next()
{
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

    Mem().ResetMmu();
    Mem().ApplyClassicPaging(0, 0);
    InsertConfiguredCard();
    _board->Reset(true);
    _ctc.Reset();
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

uint8_t PortDecoder_Next::DecodePortIn(uint16_t port, uint16_t pc)
{
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
    if (low == NextCtc::kPortLow && (port >> 11) == 0x03)
    {
        // CTC: A15:A11 = 00011, A10:A8 the channel (4-7 are not implemented)
        const unsigned channel = (port >> 8) & 7;
        const uint8_t value = channel < NextCtc::kChannels ? _ctc.Read(channel, Now28()) : 0xFF;
        _lastPortDecoded = channel < NextCtc::kChannels;
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
    if ((port & 0xC002) == 0xC000 && !IsPort_DFFD_Next(port))  // #FFFD: the selected AY's register
    {
        const uint8_t value = _audio->ReadData();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (low == 0x1F)
    {
        // Kempston joystick 1: built into the board (zxnext.vhd port_1f_lsb), 000FUDLR active high, 0 with nothing pressed
        const uint8_t value = Default_Port_KempstonJoystick_In();
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
        const uint8_t value = _board->Sprites().ReadStatus();
        _lastPortDecoded = true;
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, value, pc, disp);
        return value;
    }
    if (low == 0xFF && (_board->Stored(NextBoard::kRegPeripheral2) & 0x04))  // NR #08 bit 2: #FF reads the Timex mode
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
        const uint8_t value = _i2c.Read(port);
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
    const uint8_t value = PortDecoder_Spectrum128::DecodePortIn(port, pc);
    if (_inLog && !_lastPortDecoded)
    {
        PortUse& use = (*_inLog)[port];
        use.count++;
        use.last = value;
    }
    return value;
}

void PortDecoder_Next::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
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

    if (static_cast<uint8_t>(port) == NextCtc::kPortLow && (port >> 11) == 0x03)
    {
        const unsigned channel = (port >> 8) & 7;
        if (channel < NextCtc::kChannels)
            _ctc.Write(channel, value, Now28());
    }
    else if (port == NextI2c::kPortScl || port == NextI2c::kPortSda)
        _i2c.Write(port, value);
    else if (static_cast<uint8_t>(port) == NextDivMmc::kPort && _divMmc->PortEnabled())
        _divMmc->WritePort(value);
    else if (static_cast<uint8_t>(port) == 0xFF)
    {
        if (_context->pScreen)
            _context->pScreen->UpdateScreen();
        _board->Video().WritePortFf(value);
    }  // the Timex screen mode (NR #69 bits 5:0 alias it)
    else if (port == 0x123B)
        {
        if (_context->pScreen)
            _context->pScreen->UpdateScreen();
        _board->Video().WritePort123b(value);
        _board->RefreshLayer2Mapping();
        }
    else if ((port & 0xC002) == 0xC000 && !IsPort_DFFD_Next(port))
        _audio->WriteSelect(value);  // #FFFD
    else if ((port & 0xC002) == 0x8000)
        _audio->WriteData(value);    // #BFFD
    else if (port == 0x303B)
        _board->Sprites().WriteSlotSelect(value);
    else if (WriteDacPort(port, value))
        ;
    else if (static_cast<uint8_t>(port) == 0x6B)
        _dma.Write(value, false);
    else if (static_cast<uint8_t>(port) == 0x0B)
        _dma.Write(value, true);
    else if (static_cast<uint8_t>(port) == 0x57)
        _board->Sprites().WriteAttribute(value);
    else if (static_cast<uint8_t>(port) == 0x5B)
        _board->Sprites().WritePattern(value);
    else if (static_cast<uint8_t>(port) == kPortSpiSelect && SpiPortEnabled())
        SpiSelect(value);
    else if (static_cast<uint8_t>(port) == kPortSpiData && SpiPortEnabled())
        SpiWrite(value);
    else if (port == kPortRegSelect)
        _board->SelectRegister(value);
    else if (port == kPortRegData)
        _board->WriteSelected(value);
    else if (IsPort_7FFD_Next(port))
        Port_7FFD_Next(port, value, pc);
    else if (IsPort_1FFD_Next(port))
        Port_1FFD_Next(value);
    else if (IsPort_DFFD_Next(port))
        Port_DFFD_Next(value);
    else
    {
        PortDecoder_Spectrum128::DecodePortOut(port, value, pc);
        if (_outLog && !IsPort_FE(port) && !IsPort_7FFD(port) && (port & 0xC002) != 0xC000 && (port & 0xC002) != 0x8000)
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
    _dma.SetBus(std::move(bus));
}

/// NR #06 / #08 / #09: the AY or YM chips, turbosound, the DAC, the stereo mix
void PortDecoder_Next::AudioConfigChanged()
{
    const uint8_t nr06 = _board->Stored(0x06), nr08 = _board->Stored(0x08), nr09 = _board->Stored(0x09);
    _audio->Configure((nr06 & 3) == 0, (nr08 & 0x02) != 0, (nr08 & 0x08) != 0, (nr08 & 0x20) != 0, static_cast<uint8_t>((nr09 >> 5) & 7),
                      (nr06 & 0x40) && (nr08 & 0x10));
}

/// NR #2C (B, left), #2D (A and D, mono), #2E (C, right): the DAC's NextREG mirrors
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
    uint8_t door;
    switch (source)
    {
        case NextRegSource::NextReg: door = BRK_NR_DOOR_NEXTREG; break;
        case NextRegSource::Port: door = BRK_NR_DOOR_PORT; break;
        default: return;  // the copper is watched on its own (WatchCopper); the board itself (reset, a loader) has no one to stop
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
    if (!_context->pDebugManager) [[unlikely]]
        return;
    BreakpointManager& manager = *_context->pDebugManager->GetBreakpointsManager();
    if (manager.GetHotState()->hasMagic) [[unlikely]]
        WatchMagic(manager);
    if (!manager.GetHotState()->copperWatch) [[likely]]
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
    // The instructions the copper completes in (from, now]: a probe from the real copper's cursor, which lags by up to a line
    _watchSteps.clear();
    if (copper.Mode() != 0)
        copper.Probe(now, _watchSteps);
    auto inSpan = [&](uint64_t at) { return wrapped ? (at > from || at <= now) : (at > from && at <= now); };
    uint16_t found = BRK_INVALID;
    uint16_t address = 0;
    BreakpointHitKind kind = BreakpointHitKind::CopperInstruction;
    auto take = [&](uint16_t id, uint16_t at, BreakpointHitKind k) {
        if (id != BRK_INVALID && found == BRK_INVALID)
        {
            found = id;
            address = at;
            kind = k;
        }
    };
    for (const NextCopper::Step& step : _watchSteps)
    {
        if (!inSpan(step.at))
            continue;
        take(manager.HandleCopperPc(step.pc), step.pc, BreakpointHitKind::CopperInstruction);
        if (step.isMove && step.reg != 0)
            take(manager.HandleNextReg(BRK_NR_WRITE, step.reg, step.value, BRK_NR_DOOR_COPPER), step.reg, BreakpointHitKind::NextRegWrite);
    }
    take(manager.HandleBeam([&](uint16_t line, uint16_t hc) {
             const uint64_t at = copper.ClockOf(line, hc);
             return at != UINT64_MAX && inSpan(at);
         }),
         0, BreakpointHitKind::Beam);
    if (found != BRK_INVALID)
        FireBreakpoint(found, address, kind);
}

void PortDecoder_Next::StepDma()
{
    if (!_dma.Active())
        return;
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

void PortDecoder_Next::PerformReset(bool hard)
{
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
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::Reset;
        e.source = hard ? "hard" : "soft";
        e.value = _board->ResetBits();
        RecordInt(e);
    }
    _dma.Reset();         // zxnext.vhd: the DMA's reset_i is the hard or the soft reset
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
    return static_cast<double>(_state->t_states) + static_cast<double>(_context->pCore->GetZ80()->t) / multiplier;
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
    if (_spiSelected >= 0)
        _sd[_spiSelected].select(false);
    _spiSelected = line;
    if (line >= 0)
        _sd[line].select(true);
}

void PortDecoder_Next::SpiWrite(uint8_t value)
{
    const double now = Now();
    if (now < SpiBusyUntil())
    {
        _spiTooFast++;
        return;
    }
    SpiSettle(now);
    _spiPending = _spiSelected >= 0 ? _sd[_spiSelected].exchange(value) : 0xFF;
    _spiPendingValid = true;
    _spiBusyRatio = SpeedRatio();
    _spiBusyUntil = now + kSpiByteClocks / _spiBusyRatio;
}

double PortDecoder_Next::SpiBusyUntil()
{
    const double ratio = SpeedRatio();
    if (ratio != _spiBusyRatio)
    {
        // spi_master.vhd counts the CPU clock: the byte is 16 clocks whatever the speed. The new speed was taken over at the start of this
        // frame (Z80::BeginFrame), so what was left of the byte then is counted in the new clocks. A change older than the byte would have
        // left nothing in flight
        const double boundary = static_cast<double>(_state->t_states);
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
        return _spiRx;
    }
    SpiSettle(now);
    const uint8_t result = _spiRx;
    // a read starts a transfer of #FF; its answer is what the next read returns
    _spiPending = _spiSelected >= 0 ? _sd[_spiSelected].exchange(0xFF) : 0xFF;
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
    // Applied at the frame boundary by Z80::ApplyQueuedFrequencyMultiplier (composed with the host speed control)
    _state->hw_turbo_ratio = ratio;
    _ratio = ratio;
    UpdateContention();
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
        Mem().SetExtendedBank(static_cast<uint8_t>((Mem().GetExtendedBank() & ~0x01) | (value >> 7)));
    }
    uint8_t p1ffd = static_cast<uint8_t>(state.p1FFD & ~0x07);
    if (value & 0x04)
    {
        p1ffd = static_cast<uint8_t>(p1ffd | 0x01 | ((value & 0x02) << 1) | ((value & 0x01) << 1));
    }
    else
    {
        p1ffd = static_cast<uint8_t>(p1ffd | ((value & 0x02) << 1));
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
    _nr08NoContention = disabled;
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
    }
    if (Core* core = _context->pCore)
        core->SelectMemoryInterface();
}

void PortDecoder_Next::OnFrameEnd()
{
    _board->Copper().Sync();  // a frame no line was drawn for (turbo) still runs the copper
    if (_pendingTiming && _pendingTiming != _state->ula_timing_class)
        ApplyTiming(_pendingTiming);
}

void PortDecoder_Next::ApplyTiming(uint8_t timing)
{
    NextTiming t;
    if (!NextTimingFor(timing, t))
        return;
    CONFIG& config = _context->config;
    config.frame = t.frame;
    config.t_line = t.line;
    config.intstart = t.intStart;
    config.intlen = t.intLength;
    config.frame_duration_us = CalculateFrameDurationUs(t.frame);
    _state->ula_timing_class = timing;
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
    const char* refusal = MultifaceNmiRefusal();
    if (_intJournal.Enabled())
    {
        NextIntEvent e;
        e.kind = NextIntKind::NmiRequest;
        e.source = "m1_button";
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

std::vector<ttd::PeripheralId> PortDecoder_Next::GetTTDModelStateIds() const
{
    return {ttd::PeripheralId::NextState};
}

std::vector<std::unique_ptr<ttd::TTDSerializable>> PortDecoder_Next::CreateTTDSerializers() const
{
    std::vector<std::unique_ptr<ttd::TTDSerializable>> serializers;
    serializers.push_back(std::make_unique<ttd::TTDNextState>(*const_cast<PortDecoder_Next*>(this)));
    return serializers;
}

/// endregion </TTD>
