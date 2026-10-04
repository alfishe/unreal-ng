#pragma once
/// @file ttdusart8251.h
/// @brief TTD serializer for an 8251 USART (Usart8251::State: mode, command, buffers, the characters on the line, the board latch kept with it).

#include "debugger/ttd/ttdserializable.h"

class Usart8251;

namespace ttd
{

class TTDUsart8251 : public TTDSerializable
{
public:
    explicit TTDUsart8251(Usart8251& chip) : _chip(chip) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Usart8251"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Usart8251; }

private:
    Usart8251& _chip;
};

}  // namespace ttd
