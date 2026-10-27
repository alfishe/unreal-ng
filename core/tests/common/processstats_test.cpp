// ProcessStats: the host process's own counters (common/processstats.h) - a sample of this test process is sane
// (CPU time grows with work, memory and threads are non-zero where the platform reports them), and the sampler's CPU %
// is the CPU time over the wall time between two samples, cached within its interval

#include "pch.h"

#include "common/processstats.h"

#include <chrono>
#include <cstdint>
#include <vector>

TEST(ProcessStats_Test, ASampleOfThisProcessIsSane)
{
    const ProcessSample before = SampleProcess();
    ASSERT_TRUE(before.ok) << "the platform's CPU times answered";
    // Some work: CPU time can only grow
    volatile uint64_t sink = 0;
    for (uint64_t i = 0; i < 2'000'000; i++)
        sink = sink + i * 7;
    const ProcessSample after = SampleProcess();
    EXPECT_GE(after.cpuTimeUs, before.cpuTimeUs);
    EXPECT_EQ(after.cpuTimeUs, after.userTimeUs + after.systemTimeUs);
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    EXPECT_GT(after.residentBytes, 1024u * 1024u) << "a test binary is resident in more than a megabyte";
    EXPECT_GE(after.peakResidentBytes, after.residentBytes / 2) << "the peak is not below what is resident";
    EXPECT_GT(after.virtualBytes, 0u);
    EXPECT_GE(after.threads, 1u) << "this thread at least";
#endif
    EXPECT_GE(HostLogicalCores(), 1u);
}

TEST(ProcessStats_Test, CpuPercentIsCpuTimeOverWallTimeAndCachedWithinTheInterval)
{
    ProcessStatsSampler sampler(0.5);
    const auto t0 = ProcessStatsSampler::Clock::time_point(std::chrono::hours(1));  // a fixed origin: ReadAt() is given the times
    ProcessSample a;
    a.ok = true;
    a.cpuTimeUs = 1'000'000;
    const ProcessStats first = sampler.ReadAt(t0, a);
    EXPECT_EQ(first.cpuPercent, 0.0) << "one sample: no CPU % yet";
    EXPECT_EQ(first.intervalSeconds, 0.0);

    ProcessSample b = a;
    b.cpuTimeUs += 750'000;  // 0.75 s of CPU over 0.5 s of wall: 150 % (two threads)
    const ProcessStats second = sampler.ReadAt(t0 + std::chrono::milliseconds(500), b);
    EXPECT_NEAR(second.cpuPercent, 150.0, 1e-6);
    EXPECT_NEAR(second.cpuPercentOfHost, 150.0 / second.cores, 1e-6);
    EXPECT_NEAR(second.intervalSeconds, 0.5, 1e-9);
    EXPECT_EQ(second.samples, 2u);

    // Read(): a fresh sample after the interval, the cached one within it
    ProcessStatsSampler live(60.0);
    const ProcessStats one = live.Read();
    const ProcessStats two = live.Read();
    EXPECT_EQ(one.samples, 1u);
    EXPECT_EQ(two.samples, 1u) << "within the interval the OS is not asked again";
    EXPECT_EQ(two.sample.cpuTimeUs, one.sample.cpuTimeUs);
}
