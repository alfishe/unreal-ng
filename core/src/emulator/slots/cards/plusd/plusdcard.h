#pragma once

// The MGT +D disk interface (Miles Gordon Technology, 1987) in the Sinclair edge slot: a WD1772 floppy controller with
// two drives, 8K of ROM (G+DOS) and 8K of RAM paged over #0000-#3FFF, a printer port and a snapshot button.
// Design: docs/inprogress/2026-11-15-mgt-plus-d/design.md §3; hardware: hardware-reference.md (Fuse plusd.c, MAME
// spectrum/mgt.cpp, the G+DOS ROM).
//
// The hardware as emulated:
//   - ports, low byte decoded (Fuse; MAME's A1-A6 decode is open item H-3): #E3 status / command, #EB track, #F3
//     sector, #FB data (the WD1772's A1 A0 = 0..3); #EF control latch (write only); #E7 read pages the +D in (the card
//     leaves the data bus alone), write pages it out; #F7 printer: a write latches the data byte, a read answers
//     bit 7 = busy (no printer is connected: #FF, as Fuse without one);
//   - control latch #EF: bit 0 selects drive 1, bit 1 drive 2 (00: no drive; 11: both lines, drive 1 answers), bit 6
//     the printer strobe, bit 7 the side. A strobe pulse is two edges of bit 6 (either polarity, as Fuse takes it):
//     the first takes the data latch, the second hands it to the printer (counted: PrintedBytes, LastPrinted);
//   - paging: an opcode fetch at #0008, #003A, #0066 or #028E pages the +D in before the byte is read (the Z80's M1
//     hook, machineM1HookEnd = #0300 so higher fetches cost one compare). Paged in, #0000-#1FFF read the ROM and
//     #2000-#3FFF read and write the RAM (a HostBusOverlay; the machine's own write still lands on its ROM, which
//     ignores it). After /RESET the +D is paged in (Fuse plusd_reset; H-2), so G+DOS initialises itself at #0000;
//   - the WD1772 is a card controller (EmulatorContext::cardFdcs), not pBetaDisk; its drives are media slots
//     `fdd.plusd1` / `fdd.plusd2` (aliases `plusd1` / `plusd2`).
//
// The ROM is `rom/gdos-pd.rom` (G+DOS v1a), resolved as every ROM (working dir, executable, resources). A missing ROM
// refuses the card (the slot manager reports why). The M1 hook is exclusive: on a machine whose decoder holds it
// (ATM3, Sprinter, TS-Conf, Next) the card cannot be built either.
//
// Time travel: one blob of type PlusD (71), instance "<slot>.plusd" (SaveState: paging, latches, printer, 8K RAM, the
// WD1772's blob with its two drives and its command in flight). Disk writes end a session as on every WD machine.
// The ROM is not recorded: its hash is the fingerprint field `slots.<slot>.rom`.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/z80.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/slots/card.h"

class EmulatorContext;
class FDD;
class FloppyDriveSlot;
class WD1793;
class PlusDTtd;

class PlusDCard final : public ICard, public HostBusOverlay, public IMachineM1Hook
{
public:
    static constexpr const char* kCardId = "plusd";
    static constexpr const char* kRomPath = "rom/gdos-pd.rom";
    static constexpr uint32_t kRomSize = 0x2000;
    static constexpr uint32_t kRamSize = 0x2000;
    static constexpr uint16_t kTrapEnd = 0x0300;  ///< every trap address is below it

    /// Ports (low byte)
    static constexpr uint8_t kPortStatusCommand = 0xE3;
    static constexpr uint8_t kPortTrack = 0xEB;
    static constexpr uint8_t kPortSector = 0xF3;
    static constexpr uint8_t kPortData = 0xFB;
    static constexpr uint8_t kPortControl = 0xEF;
    static constexpr uint8_t kPortPaging = 0xE7;
    static constexpr uint8_t kPortPrinter = 0xF7;

    /// Control latch bits
    static constexpr uint8_t kControlDrive1 = 0x01;
    static constexpr uint8_t kControlDrive2 = 0x02;
    static constexpr uint8_t kControlStrobe = 0x40;
    static constexpr uint8_t kControlSide = 0x80;

    static constexpr size_t kDrives = 2;

    /// The TTD blob (SaveState / LoadState), layout v1, little endian:
    ///   0 version, 1 paged, 2 control latch, 3 printer data latch, 4 byte taken at the strobe's first edge,
    ///   5 first edge seen, 6-9 bytes printed, 10 last byte printed, 11 no drive selected, 12-19 the WD1772's spin-up
    ///   deadline, 20 RAM (8192), then the WD1772's blob (WD1793::kTtdStateSize) and its command in flight
    ///   (WD1793::kTransferContextSize)
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kStateRamOffset = 20;
    static constexpr size_t kStateFdcOffset = kStateRamOffset + 0x2000;
    static constexpr size_t kStateFdcSize = 254;
    static constexpr size_t kStateFdcContextSize = 112;
    static constexpr size_t kStateSize = kStateFdcOffset + kStateFdcSize + kStateFdcContextSize;
    static constexpr const char* kDriveSlotIds[kDrives] = { "fdd.plusd1", "fdd.plusd2" };

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    explicit PlusDCard(const CardContext& context);
    ~PlusDCard() override;

    /// The machine's +D card (the first slot holding one), nullptr without
    static PlusDCard* Find(const EmulatorContext* context);

    /// True when the opcode fetch at `address` pages the +D in
    static bool IsTrapAddress(uint16_t address)
    {
        return address == 0x0008 || address == 0x003A || address == 0x0066 || address == 0x028E;
    }

    /// region <State>
    bool IsPaged() const
    {
        return _paged;
    }
    uint8_t Control() const
    {
        return _control;
    }
    WD1793* Fdc() const
    {
        return _fdc.get();
    }
    FDD* Drive(size_t index) const
    {
        return index < kDrives ? _drives[index].get() : nullptr;
    }
    const uint8_t* Rom() const
    {
        return _rom.data();
    }
    uint8_t* Ram()
    {
        return _ram.data();
    }
    const uint8_t* RamData() const
    {
        return _ram.data();
    }
    uint8_t PrinterData() const
    {
        return _printerData;
    }
    uint32_t PrintedBytes() const
    {
        return _printedBytes;
    }
    uint8_t LastPrinted() const
    {
        return _lastPrinted;
    }

    /// What a snapshot keeps of the card (SZX PLSD, .z80 byte 59): no command in flight, no printer
    struct SnapshotRegisters
    {
        bool paged = false;
        bool stepIn = true;  ///< the WD1772's last step direction
        uint8_t control = 0;
        uint8_t track = 0;
        uint8_t sector = 0;
        uint8_t data = 0;
        uint8_t status = 0;
    };
    SnapshotRegisters CaptureSnapshot() const;
    /// The latch (drive, side), paging and the task registers; `ram` (8K) when the snapshot carries it
    void RestoreSnapshot(const SnapshotRegisters& registers, const uint8_t* ram);
    void SetPaged(bool paged)
    {
        _paged = paged;
        _snapshotPaging = true;
    }
    /// A snapshot load (Emulator::LoadSnapshotNow) around the loader: its reset pages the +D in; unless the snapshot
    /// restored the paging (SetPaged, RestoreSnapshot) the program runs paged out
    void BeginSnapshotLoad()
    {
        _snapshotPaging = false;
    }
    void EndSnapshotLoad()
    {
        if (!_snapshotPaging)
            _paged = false;
        _snapshotPaging = false;
    }

    /// The TTD blob (kStateSize bytes)
    void SaveState(uint8_t* dst) const;
    /// False (nothing changed) for a blob of another version
    bool LoadState(const uint8_t* src);
    /// endregion </State>

    /// region <PortDevice>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    uint8_t Peek(uint16_t port, bool& drives) const override;
    /// /RESET: paged in, latch cleared (no drive), the WD1772 reset; the RAM keeps its contents
    void BusReset() override;
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// `slots.<slot>.rom`: the G+DOS image the session ran (not recorded)
    void TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const override;
    /// endregion </ICard>

    /// region <HostBusOverlay>
    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;
    /// endregion </HostBusOverlay>

    /// region <IMachineM1Hook>
    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override;
    bool RepeatM1IsInert(uint16_t address) const override;
    /// endregion </IMachineM1Hook>

private:
    void LoadRom();
    void WriteControl(uint8_t value);
    void StrobeEdge();
    void WriteControlLines();
    void Attach();
    void Detach();

    EmulatorContext* _context;
    std::array<uint8_t, kRomSize> _rom{};
    std::array<uint8_t, kRamSize> _ram{};
    bool _paged = true;
    bool _snapshotPaging = false;  ///< a snapshot load restored the paging (BeginSnapshotLoad)
    uint8_t _control = 0;
    uint8_t _printerData = 0;
    uint8_t _strobeByte = 0;      ///< taken at the strobe's first edge
    bool _strobeFirstEdge = false;
    uint32_t _printedBytes = 0;
    uint8_t _lastPrinted = 0;

    std::array<std::unique_ptr<FDD>, kDrives> _drives;
    std::unique_ptr<WD1793> _fdc;
    std::array<std::unique_ptr<FloppyDriveSlot>, kDrives> _driveSlots;
    std::unique_ptr<PlusDTtd> _ttd;

    bool _fdcAdded = false;
    bool _overlayInstalled = false;
    bool _hookInstalled = false;
    uint32_t _savedHookEnd = 0x10000;
};
