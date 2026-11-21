// DivMmcCard (core/src/emulator/slots/cards/divmmc/divmmccard.h): the DivMMC in the edge slot of a 48K / +3. The card
// puts DivMmcPaging on the bus (the claims #E3 / #E7 / #EB, the overlay, the M1 hook), loads its firmware, offers its two
// SD sockets as media slots, follows the machine's reset and power cycle, records its latches and RAM for time travel,
// and reports its state in the slot report. Board rules: divmmcpaging_test.cpp; design:
// docs/inprogress/2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md §6.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdreplaycheck.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/divmmc/divmmccard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/state/devicestate.h"

namespace
{

class DivMmcCard_Test : public ::testing::Test
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

    DivMmcCard* Card() const
    {
        return dynamic_cast<DivMmcCard*>(_context->pSlotManager->FindCard("edge.1"));
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
    uint8_t Peek(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }
    /// One instruction at `pc`
    void StepAt(uint16_t pc)
    {
        Z80* cpu = Cpu();
        cpu->pc = pc;
        cpu->sp = 0xFF00;
        cpu->halted = false;
        cpu->Z80Step(true);
    }
    /// A +2A / +3 ROM (#1FFD bit 2 : #7FFD bit 4) through the latches (the test machine starts in 48 BASIC with
    /// #7FFD locked, so the ports cannot page)
    void PageRom(uint8_t rom)
    {
        _context->emulatorState.p1FFD = static_cast<uint8_t>((rom & 2) << 1);
        _context->emulatorState.p7FFD = static_cast<uint8_t>((rom & 1) << 4);
        _context->pPortDecoder->UpdateModelMemoryBanks();
    }
    /// The slot report's entry for edge.1
    StateNode SlotEntry() const
    {
        const StateNode report = DeviceState::Slots(_context);
        const StateNode* slots = report.find("slots");
        if (slots != nullptr)
            for (const StateNode& entry : slots->items)
                if (const StateNode* slot = entry.find("slot"); slot != nullptr && slot->s == "edge.1")
                    return entry;
        return {};
    }
    static std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

}  // namespace

/// The card on a 48K (~10 ms): built with the shipped UnoDOS 3, the M1 hook its board's (below #4000 only), its ports
/// on the claims, its SD sockets as media slots; the slot report names it real
TEST_F(DivMmcCard_Test, PluggedOn48K)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr) << "the edge slot builds the card";
    EXPECT_EQ(card->Firmware(), "unodos");
    const std::vector<uint8_t> rom = ReadFile(TestPathHelper::FindProjectRoot() / "testdata/storage/divmmc/unodos/unodos.rom");
    ASSERT_EQ(rom.size(), DivMmcPaging::kRomSize);
    EXPECT_TRUE(std::equal(rom.begin(), rom.end(), card->Paging().Rom())) << "rom/divmmc/unodos3.rom is UnoDOS 3";
    EXPECT_EQ(Cpu()->machineM1Hook, static_cast<IMachineM1Hook*>(&card->Paging()));
    EXPECT_EQ(Cpu()->machineM1HookEnd, DivMmcPaging::kHookEnd);
    EXPECT_TRUE(_context->pMediaManager->HasSlot("sd.divmmc0"));
    EXPECT_TRUE(_context->pMediaManager->HasSlot("sd.divmmc1"));
    EXPECT_FALSE(card->Paging().Mapped());
    const StateNode entry = SlotEntry();
    ASSERT_NE(entry.find("fit"), nullptr);
    EXPECT_EQ(entry.find("fit")->s, "real");
}

/// The card on a +3 (~10 ms): the +2A / +3 edge has no /ROMCS, the board's J3 jumper takes /ROM1OE + /ROM2OE: a real fit
TEST_F(DivMmcCard_Test, PluggedOnPlus3)
{
    Create("PLUS3", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(Cpu()->machineM1Hook, static_cast<IMachineM1Hook*>(&card->Paging()));
    const StateNode entry = SlotEntry();
    ASSERT_NE(entry.find("fit"), nullptr);
    EXPECT_EQ(entry.find("fit")->s, "real");
    EXPECT_TRUE(_context->pMediaManager->HasSlot("sd.divmmc0"));
}

/// No card, nothing of it (~5 ms): no hook, no SD slots
TEST_F(DivMmcCard_Test, AbsentLeavesTheMachineAlone)
{
    Create("48K", {});
    EXPECT_EQ(Card(), nullptr);
    EXPECT_EQ(DivMmcCard::Find(_context), nullptr);
    EXPECT_EQ(Cpu()->machineM1Hook, nullptr);
    EXPECT_EQ(Cpu()->machineM1HookEnd, 0x10000u);
    EXPECT_FALSE(_context->pMediaManager->HasSlot("sd.divmmc0"));
}

/// The ports through the claim table (~5 ms): low byte decoded; #E3 / #E7 write only (a read leaves the bus alone);
/// CONMEM puts the firmware and the selected bank at #0000-#3FFF
TEST_F(DivMmcCard_Test, PortsReachTheBoard)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    Out(0x12E3, 0x80 | 2);
    EXPECT_TRUE(card->Paging().Conmem());
    EXPECT_EQ(card->Paging().Bank(), 2);
    EXPECT_EQ(Cpu()->rd(0x0005), card->Paging().Rom()[5]) << "the firmware at #0000";
    Cpu()->wd(0x2100, 0x5A);
    EXPECT_EQ(card->Paging().RamBank(2)[0x100], 0x5A);
    bool drove = true;
    In(0x00E3, drove);
    EXPECT_FALSE(drove) << "#E3 is write only";
    In(0x00E7, drove);
    EXPECT_FALSE(drove) << "#E7 is write only";
    EXPECT_EQ(In(0x00EB, drove), 0xFF) << "no card selected";
    EXPECT_TRUE(drove);
    Out(0x34E7, 0xFE);
    EXPECT_EQ(card->Paging().SelectedCard(), 0);
    Out(0x00E3, 0x00);
    EXPECT_FALSE(card->Paging().Mapped());
    EXPECT_EQ(Cpu()->rd(0x0005), Peek(0x0005)) << "the Spectrum ROM is back";
}

/// /RESET and power (~20 ms): the machine's reset keeps #E3 and MAPRAM (the CPLD clears only the card selects and the
/// SPI engine), the reset vector's fetch at #0000 is an entry; a hard reset (power cycle) clears #E3 and MAPRAM
TEST_F(DivMmcCard_Test, ResetKeepsTheMapAndPowerCycleClearsIt)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    Out(0x00E3, 0x40 | 5);  // MAPRAM, bank 5
    Out(0x00E7, 0xFE);
    ASSERT_TRUE(card->Paging().Mapram());

    _emulator->Reset();
    EXPECT_TRUE(card->Paging().Mapram()) << "MAPRAM survives /RESET";
    EXPECT_EQ(card->Paging().Bank(), 5);
    EXPECT_EQ(card->Paging().SelectedCard(), -1) << "the card selects are reset";
    StepAt(Cpu()->pc);
    EXPECT_TRUE(card->Paging().MapCondition()) << "the reset vector #0000 is an entry";

    _emulator->Reset(true);
    EXPECT_FALSE(card->Paging().Mapram()) << "power on clears MAPRAM";
    EXPECT_EQ(card->Paging().Bank(), 0);
    EXPECT_FALSE(card->Paging().Mapped());
    EXPECT_FALSE(card->Paging().MapCondition());
}

/// A snapshot's program starts with the board out (~5 ms)
TEST_F(DivMmcCard_Test, SnapshotLoadUnmaps)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    Out(0x00E3, 0x80);
    StepAt(0x3D00);
    ASSERT_TRUE(card->Paging().Mapped());
    card->AfterSnapshotLoad();
    EXPECT_FALSE(card->Paging().Mapped());
    EXPECT_FALSE(card->Paging().Automapped());
    EXPECT_EQ(Cpu()->rd(0x0005), Peek(0x0005));
}

/// The automap on a +3 (~10 ms). The board as built traps in any ROM (the CPLD sees no ROM selection): #3Dxx maps it in
/// with the 128 editor's ROM 0 paged. The option `automap=rom3` (the Next's rule) takes the entries only with ROM 3
TEST_F(DivMmcCard_Test, Plus3AutomapInAnyRomByDefault)
{
    Create("PLUS3", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    PageRom(0);  // the +3 editor
    ASSERT_FALSE(_context->pMemory->IsCurrentROM48k());
    StepAt(0x3D00);
    EXPECT_TRUE(card->Paging().Automapped()) << "the board traps in ROM 0";
}

TEST_F(DivMmcCard_Test, Plus3Rom3AutomapOnlyWithRom3)
{
    Create("PLUS3", {{"edge.1", "divmmc"}, {"edge.1.automap", "rom3"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->Paging().GetAutomapMode(), DivMmcPaging::AutomapMode::Rom3);

    PageRom(0);  // the +3 editor
    ASSERT_FALSE(_context->pMemory->IsCurrentROM48k());
    StepAt(0x3D00);
    EXPECT_FALSE(card->Paging().Automapped()) << "ROM 0: no trap";
    StepAt(0x0038);
    EXPECT_FALSE(card->Paging().MapCondition()) << "ROM 0: no trap";

    PageRom(3);  // the 48K BASIC
    ASSERT_TRUE(_context->pMemory->IsCurrentROM48k());
    StepAt(0x0038);
    EXPECT_TRUE(card->Paging().MapCondition()) << "ROM 3: the entry counts";
    StepAt(0x3D00);
    EXPECT_TRUE(card->Paging().Automapped());
}

/// UnoDOS 3 boots through the card (~0.3 s, boot-bound): the folder in `sd.divmmc0` through the media manager, the
/// reset vector maps the firmware in, it initializes the SD card, loads /dos/unodos.sys into the board's RAM and leaves
/// the machine at the BASIC prompt
TEST_F(DivMmcCard_Test, UnoDosBootsThroughTheCard)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    MediaSource source;
    source.path = (TestPathHelper::FindProjectRoot() / "testdata/storage/divmmc/unodos/card").string();
    InsertOptions options;
    options.immediate = true;
    const MediaResult inserted = _context->pMediaManager->Insert("sd.divmmc0", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    _emulator->RunNFrames(1, true);  // the emulation thread attaches the card at the frame boundary
    ASSERT_TRUE(card->Paging().Card(0).present());
    _emulator->EnableTurboMode();

    _emulator->Reset();
    int frames = 0;
    EXPECT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 1500, &frames));
    EXPECT_EQ(card->Paging().RamBank(0)[0x0D42], 0xAA) << "UnoDOS initialized";
    EXPECT_GT(card->Paging().Card(0).blocksRead(), 0u);

    const StateNode entry = SlotEntry();
    const StateNode* live = entry.find("live");
    ASSERT_NE(live, nullptr) << "the slot report carries the card's state";
    EXPECT_EQ(live->find("firmware")->s, "unodos");
    const StateNode* cards = live->find("sd_cards");
    ASSERT_NE(cards, nullptr);
    ASSERT_EQ(cards->items.size(), 2u);
    EXPECT_TRUE(cards->items[0].find("present")->b);
    EXPECT_TRUE(cards->items[0].find("initialized")->b);
}

/// Time travel registration and the blob (~10 ms): one DivMmc device under the slot with its RAM as a region, the
/// fingerprint names the firmware; the blob brings the latches and the RAM back
TEST_F(DivMmcCard_Test, TimeTravelRegistersTheCard)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].instance, "edge.1.divmmc");
    EXPECT_EQ(devices[0].device->TTDType(), ttd::TTDDeviceType::DivMmc);
    EXPECT_EQ(devices[0].device->TTDStateSize(), DivMmcCard::kTtdStateSize);
    ASSERT_NE(devices[0].regions, nullptr);

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(_context, registry, owned, &error)) << error;
    EXPECT_TRUE(registry.IsRegistered(ttd::TTDDeviceType::DivMmc));
    std::vector<ttd::TTDDeviceRegion> regions;
    registry.CollectRegions(regions);
    const auto ram = std::find_if(regions.begin(), regions.end(),
                                  [](const ttd::TTDDeviceRegion& r) { return r.desc.id == ttd::TTDRegionId::DivMmcRam; });
    ASSERT_NE(ram, regions.end());
    EXPECT_EQ(ram->desc.bytes, DivMmcPaging::kRamSize);
    EXPECT_EQ(ram->desc.memory, card->Paging().RamData());

    std::vector<std::pair<std::string, uint64_t>> fingerprint;
    card->TtdFingerprint(fingerprint);
    ASSERT_EQ(fingerprint.size(), 1u);
    EXPECT_EQ(fingerprint[0].first, "slots.edge.1.rom");

    // The blob round trip
    Out(0x00E3, 0x80 | 0x40 | 9);
    Out(0x00E7, 0xFD);
    card->Paging().RamBank(9)[0x0123] = 0xA5;
    std::vector<uint8_t> saved(DivMmcCard::kTtdStateSize);
    devices[0].device->TTDSaveState(saved.data());
    const uint64_t hash = devices[0].device->TTDHashState();
    _emulator->Reset(true);
    card->Paging().RamBank(9)[0x0123] = 0;
    EXPECT_NE(devices[0].device->TTDHashState(), hash);
    devices[0].device->TTDLoadState(saved.data());
    EXPECT_EQ(devices[0].device->TTDHashState(), hash);
    EXPECT_TRUE(card->Paging().Conmem());
    EXPECT_TRUE(card->Paging().Mapram());
    EXPECT_EQ(card->Paging().Bank(), 9);
    EXPECT_EQ(card->Paging().SelectedCard(), 1);
    EXPECT_EQ(card->Paging().RamBank(9)[0x0123], 0xA5);
    EXPECT_EQ(Cpu()->rd(0x2123), 0xA5) << "mapped again";
}

/// The card in a recording (~0.2 s, 12 frames of a 48K): a program writes the board's RAM through CONMEM every frame; a
/// seek to any recorded frame shows the RAM and the latches as they were, and every frame replays exactly
TEST_F(DivMmcCard_Test, RecordedRamWritesReplayAndSeek)
{
    Create("48K", {{"edge.1", "divmmc"}});
    DivMmcCard* card = Card();
    ASSERT_NE(card, nullptr);
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);

    // DI; LD A,#83; OUT (#E3),A (CONMEM, bank 3); LD HL,#2000; loop: INC (HL); INC HL; LD A,H; AND #1F; OR #20;
    // LD H,A; JR loop
    const std::vector<uint8_t> program = {0xF3, 0x3E, 0x83, 0xD3, 0xE3, 0x21, 0x00, 0x20, 0x34, 0x23,
                                          0x7C, 0xE6, 0x1F, 0xF6, 0x20, 0x67, 0x18, 0xF6};
    for (size_t i = 0; i < program.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    Cpu()->pc = 0x8000;
    Cpu()->sp = 0xFF00;
    Cpu()->halted = false;
    _emulator->RunFrame(true);
    ASSERT_TRUE(card->Paging().Conmem());

    struct Seen
    {
        ttd::TTDTimePoint at;
        std::vector<uint8_t> blob;
    };
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    auto see = [&]() {
        Seen s;
        s.at = ttd->CurrentPosition();
        s.blob.resize(devices[0].device->TTDStateSize());
        devices[0].device->TTDSaveState(s.blob.data());
        return s;
    };

    ASSERT_TRUE(ttd->StartRecording());
    std::vector<Seen> seen;
    for (int f = 0; f < 12; f++)
    {
        _emulator->RunFrame(true);
        seen.push_back(see());
    }
    ttd->StopRecording();
    EXPECT_NE(seen.front().blob, seen.back().blob) << "the program wrote the RAM";

    const ttd::TTDReplayCheckResult check = ttd->QueryReplayCheck({});
    ASSERT_TRUE(check.error.empty()) << check.error;
    EXPECT_GE(check.checked, 10u);
    EXPECT_EQ(check.diverged, 0u) << ttd::TTDReplayCheckText(check);

    for (size_t i : {size_t(1), size_t(4), size_t(9), seen.size() - 1})
    {
        SCOPED_TRACE("frame " + std::to_string(i));
        ASSERT_TRUE(ttd->SeekTo(seen[i].at));
        EXPECT_EQ(see().blob, seen[i].blob) << "the card's blob and RAM";
    }
}
