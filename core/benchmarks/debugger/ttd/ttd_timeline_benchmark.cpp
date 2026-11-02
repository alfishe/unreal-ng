/// @file ttd_timeline_benchmark.cpp
/// @brief The TTD timeline summary (docs/inprogress/2026-10-31-ttd-timeline-summary, plan.md B1 / B2): the one
/// `timeline` call against the loop of old calls an agent runs today for the same picture of a recording.
///
/// Sessions: the synthetic phase program (core/src/debugger/ttd/bench/ttdtimelinefixture.h), recorded once per
/// process: 3,000 frames (10 cycles of 75-frame phases) and 50,000 frames (50 cycles of 250-frame phases).
///
/// The old loop, per window of N buckets (range(1): 20 or 100): `status`, `coverage-summary` for each kind,
/// `markers`, `bookmarks`, then per bucket `port-events` for key / ear / border / beeper / ay-write (limit 1,000, so a
/// busy bucket is cut: its count is a lower bound) and `memory-diff` of the screen page. Counters: `calls`,
/// `reply_bytes` (the replies as JSON text, what a client reads). The one call: range(1) = ports on / off, range(2) =
/// overview buckets (0 = phases only, the default reply).
///
/// TTD_TIMELINE_BASELINE (defined when this file is built against the code before the change) keeps only the old loop.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "debugger/ttd/bench/ttdtimelinefixture.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/state/statenodejson.h"

namespace
{
ttd::bench::TimelineSession& Get(unsigned frames)
{
    static std::map<unsigned, std::unique_ptr<ttd::bench::TimelineSession>> sessions;
    std::unique_ptr<ttd::bench::TimelineSession>& s = sessions[frames];
    if (!s)
        s = frames >= 50000 ? ttd::bench::RecordTimelineProgram(250, 50) : ttd::bench::RecordTimelineProgram(75, 10);
    return *s;
}

struct Totals
{
    size_t calls = 0;
    size_t bytes = 0;
    bool failed = false;
};

void Call(ttd::TTDControl& control, const std::string& verb, std::map<std::string, std::string> options, Totals& t)
{
    const ttd::TTDReply reply = control.Execute({verb, std::move(options)});
    ++t.calls;
    t.bytes += StateNodeToJsonText(reply.body).size();
    t.failed = t.failed || !reply.Ok();
}

void OldLoop(ttd::bench::TimelineSession& s, uint64_t buckets, Totals& t)
{
    ttd::TTDControl control(s.context);
    const uint64_t from = s.firstFrame;
    const uint64_t to = s.endFrame - 1;
    Call(control, "status", {}, t);
    for (const char* kind : {"executed", "written", "read"})
        Call(control, "coverage-summary",
             {{"from_frame", std::to_string(from)}, {"to_frame", std::to_string(to)}, {"kind", kind},
              {"limit", std::to_string(buckets)}},
             t);
    Call(control, "markers", {}, t);
    Call(control, "bookmarks", {}, t);
    const uint64_t frames = to - from + 1;
    const uint64_t size = (frames + buckets - 1) / buckets;
    for (uint64_t b = from; b <= to; b += size)
    {
        const uint64_t e = std::min<uint64_t>(to, b + size - 1);
        for (const char* event : {"key", "ear", "border", "beeper", "ay-write"})
            Call(control, "port-events",
                 {{"event", event}, {"from", std::to_string(b)}, {"to", std::to_string(e)}, {"limit", "1000"}}, t);
        Call(control, "memory-diff",
             {{"space", "ram5"}, {"from_frame", std::to_string(b)}, {"to_frame", std::to_string(e)}, {"limit", "1"}}, t);
    }
}

void BM_Timeline_OldLoop(benchmark::State& state)
{
    ttd::bench::TimelineSession& s = Get(static_cast<unsigned>(state.range(0)));
    if (!s.Ok())
        return state.SkipWithError("no session");
    Totals t;
    for (auto _ : state)
    {
        t = Totals();
        OldLoop(s, static_cast<uint64_t>(state.range(1)), t);
    }
    if (t.failed)
        return state.SkipWithError("a call failed");
    state.counters["calls"] = static_cast<double>(t.calls);
    state.counters["reply_bytes"] = static_cast<double>(t.bytes);
    state.counters["frames"] = static_cast<double>(s.endFrame - s.firstFrame);
}
BENCHMARK(BM_Timeline_OldLoop)
    ->Args({3000, 20})
    ->Args({3000, 100})
    ->Args({50000, 20})
    ->Args({50000, 100})
    ->Unit(benchmark::kMillisecond);

#ifndef TTD_TIMELINE_BASELINE
/// The one call over the same window; range(1) = ports (tier 2) on / off, range(2) = overview buckets (0 = none)
void BM_Timeline_OneCall(benchmark::State& state)
{
    ttd::bench::TimelineSession& s = Get(static_cast<unsigned>(state.range(0)));
    if (!s.Ok())
        return state.SkipWithError("no session");
    ttd::TTDControl control(s.context);
    Totals t;
    for (auto _ : state)
    {
        t = Totals();
        std::map<std::string, std::string> options{{"from_frame", std::to_string(s.firstFrame)},
                                                   {"to_frame", std::to_string(s.endFrame - 1)},
                                                   {"ports", state.range(1) ? "true" : "false"}};
        if (state.range(2))
        {
            options["buckets"] = "true";
            options["limit"] = std::to_string(state.range(2));
        }
        Call(control, "timeline", std::move(options), t);
    }
    if (t.failed)
        return state.SkipWithError("the call failed");
    state.counters["calls"] = static_cast<double>(t.calls);
    state.counters["reply_bytes"] = static_cast<double>(t.bytes);
    state.counters["frames"] = static_cast<double>(s.endFrame - s.firstFrame);
}
BENCHMARK(BM_Timeline_OneCall)
    ->Args({3000, 1, 0})
    ->Args({3000, 0, 0})
    ->Args({3000, 1, 20})
    ->Args({3000, 1, 100})
    ->Args({50000, 1, 0})
    ->Args({50000, 0, 0})
    ->Args({50000, 1, 20})
    ->Args({50000, 1, 100})
    ->Unit(benchmark::kMillisecond);
#endif

}  // namespace
