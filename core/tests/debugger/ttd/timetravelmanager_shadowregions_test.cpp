/// @file timetravelmanager_shadowregions_test.cpp
/// @brief Device memory as engine regions (Phase 1, Step 6): the shadow engine
/// records a card's memory as changed 4 KB pieces and restores it exactly
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.7).
///
/// v1 does not record NeoGS memory, so there is nothing to compare with: the
/// test keeps copies of the card RAM at chosen frames while recording and
/// checks that the engine brings each back. Boots a Pentagon with its NeoGS
/// card and runs a few hundred frames (the card's own Z80 writes its RAM).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

class TimeTravelManager_ShadowRegions_Test : public ::testing::Test
{
protected:
    SoundCardScope _cards{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    SoundChip_NeoGS* _ngs = nullptr;
    ttd::TimeTravelEngine _engine;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::NGS));
        _ngs = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
        ASSERT_NE(_ngs, nullptr);
        _v1 = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_v1)
            _v1->SetShadowEngine(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    int RegionIndex(const char* name) const
    {
        for (size_t i = 0; i < _engine.Regions().size(); ++i)
            if (_engine.Regions()[i].name == name)
                return static_cast<int>(i);
        return -1;
    }
};

TEST_F(TimeTravelManager_ShadowRegions_Test, NeoGSRamIsRecordedAndRestoredExactly)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());

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
            copies[_v1->GetCheckpointCount() - 1] =
                std::vector<uint8_t>(_ngs->memory().ram(), _ngs->memory().ram() + ramSize);
    }
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    const int ram = RegionIndex("neogs.ram");
    const int flash = RegionIndex("neogs.flash");
    ASSERT_GT(ram, 0) << "the card RAM is a region of the session";
    ASSERT_GT(flash, 0) << "the card flash is a region of the session";
    ASSERT_EQ(_engine.Regions()[ram].bytes, ramSize);
    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());

    // Scramble the live card RAM, then bring each recorded frame back
    std::memset(_ngs->memory().ram(), 0xA5, ramSize);
    _engine.ForgetMemory();
    for (const auto& [index, want] : copies)
    {
        ASSERT_TRUE(_engine.RestoreToMemory(index).Ok());
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

TEST_F(TimeTravelManager_ShadowRegions_Test, IdleCardMemoryCostsNothing)
{
    // After the first frame's baseline, frames in which the card writes nothing add no versions to its regions
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(30, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);
    const int flash = RegionIndex("neogs.flash");
    ASSERT_GT(flash, 0);
    for (size_t i = 1; i < _engine.CheckpointCount(); ++i)
        EXPECT_EQ(_engine.ChangeCount(i, static_cast<uint32_t>(flash)), 0u)
            << "nothing programs the flash, checkpoint " << i;
}

TEST_F(TimeTravelManager_ShadowRegions_Test, DetachingStopsTheCardMarkingWrites)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _v1->SetShadowEngine(nullptr);
    std::vector<ttd::TTDDeviceRegion> regions;
    _ngs->TTDRegions(regions);   // rebinding clears the trackers
    _ngs->memory().poke(0x8000, 0x11);
    std::vector<uint32_t> written;
    regions[0].tracker->CollectAndClear(written);
    EXPECT_TRUE(written.empty()) << "a detached engine's trackers are not marked";
    _v1->StopRecording();
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

TEST(TimeTravelManagerMoonSound_ShadowRegions_Test, WaveRamIsRecordedAndRestoredExactly)
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
    ttd::TimeTravelManager* v1 = context->pTimeTravelManager;

    ttd::TimeTravelEngine engine;
    v1->SetShadowEngine(&engine);
    ASSERT_TRUE(v1->StartRecording());
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
            copies[v1->GetCheckpointCount() - 1] = std::vector<uint8_t>(wave.RomData() + ramStart, wave.RomData() + ramStart + ramBytes);
    }
    v1->StopRecording();
    v1->SetShadowEngine(nullptr);

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

TEST(TimeTravelManagerGeneralSound_ShadowRegions_Test, CardRamIsARegion_RegistersStayInTheBlob)
{
    SoundCardScope cards{TestSound::GeneralSound};
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* v1 = context->pTimeTravelManager;

    ttd::TimeTravelEngine engine;
    v1->SetShadowEngine(&engine);
    ASSERT_TRUE(v1->StartRecording());
    emulator->RunNFrames(80, /*skipBreakpoints=*/true);
    v1->StopRecording();
    v1->SetShadowEngine(nullptr);

    int region = -1;
    for (size_t i = 0; i < engine.Regions().size(); ++i)
        if (engine.Regions()[i].name == "gs.ram")
            region = static_cast<int>(i);
    ASSERT_GT(region, 0);
    ASSERT_EQ(engine.CheckpointCount(), v1->GetCheckpointCount());

    constexpr uint8_t gsId = static_cast<uint8_t>(ttd::PeripheralId::GeneralSound);
    std::vector<uint8_t> fixed, ram, engineRam;
    size_t engineBlobBytes = 0, v1BlobBytes = 0;
    for (size_t i = 0; i < engine.CheckpointCount(); ++i)
    {
        const ttd::TTDCheckpoint* want = v1->GetCheckpoint(i);
        const ttd::TTDEngineCheckpoint* got = engine.Checkpoint(i);
        ASSERT_TRUE(ttd::bench::SplitV1GeneralSound(want->peripheralBlobs.at(gsId), fixed, ram));
        EXPECT_EQ(ttd::TTDPeripheralRegistry::DecodeBlob(gsId, got->deviceBlobs.at(gsId)), fixed) << "registers, checkpoint " << i;
        engineRam.assign(ram.size(), 0);
        ASSERT_TRUE(engine.RestoreRegion(i, static_cast<uint32_t>(region), engineRam.data()).Ok());
        ASSERT_TRUE(engineRam == ram) << "card RAM, checkpoint " << i;
        engineBlobBytes += got->deviceBlobs.at(gsId).size();
        v1BlobBytes += want->peripheralBlobs.at(gsId).size();
    }
    EXPECT_LT(engineBlobBytes * 4, v1BlobBytes) << "the engine's blob carries the registers, not the RAM";

    EmulatorTestHelper::CleanupEmulator(emulator);
}
