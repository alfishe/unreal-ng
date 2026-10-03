#pragma once

/// @file pcserialcard.h
/// @brief A PC-style 16550 card on an expansion bus (network tdd §8.1): the UART is a ComPort (the same 16550 model,
/// peers and TTD state as the #xxEF port and the ATM2IOESP), reached through IIoBusDevice instead of Z80 ports.
/// Machine-independent; the Sprinter's ISA slot wrapper (IsaBusDeviceCard) plugs it into a slot.
///
/// Preset SPRINTERESP - the SprinterESP Wi-Fi card (Roman Boykov, rev 1.0.5, schematic 2023-01-27,
/// https://github.com/romychs/SprinterESP), read from the schematic:
///   - TL16C550C, XIN from a 14.7456 MHz oscillator (divisor 8 = 115 200 baud)
///   - decode: 74HC30 + 74HC27 compare ISA A13-A3 with #3E8 >> 3 ("COM3"): A19-A14 and AEN are not decoded, so the
///     card answers #3E8-#3EF of every 16 KB ISA page and also while #9FBD holds AEN = 1; no mirror inside a page
///   - INTR straight to ISA IRQ3 (not gated by OUT2); -OUT1 through a diode to the ESP-12F's RST (MCR bit 2 set =
///     the ESP held in reset), -OUT2 through a diode to GPIO0 (MCR bit 3 set = GPIO0 low: download mode at the next
///     reset release); -RTS / -CTS to the ESP's GPIO13 / GPIO15 (UART0 CTS / RTS) through a TXB0104; -DSR, -DCD,
///     -RI not connected (modeled as DSR / DCD asserted, RI inactive, like the ATM2IOESP); -DTR not connected
///   - ISA RESET DRV to the 16550's MR only: a bus reset clears MCR, which releases the ESP's RST - the ESP reboots
///
/// Worked example (the Sprinter ESP Network Kit's ESP_RESET): MCR <- #04 at CPU #C3EC (window 3 page #D4):
/// -OUT1 goes low, the ESP stops; 1 ms later MCR <- #02 (RTS): RST released with GPIO0 high, the ESP boots and
/// prints "ready" ~0.4 s later at its factory 115 200 baud.

#include <cstdint>
#include <memory>
#include <string>

#include "emulator/io/iiobusdevice.h"
#include "emulator/io/serial/comport.h"

class EmulatorContext;
class EspModule;
class ISerialPeer;

class PcSerialCard final : public IIoBusDevice
{
public:
    /// The board (later phases add the ISA modem and SprinterSerial's two PC16552 channels: network tdd §10)
    enum class Preset : uint8_t
    {
        SprinterEsp = 0,
    };

    /// @param slotKey the slot's id ("isa1"): the UART's port key is "<slot>.uart0"
    PcSerialCard(EmulatorContext* context, Preset preset, std::unique_ptr<ISerialPeer> peer, std::string slotKey);
    ~PcSerialCard() override;

    PcSerialCard(const PcSerialCard&) = delete;
    PcSerialCard& operator=(const PcSerialCard&) = delete;

    Preset GetPreset() const { return _preset; }
    const std::string& SlotKey() const { return _slotKey; }
    std::string PortKey() const { return _slotKey + ".uart0"; }
    ComPort& Com() { return _com; }
    const ComPort& Com() const { return _com; }
    /// The ESP module on the line (AT / ESPNET peer), or null
    EspModule* Esp() const;
    /// The 16550's registers from before a refit (the same chip, a new peer on its line): the ESP's pins follow MCR
    void RestoreUart(const Uart16550::State& state);

    // IIoBusDevice
    const char* Kind() const override { return "sprinteresp"; }
    bool Decodes(uint32_t address, uint16_t& offset) const override;
    bool IgnoresAen() const override { return true; }
    std::string DecodeNote() const override;
    uint8_t Read(uint16_t offset) override;
    void Write(uint16_t offset, uint8_t value) override;
    uint8_t Peek(uint16_t offset) const override;
    void Reset() override;
    bool IoRange(uint32_t& first, uint32_t& last) const override;
    int IrqLine() const override;
    const char* RegisterName(uint16_t offset, bool write) const override;
    bool Irq() const override;
    void OnFrame() override { _com.OnFrame(); }
    void Describe(StateNode& out) const override;

private:
    /// OUT1 / OUT2 to the ESP's RST / GPIO0 (the preset's wiring)
    void OnAuxLines(bool out1, bool out2);

    Preset _preset = Preset::SprinterEsp;
    std::string _slotKey;
    ComPort _com;
};
