/// @file ttdv1feeder_test.cpp
/// @brief The engine oracle: every recorded v1 session, fed to TimeTravelEngine
/// frame by frame, restores every checkpoint exactly as v1 does
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.2, §6).
///
/// The corpus is testdata/ttd and testdata/machines/<machine>/ttd: real
/// recordings on Pentagon, TS-Conf and the other models with fixtures, and the
/// ZX-MultiSound sessions (Pentagon, ZX-Evo), recorded by v1 in the test process
/// first (ttdmultisoundsessions.h).
/// Each file boots its machine and decodes every checkpoint twice (engine
/// and v1), so the test takes a few seconds - it is the engine's acceptance
/// check, not a unit test.

#include <gtest/gtest.h>

#include "_helpers/ttdeventscompare.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdmultisoundsessions.h"
#include "_helpers/ttdslotcards.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
namespace fs = std::filesystem;

std::vector<fs::path> CorpusFiles()
{
    std::vector<fs::path> files;
    const fs::path root = TestPathHelper::FindProjectRoot() / "testdata";
    std::vector<fs::path> dirs = {root / "ttd", root / "ttd" / "port-journals"};   // the latter: keys, a tape load
    if (fs::exists(root / "machines"))
        for (const auto& machine : fs::directory_iterator(root / "machines"))
            dirs.push_back(machine.path() / "ttd");
    for (const fs::path& dir : dirs)
        if (fs::exists(dir))
            for (const auto& entry : fs::directory_iterator(dir))
                if (entry.path().extension() == ".ttd")
                    files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& recorded : ttdtest::MultiSoundSessions(ttdtest::SessionRecorder::V1))
        files.push_back(recorded);
    return files;
}
}  // namespace

class TTDV1Feeder_Test : public ::testing::Test
{
protected:
    // The fixtures were recorded on the shipped configs, cards included
    SoundCardScope _soundCards;
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;

    /// A fresh machine of a session's recorded machine: its General Sound card and its slot-built cards
    /// (ttdslotcards.h)
    void StartMachine(const ttd::TTDRecordedMachine& machine)
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        std::string err;
        _emulator = ttdtest::CreateRecordedMachine(machine, err);
        ASSERT_NE(_emulator, nullptr) << err;
        EmulatorContext* context = _emulator->GetContext();
        _v1 = context->pTimeTravelManager;
        ASSERT_NE(_v1, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, machine.generalSound));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// Load @p file into v1 on a machine of its model
    void LoadIntoV1(const fs::path& file)
    {
        ttd::TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ttd::ReadTTDFileInfo(file.string(), info, err)) << err;
        ASSERT_NO_FATAL_FAILURE(StartMachine(info.machine));
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(_v1->DeserializeSession(in, err)) << err;
        ASSERT_GT(_v1->GetCheckpointCount(), 0u);
    }
};

TEST_F(TTDV1Feeder_Test, EngineRestoresEveryCheckpointAsV1)
{
    const auto files = CorpusFiles();
    ASSERT_GE(files.size(), 5u) << "testdata/ttd/ should hold the recorded corpus";

    for (const fs::path& file : files)
    {
        SCOPED_TRACE(file.filename().string());
        ASSERT_NO_FATAL_FAILURE(LoadIntoV1(file));

        ttd::TimeTravelEngine engine;
        std::string err;
        ttd::bench::FeedStats stats;
        ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err, &stats)) << err;
        const size_t count = _v1->GetCheckpointCount();
        ASSERT_EQ(engine.CheckpointCount(), count);
        ASSERT_EQ(stats.checkpoints, count);

        std::vector<uint8_t> v1Ram;
        std::vector<uint8_t> v1Present;
        std::vector<uint8_t> engineRam;
        std::vector<uint8_t> enginePresent;
        for (size_t i = 0; i < count; ++i)
        {
            const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(i);
            const ttd::TTDEngineCheckpoint* got = engine.Checkpoint(i);
            ASSERT_NE(got, nullptr);

            // Position, parent and time
            EXPECT_EQ(got->position.frame, want->time.frame) << "checkpoint " << i;
            EXPECT_EQ(got->parent, i == 0 ? ttd::TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(i - 1));
            EXPECT_EQ(engine.CheckpointIndexOf(got->position), static_cast<int64_t>(i));

            // CPU, chipset, devices
            EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU at checkpoint " << i;
            EXPECT_EQ(std::memcmp(&got->chipset, &want->chipset, sizeof(want->chipset)), 0)
                << "chipset at checkpoint " << i;
            // Device state: identical blobs, except General Sound, whose RAM the
            // engine keeps as a region (its blob holds the registers only)
            constexpr uint8_t gsId = static_cast<uint8_t>(ttd::PeripheralId::GeneralSound);
            for (const auto& [id, blob] : want->peripheralBlobs)
            {
                std::vector<uint8_t> state;
                ASSERT_TRUE(engine.DeviceState(i, id, state)) << "device " << int(id) << " at checkpoint " << i;
                if (id != gsId)
                {
                    EXPECT_TRUE(state == ttd::TTDPeripheralRegistry::DecodeBlob(id, blob))
                        << "device " << int(id) << " at checkpoint " << i;
                    continue;
                }
                std::vector<uint8_t> fixed, gsRam, engineGsRam, present;
                ASSERT_TRUE(ttd::bench::SplitV1GeneralSound(blob, fixed, gsRam));
                EXPECT_EQ(state, fixed) << "General Sound registers at checkpoint " << i;
                ASSERT_EQ(engine.Regions()[1].name, "gs.ram");
                engineGsRam.assign(gsRam.size(), 0);
                ASSERT_TRUE(engine.RestoreRegion(i, 1, engineGsRam.data(), &present).Ok());
                ASSERT_TRUE(engineGsRam == gsRam) << "General Sound RAM at checkpoint " << i;
            }

            // Memory, byte for byte, and the same set of pieces the session knows
            ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, i, v1Ram, v1Present, err)) << err;
            engineRam.assign(v1Ram.size(), 0);
            const ttd::TTDRestoreResult r = engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent);
            ASSERT_TRUE(r.Ok()) << r.message;
            ASSERT_EQ(enginePresent, v1Present) << "pieces known at checkpoint " << i;
            if (engineRam != v1Ram)
            {
                size_t p = 0;
                while (std::memcmp(engineRam.data() + p * ttd::kTTDPieceSize, v1Ram.data() + p * ttd::kTTDPieceSize,
                                   ttd::kTTDPieceSize) == 0)
                    ++p;
                FAIL() << "RAM differs at checkpoint " << i << ", piece " << p;
            }
        }
    }
}

TEST_F(TTDV1Feeder_Test, FeedsOnlyRealChanges)
{
    // v1 stores every non-zero piece again at each key frame (every 50 frames);
    // the feeder must hand the engine only content that changed
    const fs::path idle = TestPathHelper::FindProjectRoot() / "testdata/ttd/idle_session.ttd";
    ASSERT_TRUE(fs::exists(idle));
    ASSERT_NO_FATAL_FAILURE(LoadIntoV1(idle));

    ttd::TimeTravelEngine engine;
    std::string err;
    ttd::bench::FeedStats stats;
    ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err, &stats)) << err;

    // Every piece v1 stored under a new slot id: real changes plus the key
    // frames' re-stores of unchanged content. The feeder must hand over fewer
    // (feeding every new slot would make the two equal)
    size_t newSlots = 0;
    for (size_t i = 0; i < _v1->GetCheckpointCount(); ++i)
    {
        const ttd::TTDCheckpoint* cp = _v1->GetCheckpoint(i);
        const ttd::TTDCheckpoint* prev = i ? _v1->GetCheckpoint(i - 1) : nullptr;
        for (size_t page = 0; page < cp->ramPages.size(); ++page)
            for (uint32_t sub = 0; sub < 4; ++sub)
            {
                const uint32_t slot = cp->ramPages[page].pageSlots[sub];
                if (slot != ttd::TTDPageRef::kNeverTouched &&
                    (!prev || prev->ramPages[page].pageSlots[sub] != slot))
                    ++newSlots;
            }
    }
    ASSERT_GE(_v1->GetCheckpointCount(), 100u) << "the session must span key frames";
    EXPECT_LT(stats.changedRamPieces, newSlots) << "the key frames' re-stores must not be fed as changes";
}

/// Phase 3, Step 1: every v1 session's input journal (network records and
/// received bytes included) and external events go into the engine's event
/// log, each at its time, kind numbers kept; nothing is refused. Its port
/// journals become the engine's bus journals, cursors per checkpoint equal
TEST_F(TTDV1Feeder_Test, EveryEventOfTheCorpusImportsIntoTheEventLog)
{
    size_t total = 0, markers = 0, net = 0, busRecords = 0;
    for (const fs::path& file : CorpusFiles())
    {
        SCOPED_TRACE(file.filename().string());
        ASSERT_NO_FATAL_FAILURE(LoadIntoV1(file));
        ttd::TimeTravelEngine engine;
        std::string err;
        ttd::bench::FeedStats stats;
        ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err, &stats)) << err;
        EXPECT_EQ(stats.eventsRefused, 0u);
        const uint64_t span = _v1->FrameSpan();
        ASSERT_NO_FATAL_FAILURE(ttdtest::ExpectEventsEqualV1(engine, _v1->GetInputJournal(), _v1->GetExternalEvents(), 0,
                                                             0, [span](uint64_t frame) { return frame * span; }));
        ASSERT_NO_FATAL_FAILURE(ttdtest::ExpectBusEqualV1(engine, *_v1));
        busRecords += engine.BusReads().Size() + engine.BusWrites().Size();
        total += stats.events;
        markers += _v1->GetExternalEvents().Size();
        for (const ttd::TTDInputEvent& e : _v1->GetInputJournal().Events())
            net += e.kind == ttd::TTDInputKind::NetEvent ? 1 : 0;
    }
    EXPECT_GT(total, 0u) << "the corpus holds input";
    EXPECT_GT(markers, 0u) << "the corpus holds markers";
    EXPECT_GT(busRecords, 0u) << "the corpus holds port journals";
    (void)net;
}

/// D40, J6: a v1 session's write journal and its segments go into the
/// engine's write index (memory writes; files from before D40 also journaled
/// port OUTs, which stay out)
TEST_F(TTDV1Feeder_Test, EveryWriteJournalOfTheCorpusImportsIntoTheEngine)
{
    size_t withJournal = 0;
    for (const fs::path& file : CorpusFiles())
    {
        SCOPED_TRACE(file.filename().string());
        ASSERT_NO_FATAL_FAILURE(LoadIntoV1(file));
        ttd::TimeTravelEngine engine;
        std::string err;
        ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err)) << err;
        const ttd::TTDWriteJournal* journal = _v1->GetWriteJournal();
        size_t memoryWrites = 0;
        if (journal)
            for (uint64_t seq = journal->SeqTail(); seq < journal->SeqHead(); ++seq)
                memoryWrites += journal->RecordAt(seq).isIo ? 0 : 1;
        EXPECT_EQ(engine.Writes().Size(), memoryWrites);
        EXPECT_EQ(engine.Writes().Segments(), _v1->GetSessionInfo().writeJournalSegments);
        withJournal += memoryWrites ? 1 : 0;
    }
    EXPECT_GT(withJournal, 0u) << "the corpus holds write journals";
}
