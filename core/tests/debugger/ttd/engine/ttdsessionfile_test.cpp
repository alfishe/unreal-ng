/// @file ttdsessionfile_test.cpp
/// @brief A TimeTravelEngine session through the session file (Phase 4,
/// debugger/ttd/engine/ttdsessionfile.h): every v1 session of the corpus is
/// fed into the engine, saved, loaded into a second engine, and compared
/// checkpoint by checkpoint (every region, every device, the events, the bus
/// and media journals, the write journal, the configuration); saving the
/// loaded session gives the same bytes. Damage stops the load at the first
/// unreachable part, a file cut short loads its complete parts, and a loaded
/// session is read-only.
///
/// Decodes every checkpoint of the corpus twice: an acceptance check of a few
/// seconds, not a unit test.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
namespace fs = std::filesystem;
using namespace ttd;

std::vector<fs::path> CorpusFiles()
{
    std::vector<fs::path> files;
    const fs::path root = TestPathHelper::FindProjectRoot() / "testdata";
    std::vector<fs::path> dirs = {root / "ttd", root / "ttd" / "port-journals"};
    if (fs::exists(root / "machines"))
        for (const auto& machine : fs::directory_iterator(root / "machines"))
            dirs.push_back(machine.path() / "ttd");
    for (const fs::path& dir : dirs)
        if (fs::exists(dir))
            for (const auto& entry : fs::directory_iterator(dir))
                if (entry.path().extension() == ".ttd")
                    files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

TTDSessionSaveParams Params(uint32_t perPart = 50)
{
    TTDSessionSaveParams p;
    p.checkpointsPerPart = perPart;
    p.createdMicros = 1'759'500'000'000'000ull;
    p.uuid[0] = 0x42;
    return p;
}

std::vector<uint8_t> Save(const TimeTravelEngine& engine, uint32_t perPart = 50)
{
    TTDMemorySink sink;
    std::string error;
    EXPECT_TRUE(TTDSessionFile::Save(engine, sink, error, Params(perPart))) << error;
    return sink.bytes;
}

std::vector<TTDPortRecord> Records(const TTDPortJournal& j)
{
    std::vector<TTDPortRecord> out(j.Size());
    for (uint64_t i = 0; i < j.Size(); ++i)
        j.Get(i, out[i]);
    return out;
}

bool SameRecords(const std::vector<TTDPortRecord>& a, const std::vector<TTDPortRecord>& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const TTDPortRecord& x, const TTDPortRecord& y) {
               return x.SameAccess(y) && x.value == y.value;
           });
}

/// Every checkpoint of @p b equals @p a's (the first @p count)
void ExpectSameSession(const TimeTravelEngine& a, const TimeTravelEngine& b, size_t count)
{
    ASSERT_EQ(b.Regions().size(), a.Regions().size());
    for (size_t r = 0; r < a.Regions().size(); ++r)
    {
        EXPECT_EQ(b.Regions()[r].name, a.Regions()[r].name);
        EXPECT_EQ(b.Regions()[r].bytes, a.Regions()[r].bytes);
    }
    std::vector<uint8_t> wa, wb, pa, pb;
    for (size_t i = 0; i < count; ++i)
    {
        const TTDEngineCheckpoint* x = a.Checkpoint(i);
        const TTDEngineCheckpoint* y = b.Checkpoint(i);
        ASSERT_NE(y, nullptr) << "checkpoint " << i;
        ASSERT_EQ(y->position, x->position);
        ASSERT_EQ(y->start, x->start);
        ASSERT_EQ(std::memcmp(&y->cpu, &x->cpu, sizeof(x->cpu)), 0) << "CPU at " << i;
        ASSERT_EQ(std::memcmp(&y->chipset, &x->chipset, sizeof(x->chipset)), 0) << "chipset at " << i;
        ASSERT_EQ(y->busReadCursor, x->busReadCursor);
        ASSERT_EQ(y->busWriteCursor, x->busWriteCursor);
        ASSERT_EQ(y->mediaReadCursor, x->mediaReadCursor);
        ASSERT_EQ(y->busVectorCursor, x->busVectorCursor);
        ASSERT_EQ(y->unclaimedDevices, x->unclaimedDevices);
        for (uint32_t r = 0; r < a.Regions().size(); ++r)
        {
            const size_t size = size_t(a.Regions()[r].pieces) * kTTDPieceSize;
            wa.assign(size, 0xA5);
            wb.assign(size, 0xA5);
            ASSERT_TRUE(a.RestoreRegion(i, r, wa.data(), &pa).Ok());
            ASSERT_TRUE(b.RestoreRegion(i, r, wb.data(), &pb).Ok()) << "region " << r << " at " << i;
            ASSERT_EQ(pb, pa) << "pieces known, region " << r << " at " << i;
            ASSERT_TRUE(wb == wa) << "region " << a.Regions()[r].name << " at checkpoint " << i;
        }
    }
}
}  // namespace

class TTDSessionFile_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;
    Emulator* _emulator = nullptr;
    TimeTravelManager* _v1 = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// @p file into v1 on a machine of its model, then into @p engine
    void Feed(const fs::path& file, TimeTravelEngine& engine)
    {
        TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ReadTTDFileInfo(file.string(), info, err)) << err;
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = EmulatorTestHelper::CreateStandardEmulator(info.machine.model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        _v1 = context->pTimeTravelManager;
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, info.machine.generalSound));
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(_v1->DeserializeSession(in, err)) << err;
        ASSERT_TRUE(bench::FeedV1Session(*_v1, engine, err)) << err;
    }

    /// The largest session of the corpus
    fs::path Largest()
    {
        fs::path best;
        uintmax_t size = 0;
        for (const fs::path& f : CorpusFiles())
            if (fs::file_size(f) > size)
            {
                size = fs::file_size(f);
                best = f;
            }
        return best;
    }
};

TEST_F(TTDSessionFile_Test, EveryCorpusSessionSurvivesTheFile)
{
    const auto files = CorpusFiles();
    ASSERT_GE(files.size(), 5u);
    for (const fs::path& file : files)
    {
        SCOPED_TRACE(file.filename().string());
        TimeTravelEngine original;
        ASSERT_NO_FATAL_FAILURE(Feed(file, original));
        const std::vector<uint8_t> bytes = Save(original);

        TimeTravelEngine loaded;
        TTDMemorySource source(bytes);
        std::string error;
        TTDSessionLoadReport report;
        ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
        EXPECT_TRUE(report.complete) << report.stoppedAt;
        ASSERT_EQ(loaded.CheckpointCount(), original.CheckpointCount());
        ASSERT_NO_FATAL_FAILURE(ExpectSameSession(original, loaded, original.CheckpointCount()));

        // Events, journals, configuration
        ASSERT_EQ(loaded.Events().Count(), original.Events().Count());
        for (size_t k = 0; k < original.Events().Count(); ++k)
        {
            const TTDEvent& x = original.Events().At(k);
            const TTDEvent& y = loaded.Events().At(k);
            ASSERT_EQ(y.machineTime, x.machineTime) << "event " << k;
            ASSERT_EQ(y.seq, x.seq);
            ASSERT_EQ(y.kind, x.kind);
            ASSERT_EQ(std::memcmp(y.args, x.args, sizeof(x.args)), 0);
            ASSERT_EQ(static_cast<bool>(y.payload), static_cast<bool>(x.payload));
            if (x.payload)
                ASSERT_EQ(loaded.Payloads().Bytes(y.payload), original.Payloads().Bytes(x.payload));
        }
        EXPECT_TRUE(SameRecords(Records(loaded.BusReads()), Records(original.BusReads())));
        EXPECT_TRUE(SameRecords(Records(loaded.BusWrites()), Records(original.BusWrites())));
        EXPECT_TRUE(SameRecords(Records(loaded.BusVectors()), Records(original.BusVectors())));
        EXPECT_EQ(loaded.MediaReads().Size(), original.MediaReads().Size());
        EXPECT_EQ(loaded.Writes().Size(), original.Writes().Size());
        EXPECT_EQ(loaded.Writes().Segments().size(), original.Writes().Segments().size());
        ASSERT_EQ(loaded.Configurations().size(), original.Configurations().size());
        for (size_t k = 0; k < original.Configurations().size(); ++k)
            EXPECT_EQ(loaded.Configurations()[k].fingerprint, original.Configurations()[k].fingerprint);

        // The loaded session saves to the same bytes
        EXPECT_TRUE(Save(loaded) == bytes) << "a re-save differs";
        // Bytes per stream, for the comparison with v1's file
        TTDMemorySource again(bytes);
        TTDContainerReader reader;
        ASSERT_TRUE(reader.Open(again, error));
        std::map<uint16_t, uint64_t> perStream;
        for (const TTDPartRef& part : reader.Parts())
            for (const TTDRecordRef& record : part.records)
                perStream[record.streamId] += record.storedSize + kRecordHeaderSize;
        std::string split;
        for (const auto& [id, n] : perStream)
            split += " " + reader.Header().Stream(id)->name + "=" + std::to_string(n);
        std::printf("[ session  ] %s: %zu checkpoints, %zu bytes (v1 %llu):%s\n", file.filename().string().c_str(),
                    original.CheckpointCount(), bytes.size(), static_cast<unsigned long long>(fs::file_size(file)),
                    split.c_str());
    }
}

/// A damaged piece record in part 2: the load stops there with the frames of
/// parts 0 and 1, which restore as before
TEST_F(TTDSessionFile_Test, DamageStopsTheLoadAtTheFirstUnreachablePart)
{
    TimeTravelEngine original;
    ASSERT_NO_FATAL_FAILURE(Feed(Largest(), original));
    ASSERT_GE(original.CheckpointCount(), 40u);
    std::vector<uint8_t> bytes = Save(original, 10);

    TTDMemorySource probe(bytes);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(probe, error));
    // Every later part needs part 0: it holds the first version of every piece
    // that has not changed since. Damage there makes the whole session unreachable
    {
        TTDContainerReader deps;
        ASSERT_TRUE(deps.Open(probe, error));
        deps.MarkDamaged(0, "test");
        for (const TTDPartRef& part : deps.Parts())
            EXPECT_FALSE(deps.IsReachable(part.index)) << "part " << part.index;
    }
    const TTDRecordRef& pieces = reader.Parts()[2].records[0];
    ASSERT_EQ(pieces.streamId, sessionstream::kPieces);
    ASSERT_GT(pieces.storedSize, 0u);
    bytes[pieces.offset + kRecordHeaderSize + pieces.storedSize / 2] ^= 0x20;

    TimeTravelEngine loaded;
    TTDMemorySource source(bytes);
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    EXPECT_FALSE(report.complete);
    EXPECT_EQ(report.partsLoaded, 2u);
    EXPECT_EQ(loaded.CheckpointCount(), 20u);
    EXPECT_NE(report.stoppedAt.find("part 2"), std::string::npos) << report.stoppedAt;
    ExpectSameSession(original, loaded, 20);
}

/// A file cut short (a crash while recording): its complete parts load
TEST_F(TTDSessionFile_Test, AFileCutShortLoadsItsCompleteParts)
{
    TimeTravelEngine original;
    ASSERT_NO_FATAL_FAILURE(Feed(Largest(), original));
    std::vector<uint8_t> bytes = Save(original, 10);
    TTDMemorySource probe(bytes);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(probe, error));
    ASSERT_GE(reader.Parts().size(), 4u);
    bytes.resize(static_cast<size_t>(reader.Parts()[3].records[1].offset) + 7);   // inside part 3

    TimeTravelEngine loaded;
    TTDMemorySource source(bytes);
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    EXPECT_EQ(report.partsLoaded, 3u);
    EXPECT_EQ(loaded.CheckpointCount(), 30u);
    ExpectSameSession(original, loaded, 30);
}

/// A loaded session cannot be continued (its capture state is not in the file)
TEST_F(TTDSessionFile_Test, ALoadedSessionIsReadOnly)
{
    TimeTravelEngine original;
    ASSERT_NO_FATAL_FAILURE(Feed(CorpusFiles().front(), original));
    const std::vector<uint8_t> bytes = Save(original);
    TimeTravelEngine loaded;
    TTDMemorySource source(bytes);
    std::string error;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error)) << error;
    EXPECT_TRUE(loaded.IsReadOnly());
    TTDFrameInput input;
    input.position.frame = loaded.Checkpoint(loaded.CheckpointCount() - 1)->position.frame + 1;
    EXPECT_FALSE(loaded.CaptureFrame(input, error));
    EXPECT_NE(error.find("not continued"), std::string::npos) << error;
}

/// A part with no change of its own still needs the part holding the current
/// version of every piece: frame 0 writes two pieces, nothing changes after,
/// so damage in part 0 makes every later part unreachable
TEST(TTDSessionFileDeps_Test, APartNeedsThePartsOfItsCurrentVersions)
{
    TimeTravelEngine engine;
    TTDRegionDesc ram;
    ram.name = "ram";
    ram.pieces = 2;
    ram.bytes = 2 * kTTDPieceSize;
    std::string error;
    ASSERT_TRUE(engine.BeginSession({ram}, {}, error)) << error;
    std::vector<uint8_t> bytes(2 * kTTDPieceSize);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i * 7 + i / 300);
    for (uint64_t frame = 0; frame < 40; ++frame)
    {
        TTDFrameInput input;
        input.position.frame = frame;
        input.start = frame * 69888;
        if (frame == 0)
            input.changed = {{0, 0, bytes.data()}, {0, 1, bytes.data() + kTTDPieceSize}};
        ASSERT_TRUE(engine.CaptureFrame(input, error)) << error;
    }
    const std::vector<uint8_t> file = Save(engine, 10);
    TTDMemorySource source(file);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    ASSERT_EQ(reader.Parts().size(), 4u);
    for (const TTDPartRef& part : reader.Parts())
        EXPECT_EQ(part.dependencies, part.index == 0 ? std::vector<uint32_t>{} : std::vector<uint32_t>{0})
            << "part " << part.index;
    reader.MarkDamaged(0, "test");
    for (const TTDPartRef& part : reader.Parts())
        EXPECT_FALSE(reader.IsReachable(part.index)) << "part " << part.index;

    TimeTravelEngine loaded;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error)) << error;
    ExpectSameSession(engine, loaded, 40);
}
