#include "debugger/ttd/profi/ttdprofixtkbc.h"

#include <cstring>
#include <memory>

#include "emulator/io/keyboard/profixtkbc.h"

namespace ttd
{

size_t TTDProfiXtKbc::TTDStateSize() const
{
    return sizeof(ProfiXtKbc::State);
}

void TTDProfiXtKbc::TTDSaveState(uint8_t* dst) const
{
    auto state = std::make_unique<ProfiXtKbc::State>();
    _kbc.SaveState(*state);
    std::memcpy(dst, state.get(), sizeof(ProfiXtKbc::State));
}

void TTDProfiXtKbc::TTDLoadState(const uint8_t* src)
{
    auto state = std::make_unique<ProfiXtKbc::State>();
    std::memcpy(state.get(), src, sizeof(ProfiXtKbc::State));
    _kbc.LoadState(*state);
}

uint64_t TTDProfiXtKbc::TTDHashState() const
{
    // The MCU's program position, clock and RAM, the latch and the half-rows: enough to see a replay leave the
    // recorded path
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    if (const mcs48::Mcs48* cpu = _kbc.Cpu())
    {
        mix(cpu->Pc());
        mix(cpu->Clock());
        for (int a = 0; a < 64; ++a)
            mix(cpu->Ram(static_cast<uint8_t>(a)));
    }
    mix(_kbc.Latch());
    for (int row = 0; row < 8; ++row)
        mix(_kbc.Row(row));
    return h;
}

}  // namespace ttd
