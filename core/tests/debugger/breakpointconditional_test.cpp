/// @file breakpointconditional_test.cpp
/// @brief Conditional breakpoints in the manager (breakpoint-taps conditions-and-callbacks.md, phase 1): the condition
/// decides whether a match is a hit, the two counters, the hit policy on condition-true hits, the access operands the
/// hot path supplies, refusals at creation, no adoption of a conditional breakpoint, taps with conditions during a run,
/// the error mark that never stops, bpcond under the CLI run-control gate, and the debug path a stopping breakpoint
/// holds on.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/memory/memory.h"

namespace
{
/// One pass: write 5 to #9000 and #9001, read #9000, then loop
const uint8_t kProgram[] = {
    0x3E, 0x05,        // 8000 LD A,5
    0x32, 0x00, 0x90,  // 8002 LD (#9000),A
    0x32, 0x01, 0x90,  // 8005 LD (#9001),A
    0x3A, 0x00, 0x90,  // 8008 LD A,(#9000)
    0x18, 0xF3,        // 800B JR #8000
};
constexpr uint16_t kStart = 0x8000;
constexpr unsigned kLoopInstructions = 5;

class CountingSink : public IBreakpointTapSink
{
public:
    std::vector<BreakpointTapHit> hits;
    void OnTap(const BreakpointTapHit& hit) override { hits.push_back(hit); }
};
}  // namespace

class BreakpointConditional_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    BreakpointManager* _bpm = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _bpm = _context->pDebugManager->GetBreakpointsManager();
        _z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(kProgram); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kStart + i), kProgram[i]);
        _z80->pc = kStart;
        _z80->sp = 0x7F00;
        _z80->iff1 = _z80->iff2 = 0;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static BreakpointSpec Spec(BreakpointTypeEnum type, uint8_t access, uint16_t address, const std::string& condition)
    {
        BreakpointSpec s;
        s.type = type;
        s.access = access;
        s.address = address;
        s.condition = condition;
        return s;
    }
    uint16_t Add(const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = _bpm->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }
    std::string Refused(const BreakpointSpec& spec)
    {
        std::string error;
        EXPECT_EQ(_bpm->AddBreakpoint(spec, error), BRK_INVALID) << spec.condition;
        return error;
    }
    BreakpointDescriptor Bp(uint16_t id) { return *_bpm->GetBreakpointById(id); }
};

TEST_F(BreakpointConditional_Test, ConditionDecidesTheHitAndBothCountersCount)
{
    const uint16_t id = Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x1234, "A == 7"));
    _z80->a = 6;
    EXPECT_EQ(_bpm->HandlePCChange(0x1234), BRK_INVALID) << "condition false";
    _z80->a = 7;
    EXPECT_EQ(_bpm->HandlePCChange(0x1234), id);
    EXPECT_EQ(Bp(id).matchCount, 2u) << "every match counts";
    EXPECT_EQ(Bp(id).hitCount, 1u) << "only the condition-true one is a hit";

    EXPECT_TRUE(_bpm->ResetHitCount(id));
    EXPECT_EQ(Bp(id).matchCount, 0u);
    EXPECT_EQ(Bp(id).hitCount, 0u);

    const std::string info = _bpm->FormatBreakpointInfo(id);
    EXPECT_NE(info.find("if A == 7"), std::string::npos) << info;
}

TEST_F(BreakpointConditional_Test, HitPolicyCountsConditionTrueHitsOnly)
{
    BreakpointSpec spec = Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x1234, "A == 7");
    spec.hitMode = BRK_HIT_EQUAL;
    spec.hitTarget = 2;
    const uint16_t id = Add(spec);
    const uint8_t a[] = {7, 1, 1, 7, 7};
    const bool stops[] = {false, false, false, true, false};
    for (size_t i = 0; i < std::size(a); i++)
    {
        _z80->a = a[i];
        EXPECT_EQ(_bpm->HandlePCChange(0x1234) == id, stops[i]) << "match " << i + 1;
    }
    EXPECT_EQ(Bp(id).matchCount, 5u);
    EXPECT_EQ(Bp(id).hitCount, 3u);
}

TEST_F(BreakpointConditional_Test, AccessOperandsComeFromTheAccess)
{
    const uint16_t write = Add(Spec(BRK_MEMORY, BRK_MEM_WRITE, 0x9000, "MWV == 42 && WR == 9000"));
    EXPECT_EQ(_bpm->HandleMemoryWrite(0x9000, 0x41), BRK_INVALID);
    EXPECT_EQ(_bpm->HandleMemoryWrite(0x9000, 0x42), write);
    EXPECT_EQ(_bpm->HandleMemoryWrite(0x9000), BRK_INVALID) << "a value not known is FFFFFFFF";

    const uint16_t read = Add(Spec(BRK_MEMORY, BRK_MEM_READ, 0x9100, "VAL == 0FF"));
    EXPECT_EQ(_bpm->HandleMemoryRead(0x9100, 0xFF), read);
    EXPECT_EQ(_bpm->HandleMemoryRead(0x9100, 0x00), BRK_INVALID);

    const uint16_t out = Add(Spec(BRK_IO, BRK_IO_OUT, 0x00FE, "OUT == 0FE && (VAL & 7) == 2"));
    EXPECT_EQ(_bpm->HandlePortOut(0x00FE, 0x12), out);
    EXPECT_EQ(_bpm->HandlePortOut(0x00FE, 0x13), BRK_INVALID);
    _bpm->RemoveBreakpointByID(out);
    // Unreal order: VAL & 7 == 2 is VAL & (7 == 2), never true
    const uint16_t unreal = Add(Spec(BRK_IO, BRK_IO_OUT, 0x00FE, "VAL & 7 == 2"));
    EXPECT_EQ(_bpm->HandlePortOut(0x00FE, 0x12), BRK_INVALID);
    EXPECT_EQ(Bp(unreal).matchCount, 1u);
}

TEST_F(BreakpointConditional_Test, RefusedAtCreationWithTheReason)
{
    EXPECT_NE(Refused(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "RD == 9000")).find("trigger"), std::string::npos);
    EXPECT_NE(Refused(Spec(BRK_IO, BRK_IO_IN, 0x00FE, "VAL == 1")).find("trigger"), std::string::npos)
        << "a port-in breakpoint has no written value";
    EXPECT_NE(Refused(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "A ==")).find("column"), std::string::npos);
    EXPECT_NE(Refused(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "NOSUCH == 1")).find("unknown name"), std::string::npos);
    EXPECT_NE(Refused(Spec(BRK_KEYBOARD, BRK_KEY_PRESS, BRK_KEY_ANY, "A == 1")).find("a condition applies to"),
              std::string::npos)
        << "a keyboard breakpoint takes none (a magic one does, platform-debugging P4)";
    EXPECT_TRUE(_bpm->GetAllBreakpoints().empty()) << "nothing was added";
}

TEST_F(BreakpointConditional_Test, ConditionalBreakpointIsNeverAdopted)
{
    const uint16_t plain = Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, ""));
    EXPECT_EQ(Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "")), plain) << "an equal plain request adopts it";
    const uint16_t conditional = Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "A == 1"));
    EXPECT_NE(conditional, plain);

    // A plain breakpoint that gets a condition leaves the key map: the next plain request is a new one
    std::string error;
    ASSERT_TRUE(_bpm->SetBreakpointCondition(plain, "B == 2", error)) << error;
    EXPECT_EQ(Bp(plain).condition->Canonical(), "B == 2");
    const uint16_t fresh = Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, ""));
    EXPECT_NE(fresh, plain);
    EXPECT_NE(fresh, conditional);

    EXPECT_FALSE(_bpm->SetBreakpointCondition(plain, "RD == 1", error)) << "checked like at creation";
    EXPECT_EQ(Bp(plain).condition->Canonical(), "B == 2") << "a refused text keeps the old condition";
    ASSERT_TRUE(_bpm->SetBreakpointCondition(plain, "", error));
    EXPECT_FALSE(Bp(plain).condition) << "an empty text clears it";
}

/// A debugger protocol's breakpoint (shared = false) is never merged: removing it leaves the user's, and two of its
/// own at one address are two
TEST_F(BreakpointConditional_Test, UnsharedBreakpointIsNeverAdopted)
{
    const uint16_t user = Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x1234, ""));
    BreakpointSpec own = Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x1234, "");
    own.shared = false;
    const uint16_t first = Add(own);
    const uint16_t second = Add(own);
    EXPECT_NE(first, user);
    EXPECT_NE(second, first);
    EXPECT_EQ(Add(Spec(BRK_MEMORY, BRK_MEM_EXECUTE, 0x1234, "")), user) << "a shared request still adopts the user's";

    ASSERT_TRUE(_bpm->RemoveBreakpointByID(first));
    EXPECT_NE(_bpm->HandlePCChange(0x1234), BRK_INVALID);
    ASSERT_TRUE(_bpm->RemoveBreakpointByID(user));
    EXPECT_EQ(_bpm->HandlePCChange(0x1234), second) << "the second unshared one stays";
    ASSERT_TRUE(_bpm->RemoveBreakpointByID(second));
    EXPECT_EQ(_bpm->HandlePCChange(0x1234), BRK_INVALID);

    BreakpointSpec port = Spec(BRK_IO, BRK_IO_OUT, 0x00FE, "");
    const uint16_t userPort = Add(port);
    port.shared = false;
    EXPECT_NE(Add(port), userPort);
}

/// bpcond changes a breakpoint: under another client's run control the CLI refuses it as it refuses bp
TEST(BreakpointConditionalGate_Test, BpcondNeedsRunControl)
{
    EXPECT_TRUE(automation_gate::CliCommandChangesMachine("bpcond", {"1", "A", "==", "7"}));
    EXPECT_TRUE(automation_gate::CliCommandChangesMachine("bpcond", {"1"}));
}

TEST_F(BreakpointConditional_Test, TapConditionsSeeTheValuesOfTheRun)
{
    CountingSink sink;
    const uint8_t id = _bpm->RegisterTapSink(&sink);
    auto addTap = [&](uint8_t access, uint16_t address, const std::string& condition) {
        auto* d = new BreakpointDescriptor();
        d->type = BRK_MEMORY;
        d->memoryType = access;
        d->z80address = address;
        d->hidden = true;
        d->owner = "conditional-test";
        d->tapSink = id;
        const uint16_t bp = _bpm->AddBreakpoint(d);
        std::string error;
        EXPECT_TRUE(_bpm->SetBreakpointCondition(bp, condition, error)) << error;
        return bp;
    };
    const uint16_t five = addTap(BRK_MEM_WRITE, 0x9001, "MWV == 5");
    const uint16_t six = addTap(BRK_MEM_WRITE, 0x9000, "MWV == 6");
    const uint16_t read = addTap(BRK_MEM_READ, 0x9000, "MRV == 5 && RD == 9000");

    EXPECT_EQ(_emulator->RunNCPUCycles(3 * kLoopInstructions, false), 3 * kLoopInstructions);
    EXPECT_EQ(Bp(five).hitCount, 3u) << "the fast write's hook passes the value";
    EXPECT_EQ(Bp(six).matchCount, 3u);
    EXPECT_EQ(Bp(six).hitCount, 0u);
    EXPECT_EQ(Bp(read).hitCount, 3u) << "the debug read passes the byte read";
    EXPECT_EQ(sink.hits.size(), 6u);
    _bpm->UnregisterTapSink(id);
}

/// Without a CPU behind the manager a register cannot be read: the run-time error marks the breakpoint, never stops
/// on it, and skips it until it is enabled again or gets a new condition
TEST(BreakpointConditionalMark_Test, AnErrorMarksAndNeverStops)
{
    MessageCenter::DisposeDefaultMessageCenter();
    auto* context = new EmulatorContext(LoggerLevel::LogError);
    context->config.ramsize = 128;
    auto* memory = new Memory(context);
    context->pMemory = memory;
    memory->Reset();
    auto* bpm = new BreakpointManager(context);

    BreakpointSpec spec;
    spec.condition = "A == 1";  // no dry run without a CPU: compiled, fails when evaluated
    std::string error;
    const uint16_t id = bpm->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;

    EXPECT_EQ(bpm->HandlePCChange(0x0000), BRK_INVALID) << "an error never stops";
    BreakpointDescriptor bp = *bpm->GetBreakpointById(id);
    EXPECT_EQ(bp.conditionFailed, 1);
    EXPECT_NE(std::string(bp.conditionError).find("no CPU"), std::string::npos) << bp.conditionError;
    EXPECT_NE(std::string(bp.conditionError).find("frame"), std::string::npos) << bp.conditionError;
    EXPECT_EQ(bp.matchCount, 1u);

    EXPECT_EQ(bpm->HandlePCChange(0x0000), BRK_INVALID);
    EXPECT_EQ(bpm->GetBreakpointById(id)->matchCount, 1u) << "a marked breakpoint is skipped";
    EXPECT_NE(bpm->FormatBreakpointInfo(id).find("condition failed"), std::string::npos);

    bpm->ActivateBreakpoint(id);
    EXPECT_EQ(bpm->GetBreakpointById(id)->conditionFailed, 0) << "enabled again: another try";

    ASSERT_TRUE(bpm->SetBreakpointCondition(id, "M(0) == M(0)", error)) << error;
    EXPECT_EQ(bpm->HandlePCChange(0x0000), id) << "memory needs no CPU";

    delete bpm;
    delete memory;
    context->pMemory = nullptr;
    delete context;
    MessageCenter::DisposeDefaultMessageCenter();
}

/// A breakpoint is never accepted and inert: with 'breakpoints' on, a stopping breakpoint holds the debug path on even
/// after 'debugmode' went off (the hidden Qt debugger switches it off when it adopts a machine a client created), and
/// without breakpoints 'debugmode' alone decides again
TEST(BreakpointDebugHold_Test, StoppingBreakpointFiresWhileDebugModeIsOff)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* bpm = context->pDebugManager->GetBreakpointsManager();
    Z80* z80 = context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(kProgram); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kStart + i), kProgram[i]);
    z80->pc = kStart;
    z80->sp = 0x7F00;
    z80->iff1 = z80->iff2 = 0;

    FeatureManager* features = context->pFeatureManager;
    features->setFeature(Features::kBreakpoints, true);  // switches 'debugmode' on too
    emulator->DebugOff();
    EXPECT_FALSE(z80->isDebugMode) << "no breakpoints: 'debugmode' decides";

    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_WRITE;
    spec.address = 0x9000;
    spec.hitMode = BRK_HIT_AT_LEAST;
    spec.hitTarget = 0xFFFF'FFFFu;  // counts, never stops: the run goes on
    std::string error;
    const uint16_t id = bpm->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    EXPECT_TRUE(z80->isDebugMode) << "a stopping breakpoint holds the debug path";

    emulator->RunNCPUCycles(kLoopInstructions * 3 * 20, false);
    EXPECT_GT(bpm->GetBreakpointById(id)->hitCount, 0u) << "the write reached the breakpoint";

    features->setFeature(Features::kBreakpoints, false);
    EXPECT_FALSE(z80->isDebugMode) << "'breakpoints' off: the hold lets go";
    features->setFeature(Features::kBreakpoints, true);
    features->setFeature(Features::kDebugMode, false);
    EXPECT_TRUE(z80->isDebugMode);
    EXPECT_TRUE(bpm->RemoveBreakpointByID(id));
    EXPECT_FALSE(z80->isDebugMode) << "no breakpoints left: 'debugmode' decides again";

    EmulatorTestHelper::CleanupEmulator(emulator);
}
