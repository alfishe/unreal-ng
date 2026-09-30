#include <benchmark/benchmark.h>

#include <sstream>
#include <string>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdportjournal.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/benchmark_path_helper.h"

/// Port journals (ttd-port-read-journal.md): what recording every IN and OUT
/// with its time and PC costs, and what a search over them costs.
///
/// - BM_TTD_PortJournal_Record / _Play: one read through the journal, as
///   Z80::in calls it while a session records / replays. A tape-loader-like
///   stream: one port and instruction, a 47 T loop, a value that changes every
///   700 reads.
/// - BM_TTD_PortSearch_Ear: the "ear" event over 1M recorded reads.
/// - BM_TTD_PortJournal_TapeSession: the heaviest ordinary case - a program
///   polling the tape's EAR bit in a tight loop while a tape plays (tens of
///   thousands of IN a second), recorded for 300 frames (6 s). The time is
///   the recording run; counters: reads_per_second, journal_bytes (the
///   section in a .ttd file), file_bytes (the whole session file).
namespace
{
constexpr unsigned kFrames = 300;

const uint8_t kEarPoller[] = {
    0xF3,                    // 8000 DI
    0x21, 0x00, 0x00,        // 8001 LD HL,0
    0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
    0x0E, 0xFF,              // 8008 LD C,#FF
    0x23,                    // 800A loop: INC HL
    0x3E, 0xFF,              // 800B LD A,#FF
    0xDB, 0xFE,              // 800D IN A,(#FE)
    0xE6, 0x40,              // 800F AND #40
    0xB9,                    // 8011 CP C
    0x28, 0xF6,              // 8012 JR Z,loop
    0x4F,                    // 8014 LD C,A
    0xDD, 0x75, 0x00,        // 8015 LD (IX+0),L
    0xDD, 0x74, 0x01,        // 8018 LD (IX+1),H
    0xDD, 0x23,              // 801B INC IX
    0xDD, 0x23,              // 801D INC IX
    0x18, 0xE9,              // 801F JR loop
};
}  // namespace

namespace
{
/// Read `i` of a tape loader's EAR polling loop
inline uint8_t Feed(ttd::TTDPortJournal& journal, uint64_t i, uint8_t live)
{
    const uint64_t t = 1000 + i * 47;
    return journal.OnRead(0x7FFE, live, t / 69888, static_cast<uint32_t>(t % 69888), 0x05ED);
}
}  // namespace

static void BM_TTD_PortJournal_Record(benchmark::State& state)
{
    ttd::TTDPortJournal journal;
    journal.StartRecording();
    uint64_t i = 0;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(Feed(journal, i, ((i / 700) & 1) ? 0xBF : 0xFF));
        if (++i == (1u << 24))
        {
            state.PauseTiming();
            journal.Clear();
            journal.StartRecording();
            i = 0;
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TTD_PortJournal_Record);

static void BM_TTD_PortJournal_Play(benchmark::State& state)
{
    constexpr uint64_t kReads = 1u << 22;
    ttd::TTDPortJournal journal;
    journal.StartRecording();
    for (uint64_t i = 0; i < kReads; i++)
        Feed(journal, i, ((i / 700) & 1) ? 0xBF : 0xFF);
    journal.StartPlayback(0);
    uint64_t i = 0;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(Feed(journal, i++, 0xFF));
        if (journal.GetMode() != ttd::TTDPortJournal::Mode::Play)
        {
            state.PauseTiming();
            journal.StartPlayback(0);
            i = 0;
            state.ResumeTiming();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TTD_PortJournal_Play);

static void BM_TTD_PortSearch_Ear(benchmark::State& state)
{
    constexpr uint64_t kReads = 1'000'000;
    ttd::TTDPortJournal reads(ttd::TTDPortJournal::Direction::Read);
    ttd::TTDPortJournal writes(ttd::TTDPortJournal::Direction::Write);
    reads.StartRecording();
    for (uint64_t i = 0; i < kReads; i++)
        Feed(reads, i, ((i / 700) & 1) ? 0xBF : 0xFF);
    reads.Stop();
    ttd::TTDPortQuery q;
    std::string err;
    if (!ttd::BuildPortEventQuery("ear", "", q, err))
        BenchmarkPathHelper::FailSetup(err);
    q.limit = 100000;
    size_t hits = 0;
    for (auto _ : state)
    {
        const ttd::TTDPortSearchResult r = ttd::SearchPortEvents(reads, writes, q);
        hits = r.hits.size();
        benchmark::DoNotOptimize(hits);
    }
    state.SetItemsProcessed(state.iterations() * kReads);
    state.counters["hits"] = static_cast<double>(hits);
}
BENCHMARK(BM_TTD_PortSearch_Ear)->Unit(benchmark::kMillisecond);

static void BM_TTD_PortJournal_TapeSession(benchmark::State& state)
{
    static const std::string tape = BenchmarkPathHelper::RequireTestDataFile("memory/UMT23X.tap");
    uint64_t reads = 0;
    uint64_t writes = 0;
    size_t journalBytes = 0;
    size_t fileBytes = 0;
    for (auto _ : state)
    {
        state.PauseTiming();
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (!emulator->Init())
            BenchmarkPathHelper::FailSetup("Emulator::Init failed");
        EmulatorContext* context = emulator->GetContext();
        // The shipped config fits NeoGS, which the first journal version does
        // not isolate; the lightweight player (idle, a small blob) keeps the
        // GS slot filled without it
        if (!context->pSoundManager->switchGeneralSoundCard(GSTypeKind::LW))
            BenchmarkPathHelper::FailSetup("cannot switch the GS slot to the lightweight player");
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        fm->setFeature(Features::kTurboTape, false);
        context->pMemory->UpdateFeatureCache();
        if (!emulator->LoadTape(tape))
            BenchmarkPathHelper::FailSetup("cannot load " + tape);
        Z80* z80 = context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(kEarPoller); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), kEarPoller[i]);
        z80->pc = 0x8000;
        emulator->RunNFrames(3);
        ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
        if (!ttd->StartRecording())
            BenchmarkPathHelper::FailSetup("StartRecording failed");
        state.ResumeTiming();

        emulator->RunNFrames(kFrames);

        state.PauseTiming();
        ttd->StopRecording();
        const auto info = ttd->GetSessionInfo();
        if (!info.portJournalActive)
            BenchmarkPathHelper::FailSetup("the port-read journal is off: " + info.portJournalOffReason);
        reads = info.portReadCount;
        writes = info.portWriteCount;
        journalBytes = info.portJournalBytes;
        std::ostringstream out;
        std::string err;
        if (!ttd->SerializeSession(out, err))
            BenchmarkPathHelper::FailSetup("SerializeSession: " + err);
        fileBytes = out.str().size();
        emulator->Stop();
        emulator->Release();
        delete emulator;
        state.ResumeTiming();
    }
    const double seconds = kFrames / 50.0;
    state.counters["reads_per_second"] = static_cast<double>(reads) / seconds;
    state.counters["writes_per_second"] = static_cast<double>(writes) / seconds;
    state.counters["journal_bytes"] = static_cast<double>(journalBytes);
    state.counters["journal_bytes_per_second"] = static_cast<double>(journalBytes) / seconds;
    state.counters["file_bytes"] = static_cast<double>(fileBytes);
}
BENCHMARK(BM_TTD_PortJournal_TapeSession)->Unit(benchmark::kMillisecond)->Iterations(3);
