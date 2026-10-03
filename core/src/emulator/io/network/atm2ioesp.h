#pragma once

/// @file atm2ioesp.h
/// @brief ATM2IOESP (Kulich, 2025): a TL16C550C and an ESP32 on the ATM Turbo
/// 2+ INTERNAL I/O connector (docs/inprogress/2026-10-02-atm2ioesp/reference-atm2ioesp.md).
///
/// The Z80 writes the card's bus address to #FB (`base + register`, base #F0
/// on Rev 1.5 / 2.0, #F8 on Rev 1.0: the GAL compares CT7..CT3 with its
/// jumpers) and then reads or writes the register through #FA. The UART runs
/// from a 1.8432 MHz crystal (divisor 1 = 115200); its INTRPT is not wired
/// (the connector has no interrupt line), there are no waits. The ESP module
/// ships with Espressif's AT firmware at 115200 with RTS / CTS; NedoOS's
/// drivers keep AFE off and pulse RTS by software (MCR 2, then 0).
///
/// The UART and its peer are a ComPort (the same 16550 model, peers, TTD
/// state as the #xxEF port), reached through the bus instead of ports.

#include <cstdint>
#include <memory>

#include "emulator/io/serial/comport.h"
#include "emulator/io/iiobusdevice.h"

class EmulatorContext;
class ISerialPeer;

class Atm2IoEsp final : public IIoBusDevice
{
public:
    static constexpr uint8_t kDefaultAddress = 0xF0;   ///< Rev 1.5 / 2.0 (Rev 1.0: #F8)

    Atm2IoEsp(EmulatorContext* context, std::unique_ptr<ISerialPeer> peer, uint8_t baseAddress);

    // IIoBusDevice: the #FB latch value is the bus address, CT2..CT0 the 16550 register
    const char* Kind() const override { return "atm2ioesp"; }
    bool Decodes(uint32_t busAddress, uint16_t& offset) const override
    {
        if ((busAddress & 0xF8) != _base)
            return false;
        offset = static_cast<uint16_t>(busAddress & 0x07);
        return true;
    }
    uint8_t Read(uint16_t offset) override;
    void Write(uint16_t offset, uint8_t value) override;
    uint8_t Peek(uint16_t offset) const override;
    void Reset() override { _com.Reset(); }

    uint8_t Address() const { return _base; }
    ComPort& Com() { return _com; }
    const ComPort& Com() const { return _com; }

    /// Frame boundary (machine thread): the UART catches up, the peer flushes
    void OnFrame() override { _com.OnFrame(); }

private:
    uint8_t _base = kDefaultAddress;
    ComPort _com;
};
