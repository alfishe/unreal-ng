#include "stdafx.h"

#include "portdecoder_next.h"

#include "common/modulelogger.h"
#include "emulator/cpu/core.h"

PortDecoder_Next::PortDecoder_Next(EmulatorContext* context) : PortDecoder_Spectrum128(context)
{
    _board = std::make_unique<NextBoard>(&Mem());
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
    _board->Reset();

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
    if (IsPagingLocked())
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
    if (IsPagingLocked())
        return;
    _state->p1FFD = value;
    Mem().ApplyClassicPaging(_state->p7FFD, _state->p1FFD);
}

void PortDecoder_Next::Port_DFFD_Next(uint8_t value)
{
    if (IsPagingLocked())
        return;
    Mem().SetExtendedBank(value);
    Mem().ApplyClassicPaging(_state->p7FFD, _state->p1FFD);
}

uint8_t PortDecoder_Next::DecodePortIn(uint16_t port, uint16_t pc)
{
    if (port == kPortRegSelect || port == kPortRegData)
    {
        PortDecodeDisposition disp;
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.decodedPort = port;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        const uint8_t result = port == kPortRegSelect ? _board->SelectedRegister() : _board->ReadSelected();
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

    if (port == kPortRegSelect)
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
