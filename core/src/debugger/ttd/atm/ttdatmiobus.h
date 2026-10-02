#pragma once

/// @file ttdatmiobus.h
/// @brief TTD serializer for the ATM Turbo 2+ INTERNAL I/O connector: the #FB
/// latch that addresses the devices on it (ATM2IOESP). The devices keep their
/// own blobs. Old recordings without this blob leave the latch as it is.

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDAtmIoBus : public TTDSerializable
{
public:
    explicit TTDAtmIoBus(EmulatorContext* context) : _context(context) {}

    size_t TTDStateSize() const override { return 4; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "AtmIoBus"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::AtmIoBus; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
};

}  // namespace ttd
