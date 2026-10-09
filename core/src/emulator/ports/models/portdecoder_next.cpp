#include "stdafx.h"

#include "portdecoder_next.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/io/z80n/nexttiming.h"
#include "emulator/sound/audio.h"

PortDecoder_Next::PortDecoder_Next(EmulatorContext* context) : PortDecoder_Spectrum128(context)
{
    _board = std::make_unique<NextBoard>(&Mem());
    _board->SetMachine(this);
}

PortDecoder_Next::~PortDecoder_Next()
{
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
    _board->Reset(true);
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
    return PortDecoder_Spectrum128::DecodePortIn(port, pc);
}

void PortDecoder_Next::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;
    disp.wasDecoded = true;
    disp.wasHandledInline = true;

    if (static_cast<uint8_t>(port) == kPortSpiSelect)
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
        return;
    }
    disp.decodedPort = port;
    OnPortOutComplete(port, value, pc, disp);
}

/// region <Reset>

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
    Mem().ApplyClassicPaging(0, 0);
    _spiSelected = -1;
    if (_engine)
        _engine->InvalidateBoundary();
}

/// endregion

/// region <SPI and the SD cards>

double PortDecoder_Next::Now() const
{
    const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1u;
    return static_cast<double>(_state->t_states) + static_cast<double>(_context->pCore->GetZ80()->t) / multiplier;
}

bool PortDecoder_Next::InsertSdCard(unsigned index, std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode)
{
    return _sd[index & 1].insert(std::move(media), mode);
}

bool PortDecoder_Next::InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode)
{
    return _sd[index & 1].open(path, mode);
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

void PortDecoder_Next::OnFrameEnd()
{
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
    _pendingTiming = 0;
}

/// endregion
