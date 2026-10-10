#include "stdafx.h"

#include "portdecoder_next.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/io/z80n/nexttiming.h"
#include <filesystem>

#include "common/filehelper.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
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
    _divMmc = std::make_unique<NextDivMmc>(&Mem(), _board.get());
    _divMmc->isBasicRomPaged = [this]() { return Mem().BasicRomVisible(); };
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
}

PortDecoder_Next::~PortDecoder_Next()
{
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
    _spiBusyUntil = 0;

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
    if (low == NextDivMmc::kPort)
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
    if (port == kPortRegSelect || port == kPortRegData || low == kPortSpiData)
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
            result = port == kPortRegSelect ? _board->SelectedRegister() : _board->ReadSelected();
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
    else if (static_cast<uint8_t>(port) == NextDivMmc::kPort)
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
    else if (static_cast<uint8_t>(port) == kPortSpiSelect)
        SpiSelect(value);
    else if (static_cast<uint8_t>(port) == kPortSpiData)
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
        while (Now28() < _spiBusyUntil)
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

/// [NEXT] SdCard: a folder is presented as a FAT16 card (HostFolderFat, writes kept in the session), a file as a raw image
void PortDecoder_Next::InsertConfiguredCard()
{
    const std::string path = _context->config.next_sd_path;
    if (path.empty() || _sd[0].present())
        return;
    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved) && !std::filesystem::exists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    std::error_code ec;
    if (std::filesystem::is_directory(resolved, ec))
    {
        FolderSnapshot snapshot;
        FolderScanOptions scan;
        std::string error;
        if (!FolderSnapshot::Scan(resolved, scan, snapshot, &error))
        {
            MLOGERROR("PortDecoder_Next: SD card folder '%s': %s", path.c_str(), error.c_str());
            return;
        }
        FatVolumeOptions options;
        std::vector<std::string> report;
        auto card = HostFolderFat::Build(snapshot, options, &error, &report);
        if (!card)
        {
            MLOGERROR("PortDecoder_Next: SD card folder '%s' does not fit a FAT16 card: %s", path.c_str(), error.c_str());
            return;
        }
        _sd[0].insert(std::move(card), SdCardSpi::WriteMode::Session);
    }
    else if (!_sd[0].open(resolved, SdCardSpi::WriteMode::Session))
        MLOGERROR("PortDecoder_Next: SD card image '%s' not found", path.c_str());
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
    _dma.Reset();         // zxnext.vhd: the DMA's reset_i is the hard or the soft reset
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
    return _sd[index & 1].insert(std::move(media), mode, SdCardSpi::Type::SDHC);
}

bool PortDecoder_Next::InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode)
{
    return _sd[index & 1].open(path, mode, SdCardSpi::Type::SDHC);
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
    if (now < _spiBusyUntil)
    {
        _spiTooFast++;
        return;
    }
    _spiRx = _spiSelected >= 0 ? _sd[_spiSelected].exchange(value) : 0xFF;
    _spiBusyUntil = now + kSpiByteClocks / SpeedRatio();
}

uint8_t PortDecoder_Next::SpiRead()
{
    const uint8_t result = _spiRx;
    const double now = Now();
    if (now < _spiBusyUntil)
    {
        _spiTooFast++;
        return result;
    }
    // a read starts a transfer of #FF; its answer is what the next read returns
    _spiRx = _spiSelected >= 0 ? _sd[_spiSelected].exchange(0xFF) : 0xFF;
    _spiBusyUntil = now + kSpiByteClocks / SpeedRatio();
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

bool PortDecoder_Next::GenerateDriveNmi()
{
    // zxnext.vhd: hotkey_drive / nmi_sw_gen_divmmc, and nr_06_button_drive_nmi_en (NR #06 bit 4), with no Multiface session; the
    // state machine pulses /NMI once and the DivMMC's button latch maps its ROM in at the #0066 fetch. A request while the
    // handler is in, or still on its way, is lost (nmi_activated)
    if (!(_board->Stored(0x06) & 0x10) || _divMmc->NmiHold() || _multiface->IsActive())
        return false;
    _divMmc->PressButton();
    _context->pCore->GetZ80()->RequestNonMaskedInterrupt();
    return true;
}

bool PortDecoder_Next::GenerateMultifaceNmi()
{
    // zxnext.vhd nmi_assert_mf: NR #06 bit 3 (the M1 button's enable), and CONMEM off (port #E3 bit 7) and no DivMMC handler
    if (!(_board->Stored(0x06) & 0x08) || (_divMmc->ReadPort() & 0x80) || _divMmc->NmiHold() || _multiface->NmiHold())
        return false;
    _multiface->PressButton();
    _context->pCore->GetZ80()->RequestNonMaskedInterrupt();
    return true;
}

void PortDecoder_Next::OnRetn()
{
    // divmmc_retn_seen = z80_retn_seen and not mf_is_active (the Multiface's session ends first)
    const bool multifaceActive = _multiface->IsActive();
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
