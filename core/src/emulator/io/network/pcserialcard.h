#pragma once

/// @file pcserialcard.h
/// @brief A PC-style 16550 card on an expansion bus (network tdd §8.1, §10): each UART is a ComPort (the same 16550
/// model, peers and TTD state as the #xxEF port and the ATM2IOESP), reached through IIoBusDevice instead of Z80 ports.
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
/// Preset MODEM - a period ISA internal Hayes modem (network research §5.4: what the Sprinter's ISA slots were put
/// there for; BC-Term 1.11 drives one): a 16550A-compatible UART at 1.8432 MHz at a COM base (#3F8 / #2F8 / #3E8 /
/// #2E8, jumpers), the PC card convention for decode (A9-A3 with AEN: mirrors every #400) and interrupt (MCR OUT2
/// enables the tri-state driver of the jumpered IRQ pin), the modem's lines (CTS, DSR, DCD, RI) on the UART. The
/// modem itself is the shared HayesModemPeer (peer MODEM, the default).
///
/// Preset DUAL16552 - SprinterSerial rev 1.1.1 (Roman Boykov, 2022-02-09, https://github.com/romychs/SprinterSerial),
/// read from its schematic and PCB netlist:
///   - PC16552D (two PC16550D in one package, 16-byte FIFOs, no auto flow control), XIN from a 1.8432 MHz oscillator
///   - decode: 74ALS30 compares A9, A7-A3 and two 74ALS27 NORs of A10-A12 / A13-A15 (D3; without it J1 + J2 close
///     those inputs: A15-A10 not decoded, mirrors every #400); A8 drives CHSEL: 1 = channel A (COM1, #3F8), 0 =
///     channel B (COM2, #2F8). AEN and A19-A16 are not connected
///   - channel A to a CH340 USB bridge: TXD / RXD crossed, but the modem lines run straight (the UART's -RTS / -DTR
///     to the CH340's RTS# / DTR# outputs, -CTS / -DSR / -DCD / -RI to its CTS# / DSR# / DCD# / RI# inputs): nothing
///     drives the UART's modem inputs - they read inactive (open question: a floating CMOS input has no defined level)
///   - channel B to a MAX232 and a DB-9 male: TXD, RXD, RTS, CTS; DSR / DCD / RI not connected (inactive)
///   - INTA -> J5 -> IRQ2 or IRQ3, INTB -> J6 -> IRQ2 or IRQ4, push-pull, not gated (the MF / OUT2 pins are not
///     connected); the AFR (DLAB set, register 2) is the chip's: concurrent write to both channels
///   - ISA RESET DRV to the chip's MR
///   On the Sprinter every IRQ pin of a slot is one line: with both jumpers fitted the two INTR outputs fight on it
///   (reported as `irq_contention`; modeled as the higher one winning).
///
/// Worked example (the Sprinter ESP Network Kit's ESP_RESET): MCR <- #04 at CPU #C3EC (window 3 page #D4):
/// -OUT1 goes low, the ESP stops; 1 ms later MCR <- #02 (RTS): RST released with GPIO0 high, the ESP boots and
/// prints "ready" ~0.4 s later at its factory 115 200 baud.

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "emulator/io/iiobusdevice.h"
#include "emulator/io/serial/comport.h"

class EmulatorContext;
class EspModule;
class HayesModemPeer;
class ISerialPeer;

class PcSerialCard final : public IIoBusDevice
{
public:
    /// The board (network tdd §8.1, §10)
    enum class Preset : uint8_t
    {
        SprinterEsp = 0,
        Modem = 1,
        Dual16552 = 2,
    };

    struct Settings
    {
        Preset preset = Preset::SprinterEsp;
        uint16_t base = 0x3F8;      ///< MODEM: the COM base
        uint8_t irq = 4;            ///< MODEM: the jumpered IRQ; DUAL16552: J5 (COM1: 3 / 2, 0 = open)
        uint8_t irqB = 0;           ///< DUAL16552: J6 (COM2: 4 / 2, 0 = open)
        bool partialDecode = false; ///< DUAL16552: D3 not fitted, J1 + J2 closed
    };

    /// @param slotKey the slot's id ("isa1"): the UARTs' port keys are "<slot>.uart0" / "<slot>.uart1"
    /// @param peerB the second UART's peer (DUAL16552 only)
    PcSerialCard(EmulatorContext* context, const Settings& settings, std::unique_ptr<ISerialPeer> peer,
                 std::unique_ptr<ISerialPeer> peerB, std::string slotKey);
    /// The SprinterESP (one UART)
    PcSerialCard(EmulatorContext* context, Preset preset, std::unique_ptr<ISerialPeer> peer, std::string slotKey);
    ~PcSerialCard() override;

    PcSerialCard(const PcSerialCard&) = delete;
    PcSerialCard& operator=(const PcSerialCard&) = delete;

    Preset GetPreset() const { return _settings.preset; }
    const Settings& GetSettings() const { return _settings; }
    const std::string& SlotKey() const { return _slotKey; }
    /// The number of UARTs (2 on SprinterSerial)
    int Channels() const { return _com[1] ? 2 : 1; }
    std::string PortKey(int channel = 0) const { return _slotKey + ".uart" + std::to_string(channel); }
    ComPort& Com(int channel = 0) { return *_com[((channel & 1) != 0 && _com[1]) ? 1 : 0]; }
    const ComPort& Com(int channel = 0) const { return *_com[((channel & 1) != 0 && _com[1]) ? 1 : 0]; }
    /// The ESP module on the (first) line (AT / ESPNET peer), or null
    EspModule* Esp() const;
    /// The Hayes modem on a line, or null
    HayesModemPeer* Modem(int channel = 0) const;
    /// A 16550's registers from before a refit (the same chip, a new peer on its line): the ESP's pins follow MCR
    void RestoreUart(int channel, const Uart16550::State& state);
    void RestoreUart(const Uart16550::State& state) { RestoreUart(0, state); }

    /// The ISA I/O base of a channel
    uint16_t Base(int channel = 0) const;
    /// The IRQ a channel's jumper selects (-1: open)
    int ChannelIrq(int channel) const;

    // IIoBusDevice
    const char* Kind() const override;
    bool Decodes(uint32_t address, uint16_t& offset) const override;
    bool IgnoresAen() const override { return _settings.preset != Preset::Modem; }
    std::string DecodeNote() const override;
    /// offset bits 2-0: the register; bit 3: channel B
    uint8_t Read(uint16_t offset) override;
    void Write(uint16_t offset, uint8_t value) override;
    uint8_t Peek(uint16_t offset) const override;
    void Reset() override;
    bool IoRange(uint32_t& first, uint32_t& last) const override;
    int IrqLine() const override;
    const char* RegisterName(uint16_t offset, bool write) const override;
    bool Irq() const override;
    bool IrqDriven() const override;
    void SetIrqListener(std::function<void()> changed) override;
    uint64_t NextIrqEventAt() const override;
    void CatchUp() override;
    std::string IrqCause() const override;
    void OnFrame() override;
    void Describe(StateNode& out) const override;

    /// Both INTR outputs are jumpered and drive opposite levels now (the Sprinter joins every IRQ pin of a slot)
    bool IrqContention() const;

private:
    /// OUT1 / OUT2 to the ESP's RST / GPIO0 (the SPRINTERESP's wiring)
    void OnAuxLines(bool out1, bool out2);
    void DescribeChannel(int channel, StateNode& out) const;
    /// INTR of a channel and whether its pin reaches the slot's IRQ line
    bool ChannelIntr(int channel) const;
    bool ChannelDriven(int channel) const;

    Settings _settings;
    std::string _slotKey;
    std::array<std::unique_ptr<ComPort>, 2> _com;
};
