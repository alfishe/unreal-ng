#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/filehelper.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapeslot.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/machinestatetransfer.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/trdostesthelper.h"

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

/// A ZX-Poly module runs in lockstep with three others: neither a source nor a target, and a ZX-Poly
/// configuration name is not a new-instance target
TEST_F(MachineStateTransfer_Test, refusesZXPolyModules)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> master = manager->CreateZXPolyMachine("transfer-test-zxpoly", "48K", "", &error);
    ASSERT_TRUE(master) << error;
    _created.push_back(master->GetId());
    auto plain = Create("48K");
    ASSERT_TRUE(plain);

    for (const auto& report : {MachineStateTransfer::Transfer(*master, *plain),
                               MachineStateTransfer::Transfer(*plain, *master)})
    {
        EXPECT_FALSE(report.ok);
        EXPECT_NE(report.reason.find("ZX-Poly"), std::string::npos) << report.reason;
    }

    MachineStateTransfer::Report report;
    EXPECT_EQ(MachineStateTransfer::TransferToNewInstance(*plain, "ZXPOLY-48K", 0, report), nullptr);
    EXPECT_NE(report.reason.find("ZX-Poly"), std::string::npos) << report.reason;
}


// --- Floppies and tape ------------------------------------------------------------------------------------------

namespace
{
    Medium* MediumIn(EmulatorContext& ctx, const std::string& slot)
    {
        return ctx.pMediaManager ? ctx.pMediaManager->GetMedium(slot) : nullptr;
    }

    /// Every track's stream of two disks is the same
    bool SameDisk(const DiskImage& a, const DiskImage& b)
    {
        DiskImage& ma = const_cast<DiskImage&>(a);
        DiskImage& mb = const_cast<DiskImage&>(b);
        if (ma.getCylinders() != mb.getCylinders() || ma.getSides() != mb.getSides())
            return false;
        for (size_t t = 0; t < static_cast<size_t>(ma.getCylinders()) * ma.getSides(); t++)
        {
            DiskImage::Track* ta = ma.getTrack(static_cast<uint8_t>(t));
            DiskImage::Track* tb = mb.getTrack(static_cast<uint8_t>(t));
            if (!ta || !tb || ta->rawSize() != tb->rawSize() || std::memcmp(ta->rawData(), tb->rawData(), ta->rawSize()) != 0)
                return false;
        }
        return true;
    }

    /// The controller's status register, from its state blob (byte 4)
    uint8_t FdcStatus(EmulatorContext& ctx)
    {
        if (!ctx.pBetaDisk)
            return 0;
        std::vector<uint8_t> blob(ctx.pBetaDisk->TTDStateSize());
        ctx.pBetaDisk->TTDSaveState(blob.data());
        return blob[4];
    }

    bool FdcBusy(EmulatorContext& ctx)
    {
        return (FdcStatus(ctx) & 0x01) != 0;
    }
}

/// A floppy follows as the target's own in-memory copy: same contents (the source's unsaved writes included),
/// clean, standing for a postfixed file so a save on the target never touches the source's image
TEST_F(MachineStateTransfer_Test, floppyFollowsAsCleanCopyWithItsOwnPath)
{
    auto a = Create("PENTAGON");
    auto b = Create("PENTAGON");
    ASSERT_TRUE(a && b);
    const std::string image = TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd");
    ASSERT_TRUE(a->LoadDisk(image, 0));

    const std::string slot = FloppyDriveSlot::IdFor(0);
    Medium* source = MediumIn(Ctx(a), slot);
    ASSERT_NE(source, nullptr);
    // An unsaved guest write on the source disk
    DiskImage::Track* track = source->Floppy()->getTrack(0);
    ASSERT_NE(track, nullptr);
    std::vector<uint8_t> sector(256, 0xA5);
    track->writeSectorData(8, sector.data(), sector.size());
    ASSERT_TRUE(source->IsDirty());

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_NE(Find(report, slot), nullptr) << report.ToString();
    const auto* storage = Find(report, "SD / HDD / CD");
    ASSERT_NE(storage, nullptr) << "the report must say SD / HDD / CD are not moved";
    EXPECT_NE(storage->detail.find("not moved"), std::string::npos);

    Medium* copy = MediumIn(Ctx(b), slot);
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->Floppy(), source->Floppy());
    EXPECT_TRUE(SameDisk(*source->Floppy(), *copy->Floppy()));
    EXPECT_FALSE(copy->IsDirty());
    EXPECT_EQ(copy->Access(), AccessMode::Session);
    EXPECT_NE(copy->Source().path, image);
    EXPECT_NE(copy->Source().path.find("zx-format8.pentagon-"), std::string::npos) << copy->Source().path;
    EXPECT_EQ(copy->Source().path.substr(copy->Source().path.size() - 4), ".trd");

    // The source is untouched: still dirty, still its own file; nothing was written to disk
    EXPECT_TRUE(source->IsDirty());
    EXPECT_EQ(source->Source().path, image);
    EXPECT_FALSE(FileHelper::FileExists(copy->Source().path));
}

/// A drive the source has empty is empty on the target too; a target disk with unsaved writes refuses the
/// transfer and nothing changes
TEST_F(MachineStateTransfer_Test, targetDiskWithUnsavedWritesRefuses)
{
    auto a = Create("PENTAGON");
    auto b = Create("PENTAGON");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(b->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
    const std::string slot = FloppyDriveSlot::IdFor(0);

    Medium* disk = MediumIn(Ctx(b), slot);
    ASSERT_NE(disk, nullptr);
    std::vector<uint8_t> sector(256, 0x5A);
    disk->Floppy()->getTrack(0)->writeSectorData(9, sector.data(), sector.size());

    const auto refused = MachineStateTransfer::Transfer(*a, *b);
    EXPECT_FALSE(refused.ok);
    EXPECT_NE(refused.reason.find("unsaved"), std::string::npos) << refused.reason;
    EXPECT_EQ(MediumIn(Ctx(b), slot), disk) << "a refused transfer must not touch the target's disk";

    // Clean: the empty source drive empties the target's
    ASSERT_TRUE(Ctx(b).pMediaManager->Discard(slot).Ok());
    Ctx(b).pMediaManager->WaitApplied(slot, 1000);
    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_EQ(MediumIn(Ctx(b), slot), nullptr);
}

/// Cloned in the middle of a TR-DOS disk read: the target has the disk, the controller mid-command and the disk's
/// rotation phase, so both machines finish the load identically. Slow (~0.3 s): boots TR-DOS and loads from disk
TEST_F(MachineStateTransfer_Test, cloneMidDiskReadContinuesIdentically)
{
    auto a = Create("PENTAGON");
    auto b = Create("PENTAGON");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(a->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/atarin.trd"), 0));
    {
        TRDOSTestHelper trdos(a.get());
        trdos.startCommand("RUN");
        ASSERT_TRUE(trdos.runUntil([&] { return FdcBusy(Ctx(a)); }, 70'000'000ULL, 1))
            << "the boot never reached a disk read";
    }
    b->EnableTurboMode();  // same as the helper did to the source: sound off
    a->EnableTurboMode();

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    EXPECT_EQ(Blob(Ctx(a), PeripheralId::BetaDisk), Blob(Ctx(b), PeripheralId::BetaDisk));

    a->RunNFrames(60);
    b->RunNFrames(60);
    ExpectSameCpu(Ctx(a), Ctx(b));
    for (uint16_t page = 0; page < 8; page++)
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
    EXPECT_EQ(Blob(Ctx(a), PeripheralId::BetaDisk), Blob(Ctx(b), PeripheralId::BetaDisk));
}

/// Pentagon -> Scorpion in the middle of a disk read: the Beta 128 state and its disk follow onto the other model,
/// on the source's time axis (the rotation phase holds), and the target finishes the read without an error
TEST_F(MachineStateTransfer_Test, crossModelMidDiskReadKeepsTheController)
{
    auto a = Create("PENTAGON");
    auto b = Create("SCORPION");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(a->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/atarin.trd"), 0));
    {
        TRDOSTestHelper trdos(a.get());
        trdos.startCommand("RUN");
        ASSERT_TRUE(trdos.runUntil([&] { return FdcBusy(Ctx(a)); }, 70'000'000ULL, 1));
    }

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    ASSERT_NE(MediumIn(Ctx(b), FloppyDriveSlot::IdFor(0)), nullptr);
    const auto* fdc = Find(report, "WD1793");
    ASSERT_NE(fdc, nullptr) << report.ToString();
    EXPECT_EQ(fdc->status, Status::Copied) << report.ToString();

    // Registers and FSM (the first 40 bytes of the blob: registers, drive, command, state, delay, time stamps)
    const std::vector<uint8_t> fa = Blob(Ctx(a), PeripheralId::BetaDisk);
    const std::vector<uint8_t> fb = Blob(Ctx(b), PeripheralId::BetaDisk);
    ASSERT_EQ(fa.size(), fb.size());
    EXPECT_TRUE(std::equal(fa.begin(), fa.begin() + 40, fb.begin()));
    EXPECT_EQ(Ctx(a).emulatorState.t_states + Ctx(a).pCore->GetZ80()->t,
              Ctx(b).emulatorState.t_states + Ctx(b).pCore->GetZ80()->t)
        << "the target must take the source's time axis";

    // The read completes on the target without Record Not Found / CRC error
    for (int frame = 0; frame < 200 && FdcBusy(Ctx(b)); frame++)
        b->RunNFrames(1);
    EXPECT_FALSE(FdcBusy(Ctx(b)));
    EXPECT_EQ(FdcStatus(Ctx(b)) & 0x18, 0) << "status after the read";
}

/// A tape playing mid-block follows with its position: both machines read the same bits afterwards.
/// Slower than 50 ms (~90 ms): the tape has to play for a while in both machines
TEST_F(MachineStateTransfer_Test, cloneMidTapeContinuesIdentically)
{
    auto a = Create("48K");
    auto b = Create("48K");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(a->LoadTape(TestPathHelper::GetTestDataPath("loaders/tap/action.tap")));
    Ctx(a).pTape->startTape();
    a->RunNFrames(30);
    ASSERT_TRUE(Ctx(a).pTape->IsPlaying());

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    ASSERT_NE(MediumIn(Ctx(b), TapeSlot::kId), nullptr) << report.ToString();
    EXPECT_EQ(Blob(Ctx(a), PeripheralId::Tape), Blob(Ctx(b), PeripheralId::Tape));
    a->RunNFrames(20);
    b->RunNFrames(20);
    EXPECT_EQ(Blob(Ctx(a), PeripheralId::Tape), Blob(Ctx(b), PeripheralId::Tape));
    ExpectSameCpu(Ctx(a), Ctx(b));
    for (uint16_t page : {uint16_t(0), uint16_t(2), uint16_t(5)})
        EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
}

/// 48K -> Pentagon with the tape playing: the deck state follows with the tape onto the other model
TEST_F(MachineStateTransfer_Test, crossModelTapeFollows)
{
    auto a = Create("48K");
    auto b = Create("PENTAGON");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(a->LoadTape(TestPathHelper::GetTestDataPath("loaders/tap/action.tap")));
    Ctx(a).pTape->startTape();
    a->RunNFrames(10);

    const auto report = MachineStateTransfer::Transfer(*a, *b);
    ASSERT_TRUE(report.ok) << report.ToString();
    ASSERT_NE(MediumIn(Ctx(b), TapeSlot::kId), nullptr) << report.ToString();
    EXPECT_TRUE(Ctx(b).pTape->IsPlaying());
    EXPECT_EQ(Blob(Ctx(a), PeripheralId::Tape), Blob(Ctx(b), PeripheralId::Tape));
}

// --- Invariants over every model --------------------------------------------------------------------------------
// Scenario tests pick a model or two; these sweep every creatable one, so a model whose memory map differs
// (a 48K maps banks 5, 2, 0 although its ramsize says three pages) cannot slip through.

namespace
{
    std::vector<std::string> CreatableModels()
    {
        std::vector<std::string> names;
        for (const TMemModel& model : Config::GetAvailableModels())
        {
            if (Config::IsModelCreatable(model))
                names.emplace_back(model.ShortName);
        }
        return names;
    }

    /// Every page a model can map (never fewer than the eight 128K banks)
    uint16_t MappablePages(EmulatorContext& ctx)
    {
        return std::max<uint16_t>(static_cast<uint16_t>(std::min<uint32_t>(ctx.config.ramsize / 16, MAX_RAM_PAGES)), 8);
    }

    /// A distinct pattern per page and seed: a page copied to the wrong place, or left out, shows
    void FillAllPages(EmulatorContext& ctx, uint8_t seed)
    {
        for (uint16_t page = 0; page < MappablePages(ctx); page++)
        {
            uint8_t* data = ctx.pMemory->RAMPageAddress(page);
            if (!data)
                continue;
            for (size_t i = 0; i < PAGE_SIZE; i++)
                data[i] = static_cast<uint8_t>(seed * 31 + page * 7 + i * 13 + (i >> 8));
        }
    }

    /// First address in [from, 0xFFFF] where the two CPUs see different bytes, or -1
    int FirstZ80ViewDifference(EmulatorContext& a, EmulatorContext& b, uint32_t from)
    {
        for (uint32_t address = from; address <= 0xFFFF; address++)
        {
            if (a.pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address)) !=
                b.pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address)))
                return static_cast<int>(address);
        }
        return -1;
    }
}

/// Every creatable model clones into itself: every mappable page, the whole Z80 view (ROM included), the CPU,
/// and both machines stay identical while they run. Slow (~0.5 s): two instances of every model
TEST_F(MachineStateTransfer_Test, everyModelClonesEveryPageAndContinuesIdentically)
{
    const std::vector<std::string> models = CreatableModels();
    ASSERT_GE(models.size(), 8u) << "the creatable model list shrank";

    for (const std::string& model : models)
    {
        SCOPED_TRACE(model);
        auto a = Create(model);
        auto b = Create(model);
        ASSERT_TRUE(a && b);
        FillAllPages(Ctx(a), 1);
        FillAllPages(Ctx(b), 2);  // a stale target page must not pass for a copied one
        SetCpu(Ctx(a));

        const auto report = MachineStateTransfer::Transfer(*a, *b);
        ASSERT_TRUE(report.ok) << report.ToString();
        EXPECT_TRUE(report.clone);

        for (uint16_t page = 0; page < MappablePages(Ctx(a)); page++)
            EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page;
        EXPECT_EQ(FirstZ80ViewDifference(Ctx(a), Ctx(b), 0x0000), -1);
        ExpectSameCpu(Ctx(a), Ctx(b));

        a->RunNFrames(3);
        b->RunNFrames(3);
        ExpectSameCpu(Ctx(a), Ctx(b));
        EXPECT_EQ(FirstZ80ViewDifference(Ctx(a), Ctx(b), 0x0000), -1) << "after 3 frames";
        for (uint16_t page = 0; page < MappablePages(Ctx(a)); page++)
            EXPECT_TRUE(SamePage(Ctx(a), Ctx(b), page)) << "page " << page << " after 3 frames";

        EmulatorManager::GetInstance()->RemoveEmulator(a->GetId());
        EmulatorManager::GetInstance()->RemoveEmulator(b->GetId());
    }
}

/// Every pair of creatable models: when the target accepts the state, the program sees exactly the RAM it saw on
/// the source (#4000-#FFFF; ROM sets differ by model) and the same CPU. Refusals must say why. Slow (~0.5 s):
/// one instance per model, every ordered pair transferred
TEST_F(MachineStateTransfer_Test, everyModelPairKeepsTheProgramsView)
{
    const std::vector<std::string> models = CreatableModels();
    std::vector<std::shared_ptr<Emulator>> machines;
    for (const std::string& model : models)
    {
        machines.push_back(Create(model));
        ASSERT_TRUE(machines.back()) << model;
    }

    size_t accepted = 0;
    uint8_t seed = 3;
    for (size_t s = 0; s < machines.size(); s++)
    {
        for (size_t t = 0; t < machines.size(); t++)
        {
            if (s == t)
                continue;
            SCOPED_TRACE(models[s] + " -> " + models[t]);
            EmulatorContext& source = Ctx(machines[s]);
            EmulatorContext& target = Ctx(machines[t]);
            FillAllPages(source, seed++);
            FillAllPages(target, seed++);
            SetCpu(source);

            const auto report = MachineStateTransfer::Transfer(*machines[s], *machines[t]);
            if (!report.ok)
            {
                EXPECT_FALSE(report.reason.empty()) << "a refusal must say why";
                continue;
            }
            accepted++;
            EXPECT_EQ(FirstZ80ViewDifference(source, target, 0x4000), -1) << report.ToString();
            ExpectSameCpu(source, target);
        }
    }
    // 48K / 128K / +2 / Pentagon / Scorpion states move between each other at least
    EXPECT_GE(accepted, models.size()) << "almost every pair refused";
}
