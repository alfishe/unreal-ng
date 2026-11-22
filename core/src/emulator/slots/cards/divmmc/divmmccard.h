#pragma once

// The DivMMC (Mario Prato's SD interface for the Sinclair edge, the DivIDE's paging with an SPI pair instead of the
// IDE port) as a slot card on the classic machines: 48K, 128K, +2, +2A / +3. The board is DivMmcPaging
// (emulator/io/divmmc/divmmcpaging.h, the CPLD's rules); the card puts it in the edge slot.
//
// As fitted:
//   - ports #E3 (control, write only), #E7 (card selects, write only), #EB (SPI data), low byte decoded: the
//     card's claims (refdata/cards.cpp); a read of #E3 / #E7 leaves the data bus alone;
//   - the firmware (card option `rom`): `unodos` (UnoDOS 3, shipped as rom/divmmc/unodos3.rom, default) or `esxdos`
//     (rom/divmmc/esxmmc.bin, not shipped: the user's ESXMMC.BIN). A missing or not-8K file refuses the card;
//   - the automap (card option `automap`): `on` (the CPLD, any ROM), `off` (the EEPROM jumper: nothing maps unless
//     MAPRAM), `rom3` (emulator option, the Next's rule: the entries count only with the 48K BASIC ROM paged);
//   - the ROM override: /ROMCS on the 48K / 128K / +2 edge; on the +2A / +3 the board's jumper J3 sends it to
//     /ROM1OE and /ROM2OE instead (the schematic's "Spectrum +2A, +3 Jumper"), the same 16K window;
//   - two SD sockets, media slots `sd.divmmc0` (aliases `divmmc`, `divmmc0`) and `sd.divmmc1` (`divmmc1`): FAT16 /
//     FAT32 behind an MBR, as esxDOS and UnoDOS read them;
//   - /RESET (BusReset): the card selects and the SPI engine; #E3, MAPRAM and the automap latch stay (the CPLD).
//     A power cycle (hard reset) clears #E3 and MAPRAM and powers the SD cards off. A snapshot's program starts
//     with the board unmapped.
// The M1 hook is exclusive: a machine whose decoder holds it (ATM3, Sprinter, TS-Conf, Next) cannot build the card;
// the Next has its own DivMMC (NextDivMmc).
//
// Time travel: one blob of type DivMmc (77, "<slot>.divmmc"): the board's latches and both cards' SPI protocol
// state (DivMmcPaging::kStateSize), and its 128K RAM as the engine region `divmmc.ram` (TTDRegionId::DivMmcRam),
// marked by the board's write path while the engine records. The SD sectors are the media manager's (a guest write
// is a barrier). The firmware is not recorded: its hash is the fingerprint field `slots.<slot>.rom`.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/divmmc/divmmcpaging.h"
#include "emulator/slots/card.h"

class EmulatorContext;
class DivMmcTtd;
class DivMmcSdSlot;

class DivMmcCard final : public ICard
{
public:
    static constexpr const char* kCardId = "divmmc";
    static constexpr const char* kUnoDosRomPath = "rom/divmmc/unodos3.rom";
    static constexpr const char* kEsxDosRomPath = "rom/divmmc/esxmmc.bin";
    static constexpr size_t kSdSlots = 2;
    static constexpr const char* kSdSlotIds[kSdSlots] = { "sd.divmmc0", "sd.divmmc1" };
    static constexpr size_t kTtdStateSize = DivMmcPaging::kStateSize + DivMmcPaging::kRamSize;

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    explicit DivMmcCard(const CardContext& context);
    ~DivMmcCard() override;

    /// The machine's DivMMC card (the first slot holding one), nullptr without
    static DivMmcCard* Find(const EmulatorContext* context);

    DivMmcPaging& Paging()
    {
        return *_paging;
    }
    const DivMmcPaging& Paging() const
    {
        return *_paging;
    }
    /// "unodos" / "esxdos"
    const std::string& Firmware() const
    {
        return _firmware;
    }
    const std::string& RomPath() const
    {
        return _romPath;
    }

    /// The TTD blob (kTtdStateSize bytes: the board's state, then its RAM)
    void SaveState(uint8_t* dst) const;
    bool LoadState(const uint8_t* src);

    /// region <PortDevice>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    uint8_t Peek(uint16_t port, bool& drives) const override;
    /// /RESET: DivMmcPaging::Reset
    void BusReset() override;
    /// Power on: DivMmcPaging::PowerOn
    void PowerCycle() override;
    void AfterSnapshotLoad() override;
    bool ReportLiveState(StateNode& out) const override;
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// `slots.<slot>.rom`: the firmware the session ran (not recorded)
    void TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const override;
    /// endregion </ICard>

private:
    void LoadFirmware(const CardContext& context);
    void RegisterMedia();
    void UnregisterMedia();

    EmulatorContext* _context;
    std::unique_ptr<DivMmcPaging> _paging;
    std::string _firmware;
    std::string _romPath;
    std::array<std::unique_ptr<DivMmcSdSlot>, kSdSlots> _sdSlots;
    std::unique_ptr<DivMmcTtd> _ttd;
};
