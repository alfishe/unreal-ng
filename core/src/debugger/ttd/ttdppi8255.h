#pragma once
/// @file ttdppi8255.h
/// @brief TTD serializer for an 8255 PPI (Ppi8255::State: the mode word and the three output latches).

#include "debugger/ttd/ttdserializable.h"

class Ppi8255;

namespace ttd
{

class TTDPpi8255 : public TTDSerializable
{
public:
    explicit TTDPpi8255(Ppi8255& ppi) : _ppi(ppi) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Ppi8255"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Ppi8255; }

private:
    Ppi8255& _ppi;
};

}  // namespace ttd
