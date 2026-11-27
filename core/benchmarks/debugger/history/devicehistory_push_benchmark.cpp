#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "debugger/history/devicehistory.h"
#include "emulator/sound/chips/gs/gsporttrace.h"

/// The cost of one record on a device's path (debugger/history/devicehistory.h), per record.
///
/// The traffic is a GS card's as a capture of a tape load sees it: of 64 records 2 are the host's port writes, the
/// rest the card's own (interrupts, DAC fetches, its port reads). Variants, run interleaved:
///  - Off: the history off: the device's whole cost is the On() test
///  - Ring: recording every record into a ring of 65536
///  - RingFilter: the same with the record-time filter `side=host out=true` (compiled getters: GSTraceEvent names
///    its fields), 62 of 64 records left out
///  - RenderedFilter: a record that names no fields, the filter tested on its rendered ToState
namespace
{

std::vector<GSTraceEvent> Traffic()
{
    std::vector<GSTraceEvent> events(64);
    for (size_t i = 0; i < events.size(); i++)
    {
        GSTraceEvent& e = events[i];
        e.timestamp = static_cast<int64_t>(i) * 320;
        e.frameNumber = 100;
        e.pc = static_cast<uint16_t>(0x0100 + i);
        e.value = static_cast<uint8_t>(i);
        if (i == 7 || i == 40)
        {
            e.side = GSTraceSide::Host;
            e.port = 0xB3;
            e.flags = GSTraceFlags::kDirectionOut;
        }
        else if (i % 4 == 0)
        {
            e.side = GSTraceSide::Interrupt;
        }
        else if (i % 4 == 1)
        {
            e.side = GSTraceSide::GsInternal;
            e.port = 0x04;
        }
        else
        {
            e.side = GSTraceSide::DacFetch;
            e.port = static_cast<uint16_t>(0x6000 + i);
            e.channel = static_cast<uint8_t>(i & 3);
        }
    }
    return events;
}

struct PlainRecord
{
    int side = 0;
    int port = 0;
    int value = 0;
    bool out = false;

    void ToState(StateNode& node) const
    {
        node["side"] = side == 0 ? "host" : "gs";
        node["port"] = port;
        node["value"] = value;
        node["out"] = out;
    }
};

template <typename Record>
void Run(benchmark::State& state, const std::vector<Record>& records, history::Mode mode, const char* filter)
{
    history::DeviceHistory<Record> h("zxbus.1", "records", 65536);
    history::Config config;
    config.mode = mode;
#if __has_include("debugger/history/historyfilter.h")
    config.filter = filter;
#else
    if (filter[0] != 0)
    {
        state.SkipWithError("no record-time filter in this build");
        return;
    }
#endif
    std::string error;
    if (!h.Configure(config, error))
    {
        state.SkipWithError(error.c_str());
        return;
    }
    for (auto _ : state)
    {
        for (const Record& record : records)
        {
            if (h.On()) [[unlikely]]
                h.Push(record);
            benchmark::DoNotOptimize(&record);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(records.size()));
    state.counters["ns_per_record"] = benchmark::Counter(
        static_cast<double>(state.iterations() * records.size()), benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

void BM_DeviceHistory_Off(benchmark::State& state)
{
    Run(state, Traffic(), history::Mode::Off, "");
}
void BM_DeviceHistory_Ring(benchmark::State& state)
{
    Run(state, Traffic(), history::Mode::Ring, "");
}
void BM_DeviceHistory_RingFilter(benchmark::State& state)
{
    Run(state, Traffic(), history::Mode::Ring, "side=host out=true");
}
void BM_DeviceHistory_RenderedFilter(benchmark::State& state)
{
    std::vector<PlainRecord> records(64);
    for (size_t i = 0; i < records.size(); i++)
    {
        records[i].side = (i == 7 || i == 40) ? 0 : 1;
        records[i].port = static_cast<int>(i);
        records[i].out = (i & 1) != 0 || i == 40;
    }
    Run(state, records, history::Mode::Ring, "side=host out=true");
}

}  // namespace

BENCHMARK(BM_DeviceHistory_Off)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DeviceHistory_Ring)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DeviceHistory_RingFilter)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_DeviceHistory_RenderedFilter)->Unit(benchmark::kNanosecond);
