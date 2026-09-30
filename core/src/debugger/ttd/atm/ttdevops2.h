#pragma once

/// @file ttdevops2.h
/// @brief TTD serializer for the ZX-Evo AVR's PS/2 keyboard state
/// (PeripheralId::EvoPs2): the 16-byte scan code log the Z80 pops, its
/// pointers, the parser flags, the modifier mask (register D) and the keys
/// the host holds down. The rest of the AVR is in the AtmPaging blob (extension
/// type, EEPROM page, flags) and the Ds12887 blob (clock and NVRAM cells).

#include "debugger/ttd/ttdserializable.h"

class EvoAvr;

namespace ttd
{

class TTDEvoPs2 : public TTDSerializable
{
public:
    explicit TTDEvoPs2(EvoAvr& avr) : _avr(avr) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoPs2"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoPs2; }
    uint64_t TTDHashState() const override;

private:
    EvoAvr& _avr;
};

}  // namespace ttd
