// libopl4 — FM synthesis engine interface (rearchitecture §4.3).
//
// The bus layer (src/fm/fmbus.h) owns everything guest-visible: register
// shadow, bank aliasing, NEW/NEW2, timers, status, routing. A synthesis
// backend receives already-de-aliased register writes and emits samples.
// It owns no timers, no status byte, no NEW/NEW2, no routing.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opl4
{

struct FmCaps
{
    bool perChannelTaps = false; // can emit the 18 per-channel values
    bool exactStateSave = false; // save/restore is bit-exact and cheap
    const char* name = "";       // engine name, e.g. "opl4"
};

struct FmOutput
{
    int32_t channel[18] = {};   // valid only if FmCaps::perChannelTaps
    int32_t mixL = 0, mixR = 0; // always valid; routing already applied by the
                                // backend ONLY when perChannelTaps == false
};

class IFmSynth
{
public:
    virtual ~IFmSynth() = default;

    virtual void Reset() = 0;

    // De-aliased register write. reg is 0x000..0x1FF: bank already resolved,
    // NEW/NEW2 gating already applied by FmBus. The backend keeps whatever
    // internal shadow it needs but is never the authority on bus behaviour.
    virtual void WriteReg(uint16_t reg, uint8_t data) = 0;

    // One 684-clock FM tick.
    virtual void Advance(FmOutput& out) = 0;

    virtual FmCaps Caps() const = 0;

    // Variable-size state. LayoutTag() changes whenever the layout changes;
    // the TTD store refuses a mismatched tag.
    virtual size_t StateSize() const = 0;
    virtual void SaveState(uint8_t* dst) const = 0;
    virtual void LoadState(const uint8_t* src) = 0;
    virtual uint32_t LayoutTag() const = 0;
};

} // namespace opl4
