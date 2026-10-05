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
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "_helpers/ttdsyntheticsession.h"
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
        // The card the session was recorded with, fitted at creation: the shipped 48K / 128K / +2 / +2A / +3, Profi
        // and Sprinter configs have no GS since 2026-10-04 (owner decision), and a switch cannot fill an empty slot
        {
            GeneralSoundFitScope fit(info.machine.generalSound);
            _emulator = EmulatorTestHelper::CreateStandardEmulator(info.machine.model, LoggerLevel::LogError);
        }
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

/// D31: every v1 session of the corpus converts to an engine file marked as
/// converted, which loads as the session fed straight into the engine
TEST_F(TTDSessionFile_Test, EveryV1SessionConverts)
{
    for (const fs::path& file : CorpusFiles())
    {
        SCOPED_TRACE(file.filename().string());
        TimeTravelEngine fed;
        ASSERT_NO_FATAL_FAILURE(Feed(file, fed));
        TTDMemorySink sink;
        std::string error;
        ASSERT_TRUE(bench::ConvertV1Session(*_v1, sink, error)) << error;
        TimeTravelEngine loaded;
        TTDMemorySource source(sink.bytes);
        TTDSessionLoadReport report;
        ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
        EXPECT_TRUE(report.convertedFromV1);
        EXPECT_TRUE(report.complete);
        ASSERT_EQ(loaded.CheckpointCount(), fed.CheckpointCount());
        ASSERT_NO_FATAL_FAILURE(ExpectSameSession(fed, loaded, fed.CheckpointCount()));
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

/// region <Writing as it records>

namespace
{
/// A synthetic session: four pieces, one changing per frame, a bus read every
/// frame, a marker every seventh frame
struct Recorder
{
    TimeTravelEngine engine;
    std::vector<uint8_t> memory = std::vector<uint8_t>(4 * kTTDPieceSize);
    uint64_t frame = 0;

    Recorder()
    {
        TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = 4;
        ram.bytes = 4 * kTTDPieceSize;
        std::string error;
        EXPECT_TRUE(engine.BeginSession({ram}, {}, error)) << error;
    }

    void Frame()
    {
        const uint32_t piece = static_cast<uint32_t>(frame % 4);
        uint8_t* p = memory.data() + size_t(piece) * kTTDPieceSize;
        for (size_t i = 0; i < kTTDPieceSize; i += 97)
            p[i] = static_cast<uint8_t>(p[i] + frame + i);
        TTDFrameInput input;
        input.position.frame = frame;
        input.start = frame * 69888;
        if (frame == 0)
            for (uint32_t k = 0; k < 4; ++k)
                input.changed.push_back({0, k, memory.data() + size_t(k) * kTTDPieceSize});
        else
            input.changed.push_back({0, piece, p});
        std::string error;
        ASSERT_TRUE(engine.CaptureFrame(input, error)) << error;
        engine.AppendBusRead({frame, 1000, 0xFE, 0x8000, static_cast<uint8_t>(frame)});
        if (frame % 7 == 3)
        {
            TTDEvent ev;
            ev.kind = TTDEventKind::OtherMarker;
            engine.AppendEvent(frame, 2000, ev);
        }
        ++frame;
    }
};

/// Blocks every write until released, then passes it on (a disk that stalls)
class GatedSink : public ITTDByteSink
{
public:
    bool Write(const uint8_t* data, size_t size) override
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this]() { return _open; });
        inner.Write(data, size);
        return true;
    }
    bool Sync() override { return true; }
    uint64_t Size() const override { return inner.Size(); }
    void Open()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _open = true;
        }
        _cv.notify_all();
    }
    TTDMemorySink inner;

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _open = true;

public:
    void Close()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _open = false;
    }
};

/// Fails every write once @p limit bytes went through (a full disk)
class FullSink : public TTDMemorySink
{
public:
    explicit FullSink(size_t limit) : _limit(limit) {}
    bool Write(const uint8_t* data, size_t size) override
    {
        if (bytes.size() + size > _limit)
            return false;
        return TTDMemorySink::Write(data, size);
    }

private:
    size_t _limit;
};
}  // namespace

/// Writing part by part on the writer thread while recording gives the same
/// file as saving the finished session: part boundaries depend on frames and
/// bytes only, never on the thread's timing (QR-3)
TEST(TTDSessionWriter_Test, WritingAsItRecordsEqualsSavingAtTheEnd)
{
    Recorder rec;
    TTDMemorySink sink;
    TTDSessionWriter writer;
    std::string error;
    rec.Frame();
    ASSERT_TRUE(writer.Begin(rec.engine, sink, Params(10), error)) << error;
    for (int i = 1; i < 95; ++i)
    {
        rec.Frame();
        ASSERT_TRUE(writer.Collect(rec.engine));
    }
    EXPECT_EQ(writer.PartsQueued(), 9u) << "parts whose next part has started";
    ASSERT_TRUE(writer.Finish(rec.engine)) << writer.Error();
    EXPECT_TRUE(sink.bytes == Save(rec.engine, 10));

    TimeTravelEngine loaded;
    TTDMemorySource source(sink.bytes);
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error)) << error;
    ExpectSameSession(rec.engine, loaded, 95);
    EXPECT_EQ(loaded.Events().Count(), rec.engine.Events().Count());
    EXPECT_TRUE(SameRecords(Records(loaded.BusReads()), Records(rec.engine.BusReads())));
}

/// A stalled disk: capture goes on (Collect never waits), the lag is
/// reported, and past the hard limit the writer stops with the reason; the
/// parts already written stay valid
TEST(TTDSessionWriter_Test, ASlowDiskNeverHoldsCapture)
{
    Recorder rec;
    GatedSink sink;
    TTDSessionWriter writer;
    std::string error;
    rec.Frame();
    TTDSessionWriterLimits limits;
    limits.lagSoftBytes = 1;
    limits.lagHardBytes = 20'000;
    ASSERT_TRUE(writer.Begin(rec.engine, sink, Params(5), error, limits)) << error;
    sink.Close();
    bool stopped = false;
    for (int i = 1; i < 400 && !stopped; ++i)
    {
        rec.Frame();
        stopped = !writer.Collect(rec.engine);   // returns at once: the sink is closed
    }
    EXPECT_TRUE(stopped) << "the hard limit stops the writer";
    EXPECT_TRUE(writer.Behind());
    EXPECT_NE(writer.Error().find("cannot keep up"), std::string::npos) << writer.Error();
    sink.Open();
    writer.Finish(rec.engine);

    TTDMemorySource source(sink.inner.bytes);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_GT(reader.Parts().size(), 0u) << "the parts queued before the limit were written";
    EXPECT_TRUE(reader.Finalized()) << "a writer that fell behind still finishes its file";
}

/// A write error (disk full): the writer stops with the reason, and the file
/// opens up to its last complete part
TEST(TTDSessionWriter_Test, AWriteErrorKeepsTheFileValid)
{
    Recorder rec;
    FullSink sink(30'000);
    TTDSessionWriter writer;
    std::string error;
    rec.Frame();
    ASSERT_TRUE(writer.Begin(rec.engine, sink, Params(5), error)) << error;
    bool ok = true;
    for (int i = 1; i < 300 && ok; ++i)
    {
        rec.Frame();
        ok = writer.Collect(rec.engine);
    }
    writer.Finish(rec.engine);
    EXPECT_TRUE(writer.Failed());
    EXPECT_NE(writer.Error().find("write failed"), std::string::npos) << writer.Error();

    TTDMemorySource source(sink.bytes);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_FALSE(reader.Finalized());
    ASSERT_GT(reader.Parts().size(), 0u);
    TimeTravelEngine loaded;
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    ExpectSameSession(rec.engine, loaded, loaded.CheckpointCount());
    EXPECT_EQ(loaded.CheckpointCount(), reader.Parts().size() * 5);
}

/// The emulator dies while recording (a child process aborts with parts
/// still queued and one half written): the file opens up to its last
/// complete part, and those parts load back exactly (FR-13)
TEST(TTDSessionWriterDeathTest, ACrashKeepsTheCompleteParts)
{
    GTEST_FLAG_SET(death_test_style, "fast");   // forked: the child writes where the parent reads
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("crashed-session.ttd");
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(path), ec);
    EXPECT_DEATH(
        {
            Recorder rec;
            TTDFileSink sink(path);
            TTDSessionWriter writer;
            std::string error;
            rec.Frame();
            if (!sink.Valid() || !writer.Begin(rec.engine, sink, Params(5), error))
                std::exit(1);
            for (int i = 1; i < 40; ++i)
            {
                rec.Frame();
                writer.Collect(rec.engine);
            }
            // Some parts on disk for sure, then more queued while it dies
            TestWait::For([&writer]() { return writer.QueuedBytes() == 0; });
            for (int i = 0; i < 40; ++i)
            {
                rec.Frame();
                writer.Collect(rec.engine);
            }
            std::abort();
        },
        "");

    TTDFileSource source(path);
    ASSERT_TRUE(source.Valid()) << source.Error();
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_FALSE(reader.Finalized());
    ASSERT_GE(reader.Parts().size(), 7u) << "the parts written before the wait";
    TimeTravelEngine loaded;
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    EXPECT_EQ(loaded.CheckpointCount(), reader.Parts().size() * 5);

    // The same frames, recorded again without the crash
    Recorder again;
    for (size_t i = 0; i < loaded.CheckpointCount() + 1; ++i)
        again.Frame();
    ExpectSameSession(again.engine, loaded, loaded.CheckpointCount());
}

/// endregion </Writing as it records>

/// region <Fixtures for the analyzer (QR-5)>

namespace
{
/// The counts the analyzer must find in a fixture (testdata/ttd/v2/expected.json)
struct FixtureCounts
{
    size_t checkpoints = 0, versions = 0, parts = 0, events = 0, busReads = 0, busWrites = 0;
    bool finalized = false, converted = false;
};

FixtureCounts CountsOf(const std::vector<uint8_t>& bytes)
{
    FixtureCounts n;
    TTDMemorySource source(bytes);
    TTDContainerReader reader;
    std::string error;
    EXPECT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
    n.parts = reader.Parts().size();
    n.finalized = reader.Finalized();
    TimeTravelEngine engine;
    TTDSessionLoadReport report;
    EXPECT_TRUE(TTDSessionFile::Load(engine, source, error, &report)) << error;
    n.converted = report.convertedFromV1;
    n.checkpoints = engine.CheckpointCount();
    for (size_t i = 0; i < engine.CheckpointCount(); ++i)
        for (const TTDEngineCheckpoint::RegionRefs& r : engine.Checkpoint(i)->regions)
            n.versions += r.changeCount;
    n.events = engine.Events().Count();
    n.busReads = engine.BusReads().Size();
    n.busWrites = engine.BusWrites().Size();
    return n;
}

fs::path FixtureDir()
{
    return TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "v2";
}

/// A synthetic recording in segments, as the writer puts it on disk
std::vector<uint8_t> SyntheticFixture()
{
    ttdtest::Session s(ttdtest::Growable(20));
    TTDMemorySink sink;
    TTDSessionWriter writer(false);
    TTDSessionSaveParams params = Params(8);
    std::string error;
    s.Frame();
    EXPECT_TRUE(writer.Begin(s.engine, sink, params, error)) << error;
    for (int i = 1; i < 60; ++i)
    {
        s.Frame();
        writer.Collect(s.engine);
    }
    EXPECT_TRUE(writer.Finish(s.engine));
    return sink.bytes;
}

/// The same with a record of an ancillary stream this version does not know
/// (id 0x01FE): readers skip it
std::vector<uint8_t> AncillaryFixture()
{
    const std::vector<uint8_t> base = SyntheticFixture();
    TTDMemorySource source(base);
    TTDContainerReader reader;
    std::string error;
    EXPECT_TRUE(reader.Open(source, error));
    TTDContainerHeader header = reader.Header();
    header.streams.push_back({0x01FE, 1, TTDStreamKind::Ancillary, "future"});
    TTDMemorySink sink;
    TTDContainerWriter writer;
    EXPECT_TRUE(writer.Begin(sink, header, &error));
    for (const TTDPartRef& part : reader.Parts())
    {
        std::vector<uint8_t> stored;
        for (const TTDRecordRef& r : part.records)
        {
            EXPECT_TRUE(reader.ReadStored(r, stored));
            writer.AddStoredRecord(r.streamId, r.flags, stored.data(), stored.size(), r.rawSize);
        }
        const std::vector<uint8_t> picture(64, static_cast<uint8_t>(part.index));
        writer.AddRecord(0x01FE, picture);
        writer.EndPart({part.firstFrame, part.frameCount, part.branch, part.dependencies, part.extra});
    }
    EXPECT_TRUE(writer.Finalize());
    return sink.bytes;
}
}  // namespace

/// Writes testdata/ttd/v2/ (run by hand after a format change, then commit the
/// files: *.ttd is git-ignored, add them with -f)
TEST_F(TTDSessionFile_Test, DISABLED_WriteAnalyzerFixtures)
{
    fs::create_directories(FixtureDir());
    std::map<std::string, std::vector<uint8_t>> files;
    files["synthetic.ttd"] = SyntheticFixture();
    {
        // Cut inside the last part: an unfinished file (a crash)
        std::vector<uint8_t> cut = files["synthetic.ttd"];
        TTDMemorySource source(cut);
        TTDContainerReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(source, error));
        cut.resize(static_cast<size_t>(reader.Parts().back().records.back().offset) + 10);
        files["synthetic-unfinished.ttd"] = cut;
    }
    files["synthetic-ancillary.ttd"] = AncillaryFixture();
    {
        TimeTravelEngine fed;
        ASSERT_NO_FATAL_FAILURE(Feed(TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "active_demo.ttd", fed));
        TTDMemorySink sink;
        std::string error;
        ASSERT_TRUE(bench::ConvertV1Session(*_v1, sink, error)) << error;
        files["active-demo-converted.ttd"] = sink.bytes;
    }
    std::string json = "{\n";
    for (const auto& [name, bytes] : files)
    {
        std::ofstream(FixtureDir() / name, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                                   static_cast<std::streamsize>(bytes.size()));
        const FixtureCounts n = CountsOf(bytes);
        json += "  \"" + name + "\": {\"checkpoints\": " + std::to_string(n.checkpoints) +
                ", \"versions\": " + std::to_string(n.versions) + ", \"parts\": " + std::to_string(n.parts) +
                ", \"events\": " + std::to_string(n.events) + ", \"bus_reads\": " + std::to_string(n.busReads) +
                ", \"bus_writes\": " + std::to_string(n.busWrites) +
                ", \"finalized\": " + (n.finalized ? "true" : "false") +
                ", \"converted_from_v1\": " + (n.converted ? "true" : "false") + "},\n";
    }
    json.erase(json.size() - 2, 1);   // the last comma
    json += "}\n";
    std::ofstream(FixtureDir() / "expected.json") << json;
}

/// The committed fixtures still load with the counts they were written with:
/// a format change that forgets to rewrite them fails here
TEST_F(TTDSessionFile_Test, CommittedFixturesStillLoad)
{
    std::ifstream in(FixtureDir() / "expected.json");
    ASSERT_TRUE(in.is_open()) << "testdata/ttd/v2/expected.json";
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (const char* name : {"synthetic.ttd", "synthetic-unfinished.ttd", "synthetic-ancillary.ttd",
                             "active-demo-converted.ttd"})
    {
        SCOPED_TRACE(name);
        std::ifstream f(FixtureDir() / name, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const FixtureCounts n = CountsOf(bytes);
        const size_t at = json.find(std::string("\"") + name + "\"");
        ASSERT_NE(at, std::string::npos);
        const std::string entry = json.substr(at, json.find('}', at) - at);
        auto field = [&entry](const std::string& key) {
            const size_t k = entry.find("\"" + key + "\": ");
            return k == std::string::npos ? std::string() : entry.substr(k + key.size() + 4, entry.find_first_of(",}", k + key.size() + 4) - (k + key.size() + 4));
        };
        EXPECT_EQ(field("checkpoints"), std::to_string(n.checkpoints));
        EXPECT_EQ(field("versions"), std::to_string(n.versions));
        EXPECT_EQ(field("parts"), std::to_string(n.parts));
        EXPECT_EQ(field("events"), std::to_string(n.events));
        EXPECT_EQ(field("bus_reads"), std::to_string(n.busReads));
        EXPECT_EQ(field("finalized"), n.finalized ? "true" : "false");
        EXPECT_EQ(field("converted_from_v1"), n.converted ? "true" : "false");
    }
}

/// endregion </Fixtures for the analyzer>

/// region <Frame-boundary streams (Phase 4 Step 4)>

namespace
{
/// The picture a stream would copy at @p frame: 2 KB, a few bytes moving per frame
std::vector<uint8_t> Picture(uint64_t frame)
{
    std::vector<uint8_t> p(2048);
    for (size_t i = 0; i < p.size(); ++i)
        p[i] = static_cast<uint8_t>(i / 64);
    for (size_t k = 0; k < 8; ++k)
        p[(frame * 37 + k * 251) % p.size()] = static_cast<uint8_t>(frame + k);
    return p;
}
}  // namespace

/// A stream switched on at frame 10 and off at 40: its copies are written
/// through (they leave memory once their part is queued), every copy reads
/// back from the file exactly, and the frames it was off answer "not recorded"
TEST(TTDFrameStream_Test, CopiesAreWrittenThroughAndReadBack)
{
    ttdtest::Session s(ttdtest::Growable(0));
    s.engine.Streams().Register(0, "picture", [&s](const TTDPosition& at) {
        const std::vector<uint8_t> p = Picture(at.frame);
        s.engine.AddFrameStreamCopy(0, at.frame, p.data(), p.size());
    });
    TTDMemorySink sink;
    TTDSessionWriter writer;
    std::string error;
    s.Frame();
    ASSERT_TRUE(writer.Begin(s.engine, sink, Params(5), error)) << error;
    size_t peak = 0;
    for (int i = 1; i < 60; ++i)
    {
        if (i == 10)
            s.engine.Streams().SetEnabled(0, true);
        if (i == 40)
            s.engine.Streams().SetEnabled(0, false);
        s.Frame();
        ASSERT_TRUE(writer.Collect(s.engine)) << writer.Error();
        size_t held = 0;
        for (const auto& [id, copies] : s.engine.FrameStreamCopies())
            held += copies.size();
        peak = std::max(peak, held);
    }
    ASSERT_TRUE(writer.Finish(s.engine)) << writer.Error();
    EXPECT_LE(peak, 10u) << "at most two parts' copies wait in memory";

    TTDMemorySource source(sink.bytes);
    std::vector<uint8_t> out;
    for (uint64_t f = 10; f < 40; ++f)
    {
        ASSERT_TRUE(TTDSessionFile::ReadFrameStream(source, 0, f, out, error)) << "frame " << f << ": " << error;
        ASSERT_EQ(out, Picture(f)) << "frame " << f;
    }
    EXPECT_FALSE(TTDSessionFile::ReadFrameStream(source, 0, 5, out, error));
    EXPECT_NE(error.find("not recorded"), std::string::npos) << error;
    EXPECT_FALSE(TTDSessionFile::ReadFrameStream(source, 0, 45, out, error));

    // The stream is ancillary: the session loads without it, and the copies cost little
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error));
    ASSERT_NE(reader.Header().Stream(sessionstream::kFrameStreamFirst), nullptr);
    EXPECT_EQ(reader.Header().Stream(sessionstream::kFrameStreamFirst)->kind, TTDStreamKind::Ancillary);
    uint64_t stored = 0;
    for (const TTDPartRef& part : reader.Parts())
        for (const TTDRecordRef& r : part.records)
            if (r.streamId == sessionstream::kFrameStreamFirst)
                stored += r.storedSize;
    EXPECT_GT(stored, 0u);
    EXPECT_LT(stored, 30u * 2048 / 4) << "differences compress: well under a quarter of the copies";
    TimeTravelEngine loaded;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error)) << error;
    EXPECT_EQ(loaded.CheckpointCount(), 60u);
}

/// No writer (no file): the copies stay in memory and read from there
TEST(TTDFrameStream_Test, WithoutAFileCopiesStayInMemory)
{
    ttdtest::Session s(ttdtest::Growable(0));
    s.engine.Streams().Register(0, "picture", [&s](const TTDPosition& at) {
        const std::vector<uint8_t> p = Picture(at.frame);
        s.engine.AddFrameStreamCopy(0, at.frame, p.data(), p.size());
    });
    s.engine.Streams().SetEnabled(0, true);
    for (int i = 0; i < 20; ++i)
        s.Frame();
    std::vector<uint8_t> out;
    ASSERT_TRUE(s.engine.FrameStreamCopy(0, 7, out));
    EXPECT_EQ(out, Picture(7));
    EXPECT_GE(s.engine.HeapBreakdown().frameStreams, 20u * 2048);
}

/// endregion </Frame-boundary streams>
