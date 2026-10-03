#include "debugger/ttd/atm/ttdatm2kbc.h"

#include <cstring>
#include <memory>
#include <vector>

#include "emulator/io/keyboard/atm2kbc.h"

namespace ttd
{

size_t TTDAtm2Kbc::TTDStateSize() const
{
    return sizeof(Atm2Kbc::State);
}

void TTDAtm2Kbc::TTDSaveState(uint8_t* dst) const
{
    auto state = std::make_unique<Atm2Kbc::State>();
    _kbc.SaveState(*state);
    std::memcpy(dst, state.get(), sizeof(Atm2Kbc::State));
}

void TTDAtm2Kbc::TTDLoadState(const uint8_t* src)
{
    auto state = std::make_unique<Atm2Kbc::State>();
    std::memcpy(state.get(), src, sizeof(Atm2Kbc::State));
    _kbc.LoadState(*state);
}

uint64_t TTDAtm2Kbc::TTDHashState() const
{
    // The MCU's program position, clock and RAM: enough to see a replay leave the recorded path
    const mcs51::Mcs51* cpu = _kbc.Cpu();
    if (!cpu)
        return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(cpu->Pc());
    mix(cpu->Clock());
    for (int a = 0; a < 256; ++a)
        mix(cpu->Ram(static_cast<uint8_t>(a)));
    mix(_kbc.GetKeyboard().count);
    return h;
}

TTDDeviceDescriptor TTDAtm2Kbc::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    d.firmwareFingerprint = _kbc.FirmwareHash();
    // Offsets taken from the structure itself, so a layout change moves them along
    static const std::vector<TTDTimeField> fields = [] {
        auto s = std::make_unique<Atm2Kbc::State>();
        const auto* base = reinterpret_cast<const uint8_t*>(s.get());
        auto at = [base](const void* field, uint8_t width) {
            return TTDTimeField{static_cast<uint16_t>(static_cast<const uint8_t*>(field) - base), width};
        };
        std::vector<TTDTimeField> f = {
            at(&s->tBase, 8),       at(&s->mcuBase, 8),          at(&s->lastNow, 8),
            at(&s->answerClock, 8), at(&s->reads, 8),            at(&s->cpu.clock, 8),
            at(&s->cpu.instructions, 8),
            at(&s->cpu.sfr[0x8A - 0x80], 1),   // TL0
            at(&s->cpu.sfr[0x8C - 0x80], 1),   // TH0
            at(&s->cpu.sfr[0xCC - 0x80], 1),   // TL2
            at(&s->cpu.sfr[0xCD - 0x80], 1),   // TH2
        };
        for (const uint64_t& v : s->cpu.visibleAt)
            f.push_back(at(&v, 8));
        return f;
    }();
    d.timeFields = fields;
    return d;
}

}  // namespace ttd
