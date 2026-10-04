#pragma once

/// @file ne2000board.h
/// @brief An NE2000-class Ethernet card: the DP8390 core with the board around it (network tdd §6; RTL8019AS
/// datasheet 2005-08-26; Linux ne.c for the PROM layout and the reset port). Machine-independent: it is an
/// IIoBusDevice, the bus a machine has (the Sprinter's ISA slot wrapper) reaches it at its I/O base.
///
/// I/O layout (32 bytes from the base, ISA A9-A0 decoded, so every 1 KB ISA page mirrors it):
///   +#00-#0F  the DP8390 registers (page by CR bits 7-6; page 3 is the variant's)
///   +#10-#17  the data port (remote DMA, one byte per access: the 8-bit slot)
///   +#18-#1F  the reset port: a read resets the chip (ISR.RST); a write does nothing
///
/// Variants (network tdd §6.4):
///   RTL8019AS (default): page 0 #0A / #0B read 'P' 'p'; page 3 = 9346CR (the 93C46 EEPROM bit-banged), BPAGE,
///             CONFIG0-4, INTR, CSNSAV; 16 KB of RAM at #4000-#7FFF, only #4000-#5FFF in byte mode (DCR.WTS = 0,
///             the kit's PSTOP #60); the EEPROM is generated from the slot settings (base, IRQ, MAC)
///   UM9003:   ID #20 #01; page 3 mirrors page 1; reading the reset port stalls the ISA cycle - the machine hangs
///             as on the real card (network open question Q5 A), the report says so
///   NE1000:   no ID (#FF), no page 3; 8 KB of RAM at #2000-#3FFF; the PROM not doubled
/// The PROM (remote DMA #0000-#001F): the MAC, each byte twice, then #57 #57 at 28-31 ('W' 'W', the NE2000
/// signature Linux ne.c checks); the RTL kit's NETCFG reads either layout.
///
/// Worked example (NICINFO finds the card): page 0, read #0A / #0B -> #50 #70; remote read of 32 bytes from #0000
/// -> 02 02 53 53 50 50 00 00 00 02 02 ... (MAC 02:53:50:00:00:02 doubled): the kit takes PROM[0, 2, .., 10].

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/iiobusdevice.h"
#include "emulator/io/network/ethernet/dp8390.h"
#include "emulator/io/network/ethernet/ethernetcard.h"
#include "emulator/io/network/ethernet/eeprom93c46.h"
#include "emulator/io/network/ethernet/ethernetlink.h"

class Ne2000Board final : public IEthernetCard, private Dp8390::IBoard
{
public:
    enum class Variant : uint8_t
    {
        Rtl8019as = 0,
        Um9003 = 1,
        Ne1000 = 2,
    };
    static const char* VariantName(Variant v);

    struct Settings
    {
        Variant variant = Variant::Rtl8019as;
        uint16_t base = 0x300;        ///< #200..#3E0 in steps of #20
        uint8_t irq = 3;              ///< written into CONFIG1 / the EEPROM (informational: one line per slot)
        std::array<uint8_t, 6> mac{};
        std::string key = "eth";      ///< the port key ("isa2.eth")
    };

    /// `clock`: the machine's time in base T-states (3.5 MHz units)
    Ne2000Board(const Settings& settings, std::function<uint64_t()> clock);

    const Settings& GetSettings() const { return _settings; }
    Dp8390& Chip() { return _chip; }
    const Dp8390& Chip() const { return _chip; }

    /// The wire the card is plugged into (the virtual network's Ethernet gateway); null = no cable: transmitted
    /// frames are lost (counted)
    void SetLink(IEthernetLink* link) override { _link = link; }
    IEthernetLink* Link() const override { return _link; }

    // IIoBusDevice
    const char* Kind() const override { return "ne2000"; }
    bool Decodes(uint32_t address, uint16_t& offset) const override;
    uint8_t Read(uint16_t offset) override;
    void Write(uint16_t offset, uint8_t value) override;
    uint8_t Peek(uint16_t offset) const override;
    void Reset() override;
    bool Irq() const override;
    void SetIrqListener(std::function<void()> changed) override;
    /// The selected IRQ pin is driven while the chip may drive it: on the RTL8019AS only with CONFIG1.IRQEN set
    /// (clear: high impedance), and only a pin an 8-bit slot has (IRQ 2/9, 3, 4, 5, 7: IRQ 10-15 sit on the
    /// 16-bit connector)
    bool IrqDriven() const override;
    /// A transmit on the wire ends with PTX (ISR bit 1)
    uint64_t NextIrqEventAt() const override;
    void CatchUp() override { _chip.Advance(Now()); }
    std::string IrqCause() const override;
    /// The IRQ number the card selects now (RTL8019AS: CONFIG1.IRQS; the others: the jumper, Settings::irq)
    int SelectedIrq() const;
    bool Stalled() const override { return _stalled; }
    void OnFrame() override;
    void Describe(StateNode& out) const override;
    bool IoRange(uint32_t& first, uint32_t& last) const override
    {
        first = _settings.base;
        last = _settings.base + 0x1Fu;
        return true;
    }
    int IrqLine() const override { return _settings.irq; }
    const char* RegisterName(uint16_t offset, bool write) const override;

    // IEthernetPort
    const std::string& PortKey() const override { return _settings.key; }
    void StationMac(uint8_t out[6]) const override;
    bool Offer(const uint8_t* frame, size_t length) override;

    /// The 93C46 content the board was built with (RTL8019AS layout, datasheet §6.3)
    static std::array<uint16_t, Eeprom93c46::kWords> BuildEeprom(const Settings& settings);

    /// Observation (not state)
    struct Counters
    {
        uint64_t txNoLink = 0;        ///< frames sent with no cable
        uint64_t resetPortReads = 0;
    };
    const Counters& GetCounters() const { return _counters; }

    // --- TTD (EthernetNics blob) -----------------------------------------------
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kRamSize = 0x4000;
    size_t StateSize() const;
    void SaveState(uint8_t* dst) const;
    /// False when the blob is of another version / variant (nothing loaded)
    bool LoadState(const uint8_t* src, size_t size);
    // IEthernetCard: the same bytes
    size_t CardStateBound() const override { return StateSize(); }
    void SaveCardState(std::vector<uint8_t>& out) const override
    {
        out.resize(StateSize());
        SaveState(out.data());
    }
    bool LoadCardState(const uint8_t* src, size_t size) override { return LoadState(src, size); }

private:
    // Dp8390::IBoard
    uint8_t BufferRead(uint16_t address) const override;
    void BufferWrite(uint16_t address, uint8_t value) override;
    void Transmit(const uint8_t* frame, size_t length) override;

    uint8_t ReadPage3(uint8_t offset) const;
    void WritePage3(uint8_t offset, uint8_t value);
    uint8_t ReadChip(uint8_t offset, bool peek);
    uint8_t PeekChip(uint8_t offset) const;
    void LoadConfigFromEeprom();
    bool RamAddress(uint16_t address, size_t& index) const;
    uint64_t Now() const { return _clock ? _clock() : 0; }

    Settings _settings;
    std::function<uint64_t()> _clock;
    Dp8390 _chip;
    Eeprom93c46 _eeprom;
    std::vector<uint8_t> _ram;
    std::array<uint8_t, 32> _prom{};
    IEthernetLink* _link = nullptr;

    // RTL8019AS page 3
    uint8_t _cr9346 = 0;    ///< EEM1-0 and the pins written
    uint8_t _bpage = 0;
    uint8_t _config1 = 0, _config2 = 0, _config3 = 0, _config4 = 0;
    bool _stalled = false;
    Counters _counters;
};
