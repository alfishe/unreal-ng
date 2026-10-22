// The run / step / time-travel actions of steppingactions.cpp (Stream Deck use cases D1, D2, D3, D5): the time-travel
// scanline unit (ttd.step_back_scanline, reverse-step scanlines), the bookmarks' previous / next, the last write to an
// address (a param or the debugger's cursor) with the seek there, the history's ends, ttd.seek_by in the units the curve
// does not cover and the scrub unit, on both time-travel backends; then the runs to a place and the pace on a started
// machine

#include "pch.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/actions/steppingactions.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/runpacer.h"
#include "emulator/memory/memory.h"

using namespace actions;

namespace
{
ActionValue Press(StateNode params = StateNode::Object())
{
    ActionValue value;
    value.params = std::move(params);
    return value;
}

ActionValue Delta(double delta, StateNode params = StateNode::Object())
{
    ActionValue value;
    value.kind = ValueKind::Delta;
    value.delta = delta;
    value.params = std::move(params);
    return value;
}

ActionValue Text(const std::string& text)
{
    ActionValue value;
    value.kind = ValueKind::Text;
    value.text = text;
    return value;
}

StateNode Params(std::initializer_list<std::pair<const char*, StateNode>> list)
{
    StateNode params = StateNode::Object();
    for (const auto& [name, value] : list)
        params[name] = value;
    return params;
}

ActionOrigin Gui()
{
    ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    origin.identity = "test";
    return origin;
}

/// The core actions (builtin and stepping) over one instance the test owns
class TestRegistry
{
public:
    explicit TestRegistry(Emulator* emulator)
        : _shared(emulator, [](Emulator*) {}),
          _registry(std::make_unique<ActionRegistry>([this](const std::string& id) {
              return id == _shared->GetId() ? _shared : std::shared_ptr<Emulator>();
          }))
    {
        _handles = RegisterBuiltinActions(*_registry);
        for (RegistrationHandle& handle : RegisterSteppingActions(*_registry))
            _handles.push_back(std::move(handle));
    }
    ~TestRegistry()
    {
        _handles.clear();
        _registry->Shutdown();
        execution::RunPacer::Drop(_shared->GetId());
        DropSteppingState(_shared->GetId());
    }
    ActionResult Run(const char* id, ActionValue value)
    {
        return _registry->Run(id, std::move(value), _shared->GetId(), Gui());
    }
    std::optional<bool> Toggle(const char* id) { return _registry->ToggleState(id, _shared->GetId()); }

private:
    std::shared_ptr<Emulator> _shared;
    std::unique_ptr<ActionRegistry> _registry;
    std::vector<RegistrationHandle> _handles;
};

int64_t Int(const StateNode& body, const char* key)
{
    const StateNode* node = body.find(key);
    return node != nullptr && node->kind == StateNode::Kind::Int ? node->i : -1;
}
}  // namespace

TEST(DebuggerCursors_Test, PanesPerInstanceAClearAndTheInstanceGoing)
{
    DebuggerCursors& cursors = DebuggerCursors::Instance();
    cursors.Set("i1", "disasm", 0x8000);
    cursors.Set("i1", "memory", 0x4000);
    cursors.Set("i2", "disasm", 0x1234);
    EXPECT_EQ(cursors.Get("i1", "disasm"), std::optional<uint16_t>(0x8000));
    EXPECT_EQ(cursors.Get("i1", "memory"), std::optional<uint16_t>(0x4000));
    cursors.Set("i1", "disasm", 0x8003);  // the cursor row moved (a key, a deck's dbg.cursor_down)
    EXPECT_EQ(cursors.Get("i1", "disasm"), std::optional<uint16_t>(0x8003));
    cursors.Set("i1", "memory", std::nullopt);
    EXPECT_FALSE(cursors.Get("i1", "memory").has_value());
    DropSteppingState("i1");
    EXPECT_FALSE(cursors.Get("i1", "disasm").has_value());
    EXPECT_EQ(cursors.Get("i2", "disasm"), std::optional<uint16_t>(0x1234)) << "another instance's stays";
    DropSteppingState("i2");
}

/// region <Time travel on both backends>

/// Each test inits a bare instance and records 6-8 frames of a write loop on it: boot-bound, about 40-60 ms

class SteppingTtd_Test : public ::testing::TestWithParam<bool>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;
    std::unique_ptr<TestRegistry> _registry;
    uint64_t _first = 0;
    uint64_t _end = 0;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        if (GetParam())
        {
            _controller = std::make_unique<ttd::TimeTravelController>(_context);
            _context->pTimeTravelHooks = _controller.get();
            _context->ttdWriteSink = _controller.get();
            _context->pTimeTravelController = _controller.get();
        }
        _registry = std::make_unique<TestRegistry>(_emulator);
    }

    void TearDown() override
    {
        _registry.reset();
        if (_controller)
        {
            _controller->StopRecording();
            _context->pTimeTravelHooks = _context->pTimeTravelManager;
            _context->ttdWriteSink = _context->pTimeTravelManager;
            _context->pTimeTravelController = nullptr;
            _controller.reset();
        }
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    ttd::TTDReply Ttd(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }

    /// A loop writing #42 to #9000 at $8000 (LD A,#42; LD (#9000),A; JR $8000), interrupts off; then `frames` recorded
    void Record(int frames)
    {
        Memory* memory = _emulator->GetMemory();
        const uint8_t code[] = {0x3E, 0x42, 0x32, 0x00, 0x90, 0x18, 0xF9};
        for (uint16_t i = 0; i < sizeof(code); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
        ASSERT_TRUE(Ttd("start").Ok());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        ASSERT_TRUE(Ttd("stop").Ok());
        const ttd::TTDCursor cursor = _context->pTimeTravelHooks->PublishedCursor();
        ASSERT_TRUE(cursor.history);
        _first = cursor.first;
        _end = cursor.end;
        ASSERT_GE(_end, _first + 6);
    }

    void Seek(uint64_t frame, uint32_t tinframe)
    {
        ASSERT_TRUE(Ttd("seek", {{"frame", std::to_string(frame)}, {"tinframe", std::to_string(tinframe)}}).Ok());
    }

    /// The position as machine t-states (frames of config.frame)
    uint64_t Here()
    {
        const ttd::TTDCursor cursor = _context->pTimeTravelHooks->PublishedCursor();
        return cursor.frame * _context->config.frame + cursor.tinframe;
    }
};

INSTANTIATE_TEST_SUITE_P(Backends, SteppingTtd_Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Engine" : "V1"; });

TEST_P(SteppingTtd_Test, ReverseStepTakesScanlinesOfTheMachinesLine)
{
    Record(8);
    const unsigned line = _context->config.t_line;
    ASSERT_GT(line, 0u);
    Seek(_first + 4, 3 * line + 100);
    const uint64_t before = Here();

    const ttd::TTDReply reply = Ttd("reverse-step", {{"scanlines", "2"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.find("reached")->b);
    EXPECT_EQ(reply.body.find("mode")->s, "scanlines");
    EXPECT_EQ(static_cast<uint64_t>(reply.body.find("tstates")->i), 2ull * line);
    // At or before the target, on an instruction start (the loop's longest is 13 t-states)
    EXPECT_GE(before - Here(), 2ull * line);
    EXPECT_LE(before - Here(), 2ull * line + 13);

    EXPECT_EQ(Ttd("reverse-step", {{"scanlines", "1"}, {"count", "1"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Ttd("reverse-step", {{"scanlines", "x"}}).error, ttd::TTDControlError::BadRequest);
}

TEST_P(SteppingTtd_Test, ScanlineStepsGoBackAndForwardOneLine)
{
    EXPECT_EQ(_registry->Run("ttd.step_back_scanline", Press()).error, "not_now") << "record first";
    Record(8);
    const unsigned line = _context->config.t_line;
    Seek(_first + 4, 2000);
    uint64_t before = Here();
    ActionResult result = _registry->Run("ttd.step_back_scanline", Press());
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_GE(before - Here(), static_cast<uint64_t>(line));
    EXPECT_LE(before - Here(), static_cast<uint64_t>(line) + 13);

    before = Here();
    result = _registry->Run("ttd.step_forward_scanline", Press(Params({{"count", 3}})));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_GE(Here() - before, 3ull * line - 13);
    EXPECT_LE(Here() - before, 3ull * line + 13);

    EXPECT_EQ(_registry->Run("ttd.step_back_scanline", Press(Params({{"count", 0}}))).error, "Bad Request");
}

TEST_P(SteppingTtd_Test, BookmarksPreviousAndNextAndTheirEnds)
{
    Record(8);
    EXPECT_EQ(_registry->Run("ttd.bookmark_next", Press()).error, "Conflict") << "no bookmarks";
    ASSERT_TRUE(Ttd("bookmark-add", {{"label", "a"}, {"frame", std::to_string(_first + 2)}}).Ok());
    ASSERT_TRUE(Ttd("bookmark-add", {{"label", "b"}, {"frame", std::to_string(_first + 5)}}).Ok());
    Seek(_first + 3, 0);

    ActionResult result = _registry->Run("ttd.bookmark_next", Press());
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(result.body.find("label")->s, "b");
    EXPECT_EQ(_context->pTimeTravelHooks->PublishedCursor().frame, _first + 5);
    EXPECT_EQ(_registry->Run("ttd.bookmark_next", Press()).message, "no later bookmark");

    result = _registry->Run("ttd.bookmark_prev", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("label")->s, "a");
    EXPECT_EQ(_context->pTimeTravelHooks->PublishedCursor().frame, _first + 2);
    result = _registry->Run("ttd.bookmark_prev", Press());
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "no earlier bookmark");

    // The bookmarks unit of the dial: two on from a
    result = _registry->Run("ttd.seek_by", Delta(2, Params({{"unit", "bookmarks"}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("unit")->s, "bookmarks");
    EXPECT_EQ(result.body.find("label")->s, "b") << "only one is later: the last of them";

    result = _registry->Run("ttd.bookmarks", Press());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Int(result.body, "count"), 2);
}

TEST_P(SteppingTtd_Test, TheLastWriteToTheCursorsAddressAndTheSeekThere)
{
    Record(6);
    Seek(_end - 1, 0);
    EXPECT_EQ(_registry->Run("ttd.find_last_write", Press()).error, "Bad Request") << "no address, no cursor";

    ActionResult result = _registry->Run("ttd.find_last_write", Press(Params({{"addr", "#9000"}, {"seek", false}})));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    ASSERT_TRUE(result.body.find("found")->b);
    EXPECT_EQ(Int(result.body, "addr"), 0x9000);
    EXPECT_EQ(Int(result.body, "value"), 0x42);
    EXPECT_EQ(Int(result.body, "pc"), 0x8002) << "the LD (#9000),A";
    EXPECT_EQ(result.body.find("seeked"), nullptr);

    // The memory view's cursor names the address; the seek goes there
    DebuggerCursors::Instance().Set(_emulator->GetId(), "memory", 0x9000);
    result = _registry->Run("ttd.find_last_write", Press());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.body.find("found")->b);
    EXPECT_TRUE(result.body.find("seeked")->b);
    const ttd::TTDCursor cursor = _context->pTimeTravelHooks->PublishedCursor();
    EXPECT_EQ(cursor.frame, static_cast<uint64_t>(Int(result.body, "frame")));

    // Nothing reads #9100
    result = _registry->Run("ttd.find_last", Press(Params({{"addr", 0x9100}, {"access", "read"}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.body.find("found")->b);
}

TEST_P(SteppingTtd_Test, TheHistorysStartAndEnd)
{
    Record(6);
    Seek(_first + 3, 0);
    ASSERT_TRUE(_registry->Run("ttd.seek_start", Press()).ok);
    EXPECT_EQ(_context->pTimeTravelHooks->PublishedCursor().frame, _first);
    const ActionResult end = _registry->Run("ttd.seek_end", Press());
    ASSERT_TRUE(end.ok) << end.message;
    EXPECT_GE(_context->pTimeTravelHooks->PublishedCursor().frame, _end - 1);
}

TEST_P(SteppingTtd_Test, SeekByInInstructionsTStatesAndTheChosenUnit)
{
    Record(8);
    Seek(_first + 4, 1000);
    uint64_t before = Here();
    ActionResult result = _registry->Run("ttd.seek_by", Delta(-3, Params({{"unit", "instructions"}})));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(result.body.find("unit")->s, "instructions");
    EXPECT_GT(before, Here());
    EXPECT_LE(before - Here(), 3ull * 13) << "three of the loop's instructions";

    before = Here();
    result = _registry->Run("ttd.seek_by", Delta(2, Params({{"unit", "tstates"}, {"count", 500}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GE(Here() - before, 1000ull - 13);
    EXPECT_LE(Here() - before, 1000ull + 13);

    // The chosen unit: scanlines, one a step; ttd.unit_back takes it
    result = _registry->Run("ttd.scrub_unit", Text("scanline"));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("unit")->s, "scanlines");
    EXPECT_EQ(ScrubUnitOf(_emulator->GetId()), "scanlines");
    before = Here();
    result = _registry->Run("ttd.unit_back", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_GE(before - Here(), static_cast<uint64_t>(_context->config.t_line));

    // next / prev walk the units; a batch is 10 frames a step
    ASSERT_TRUE(_registry->Run("ttd.scrub_unit", Text("next")).ok);
    EXPECT_EQ(ScrubUnitOf(_emulator->GetId()), "bookmarks");
    ASSERT_TRUE(_registry->Run("ttd.scrub_unit", Text("next")).ok);
    EXPECT_EQ(ScrubUnitOf(_emulator->GetId()), "frames") << "round";
    EXPECT_EQ(_registry->Run("ttd.scrub_unit", Text("minutes")).error, "Bad Request");
    Seek(_first + 1, 0);
    result = _registry->Run("ttd.seek_by", Delta(1, Params({{"unit", "batch"}, {"count", 3}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Int(result.body, "frames"), 3);
    EXPECT_EQ(result.body.find("unit")->s, "batch");
}

/// endregion </Time travel on both backends>

/// region <Runs to a place and the pace (a started machine)>

class SteppingRun_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    std::unique_ptr<TestRegistry> _registry;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(EmulatorTestHelper::EnableDebugFeatures(_emulator));
        _emulator->EnableTurboMode();  // the frames only have to advance; nothing here looks at pixels
        _registry = std::make_unique<TestRegistry>(_emulator);
        _emulator->StartAsync();
        ASSERT_TRUE(TestWait::For([&]() { return _emulator->GetContext()->emulatorState.frame_counter > 1; }));
        _emulator->Pause();
        ASSERT_TRUE(TestWait::For([&]() {
            const execution::Snapshot s = _emulator->GetArbiter().GetSnapshot();
            return s.intent == execution::Intent::Pause && s.loopParked;
        }));
        Memory* memory = _emulator->GetMemory();
        for (uint16_t a = 0x8000; a < 0x8100; a++)
            memory->DirectWriteToZ80Memory(a, 0x00);
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
    }

    void TearDown() override
    {
        _registry.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint16_t Pc() { return _emulator->GetZ80State()->pc; }
};

/// Boot-bound (a started machine): about 30 ms
TEST_F(SteppingRun_Test, RunUntilPcAndToTheCursorWhichABreakpointStops)
{
    ActionResult result = _registry->Run("run.until_pc", Press(Params({{"pc", "0x8010"}})));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_TRUE(result.body.find("hit")->b);
    EXPECT_EQ(Pc(), 0x8010);
    EXPECT_EQ(_registry->Run("run.until_pc", Press()).error, "Bad Request") << "no pc";

    // To the disassembly's cursor; a breakpoint on the way stops it first
    EXPECT_EQ(_registry->Run("run.to_cursor", Press()).error, "Bad Request") << "no cursor yet";
    DebuggerCursors::Instance().Set(_emulator->GetId(), "disasm", 0x8030);
    auto* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = 0x8020;
    ASSERT_NE(_emulator->GetBreakpointManager()->AddBreakpoint(breakpoint), 0);
    result = _registry->Run("run.to_cursor", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.body.find("hit")->b);
    EXPECT_EQ(result.body.find("stop")->find("reason")->s, "breakpoint");
    EXPECT_EQ(Pc(), 0x8020);
    result = _registry->Run("run.to_cursor", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.body.find("hit")->b) << "on from the breakpoint to the cursor";
    EXPECT_EQ(Pc(), 0x8030);

    // until_pc skips breakpoints unless asked
    _emulator->GetZ80State()->pc = 0x8000;
    result = _registry->Run("run.until_pc", Press(Params({{"pc", 0x8040}})));
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Pc(), 0x8040);
}

/// Boot-bound (a started machine): about 30 ms
TEST_F(SteppingRun_Test, ThePixelTheListingAndThePaceActions)
{
    ActionResult result = _registry->Run("run.to_pixel", Press());
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(result.body.find("state")->s, "paused");
    EXPECT_EQ(_registry->Run("run.step_line", Press()).error, "Conflict") << "no listing loaded";
    EXPECT_EQ(_registry->Run("run.to_line", Press()).error, "Bad Request") << "no line";

    _emulator->GetZ80State()->pc = 0x8000;
    result = _registry->Run("run.pace_set", Press(Params({{"preset", "1op"}, {"interval_ms", 250}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("pace")->find("preset")->s, "1op");
    EXPECT_EQ(Int(*result.body.find("pace"), "interval_ms"), 250);
    ASSERT_TRUE(_registry->Run("run.pace_step", Press()).ok);
    EXPECT_EQ(Pc(), 0x8001);
    ASSERT_TRUE(_registry->Run("run.pace_prev", Press()).ok);
    EXPECT_EQ(PaceReadout(_emulator->GetId()).find("preset")->s, "1line");
    ASSERT_TRUE(_registry->Run("run.pace_by", Delta(-2)).ok);
    EXPECT_EQ(PaceReadout(_emulator->GetId()).find("preset")->s, "100lines");
    EXPECT_EQ(_registry->Run("run.pace_set", Press(Params({{"preset", "warp"}}))).error, "Bad Request");

    EXPECT_EQ(_registry->Toggle("run.pace_toggle"), std::optional<bool>(false));
    ASSERT_TRUE(_registry->Run("run.pace_toggle", Press()).ok);
    EXPECT_EQ(_registry->Toggle("run.pace_toggle"), std::optional<bool>(true));
    EXPECT_TRUE(PaceReadout(_emulator->GetId()).find("running")->b);
    ASSERT_TRUE(_registry->Run("run.pace_stop", Press()).ok);
    EXPECT_EQ(_registry->Toggle("run.pace_toggle"), std::optional<bool>(false));
}

/// Boot-bound (a started machine): about 20 ms
TEST_F(SteppingRun_Test, StepByInAUnitRunsForward)
{
    const uint64_t frames = _emulator->GetContext()->emulatorState.frame_counter;
    ActionResult result = _registry->Run("run.step_by", Delta(2, Params({{"unit", "frames"}})));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(_emulator->GetContext()->emulatorState.frame_counter - frames, 2u);
    EXPECT_EQ(result.body.find("unit")->s, "frames");

    _emulator->GetZ80State()->pc = 0x8000;  // the frames ran on past the NOPs
    const uint16_t pc = Pc();
    result = _registry->Run("run.step_by", Delta(1, Params({{"unit", "tstates"}, {"count", 40}})));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Pc() - pc, 10) << "40 t-states of NOPs";
    EXPECT_EQ(_registry->Run("run.step_by", Delta(1, Params({{"unit", "bookmarks"}}))).error, "Bad Request");
}

/// endregion </Runs to a place and the pace>
