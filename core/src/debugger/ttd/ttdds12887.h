#pragma once

/// @file ttdds12887.h
/// @brief TTD serializer for the shared MC146818 / DS12887 clock chip.
///
/// One blob (PeripheralId::Ds12887) for every machine that wires the chip:
/// all cells, the address latch, the pending C flags and the time base. While
/// a session records, the chip runs on emulated time (TTDRecordingStarted), so
/// the blob holds no host-clock dependence and a replay reads the same time
/// the recording read.

#include "debugger/ttd/ttdserializable.h"

class Ds12887;

namespace ttd
{

class TTDDs12887 : public TTDSerializable
{
public:
    explicit TTDDs12887(Ds12887& chip) : _chip(chip) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Ds12887"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Ds12887; }
    uint64_t TTDHashState() const override;
    void TTDRecordingStarted() override;
    void TTDRecordingStopped() override;

private:
    Ds12887& _chip;
};

}  // namespace ttd
