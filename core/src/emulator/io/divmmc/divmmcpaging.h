#pragma once

/// @file divmmcpaging.h
/// @brief The DivMMC interface (and the DivIDE's paging, which is the same): an 8K EEPROM, banked 8K RAM, the
/// control port #E3, the SPI pair #E7 / #EB with SD cards, and the automap that swaps all of it into #0000-#3FFF
/// when the Spectrum ROM reaches a well-known address.
///
/// On a classic machine the device sits on the bus the way the board does: a host bus overlay over #0000-#3FFF
/// (the machine's memory stays 16K windows), an M1 observer for the automap, and the three ports as a low-byte
/// full-decode card. The slot card `divmmc` (slots/cards/divmmc/divmmccard.h) is this device in the edge slot.
/// docs/inprogress/2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md §2.
/// The Next's own DivMMC uses its 8K slot table instead (esxdos-and-sd.md section 3).
///
/// The board as built (Mario Prato's DivMMC CPLD, divmmc.vhd):
///   - the automap: on every opcode fetch (the falling /MREQ of an M1 cycle) the CPLD takes
///       mapcond <= entry or #3Dxx or (mapcond and not #1FF8-#1FFF)
///       automap <= mapcond (the value before this fetch) or #3Dxx
///     with the entries #0000 #0008 #0038 #0066 #04C6 #0562. An entry maps the board in from the next opcode
///     fetch on (the whole instruction at the entry, operands included, comes from the Spectrum ROM); #3Dxx maps
///     it in for this fetch already; a fetch in #1FF8-#1FFF still comes from the board and maps it out from the
///     next fetch on. Nothing else (no ROM selection input: the board traps in any ROM the machine has paged);
///   - /ROMCS = (automap and not EEPROM jumper) or (automap and MAPRAM) or CONMEM: with the EEPROM jumper set
///     (the "automap off" position, EEPROM writable) the latch still runs but maps nothing unless MAPRAM is set;
///   - #E3 (bank, MAPRAM, CONMEM) is cleared by the power-on circuit only; the CPU's /RESET clears the card
///     selects of #E7 and the SPI engine. MAPRAM, once set, stays until power off. mapcond / automap have no reset:
///     the next opcode fetches (the reset vector) decide them;
///   - reads: CONMEM puts the EEPROM at #0000-#1FFF; MAPRAM without CONMEM puts bank 3 there (read only);
///     #2000-#3FFF is the selected bank, written except bank 3 under MAPRAM without CONMEM.
/// Not modelled: EEPROM writes (CONMEM with the jumper set: firmware flashing), the NMI button, the 512K boards.
///
/// The automap option `rom3` is not a classic board's behaviour: it is the Next's rule (zxnext.vhd
/// sram_divmmc_automap_rom3_en, every entry taken only while the 48K BASIC ROM is paged at #0000), offered for a
/// +2A / +3 whose firmware wants it. The default `on` is the CPLD.
///
/// Cost on the machine: the overlay is installed only while the board is mapped in; the M1 observer runs only for
/// fetches below #4000 (machineM1HookEnd), and for every fetch while an automap change waits for the next one.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "emulator/cpu/z80.h"
#include "emulator/bus/bustap.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/ports/portdecoder.h"

class Core;
class EmulatorContext;
namespace ttd
{
class TTDRegionTracker;
}

class DivMmcPaging final : public PortDevice, public HostBusOverlay, public IMachineM1Hook
{
public:
    static constexpr uint16_t kPortControl = 0xE3;
    static constexpr uint16_t kPortSpiSelect = 0xE7;
    static constexpr uint16_t kPortSpiData = 0xEB;
    static constexpr uint32_t kRomSize = 0x2000;
    static constexpr uint32_t kBankSize = 0x2000;
    static constexpr unsigned kBanks = 16;  ///< 128K, the common DivMMC
    static constexpr uint32_t kRamSize = kBanks * kBankSize;
    static constexpr uint32_t kHookEnd = 0x4000;  ///< every automap address is below it

    /// #E3 bits
    static constexpr uint8_t kConmem = 0x80;
    static constexpr uint8_t kMapram = 0x40;
    static constexpr uint8_t kBankMask = 0x0F;

    enum class AutomapMode : uint8_t
    {
        On,    ///< the CPLD: every entry in any ROM
        Off,   ///< the EEPROM jumper set: the latch runs, nothing maps unless MAPRAM is set
        Rom3,  ///< emulator option (the Next's rule): the entries count only while the 48K BASIC ROM is paged
    };

    /// The state blob (SaveState / LoadState), layout v1, little endian: 0 version, 1 #E3 (CONMEM, bank), 2 MAPRAM,
    /// 3 mapcond, 4 automap, 5 automap mode, 6 selected card (#FF none), 7 SPI receive latch, 8-15 reserved, then the
    /// two cards' protocol states (SdCardSpi::STATE_SIZE each). The RAM is not in it (RamData, kRamSize)
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kStateCardOffset = 16;
    static constexpr size_t kStateSize = kStateCardOffset + 2 * SdCardSpi::STATE_SIZE;

    explicit DivMmcPaging(EmulatorContext* context);
    ~DivMmcPaging() override;
    DivMmcPaging(const DivMmcPaging&) = delete;
    DivMmcPaging& operator=(const DivMmcPaging&) = delete;

    /// Put the board on the bus: the three ports (unless the owner routes them, `ownPorts` false: a slot card's
    /// claims), the overlay over #0000-#3FFF while mapped, the M1 observer. False (and nothing installed) when a
    /// port, the overlay chain or the machine's M1 hook is taken
    bool Attach(bool ownPorts = true);
    void Detach();
    bool IsAttached() const { return _attached; }

    /// The firmware image (8K, esxDOS / UnoDOS .rom)
    bool LoadRom(const uint8_t* data, size_t size);
    bool LoadRomFile(const std::string& path);
    bool HasRom() const { return _hasRom; }
    const uint8_t* Rom() const { return _rom.get(); }

    /// The CPU's /RESET: the card selects off, the SPI latch idle. #E3, MAPRAM and the automap latch stay (the
    /// CPLD has no reset on them); the reset vector's fetch at #0000 is an entry
    void Reset();
    /// Power on: everything of Reset, #E3 and MAPRAM cleared, the automap latch off
    void PowerOn();
    /// A snapshot's program starts: the board is not mapped (CONMEM and the automap latch off; MAPRAM stays)
    void UnmapForProgram();

    /// region <State>
    bool Conmem() const { return (_control & kConmem) != 0; }
    bool Mapram() const { return _mapram; }
    uint8_t Bank() const { return _control & kBankMask; }
    /// The automap latch: what the bus sees now
    bool Automapped() const { return _automap; }
    /// mapcond: what the latch takes at the next opcode fetch (an entry or the off-area seen, not yet applied)
    bool MapCondition() const { return _mapcond; }
    /// The board drives #0000-#3FFF now
    bool Mapped() const { return Conmem() || (_automap && (_mode != AutomapMode::Off || _mapram)); }
    uint8_t Control() const { return _control; }
    uint8_t* RamBank(unsigned bank) { return _ram.get() + (bank % kBanks) * kBankSize; }
    uint8_t* RamData() { return _ram.get(); }
    const uint8_t* RamData() const { return _ram.get(); }
    SdCardSpi& Card(unsigned index) { return _card[index & 1]; }
    const SdCardSpi& Card(unsigned index) const { return _card[index & 1]; }
    /// The selected SPI card (0, 1), -1 for none
    int SelectedCard() const { return _selected; }
    /// What a read of #EB returns now (the byte of the last exchange)
    uint8_t SpiReceive() const { return _spiRx; }
    BusTap& SpiTap() { return _spiTap; }
    AutomapMode GetAutomapMode() const { return _mode; }
    void SetAutomapMode(AutomapMode mode);
    /// On / Off (the EEPROM jumper)
    void SetAutomapEnabled(bool on) { SetAutomapMode(on ? AutomapMode::On : AutomapMode::Off); }
    /// The time-travel engine's write tracker of the RAM while it records (nullptr: not recording)
    void SetRamTracker(ttd::TTDRegionTracker* tracker) { _ramTracker = tracker; }

    /// The blob (kStateSize bytes)
    void SaveState(uint8_t* dst) const;
    /// False (nothing changed) for a blob of another version
    bool LoadState(const uint8_t* src);
    /// endregion

    /// PortDevice: #E3 write-only, #E7 write-only (reads #FF), #EB read / write
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    /// HostBusOverlay
    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    /// IMachineM1Hook: the automap
    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override;
    bool RepeatM1IsInert(uint16_t address) const override;

private:
    struct Latch
    {
        bool mapcond = false;
        bool automap = false;
    };
    /// The CPLD's latch after an opcode fetch at `address`
    Latch NextLatch(uint16_t address) const;
    /// The overlay while mapped, the M1 range while a latch change waits
    void UpdateBus();
    bool BasicRomPaged() const;
    uint8_t ReadMapped(uint16_t addr) const;
    void SpiSelect(uint8_t value);
    uint8_t TapSlot() const { return _selected >= 0 ? static_cast<uint8_t>(_selected) : BusTap::kNoSlot; }

    EmulatorContext* _context;
    Core* _core = nullptr;  ///< set by Attach
    bool _attached = false;
    bool _ownPorts = true;
    bool _overlayInstalled = false;
    bool _hookInstalled = false;
    uint32_t _savedHookEnd = 0x10000;
    bool _hasRom = false;
    AutomapMode _mode = AutomapMode::On;
    bool _mapcond = false;
    bool _automap = false;
    bool _mapram = false;  ///< sticky: a write can only set it
    uint8_t _control = 0;
    uint8_t _spiRx = 0xFF;
    int _selected = -1;
    std::unique_ptr<uint8_t[]> _rom;
    std::unique_ptr<uint8_t[]> _ram;
    ttd::TTDRegionTracker* _ramTracker = nullptr;
    // The bus analyzer's view of the SPI pair (docs/inprogress/2026-10-11-bus-analyzer); registered while attached
    BusTap _spiTap{"divmmc.spi", BusProtocol::Spi, "DivMMC SPI (#E7 chip selects, #EB data): SD cards 0 and 1"};
    SdCardSpi _card[2];
};
