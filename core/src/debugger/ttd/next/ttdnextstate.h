#pragma once

/// @file ttdnextstate.h
/// @brief TTD serializer for the ZX Spectrum Next (PeripheralId::NextState): one image of NextStateCodec, so a seek puts the NextREG file,
/// the memory map, the palettes, sprites, copper, DMA, CTC, interrupt controller, I2C RTC, Multiface and DivMMC back to the checkpoint.
/// Without it a restore left all of those at their values from the moment of the seek (the guest RAM came back, its machine did not).

#include "debugger/ttd/ttdserializable.h"

class PortDecoder_Next;

namespace ttd {

class TTDNextState : public TTDSerializable
{
public:
    explicit TTDNextState(PortDecoder_Next& decoder);

    size_t TTDStateSize() const override { return _size; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "NextState"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::NextState; }
    /// FNV-1a over the image
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Next& _decoder;
    size_t _size;  ///< the image has a fixed length for the life of the machine
};

}  // namespace ttd
