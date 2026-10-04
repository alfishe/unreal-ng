#pragma once

/// @file ttdevoavrvolatile.h
/// @brief TTD serializer for the ZX-Evo AVR's volatile registers on machines
/// whose paging blob does not carry them (PeripheralId::EvoAvrVolatile; TS-Conf).
///
/// The extension register type the Z80 selected (#EFF7 extension reads), the
/// EEPROM window (page, mode), the Caps Lock LED and the tape-out mode. On the
/// ATM3 they ride in AtmPaging (id 8). The clock and its cells are the Ds12887
/// blob; the 4 KB EEPROM contents become the engine region EvoAvrEeprom.

#include "debugger/ttd/ttdserializable.h"

class EvoAvr;

namespace ttd
{

class TTDEvoAvrVolatile : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit TTDEvoAvrVolatile(EvoAvr& avr) : _avr(avr) {}

    size_t TTDStateSize() const override { return 4; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoAvrVolatile"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoAvrVolatile; }
    uint64_t TTDHashState() const override;

private:
    EvoAvr& _avr;
};

}  // namespace ttd
