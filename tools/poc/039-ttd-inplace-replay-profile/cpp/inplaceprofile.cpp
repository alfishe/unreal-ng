/// @file inplaceprofile.cpp
/// @brief POC 039 (tools/poc/039-ttd-inplace-replay-profile/README.md): where today's in-place TTD replay spends its
/// time, step by step, and what a recording costs (frame time, capture, session size). Workloads W1-W3 of POC 038.
///
///   ttd-inplace-profile --workload w1 --frames 3000 [--repeat 3] [--loop STEP --seconds S]
///
/// --loop runs one step over the window again and again for S seconds (for an external sampler) and prints nothing
/// else. Steps: continuous, restore, restore-run, restore-run-m1, seek, seek-mid, frame-cache, cpu-view, cpu-view-mid,
/// exec-all, writes-all, changes-all, writes-all-journal, engine-full, engine-incremental; and two that record
/// instead: live (the frames without time travel), record (a recording).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <time.h>

#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// This thread's CPU time: the machine runs on the calling thread, and a loaded host does not stretch it
double ThreadCpuMs()
{
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

// ---------------------------------------------------------------------------
// An instance (as POC 038's, without turbo: the paced 1x path a user records with)
// ---------------------------------------------------------------------------
struct Instance
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    bool Make(const std::string& id, const std::string& model, bool timeTravel, bool debugMode = false)
    {
        emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, model, LoggerLevel::LogNone);
        if (!emulator)
            return false;
        context = emulator->GetContext();
        context->pFeatureManager->setFeature(Features::kDebugMode, timeTravel || debugMode);
        context->pFeatureManager->setFeature(Features::kTimeTravel, timeTravel);
        context->pMemory->UpdateFeatureCache();
        context->pSoundManager->UpdateFeatureCache();
        ttd = context->pTimeTravelController;
        return !timeTravel || ttd != nullptr;
    }
    ~Instance()
    {
        if (emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
    Z80* Cpu() const { return context->pCore->GetZ80(); }

    /// The machine's state: RAM pages 0-7, the main registers, the frame and its T (FNV-1a)
    uint64_t StateHash() const
    {
        uint64_t h = 1469598103934665603ull;
        auto add = [&h](const uint8_t* p, size_t n) {
            for (size_t i = 0; i < n; ++i)
                h = (h ^ p[i]) * 1099511628211ull;
        };
        for (uint16_t page = 0; page < 8; ++page)
            if (const uint8_t* data = context->pMemory->RAMPageAddress(page))
                add(data, 0x4000);
        const Z80* z80 = Cpu();
        const uint16_t regs[] = {z80->pc, z80->sp, z80->af, z80->bc, z80->de, z80->hl, z80->ix, z80->iy};
        add(reinterpret_cast<const uint8_t*>(regs), sizeof(regs));
        const uint64_t when[] = {context->emulatorState.frame_counter, static_cast<uint64_t>(z80->t)};
        add(reinterpret_cast<const uint8_t*>(when), sizeof(when));
        return h;
    }
};

// ---------------------------------------------------------------------------
// Workloads (POC 038's W1-W3)
// ---------------------------------------------------------------------------
void SetUpW1(Instance& s)
{
    const std::vector<uint8_t> loop = {0xF3, 0x0E, 0x00, 0x3E, 0x11, 0x32, 0xF0, 0x80, 0x06, 0x00, 0x10, 0xFE,
                                       0x0C, 0x79, 0x32, 0x00, 0x81, 0x3E, 0x05, 0x32, 0x01, 0x81, 0x79, 0x0F,
                                       0x0F, 0x0F, 0x0F, 0xE6, 0x01, 0x32, 0x03, 0x81, 0x3A, 0x00, 0x00, 0x79,
                                       0xE6, 0x3F, 0xCC, 0x00, 0x82, 0x18, 0xDD};
    const std::vector<uint8_t> swap = {0xC5, 0x3A, 0xF0, 0x80, 0xEE, 0x02, 0x32, 0xF0, 0x80, 0x01,
                                       0xFD, 0x7F, 0xED, 0x79, 0xCD, 0x00, 0xC0, 0xC1, 0xC9};
    Memory* memory = s.context->pMemory;
    for (size_t i = 0; i < loop.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
    for (size_t i = 0; i < swap.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), swap[i]);
    memory->RAMPageAddress(1)[0] = 0xC9;
    memory->RAMPageAddress(3)[0] = 0xC9;
    s.Cpu()->pc = 0x8000;
    s.Cpu()->sp = 0xBF00;
}

bool SetUpWorkload(Instance& s, const std::string& workload)
{
    if (workload == "w1")
    {
        SetUpW1(s);
        return true;
    }
    if (workload == "w2")
    {
        if (!s.emulator->LoadSnapshot("testdata/loaders/sna/7threality.sna"))
        {
            std::fprintf(stderr, "cannot load testdata/loaders/sna/7threality.sna (run from the repository root)\n");
            return false;
        }
        if (s.emulator->IsPaused())
            s.emulator->Resume();
        s.emulator->RunNFrames(100, true);
        return true;
    }
    if (workload == "w3")
    {
        s.emulator->RunNFrames(150, true);
        return true;
    }
    std::fprintf(stderr, "unknown workload %s\n", workload.c_str());
    return false;
}

std::string ModelOf(const std::string& workload)
{
    return workload == "w3" ? "48K" : "PENTAGON";
}

struct Targets
{
    uint16_t execPc = 0x8200;
    uint16_t write = 0x8103;
};

Targets TargetsOf(const std::string& workload)
{
    Targets t;
    if (workload == "w3")
    {
        t.execPc = 0x028E;
        t.write = 0x5C08;
    }
    return t;
}

/// One frame of the workload: W3 types a fixed key sequence through the live input path (journaled)
void RunWorkloadFrame(Instance& s, const std::string& workload, unsigned f)
{
    if (workload == "w3" && s.ttd && (f % 9 == 0 || f % 9 == 3))
    {
        static const uint8_t keys[] = {ZXKEY_P, ZXKEY_1, ZXKEY_SPACE, ZXKEY_A, ZXKEY_ENTER, ZXKEY_L, ZXKEY_E, ZXKEY_Q,
                                       ZXKEY_ENTER};
        const unsigned k = (f / 9) * 2 + (f % 9 == 3 ? 1 : 0);
        ttd::TTDInputEvent ev;
        ev.kind = ttd::TTDInputKind::Key;
        ev.key = keys[(k / 2) % (sizeof(keys) / sizeof(keys[0]))];
        ev.pressed = f % 9 == 0;
        s.ttd->SubmitLiveInput(ev);
    }
    s.emulator->RunNFrames(1, true);
}

// ---------------------------------------------------------------------------
// The recorded session and its window
// ---------------------------------------------------------------------------
struct Session
{
    Instance s;
    uint64_t first = 0;   ///< the window: [first, last), after the baseline checkpoint
    uint64_t last = 0;
    size_t firstIndex = 0;   ///< the timeline index of frame `first`

    uint32_t FrameT() const { return s.context->emulatorState.BaseToCpuT(s.context->config.frame); }
    size_t IndexOf(uint64_t frame) const { return firstIndex + static_cast<size_t>(frame - first); }

    /// Run from the CPU's position to the frame's end (as BuildFrameCache and POC 038's clones do)
    void RunToFrameEnd()
    {
        const uint32_t frameT = FrameT();
        const uint32_t t = static_cast<uint32_t>(s.Cpu()->t);
        if (t < frameT)
            s.emulator->RunTStates(frameT - t, /*skipBreakpoints=*/true);
    }
};

struct RecordCost
{
    uint64_t endHash = 0;   ///< the machine after the frames: the same with or without time travel
    double frameUs = 0;
    double captureUs = 0;   ///< perf.lastCaptureNs, averaged over the frames
};

/// Record @p frames on a fresh instance; without @p ttd the same frames run with time travel off
RecordCost Record(Instance& s, const std::string& workload, unsigned frames, bool ttd, bool journal)
{
    RecordCost cost;
    if (ttd)
    {
        s.ttd->SetEnableWriteJournal(journal);
        s.ttd->StartRecording();
    }
    double captureNs = 0;
    const double start = ThreadCpuMs();
    for (unsigned f = 0; f < frames; ++f)
    {
        RunWorkloadFrame(s, workload, f);
        if (ttd)
            captureNs += static_cast<double>(s.ttd->GetPerfCounters().lastCaptureNs);
    }
    cost.frameUs = (ThreadCpuMs() - start) * 1000.0 / frames;
    cost.endHash = s.StateHash();
    cost.captureUs = captureNs / 1000.0 / frames;
    if (ttd)
        s.ttd->StopRecording();
    return cost;
}

bool MakeSession(Session& session, const std::string& workload, unsigned frames, bool journal, RecordCost* cost)
{
    if (!session.s.Make(journal ? "poc039-journal" : "poc039-source", ModelOf(workload), true))
        return false;
    if (!SetUpWorkload(session.s, workload))
        return false;
    const RecordCost c = Record(session.s, workload, frames, true, journal);
    if (cost)
        *cost = c;
    ttd::TimeTravelController& t = *session.s.ttd;
    session.firstIndex = 1;   // after the baseline checkpoint (taken inside a frame)
    session.first = t.GetCheckpoint(session.firstIndex)->time.frame;
    session.last = t.GetCheckpoint(t.GetCheckpointCount() - 1)->time.frame;
    for (size_t i = session.firstIndex; i < t.GetCheckpointCount(); ++i)
        if (t.GetCheckpoint(i)->time.frame != session.first + (i - session.firstIndex))
        {
            std::fprintf(stderr, "checkpoint %zu is not frame %llu: one checkpoint per frame expected\n", i,
                         static_cast<unsigned long long>(session.first + (i - session.firstIndex)));
            return false;
        }
    return t.SeekTo(t.SessionEndPosition());
}

// ---------------------------------------------------------------------------
// Steps: each runs once over the window and returns the frames it covered
// ---------------------------------------------------------------------------
struct RestoreSplit
{
    double cpuChipset = 0, devices = 0, memory = 0, screen = 0, replay = 0, present = 0;
    double links = 0, pieces = 0, skipped = 0;   ///< piece store work (difference links, pieces written / kept)
    void Add(const ttd::TTDPerfCounters& p)
    {
        cpuChipset += static_cast<double>(p.lastRestoreCpuChipsetNs);
        devices += static_cast<double>(p.lastRestoreDevicesNs);
        memory += static_cast<double>(p.lastRestoreMemoryNs);
        screen += static_cast<double>(p.lastRestoreScreenNs);
        replay += static_cast<double>(p.lastReplayNs);
        present += static_cast<double>(p.lastPresentNs);
    }
};

using Step = std::function<uint64_t(Session&, RestoreSplit&)>;

uint64_t LinksDecoded(Session& w)
{
    return w.s.ttd->GetEngine().PieceStore().GetWork().linksDecoded;
}

/// The engine's memory restore alone, as the controller does it (every piece decoded, the known content forgotten)
uint64_t EngineFull(Session& w, RestoreSplit& split)
{
    ttd::TimeTravelEngine& engine = w.s.ttd->GetEngine();
    for (uint64_t f = w.first; f < w.last; ++f)
    {
        const int64_t index = engine.CheckpointIndexOf({0, f, 0});
        ttd::TTDRestoreStats stats;
        engine.ForgetMemory();
        engine.RestoreToMemory(size_t(index), nullptr, &stats);
        split.links += static_cast<double>(stats.linksDecoded);
        split.pieces += static_cast<double>(stats.piecesDecoded);
        split.skipped += static_cast<double>(stats.piecesSkipped);
    }
    return w.last - w.first;
}

/// The same restores frame after frame, the engine keeping what memory holds: only changed pieces are decoded
/// (the lower bound of a restore that trusts its own last write; nothing runs in between)
uint64_t EngineIncremental(Session& w, RestoreSplit& split)
{
    ttd::TimeTravelEngine& engine = w.s.ttd->GetEngine();
    engine.ForgetMemory();
    for (uint64_t f = w.first; f < w.last; ++f)
    {
        const int64_t index = engine.CheckpointIndexOf({0, f, 0});
        ttd::TTDRestoreStats stats;
        engine.RestoreToMemory(size_t(index), nullptr, &stats);
        split.links += static_cast<double>(stats.linksDecoded);
        split.pieces += static_cast<double>(stats.piecesDecoded);
        split.skipped += static_cast<double>(stats.piecesSkipped);
    }
    return w.last - w.first;
}

/// Exactness: every frame replayed from its checkpoint reaches the next checkpoint's state (returns the frames that
/// do; the replay runs in debug mode, whatever mode recorded)
uint64_t Verify(Session& w, RestoreSplit&)
{
    uint64_t exact = 0;
    for (uint64_t f = w.first; f + 1 < w.last; ++f)
    {
        w.s.ttd->RestoreCheckpointForTesting(w.IndexOf(f + 1));
        const uint64_t expected = w.s.StateHash();
        w.s.ttd->RestoreCheckpointForTesting(w.IndexOf(f));
        w.RunToFrameEnd();
        exact += w.s.StateHash() == expected ? 1 : 0;
    }
    return exact;
}

uint64_t Continuous(Session& w, RestoreSplit&)
{
    w.s.ttd->SeekTo({w.first, 0});
    for (uint64_t f = w.first; f < w.last; ++f)
        w.RunToFrameEnd();
    return w.last - w.first;
}

uint64_t RestoreOnly(Session& w, RestoreSplit& split)
{
    const uint64_t links = LinksDecoded(w);
    for (uint64_t f = w.first; f < w.last; ++f)
    {
        w.s.ttd->RestoreCheckpointForTesting(w.IndexOf(f));
        split.Add(w.s.ttd->GetPerfCounters());
    }
    split.links += static_cast<double>(LinksDecoded(w) - links);
    split.present = 0;   // not presented: the counter holds an earlier seek's
    return w.last - w.first;
}

uint64_t RestoreRun(Session& w, RestoreSplit& split, bool m1)
{
    Z80* z80 = w.s.Cpu();
    uint64_t instructions = 0;
    auto prev = z80->m1TraceHook;
    if (m1)
        z80->m1TraceHook = [&instructions](uint16_t) { ++instructions; };
    for (uint64_t f = w.first; f < w.last; ++f)
    {
        w.s.ttd->RestoreCheckpointForTesting(w.IndexOf(f));
        split.Add(w.s.ttd->GetPerfCounters());
        split.present = 0;   // not presented: the counter holds an earlier seek's
        w.RunToFrameEnd();
    }
    z80->m1TraceHook = prev;
    return w.last - w.first;
}

uint64_t Seek(Session& w, RestoreSplit& split, bool mid)
{
    const uint32_t t = mid ? w.FrameT() / 2 : 0;
    const uint64_t links = LinksDecoded(w);
    for (uint64_t f = w.first; f < w.last; ++f)
    {
        w.s.ttd->SeekTo({f, t});
        split.Add(w.s.ttd->GetPerfCounters());
    }
    split.links += static_cast<double>(LinksDecoded(w) - links);
    return w.last - w.first;
}

uint64_t FrameCache(Session& w, RestoreSplit&)
{
    uint64_t instructions = 0;
    for (uint64_t f = w.first; f < w.last; ++f)
        if (const ttd::TTDFrameCache* cache = w.s.ttd->GetFrameCache(f))
            instructions += cache->entries.size();
    return instructions ? w.last - w.first : 0;
}

uint64_t CpuView(Session& w, RestoreSplit&, bool mid)
{
    const uint32_t t = mid ? w.FrameT() / 2 : 0;
    uint64_t ok = 0;
    for (uint64_t f = w.first; f < w.last; ++f)
        ok += w.s.ttd->ReadCpuView({f, t}, 0, 0x10000).ok ? 1 : 0;
    return ok;
}

uint64_t ExecAll(Session& w, RestoreSplit&, uint16_t pc)
{
    ttd::TTDReverseContinueQuery q;
    q.pcs = {pc};
    q.all = true;
    q.limit = ttd::TimeTravelController::kReverseHitLimitMax;
    q.fromFrame = w.first;
    q.beforeGlobalT = w.s.ttd->GlobalT({w.last, 0});
    while (true)
    {
        const auto r = w.s.ttd->ReverseContinue(q);
        if (!r.truncated || r.hits.empty())
            break;
        q.beforeGlobalT = r.hits.back().globalT - 1;
    }
    return w.last - w.first;
}

uint64_t WritesAll(Session& w, RestoreSplit&, uint16_t addr, bool changesOnly)
{
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = addr;
    q.access = ttd::TTDAccessType::Write;
    q.beforeGlobalT = w.s.ttd->GlobalT({w.last, 0});
    ttd::TTDFindOptions options;
    options.all = true;
    options.limit = ttd::TimeTravelController::kReverseHitLimitMax;
    options.fromFrame = w.first;
    options.changesOnly = changesOnly;
    while (true)
    {
        const auto r = w.s.ttd->FindAccesses(q, options);
        if (!r.truncated || r.hits.empty())
            break;
        q.beforeGlobalT = r.hits.back().globalT - 1;
    }
    return w.last - w.first;
}

struct Options
{
    std::string workload = "w1";
    unsigned frames = 3000;
    unsigned repeat = 3;
    std::string loop;
    double seconds = 10;
    bool recordOnly = false;   ///< --record-only: the recording table and nothing else
};

double Median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}
}  // namespace

int main(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--workload") o.workload = next();
        else if (a == "--frames") o.frames = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--repeat") o.repeat = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--loop") o.loop = next();
        else if (a == "--seconds") o.seconds = std::stod(next());
        else if (a == "--record-only") o.recordOnly = true;
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    const Targets targets = TargetsOf(o.workload);
    const bool loopOnly = !o.loop.empty();

    // ---- Recording: the frame without time travel, with a recording, with the write journal ----
    if (!loopOnly)
    {
        uint64_t hashes[4] = {};
        std::vector<double> plain, debug, rec, recCapture, journal, journalCapture;
        for (unsigned r = 0; r < o.repeat; ++r)
        {
            Instance a;
            if (!a.Make("poc039-plain", ModelOf(o.workload), false) || !SetUpWorkload(a, o.workload))
                return 1;
            const RecordCost ca = Record(a, o.workload, o.frames, false, false);
            plain.push_back(ca.frameUs);
            hashes[0] = ca.endHash;
            Instance d;
            if (!d.Make("poc039-debug", ModelOf(o.workload), false, true) || !SetUpWorkload(d, o.workload))
                return 1;
            const RecordCost cd = Record(d, o.workload, o.frames, false, false);
            debug.push_back(cd.frameUs);
            hashes[1] = cd.endHash;
            Instance b;
            if (!b.Make("poc039-rec", ModelOf(o.workload), true) || !SetUpWorkload(b, o.workload))
                return 1;
            const RecordCost cb = Record(b, o.workload, o.frames, true, false);
            rec.push_back(cb.frameUs);
            hashes[2] = cb.endHash;
            recCapture.push_back(cb.captureUs);
            Instance c;
            if (!c.Make("poc039-recj", ModelOf(o.workload), true) || !SetUpWorkload(c, o.workload))
                return 1;
            const RecordCost cc = Record(c, o.workload, o.frames, true, true);
            journal.push_back(cc.frameUs);
            hashes[3] = cc.endHash;
            journalCapture.push_back(cc.captureUs);
        }
        std::printf("workload %s, %u frames, median of %u\n", o.workload.c_str(), o.frames, o.repeat);
        std::printf("end state: %016llx %016llx %016llx %016llx (no TTD, debug mode, recording, with the journal)\n",
                    static_cast<unsigned long long>(hashes[0]), static_cast<unsigned long long>(hashes[1]),
                    static_cast<unsigned long long>(hashes[2]), static_cast<unsigned long long>(hashes[3]));
        std::printf("record: frame %.1f us without TTD | %.1f us in debug mode | %.1f us recording (capture %.1f) | %.1f us with the write "
                    "journal (capture %.1f)\n",
                    Median(plain), Median(debug), Median(rec), Median(recCapture), Median(journal), Median(journalCapture));
        if (o.recordOnly)
            return 0;
    }

    if (o.loop == "live" || o.loop == "record")
    {
        const auto start = Clock::now();
        while (MsSince(start) < o.seconds * 1000.0)
        {
            Instance a;
            if (!a.Make("poc039-loop", ModelOf(o.workload), o.loop == "record") || !SetUpWorkload(a, o.workload))
                return 1;
            Record(a, o.workload, o.frames, o.loop == "record", false);
        }
        return 0;
    }

    Session plainSession, journalSession;
    if (!MakeSession(plainSession, o.workload, o.frames, false, nullptr))
        return 1;
    const bool needJournal = !loopOnly || o.loop == "writes-all-journal";
    if (needJournal && !MakeSession(journalSession, o.workload, o.frames, true, nullptr))
        return 1;

    if (!loopOnly)
    {
        std::printf("regions:");
        for (const ttd::TTDRegionDesc& r : plainSession.s.ttd->GetEngine().Regions())
            std::printf(" %s %u", r.name.c_str(), r.pieces);
        std::printf(" (4 KB pieces)\n");
        for (Session* w : {&plainSession, &journalSession})
        {
            std::ostringstream serialized;
            std::string error;
            w->s.ttd->SerializeSession(serialized, error);
            const ttd::TTDSessionInfo info = w->s.ttd->GetSessionInfo();
            const ttd::TTDHeapBreakdown heap = w->s.ttd->GetHeapBreakdown();
            const double frames = static_cast<double>(info.checkpointCount);
            std::printf("session%s: %zu checkpoints, file %.0f B/frame, heap %.0f B/frame (pages %.0f + slack %.0f, "
                        "checkpoints %.0f, page refs %.0f, device blobs %.0f, journals %.0f, write journal %.0f, "
                        "coverage %.0f, ports %.0f)\n",
                        w == &journalSession ? " with the write journal" : "", info.checkpointCount,
                        static_cast<double>(serialized.str().size()) / frames,
                        static_cast<double>(info.sessionHeapBytes) / frames,
                        static_cast<double>(heap.ramPayload) / frames, static_cast<double>(heap.ramPayloadSlack) / frames,
                        static_cast<double>(heap.checkpoints) / frames, static_cast<double>(heap.pageRefs) / frames,
                        static_cast<double>(heap.deviceBlobs) / frames, static_cast<double>(heap.inputJournals) / frames,
                        static_cast<double>(heap.writeJournal + heap.writeJournalSlack) / frames,
                        static_cast<double>(heap.coverage + heap.coverageSlack) / frames,
                        static_cast<double>(heap.portReads + heap.portWrites + heap.portJournalSlack) / frames);
        }
    }

    if (o.loop == "verify")
    {
        RestoreSplit unused;
        const uint64_t exact = Verify(plainSession, unused);
        std::printf("verify: %llu of %llu frames reach the next checkpoint exactly\n",
                    static_cast<unsigned long long>(exact),
                    static_cast<unsigned long long>(plainSession.last - plainSession.first - 1));
        return 0;
    }

    const std::vector<std::pair<std::string, Step>> steps = {
        {"continuous", Continuous},
        {"restore", RestoreOnly},
        {"restore-run", [](Session& w, RestoreSplit& s) { return RestoreRun(w, s, false); }},
        {"restore-run-m1", [](Session& w, RestoreSplit& s) { return RestoreRun(w, s, true); }},
        {"seek", [](Session& w, RestoreSplit& s) { return Seek(w, s, false); }},
        {"seek-mid", [](Session& w, RestoreSplit& s) { return Seek(w, s, true); }},
        {"frame-cache", FrameCache},
        {"cpu-view", [](Session& w, RestoreSplit& s) { return CpuView(w, s, false); }},
        {"cpu-view-mid", [](Session& w, RestoreSplit& s) { return CpuView(w, s, true); }},
        {"exec-all", [targets](Session& w, RestoreSplit& s) { return ExecAll(w, s, targets.execPc); }},
        {"writes-all", [targets](Session& w, RestoreSplit& s) { return WritesAll(w, s, targets.write, false); }},
        {"changes-all", [targets](Session& w, RestoreSplit& s) { return WritesAll(w, s, targets.write, true); }},
        {"writes-all-journal", [targets](Session& w, RestoreSplit& s) { return WritesAll(w, s, targets.write, false); }},
        {"engine-full", EngineFull},
        {"engine-incremental", EngineIncremental},
    };

    for (const auto& [name, step] : steps)
    {
        if (loopOnly && name != o.loop)
            continue;
        Session& w = name == "writes-all-journal" ? journalSession : plainSession;
        if (loopOnly)
        {
            const auto start = Clock::now();
            RestoreSplit split;
            while (MsSince(start) < o.seconds * 1000.0)
                step(w, split);
            return 0;
        }
        std::vector<double> perFrame;
        RestoreSplit split;
        uint64_t frames = 0;
        for (unsigned r = 0; r < o.repeat; ++r)
        {
            RestoreSplit one;
            const double start = ThreadCpuMs();
            frames = step(w, one);
            perFrame.push_back(frames ? (ThreadCpuMs() - start) * 1000.0 / static_cast<double>(frames) : 0);
            if (r == 0)
                split = one;
        }
        const double n = frames ? static_cast<double>(frames) * 1000.0 : 1;
        std::printf("%-20s %8.1f us/frame", name.c_str(), Median(perFrame));
        if (split.cpuChipset + split.devices + split.memory + split.screen + split.replay + split.present > 0)
            std::printf("  (restore: cpu+chipset %.1f, devices %.1f, memory %.1f, screen %.1f | replay %.1f | present "
                        "%.1f)",
                        split.cpuChipset / n, split.devices / n, split.memory / n, split.screen / n, split.replay / n,
                        split.present / n);
        if (split.links + split.pieces > 0)
            std::printf("  [per frame: %.1f links%s]", split.links * 1000.0 / n,
                        split.pieces + split.skipped > 0
                            ? (" | " + std::to_string(split.pieces * 1000.0 / n).substr(0, 6) + " pieces decoded, " +
                               std::to_string(split.skipped * 1000.0 / n).substr(0, 6) + " kept").c_str()
                            : "");
        std::printf("\n");
    }
    return 0;
}
