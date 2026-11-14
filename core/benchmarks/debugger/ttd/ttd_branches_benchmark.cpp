// ttd_branches_benchmark.cpp - TTD branches, P1 (docs/inprogress/2026-11-08-ttd-branches/test-strategy.md §5):
//
//   B-2  branch switch, memory-resident, against a seek of the same distance on one line
//        (pass: switch p99 <= seek p99 x 2)
//   B-6  lookups: position -> owner at lineage depth 1 / 10 / 100; select by tag over 10 000 branches
//        (pass: microseconds, flat in session length)
//   B-7  lineage queries (find-last, reverse-continue) at depth 1 and 10 against a linear session of the same
//        length (pass: <= 10 % over the linear session at depth 10)
//   B-5  demux (storage.md §4.1): find and read one branch in a recording of 1000 branches; one file with the
//        branches' parts mixed (layout a, the folder joined) against the folder's branches.tdx and the branch's
//        own files (b + c) (pass: b + c no slower than a)
//   B-9  save all branches / load / save lineage of a 30-minute session with 100 branches (pass: measured; the
//        load <= 1 s, PR-8)
//   B-4  analyzer workload: 1000 experiments of 200 frames from one point, each a branch-create with a poke, an
//        input script, a run, a read and go_back (the analyzer's kept experiment, l3experimentbranch.h), then one
//        prune by the run id (pass: measured - throughput, memory, disk bytes and files per branch)
//
// Run with:  ./core-benchmarks --benchmark_filter="TTD_Branch"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "debugger/ttd/engine/ttdbranchtable.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "loaders/benchmark_path_helper.h"
#include "debugger/ttd/ttdrecordingfolders.h"

namespace
{
struct BranchFixture
{
    Emulator* emulator = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    bool Init()
    {
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            return false;
        EmulatorContext* context = emulator->GetContext();
        ttd = context ? context->pTimeTravelController : nullptr;
        if (!ttd)
            return false;
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        return true;
    }

    uint64_t Frame() const { return ttd->CurrentPosition().frame; }

    /// A linear session of @p frames
    void Linear(uint32_t frames)
    {
        ttd->InvalidateSession("benchmark reset");
        ttd->StartRecording();
        emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        ttd->StopRecording();
    }

    /// A lineage of @p frames in all, forked @p depth times at even steps: each fork leaves one frame of the
    /// parent's own after it (an old future), the lineage goes on from the fork
    void Lineage(uint32_t frames, uint32_t depth)
    {
        ttd->InvalidateSession("benchmark reset");
        ttd->StartRecording();
        const uint64_t start = Frame();
        const uint32_t step = frames / (depth + 1);
        emulator->RunNFrames(step + 1, /*skipBreakpoints=*/true);
        for (uint32_t k = 1; k <= depth; ++k)
        {
            const uint64_t at = start + uint64_t(step) * k;
            ttd->SeekTo({at, 0});
            ttd->ResumeRecordingFrom({at, 0});
            const uint32_t next = k == depth ? frames - step * k : step + 1;
            emulator->RunNFrames(next, /*skipBreakpoints=*/true);
        }
        ttd->StopRecording();
    }
};

BranchFixture g_fixture;
bool g_ready = false;

bool EnsureFixture()
{
    if (!g_ready)
        g_ready = g_fixture.Init();
    return g_ready;
}

/// p50 / p99 of the timed calls, in microseconds
void Percentiles(benchmark::State& state, std::vector<double>& us)
{
    if (us.empty())
        return;
    std::sort(us.begin(), us.end());
    state.counters["p50_us"] = us[us.size() / 2];
    state.counters["p99_us"] = us[std::min(us.size() - 1, us.size() * 99 / 100)];
}

template <typename F>
double TimeUs(F&& f)
{
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

// ---- B-2 --------------------------------------------------------------------------------------------------------

/// A switch between the trunk and a branch forked at frame 100 of 200, both at frame 150 (+ the fixed offset)
void BM_TTD_Branch_Switch(benchmark::State& state)
{
    if (!EnsureFixture())
    {
        state.SkipWithError("fixture");
        return;
    }
    BranchFixture& f = g_fixture;
    f.ttd->InvalidateSession("benchmark reset");
    f.ttd->StartRecording();
    const uint64_t start = f.Frame();
    f.emulator->RunNFrames(200, true);
    f.ttd->SeekTo({start + 100, 0});
    f.ttd->ResumeRecordingFrom({start + 100, 0});
    f.emulator->RunNFrames(100, true);
    f.ttd->StopRecording();
    const uint32_t branch = f.ttd->ActiveBranch();
    const ttd::TTDTimePoint at{start + 150 + static_cast<uint64_t>(state.range(0)), 0};

    std::vector<double> us;
    std::string error;
    bool toTrunk = true;
    for (auto _ : state)
    {
        const uint32_t target = toTrunk ? ttd::kTTDTrunk : branch;
        us.push_back(TimeUs([&] { benchmark::DoNotOptimize(f.ttd->SwitchBranch(target, at, error)); }));
        toTrunk = !toTrunk;
    }
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_Switch)->Arg(0)->Arg(7)->Iterations(200)->Unit(benchmark::kMicrosecond);

/// The baseline: seeks on one line between two positions 100 frames apart, at the switch's offsets in their frames
void BM_TTD_Branch_SeekBaseline(benchmark::State& state)
{
    if (!EnsureFixture())
    {
        state.SkipWithError("fixture");
        return;
    }
    BranchFixture& f = g_fixture;
    f.Linear(300);
    const uint64_t start = f.ttd->GetEarliestRecordedFrame();
    const uint64_t offset = static_cast<uint64_t>(state.range(0));
    std::vector<double> us;
    bool first = true;
    for (auto _ : state)
    {
        const ttd::TTDTimePoint at{start + (first ? 150 : 250) + offset, 0};
        us.push_back(TimeUs([&] { benchmark::DoNotOptimize(f.ttd->SeekTo(at)); }));
        first = !first;
    }
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_SeekBaseline)->Arg(0)->Arg(7)->Iterations(200)->Unit(benchmark::kMicrosecond);

// ---- B-6 --------------------------------------------------------------------------------------------------------

/// The owner of a position on a lineage of @p depth forks (100 frames each); range(1) scales the session length
void BM_TTD_Branch_OwnerLookup(benchmark::State& state)
{
    const uint32_t depth = static_cast<uint32_t>(state.range(0));
    const uint64_t span = 100 * static_cast<uint64_t>(state.range(1));
    ttd::TTDBranchTable table;
    std::string error;
    uint32_t tip = ttd::kTTDTrunk;
    for (uint32_t k = 1; k <= depth; ++k)
    {
        table.SetEnd(tip, {tip, span * k + span / 2, 0});
        ttd::TTDBranchMeta meta;
        meta.name = "b" + std::to_string(k);
        tip = *table.Create(tip, {tip, span * k, 0}, ttd::TTDNewGuid(), meta, error);
    }
    table.SetEnd(tip, {tip, span * (depth + 1), 0});
    const std::vector<ttd::TTDLineageSpan> lineage = table.Lineage(tip);
    uint64_t frame = 0;
    const uint64_t total = span * (depth + 1);
    for (auto _ : state)
    {
        frame = (frame + 7919) % total;
        benchmark::DoNotOptimize(ttd::TTDBranchTable::OwnerOf(lineage, {tip, frame, 0}));
    }
}
BENCHMARK(BM_TTD_Branch_OwnerLookup)
    ->Args({1, 1})->Args({10, 1})->Args({100, 1})->Args({10, 100})->Args({100, 100})
    ->Unit(benchmark::kNanosecond);

/// The lineage itself (what a switch or a query builds once)
void BM_TTD_Branch_Lineage(benchmark::State& state)
{
    const uint32_t depth = static_cast<uint32_t>(state.range(0));
    ttd::TTDBranchTable table;
    std::string error;
    uint32_t tip = ttd::kTTDTrunk;
    for (uint32_t k = 1; k <= depth; ++k)
    {
        table.SetEnd(tip, {tip, 100ull * k + 50, 0});
        tip = *table.Create(tip, {tip, 100ull * k, 0}, ttd::TTDNewGuid(), ttd::TTDBranchMeta{}, error);
    }
    for (auto _ : state)
        benchmark::DoNotOptimize(table.Lineage(tip));
}
BENCHMARK(BM_TTD_Branch_Lineage)->Arg(1)->Arg(10)->Arg(100)->Unit(benchmark::kNanosecond);

/// Select by tag / creator over 10 000 branches of the trunk, one in 100 tagged
void BM_TTD_Branch_SelectByTag(benchmark::State& state)
{
    ttd::TTDBranchTable table;
    std::string error;
    table.SetEnd(ttd::kTTDTrunk, {ttd::kTTDTrunk, 20000, 0});
    for (uint32_t k = 0; k < 10000; ++k)
    {
        ttd::TTDBranchMeta meta;
        meta.createdBy = k % 2 ? "mcp:claude" : "analyzer";
        if (k % 100 == 0)
            meta.tags = {"keep"};
        meta.name = "x" + std::to_string(k);
        table.Create(ttd::kTTDTrunk, {ttd::kTTDTrunk, 1 + k, 0}, ttd::TTDNewGuid(), meta, error);
    }
    ttd::TTDBranchFilter filter;
    if (state.range(0) == 0)
        filter.tags = {"keep"};
    else
        filter.createdBy = "mcp";
    size_t found = 0;
    for (auto _ : state)
    {
        const std::vector<uint32_t> ids = table.Select(filter);
        found = ids.size();
        benchmark::DoNotOptimize(ids.data());
    }
    state.counters["found"] = static_cast<double>(found);
}
BENCHMARK(BM_TTD_Branch_SelectByTag)->Arg(0)->Arg(1)->Unit(benchmark::kMicrosecond);

// ---- B-7 --------------------------------------------------------------------------------------------------------

/// range(0): 0 linear, else the lineage depth; range(1): 0 find-last of a write that never happens, 1 reverse-continue
/// to a PC that never runs (both walk the whole lineage back to the session start)
void BM_TTD_Branch_LineageQuery(benchmark::State& state)
{
    if (!EnsureFixture())
    {
        state.SkipWithError("fixture");
        return;
    }
    BranchFixture& f = g_fixture;
    const uint32_t frames = 200;
    const uint32_t depth = static_cast<uint32_t>(state.range(0));
    if (depth == 0)
        f.Linear(frames);
    else
        f.Lineage(frames, depth);
    const ttd::TTDTimePoint end = f.ttd->SessionEndPosition();
    ttd::TTDSearchQuery q;
    q.access = ttd::TTDAccessType::Write;
    q.hasPcFilter = true;              // writes by code at 0x3D00 (the font, never run): a miss over all of history
    q.pcFrom = q.pcTo = 0x3D00;
    const bool reverse = state.range(1) == 1;
    std::vector<double> us;
    bool matched = false;
    ttd::TTDSearchWindow window;
    for (auto _ : state)
    {
        state.PauseTiming();
        f.ttd->SeekTo(end);
        state.ResumeTiming();
        us.push_back(TimeUs([&] {
            if (reverse)
            {
                const auto r = f.ttd->ReverseContinue(std::vector<uint16_t>{0x3D00});
                matched = r.matched;
                window = r.window;
            }
            else
                matched = f.ttd->FindLastAccess(q, nullptr, &window).has_value();
        }));
    }
    // Every variant must walk back to the session start and find nothing: "from" is the frames before the end
    state.counters["branches"] = static_cast<double>(f.ttd->Branches().Count());
    state.counters["matched"] = matched ? 1 : 0;
    state.counters["walked"] = static_cast<double>(end.frame - window.from.frame);
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_LineageQuery)
    ->Args({0, 0})->Args({1, 0})->Args({10, 0})->Args({0, 1})->Args({1, 1})->Args({10, 1})
    ->Iterations(20)->Unit(benchmark::kMillisecond);

// ---- B-5 --------------------------------------------------------------------------------------------------------

namespace fs = std::filesystem;

/// <project>/scratch/<name>, emptied
std::string BenchScratch(const std::string& name)
{
    const fs::path dir = BenchmarkPathHelper::findProjectRoot() / "scratch" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return FileHelper::FromFsPath(dir);
}

/// A recording folder of kFolderBranches branches, each forked from the trunk at its own frame and run
/// kFolderBranchFrames frames, and the same folder joined into one file (the parts of all branches in one index)
struct FolderFixture
{
    static constexpr uint32_t kFolderBranches = 1000;
    static constexpr uint32_t kFolderBranchFrames = 20;
    bool tried = false;
    bool ready = false;
    std::string folder;
    std::string joined;
    size_t files = 0;
    uint64_t folderBytes = 0;
    uint64_t joinedBytes = 0;
    double recordSeconds = 0;
};
FolderFixture g_folder;

bool EnsureFolder()
{
    if (g_folder.tried)
        return g_folder.ready;
    g_folder.tried = true;
    if (!EnsureFixture())
        return false;
    BranchFixture& f = g_fixture;
    const std::string root = BenchScratch("bench-ttd-branch-folder");
    const auto t0 = std::chrono::steady_clock::now();
    f.ttd->InvalidateSession("benchmark reset");
    f.ttd->SetShadowRecordingRoot(root);
    f.ttd->StartRecording();
    const uint64_t start = f.Frame();
    f.emulator->RunNFrames(FolderFixture::kFolderBranches + 20, /*skipBreakpoints=*/true);
    f.ttd->StopRecording();
    std::string error;
    for (uint32_t k = 0; k < FolderFixture::kFolderBranches; ++k)
    {
        const ttd::TTDTimePoint at{start + 10 + k, 0};
        if (f.ttd->ActiveBranch() != ttd::kTTDTrunk ? !f.ttd->SwitchBranch(ttd::kTTDTrunk, at, error)
                                                    : !f.ttd->SeekTo(at))
            return false;
        if (!f.ttd->ResumeRecordingFrom(at))
            return false;
        f.emulator->RunNFrames(FolderFixture::kFolderBranchFrames, /*skipBreakpoints=*/true);
        f.ttd->StopRecording();
    }
    f.ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{start + 5, 0}, error);   // finishes the last file
    g_folder.recordSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // A copy: the session's folder goes with the next reset (InvalidateSession discards it)
    const std::string live = f.ttd->ShadowRecordingFolder();
    g_folder.folder = FileHelper::PathCombine(BenchScratch("bench-ttd-branch-folder-copy"), "folder");
    std::error_code ec;
    fs::copy(FileHelper::ToFsPath(live), FileHelper::ToFsPath(g_folder.folder), fs::copy_options::recursive, ec);
    if (ec)
        return false;
    auto opened = ttd::TTDRecordingFolder::Open(g_folder.folder);
    if (!opened || !opened->HasBranchIndex())
        return false;
    for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(g_folder.folder)))
    {
        ++g_folder.files;
        g_folder.folderBytes += entry.file_size();
    }
    g_folder.joined = FileHelper::PathCombine(BenchScratch("bench-ttd-branch-joined"), "joined.ttd");
    if (!opened->SaveAs(g_folder.joined, true, error))
        return false;
    g_folder.joinedBytes = fs::file_size(FileHelper::ToFsPath(g_folder.joined));
    opened.reset();
    f.ttd->InvalidateSession("benchmark reset");
    f.ttd->SetShadowRecordingRoot(std::string());   // the next benchmarks record without a folder
    g_folder.ready = true;
    return true;
}

/// Read every record of the parts @p take accepts; the parts read
size_t ReadParts(ttd::TTDContainerReader& reader, uint64_t& bytes, const std::function<bool(const ttd::TTDPartRef&)>& take)
{
    size_t parts = 0;
    std::vector<uint8_t> out;
    for (const ttd::TTDPartRef& part : reader.Parts())
    {
        if (!take(part))
            continue;
        ++parts;
        for (const ttd::TTDRecordRef& record : part.records)
        {
            if (!reader.ReadRecord(record, out))
                return 0;
            bytes += out.size();
        }
    }
    return parts;
}

/// range(0): 0 layout (a), the joined file: open it, read its index, read the branch's parts; 1 layout (b) + (c):
/// read branches.tdx, open the branch's files, read them. The branch changes every call (cold-ish index lookups,
/// the file cache warm for both)
void BM_TTD_Branch_FolderDemux(benchmark::State& state)
{
    if (!EnsureFolder())
    {
        state.SkipWithError("folder fixture");
        return;
    }
    const bool folder = state.range(0) == 1;
    const std::string indexPath = FileHelper::PathCombine(g_folder.folder, ttd::kBranchIndexFile);
    std::vector<double> us;
    uint32_t pick = 0;
    size_t parts = 0;
    uint64_t bytes = 0;
    bool failed = false;
    for (auto _ : state)
    {
        const uint32_t line = 1 + (pick++ * 389u) % FolderFixture::kFolderBranches;
        parts = 0;
        bytes = 0;
        us.push_back(TimeUs([&] {
            std::string error;
            if (!folder)
            {
                ttd::TTDFileSource source(g_folder.joined);
                ttd::TTDContainerReader reader;
                if (!reader.Open(source, error))
                {
                    failed = true;
                    return;
                }
                parts = ReadParts(reader, bytes, [&](const ttd::TTDPartRef& p) { return p.branch == line; });
                return;
            }
            ttd::TTDBranchIndex index;
            if (!ttd::TTDRecordingFolder::ReadBranchIndex(indexPath, index, error))
            {
                failed = true;
                return;
            }
            for (const ttd::TTDBranchIndex::Segment& segment : index.segments)
            {
                if (segment.line != line)
                    continue;
                ttd::TTDFileSource source(FileHelper::PathCombine(g_folder.folder, segment.file));
                ttd::TTDContainerReader reader;
                if (!reader.Open(source, error))
                {
                    failed = true;
                    return;
                }
                parts += ReadParts(reader, bytes, [](const ttd::TTDPartRef&) { return true; });
            }
        }));
        if (failed || parts == 0)
        {
            state.SkipWithError("the branch's parts were not found");
            return;
        }
    }
    state.counters["parts"] = static_cast<double>(parts);
    state.counters["read_kb"] = static_cast<double>(bytes) / 1024.0;
    state.counters["files"] = static_cast<double>(g_folder.files);
    state.counters["folder_mb"] = static_cast<double>(g_folder.folderBytes) / (1024.0 * 1024.0);
    state.counters["joined_mb"] = static_cast<double>(g_folder.joinedBytes) / (1024.0 * 1024.0);
    state.counters["record_s"] = g_folder.recordSeconds;
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_FolderDemux)->Arg(0)->Arg(1)->Iterations(200)->Unit(benchmark::kMicrosecond);

// ---- B-9 --------------------------------------------------------------------------------------------------------

/// A 30-minute session (90 000 frames of the trunk) with 100 branches: 90 forked from the trunk at even steps
/// (kSessionBranchFrames each), then a chain of 10, each forked from the middle of the one before (the active
/// lineage is 11 deep)
struct SessionFixture
{
    static constexpr uint32_t kTrunkFrames = 90000;
    static constexpr uint32_t kSessionBranchFrames = 100;
    bool tried = false;
    bool ready = false;
    std::string dir;
    double recordSeconds = 0;
};
SessionFixture g_session;

bool EnsureSession()
{
    if (g_session.tried)
        return g_session.ready;
    g_session.tried = true;
    if (!EnsureFixture())
        return false;
    BranchFixture& f = g_fixture;
    g_session.dir = BenchScratch("bench-ttd-branch-session");
    const auto t0 = std::chrono::steady_clock::now();
    f.ttd->SetShadowRecordingRoot(std::string());
    f.ttd->InvalidateSession("benchmark reset");
    f.ttd->StartRecording();
    const uint64_t start = f.Frame();
    f.emulator->RunNFrames(SessionFixture::kTrunkFrames, /*skipBreakpoints=*/true);
    f.ttd->StopRecording();
    std::string error;
    const uint32_t step = SessionFixture::kTrunkFrames / 91;
    for (uint32_t k = 1; k <= 90; ++k)
    {
        const ttd::TTDTimePoint at{start + uint64_t(step) * k, 0};
        if (f.ttd->ActiveBranch() != ttd::kTTDTrunk ? !f.ttd->SwitchBranch(ttd::kTTDTrunk, at, error)
                                                    : !f.ttd->SeekTo(at))
            return false;
        if (!f.ttd->ResumeRecordingFrom(at))
            return false;
        f.emulator->RunNFrames(SessionFixture::kSessionBranchFrames, /*skipBreakpoints=*/true);
        f.ttd->StopRecording();
    }
    for (uint32_t k = 0; k < 10; ++k)
    {
        const ttd::TTDTimePoint at{f.Frame() - SessionFixture::kSessionBranchFrames / 2, 0};
        if (!f.ttd->SeekTo(at) || !f.ttd->ResumeRecordingFrom(at))
            return false;
        f.emulator->RunNFrames(SessionFixture::kSessionBranchFrames, /*skipBreakpoints=*/true);
        f.ttd->StopRecording();
    }
    g_session.recordSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    g_session.ready = f.ttd->Branches().Count() == 101;
    return g_session.ready;
}

/// range(0): 0 save all branches, 2 save the active lineage, 1 load the save-all file (last: it replaces the
/// session with the loaded one, the same)
void BM_TTD_Branch_SaveLoad(benchmark::State& state)
{
    if (!EnsureSession())
    {
        state.SkipWithError("session fixture");
        return;
    }
    BranchFixture& f = g_fixture;
    const int mode = static_cast<int>(state.range(0));
    const std::string allPath = FileHelper::PathCombine(g_session.dir, "all.ttd");
    const std::string lineagePath = FileHelper::PathCombine(g_session.dir, "lineage.ttd");
    std::vector<double> us;
    std::string error;
    bool ok = true;
    for (auto _ : state)
    {
        us.push_back(TimeUs([&] {
            if (mode == 1)
            {
                ok = f.ttd->DeserializeSessionFile(allPath, error);
                return;
            }
            std::ofstream out(FileHelper::ToFsPath(mode == 0 ? allPath : lineagePath), std::ios::binary | std::ios::trunc);
            ok = f.ttd->SerializeSession(out, error, /*lineageOnly=*/mode == 2) && out.good();
        }));
        if (!ok)
        {
            state.SkipWithError(error.c_str());
            return;
        }
    }
    const std::string path = mode == 2 ? lineagePath : allPath;
    state.counters["file_mb"] = static_cast<double>(fs::file_size(FileHelper::ToFsPath(path))) / (1024.0 * 1024.0);
    state.counters["branches"] = static_cast<double>(f.ttd->Branches().Count());
    state.counters["record_s"] = g_session.recordSeconds;
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_SaveLoad)->Arg(0)->Arg(2)->Arg(1)->Iterations(3)->Unit(benchmark::kMillisecond);

// ---- B-4 --------------------------------------------------------------------------------------------------------

/// Bytes and files under @p dir (recursive)
void FolderSize(const std::string& dir, uint64_t& bytes, size_t& files)
{
    bytes = 0;
    files = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(FileHelper::ToFsPath(dir), ec); !ec && it != fs::end(it);
         it.increment(ec))
        if (it->is_regular_file(ec))
        {
            ++files;
            bytes += it->file_size(ec);
        }
}

/// range(0) experiments, each 200 frames from the same point of a 300-frame trunk, recorded into a session folder
/// (the shadow recording: the disk a session with kept experiments costs). One iteration: create them all, prune
void BM_TTD_Branch_AnalyzerWorkload(benchmark::State& state)
{
    if (!EnsureFixture())
    {
        state.SkipWithError("fixture");
        return;
    }
    BranchFixture& f = g_fixture;
    const auto experiments = static_cast<uint32_t>(state.range(0));
    constexpr uint32_t kFrames = 200;
    const std::string root = BenchScratch("bench-ttd-branch-analyzer");
    std::vector<double> us;
    double createSeconds = 0;
    double pruneMs = 0;
    uint64_t historyBefore = 0;
    uint64_t historyAfter = 0;
    uint64_t historyPruned = 0;
    uint64_t diskBefore = 0;
    uint64_t diskAfter = 0;
    uint64_t diskPruned = 0;
    size_t filesBefore = 0;
    size_t filesAfter = 0;
    size_t filesPruned = 0;
    for (auto _ : state)
    {
        state.PauseTiming();
        f.ttd->InvalidateSession("benchmark reset");
        f.ttd->SetShadowRecordingRoot(root);
        f.ttd->StartRecording();
        const uint64_t start = f.Frame();
        f.emulator->RunNFrames(300, /*skipBreakpoints=*/true);
        f.ttd->StopRecording();
        historyBefore = f.ttd->HistoryBytes();
        const std::string folder = f.ttd->ShadowRecordingFolder();
        FolderSize(folder, diskBefore, filesBefore);
        ttd::TTDControl control(f.emulator->GetContext());
        state.ResumeTiming();

        const auto t0 = std::chrono::steady_clock::now();
        for (uint32_t k = 0; k < experiments; ++k)
        {
            const std::map<std::string, std::string> options = {
                {"frame", std::to_string(start + 50)},
                {"tinframe", "0"},
                {"name", "exp " + std::to_string(k)},
                {"run", "b4"},
                {"created_by", "analyzer"},
                {"tags", "analyzer,experiment"},
                {"poke", "#C000=" + std::to_string(k & 0xFF)},
                {"run_frames", std::to_string(kFrames)},
                {"input", "0:q:down,100:q:up"},
                {"read", "#C000"},
                {"go_back", "true"}};
            ttd::TTDReply reply;
            us.push_back(TimeUs([&] { reply = control.Execute({"branch-create", options}); }));
            if (!reply.Ok())
            {
                state.SkipWithError(reply.message.c_str());
                return;
            }
        }
        createSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        state.PauseTiming();
        historyAfter = f.ttd->HistoryBytes();
        FolderSize(folder, diskAfter, filesAfter);
        state.ResumeTiming();
        ttd::TTDReply pruned;
        pruneMs = TimeUs([&] { pruned = control.Execute({"branch-prune", {{"run", "b4"}, {"dry_run", "false"}}}); }) /
                  1000.0;
        state.PauseTiming();
        if (!pruned.Ok() || pruned.body.find("deleted")->items.size() != experiments)
        {
            state.SkipWithError("prune");
            return;
        }
        historyPruned = f.ttd->HistoryBytes();
        FolderSize(folder, diskPruned, filesPruned);
        state.ResumeTiming();
    }
    f.ttd->InvalidateSession("benchmark reset");
    f.ttd->SetShadowRecordingRoot(std::string());
    const double n = experiments;
    state.counters["experiments_per_s"] = n / createSeconds;
    state.counters["prune_ms"] = pruneMs;
    state.counters["mem_kb_per_branch"] = (static_cast<double>(historyAfter) - historyBefore) / n / 1024.0;
    state.counters["mem_kb_left_after_prune"] = (static_cast<double>(historyPruned) - historyBefore) / 1024.0;
    state.counters["disk_kb_per_branch"] = (static_cast<double>(diskAfter) - diskBefore) / n / 1024.0;
    state.counters["disk_mb_left_after_prune"] = (static_cast<double>(diskPruned) - diskBefore) / (1024.0 * 1024.0);
    state.counters["files_per_branch"] = (static_cast<double>(filesAfter) - filesBefore) / n;
    state.counters["files_left_after_prune"] = static_cast<double>(filesPruned) - static_cast<double>(filesBefore);
    Percentiles(state, us);
}
BENCHMARK(BM_TTD_Branch_AnalyzerWorkload)->Arg(1000)->Iterations(1)->Unit(benchmark::kSecond);

}  // namespace
