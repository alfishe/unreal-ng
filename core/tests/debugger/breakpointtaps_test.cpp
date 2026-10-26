/// @file breakpointtaps_test.cpp
/// @brief Taps (docs/inprogress/2026-10-26-breakpoint-taps): breakpoints that tell a registered sink instead of
/// stopping. Several sinks at once, the demand the armed set derives (the exec check, the fast write's hook, the debug
/// path for reads), the hook seeing only marked addresses, the hit policy, and no tap during a TTD replay.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
/// One pass: write #9000 and #9001, read #9000, then loop
const uint8_t kProgram[] = {
    0x3E, 0x05,        // 8000 LD A,5
    0x32, 0x00, 0x90,  // 8002 LD (#9000),A
    0x32, 0x01, 0x90,  // 8005 LD (#9001),A
    0x3A, 0x00, 0x90,  // 8008 LD A,(#9000)
    0x18, 0xF3,        // 800B JR #8000
};
constexpr uint16_t kStart = 0x8000;
constexpr unsigned kLoopInstructions = 5;

class RecordingSink : public IBreakpointTapSink
{
public:
    std::vector<BreakpointTapHit> hits;
    void OnTap(const BreakpointTapHit& hit) override { hits.push_back(hit); }
};
}  // namespace

class BreakpointTaps_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    BreakpointManager* _breakpoints = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _breakpoints = _context->pDebugManager->GetBreakpointsManager();
        _z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(kProgram); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kStart + i), kProgram[i]);
        _z80->pc = kStart;
        _z80->sp = 0x7F00;
        _z80->iff1 = _z80->iff2 = 0;  // no interrupt in the loop
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// A hidden tap of @p sink
    uint16_t AddTap(uint8_t sink, uint8_t memoryType, uint16_t address, uint32_t cookie = 0)
    {
        auto* descriptor = new BreakpointDescriptor();
        descriptor->type = BRK_MEMORY;
        descriptor->memoryType = memoryType;
        descriptor->z80address = address;
        descriptor->hidden = true;
        descriptor->owner = "taps-test";
        descriptor->tapSink = sink;
        descriptor->tapCookie = cookie;
        return _breakpoints->AddBreakpoint(descriptor);
    }

    /// Runs @p passes passes of the loop (RunNCPUCycles counts instructions)
    void RunPasses(unsigned passes)
    {
        EXPECT_EQ(_emulator->RunNCPUCycles(passes * kLoopInstructions, false), passes * kLoopInstructions)
            << "no breakpoint stopped the run";
    }

    bool FastMemoryPath() const
    {
        return _z80->MemIf == _z80->FastMemIf || _z80->MemIf == _z80->FastContendedMemIf;
    }
};

TEST_F(BreakpointTaps_Test, TwoSinksOnOneAddressBothToldNeitherStops)
{
    RecordingSink first, second;
    const uint8_t a = _breakpoints->RegisterTapSink(&first);
    const uint8_t b = _breakpoints->RegisterTapSink(&second);
    ASSERT_NE(a, BRK_NO_TAP);
    ASSERT_NE(b, BRK_NO_TAP);
    ASSERT_NE(a, b);
    const uint16_t one = AddTap(a, BRK_MEM_EXECUTE, 0x8002, 11);
    const uint16_t two = AddTap(b, BRK_MEM_EXECUTE, 0x8002, 22);
    EXPECT_NE(one, two) << "a tap never adopts another breakpoint";

    RunPasses(3);
    EXPECT_EQ(_breakpoints->GetLastTriggeredBreakpointID(), BRK_INVALID) << "a tap never stops";
    ASSERT_EQ(first.hits.size(), 3u);
    ASSERT_EQ(second.hits.size(), 3u);
    EXPECT_EQ(first.hits[0].cookie, 11u);
    EXPECT_EQ(second.hits[0].cookie, 22u);
    EXPECT_EQ(first.hits[0].kind, BRK_TAP_EXEC);
    EXPECT_EQ(first.hits[0].address, 0x8002);
    EXPECT_EQ(first.hits[0].breakpointID, one);

    _breakpoints->UnregisterTapSink(a);
    EXPECT_EQ(_breakpoints->GetBreakpointById(one), nullptr) << "unregistering removes the sink's taps";
    EXPECT_NE(_breakpoints->GetBreakpointById(two), nullptr) << "and only them";
    _breakpoints->UnregisterTapSink(b);
}

TEST_F(BreakpointTaps_Test, DemandFollowsTheArmedSet)
{
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_FALSE(_z80->isDebugMode);
    RecordingSink sink;
    const uint8_t id = _breakpoints->RegisterTapSink(&sink);

    const uint16_t exec = AddTap(id, BRK_MEM_EXECUTE, 0x8000);
    EXPECT_TRUE(_z80->execTapHold);
    EXPECT_FALSE(features->debugModeInEffect());

    const uint16_t write = AddTap(id, BRK_MEM_WRITE, 0x9000);
    EXPECT_TRUE(_breakpoints->GetTapDemand().write);
    EXPECT_FALSE(features->tapDebugHold()) << "a write tap takes the fast write's hook";
    EXPECT_TRUE(FastMemoryPath());

    const uint16_t read = AddTap(id, BRK_MEM_READ, 0x9000);
    EXPECT_TRUE(features->tapDebugHold()) << "a read tap takes the debug path";
    EXPECT_TRUE(_z80->isDebugMode);
    _breakpoints->RemoveBreakpointByID(read);
    EXPECT_FALSE(features->tapDebugHold());
    EXPECT_FALSE(_z80->isDebugMode);

    _breakpoints->RemoveBreakpointByID(exec);
    EXPECT_FALSE(_z80->execTapHold);
    _breakpoints->RemoveBreakpointByID(write);
    EXPECT_FALSE(_breakpoints->GetTapDemand().write);

    // A stopping breakpoint is no tap: it raises no demand
    _breakpoints->AddExecutionBreakpoint(0x8000);
    EXPECT_FALSE(_z80->execTapHold);
    _breakpoints->UnregisterTapSink(id);
}

TEST_F(BreakpointTaps_Test, WriteHookSeesTheMarkedAddressOnly)
{
    RecordingSink sink;
    const uint8_t id = _breakpoints->RegisterTapSink(&sink);
    AddTap(id, BRK_MEM_WRITE, 0x9000, 7);
    ASSERT_TRUE(FastMemoryPath());

    RunPasses(4);
    ASSERT_EQ(sink.hits.size(), 4u) << "one write to #9000 a pass; #9001 and the read are not told";
    for (const BreakpointTapHit& hit : sink.hits)
    {
        EXPECT_EQ(hit.kind, BRK_TAP_WRITE);
        EXPECT_EQ(hit.address, 0x9000);
        EXPECT_EQ(hit.cookie, 7u);
    }
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 5) << "the store happened";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9001), 5);
    _breakpoints->UnregisterTapSink(id);
}

TEST_F(BreakpointTaps_Test, WriteTapOnTheDebugPathIsToldOnce)
{
    // With the debug mode on the CPU writes through the debug path; the fast write's hook never sees them, so the
    // debug path tells the tap (even with the 'breakpoints' feature off) and nothing is told twice
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature("debugmode", true);
    features->setFeature("breakpoints", false);
    ASSERT_TRUE(_z80->isDebugMode);
    RecordingSink sink;
    const uint8_t id = _breakpoints->RegisterTapSink(&sink);
    AddTap(id, BRK_MEM_WRITE, 0x9000);

    RunPasses(3);
    EXPECT_EQ(sink.hits.size(), 3u);
    _breakpoints->UnregisterTapSink(id);
}

TEST_F(BreakpointTaps_Test, HitPolicyDecidesWhichHitsAreTold)
{
    RecordingSink sink;
    const uint8_t id = _breakpoints->RegisterTapSink(&sink);
    auto* descriptor = new BreakpointDescriptor();
    descriptor->type = BRK_MEMORY;
    descriptor->memoryType = BRK_MEM_EXECUTE;
    descriptor->z80address = 0x8000;
    descriptor->hidden = true;
    descriptor->tapSink = id;
    descriptor->hitMode = BRK_HIT_MULTIPLE;
    descriptor->hitTarget = 2;
    _breakpoints->AddBreakpoint(descriptor);

    RunPasses(6);
    EXPECT_EQ(sink.hits.size(), 3u) << "every second pass";
    _breakpoints->UnregisterTapSink(id);
}

TEST_F(BreakpointTaps_Test, NoTapDuringAReplay)
{
    RecordingSink sink;
    const uint8_t id = _breakpoints->RegisterTapSink(&sink);
    AddTap(id, BRK_MEM_EXECUTE, 0x8000);
    AddTap(id, BRK_MEM_WRITE, 0x9000);

    _context->ttdReplayActive = true;  // the replay re-executes known history
    RunPasses(3);
    _context->ttdReplayActive = false;
    EXPECT_TRUE(sink.hits.empty());

    RunPasses(1);
    EXPECT_EQ(sink.hits.size(), 2u) << "told again once the replay ends";
    _breakpoints->UnregisterTapSink(id);
}

TEST(BreakpointTapsNext_Test, WriteTapsTakeTheDebugPathOnTheNext)
{
    // The Next writes through its own slot table, not MemoryWriteFast: its write taps hold the debug path
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager();
    RecordingSink sink;
    const uint8_t id = breakpoints->RegisterTapSink(&sink);
    auto* descriptor = new BreakpointDescriptor();
    descriptor->type = BRK_MEMORY;
    descriptor->memoryType = BRK_MEM_WRITE;
    descriptor->z80address = 0x9000;
    descriptor->hidden = true;
    descriptor->tapSink = id;
    breakpoints->AddBreakpoint(descriptor);
    EXPECT_FALSE(context->pMemory->SupportsWriteTapHook());
    EXPECT_TRUE(emulator->GetFeatureManager()->tapDebugHold());
    breakpoints->UnregisterTapSink(id);
    EXPECT_FALSE(emulator->GetFeatureManager()->tapDebugHold());
    EmulatorTestHelper::CleanupEmulator(emulator);
}
