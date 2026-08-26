#include "pch.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// Benchmark for breakpoint hot path performance
/// Measures per-access cost in four states:
///   1. No breakpoints armed
///   2. 1 execute breakpoint (cost on read/write paths)
///   3. 1 write breakpoint at different address (cost on unrelated writes)
///   4. 10 mixed breakpoints
///
/// Expected Phase 0 improvement:
///   Before: ~25-50 ns per access when any breakpoint is armed
///   After:  ~3-4 ns for unrelated accesses (10x improvement)

#ifdef NDEBUG
constexpr double MAX_MISS_PATH_NS = 5.0;
constexpr double MAX_HIT_PATH_NS = 200.0;
#else
// Debug builds have significant overhead for memory access and test execution
constexpr double MAX_MISS_PATH_NS = 50.0;
constexpr double MAX_HIT_PATH_NS = 1000.0;
#endif

class BreakpointHotpathBench : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    BreakpointManagerCUT* _brkManager = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Initialize Memory component (required for MapZ80AddressToPhysicalPage)
        _context->pMemory = new Memory(_context);
        _context->pMemory->Reset();

        _brkManager = new BreakpointManagerCUT(_context);
    }

    void TearDown() override
    {
        if (_brkManager)
        {
            delete _brkManager;
            _brkManager = nullptr;
        }

        if (_context)
        {
            if (_context->pMemory)
            {
                delete _context->pMemory;
                _context->pMemory = nullptr;
            }
            delete _context;
            _context = nullptr;
        }

        MessageCenter::DisposeDefaultMessageCenter();
    }

    // Measure average time per call in nanoseconds
    template <typename Func>
    double MeasureNsPerCall(Func&& func, size_t iterations)
    {
        // Warmup
        for (size_t i = 0; i < iterations / 10; ++i)
        {
            func();
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < iterations; ++i)
        {
            func();
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        return static_cast<double>(durationNs) / static_cast<double>(iterations);
    }
};

/// Benchmark: cost of HandleMemoryRead with no breakpoints
TEST_F(BreakpointHotpathBench, NoBreakpoints_MemRead)
{
    constexpr size_t ITERATIONS = 1'000'000;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint16_t> addrDist(0, 0xFFFF);

    // Pregenerate random addresses
    std::vector<uint16_t> addresses(ITERATIONS);
    for (auto& addr : addresses)
    {
        addr = addrDist(rng);
    }

    size_t idx = 0;
    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandleMemoryRead(addresses[idx++ % ITERATIONS]);
        },
        ITERATIONS);

    std::cout << "[NoBreakpoints_MemRead] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // With no breakpoints, should be nearly instant (empty check)
    EXPECT_LT(nsPerCall, MAX_MISS_PATH_NS) << "No-breakpoint path should be < " << MAX_MISS_PATH_NS << " ns";
}

/// Benchmark: cost of HandleMemoryRead with 1 exec breakpoint (read path should be cheap)
TEST_F(BreakpointHotpathBench, OneExecBp_MemRead)
{
    constexpr size_t ITERATIONS = 1'000'000;

    // Add 1 execute breakpoint at address 0x0000
    _brkManager->AddExecutionBreakpoint(0x0000);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint16_t> addrDist(0x0100, 0xFFFF);  // Avoid bp address

    std::vector<uint16_t> addresses(ITERATIONS);
    for (auto& addr : addresses)
    {
        addr = addrDist(rng);
    }

    size_t idx = 0;
    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandleMemoryRead(addresses[idx++ % ITERATIONS]);
        },
        ITERATIONS);

    std::cout << "[OneExecBp_MemRead] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // With only exec bp, read path hits kind flag (hasRead==0) and returns immediately
    EXPECT_LT(nsPerCall, MAX_MISS_PATH_NS) << "Kind-filtered path should be < " << MAX_MISS_PATH_NS << " ns";
}

/// Benchmark: cost of HandleMemoryWrite with 1 write breakpoint at different address
TEST_F(BreakpointHotpathBench, OneWriteBp_MemWriteMiss)
{
    constexpr size_t ITERATIONS = 1'000'000;

    // Add 1 write breakpoint at address 0x5C78
    _brkManager->AddMemWriteBreakpoint(0x5C78);

    std::mt19937 rng(12345);
    // Random addresses, mostly not 0x5C78
    std::uniform_int_distribution<uint16_t> addrDist(0x0000, 0xFFFF);

    std::vector<uint16_t> addresses(ITERATIONS);
    for (auto& addr : addresses)
    {
        addr = addrDist(rng);
        if (addr == 0x5C78)
            addr = 0x5C79;  // Ensure we're measuring miss path
    }

    size_t idx = 0;
    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandleMemoryWrite(addresses[idx++ % ITERATIONS]);
        },
        ITERATIONS);

    std::cout << "[OneWriteBp_MemWriteMiss] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // Miss path checks kind flag + address filter, no hash lookup
    EXPECT_LT(nsPerCall, MAX_MISS_PATH_NS) << "Miss path should be < " << MAX_MISS_PATH_NS << " ns";
}

/// Benchmark: cost when we actually hit the breakpoint address
TEST_F(BreakpointHotpathBench, OneWriteBp_MemWriteHit)
{
    constexpr size_t ITERATIONS = 1'000'000;

    // Add 1 write breakpoint at address 0x5C78
    _brkManager->AddMemWriteBreakpoint(0x5C78);

    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandleMemoryWrite(0x5C78);
        },
        ITERATIONS);

    std::cout << "[OneWriteBp_MemWriteHit] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // Hit path cost should remain similar (we still do the lookup)
    // This is expected to stay ~25-50 ns - the optimization is for the miss path
    EXPECT_LT(nsPerCall, MAX_HIT_PATH_NS) << "Hit path sanity check";
}

/// Benchmark: cost with 10 mixed breakpoints, miss path
TEST_F(BreakpointHotpathBench, TenMixedBps_MemWriteMiss)
{
    constexpr size_t ITERATIONS = 1'000'000;

    // Add 10 mixed breakpoints at various addresses
    _brkManager->AddExecutionBreakpoint(0x0000);
    _brkManager->AddExecutionBreakpoint(0x0100);
    _brkManager->AddMemReadBreakpoint(0x4000);
    _brkManager->AddMemReadBreakpoint(0x4100);
    _brkManager->AddMemWriteBreakpoint(0x5C78);
    _brkManager->AddMemWriteBreakpoint(0x5C00);
    _brkManager->AddMemWriteBreakpoint(0x6000);
    _brkManager->AddPortInBreakpoint(0xFE);
    _brkManager->AddPortOutBreakpoint(0xFE);
    _brkManager->AddCombinedMemoryBreakpoint(0x8000, BRK_MEM_READ | BRK_MEM_WRITE);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint16_t> addrDist(0xA000, 0xFFFF);  // Avoid all bp addresses

    std::vector<uint16_t> addresses(ITERATIONS);
    for (auto& addr : addresses)
    {
        addr = addrDist(rng);
    }

    size_t idx = 0;
    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandleMemoryWrite(addresses[idx++ % ITERATIONS]);
        },
        ITERATIONS);

    std::cout << "[TenMixedBps_MemWriteMiss] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // Miss path cost is independent of breakpoint count
    EXPECT_LT(nsPerCall, MAX_MISS_PATH_NS) << "Miss path should be < " << MAX_MISS_PATH_NS << " ns regardless of bp count";
}

/// Benchmark: HandlePCChange (execute path)
TEST_F(BreakpointHotpathBench, OneExecBp_PCChangeMiss)
{
    constexpr size_t ITERATIONS = 1'000'000;

    _brkManager->AddExecutionBreakpoint(0x0000);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint16_t> addrDist(0x0100, 0xFFFF);

    std::vector<uint16_t> addresses(ITERATIONS);
    for (auto& addr : addresses)
    {
        addr = addrDist(rng);
    }

    size_t idx = 0;
    double nsPerCall = MeasureNsPerCall(
        [&]()
        {
            _brkManager->HandlePCChange(addresses[idx++ % ITERATIONS]);
        },
        ITERATIONS);

    std::cout << "[OneExecBp_PCChangeMiss] " << std::fixed << std::setprecision(2) << nsPerCall
              << " ns/call" << std::endl;

    // Miss path checks kind flag + address filter
    EXPECT_LT(nsPerCall, MAX_MISS_PATH_NS) << "PC change miss path should be < " << MAX_MISS_PATH_NS << " ns";
}

/// Summary test that prints all results in a table
TEST_F(BreakpointHotpathBench, PrintSummary)
{
    std::cout << "\n=== Breakpoint Hot Path Benchmark Summary ===" << std::endl;
    std::cout << "Run individual tests for detailed measurements." << std::endl;
    std::cout << "\nTarget after Phase 0:" << std::endl;
    std::cout << "  - Miss path (unrelated address): <= 5 ns" << std::endl;
    std::cout << "  - Kind-filtered path (no bp of this kind): <= 2 ns" << std::endl;
    std::cout << "  - Hit path: unchanged (~25-50 ns)" << std::endl;
    SUCCEED();
}

/// Validation: verify hot state filter matches breakpoint descriptors
TEST_F(BreakpointHotpathBench, HotStateFilterValidation)
{
    const BreakpointHotState* hot = _brkManager->GetHotState();

    // Initially empty - all flags should be 0
    EXPECT_EQ(hot->hasExec, 0);
    EXPECT_EQ(hot->hasRead, 0);
    EXPECT_EQ(hot->hasWrite, 0);
    EXPECT_EQ(hot->hasPortIn, 0);
    EXPECT_EQ(hot->hasPortOut, 0);

    // Add exec breakpoint at 0x0000
    _brkManager->AddExecutionBreakpoint(0x0000);
    EXPECT_EQ(hot->hasExec, 1);
    EXPECT_TRUE(hot->addressFlags[0x0000] & BRK_FILTER_EXEC);
    EXPECT_FALSE(hot->addressFlags[0x0001] & BRK_FILTER_EXEC);

    // Add write breakpoint at 0x5C78
    _brkManager->AddMemWriteBreakpoint(0x5C78);
    EXPECT_EQ(hot->hasWrite, 1);
    EXPECT_TRUE(hot->addressFlags[0x5C78] & BRK_FILTER_WRITE);
    EXPECT_FALSE(hot->addressFlags[0x5C78] & BRK_FILTER_READ);

    // Add read breakpoint at 0x4000
    _brkManager->AddMemReadBreakpoint(0x4000);
    EXPECT_EQ(hot->hasRead, 1);
    EXPECT_TRUE(hot->addressFlags[0x4000] & BRK_FILTER_READ);

    // Add port breakpoints (different ports to avoid key collision)
    _brkManager->AddPortInBreakpoint(0xFE);
    EXPECT_EQ(hot->hasPortIn, 1);
    _brkManager->AddPortOutBreakpoint(0x7FFD);  // Different port
    EXPECT_EQ(hot->hasPortOut, 1);

    // Remove write breakpoint - should clear flag and filter
    auto bpId = _brkManager->GetAllBreakpoints().begin()->first;
    for (const auto& [id, bp] : _brkManager->GetAllBreakpoints())
    {
        if (bp->z80address == 0x5C78 && (bp->memoryType & BRK_MEM_WRITE))
        {
            _brkManager->RemoveBreakpointByID(id);
            break;
        }
    }
    EXPECT_FALSE(hot->addressFlags[0x5C78] & BRK_FILTER_WRITE);

    // Clear all - everything should reset
    _brkManager->ClearBreakpoints();
    EXPECT_EQ(hot->hasExec, 0);
    EXPECT_EQ(hot->hasRead, 0);
    EXPECT_EQ(hot->hasWrite, 0);
    EXPECT_EQ(hot->hasPortIn, 0);
    EXPECT_EQ(hot->hasPortOut, 0);
    EXPECT_EQ(hot->addressFlags[0x0000], 0);
}
