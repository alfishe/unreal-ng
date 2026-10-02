#pragma once

/// @file ttdzifi.h
/// @brief TTD serializer for the TS AVR firmware's ZiFi API block (registers,
/// the data-register selector, the last-byte times of both rings). Its UART
/// and peer travel in the ZiFiLine blob, the RS-232 rings in the SerialPort
/// blob. A blob of another version is not loaded.

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDZiFi : public TTDSerializable
{
public:
    explicit TTDZiFi(EmulatorContext* context) : _context(context) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "ZiFi"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ZiFi; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
};

}  // namespace ttd
