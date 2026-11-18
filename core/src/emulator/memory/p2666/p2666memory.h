#pragma once
#include "emulator/memory/memory.h"

/// Pentagon 2.666 LE memory (MM_P2666): the four windows come from the decoder's dispatcher maps
/// (PortDecoderP2666::UpdateModelMemoryBanks). The board's DOS signal is the decoder's M1 hook (HR §3): the generic
/// TR-DOS session machinery (CF_SETDOSROM at #3Dxx, CF_LEAVEDOSRAM / CF_LEAVEDOSADR) stays off, it would leave DOS
/// where the board keeps it (cpm = 0, #BF.0 = 1) and miss the CP/M re-entry at #4000.
class P2666Memory : public Memory
{
public:
    P2666Memory() = delete;
    explicit P2666Memory(EmulatorContext* context) : Memory(context) {}
    ~P2666Memory() override = default;

protected:
    bool UpdateModelBanks() override;
};
