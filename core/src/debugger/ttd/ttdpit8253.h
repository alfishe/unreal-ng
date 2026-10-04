#pragma once
/// @file ttdpit8253.h
/// @brief TTD serializer for an 8253 PIT (Pit8253::State: the three counters, the clock position).

#include "debugger/ttd/ttdserializable.h"

class Pit8253;

namespace ttd
{

class TTDPit8253 : public TTDSerializable
{
public:
    explicit TTDPit8253(Pit8253& chip) : _chip(chip) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Pit8253"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Pit8253; }

private:
    Pit8253& _chip;
};

}  // namespace ttd
