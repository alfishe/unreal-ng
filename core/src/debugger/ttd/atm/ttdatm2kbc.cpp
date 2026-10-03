#include "debugger/ttd/atm/ttdatm2kbc.h"

#include <cstring>
#include <memory>

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
    return d;
}

}  // namespace ttd
