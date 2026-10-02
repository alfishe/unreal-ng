#pragma once

/// @file ttdevomouse.h
/// @brief TTD serializer for the ZX-Evo AVR's PS/2 mouse (PeripheralId::EvoMouse):
/// the three registers the Z80 reads at the Kempston addresses and whether a
/// mouse is plugged in. Its resolution is an AVR RTC cell (the Ds12887 blob).

#include "debugger/ttd/ttdserializable.h"

class EvoAvrMouse;

namespace ttd
{

class TTDEvoMouse : public TTDSerializable
{
public:
    explicit TTDEvoMouse(EvoAvrMouse& mouse) : _mouse(mouse) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoMouse"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoMouse; }
    uint64_t TTDHashState() const override;

private:
    EvoAvrMouse& _mouse;
};

}  // namespace ttd
