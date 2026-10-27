#include "debugger/ttd/profi/ttdprofixtkbc.h"

#include <cstring>
#include <memory>
#include <vector>

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

TTDDeviceDescriptor TTDProfiXtKbc::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    // The controller's time bases and the MCU's clock and instruction count advance every frame at an idle prompt
    // (measured 2026-10-09: 24 of 1,344 bytes; ATM2's controller declares the same). Offsets taken from the
    // structure itself, so a layout change moves them along
    static const std::vector<TTDTimeField> fields = [] {
        auto s = std::make_unique<ProfiXtKbc::State>();
        const auto* base = reinterpret_cast<const uint8_t*>(s.get());
        auto at = [base](const void* field, uint8_t width) {
            return TTDTimeField{static_cast<uint16_t>(static_cast<const uint8_t*>(field) - base), width};
        };
        return std::vector<TTDTimeField>{
            at(&s->tBase, 8),       at(&s->mcuBase, 8),     at(&s->lastNow, 8),       at(&s->answerClock, 8),
            at(&s->reads, 8),       at(&s->lastWaitMcu, 8), at(&s->cpu.clock, 8),     at(&s->cpu.instructions, 8),
        };
    }();
    d.timeFields = fields;
    return d;
}

}  // namespace ttd
