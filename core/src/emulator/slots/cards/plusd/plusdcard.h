#pragma once

// The MGT disk interfaces (Miles Gordon Technology) in the Sinclair edge slot, one class for both models:
//   - the +D (1987): a WD1772 with two drives, 8K of ROM (G+DOS) and 8K of RAM paged over #0000-#3FFF, a printer port
//     and a snapshot button;
//   - the DISCiPLE (1986): the same core (8K ROM GDOS, 8K RAM, WD1772, two drives, printer, snapshot button) plus a
//     Kempston joystick port, the inhibit button, the /DDEN line and a RAM / ROM swap.
// Design: docs/inprogress/2026-11-15-mgt-plus-d/design.md §3 and §12 (DISCiPLE); hardware: hardware-reference.md
// (Fuse plusd.c / disciple.c, MAME spectrum/mgt.cpp, the G+DOS and GDOS ROMs).
//
// The +D as emulated:
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
//     ignores it). /RESET pages the +D out (Fuse maps nothing after plusd_reset; H-2, H-7);
//
// The DISCiPLE as emulated (MAME mgt.cpp, written with the PAL dumps; Fuse disciple.c agrees on the ports):
//   - ports, low byte: #1B / #5B / #9B / #DB the WD1772 (A7 A6 = register); #1F write: the control latch, read: the
//     Kempston joystick on bits 0-4 (the machine's joystick), bit 6 printer not busy (no printer: 0, busy), bits 5 and
//     7 low; #3B network (not emulated: writes ignored); #7B write swaps RAM and ROM (RAM at #0000, ROM at #2000),
//     read swaps them back; #BB read pages the card in, write pages it out; #FB printer data (write only);
//   - control latch #1F: bit 0 = 1 drive 1, 0 drive 2 (always one drive), bit 1 side, bit 2 /DDEN (1 = single
//     density), bit 3 ROM A13 (an 8K ROM: ignored), bit 4 lets the card page with the inhibit button pressed, bit 5
//     the pass-through select, bit 6 printer strobe (pulses as on the +D), bit 7 network;
//   - paging: opcode fetches at #0001, #0008, #0066 and #028E. With the inhibit button pressed (card option
//     `inhibit=on`) the card pages in only while latch bit 4 is set, and a latch write with bit 4 clear pages it out.
//     After /RESET paging is held off for 10 us (MAME m_reset_delay): the reset vector's fetch at #0001 would page in
//     GDOS, whose reset entry jumps to the 48K ROM's START at #11CB, a 128K's ROM 0 then hangs; held, the machine boots
//     its own ROM and GDOS comes in at its first trap. The RAM / ROM swap is cleared by /RESET.
//
// Both: the WD1772 is a card controller (EmulatorContext::cardFdcs), not pBetaDisk; its drives are media slots
// `fdd.plusd1` / `fdd.plusd2` (aliases `plusd1` / `plusd2`), `fdd.disciple1` / `fdd.disciple2` (`disciple1` /
// `disciple2`).
//
// The ROM is `rom/gdos-pd.rom` (G+DOS v1a) or `rom/gdos.rom` (GDOS v3), resolved as every ROM (working dir,
// executable, resources). A missing ROM refuses the card (the slot manager reports why). The M1 hook is exclusive: on a
// machine whose decoder holds it (ATM3, Sprinter, TS-Conf, Next) the card cannot be built either.
//
// Time travel: one blob of type PlusD (75, "<slot>.plusd") or Disciple (76, "<slot>.disciple"): paging, latches,
// printer, 8K RAM, the WD1772's blob with its two drives and its command in flight; the DISCiPLE's blob adds the swap,
// the reset hold-off and the inhibit button. Disk writes end a session as on every WD machine. The ROM is not recorded:
// its hash is the fingerprint field `slots.<slot>.rom`.

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
    enum class Model : uint8_t
    {
        PlusD,
        Disciple,
    };

    static constexpr const char* kCardId = "plusd";
    static constexpr const char* kDiscipleCardId = "disciple";
    static constexpr const char* kRomPath = "rom/gdos-pd.rom";
    static constexpr const char* kDiscipleRomPath = "rom/gdos.rom";
    static constexpr uint32_t kRomSize = 0x2000;
    static constexpr uint32_t kRamSize = 0x2000;
    static constexpr uint16_t kTrapEnd = 0x0300;  ///< every trap address is below it

    /// +D ports (low byte)
    static constexpr uint8_t kPortStatusCommand = 0xE3;
    static constexpr uint8_t kPortTrack = 0xEB;
    static constexpr uint8_t kPortSector = 0xF3;
    static constexpr uint8_t kPortData = 0xFB;
    static constexpr uint8_t kPortControl = 0xEF;
    static constexpr uint8_t kPortPaging = 0xE7;
    static constexpr uint8_t kPortPrinter = 0xF7;

    /// DISCiPLE ports (low byte): the WD1772's register is A7 A6
    static constexpr uint8_t kDisciplePortFdc = 0x1B;  ///< #1B status / command, #5B track, #9B sector, #DB data
    static constexpr uint8_t kDisciplePortControl = 0x1F;  ///< write: control latch; read: Kempston, printer busy
    static constexpr uint8_t kDisciplePortNetwork = 0x3B;
    static constexpr uint8_t kDisciplePortBoot = 0x7B;     ///< write: RAM at #0000; read: ROM at #0000
    static constexpr uint8_t kDisciplePortPaging = 0xBB;   ///< read: page in; write: page out
    static constexpr uint8_t kDisciplePortPrinter = 0xFB;  ///< write: printer data

    /// +D control latch bits
    static constexpr uint8_t kControlDrive1 = 0x01;
    static constexpr uint8_t kControlDrive2 = 0x02;
    static constexpr uint8_t kControlStrobe = 0x40;
    static constexpr uint8_t kControlSide = 0x80;

    /// DISCiPLE control latch bits
    static constexpr uint8_t kDiscipleControlDrive1 = 0x01;  ///< 1: drive 1, 0: drive 2
    static constexpr uint8_t kDiscipleControlSide = 0x02;
    static constexpr uint8_t kDiscipleControlSingleDensity = 0x04;  ///< the /DDEN line
    static constexpr uint8_t kDiscipleControlRomBank = 0x08;
    static constexpr uint8_t kDiscipleControlInhibitOverride = 0x10;
    static constexpr uint8_t kDiscipleControlStrobe = 0x40;

    /// The DISCiPLE's paging hold-off after /RESET (MAME: 10 us, "a guess")
    static constexpr uint64_t kResetHoldTStates = 35;

    static constexpr size_t kDrives = 2;

    /// The TTD blob (SaveState / LoadState), layout v1, little endian:
    ///   0 version, 1 paged, 2 control latch, 3 printer data latch, 4 byte taken at the strobe's first edge,
    ///   5 first edge seen, 6-9 bytes printed, 10 last byte printed, 11 no drive selected, 12-19 the WD1772's spin-up
    ///   deadline, 20 RAM (8192), then the WD1772's blob (WD1793::kTtdStateSize) and its command in flight
    ///   (WD1793::kTransferContextSize). The DISCiPLE's blob goes on (kDiscipleStateSize): RAM / ROM swapped, reset
    ///   hold-off on, its start seen, inhibit button, 4 reserved, the hold-off's start (8 bytes, absolute T-states)
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kStateRamOffset = 20;
    static constexpr size_t kStateFdcOffset = kStateRamOffset + 0x2000;
    static constexpr size_t kStateFdcSize = 254;
    static constexpr size_t kStateFdcContextSize = 112;
    static constexpr size_t kStateSize = kStateFdcOffset + kStateFdcSize + kStateFdcContextSize;
    static constexpr size_t kDiscipleStateOffset = kStateSize;
    static constexpr size_t kDiscipleStateSize = kStateSize + 16;
    static constexpr const char* kDriveSlotIds[kDrives] = { "fdd.plusd1", "fdd.plusd2" };
    static constexpr const char* kDiscipleDriveSlotIds[kDrives] = { "fdd.disciple1", "fdd.disciple2" };

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);
    static std::unique_ptr<ICard> CreateDisciple(const CardContext& context);

    PlusDCard(const CardContext& context, Model model);
    ~PlusDCard() override;

    /// The machine's MGT card, either model (the first slot holding one), nullptr without
    static PlusDCard* Find(const EmulatorContext* context);

    /// True when the opcode fetch at `address` pages the card in
    static bool IsTrapAddress(Model model, uint16_t address)
    {
        if (model == Model::Disciple)
            return address == 0x0001 || address == 0x0008 || address == 0x0066 || address == 0x028E;
        return address == 0x0008 || address == 0x003A || address == 0x0066 || address == 0x028E;
    }
    static bool IsTrapAddress(uint16_t address)
    {
        return IsTrapAddress(Model::PlusD, address);
    }

    /// region <Model>
    Model GetModel() const
    {
        return _model;
    }
    bool IsDisciple() const
    {
        return _model == Model::Disciple;
    }
    /// "plusd" / "disciple"
    const char* CardId() const
    {
        return IsDisciple() ? kDiscipleCardId : kCardId;
    }
    /// "MGT +D" / "MGT DISCiPLE"
    const char* DisplayName() const
    {
        return IsDisciple() ? "MGT DISCiPLE" : "MGT +D";
    }
    /// The media slot of drive `index` (0, 1)
    const char* DriveSlotId(size_t index) const
    {
        return (IsDisciple() ? kDiscipleDriveSlotIds : kDriveSlotIds)[index < kDrives ? index : 0];
    }
    size_t StateSize() const
    {
        return IsDisciple() ? kDiscipleStateSize : kStateSize;
    }
    /// endregion </Model>

    /// region <State>
    bool IsPaged() const
    {
        return _paged;
    }
    uint8_t Control() const
    {
        return _control;
    }
    /// DISCiPLE: RAM at #0000, ROM at #2000 (port #7B written)
    bool IsMemorySwapped() const
    {
        return _memSwap;
    }
    /// DISCiPLE: paging held off after /RESET
    bool IsResetHeld() const
    {
        return _resetHold;
    }
    /// DISCiPLE: the inhibit button (card option `inhibit`)
    bool IsInhibited() const
    {
        return _inhibit;
    }
    void SetInhibit(bool pressed)
    {
        _inhibit = pressed;
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
        SetPagedLine(paged);
        _snapshotPaging = true;
    }
    /// A snapshot load (Emulator::LoadSnapshotNow) around the loader: its reset pages the card out; unless the
    /// snapshot restored the paging (SetPaged, RestoreSnapshot) the program runs paged out
    void BeginSnapshotLoad()
    {
        _snapshotPaging = false;
    }
    void EndSnapshotLoad()
    {
        if (!_snapshotPaging)
            SetPagedLine(false);
        _snapshotPaging = false;
        // The program runs: no reset hold-off on the DISCiPLE
        _resetHold = false;
    }

    /// The TTD blob (StateSize() bytes)
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
    /// /RESET: paged out, the WD1772 reset; the RAM keeps its contents. +D: latch cleared (no drive); DISCiPLE: the
    /// latch keeps its value (a 74LS374), the swap cleared, paging held off for kResetHoldTStates
    void BusReset() override;
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// `slots.<slot>.rom`: the DOS ROM the session ran (not recorded)
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
    /// The paging line: the host bus overlay (ROM / RAM at #0000-#3FFF) is installed only while it is high, so a
    /// paged-out card costs the machine's memory accesses nothing (hostbusoverlay.h)
    void SetPagedLine(bool paged);
    void LoadRom();
    void WriteControl(uint8_t value);
    void StrobeEdge();
    void WriteControlLines();
    void Attach();
    void Detach();

    /// region <DISCiPLE>
    uint8_t DiscipleRead(uint16_t port, bool& drives);
    void DiscipleWrite(uint16_t port, uint8_t value);
    uint8_t DisciplePeek(uint16_t port, bool& drives) const;
    void DiscipleWriteControl(uint8_t value);
    /// The card may page in: no reset hold-off, and the inhibit button released or overridden by latch bit 4
    bool DisciplePagingAllowed();
    /// True while the reset hold-off runs (moves it on first)
    bool HoldingOff();
    /// The reset hold-off ends kResetHoldTStates after the first opcode fetch that follows /RESET
    void UpdateResetHold();
    uint64_t Now() const;
    /// endregion </DISCiPLE>

    EmulatorContext* _context;
    Model _model;
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

    // DISCiPLE
    bool _memSwap = false;
    bool _resetHold = false;
    bool _resetHoldStarted = false;
    uint64_t _resetHoldStart = 0;
    bool _inhibit = false;

    std::array<std::unique_ptr<FDD>, kDrives> _drives;
    std::unique_ptr<WD1793> _fdc;
    std::array<std::unique_ptr<FloppyDriveSlot>, kDrives> _driveSlots;
    std::unique_ptr<PlusDTtd> _ttd;

    bool _fdcAdded = false;
    bool _overlayInstalled = false;
    bool _hookInstalled = false;
    uint32_t _savedHookEnd = 0x10000;
};
