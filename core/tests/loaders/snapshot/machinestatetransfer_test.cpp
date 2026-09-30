#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/machinestatetransfer.h"
#include "_helpers/testpathhelper.h"

using Status = MachineStateTransfer::ItemStatus;
using ttd::PeripheralId;

/// MachineStateTransfer: moving a running machine's state into another instance, in memory
class MachineStateTransfer_Test : public ::testing::Test
{
protected:
    std::vector<std::string> _created;

    void TearDown() override
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        for (const std::string& id : _created)
            manager->RemoveEmulator(id);
        _created.clear();
    }

    /// A machine of the given model; `config` adjusts it before its devices are built
    std::shared_ptr<Emulator> Create(const std::string& model, uint32_t ramKB = 0,
                                     std::function<void(CONFIG&)> config = {})
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        const TMemModel* info = Config::FindModelByShortName(model);
        EXPECT_NE(info, nullptr) << model;
        if (!info)
            return nullptr;
        std::string error;
        auto emulator = manager->CreateEmulatorWithModelAndRAM("transfer-test-" + model, model,
                                                               ramKB ? ramKB : info->defaultRAM, LoggerLevel::LogError,
                                                               &error, std::move(config));
        EXPECT_NE(emulator, nullptr) << model << ": " << error;
        if (emulator)
            _created.push_back(emulator->GetId());
        return emulator;
    }

    /// TSFM in the TurboSound slot, NeoGS, MoonSound: the cards the transfer must carry across machines
    static void FitCards(CONFIG& config)
    {
        config.sound.turboSoundKind = TurboSoundKind::FM;
        config.sound.gsTypeKind = GSTypeKind::NGS;
        config.sound.moonsound = 1;
    }

    static EmulatorContext& Ctx(const std::shared_ptr<Emulator>& e) { return *e->GetContext(); }

    static void FillPage(EmulatorContext& ctx, uint16_t page, uint8_t seed)
    {
        uint8_t* data = ctx.pMemory->RAMPageAddress(page);
        ASSERT_NE(data, nullptr);
        for (size_t i = 0; i < PAGE_SIZE; i++)
            data[i] = static_cast<uint8_t>(seed + i * 7);
    }

    static bool SamePage(EmulatorContext& a, EmulatorContext& b, uint16_t page)
    {
        const uint8_t* pa = a.pMemory->RAMPageAddress(page);
        const uint8_t* pb = b.pMemory->RAMPageAddress(page);
        return pa && pb && std::memcmp(pa, pb, PAGE_SIZE) == 0;
    }

    static void SetCpu(EmulatorContext& ctx)
    {
        Z80* z80 = ctx.pCore->GetZ80();
        z80->pc = 0x8123;
        z80->sp = 0xBEEF;
        z80->af = 0x1234;
        z80->bc = 0x5678;
        z80->hl = 0x9ABC;
        z80->ix = 0x4321;
        z80->memptr = 0x6789;
        z80->im = 2;
        z80->iff1 = 1;
        z80->iff2 = 1;
    }

    static void ExpectSameCpu(EmulatorContext& a, EmulatorContext& b)
    {
        const Z80* za = a.pCore->GetZ80();
        const Z80* zb = b.pCore->GetZ80();
        EXPECT_EQ(za->pc, zb->pc);
        EXPECT_EQ(za->sp, zb->sp);
        EXPECT_EQ(za->af, zb->af);
        EXPECT_EQ(za->bc, zb->bc);
        EXPECT_EQ(za->hl, zb->hl);
        EXPECT_EQ(za->ix, zb->ix);
        EXPECT_EQ(za->memptr, zb->memptr);
        EXPECT_EQ(za->im, zb->im);
        EXPECT_EQ(za->iff1, zb->iff1);
    }

    /// The device's state blob in each machine; empty when the machine lacks the device
    static std::vector<uint8_t> Blob(EmulatorContext& ctx, PeripheralId id)
    {
        ttd::TTDPeripheralRegistry registry;
        std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
        ttd::RegisterMachinePeripherals(&ctx, registry, owned);
        std::vector<uint8_t> blob;
        if (ttd::TTDSerializable* device = registry.GetDevice(id))
        {
            blob.resize(device->TTDStateSize());
            device->TTDSaveState(blob.data());
        }
        return blob;
    }

    static const MachineStateTransfer::Item* Find(const MachineStateTransfer::Report& report, const std::string& name)
    {
        for (const auto& item : report.items)
        {
            if (item.name == name)
                return &item;
        }
        return nullptr;
    }

    /// Non-trivial card state: pattern in NeoGS RAM and flash and in MoonSound wave SRAM, then a few frames
    static void MutateCards(const std::shared_ptr<Emulator>& e)
    {
        EmulatorContext& ctx = Ctx(e);
        auto* neogs = dynamic_cast<SoundChip_NeoGS*>(ctx.pSoundManager->getGeneralSound());
        ASSERT_NE(neogs, nullptr) << "NeoGS fitted";
        uint8_t* ram = neogs->memory().ram();
        for (size_t i = 0; i < 4096; i++)
            ram[neogs->memory().ramSize() - 4096 + i] = static_cast<uint8_t>(i * 13 + 5);
        neogs->flash().data()[Flash29F040B::SIZE - 1] = 0x5A;

        SoundChip_Moonsound* moon = ctx.pSoundManager->getMoonSound();
        ASSERT_NE(moon, nullptr) << "MoonSound fitted";
        opl4::WaveMemory& wave = moon->waveMemory();
        ASSERT_GT(wave.RamEnd(), wave.RomEnd()) << "MoonSound has wave SRAM";
        uint8_t pattern[256];
        for (int i = 0; i < 256; i++)
            pattern[i] = static_cast<uint8_t>(255 - i);
        wave.WriteSram(wave.RomEnd() + 1000, pattern, sizeof pattern);

        e->RunNFrames(3);
    }

    /// NeoGS blob bytes 147-154: _frameStartZxTacts and _frameTicks, the card's anchor to the ZX frame. After a
    /// transfer to a model with another frame length the target re-anchors the card to its own frame (the card's
    /// own time, CPU, devices and memory are unchanged), so these bytes legitimately differ
    static constexpr size_t NEOGS_ZX_FRAME_ANCHOR_BEGIN = 147;
    static constexpr size_t NEOGS_ZX_FRAME_ANCHOR_END = 155;

    static void ExpectCardsFollowed(EmulatorContext& src, EmulatorContext& dst)
    {
        const bool sameFrame = src.config.frame == dst.config.frame && src.config.intstart == dst.config.intstart &&
                               src.config.intlen == dst.config.intlen;
        for (PeripheralId id : {PeripheralId::TSFM, PeripheralId::NeoGS, PeripheralId::MoonSound})
        {
            const std::vector<uint8_t> a = Blob(src, id);
            const std::vector<uint8_t> b = Blob(dst, id);
            ASSERT_FALSE(a.empty()) << "source has device " << int(id);
            ASSERT_EQ(a.size(), b.size()) << "device " << int(id);
            size_t firstDiff = a.size();
            for (size_t i = 0; i < a.size(); i++)
            {
                const bool frameAnchor = id == PeripheralId::NeoGS && i >= NEOGS_ZX_FRAME_ANCHOR_BEGIN &&
                                         i < NEOGS_ZX_FRAME_ANCHOR_END;
                if (a[i] != b[i] && !(frameAnchor && !sameFrame))
                {
                    firstDiff = i;
                    break;
                }
            }
            EXPECT_EQ(firstDiff, a.size()) << "device " << int(id) << " state differs from offset " << firstDiff
                                           << " (of " << a.size() << " bytes)";
        }

        auto* ns = dynamic_cast<SoundChip_NeoGS*>(src.pSoundManager->getGeneralSound());
        auto* nd = dynamic_cast<SoundChip_NeoGS*>(dst.pSoundManager->getGeneralSound());
        ASSERT_TRUE(ns && nd);
        ASSERT_EQ(ns->memory().ramSize(), nd->memory().ramSize());
        EXPECT_EQ(std::memcmp(ns->memory().ram(), nd->memory().ram(), ns->memory().ramSize()), 0) << "NeoGS RAM";
        EXPECT_EQ(std::memcmp(ns->flash().data(), nd->flash().data(), Flash29F040B::SIZE), 0) << "NeoGS flash";
        EXPECT_FALSE(nd->flash().modified()) << "a transferred flash never marks the target's image for writing";

        const opl4::WaveMemory& ws = src.pSoundManager->getMoonSound()->waveMemory();
        const opl4::WaveMemory& wd = dst.pSoundManager->getMoonSound()->waveMemory();
        ASSERT_EQ(ws.RamEnd(), wd.RamEnd());
        EXPECT_EQ(std::memcmp(ws.Data() + ws.RomEnd(), wd.Data() + wd.RomEnd(), ws.RamEnd() - ws.RomEnd()), 0)
            << "MoonSound wave SRAM";
    }
};

/// Same model and RAM size: the full state, as a TTD restore, cards included
TEST_F(MachineStateTransfer_Test, cloneCopiesMachineAndCards)
{
    auto a = Create("PENTAGON", 128, FitCards);
    auto b = Create("PENTAGON", 128, FitCards);
    ASSERT_TRUE(a && b);

    MutateCards(a);
    FillPage(Ctx(a), 3, 0x11);
    FillPage(Ctx(a), 7, 0x77);
    Ctx(a).pPortDecoder->DecodePortOut(0x7FFD, 0x13, 0);
    SetCpu(Ctx(a));

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_TRUE(report.clone);

    for (uint16_t page = 0; page < 8; page++)
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
    EXPECT_EQ(Ctx(b).emulatorState.p7FFD, 0x13);
    ExpectSameCpu(Ctx(a), Ctx(b));
    ExpectCardsFollowed(Ctx(a), Ctx(b));
}

/// Another model of the 128K class: pages, #7FFD through the target's decoder, CPU, and the cards
TEST_F(MachineStateTransfer_Test, crossModel128KToPentagonCarriesCards)
{
    auto a = Create("128k", 0, FitCards);
    auto b = Create("PENTAGON", 128, FitCards);
    ASSERT_TRUE(a && b);

    MutateCards(a);
    FillPage(Ctx(a), 4, 0x44);
    Ctx(a).pPortDecoder->DecodePortOut(0x7FFD, 0x14, 0);
    SetCpu(Ctx(a));

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_FALSE(report.clone);

    for (uint16_t page = 0; page < 8; page++)
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
    EXPECT_EQ(Ctx(b).emulatorState.p7FFD, 0x14);
    ExpectSameCpu(Ctx(a), Ctx(b));
    ExpectCardsFollowed(Ctx(a), Ctx(b));
}

/// A 48K program on a 128K: pages 5, 2, 0, locked in 48K mode on the 48 BASIC ROM
TEST_F(MachineStateTransfer_Test, from48KLocksTheTargetIn48KMode)
{
    auto a = Create("48K");
    auto b = Create("128k");
    ASSERT_TRUE(a && b);

    for (uint16_t page : {uint16_t(5), uint16_t(2), uint16_t(0)})
        FillPage(Ctx(a), page, static_cast<uint8_t>(page * 16 + 1));
    SetCpu(Ctx(a));

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();

    for (uint16_t page : {uint16_t(5), uint16_t(2), uint16_t(0)})
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
    EXPECT_EQ(Ctx(b).emulatorState.p7FFD, 0x30) << "locked, 48 BASIC ROM, page 0 at #C000";
    ExpectSameCpu(Ctx(a), Ctx(b));
}

/// +2A/+3 all-RAM mode has no 128K equivalent: refused, and the target is untouched
TEST_F(MachineStateTransfer_Test, refusesPlus3AllRamModeOn128K)
{
    auto a = Create("PLUS3");
    auto b = Create("128k");
    ASSERT_TRUE(a && b);

    Ctx(a).pPortDecoder->DecodePortOut(0x1FFD, 0x01, 0);
    ASSERT_EQ(Ctx(a).emulatorState.p1FFD & 0x01, 0x01);
    FillPage(Ctx(b), 1, 0xAB);
    std::vector<uint8_t> before(Ctx(b).pMemory->RAMPageAddress(1), Ctx(b).pMemory->RAMPageAddress(1) + PAGE_SIZE);

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    EXPECT_FALSE(report.ok);
    const auto* item = Find(report, "memory map");
    ASSERT_NE(item, nullptr) << report.ToString();
    EXPECT_EQ(item->status, Status::Refused);
    EXPECT_EQ(std::memcmp(before.data(), Ctx(b).pMemory->RAMPageAddress(1), PAGE_SIZE), 0) << "target untouched";
}

/// Pentagon 1024 data above page 7 fits only a Pentagon with as much RAM
TEST_F(MachineStateTransfer_Test, pentagonExtendedMemoryNeedsTheSameFamily)
{
    auto a = Create("PENTAGON", 1024);
    auto to128 = Create("128k");
    auto to1024 = Create("PENTAGON", 1024);
    ASSERT_TRUE(a && to128 && to1024);

    FillPage(Ctx(a), 20, 0x20);

    EXPECT_FALSE(MachineStateTransfer::Transfer(*a, *to128).ok);
    const auto report = MachineStateTransfer::Transfer(*a, *to1024);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_TRUE(SamePage(Ctx(a), Ctx(to1024), 20));
}

/// A machine with its own memory map moves only to the same model
TEST_F(MachineStateTransfer_Test, refusesModelSpecificMachineOnAnotherModel)
{
    auto a = Create("ATM3");
    auto b = Create("128k");
    ASSERT_TRUE(a && b);

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(Find(report, "memory map"), nullptr) << report.ToString();
}

/// A card the target lacks is dropped (named in the report); the rest moves
TEST_F(MachineStateTransfer_Test, cardTheTargetLacksIsReportedDropped)
{
    auto a = Create("128k", 0, FitCards);
    auto b = Create("PENTAGON", 128);  // the test runner leaves the cards out
    ASSERT_TRUE(a && b);

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_GE(report.Count(Status::Dropped), 1u) << report.ToString();
}

/// A new instance gets the source's cards before its devices are built, so their state follows
TEST_F(MachineStateTransfer_Test, newInstanceGetsTheSourceCards)
{
    auto a = Create("128k", 0, FitCards);
    ASSERT_TRUE(a);
    MutateCards(a);
    FillPage(Ctx(a), 6, 0x66);

    MachineStateTransfer::Report report;
    auto b = MachineStateTransfer::TransferToNewInstance(*a, "PENTAGON", 128, report);
    ASSERT_NE(b, nullptr) << report.ToString();
    _created.push_back(b->GetId());

    EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), 6));
    ExpectCardsFollowed(Ctx(a), Ctx(b));
}

/// Check changes nothing
TEST_F(MachineStateTransfer_Test, checkChangesNothing)
{
    auto a = Create("128k");
    auto b = Create("PENTAGON", 128);
    ASSERT_TRUE(a && b);

    FillPage(Ctx(a), 3, 0x33);
    FillPage(Ctx(b), 3, 0x99);
    const auto report = MachineStateTransfer::Check(Ctx(a), Ctx(b));
    EXPECT_TRUE(report.ok) << report.ToString();
    EXPECT_FALSE(SamePage(Ctx(a), Ctx(b), 3));
}

/// The strongest check of completeness: a clone runs on exactly as the source does. Any state the transfer
/// missed makes the two machines diverge within a few frames
TEST_F(MachineStateTransfer_Test, cloneContinuesIdenticallyToTheSource)
{
    auto a = Create("PENTAGON", 128);
    auto b = Create("PENTAGON", 128);
    ASSERT_TRUE(a && b);

    ASSERT_TRUE(a->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80")));
    a->RunNFrames(10);

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();

    a->RunNFrames(50);
    b->RunNFrames(50);

    ExpectSameCpu(Ctx(a), Ctx(b));
    EXPECT_EQ(Ctx(a).pCore->GetZ80()->t, Ctx(b).pCore->GetZ80()->t) << "in-frame position";
    for (uint16_t page = 0; page < 8; page++)
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page << " after 50 frames";
}

/// The same with the cards running: NeoGS executes its own Z80 from the transferred RAM and flash, MoonSound
/// plays from the transferred wave SRAM, TSFM runs its FM cores. After 20 frames every card's full state must
/// still match. Slower than 50 ms: three sound cards are emulated in both machines (justified by the purpose)
TEST_F(MachineStateTransfer_Test, cloneWithCardsContinuesIdentically)
{
    auto a = Create("PENTAGON", 128, FitCards);
    auto b = Create("PENTAGON", 128, FitCards);
    ASSERT_TRUE(a && b);

    MutateCards(a);
    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();

    a->RunNFrames(20);
    b->RunNFrames(20);

    ExpectSameCpu(Ctx(a), Ctx(b));
    for (uint16_t page = 0; page < 8; page++)
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
    ExpectCardsFollowed(Ctx(a), Ctx(b));
}
