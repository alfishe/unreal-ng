/// @file ttd_matrix_benchmark.cpp
/// @brief The TTD benchmark matrix (PLAN #40 Phase 0, Step 2, TTD v2 requirements §5):
/// every engine x configuration x workload, one benchmark each, the metrics
/// BM-1..BM-8 as counters. Harness: core/src/debugger/ttd/bench/ttdbench.h.
///
/// Names: TTDMatrix/<engine>/<configuration>/<workload>
///
/// Environment:
///   UNREAL_TTD_BENCH_SET          ci (default, <= 2 min) | turbo (the V1b question) | full
///   UNREAL_TTD_BENCH_ENGINE       v1 (default) | all | a comma list
///   UNREAL_TTD_BENCH_PERIPHERALS  overlay on every configuration: none | ay+gs512+... (BR-5)
///   UNREAL_TTD_BENCH_FRAMES       measured frames per workload (overrides the matrix)
///   UNREAL_TTD_BENCH_SEEKS        random seek positions for BM-5 (default 200)
///   UNREAL_TTD_BENCH_OVERHEAD     0 skips BM-1 (four more runs per case)
///   UNREAL_TTD_BENCH_DIRTY        1 adds BM-8 (capture vs dirty 4 KB pieces)
///   UNREAL_TTD_BENCH_KEEP_SESSIONS <dir>: keep each case's saved .ttd session there
///                                 (input data for tools/poc/011-ttd-v2-capture-analysis/experiments)
///
/// Results: add --benchmark_format=json --benchmark_out=<file>; the JSON context
/// carries the engine set, git commit and build type. Compare runs with
/// tools/verification/ttd-bench/ttd_bench_compare.py.

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/ttd/bench/ttdbench.h"
#include "emulator/buildinfo.h"
#include "loaders/benchmark_path_helper.h"

namespace
{
std::string Env(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::string(fallback);
}

std::vector<std::string> Engines()
{
    const std::string wanted = Env("UNREAL_TTD_BENCH_ENGINE", "v1");
    if (wanted == "all")
        return ttd::bench::EngineNames();
    std::vector<std::string> names;
    std::stringstream in(wanted);
    std::string name;
    while (std::getline(in, name, ','))
        names.push_back(name);
    return names;
}

ttd::bench::Options MakeOptions(const std::string& set)
{
    ttd::bench::Options o;
    o.framesOverride = static_cast<uint32_t>(std::atoi(Env("UNREAL_TTD_BENCH_FRAMES", "0").c_str()));
    o.seekSamples = static_cast<uint32_t>(std::atoi(Env("UNREAL_TTD_BENCH_SEEKS", set == "ci" ? "40" : "200").c_str()));
    o.overhead = Env("UNREAL_TTD_BENCH_OVERHEAD", "1") != "0";
    o.dirtySweep = Env("UNREAL_TTD_BENCH_DIRTY", "0") == "1";
    o.resolveTestData = [](const std::string& relative) { return BenchmarkPathHelper::RequireTestDataFile(relative); };
    const std::filesystem::path scratch = BenchmarkPathHelper::findProjectRoot() / "scratch";
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);
    o.scratchDir = scratch.string();
    o.keepSessionDir = Env("UNREAL_TTD_BENCH_KEEP_SESSIONS", "");
    return o;
}

/// Registers the matrix before main() parses the benchmark filter
struct MatrixRegistrar
{
    MatrixRegistrar()
    {
        const std::string set = Env("UNREAL_TTD_BENCH_SET", "ci");
        const std::string overlay = Env("UNREAL_TTD_BENCH_PERIPHERALS", "");

        benchmark::AddCustomContext("ttd_bench_set", set);
        benchmark::AddCustomContext("ttd_bench_peripherals", overlay.empty() ? "matrix" : overlay);
        benchmark::AddCustomContext("git_commit", buildinfo::kGitCommit);
        benchmark::AddCustomContext("git_branch", buildinfo::kGitBranch);
        benchmark::AddCustomContext("unreal_build_type", buildinfo::kBuildType);

        std::vector<ttd::bench::Case> cases = ttd::bench::Matrix(set);
        if (!overlay.empty())
        {
            ttd::bench::PeripheralSet periph;
            std::string error;
            if (ttd::bench::PeripheralSet::Parse(overlay, periph, error))
                for (ttd::bench::Case& c : cases)
                {
                    c.config.peripherals = periph;
                    c.config.name += "+" + periph.Name();
                }
        }

        for (const std::string& engineName : Engines())
        {
            benchmark::AddCustomContext("ttd_engine_" + engineName, "yes");
            for (const ttd::bench::Case& c : cases)
            {
                const std::string name = "TTDMatrix/" + engineName + "/" + c.Name();
                benchmark::RegisterBenchmark(name, [engineName, c, set](benchmark::State& state) {
                    std::unique_ptr<ttd::bench::Engine> engine = ttd::bench::CreateEngine(engineName);
                    if (!engine)
                    {
                        state.SkipWithError(("unknown engine " + engineName).c_str());
                        return;
                    }
                    const ttd::bench::Options options = MakeOptions(set);
                    ttd::bench::Result result;
                    for (auto _ : state)
                        result = ttd::bench::RunCase(*engine, c, options);
                    if (!result.ok)
                    {
                        state.SkipWithError(result.error.c_str());
                        return;
                    }
                    for (const auto& [metric, value] : result.metrics)
                        state.counters[metric] = benchmark::Counter(value);
                })
                    ->Iterations(1)
                    ->Unit(benchmark::kMillisecond);
            }
        }
    }
};

const MatrixRegistrar g_registrar;
}  // namespace
