// PlusDCard (core/src/emulator/slots/cards/plusd/plusdcard.h): the MGT +D in the edge slot of a 48K / 128K. Its ports
// reach the WD1772, the control latch selects the drive and the side, the paging port and the M1 traps page the
// G+DOS ROM and the RAM over #0000-#3FFF, and G+DOS starts the machine to BASIC.
// Design: docs/inprogress/2026-11-15-mgt-plus-d/design.md §3, test plan §2.2.

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/machinevariants.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/state/devicestate.h"

namespace
{

class PlusDCard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::string& model, const std::vector<std::pair<std::string, std::string>>& slots)
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    PlusDCard* Card() const
    {
        return dynamic_cast<PlusDCard*>(_context->pSlotManager->FindCard("edge.1"));
    }

    Z80* Cpu() const
    {
        return _context->pCore->GetZ80();
    }

    uint8_t In(uint16_t port, bool& drove)
    {
        return _context->pPortDecoder->ReadCycle(port, 0x8000, drove);
    }

    void Out(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->WriteCycle(port, value, 0x8000);
    }

    /// Put a program at #8000 and run `steps` instructions of it
    void Run(const std::vector<uint8_t>& program, int steps)
    {
        uint16_t address = 0x8000;
        for (uint8_t b : program)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
        Z80* cpu = Cpu();
        cpu->pc = 0x8000;
        cpu->sp = 0xFF00;
        cpu->halted = false;
        for (int i = 0; i < steps; i++)
            cpu->Z80Step(true);
    }

    /// What the CPU reads at `address` (LD A,(nn) through the bus overlays), stored at #9000
    uint8_t CpuReads(uint16_t address)
    {
        Run({0x3A, static_cast<uint8_t>(address), static_cast<uint8_t>(address >> 8),  // LD A,(nn)
             0x32, 0x00, 0x90,                                                       // LD (#9000),A
             0x18, 0xFE},                                                            // JR $
            2);
        return _context->pMemory->DirectReadFromZ80Memory(0x9000);
    }
};

}  // namespace

/// The card on a 48K (~10 ms): built, its controller in the card list, its drives as media slots, the M1 hook its own
TEST_F(PlusDCard_Test, PluggedOn48K)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr) << "the edge slot builds the card";
    ASSERT_NE(card->Fdc(), nullptr);
    EXPECT_TRUE(card->Fdc()->Is1772());
    EXPECT_EQ(_context->cardFdcCount, 1u);
    EXPECT_EQ(_context->cardFdcs[0], card->Fdc());
    EXPECT_EQ(Cpu()->machineM1Hook, static_cast<IMachineM1Hook*>(card));
    EXPECT_EQ(Cpu()->machineM1HookEnd, PlusDCard::kTrapEnd);
    EXPECT_TRUE(_context->pMediaManager->HasSlot("fdd.plusd1"));
    EXPECT_TRUE(_context->pMediaManager->HasSlot("fdd.plusd2"));
    EXPECT_EQ(card->Drive(0)->getSlotId(), "fdd.plusd1");
    EXPECT_EQ(card->Rom()[5], 0xC3) << "G+DOS v1a: NOP; LD BC,#0101; NOP; JP #0533";
}

/// No card, nothing of it (~5 ms): no controller, no hook, no drive slots
TEST_F(PlusDCard_Test, AbsentLeavesTheMachineAlone)
{
    Create("48K", {});
    EXPECT_EQ(Card(), nullptr);
    EXPECT_EQ(_context->cardFdcCount, 0u);
    EXPECT_FALSE(_context->pMainLoop->StepsCardFdcs()) << "no card controller to step";
    EXPECT_EQ(Cpu()->machineM1Hook, nullptr);
    EXPECT_FALSE(_context->pMediaManager->HasSlot("fdd.plusd1"));
}

/// The FDC registers on #E3 / #EB / #F3 / #FB, the low byte decoded (~5 ms)
TEST_F(PlusDCard_Test, PortsReachTheWd1772)
{
    Create("48K", {{"edge.1", "plusd"}});
    ASSERT_NE(Card(), nullptr);
    bool drove = false;
    Out(0x00EB, 0x23);
    EXPECT_EQ(In(0x12EB, drove), 0x23) << "track register, A8-A15 not decoded";
    EXPECT_TRUE(drove);
    Out(0xFFF3, 0x05);
    EXPECT_EQ(In(0x00F3, drove), 0x05) << "sector register";
    Out(0x00FB, 0x5A);
    EXPECT_EQ(Card()->Fdc()->getDataRegister(), 0x5A) << "data register";
    EXPECT_EQ(Card()->Fdc()->getTrackRegister(), 0x23);

    bool peekDrives = false;
    EXPECT_EQ(Card()->Peek(0x00EB, peekDrives), 0x23);
    EXPECT_TRUE(peekDrives);
    In(0x00E7, drove);
    EXPECT_FALSE(drove) << "#E7: the card pages in and leaves the bus alone";
}

/// The control latch #EF: drive 1 / 2 / none, side on bit 7 (~5 ms)
TEST_F(PlusDCard_Test, ControlLatchSelectsDriveAndSide)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    WD1793* fdc = card->Fdc();
    EXPECT_NE(fdc->getDrive(), card->Drive(0)) << "after reset no drive is selected";
    EXPECT_NE(fdc->getDrive(), card->Drive(1));

    Out(0x00EF, 0x01);
    EXPECT_EQ(fdc->getDrive(), card->Drive(0));
    EXPECT_FALSE(fdc->getSideUp());
    Out(0x00EF, 0x81);
    EXPECT_TRUE(fdc->getSideUp()) << "bit 7: side 1";
    Out(0x00EF, 0x82);
    EXPECT_EQ(fdc->getDrive(), card->Drive(1));
    EXPECT_EQ(card->Control(), 0x82);
    Out(0x00EF, 0x00);
    EXPECT_NE(fdc->getDrive(), card->Drive(0));
    EXPECT_NE(fdc->getDrive(), card->Drive(1));
}

/// Paged in, the CPU sees the ROM at #0000-#1FFF and the RAM at #2000-#3FFF; paged out, the machine's ROM (~5 ms)
TEST_F(PlusDCard_Test, OverlayShowsRomAndRam)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    ASSERT_FALSE(card->IsPaged()) << "paged out after reset";
    bool drove = false;
    In(0x00E7, drove);
    ASSERT_TRUE(card->IsPaged()) << "IN #E7 pages in";
    EXPECT_EQ(CpuReads(0x0001), card->Rom()[1]);

    // LD A,#A5; LD (#2345),A: the write lands in the +D RAM
    Run({0x3E, 0xA5, 0x32, 0x45, 0x23, 0x18, 0xFE}, 2);
    EXPECT_EQ(card->Ram()[0x0345], 0xA5);
    EXPECT_EQ(CpuReads(0x2345), 0xA5);

    Out(0x00E7, 0x00);
    EXPECT_FALSE(card->IsPaged()) << "OUT #E7 pages out";
    EXPECT_EQ(CpuReads(0x0001), 0xAF) << "the 48K ROM: DI; XOR A at #0000";
    EXPECT_NE(CpuReads(0x2345), 0xA5) << "the 48K ROM at #2345";

    In(0x00E7, drove);
    EXPECT_TRUE(card->IsPaged()) << "IN #E7 pages in again";
}

/// The card's bus overlay is on only while it is paged in: paged out, the memory runs the plain interfaces, as on
/// a 48K without the card (test-plan §7: the idle card costs nothing per access) (~5 ms)
TEST_F(PlusDCard_Test, OverlayOnlyWhilePagedIn)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    Core* core = _context->pCore;
    ASSERT_FALSE(card->IsPaged());
    EXPECT_FALSE(core->IsBusOverlayInstalled(card)) << "paged out after reset";
    EXPECT_EQ(std::string(core->GetMemoryInterfaceName()).find("overlay"), std::string::npos)
        << core->GetMemoryInterfaceName();

    card->BeforeMachineM1(0x028E);
    EXPECT_TRUE(core->IsBusOverlayInstalled(card)) << "the trap pages in";
    EXPECT_NE(std::string(core->GetMemoryInterfaceName()).find("overlay"), std::string::npos);
    EXPECT_EQ(CpuReads(0x0001), card->Rom()[1]);

    Out(0x00E7, 0x00);
    EXPECT_FALSE(core->IsBusOverlayInstalled(card)) << "OUT #E7 pages out";
    bool drove = false;
    In(0x00E7, drove);
    EXPECT_TRUE(core->IsBusOverlayInstalled(card)) << "IN #E7 pages in";

    // A snapshot that restores the card paged out, and a reset, take the overlay off too
    card->SetPaged(false);
    EXPECT_FALSE(core->IsBusOverlayInstalled(card));
    card->SetPaged(true);
    EXPECT_TRUE(core->IsBusOverlayInstalled(card));
    card->BusReset();
    EXPECT_FALSE(core->IsBusOverlayInstalled(card)) << "reset pages out";
}

/// The WD1772 is stepped only while it is awake: asleep (2 s idle) the machine's step skips it, a port access
/// wakes it (test-plan §7) (~5 ms)
TEST_F(PlusDCard_Test, WorkGateFollowsTheControllersSleep)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    WD1793CUT* fdc = reinterpret_cast<WD1793CUT*>(card->Fdc());
    fdc->enterSleepMode();  // as after 2 s idle
    EXPECT_FALSE(_context->pMainLoop->StepsCardFdcs()) << "asleep: not stepped";

    bool drove = false;
    In(0x00E3, drove);  // a status read
    ASSERT_FALSE(card->Fdc()->IsAsleep()) << "a port access wakes it";
    EXPECT_TRUE(_context->pMainLoop->StepsCardFdcs());

    fdc->enterSleepMode();
    EXPECT_FALSE(_context->pMainLoop->StepsCardFdcs()) << "asleep again";
    fdc->wakeUp();

    // Awake, the card's removal takes the work off and adding it back puts it on
    _context->RemoveCardFdc(card->Fdc());
    EXPECT_FALSE(_context->pMainLoop->StepsCardFdcs());
    ASSERT_TRUE(_context->AddCardFdc(card->Fdc()));
    EXPECT_TRUE(_context->pMainLoop->StepsCardFdcs());
}

/// An opcode fetch at a trap address pages in before the byte is read; other low fetches do not (~5 ms)
TEST_F(PlusDCard_Test, M1TrapsPageIn)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);

    for (uint16_t trap : {0x0008, 0x003A, 0x0066, 0x028E})
    {
        Out(0x00E7, 0x00);
        ASSERT_FALSE(card->IsPaged());
        Cpu()->pc = static_cast<uint16_t>(trap - 1);  // the fetch before the trap does not page in
        Cpu()->halted = false;
        card->BeforeMachineM1(static_cast<uint16_t>(trap - 1));
        EXPECT_FALSE(card->IsPaged()) << "fetch at " << trap - 1;
        card->BeforeMachineM1(trap);
        EXPECT_TRUE(card->IsPaged()) << "fetch at " << trap;
    }

    // Through the CPU: RST 8 from paged-out RAM code fetches #0008 from the +D ROM
    Out(0x00E7, 0x00);
    Run({0xCF}, 2);  // RST 8, then the instruction at #0008
    EXPECT_TRUE(card->IsPaged());
}

/// /RESET pages out and clears the latch; the RAM survives (~10 ms)
TEST_F(PlusDCard_Test, ResetPagesOut)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    card->Ram()[0x10] = 0x77;
    Out(0x00EF, 0x01);
    bool drove = false;
    In(0x00E7, drove);
    ASSERT_TRUE(card->IsPaged());

    _emulator->Reset();
    EXPECT_FALSE(card->IsPaged()) << "the machine starts from its own ROM";
    EXPECT_EQ(CpuReads(0x0001), 0xAF) << "the 48K ROM: DI; XOR A at #0000";
    EXPECT_EQ(card->Control(), 0x00);
    EXPECT_EQ(card->Ram()[0x10], 0x77) << "the RAM keeps its contents over a reset";
}

/// The 48K boots to the BASIC prompt with the card: its first interrupt pages G+DOS in (#003A), which sets its RAM up
TEST_F(PlusDCard_Test, GdosBoots48K)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    _emulator->Reset();
    ASSERT_FALSE(card->IsPaged());
    ASSERT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 300)) << "G+DOS hands the machine to BASIC";
    // The first interrupt after the reset runs G+DOS's RAM set-up (#02A2: 8K cleared, ~3 frames, then the 'X' mark at
    // #3DE4); the later ones only pass through the #003A trap and page out at #0040
    EmulatorTestHelper::RunFramesFast(_emulator, 6);
    EXPECT_EQ(card->Ram()[0x1DE4], 'X') << "G+DOS set its RAM up";
    EXPECT_FALSE(card->IsPaged()) << "G+DOS pages itself out; pc=" << std::hex << Cpu()->pc;
}

/// G+DOS starts the 128K to its menu / BASIC
TEST_F(PlusDCard_Test, GdosBoots128K)
{
    Create("128K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    _emulator->Reset();
    ASSERT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 300)) << "G+DOS hands the machine to BASIC";
    EmulatorTestHelper::RunFramesFast(_emulator, 6);
    EXPECT_EQ(card->Ram()[0x1DE4], 'X') << "G+DOS set its RAM up";
    EXPECT_FALSE(card->IsPaged()) << "pc=" << std::hex << Cpu()->pc;
}

/// The 128K with the card boots to its menu whatever the RAM holds at power-on: the reset runs ROM 0, not the +D's
/// #0000 entry (it hands over to the 48K ROM's #11CB; with random RAM the 128K hung at ROM 0's #032C HALT). Three
/// machines with random RAM (~150 ms)
TEST(PlusDCard_Boot, Boots128KFromRandomRam)
{
    for (int machine = 0; machine < 3; machine++)
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride({{"edge.1", "plusd"}}, slotSet, error)) << error;
        Emulator* emulator =
            EmulatorTestHelper::CreateStandardEmulator("128K", LoggerLevel::LogError, RamPowerOn::Random, slotSet);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* context = emulator->GetContext();
        PlusDCard* card = PlusDCard::Find(context);
        ASSERT_NE(card, nullptr);
        emulator->Reset();
        EXPECT_FALSE(card->IsPaged());
        EXPECT_TRUE(EmulatorTestHelper::RunUntilBASICReady(emulator, 300)) << "machine " << machine;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// The presets by name and alias: the 48K / 128K with `edge.1 = plusd`, reported as the variant (~1 ms)
TEST(PlusDCard_Variant, FoundByNameAndApplied)
{
    const MachineVariant* variant48 = MachineVariants::Find("48K-PLUSD");
    const MachineVariant* variant128 = MachineVariants::Find("128k-plusd");
    ASSERT_NE(variant48, nullptr);
    ASSERT_NE(variant128, nullptr);
    EXPECT_EQ(MachineVariants::Find("48K-PLUS-D"), variant48);
    EXPECT_STREQ(variant48->baseModel, "48K");
    EXPECT_STREQ(variant128->baseModel, "128K");

    CONFIG config{};
    config.mem_model = MM_SPECTRUM48;
    config.trdos_present = true;  // the shipped spectrum48 ini says Beta128=1
    EXPECT_EQ(MachineVariants::Of(config), nullptr);
    variant48->apply(config);
    EXPECT_EQ(MachineVariants::Of(config), variant48);
    EXPECT_FALSE(config.trdos_present) << "no Beta 128 next to the +D";

    CONFIG config128{};
    config128.mem_model = MM_SPECTRUM128;
    variant128->apply(config128);
    EXPECT_EQ(MachineVariants::Of(config128), variant128);
}

/// Created by the preset name, the machine has the card (~10 ms)
TEST_F(PlusDCard_Test, PresetBuildsTheCard)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K-PLUSD", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    EXPECT_EQ(_context->config.mem_model, MM_SPECTRUM48);
    EXPECT_NE(Card(), nullptr);
    EXPECT_EQ(EmulatorManager::GetMachineIdentity(*_emulator).Variant, "48K-PLUSD");
}

/// `state fdc` on a 48K-PLUSD reports the card's WD1772: paging, the latch, the drives by media slot (~10 ms)
TEST_F(PlusDCard_Test, StateFdcReportsTheCard)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K-PLUSD", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    ASSERT_NE(Card(), nullptr);
    Out(0x00EF, 0x02);
    bool drove = false;
    In(0x00E7, drove);  // pages in (a reset leaves the card paged out)

    const StateNode report = DeviceState::Fdc(_context);
    ASSERT_NE(report.find("controller"), nullptr);
    EXPECT_EQ(report.find("controller")->s, "WD1772 (MGT +D)");
    EXPECT_EQ(report.find("slot")->s, "edge.1");
    EXPECT_TRUE(report.find("paged")->b);
    EXPECT_EQ(report.find("selected_drive")->i, 1);
    const StateNode* drives = report.find("drives");
    ASSERT_NE(drives, nullptr);
    ASSERT_EQ(drives->items.size(), 2u);
    EXPECT_EQ(drives->items[1].find("media_slot")->s, "fdd.plusd2");
}

/// The printer port (~5 ms): #F7 latches the data, a strobe pulse on #EF bit 6 (two edges) prints the byte latched at
/// the first; no printer: #F7 reads busy
/// The card's drives as media slots on a real 48K-PLUSD (new-platform checklist 6, ~10 ms): the aliases resolve, a
/// disk inserts and swaps, a write of the guest's reaches the manager (the disk is dirty and holds the eject), an eject
/// empties the drive
TEST_F(PlusDCard_Test, DrivesAreMediaSlots)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    MediaManager& manager = *_context->pMediaManager;

    std::string slot;
    ASSERT_TRUE(MediaControl::ResolveSelector(manager, "plusd2", slot).Ok());
    EXPECT_EQ(slot, "fdd.plusd2");

    const std::string fixture = (TestPathHelper::FindProjectRoot() / "testdata/loaders/mgt/synthetic.mgt").string();
    const std::string copy = TestPathHelper::GetUniqueTestScratchPath("plusd-slot.mgt");
    std::filesystem::copy_file(fixture, copy, std::filesystem::copy_options::overwrite_existing);
    MediaSource first;
    first.path = fixture;
    InsertOptions readOnly;
    readOnly.access = AccessMode::ReadOnly;
    ASSERT_TRUE(manager.Insert(slot, first, readOnly).Ok());
    DiskImage* before = card->Drive(1)->getDiskImage();
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(card->Drive(0)->getDiskImage(), nullptr) << "drive 1 untouched";

    MediaSource second;
    second.path = copy;
    ASSERT_TRUE(manager.Insert(slot, second).Ok()) << "a swap replaces the disk";
    DiskImage* disk = card->Drive(1)->getDiskImage();
    ASSERT_NE(disk, nullptr);
    EXPECT_EQ(disk, manager.GetMedium(slot)->Floppy());

    const std::vector<uint8_t> sector(512, 0x42);
    disk->getTrack(4)->writeSectorData(0, sector.data(), sector.size());
    EXPECT_TRUE(manager.Info(slot)->dirty) << "the write reached NoteWrite";
    EXPECT_FALSE(manager.Eject(slot).Ok()) << "unsaved writes hold the disk";

    EjectOptions discard;
    discard.disposition = Disposition::Discard;
    ASSERT_TRUE(manager.Eject(slot, discard).Ok());
    EXPECT_EQ(card->Drive(1)->getDiskImage(), nullptr);
}

TEST_F(PlusDCard_Test, PrinterStrobeTakesTheLatch)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    bool drove = false;
    EXPECT_EQ(In(0x00F7, drove), 0xFF) << "no printer: busy";
    EXPECT_TRUE(drove);

    Out(0x00F7, 'A');
    Out(0x00EF, 0x40);  // first edge: 'A' taken
    Out(0x00F7, 'B');
    Out(0x00EF, 0x00);  // second edge: printed
    EXPECT_EQ(card->PrintedBytes(), 1u);
    EXPECT_EQ(card->LastPrinted(), 'A');
    EXPECT_EQ(card->PrinterData(), 'B');

    Out(0x00EF, 0x41);  // the other polarity, the drive bit changing with it
    Out(0x00EF, 0x01);
    EXPECT_EQ(card->PrintedBytes(), 2u);
    EXPECT_EQ(card->LastPrinted(), 'B');
    Out(0x00EF, 0x81);  // no strobe edge: nothing
    EXPECT_EQ(card->PrintedBytes(), 2u);
}

/// Time travel (~10 ms): one PlusD blob under the slot, registered with the machine and matching the slot set
TEST_F(PlusDCard_Test, TimeTravelRegistersTheCard)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].instance, "edge.1.plusd");
    EXPECT_EQ(devices[0].device->TTDType(), ttd::TTDDeviceType::PlusD);
    EXPECT_EQ(devices[0].device->TTDStateSize(), PlusDCard::kStateSize);

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(_context, registry, owned, &error)) << error;
    EXPECT_TRUE(registry.IsRegistered(ttd::TTDDeviceType::PlusD));

    std::vector<std::pair<std::string, uint64_t>> fingerprint;
    card->TtdFingerprint(fingerprint);
    ASSERT_EQ(fingerprint.size(), 1u);
    EXPECT_EQ(fingerprint[0].first, "slots.edge.1.rom");
}

/// The blob round trip (~5 ms): paging, latches, printer, RAM, the WD1772's registers and "no drive" come back
TEST_F(PlusDCard_Test, StateRoundTrip)
{
    Create("48K", {{"edge.1", "plusd"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    Out(0x00EF, 0x82);  // drive 2, side 1
    Out(0x00EB, 0x21);
    Out(0x00F3, 0x07);
    Out(0x00F7, 'Z');
    Out(0x00EF, 0x02);  // drive 2, side 0
    card->Ram()[0x0123] = 0xA5;
    card->Ram()[0x1FFF] = 0x5A;
    Out(0x00E7, 0);  // paged out
    std::vector<uint8_t> saved(PlusDCard::kStateSize);
    card->SaveState(saved.data());

    Out(0x00EF, 0x00);  // no drive
    Out(0x00EB, 0x00);
    Out(0x00F7, 0);
    card->Ram()[0x0123] = 0;
    card->Ram()[0x1FFF] = 0;
    bool drove = false;
    In(0x00E7, drove);  // paged in
    ASSERT_TRUE(card->LoadState(saved.data()));

    EXPECT_FALSE(card->IsPaged());
    EXPECT_EQ(card->Control(), 0x02);
    EXPECT_EQ(card->PrinterData(), 'Z');
    EXPECT_EQ(card->Ram()[0x0123], 0xA5);
    EXPECT_EQ(card->Ram()[0x1FFF], 0x5A);
    EXPECT_EQ(card->Fdc()->getTrackRegister(), 0x21);
    EXPECT_EQ(card->Fdc()->getSectorRegister(), 0x07);
    EXPECT_EQ(card->Fdc()->getDrive(), card->Drive(1));

    // Saved with no drive on the select lines: restored with none, not the last drive the blob names
    Out(0x00EF, 0x00);
    card->SaveState(saved.data());
    Out(0x00EF, 0x01);
    ASSERT_TRUE(card->LoadState(saved.data()));
    EXPECT_NE(card->Fdc()->getDrive(), card->Drive(0));
    EXPECT_NE(card->Fdc()->getDrive(), card->Drive(1));

    saved[0] = PlusDCard::kStateVersion + 1;
    EXPECT_FALSE(card->LoadState(saved.data())) << "another layout is refused";
}
