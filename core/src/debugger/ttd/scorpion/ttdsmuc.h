#pragma once

/// @file ttdsmuc.h
/// @brief TTD serializer for the Scorpion SMUC board (PeripheralId::Smuc): its
/// two latches, the IDE window register file and the serial EEPROM link.
///
/// - #FFBA (pFFBA): serial-link lines, and bit 7 routes #DFBA to the clock's
///   data or address register; the IDE core reads it on every access.
/// - #7FBA (p7FBA): the virtual FDD latch.
/// - The 8 IDE window registers the board answers when no disk core is fitted.
/// - The EEPROM's serial link between two #FFBA writes: a seek inside a
///   transfer resumes the same bit.
/// The clock is the Ds12887 blob. The 2 KB EEPROM contents are not in this
/// blob: they become the engine region SmucEeprom (TTD v2, registry gap 10).

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;
class PortDecoder_Scorpion256;

namespace ttd
{

class TTDSmuc : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    TTDSmuc(PortDecoder_Scorpion256& decoder, EmulatorContext& context) : _decoder(decoder), _context(context) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Smuc"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Smuc; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Scorpion256& _decoder;
    EmulatorContext& _context;
};

}  // namespace ttd
