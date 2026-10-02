/// @file ttdbench_test.cpp
/// @brief The TTD CI gate (PLAN #40 Phase 0, Step 2, TTD v2 requirements BR-7 / BR-8) and
/// checks of the benchmark harness itself (core/src/debugger/ttd/bench/).
///
/// The gate runs the "ci" subset of the benchmark matrix through the same
/// harness core-benchmarks uses, with the engine every build ships, and checks:
///
///  - **Bytes against the stored baseline** (testdata/ttd/bench/v1-ci-gate.txt):
///    every stream per frame, the session footprint and the saved file size.
///    The workloads are replayable (fixed start state, scripted input, frozen
///    RTC, zeroed power-on RAM), so these counts repeat to the byte in any
///    test order; losing compression or delta
///    coding, a new per-frame stream or a bigger checkpoint shows up here with
///    no clock involved. Capacity-based metrics carry a tolerance in the file
///    because allocators grow containers differently.
///  - **Capture work against the stored baseline**, the same way: per frame,
///    the RAM pages the capture walked, the bytes it copied into the delta
///    base, scanned, handed to zstd, the zstd calls, the chain links it
///    decoded and the device-state bytes (`bm2_work_*`). A capture path that
///    starts walking, copying or compressing more - a scan gone quadratic, a
///    second full copy - changes these counts, and counts do not depend on the
///    host or its load.
///
/// No assertion here reads a clock. The first version checked capture as a
/// share of frame time (median); on a shared host at load ~150 the 4 MB
/// copies of ZX-Evo capture slowed far more than emulation and the share
/// crossed any sane budget (56% against 50%). Capture time stays a benchmark
/// metric (bm2_capture_us_*, bm2_capture_share_pct), read with the host load
/// in mind.
///  - **Seek and save / load work** on every case (the harness fails the case
///    otherwise).
///
/// It replaces TTD_Capture_Cost_Gate_Test, whose frame-time rounds were
/// load-sensitive. A deliberate format or capture change updates the baseline:
///
///   UNREAL_TTD_BENCH_OVERHEAD=0 core-benchmarks --benchmark_filter=TTDMatrix/v1/
///       --benchmark_format=json --benchmark_out=scratch/ci.json
///   python3 tools/verification/ttd-bench/ttd_bench_compare.py export-gate
///       scratch/ci.json testdata/ttd/bench/v1-ci-gate.txt
///
/// Each gate case boots a machine and records 60 frames (~0.5-1 s in Release),
/// well past the 50 ms guideline: the workload is the thing being gated.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/bench/ttdbench.h"

namespace ttd::bench
{
/// gtest prints a failing parameter with this instead of its raw bytes
void PrintTo(const Case& c, std::ostream* os)
{
    *os << c.Name();
}
}  // namespace ttd::bench

namespace
{

/// case name -> metric -> {value, tolerance percent}
struct Expected
{
    double value = 0;
    double tolerancePct = 0;
};
using Baseline = std::map<std::string, std::map<std::string, Expected>>;

bool LoadBaseline(const std::string& path, Baseline& out, std::string& error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "cannot open " + path;
        return false;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        lineNo++;
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string caseName, metric;
        Expected e;
        if (!(fields >> caseName >> metric >> e.value >> e.tolerancePct))
        {
            error = path + ":" + std::to_string(lineNo) + ": expected <case> <metric> <value> <tolerance>";
            return false;
        }
        out[caseName][metric] = e;
    }
    return true;
}

const Baseline& GateBaseline()
{
    static const Baseline baseline = [] {
        Baseline b;
        std::string error;
        if (!LoadBaseline(TestPathHelper::GetTestDataPath("ttd/bench/v1-ci-gate.txt"), b, error))
            ADD_FAILURE() << error;
        return b;
    }();
    return baseline;
}

ttd::bench::Options GateOptions()
{
    ttd::bench::Options o;
    o.seekSamples = 10;  // enough to prove seeks work; their timing is the benchmark's job
    o.seekRepeats = 1;
    o.overhead = false;  // BM-1 needs four more machines per case
    o.saveLoad = true;   // the file size is a gated byte metric
    o.resolveTestData = [](const std::string& relative) { return TestPathHelper::GetTestDataPath(relative); };
    const std::filesystem::path dir = TestPathHelper::GetUniqueTestScratchPath("ttdbench");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    o.scratchDir = dir.string();
    return o;
}

std::string TestName(const ttd::bench::Case& c)
{
    std::string name = c.Name();
    for (char& ch : name)
        if (!std::isalnum(static_cast<unsigned char>(ch)))
            ch = '_';
    return name;
}

}  // namespace

class TTDBench_Test : public ::testing::TestWithParam<ttd::bench::Case>
{
};

TEST_P(TTDBench_Test, CiGate)
{
    const ttd::bench::Case& c = GetParam();
    const auto expectedIt = GateBaseline().find(c.Name());
    ASSERT_NE(expectedIt, GateBaseline().end()) << c.Name() << " is in the ci matrix but not in the gate baseline";

    std::unique_ptr<ttd::bench::Engine> engine = ttd::bench::CreateEngine("v1");
    ASSERT_NE(engine, nullptr);
    const ttd::bench::Result result = ttd::bench::RunCase(*engine, c, GateOptions());
    ASSERT_TRUE(result.ok) << c.Name() << ": " << result.error;

    for (const auto& [metric, expected] : expectedIt->second)
    {
        const auto actual = result.metrics.find(metric);
        if (actual == result.metrics.end())
        {
            ADD_FAILURE() << c.Name() << ": the harness no longer reports " << metric;
            continue;
        }
        const double allowed = std::max(1e-6, std::abs(expected.value) * expected.tolerancePct / 100.0);
        EXPECT_LE(std::abs(actual->second - expected.value), allowed)
            << c.Name() << " " << metric << ": " << actual->second << " vs baseline " << expected.value
            << " (tolerance " << expected.tolerancePct << "%). A capture or format change that is meant "
            << "has to update testdata/ttd/bench/v1-ci-gate.txt (see the header of this file).";
    }

    // The capture-cost check lives in the baseline: without the work metrics
    // it would silently check nothing
    for (const char* metric : {"bm2_work_pages_visited_opf", "bm2_work_delta_base_bpf", "bm2_work_compress_input_bpf"})
        EXPECT_TRUE(expectedIt->second.count(metric))
            << c.Name() << ": the gate baseline has no " << metric << " - re-export it (see the header of this file)";

    // Informational only (a clock): visible in the test report, never asserted
    RecordProperty("capture_share_pct", std::to_string(result.metrics.at("bm2_capture_share_pct")));
}

INSTANTIATE_TEST_SUITE_P(Ci, TTDBench_Test, ::testing::ValuesIn(ttd::bench::Matrix("ci")),
                         [](const ::testing::TestParamInfo<ttd::bench::Case>& info) { return TestName(info.param); });

/// Every matrix names cases uniquely: the benchmark names and the gate
/// baseline are keyed by them
TEST(TTDBench_Matrix_Test, CaseNamesAreUnique)
{
    for (const char* set : {"ci", "turbo", "full"})
    {
        std::map<std::string, int> seen;
        for (const ttd::bench::Case& c : ttd::bench::Matrix(set))
            EXPECT_EQ(++seen[c.Name()], 1) << set << ": " << c.Name() << " appears twice";
        EXPECT_FALSE(seen.empty()) << set;
    }
}

TEST(TTDBench_Matrix_Test, PeripheralSetRoundTrips)
{
    ttd::bench::PeripheralSet set;
    std::string error;
    ASSERT_TRUE(ttd::bench::PeripheralSet::Parse("moon+gs512+tsfm", set, error)) << error;
    ttd::bench::PeripheralSet again;
    ASSERT_TRUE(ttd::bench::PeripheralSet::Parse(set.Name(), again, error)) << error;
    EXPECT_EQ(again.Name(), set.Name());

    EXPECT_FALSE(ttd::bench::PeripheralSet::Parse("gs256", set, error));
    EXPECT_FALSE(error.empty());
}

TEST(TTDBench_Matrix_Test, ByteMetricsAreTheDeterministicOnes)
{
    EXPECT_TRUE(ttd::bench::IsByteMetric("bm3_ram_payload_bpf"));
    EXPECT_TRUE(ttd::bench::IsByteMetric("bm7_file_bytes"));
    EXPECT_TRUE(ttd::bench::IsByteMetric("checkpoints"));
    EXPECT_TRUE(ttd::bench::IsByteMetric("bm2_work_compress_calls_opf"));
    EXPECT_FALSE(ttd::bench::IsByteMetric("bm2_capture_us_p99"));
    EXPECT_FALSE(ttd::bench::IsByteMetric("bm2_capture_share_pct"));
}
