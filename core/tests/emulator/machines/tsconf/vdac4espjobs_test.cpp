// VDAC4 ESP app jobs (design.md §7.5): while the app owns the board, the runtime runs the ESP as jobs, on its
// worker thread or at once; the outputs reach the firmware a fixed number of Polls later. The thread must not change
// what the firmware and the board see: the same accesses, the same outputs, in the same Poll.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/vdac4/esp/espapphost.h"
#include "emulator/platforms/tsconf/vdac4/esp/espxtensaruntime.h"

using namespace vdac4;

namespace
{

std::vector<uint8_t> ReadFile(const std::string& relative)
{
    std::ifstream file(TestPathHelper::GetTestDataPath(relative), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// The firmware and the Tang as the app sees them; everything it gets is written to `trace` with the Poll it came in
class TraceHost final : public IVdac4AppHost, public IVdac4TangLink
{
public:
    std::ostringstream trace;  ///< the firmware's side (main thread)
    std::ostringstream links;  ///< the LINK (the job's thread; the main thread only when no job runs)
    uint32_t nowMs = 0;
    uint32_t poll = 0;
    std::thread::id mainThread = std::this_thread::get_id();
    uint64_t offThreadAccesses = 0;
    uint64_t linkAccesses = 0;
    uint64_t stamp = 0;  ///< the stamp the running job got (written by the hooks)

    bool SendFrame(uint8_t command, const uint8_t*, size_t size) override
    {
        trace << poll << " frame " << int(command) << ' ' << size << '\n';
        return true;
    }
    int SendEvent(uint8_t code, const uint8_t*, size_t size) override
    {
        trace << poll << " event " << int(code) << ' ' << size << '\n';
        return kZmOk;
    }
    int Reply(uint8_t response, const uint8_t*, size_t size) override
    {
        trace << poll << " reply " << int(response) << ' ' << size << '\n';
        return kZmOk;
    }
    int SetMediaRange(uint8_t first, uint8_t last) override
    {
        trace << poll << " media " << int(first) << ' ' << int(last) << '\n';
        return kZmOk;
    }
    void AppDone() override { trace << poll << " done\n"; }
    uint32_t NowMs() const override { return nowMs; }
    int Resource(const char*, const uint8_t*&, size_t&) override { return kZmNotFound; }
    void ReleaseResource(const char*) override {}
    int ReadModule(const char*, std::vector<uint8_t>&) override { return kZmNotFound; }
    void FtOpened(bool selected) override { trace << poll << " ft_open " << selected << '\n'; }
    IVdac4TangLink* Tang() override { return this; }
    size_t EventQueueRoom() const override { return 8; }

    // The LINK: the access and the stamp of the job it came in (the job's own time, not the Poll it ran in)
    bool LinkRead(uint32_t address, uint32_t& value) override
    {
        Access();
        links << "@" << stamp << " read " << address << '\n';
        value = address ^ 0x5A5A5A5Au;
        return true;
    }
    bool LinkWrite(uint32_t address, uint32_t value) override
    {
        Access();
        links << "@" << stamp << " write " << address << ' ' << value << '\n';
        return true;
    }

private:
    void Access()
    {
        ++linkAccesses;
        if (std::this_thread::get_id() != mainThread)
            ++offThreadAccesses;
    }
};

struct DoomRun
{
    std::string trace;
    std::string links;
    std::string log;
    uint64_t espUs = 0;
    EspJobStats stats;
    uint64_t offThreadAccesses = 0;
    uint64_t linkAccesses = 0;
};

/// DOOM (no WAD: it idles on its IO signal and answers the status command) for `ms` emulated milliseconds, polled
/// every millisecond, the status command every 25 ms
DoomRun RunDoom(bool threaded, uint32_t latency, uint32_t ms)
{
    DoomRun run;
    TraceHost host;
    EspXtensaRuntime runtime(nullptr, [&run, &host](const std::string& line) {
        run.log += std::to_string(host.poll) + " " + line + "\n";
    });
    EspJobHooks hooks;
    hooks.hosted = []() { return true; };
    hooks.stamp = [&host]() { return static_cast<uint64_t>(host.nowMs); };
    hooks.begin = [&host](uint64_t stamp) { host.stamp = stamp; };
    hooks.hostedAfter = [](uint64_t) { return true; };
    runtime.SetJobHooks(hooks);
    runtime.SetThreaded(threaded);
    runtime.SetLatency(latency);

    Vdac4AppInfo info;
    EXPECT_EQ(runtime.StartApp("DOOM.ELF", ReadFile("vdac4/DOOM.ELF"), host, info), kZmOk) << run.log;
    for (uint32_t i = 1; i <= ms; ++i)
    {
        host.poll = i;
        host.nowMs = i;
        if (i % 25 == 0)
            runtime.Command(0x73, nullptr, 0);
        runtime.Poll(host.nowMs);
    }
    runtime.Flush();
    run.trace = host.trace.str();
    run.links = host.links.str();
    run.espUs = runtime.Host().NowUs();
    run.stats = runtime.Stats();
    run.offThreadAccesses = host.offThreadAccesses;
    run.linkAccesses = host.linkAccesses;
    runtime.StopApp();
    return run;
}

}  // namespace

/// Threaded and inline jobs: the same trace, log and ESP time (latency 1 and 4)
TEST(Vdac4EspJobs_Test, ThreadedEqualsInline)
{
    for (uint32_t latency : {1u, 4u})
    {
        const DoomRun inlineRun = RunDoom(false, latency, 200);
        const DoomRun threadedRun = RunDoom(true, latency, 200);
        EXPECT_GT(inlineRun.stats.jobs, 150u) << "latency " << latency;
        EXPECT_EQ(inlineRun.stats.threadedJobs, 0u);
        EXPECT_EQ(threadedRun.stats.threadedJobs, threadedRun.stats.jobs);
        EXPECT_EQ(threadedRun.stats.jobs, inlineRun.stats.jobs);
        EXPECT_EQ(threadedRun.espUs, inlineRun.espUs) << "latency " << latency;
        EXPECT_EQ(threadedRun.trace, inlineRun.trace) << "latency " << latency;
        EXPECT_EQ(threadedRun.links, inlineRun.links) << "latency " << latency;
        EXPECT_EQ(threadedRun.log, inlineRun.log) << "latency " << latency;
        EXPECT_EQ(threadedRun.offThreadAccesses, threadedRun.linkAccesses);
        EXPECT_EQ(inlineRun.offThreadAccesses, 0u);
        EXPECT_NE(inlineRun.trace.find("reply 243"), std::string::npos) << inlineRun.trace;  // F3: the status
    }
}

/// Outputs of a job reach the firmware `latency` Polls after it was posted: a later latency only delays them
TEST(Vdac4EspJobs_Test, LatencyDelaysOutputs)
{
    const DoomRun one = RunDoom(false, 1, 120);
    const DoomRun four = RunDoom(false, 4, 120);
    EXPECT_EQ(one.espUs, four.espUs);
    EXPECT_FALSE(one.log.empty());
    // The same lines in the same order; each no earlier than with latency 1
    std::istringstream a(one.log), b(four.log);
    std::string lineA, lineB;
    size_t lines = 0;
    while (std::getline(a, lineA) && std::getline(b, lineB))
    {
        const size_t spaceA = lineA.find(' '), spaceB = lineB.find(' ');
        EXPECT_EQ(lineA.substr(spaceA), lineB.substr(spaceB));
        EXPECT_GE(std::stoul(lineB.substr(0, spaceB)), std::stoul(lineA.substr(0, spaceA))) << lineA << " / " << lineB;
        ++lines;
    }
    EXPECT_GT(lines, 0u);
}
