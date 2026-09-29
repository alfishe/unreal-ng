#include <benchmark/benchmark.h>

#include <sstream>
#include <string>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "loaders/benchmark_path_helper.h"

/// Session save and load (.ttd): time and file size, by the number of input
/// events in the session. A 300-frame recording of action.sna (6 s of play)
/// with N keyboard events spread evenly over it: N = 0 (no input), 60 (typing,
/// ~10 events a second), 1,000, and 100,000 (a stress case far beyond real
/// input). Counters: file_bytes (the .ttd size), input_events (events in the
/// session). Written to compare builds: the same benchmark on a build without
/// saved input shows what the input-journal and external-event sections add
/// (ttd-offline-analysis.md O-1).
namespace
{
const char* ACTION_SNA_RELATIVE = "loaders/sna/action.sna";
constexpr unsigned kFrames = 300;

struct Session
{
    Emulator* emulator = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;
    std::string file;
    size_t inputEvents = 0;

    void Record(int64_t events)
    {
        static const std::string actionSna = BenchmarkPathHelper::RequireTestDataFile(ACTION_SNA_RELATIVE);
        emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            BenchmarkPathHelper::FailSetup("Emulator::Init failed");
        if (!emulator->LoadSnapshot(actionSna))
            BenchmarkPathHelper::FailSetup("cannot load " + actionSna);
        EmulatorContext* context = emulator->GetContext();
        ttd = context->pTimeTravelManager;
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        ttd->StartRecording();

        // N events over kFrames frames: run the machine in equal slices of
        // T-states (RunNCPUCycles would count instructions) and
        // submit one key press or release between them (synchronous mode:
        // applied and journaled at once)
        const uint64_t totalT = static_cast<uint64_t>(kFrames) * context->config.frame;
        const uint64_t slice = events > 0 ? totalT / static_cast<uint64_t>(events) : totalT;
        uint64_t ran = 0;
        for (int64_t i = 0; i < events; i++)
        {
            if (slice > 0)
                emulator->RunTStates(static_cast<unsigned>(slice));
            ran += slice;
            ttd::TTDInputEvent ev;
            ev.kind = ttd::TTDInputKind::Key;
            ev.key = ZXKEY_A;
            ev.pressed = (i & 1) == 0;
            ttd->SubmitLiveInput(ev);
        }
        if (ran < totalT)
            emulator->RunTStates(static_cast<unsigned>(totalT - ran));
        ttd->StopRecording();
        inputEvents = ttd->GetInputJournal().Size();

        std::ostringstream out;
        std::string err;
        if (!ttd->SerializeSession(out, err))
            BenchmarkPathHelper::FailSetup("SerializeSession: " + err);
        file = out.str();
    }

    void Release()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
            delete emulator;
            emulator = nullptr;
        }
    }
};
}  // namespace

static void BM_TTD_SerializeSession(benchmark::State& state)
{
    Session s;
    s.Record(state.range(0));
    for (auto _ : state)
    {
        std::ostringstream out;
        std::string err;
        const bool ok = s.ttd->SerializeSession(out, err);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out.tellp());
    }
    state.counters["file_bytes"] = static_cast<double>(s.file.size());
    state.counters["input_events"] = static_cast<double>(s.inputEvents);
    s.Release();
}
BENCHMARK(BM_TTD_SerializeSession)->Arg(0)->Arg(60)->Arg(1000)->Arg(100000)->Unit(benchmark::kMillisecond);

static void BM_TTD_DeserializeSession(benchmark::State& state)
{
    Session s;
    s.Record(state.range(0));
    for (auto _ : state)
    {
        std::istringstream in(s.file);
        std::string err;
        const bool ok = s.ttd->DeserializeSession(in, err);
        benchmark::DoNotOptimize(ok);
    }
    state.counters["file_bytes"] = static_cast<double>(s.file.size());
    state.counters["input_events"] = static_cast<double>(s.inputEvents);
    s.Release();
}
BENCHMARK(BM_TTD_DeserializeSession)->Arg(0)->Arg(60)->Arg(1000)->Arg(100000)->Unit(benchmark::kMillisecond);
