#pragma once

/// @file divmmcpaging.h
/// @brief The DivMMC interface (and the DivIDE's paging, which is the same): an 8K EEPROM, banked 8K RAM, the
/// control port #E3, the SPI pair #E7 / #EB with SD cards, and the automap that swaps all of it into #0000-#3FFF
/// when the Spectrum ROM reaches a well-known address.
///
/// On a classic machine the device sits on the bus the way the board does: a host bus overlay over
/// #0000-#3FFF (the machine's memory stays 16K windows), an M1 observer for the automap, and the three ports as a
/// low-byte full-decode card. docs/inprogress/2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md §2.
/// The Next's own DivMMC uses its 8K slot table instead (esxdos-and-sd.md section 3); the rules are the same.

#include <cstdint>
#include <memory>
#include <string>

#include "emulator/cpu/z80.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/ports/portdecoder.h"

class EmulatorContext;

class DivMmcPaging final : public PortDevice, public HostBusOverlay, public IMachineM1Hook
{
public:
    static constexpr uint16_t kPortControl = 0xE3;
    static constexpr uint16_t kPortSpiSelect = 0xE7;
    static constexpr uint16_t kPortSpiData = 0xEB;
    static constexpr uint32_t kRomSize = 0x2000;
    static constexpr uint32_t kBankSize = 0x2000;
    static constexpr unsigned kBanks = 16;  ///< 128K, the common DivMMC

    /// #E3 bits
    static constexpr uint8_t kConmem = 0x80;
    static constexpr uint8_t kMapram = 0x40;
    static constexpr uint8_t kBankMask = 0x0F;

    explicit DivMmcPaging(EmulatorContext* context);
    ~DivMmcPaging() override;
    DivMmcPaging(const DivMmcPaging&) = delete;
    DivMmcPaging& operator=(const DivMmcPaging&) = delete;

    /// Put the board on the bus: the three ports, the overlay over #0000-#3FFF, the M1 observer. False (and nothing
    /// installed) when a port, the overlay chain or the machine's M1 hook is taken
    bool Attach();
    void Detach();
    bool IsAttached() const { return _attached; }

    /// The firmware image (8K, esxDOS / UnoDOS .rom)
    bool LoadRom(const uint8_t* data, size_t size);
    bool LoadRomFile(const std::string& path);
    bool HasRom() const { return _hasRom; }

    /// The board's reset: unmapped, bank 0, MAPRAM cleared (a DivMMC resets its map; a DivIDE has no reset input)
    void Reset();

    /// region <State>
    bool Conmem() const { return (_control & kConmem) != 0; }
    bool Mapram() const { return _mapram; }
    uint8_t Bank() const { return _control & kBankMask; }
    bool Automapped() const { return _automap; }
    bool Mapped() const { return _automap || Conmem(); }
    uint8_t Control() const { return _control; }
    uint8_t* RamBank(unsigned bank) { return _ram.get() + (bank % kBanks) * kBankSize; }
    SdCardSpi& Card(unsigned index) { return _card[index & 1]; }
    /// Turn the trap off (the config's "automap off": the firmware is reached through #E3 only)
    void SetAutomapEnabled(bool on) { _automapEnabled = on; if (!on) _automap = false; }
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

private:
    uint8_t ReadMapped(uint16_t addr) const;
    void SpiSelect(uint8_t value);

    EmulatorContext* _context;
    bool _attached = false;
    bool _hasRom = false;
    bool _automapEnabled = true;
    bool _automap = false;
    bool _mapram = false;  ///< sticky: a write can only set it
    uint8_t _control = 0;
    uint8_t _spiRx = 0xFF;
    int _selected = -1;
    std::unique_ptr<uint8_t[]> _rom;
    std::unique_ptr<uint8_t[]> _ram;
    SdCardSpi _card[2];
};
