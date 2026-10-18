#include "sprinterpldtmk.h"

#include <cstring>

#include "emulator/ports/models/portdecoder_sprinter.h"

SprinterPldTmk::SprinterPldTmk()
{
    _descriptor.name = "TMK";
    _descriptor.fullHash = kFullHash;
    _descriptor.headHash = kHeadHash;
    _descriptor.streams = {{kFullHash, "tmk79's K30 build SP2_ACEX.ttf of 2026-02-25 (as an .ACX: the 59 215 bytes)"}};
}

bool SprinterPldTmk::ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value)
{
    (void)decoder; (void)port; (void)value;
    // ACC_RECT_W_ENA = DFF(DCPP == #81): not gated by the direction, a read reloads the row counter too. Nothing
    // drives the data bus for #80 / #81: Standard's answer for a free code
    if (code == kCodeRectWidth)
        _state.rectCounter = _state.rectWidth;
    return false;
}

bool SprinterPldTmk::WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value)
{
    (void)decoder; (void)port;
    switch (code)
    {
        case kCodeCtrl:
            _state.ctrl = value;
            return true;
        case kCodeRectWidth:
            _state.rectWidth = value;
            _state.rectCounter = value;  // RECT_COUNTER loads ACC_RECT_W while the code is on the bus
            return true;
        default:
            return false;
    }
}

SprinterAccelerator* SprinterPldTmk::Accelerator(PortDecoder_Sprinter& decoder)
{
    SprinterAccelerator& accelerator = decoder.StandardAccelerator();
    accelerator.AttachTmk(&_state);
    return &accelerator;
}

void SprinterPldTmk::OnReset(SprinterResetKind kind, SprinterPldState& pld)
{
    (void)kind; (void)pld;
    // ACC_CTRL, ACC_RECT_W and RECT_COUNTER clear on /RESET (as ACC_MODE does, SprinterAccelerator::Reset); the
    // buffer RAM keeps its contents
    _state.ctrl = 0;
    _state.rectWidth = 0;
    _state.rectCounter = 0;
}

void SprinterPldTmk::SaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_state, sizeof(_state));
}

void SprinterPldTmk::LoadState(const uint8_t* src)
{
    std::memcpy(&_state, src, sizeof(_state));
}
