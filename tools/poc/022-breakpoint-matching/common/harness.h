#pragma once

// Replays a trace through a matcher under Google Benchmark, after checking the matcher against the reference.
//
// A matcher is any type with
//   Matcher(const std::vector<Breakpoint>& set, const PhysPage slots[4]);   // build (set time, not measured)
//   int  Match(uint8_t kind, uint16_t addr, const PhysPage slots[4]);       // one access: id or kNone
//   void Remap(int slot, PhysPage page, const PhysPage slots[4]);           // the paging port moved a slot
// The replay keeps the slot table (what Memory::MapZ80AddressToPhysicalPage reads) and hands it in.

#include <benchmark/benchmark.h>

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "breakpoints.h"
#include "trace.h"

namespace poc
{

inline std::vector<std::string> TraceFiles()
{
    std::vector<std::string> files;
    if (DIR* d = opendir(TraceDir().c_str()))
    {
        while (dirent* e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n.size() > 6 && n.substr(n.size() - 6) == ".trace")
                files.push_back(TraceDir() + "/" + n);
        }
        closedir(d);
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// Traces loaded once per process
inline const Trace& GetTrace(const std::string& path)
{
    static std::map<std::string, std::unique_ptr<Trace>> cache;
    auto& slot = cache[path];
    if (!slot)
    {
        slot = std::make_unique<Trace>();
        if (!slot->Load(path))
        {
            std::fprintf(stderr, "cannot load %s\n", path.c_str());
            std::exit(1);
        }
    }
    return *slot;
}

inline const TraceProfile& GetProfile(const Trace& t)
{
    static std::map<const Trace*, std::unique_ptr<TraceProfile>> cache;
    auto& slot = cache[&t];
    if (!slot)
        slot = std::make_unique<TraceProfile>(t);
    return *slot;
}

/// Every recorded trace (experiments may add synthetic ones to the list they register on)
inline std::vector<const Trace*> AllTraces()
{
    std::vector<const Trace*> traces;
    for (const std::string& path : TraceFiles())
        traces.push_back(&GetTrace(path));
    return traces;
}

/// Checks the matcher against the reference over the first `limit` events: same hit / miss on every access.
/// A mismatch is a broken matcher, not a slow one: the process stops.
template <typename M>
void Verify(const char* matcherName, const Trace& t, const std::vector<Breakpoint>& set, size_t limit)
{
    ReferenceMatcher ref(set);
    PhysPage slots[4];
    std::copy(t.initialSlots, t.initialSlots + 4, slots);
    auto mp = std::make_unique<M>(set, slots);  // matchers may hold large tables: on the heap
    M& m = *mp;
    const size_t n = std::min(limit, t.events.size());
    for (size_t i = 0; i < n; i++)
    {
        const Event& e = t.events[i];
        if (e.kind == KRemap)
        {
            slots[e.addr & 3] = e.value;
            m.Remap(e.addr & 3, e.value, slots);
            continue;
        }
        const bool want = ref.Match(e.kind, e.addr, slots) != kNone;
        const bool got = m.Match(e.kind, e.addr, slots) != kNone;
        if (want != got)
        {
            std::fprintf(stderr, "%s disagrees with the reference on %s event %zu (kind %u addr %04X): want %d got %d\n",
                         matcherName, t.name.c_str(), i, e.kind, e.addr, want, got);
            std::exit(3);
        }
    }
}

/// One measured replay: ns per access (remaps counted as events too, they are part of the work)
template <typename M>
void Replay(benchmark::State& state, const Trace& t, const std::vector<Breakpoint>& set)
{
    PhysPage slots[4];
    uint64_t hits = 0;
    for (auto _ : state)
    {
        state.PauseTiming();  // the build is set time, not access time
        std::copy(t.initialSlots, t.initialSlots + 4, slots);
        auto mp = std::make_unique<M>(set, slots);
        M& m = *mp;
        state.ResumeTiming();
        uint64_t h = 0;
        for (const Event& e : t.events)
        {
            if (e.kind == KRemap)
            {
                slots[e.addr & 3] = e.value;
                m.Remap(e.addr & 3, e.value, slots);
                continue;
            }
            h += m.Match(e.kind, e.addr, slots) != kNone;
        }
        benchmark::DoNotOptimize(h);
        hits = h;
    }
    state.counters["ns_per_access"] =
        benchmark::Counter(static_cast<double>(t.events.size()) * static_cast<double>(state.iterations()),
                           benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
    state.counters["hits"] = static_cast<double>(hits);
    state.counters["events"] = static_cast<double>(t.events.size());
}

/// Registers matcher M over the traces and every set recipe: "<matcher>/<set>/<trace>"
template <typename M>
void RegisterMatcher(const std::string& matcherName, const std::vector<SetSpec>& specs, size_t verifyLimit = 300000,
                     std::vector<const Trace*> traces = AllTraces())
{
    for (const Trace* tp : traces)
    {
        const Trace& t = *tp;
        for (const SetSpec& spec : specs)
        {
            std::vector<Breakpoint> set = MakeSet(spec, GetProfile(t));
            // The reference is O(set) per access: the whole trace for small sets, a prefix for large ones.
            // Checked once, when the benchmark first runs (a filtered-out benchmark costs nothing)
            const size_t limit = spec.count <= 100 ? t.events.size() : (spec.count <= 1000 ? verifyLimit : verifyLimit / 6);
            auto verified = std::make_shared<bool>(false);
            const std::string name = matcherName + "/" + spec.name + "/" + t.name;
            benchmark::RegisterBenchmark(name.c_str(), [&t, set, limit, verified, matcherName](benchmark::State& st) {
                if (!*verified)
                {
                    Verify<M>(matcherName.c_str(), t, set, limit);
                    *verified = true;
                }
                Replay<M>(st, t, set);
            })
                ->Unit(benchmark::kMillisecond)
                ->MinTime(0.3);
        }
    }
}

/// The unarmed debug path: per access, the one "any breakpoint of this kind?" flag every design keeps
/// (BreakpointHotState::hasExec / hasRead / ...), with none set. The READMEs report each matcher against it
struct UnarmedMatcher
{
    UnarmedMatcher(const std::vector<Breakpoint>&, const PhysPage*) {}
    int Match(uint8_t kind, uint16_t, const PhysPage*) { return _has[kind & 7] ? 0 : kNone; }
    void Remap(int, PhysPage, const PhysPage*) {}
    volatile uint8_t _has[8] = {};  // read from memory every access, as the real flag is
};

inline void RegisterFloor(std::vector<const Trace*> traces = AllTraces())
{
    for (const Trace* tp : traces)
    {
        const Trace& t = *tp;
        const std::string name = "unarmed/none/" + t.name;
        benchmark::RegisterBenchmark(name.c_str(), [&t](benchmark::State& st) { Replay<UnarmedMatcher>(st, t, {}); })
            ->Unit(benchmark::kMillisecond)
            ->MinTime(0.3);
    }
}

/// N = 0, 1, 10, 100, 1000, 10000 of one shape, cold and warm
inline std::vector<SetSpec> ScaleSpecs(SetSpec::Shape shape, const char* tag, uint16_t rangeLen = 256,
                                       std::vector<uint32_t> counts = {0, 1, 10, 100, 1000, 10000})
{
    std::vector<SetSpec> specs;
    for (bool warm : {false, true})
        for (uint32_t n : counts)
        {
            if (warm && n == 0)
                continue;
            SetSpec s;
            s.name = std::string(tag) + (warm ? "-warm-" : "-cold-") + std::to_string(n);
            s.count = n;
            s.shape = shape;
            s.warm = warm;
            s.rangeLen = rangeLen;
            specs.push_back(s);
        }
    return specs;
}

}  // namespace poc
