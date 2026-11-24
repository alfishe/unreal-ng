/// @file ttdseekscaling_test.cpp
/// @brief Seek cost against session length (PR-5 / PR-6 / PR-7; P1.S10 soak, 2026-11-21).
///
/// Counted guards (short tier): a pause for browsing writes the paused segment's part of the write journal, not the
/// whole journal again; a folder joined after pauses holds the journal once; the timeline counts writes without
/// decoding the journal. LongSession (long tier): seek and pause at N and 4N frames of a folder recording with the
/// write journal, the cost may not follow the length.
///
/// Measure: a diagnostic, run only with UNREAL_TTD_SEEK_SCALING=<frames,...> (the session lengths to measure at):
/// PENTAGON + a game snapshot with keys, recording into a folder with the write journal on and a 15,000-frame
/// history limit (the 2026-11-21 soak's configuration). At each length random seeks over the whole session and inside
/// the memory window, frame-aligned and inside a frame, each split into its phases, then a whole-session timeline.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/engine/ttdwriteindex.h"
#include "debugger/ttd/machinestatehash.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcoverageindex.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

double Pct(std::vector<double> v, double p)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    return v[i];
}

std::vector<uint64_t> Lengths()
{
    std::vector<uint64_t> out;
    const char* env = std::getenv("UNREAL_TTD_SEEK_SCALING");
    if (!env)
        return out;
    std::stringstream ss(env);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty())
            out.push_back(std::strtoull(item.c_str(), nullptr, 10));
    std::sort(out.begin(), out.end());
    return out;
}

bool EnvOn(const char* name, bool dflt)
{
    const char* v = std::getenv(name);
    return v ? v[0] == '1' : dflt;
}

/// The host's one-minute load average (0 where the system has none): a timing's context
double HostLoad()
{
#ifdef _WIN32
    return 0.0;
#else
    double load[1] = {0.0};
    return getloadavg(load, 1) == 1 ? load[0] : 0.0;
#endif
}

/// The trunk's segment files of a recording folder, by number (the newest last)
std::vector<std::string> TrunkFiles(const std::string& folder)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(FileHelper::ToFsPath(folder), ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() == std::strlen("segment-0000.ttd") && name.rfind("segment-", 0) == 0 &&
            entry.path().extension() == ".ttd")
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

uint64_t FileBytes(const std::string& path)
{
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(FileHelper::ToFsPath(path), ec);
    return ec ? 0 : size;
}

/// A Pentagon running a loop that folds the keyboard port into RAM #6000-#7FFF: thousands of journaled writes a
/// frame, memory changing every frame (as TimeTravelController_TwoTiers_Test)
class TTDSeekScaling_Folder_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0xDB, 0xFE,        // 8004 loop: IN A,(#FE)
            0x86,              // 8006 ADD A,(HL)
            0x77,              // 8007 LD (HL),A
            0x23,              // 8008 INC HL
            0x7C,              // 8009 LD A,H  -- wrap #7FFF -> #6000
            0xE6, 0x1F,        // 800A AND #1F
            0xF6, 0x60,        // 800C OR #60
            0x67,              // 800E LD H,A
            0x18, 0xF3,        // 800F JR loop
        };
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// Recording into a folder with the write journal, segments of @p segmentFrames
    void Start(uint64_t segmentFrames)
    {
        _ttd->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("ttd-seek-scaling"));
        _ttd->SetHistoryLimit(segmentFrames * 8, 0);
        _ttd->SetEnableWriteJournal(true);
        ASSERT_TRUE(_ttd->StartRecording());
    }

    /// A browse: the first seek pauses the recording, which finishes the open file (written behind the pause:
    /// waited for here). The size of that file
    uint64_t PauseAndMeasure()
    {
        const ttd::TTDTimePoint end = _ttd->SessionEndPosition();
        EXPECT_TRUE(_ttd->SeekTo({end.frame - 1, 0}));
        _ttd->WaitShadowFiles();
        const std::vector<std::string> files = TrunkFiles(_ttd->ShadowRecordingFolder());
        return files.empty() ? 0 : FileBytes(files.back());
    }

    void Resume()
    {
        ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->SessionEndPosition()));
    }
};
}  // namespace

/// A pause for browsing finishes the open file. It holds that segment's part of the write journal: at 5x the history
/// the file is as large as at 1x (before 2026-11-21 it held the whole journal, written again at each pause: 11 s and
/// 272 MB at 240,000 frames). Runtime: a Pentagon recording 120 frames with the journal on (~0.3 s)
TEST_F(TTDSeekScaling_Folder_Test, APauseWritesItsSegmentNotTheHistory)
{
    Start(8);
    _emulator->RunNFrames(24);
    const uint64_t early = PauseAndMeasure();
    Resume();
    _emulator->RunNFrames(96);   // 5x the history; the paused segment again eight frames
    const uint64_t late = PauseAndMeasure();
    ASSERT_GT(early, 0u);
    EXPECT_GT(_ttd->GetEngine().Writes().Size(), 120u * 1000u) << "the journal holds the loop's writes";
    EXPECT_LT(static_cast<double>(late), static_cast<double>(early) * 1.5 + 16384.0)
        << "the paused file's size follows the history: early " << early << ", late " << late;
}

/// The folder joined after pauses holds everything once: each file its own part of the write journal, the spans
/// joined, and the frame recorded right after each resume (between the paused file's end and the next file's first
/// checkpoint) in the next file. Before 2026-11-21 that frame was in no file: its port reads, port writes and
/// memory writes were lost on load (P1.S10 follow-up)
TEST_F(TTDSeekScaling_Folder_Test, AFolderAfterPausesHoldsEveryRecordOnce)
{
    Start(8);
    _emulator->RunNFrames(20);
    PauseAndMeasure();
    Resume();
    _emulator->RunNFrames(60);   // past the first coverage block (64 frames): the last part starts after it
    PauseAndMeasure();
    Resume();
    _emulator->RunNFrames(13);
    _ttd->StopRecording();

    const ttd::TimeTravelEngine& live = _ttd->GetEngine();
    const std::vector<std::string> files = TrunkFiles(_ttd->ShadowRecordingFolder());
    ASSERT_GE(files.size(), 6u);
    ttd::TimeTravelEngine loaded;
    std::string error;
    ttd::TTDSessionLoadReport report;
    ASSERT_TRUE(ttd::LoadRecording(loaded, files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(loaded.CheckpointCount(), live.CheckpointCount());

    // The port reads frame by frame: a frame the files lack shows by number
    auto perFrame = [](const ttd::TTDPortJournal& j) {
        std::vector<std::pair<uint64_t, uint64_t>> counts;
        ttd::TTDPortRecord r;
        for (uint64_t i = j.FirstIndex(); i < j.Size(); ++i)
            if (j.Get(i, r))
            {
                if (counts.empty() || counts.back().first != r.frame)
                    counts.emplace_back(r.frame, 0);
                ++counts.back().second;
            }
        return counts;
    };
    EXPECT_EQ(perFrame(loaded.BusReads()), perFrame(live.BusReads())) << "port reads per frame";
    EXPECT_EQ(loaded.BusWrites().Size(), live.BusWrites().Size()) << "port writes";
    EXPECT_EQ(loaded.Events().Count(), live.Events().Count()) << "events";
    EXPECT_EQ(loaded.Writes().Size(), live.Writes().Size()) << "every memory write once";
    EXPECT_EQ(loaded.Writes().Segments(), live.Writes().Segments());
    std::vector<uint64_t> a, b;
    loaded.Writes().ForEach([&](const ttd::TTDWriteRecord& r) { a.push_back(r.globalT); });
    live.Writes().ForEach([&](const ttd::TTDWriteRecord& r) { b.push_back(r.globalT); });
    EXPECT_TRUE(a == b) << "the same records in the same order";

    // The coverage index: a part per paused file (from where the last part's open blocks started), joined in order,
    // is the live index (before 2026-11-21 each pause wrote it whole: 90 MB at 240,000 frames)
    ASSERT_GE(report.coverageParts.size(), 3u) << "a part per pause and the stop";
    EXPECT_EQ(report.coverageParts.front().first, 0u);
    EXPECT_GT(report.coverageParts.back().first, 0u) << "the later parts start where the last one's blocks were open";
    std::vector<uint8_t> joinedBytes = ttd::TimeTravelController::JoinCoverageParts(report.coverageParts);
    auto joined = std::make_unique<ttd::TTDCoverageIndex>();
    std::istringstream in(std::string(joinedBytes.begin(), joinedBytes.end()));
    ASSERT_TRUE(joined->Deserialize(in));
    const ttd::TTDCoverageIndex& liveCoverage = _ttd->GetCoverageIndex();
    uint64_t first = 0, lastFrame = 0;
    ASSERT_TRUE(liveCoverage.CoveredRange(ttd::TTDCoverageKind::Executed, first, lastFrame));
    size_t held = 0;
    for (uint64_t f = first; f <= lastFrame; ++f)
        for (const ttd::TTDCoverageKind kind : {ttd::TTDCoverageKind::Executed, ttd::TTDCoverageKind::Written})
        {
            std::vector<ttd::TTDCoverageKey> got, want;
            const bool has = joined->GetFrameKeys(kind, f, got);
            ASSERT_EQ(has, liveCoverage.GetFrameKeys(kind, f, want)) << "frame " << f;
            EXPECT_EQ(got, want) << "frame " << f;
            held += has;
        }
    EXPECT_GT(held, 80u);
}

/// The trunk's files closed while recording carry the coverage index's part since the last one: the first pause after
/// a long run writes only the blocks open since the last file, not every block since the start (0.4-0.5 s and 89 MB
/// after 240,000 frames, P1.S10 seek tails). The parts joined are the live index. Runtime: 140 frames recorded with
/// the journal (~0.4 s)
TEST_F(TTDSeekScaling_Folder_Test, AFirstPauseWritesTheCoverageSinceTheLastFile)
{
    Start(8);
    _emulator->RunNFrames(140);   // two coverage blocks sealed, 17 files closed
    PauseAndMeasure();
    _ttd->StopRecording();

    const std::vector<std::string> files = TrunkFiles(_ttd->ShadowRecordingFolder());
    ASSERT_GE(files.size(), 15u);
    ttd::TimeTravelEngine loaded;
    std::string error;
    ttd::TTDSessionLoadReport report;
    ASSERT_TRUE(ttd::LoadRecording(loaded, files, 0, error, &report)) << error;
    ASSERT_GE(report.coverageParts.size(), 3u) << "a part per file closed after a block was sealed";
    EXPECT_EQ(report.coverageParts.front().first, 0u);
    EXPECT_GE(report.coverageParts.back().first, 64u) << "the pause's part starts at the last open block";
    size_t total = 0;
    for (const auto& part : report.coverageParts)
        total += part.second.size();
    EXPECT_LT(report.coverageParts.back().second.size() * 2, total) << "the pause writes a part, not the index";

    std::vector<uint8_t> joinedBytes = ttd::TimeTravelController::JoinCoverageParts(report.coverageParts);
    auto joined = std::make_unique<ttd::TTDCoverageIndex>();
    std::istringstream in(std::string(joinedBytes.begin(), joinedBytes.end()));
    ASSERT_TRUE(joined->Deserialize(in));
    const ttd::TTDCoverageIndex& liveCoverage = _ttd->GetCoverageIndex();
    uint64_t first = 0, lastFrame = 0;
    ASSERT_TRUE(liveCoverage.CoveredRange(ttd::TTDCoverageKind::Executed, first, lastFrame));
    size_t held = 0;
    for (uint64_t f = first; f <= lastFrame; ++f)
        for (const ttd::TTDCoverageKind kind : {ttd::TTDCoverageKind::Executed, ttd::TTDCoverageKind::Written})
        {
            std::vector<ttd::TTDCoverageKey> got, want;
            const bool has = joined->GetFrameKeys(kind, f, got);
            ASSERT_EQ(has, liveCoverage.GetFrameKeys(kind, f, want)) << "frame " << f;
            EXPECT_EQ(got, want) << "frame " << f;
            held += has;
        }
    EXPECT_GT(held, 200u);
}

/// The timeline counts a range's writes from the index's running counts: the sum is every journaled memory write of
/// the session, and a query decodes at most a block per range end (before: the whole journal per query, 16 s at
/// 240,000 frames)
TEST_F(TTDSeekScaling_Folder_Test, TheTimelineCountsWritesWithoutDecodingTheJournal)
{
    Start(8);
    _emulator->RunNFrames(40);
    _ttd->StopRecording();
    const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    const uint64_t firstStart = engine.Checkpoint(engine.FirstCheckpoint())->start;
    uint64_t want = 0;
    engine.Writes().ForEach([&](const ttd::TTDWriteRecord& r) { want += r.globalT > firstStart && !r.isIo ? 1u : 0u; });
    ASSERT_GT(want, 40u * 1000u);

    const uint64_t decodedBefore = engine.Writes().BlocksDecoded();
    ttd::TTDTimelineOptions options;
    options.buckets = true;
    const ttd::TTDTimelineResult timeline = _ttd->QueryTimeline(options);
    ASSERT_TRUE(timeline.error.empty()) << timeline.error;
    uint64_t got = 0;
    for (const ttd::TTDTimelineBucket& b : timeline.buckets)
        got += b.writes;
    EXPECT_EQ(got, want);
    const uint64_t ranges = timeline.buckets.size() + timeline.phases.size();
    EXPECT_LE(engine.Writes().BlocksDecoded() - decodedBefore, 2 * ranges)
        << "of " << engine.Writes().Size() / ttd::kWriteBlockRecords << " blocks";
}

/// The timeline over segments that left memory reads none of them back: the change records stay, and a baseline's
/// content changes were noted at capture (before 2026-11-21 every evicted segment was read back per query: 2 s at
/// 240,000 frames). The noted changes are what decoding both versions finds
TEST_F(TTDSeekScaling_Folder_Test, TheTimelineReadsNoEvictedSegmentBack)
{
    Start(8);
    _emulator->RunNFrames(200);
    _ttd->StopRecording();
    const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    ASSERT_GT(engine.EvictedSegments(), 10u);
    const uint64_t loadsBefore = _ttd->GetSessionInfo().readBackLoads;
    const ttd::TTDTimelineResult timeline = _ttd->QueryTimeline(ttd::TTDTimelineOptions{});
    ASSERT_TRUE(timeline.error.empty()) << timeline.error;
    EXPECT_EQ(_ttd->GetSessionInfo().readBackLoads, loadsBefore) << "segments read back by the timeline";

    // Every baseline's noted changes against both versions decoded (the segments read back for it)
    size_t baselines = 0, changed = 0;
    std::vector<uint8_t> now(ttd::kTTDPieceSize), before(ttd::kTTDPieceSize);
    for (const ttd::TTDSegmentInfo& segment : engine.Segments())
    {
        const size_t index = segment.firstCheckpoint;
        if (index == 0 || index <= engine.FirstCheckpoint())
            continue;
        const std::vector<uint32_t>* diff = engine.BaselineDiff(index);
        ASSERT_NE(diff, nullptr) << "baseline " << index;
        std::vector<uint32_t> decoded;
        for (uint32_t r = 0; r < engine.Regions().size(); ++r)
            for (const auto& [piece, id] : engine.ChangesAt(index, r))
            {
                const uint32_t previous = engine.VersionAt(index - 1, r, piece);
                if (!(previous != ttd::TimeTravelEngine::kAbsent && engine.PieceStore().Decode(id, now.data()) &&
                      engine.PieceStore().Decode(previous, before.data()) && now == before))
                    decoded.push_back(r << 16 | piece);
            }
        EXPECT_EQ(*diff, decoded) << "baseline " << index;
        ++baselines;
        changed += decoded.size();
    }
    EXPECT_GT(baselines, 10u);
    EXPECT_GT(changed, baselines) << "the loop changes RAM every frame";
}

namespace
{
/// The machine a seek landed on: the CPU's registers and the 128 K of RAM
uint64_t MachineHash(EmulatorContext* context)
{
    const Z80* z80 = context->pCore->GetZ80();
    const uint16_t regs[] = {z80->pc, z80->sp, z80->af, z80->bc, z80->de, z80->hl, z80->ix, z80->iy};
    uint64_t h = ttd::HashBytes(reinterpret_cast<const uint8_t*>(regs), sizeof(regs));
    for (uint16_t page = 0; page < 8; ++page)
        h = ttd::HashCombine(h, context->pMemory->RAMPageAddress(page), PAGE_SIZE);
    return h;
}

/// Seeks into segments evicted to their files against seeks into the resident head segment: @p segmentFrames a
/// segment (its file in parts of 50 checkpoints), @p frames recorded, a one-segment read-back cache so each seek
/// into the past reads its segment back. Every seek is checked (the frame replayed against the next checkpoint), and
/// a frame reached by reading a stretch of its segment through the file's seek index lands on the machine reached
/// with the file read from its start and with the segment read whole
void SeekEvictedAgainstResident(Emulator* emulator, ttd::TimeTravelController* ttd, uint64_t segmentFrames,
                                uint64_t frames, int seeks, double& residentMs, double& evictedMs)
{
    EmulatorContext* context = emulator->GetContext();
    ttd->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("ttd-seek-evicted"));
    ttd->SetHistoryLimit(segmentFrames * 8, 1);   // the byte limit evicts every segment with a complete file
    ttd->SetReadBackCapacity(1);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(static_cast<int>(frames));
    const ttd::TTDTimePoint end = ttd->SessionEndPosition();
    ASSERT_TRUE(ttd->SeekTo({end.frame - 1, 0}));   // the pause: every completed file is out of memory
    const ttd::TimeTravelEngine& engine = ttd->GetEngine();
    const std::deque<ttd::TTDSegmentInfo>& segments = engine.Segments();
    ASSERT_GE(segments.size(), 3u);
    ASSERT_GE(engine.EvictedSegments(), 2u);
    const ttd::TTDSegmentInfo head = segments.back();

    ttd->SetCheckSeeks(true);
    std::mt19937 rng(0x5EE7);
    std::vector<double> resident, evicted;
    const uint64_t loads0 = ttd->GetSessionInfo().readBackLoads;
    const uint64_t windows0 = ttd->ReadBackWindowLoads();
    for (int i = 0; i < seeks; ++i)
    {
        // Resident: the head segment. Evicted: the segments before, in turn (the cache holds one)
        const uint64_t near = head.firstFrame + 1 + rng() % (end.frame - 2 - head.firstFrame);
        const size_t k = static_cast<size_t>(i) % (segments.size() - 1);
        const uint64_t span = engine.SegmentFrameSpan(0, k);
        const uint64_t far = segments[k].firstFrame + 1 + rng() % (span - 2);
        for (const auto& [frame, out] : {std::pair{near, &resident}, std::pair{far, &evicted}})
        {
            const Clock::time_point s0 = Clock::now();
            ASSERT_TRUE(ttd->SeekTo({frame, 0})) << "frame " << frame;
            out->push_back(Ms(s0, Clock::now()));
        }
        if (i < 4)
        {
            // The machine of the stretch read back equals the machine of the file read from its start, and of the
            // whole segment (without the seek index a second restore in the segment, past the part read, reads it
            // whole). Another segment first each time: the cache holds one
            EXPECT_FALSE(segments[k].Whole()) << "a stretch";
            const uint64_t stretch = MachineHash(context);
            const size_t other = (k + 1) % (segments.size() - 1);
            ttd->SetSegmentWindowReads(false);
            ASSERT_TRUE(ttd->SeekTo({segments[other].firstFrame + 1, 0}));
            ASSERT_TRUE(ttd->SeekTo({far, 0}));
            EXPECT_EQ(MachineHash(context), stretch) << "from the file's start: frame " << far << " of segment " << k;
            ASSERT_TRUE(ttd->SeekTo({segments[k].firstFrame + span - 1, 0}));
            EXPECT_TRUE(segments[k].Whole());
            ASSERT_TRUE(ttd->SeekTo({far, 0}));
            EXPECT_EQ(MachineHash(context), stretch) << "whole: frame " << far << " of segment " << k;
            ttd->SetSegmentWindowReads(true);
        }
    }
    const ttd::TTDSessionInfo info = ttd->GetSessionInfo();
    EXPECT_GE(info.readBackLoads - loads0, static_cast<uint64_t>(seeks)) << "each seek into the past reads back";
    EXPECT_GE(ttd->ReadBackWindowLoads() - windows0, static_cast<uint64_t>(seeks) - 4) << "through the seek index";
    EXPECT_EQ(info.readBackFailures, 0u);
    EXPECT_GE(info.seekChecks, static_cast<uint64_t>(2 * seeks));
    EXPECT_EQ(info.seekCheckDivergences, 0u);
    std::nth_element(resident.begin(), resident.begin() + resident.size() / 2, resident.end());
    std::nth_element(evicted.begin(), evicted.begin() + evicted.size() / 2, evicted.end());
    residentMs = resident[resident.size() / 2];
    evictedMs = evicted[evicted.size() / 2];
    std::fprintf(stderr, "segment %llu frames: seek median resident %.2f ms, evicted %.2f ms\n",
                 static_cast<unsigned long long>(segmentFrames), residentMs, evictedMs);
}
}  // namespace

/// A seek into history evicted to the disk tier costs about a seek in memory: the segment's file is read up to the
/// part holding the frame (and one more), not whole, and lands on the same machine (P1.S10 seek tails: 5-11 ms
/// against 1.5 ms before). Segments of 160 frames (four parts), 360 frames recorded; the bound is loose (CI under
/// load), the long tier measures segments of the harness's size. Runtime: about 0.8 s
TEST_F(TTDSeekScaling_Folder_Test, ASeekIntoEvictedHistoryCostsAboutASeekInMemory)
{
    _ttd->SetEnableWriteJournal(true);
    double resident = 0, evicted = 0;
    SeekEvictedAgainstResident(_emulator, _ttd, 160, 360, 12, resident, evicted);
    EXPECT_LT(evicted, resident * 3 + 2.0) << "resident " << resident << " ms";
}

/// Long tier: segments of 1,875 frames (the harness's 15,000-frame window, 38 parts), 4,000 frames recorded.
/// Runtime: about 10 s
TEST_F(TTDSeekScaling_Folder_Test, ASeekIntoEvictedHistoryLong)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";
    _ttd->SetEnableWriteJournal(true);
    double resident = 0, evicted = 0;
    SeekEvictedAgainstResident(_emulator, _ttd, 1875, 4000, 20, resident, evicted);
    EXPECT_LT(evicted, resident * 2.5 + 1.5) << "resident " << resident << " ms";
}

/// A pause does not wait for the disk: the folder's open file is laid out and written behind it (a loaded host's
/// writer backlog made pauses of 6-118 ms, 2-92 of them the wait, 2026-11-24). Here the writer threads write nothing
/// until the test lets them: the pause returns with the file still being written, a resume waits for it, and the
/// next pause's file and a stop after a pause end whole: the folder loads back every frame
TEST_F(TTDSeekScaling_Folder_Test, APauseDoesNotWaitForTheDisk)
{
    Start(100);
    _emulator->RunNFrames(250);
    std::atomic<bool> disk{false};
    ttd::TTDSessionWriter::SetWriteGateForTesting(&disk);
    const ttd::TTDTimePoint end = _ttd->SessionEndPosition();
    ASSERT_TRUE(_ttd->SeekTo({end.frame - 1, 0}));   // the pause: returns with its file not written
    EXPECT_TRUE(_ttd->ShadowFilesFinishing()) << "the pause's file is written behind it";
    ASSERT_TRUE(_ttd->SeekTo({end.frame - 50, 0}));   // browsing goes on meanwhile
    EXPECT_TRUE(_ttd->ShadowFilesFinishing());
    disk = true;
    ttd::TTDSessionWriter::SetWriteGateForTesting(nullptr);
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->SessionEndPosition()));   // waits for the paused file
    EXPECT_FALSE(_ttd->ShadowFilesFinishing());
    _emulator->RunNFrames(120);
    const ttd::TTDTimePoint end2 = _ttd->SessionEndPosition();
    ASSERT_TRUE(_ttd->SeekTo({end2.frame - 1, 0}));   // a second pause
    _ttd->StopRecording();   // a stop after a pause: the files whole on return
    EXPECT_FALSE(_ttd->ShadowFilesFinishing());

    const std::vector<std::string> files = TrunkFiles(_ttd->ShadowRecordingFolder());
    ASSERT_GE(files.size(), 2u);
    ttd::TimeTravelEngine loaded;
    std::string error;
    ttd::TTDSessionLoadReport report;
    ASSERT_TRUE(ttd::LoadRecording(loaded, files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    EXPECT_EQ(loaded.CheckpointCount(), _ttd->GetEngine().CheckpointCount()) << "every checkpoint reached the disk";
}

/// Long tier: a folder recording with the write journal at N and 4N frames (segments of 250 frames). The first seek
/// of a browse (the pause) and the seeks after it may not follow the length: medians, a loose bound (CI runs
/// under load). Runtime: 2,000 frames of recording, about 1 s
TEST_F(TTDSeekScaling_Folder_Test, LongSession)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1)";
    Start(250);
    auto measure = [&](uint64_t frames, double& pauseMs, double& seekMs) {
        // The least of 3 pauses, 100 frames of recording apart: one pause is one sample, and a loaded host stalls
        // a sample now and then (2 against 92 ms at load 52 on 20 cores, with the pause no longer waiting for the disk)
        _emulator->RunNFrames(static_cast<int>(frames - 200));
        ttd::TTDTimePoint end;
        pauseMs = 1e9;
        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->SessionEndPosition()));
                _emulator->RunNFrames(100);
            }
            end = _ttd->SessionEndPosition();
            const Clock::time_point p0 = Clock::now();
            ASSERT_TRUE(_ttd->SeekTo({end.frame - 1, 0}));
            pauseMs = std::min(pauseMs, Ms(p0, Clock::now()));
        }
        const uint64_t first = _ttd->GetEarliestRecordedFrame();
        std::mt19937 rng(9);
        std::vector<double> seeks;
        for (int i = 0; i < 40; ++i)
        {
            const uint64_t frame = first + rng() % (end.frame - first);
            const Clock::time_point s0 = Clock::now();
            ASSERT_TRUE(_ttd->SeekTo({frame, 0}));
            seeks.push_back(Ms(s0, Clock::now()));
        }
        // The median: the fastest seek of a short history can be next to no work (0.11 ms against 0.5)
        std::nth_element(seeks.begin(), seeks.begin() + seeks.size() / 2, seeks.end());
        seekMs = seeks[seeks.size() / 2];
        ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->SessionEndPosition()));
    };
    double pause1 = 0, seek1 = 0, pause4 = 0, seek4 = 0;
    measure(500, pause1, seek1);
    measure(1500, pause4, seek4);   // 2,000 frames: 4x
    std::printf("[ scaling  ] pause (least of 3) %.2f -> %.2f ms, seek (median of 40) %.3f -> %.3f ms\n", pause1, pause4, seek1, seek4);
    EXPECT_LT(pause4, pause1 * 2.0 + 20.0) << "the pause follows the history";
    EXPECT_LT(seek4, seek1 * 1.5 + 0.5) << "the seek follows the history";
    const Clock::time_point t0 = Clock::now();
    const ttd::TTDTimelineResult timeline = _ttd->QueryTimeline(ttd::TTDTimelineOptions{});
    EXPECT_TRUE(timeline.error.empty()) << timeline.error;
    EXPECT_LT(Ms(t0, Clock::now()), 1000.0) << "the whole session's timeline";
    _ttd->StopRecording();
}

namespace
{
/// One recording measured at each length (TTDSeekScaling_Test.Measure): @p ab the side's switches
void MeasureSide(const std::string& side, const std::string& ab, int round, const std::vector<uint64_t>& allLengths)
{
    auto abHas = [&](const char* name) { return ("," + ab + ",").find(std::string(",") + name + ",") != std::string::npos; };
    /// "<name>=<value>" among the switches ("" when absent)
    auto abValue = [&](const char* name) {
        const std::string key = std::string(",") + name + "=";
        const size_t at = ("," + ab).find(key);
        if (at == std::string::npos)
            return std::string();
        const std::string rest = ab.substr(at + key.size() - 1);
        return rest.substr(0, rest.find(','));
    };
    // len=<a>:<b>:... the side's own lengths (a mechanism measured at two lengths beside sides measured at four)
    std::vector<uint64_t> lengths = allLengths;
    if (const std::string own = abValue("len"); !own.empty())
    {
        lengths.clear();
        std::stringstream ss(own);
        std::string item;
        while (std::getline(ss, item, ':'))
            if (!item.empty())
                lengths.push_back(std::strtoull(item.c_str(), nullptr, 10));
        std::sort(lengths.begin(), lengths.end());
    }
    const bool journal = EnvOn("UNREAL_TTD_SEEK_JOURNAL", true);
    const bool coverage = EnvOn("UNREAL_TTD_SEEK_COVERAGE", false) || abHas("coverage");
    const bool folder = EnvOn("UNREAL_TTD_SEEK_FOLDER", true);
    const char* limitEnv = std::getenv("UNREAL_TTD_SEEK_LIMIT");
    const uint64_t limit = limitEnv ? std::strtoull(limitEnv, nullptr, 10) : 15000;
    const char* samplesEnv = std::getenv("UNREAL_TTD_SEEK_SAMPLES");
    const int samples = !abValue("samples").empty() ? std::atoi(abValue("samples").c_str()) : samplesEnv ? std::atoi(samplesEnv) : 40;
    // The side's switches, each one mechanism as it was before it:
    //   nowindow         evicted segments are read from their file's start (no seek index used)
    //   noindex          segment files are written without piece tables and seek index
    //   packedpieces     segment files store their piece records compressed (zstd), as the other records
    //   nobaselinereuse  a baseline stores every piece anew
    //   stride=<n>       the piece store's skip stride (1: every version builds on the previous one)
    //   walkheap         the journals' size getters walk every block (and check the running sums)
    //   coverage         the coverage index on (as UNREAL_TTD_SEEK_COVERAGE=1, for this side)
    //   len=<a>:<b>      the side's own session lengths; samples=<n> its own seeks per kind; rounds=<n> its rounds
    const std::string stride = abValue("stride");
    ttd::TTDPieceStore::SetDefaultSkipStride(stride.empty() ? uint16_t{7} : static_cast<uint16_t>(std::atoi(stride.c_str())));
    ttd::TimeTravelEngine::SetBaselineReuse(!abHas("nobaselinereuse"));
    ttd::TTDSessionWriter::SetRawPieceRecords(!abHas("packedpieces"));
    ttd::TTDWriteIndex::SetWalkBlocksForMeasurement(abHas("walkheap"));

    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ASSERT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    emulator->RunNFrames(50);

    const char* rootEnv = std::getenv("UNREAL_TTD_SEEK_ROOT");
    const std::string root = (rootEnv ? std::string(rootEnv) : TestPathHelper::GetUniqueTestScratchPath("ttd-seek-scaling")) +
                             (side.empty() ? std::string() : "/" + side + "-" + std::to_string(round));
    if (folder)
        ttd->SetShadowRecordingRoot(root);
    ttd->SetHistoryLimit(limit, 0);
    ttd->SetEnableWriteJournal(journal);
    ttd->SetEnableCoverageIndex(coverage);
    ttd->SetSegmentWindowReads(!abHas("nowindow"));
    ttd->SetSegmentSeekIndex(!abHas("noindex"));
    ASSERT_TRUE(ttd->StartRecording());
    const uint64_t start = context->emulatorState.frame_counter;
    std::fprintf(stderr, "# side=%s round=%d load=%.0f journal=%d coverage=%d folder=%d limit=%llu ab=[%s] root=%s\n",
                 side.empty() ? "-" : side.c_str(), round, HostLoad(), journal, coverage, folder,
                 static_cast<unsigned long long>(limit), ab.c_str(), root.c_str());

    auto* keys = context->pDebugManager->GetKeyboardManager();
    std::mt19937 rng(0x5EEC);
    const ZXKeysEnum gameKeys[] = {ZXKEY_Q, ZXKEY_A, ZXKEY_O, ZXKEY_P, ZXKEY_SPACE, ZXKEY_M, ZXKEY_1};
    uint64_t recorded = 0;
    const Clock::time_point runStart = Clock::now();
    for (const uint64_t length : lengths)
    {
        // Record up to the length: a key held for a few frames now and then
        while (recorded < length)
        {
            const uint64_t chunk = std::min<uint64_t>(length - recorded, 25 + rng() % 50);
            const ZXKeysEnum key = gameKeys[rng() % (sizeof(gameKeys) / sizeof(gameKeys[0]))];
            keys->PressKey(key);
            emulator->RunNFrames(static_cast<int>(std::min<uint64_t>(chunk, 8)));
            keys->ReleaseKey(key);
            if (chunk > 8)
                emulator->RunNFrames(static_cast<int>(chunk - 8));
            recorded += chunk;
        }
        const double recordS = Ms(runStart, Clock::now()) / 1000.0;

        const ttd::TTDTimePoint end = ttd->SessionEndPosition();
        const uint64_t first = ttd->GetEarliestRecordedFrame();
        const uint32_t span = ttd->FrameSpan();

        // The first seek of a cycle pauses the recording for browsing
        const Clock::time_point p0 = Clock::now();
        ASSERT_TRUE(ttd->SeekTo({end.frame - 1, 0}));
        const double pauseSeekMs = Ms(p0, Clock::now());
        uint64_t folderBytes = 0, newestFile = 0;
        if (folder)
        {
            std::error_code ec;
            std::filesystem::file_time_type newest{};
            for (const auto& entry : std::filesystem::directory_iterator(ttd->ShadowRecordingFolder(), ec))
                if (entry.is_regular_file(ec))
                {
                    folderBytes += entry.file_size(ec);
                    if (entry.last_write_time(ec) >= newest)
                    {
                        newest = entry.last_write_time(ec);
                        newestFile = entry.file_size(ec);
                    }
                }
        }

        struct Kind
        {
            const char* name;
            bool far;
            bool mid;
            bool step = false;   ///< frame by frame back from a random frame of the window (stepping, scrubbing)
            bool evicted = false;   ///< only frames whose segment left memory (far: the whole session, the window too)
        };
        const Kind kinds[] = {{"near/frame", false, false},      {"near/mid", false, true},
                              {"far/frame", true, false},        {"far/mid", true, true},
                              {"step/back", false, false, true}, {"evicted/frame", true, false, false, true},
                              {"evicted/mid", true, true, false, true}};
        // The frames in memory: the window's segments (an eighth of the limit each, one more held while it fills)
        const uint64_t windowLo = end.frame > limit ? end.frame - limit : first;
        const uint64_t evictedHi = windowLo > first + limit / 4 ? windowLo - limit / 4 : first;
        for (const Kind& k : kinds)
        {
            if (k.evicted && (evictedHi <= first || ttd->GetEngine().EvictedSegments() == 0))
                continue;
            std::vector<double> total, restore, replay, present, other, readBack, seekRestore, decode, pieces, links,
                parts, imported, readKb;
            uint64_t previous = 0;
            const uint64_t loads0 = ttd->GetSessionInfo().readBackLoads;
            const uint64_t windowLoads0 = ttd->ReadBackWindowLoads();
            for (int i = 0; i < samples; ++i)
            {
                const uint64_t lo = k.far ? first : windowLo;
                const uint64_t hi = k.evicted ? evictedHi : end.frame;
                const uint64_t frame = k.step && i > 0 && previous > lo ? previous - 1
                                                                        : lo + rng() % std::max<uint64_t>(1, hi - lo);
                previous = frame;
                const uint32_t t = k.mid ? 1 + static_cast<uint32_t>(rng() % (span - 1)) : 0;
                const uint64_t readBack0 = ttd->GetPerfCounters().readBackNs;
                const uint64_t windows0 = ttd->ReadBackWindowLoads();
                const Clock::time_point s0 = Clock::now();
                ttd::TTDSeekResult result;
                const bool ok = ttd->SeekTo({frame, t}, &result);
                const double ms = Ms(s0, Clock::now());
                EXPECT_TRUE(ok || result.haltReason == ttd::TTDSeekHaltReason::ExternalEvent)
                    << "frame " << frame << " t " << t;
                const ttd::TTDPerfCounters& perf = ttd->GetPerfCounters();
                total.push_back(ms);
                restore.push_back(static_cast<double>(perf.lastRestoreTotalNs()) / 1e6);
                replay.push_back(static_cast<double>(perf.lastReplayNs) / 1e6);
                present.push_back(static_cast<double>(perf.lastPresentNs) / 1e6);
                other.push_back(ms - present.back() - replay.back());
                readBack.push_back(static_cast<double>(perf.readBackNs - readBack0) / 1e6);
                seekRestore.push_back(static_cast<double>(perf.lastSeekRestoreNs) / 1e6 - readBack.back());
                decode.push_back(static_cast<double>(perf.lastSeekMemoryNs) / 1e6 - readBack.back());
                pieces.push_back(static_cast<double>(perf.lastSeekPiecesDecoded));
                links.push_back(static_cast<double>(perf.lastSeekLinksDecoded));
                if (ttd->ReadBackWindowLoads() != windows0)
                {
                    const ttd::TTDSegmentWindowScratch::Work& work = ttd->GetEngine().LastWindowWork();
                    parts.push_back(work.partsRead);
                    imported.push_back(work.versionsImported);
                    readKb.push_back(static_cast<double>(work.bytesRead) / 1024.0);
                }
            }
            std::fprintf(stderr,
                         "len=%llu %-13s total p50 %.2f p95 %.2f p99 %.2f max %.2f | seek-part p50 %.2f max %.2f | replay p50 "
                         "%.2f | present p50 %.2f max %.2f | last-restore p50 %.2f ms\n",
                         static_cast<unsigned long long>(length), k.name, Pct(total, 0.5), Pct(total, 0.95),
                         Pct(total, 0.99), Pct(total, 1.0), Pct(other, 0.5), Pct(other, 1.0), Pct(replay, 0.5),
                         Pct(present, 0.5), Pct(present, 1.0), Pct(restore, 0.5));
            // The seek's own restore: the read back from the disk tier, the memory decode, the rest (devices, CPU,
            // screen); what is left of the seek part is the session's publish and the bookkeeping
            std::fprintf(stderr,
                         "    split %-13s read back p50 %.2f p95 %.2f (loads %llu, by index %llu) | decode p50 %.2f | restore "
                         "rest p50 %.2f | outside restore p50 %.2f ms | pieces p50 %.0f links p50 %.0f p95 %.0f",
                         k.name, Pct(readBack, 0.5), Pct(readBack, 0.95),
                         static_cast<unsigned long long>(ttd->GetSessionInfo().readBackLoads - loads0),
                         static_cast<unsigned long long>(ttd->ReadBackWindowLoads() - windowLoads0), Pct(decode, 0.5),
                         Pct(seekRestore, 0.5) - Pct(decode, 0.5), Pct(other, 0.5) - Pct(seekRestore, 0.5) - Pct(readBack, 0.5),
                         Pct(pieces, 0.5), Pct(links, 0.5), Pct(links, 0.95));
            if (!parts.empty())
                std::fprintf(stderr, " | by index: parts p50 %.0f p95 %.0f, versions p50 %.0f, read p50 %.0f KB",
                             Pct(parts, 0.5), Pct(parts, 0.95), Pct(imported, 0.5), Pct(readKb, 0.5));
            std::fprintf(stderr, "\n");
        }

        // UNREAL_TTD_SEEK_LOOP=<kind>:<seconds> at the last length: that kind's seeks over and over, for a profiler
        if (const char* loop = std::getenv("UNREAL_TTD_SEEK_LOOP"); loop && length == lengths.back())
        {
            const std::string spec = loop;
            const size_t colon = spec.find(':');
            const std::string kindName = spec.substr(0, colon);
            const double seconds = colon == std::string::npos ? 20.0 : std::atof(spec.c_str() + colon + 1);
            const Kind* kind = &kinds[0];
            for (const Kind& k : kinds)
                if (kindName == k.name)
                    kind = &k;
            std::fprintf(stderr, "# loop %s for %.0f s (pid %d)\n", kind->name, seconds, static_cast<int>(getpid()));
            size_t n = 0;
            const Clock::time_point l0 = Clock::now();
            while (Ms(l0, Clock::now()) < seconds * 1000.0)
            {
                const uint64_t lo = kind->far ? first : windowLo;
                const uint64_t hi = kind->evicted ? evictedHi : end.frame;
                const uint64_t frame = lo + rng() % std::max<uint64_t>(1, hi - lo);
                if (kindName == "timeline")
                    (void)ttd->QueryTimeline(ttd::TTDTimelineOptions{});
                else
                    ttd->SeekTo({frame, kind->mid ? 1 + static_cast<uint32_t>(rng() % (span - 1)) : 0});
                ++n;
            }
            std::fprintf(stderr, "# loop done: %zu seeks\n", n);
        }

        const Clock::time_point tl0 = Clock::now();
        ttd::TTDTimelineOptions options;
        const ttd::TTDTimelineResult timeline = ttd->QueryTimeline(options);
        const double timelineMs = Ms(tl0, Clock::now());
        // Again: the coverage block summaries are built by the first query
        const Clock::time_point tl1 = Clock::now();
        (void)ttd->QueryTimeline(options);
        const double timelineAgainMs = Ms(tl1, Clock::now());
        options.ports = false;
        const Clock::time_point tl2 = Clock::now();
        (void)ttd->QueryTimeline(options);
        const double timelineNoPortsMs = Ms(tl2, Clock::now());
        std::fprintf(stderr, "len=%llu timeline first %.1f ms, again %.1f ms, without ports %.1f ms\n",
                     static_cast<unsigned long long>(length), timelineMs, timelineAgainMs, timelineNoPortsMs);

        const ttd::TTDSessionInfo info = ttd->GetSessionInfo();
        const ttd::TTDEngineHeapBreakdown heap = ttd->GetEngine().HeapBreakdown();
        std::fprintf(stderr,
                     "len=%llu load=%.0f recorded in %.1f s; pause+seek %.2f ms; timeline %.1f ms (%s, %zu phases); write records "
                     "%llu; evicted segments %zu; read back %zu; heap %.1f MB (write journal %.1f MB); folder %.1f MB, newest file %.1f MB\n",
                     static_cast<unsigned long long>(length), HostLoad(), recordS, pauseSeekMs, timelineMs,
                     timeline.error.empty() ? "ok" : timeline.error.c_str(), timeline.phases.size(),
                     static_cast<unsigned long long>(info.writeJournalRecords), ttd->GetEngine().EvictedSegments(), ttd->GetEngine().ReadBackSegments(),
                     static_cast<double>(info.sessionHeapBytes) / 1e6, static_cast<double>(heap.writeJournal) / 1e6,
                     static_cast<double>(folderBytes) / 1e6, static_cast<double>(newestFile) / 1e6);

        // An evicted segment's file by stream: what the seek data adds, what the container's compression saves
        if (folder && length == lengths.back())
        {
            const std::vector<std::string> files = TrunkFiles(ttd->ShadowRecordingFolder());
            if (files.size() > 2)
            {
                const std::string& path = files[files.size() / 2];
                ttd::TTDFileSource source(path);
                ttd::TTDContainerReader reader;
                std::string error;
                if (source.Valid() && reader.Open(source, error))
                {
                    std::map<uint16_t, std::pair<uint64_t, uint64_t>> streams;   // stored, raw
                    for (const ttd::TTDPartRef& part : reader.Parts())
                        for (const ttd::TTDRecordRef& record : part.physical)
                        {
                            streams[record.streamId].first += record.storedSize;
                            streams[record.streamId].second += record.rawSize;
                        }
                    std::fprintf(stderr, "file %s: %llu bytes, %zu parts, index extra %zu bytes; streams (stored/raw KB):",
                                 std::filesystem::path(path).filename().string().c_str(),
                                 static_cast<unsigned long long>(source.Size()), reader.Parts().size(),
                                 reader.IndexExtra().size());
                    for (const auto& [id, sizes] : streams)
                        std::fprintf(stderr, " %u=%.1f/%.1f", id, static_cast<double>(sizes.first) / 1024.0,
                                     static_cast<double>(sizes.second) / 1024.0);
                    std::fprintf(stderr, "\n");
                }
            }
            const ttd::TTDEngineHeapBreakdown hb = ttd->GetEngine().HeapBreakdown();
            std::fprintf(stderr,
                         "len=%llu engine heap: piece payload %.1f MB, versions %.1f MB (%zu live), arena slack %.1f MB, delta "
                         "base %.2f MB, tables %.1f MB\n",
                         static_cast<unsigned long long>(length), static_cast<double>(hb.piecePayload) / 1e6,
                         static_cast<double>(hb.pieceVersions) / 1e6, ttd->GetEngine().PieceStore().LiveVersions(),
                         static_cast<double>(hb.arenaSlack) / 1e6, static_cast<double>(hb.deltaBase) / 1e6,
                         static_cast<double>(hb.referenceTables) / 1e6);
        }

        // Back to the end, recording goes on (as the soak did after each cycle)
        ASSERT_TRUE(ttd->ResumeRecordingFrom(ttd->SessionEndPosition()));
        (void)start;
    }
    ttd->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
    // The switches back to the shipped state
    ttd::TTDPieceStore::SetDefaultSkipStride(7);
    ttd::TimeTravelEngine::SetBaselineReuse(true);
    ttd::TTDSessionWriter::SetRawPieceRecords(true);
    ttd::TTDWriteIndex::SetWalkBlocksForMeasurement(false);
}
}  // namespace

/// The seek harness. UNREAL_TTD_SEEK_SCALING gives the lengths. One recording with the switches of UNREAL_TTD_SEEK_AB,
/// or an A/B in one process: UNREAL_TTD_SEEK_SIDES=<name>=<switches>;<name>=<switches>;... ("-": no switch) measured
/// UNREAL_TTD_SEEK_ROUNDS times, the sides one after another, their order reversed every other round. Each
/// recording's lines follow its "# side=<name> round=<n>" line
TEST(TTDSeekScaling_Test, Measure)
{
    const std::vector<uint64_t> lengths = Lengths();
    if (lengths.empty())
        GTEST_SKIP() << "diagnostic: set UNREAL_TTD_SEEK_SCALING=10000,50000,...";
    std::vector<std::pair<std::string, std::string>> sides;
    if (const char* env = std::getenv("UNREAL_TTD_SEEK_SIDES"))
    {
        std::stringstream ss(env);
        std::string item;
        while (std::getline(ss, item, ';'))
        {
            const size_t eq = item.find('=');
            if (item.empty() || eq == std::string::npos)
                continue;
            const std::string switches = item.substr(eq + 1);
            sides.emplace_back(item.substr(0, eq), switches == "-" ? std::string() : switches);
        }
    }
    if (sides.empty())
        sides.emplace_back(std::string(), std::getenv("UNREAL_TTD_SEEK_AB") ? std::getenv("UNREAL_TTD_SEEK_AB") : "");
    const char* roundsEnv = std::getenv("UNREAL_TTD_SEEK_ROUNDS");
    const int rounds = roundsEnv ? std::max(1, std::atoi(roundsEnv)) : 1;
    for (int round = 1; round <= rounds; ++round)
        for (size_t k = 0; k < sides.size(); ++k)
        {
            const auto& [name, ab] = sides[round % 2 == 0 ? sides.size() - 1 - k : k];
            // rounds=<n> among a side's switches: the side is measured in the first n rounds only
            if (const size_t at = ("," + ab).find(",rounds="); at != std::string::npos && round > std::atoi(ab.c_str() + at + 7))
                continue;
            ASSERT_NO_FATAL_FAILURE(MeasureSide(name, ab, round, lengths)) << "side " << name << " round " << round;
        }
}
