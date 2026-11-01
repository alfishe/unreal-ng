/// @file ttdengineregions_test.cpp
/// @brief Device memory as engine regions (Phase 1, Step 6): the engine
/// records a card's memory as changed 4 KB pieces and restores it exactly
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.7).
///
/// The test keeps copies of the card RAM at chosen frames while recording and
/// checks that the engine brings each back. Boots a Pentagon with its NeoGS
/// card and runs a few hundred frames (the card's own Z80 writes its RAM).

#include <gtest/gtest.h>

#include <cstring>
#include <cstddef>
#include <map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

class TTDEngineRegions_Test : public ::testing::Test
{
protected:
    SoundCardScope _cards{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    SoundChip_NeoGS* _ngs = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::NGS));
        _ngs = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
        ASSERT_NE(_ngs, nullptr);
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    ttd::TimeTravelEngine& Engine() const { return _ttd->GetEngine(); }

    int RegionIndex(const char* name) const
    {
        for (size_t i = 0; i < Engine().Regions().size(); ++i)
            if (Engine().Regions()[i].name == name)
                return static_cast<int>(i);
        return -1;
    }
};

TEST_F(TTDEngineRegions_Test, NeoGSRamIsRecordedAndRestoredExactly)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Copies of the card RAM at chosen frames; a debugger edit in between
    const size_t ramSize = _ngs->memory().ramSize();
    std::map<size_t, std::vector<uint8_t>> copies;   // checkpoint index -> card RAM
    for (int f = 0; f < 120; ++f)
    {
        if (f == 40)
            for (uint16_t a = 0; a < 64; ++a)
                _ngs->memory().poke(static_cast<uint16_t>(0x8000 + a * 97), static_cast<uint8_t>(a ^ 0x5A));
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        if (f == 10 || f == 39 || f == 41 || f == 119)
            copies[Engine().CheckpointCount() - 1] =
                std::vector<uint8_t>(_ngs->memory().ram(), _ngs->memory().ram() + ramSize);
    }
    _ttd->StopRecording();

    const int ram = RegionIndex("neogs.ram");
    const int flash = RegionIndex("neogs.flash");
    ASSERT_GT(ram, 0) << "the card RAM is a region of the session";
    ASSERT_GT(flash, 0) << "the card flash is a region of the session";
    ASSERT_EQ(Engine().Regions()[ram].bytes, ramSize);

    // Scramble the live card RAM, then bring each recorded frame back
    std::memset(_ngs->memory().ram(), 0xA5, ramSize);
    Engine().ForgetMemory();
    for (const auto& [index, want] : copies)
    {
        ASSERT_TRUE(Engine().RestoreToMemory(index).Ok());
        const uint8_t* live = _ngs->memory().ram();
        if (std::memcmp(live, want.data(), ramSize) != 0)
        {
            size_t at = 0;
            while (live[at] == want[at])
                ++at;
            FAIL() << "card RAM differs after restoring checkpoint " << index << " at byte " << at;
        }
    }
    EXPECT_NE(copies.begin()->second, copies.rbegin()->second) << "the card changed its RAM while recording";
}

TEST_F(TTDEngineRegions_Test, IdleCardMemoryCostsNothing)
{
    // After the first frame's baseline, frames in which the card writes nothing add no versions to its regions
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(30, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    const int flash = RegionIndex("neogs.flash");
    ASSERT_GT(flash, 0);
    for (size_t i = 1; i < Engine().CheckpointCount(); ++i)
        EXPECT_EQ(Engine().ChangeCount(i, static_cast<uint32_t>(flash)), 0u)
            << "nothing programs the flash, checkpoint " << i;
}

TEST(Flash29F040BTracker_Test, ProgramEraseAndLoadAreMarked)
{
    Flash29F040B flash(1000000);
    ttd::TTDRegionTracker tracker;
    tracker.Bind(flash.data(), Flash29F040B::SIZE);
    std::vector<uint32_t> written;

    flash.markWritten(5000);
    tracker.CollectAndClear(written);
    EXPECT_TRUE(written.empty()) << "no tracker set: nothing marked";

    flash.setTracker(&tracker);
    flash.markWritten(5000);
    tracker.CollectAndClear(written);
    EXPECT_EQ(written, (std::vector<uint32_t>{1}));

    written.clear();
    std::vector<uint8_t> image(1000, 0x12);
    flash.load(image.data(), image.size());
    tracker.CollectAndClear(written);
    EXPECT_EQ(written.size(), Flash29F040B::SIZE / ttd::kTTDPieceSize) << "a load replaces the whole array";
}

TEST(TTDEngineMoonSoundRegions_Test, WaveRamIsRecordedAndRestoredExactly)
{
    SoundCardScope cards{TestSound::MoonSound};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    SoundChip_Moonsound* moon = context->pSoundManager->getMoonSound();
    ASSERT_NE(moon, nullptr) << "the Pentagon config fits a MoonSound";
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    ttd::TimeTravelEngine& engine = controller->GetEngine();
    ASSERT_TRUE(controller->StartRecording());
    opl4::WaveMemory& wave = moon->waveMemory();
    const uint32_t ramStart = wave.RomEnd();
    const uint32_t ramBytes = wave.RamEnd() - ramStart;
    ASSERT_GT(ramBytes, 0u);

    std::map<size_t, std::vector<uint8_t>> copies;
    for (int f = 0; f < 60; ++f)
    {
        if (f == 10 || f == 30)   // a sample upload: the wave memory's own write path
            for (uint32_t i = 0; i < 9000; ++i)
                wave.Write(ramStart + (uint32_t(f) * 4099 + i * 13) % ramBytes, static_cast<uint8_t>(i * 7 + f));
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        if (f == 5 || f == 10 || f == 29 || f == 59)
            copies[engine.CheckpointCount() - 1] = std::vector<uint8_t>(wave.RomData() + ramStart, wave.RomData() + ramStart + ramBytes);
    }
    controller->StopRecording();

    int region = -1;
    for (size_t i = 0; i < engine.Regions().size(); ++i)
        if (engine.Regions()[i].name == "moonsound.wave")
            region = static_cast<int>(i);
    ASSERT_GT(region, 0);

    std::memset(wave.RomData() + ramStart, 0x3C, ramBytes);
    engine.ForgetMemory();
    for (const auto& [index, want] : copies)
    {
        ASSERT_TRUE(engine.RestoreToMemory(index).Ok());
        ASSERT_EQ(std::memcmp(wave.RomData() + ramStart, want.data(), ramBytes), 0)
            << "wave RAM differs after restoring checkpoint " << index;
    }
    EXPECT_NE(copies.begin()->second, copies.rbegin()->second);

    // Frames without an upload add nothing to the wave RAM region
    size_t quiet = 0;
    for (size_t i = 1; i < engine.CheckpointCount(); ++i)
        quiet += engine.ChangeCount(i, static_cast<uint32_t>(region)) == 0 ? 1 : 0;
    EXPECT_GE(quiet, engine.CheckpointCount() - 3);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(TTDEngineGeneralSoundRegions_Test, CardRamIsARegion_RegistersStayInTheBlob)
{
    SoundCardScope cards{TestSound::GeneralSound};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    const ttd::TimeTravelEngine& engine = controller->GetEngine();

    // The card's whole state (its registers, then its RAM) after every frame
    constexpr uint8_t gsId = static_cast<uint8_t>(ttd::PeripheralId::GeneralSound);
    ttd::TTDSerializable* card = nullptr;
    for (const ttd::TTDRegistryEntry& e : controller->GetPeripheralRegistry().Entries())
        if (static_cast<uint8_t>(e.legacyId) == gsId)
            card = e.device;
    ASSERT_NE(card, nullptr);
    std::map<size_t, std::vector<uint8_t>> live;   // checkpoint index -> the card's state
    ASSERT_TRUE(controller->StartRecording());
    for (int f = 0; f < 80; ++f)
    {
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        card->TTDSaveStateTo(live[engine.CheckpointCount() - 1]);
    }
    controller->StopRecording();

    int region = -1;
    for (size_t i = 0; i < engine.Regions().size(); ++i)
        if (engine.Regions()[i].name == "gs.ram")
            region = static_cast<int>(i);
    ASSERT_GT(region, 0);
    const size_t ramBytes = engine.Regions()[region].bytes;

    std::vector<uint8_t> engineRam;
    size_t engineBlobBytes = 0, liveBytes = 0;
    for (const auto& [i, whole] : live)
    {
        ASSERT_GT(whole.size(), ramBytes);
        const std::vector<uint8_t> fixed(whole.begin(), whole.end() - static_cast<std::ptrdiff_t>(ramBytes));
        std::vector<uint8_t> state;
        ASSERT_TRUE(engine.DeviceStateOfV1Id(i, gsId, state)) << "checkpoint " << i;
        EXPECT_EQ(state, fixed) << "registers, checkpoint " << i;
        engineRam.assign(size_t(engine.Regions()[region].pieces) * ttd::kTTDPieceSize, 0);
        ASSERT_TRUE(engine.RestoreRegion(i, static_cast<uint32_t>(region), engineRam.data()).Ok());
        ASSERT_EQ(std::memcmp(engineRam.data(), whole.data() + fixed.size(), ramBytes), 0) << "card RAM, checkpoint " << i;
        engineBlobBytes += state.size();
        liveBytes += whole.size();
    }
    EXPECT_LT(engineBlobBytes * 4, liveBytes) << "the engine's blob carries the registers, not the RAM";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The GS ROM is configuration, not recorded: its fingerprint is the card's
/// descriptor's. Restoring a checkpoint after another ROM was loaded restores
/// the card exactly and reports FirmwareDiffers for it (a replay from there may
/// differ); with the recorded ROM back the restore is Exact
TEST(TTDEngineGeneralSoundRegions_Test, AnotherRomIsReportedAsFirmwareDiffers)
{
    SoundCardScope cards{TestSound::GeneralSound};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    GeneralSoundCard* gs = context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr);
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    ttd::TimeTravelEngine& engine = controller->GetEngine();

    // Record on gs104 (whatever the config fits, the test picks its two ROMs)
    gs->loadROM("rom/gs104.rom");
    ASSERT_TRUE(gs->isROMLoaded());
    const uint64_t recorded = gs->TTDDescribe().firmwareFingerprint;
    ASSERT_TRUE(controller->StartRecording());
    emulator->RunNFrames(20, /*skipBreakpoints=*/true);
    controller->StopRecording();
    ASSERT_GT(engine.CheckpointCount(), 10u);

    gs->loadROM("rom/gs105a.rom");
    ASSERT_TRUE(gs->isROMLoaded());
    ASSERT_NE(gs->TTDDescribe().firmwareFingerprint, recorded);
    ttd::TTDRestoreResult r = engine.RestoreDevices(10, {});
    EXPECT_EQ(r.status, ttd::TTDRestoreStatus::NotBitExact) << r.message;
    ASSERT_EQ(r.issues.size(), 1u) << r.message;
    EXPECT_EQ(r.issues[0].kind, ttd::TTDRestoreIssueKind::FirmwareDiffers);
    EXPECT_EQ(r.issues[0].device.type, ttd::TTDDeviceType::GeneralSound);

    gs->loadROM("rom/gs104.rom");
    r = engine.RestoreDevices(10, {});
    EXPECT_EQ(r.status, ttd::TTDRestoreStatus::Exact) << r.message;

    EmulatorTestHelper::CleanupEmulator(emulator);
}
