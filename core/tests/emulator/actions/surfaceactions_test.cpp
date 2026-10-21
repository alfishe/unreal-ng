// The control-surface actions of phase S2 (docs/inprogress/2026-10-18-control-surfaces/actions.md §2-§4,
// architecture §6): ttd.seek_by (the curve, the clamp to the history, the marks) on both time-travel backends,
// run.step_by (step in forward, back through the history, the 16 cap, a breakpoint ends it), and ctx.set with the
// context state (the last setter wins, the default `ttd` with a history, NC_SURFACE_CONTEXT)

#include "pch.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/actions/contextstate.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

using namespace actions;

namespace
{
ActionValue Delta(double delta, double rate = 0, StateNode params = StateNode::Object())
{
    ActionValue value;
    value.kind = ValueKind::Delta;
    value.delta = delta;
    value.rate = rate;
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

ActionOrigin Origin(execution::Origin origin, const std::string& identity = "test", const std::string& client = "")
{
    ActionOrigin out;
    out.origin = origin;
    out.identity = identity;
    out.client = client;
    return out;
}

int64_t Int(const StateNode& body, const char* key)
{
    const StateNode* node = body.find(key);
    return node != nullptr ? node->i : -1;
}

int64_t Arrived(const ActionResult& result)
{
    const StateNode* at = result.body.find("arrived_at");
    return at != nullptr ? Int(*at, "frame") : -1;
}

/// The marks of a seek_by result as "kind@frame" strings
std::vector<std::string> Marks(const ActionResult& result)
{
    std::vector<std::string> out;
    if (const StateNode* marks = result.body.find("marks"))
        for (const StateNode& mark : marks->items)
            out.push_back(mark.find("kind")->s + "@" + std::to_string(mark.find("frame")->i));
    return out;
}

/// A registry with the core actions over one instance the test owns
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
    }
    ~TestRegistry()
    {
        _handles.clear();
        _registry->Shutdown();
    }
    ActionResult Run(const char* id, ActionValue value, ActionOrigin origin = Origin(execution::Origin::Gui))
    {
        return _registry->Run(id, std::move(value), _shared->GetId(), std::move(origin));
    }
    std::string Unavailable(const char* id, const ActionValue& value)
    {
        return _registry->Unavailable(id, _shared->GetId(), Origin(execution::Origin::Gui), value);
    }

private:
    std::shared_ptr<Emulator> _shared;
    std::unique_ptr<ActionRegistry> _registry;
    std::vector<RegistrationHandle> _handles;
};
}  // namespace

/// region <ttd.seek_by: the curve>

TEST(SeekCurve_Test, TheRateBendsTheStepFromMinToMaxGeometrically)
{
    const SeekCurve curve;  // frames 1 -> 100, rates 4 -> 40
    EXPECT_EQ(SeekFrames(curve, -3, 0, 50), -3) << "rate 0: min per step";
    EXPECT_EQ(SeekFrames(curve, -3, 4, 50), -3) << "at rate_lo: min";
    EXPECT_EQ(SeekFrames(curve, 2, 22, 50), 20) << "midway: sqrt(min * max) = 10 per step";
    EXPECT_EQ(SeekFrames(curve, 2, 40, 50), 200) << "at rate_hi: max";
    EXPECT_EQ(SeekFrames(curve, 2, 400, 50), 200) << "above: max";
    EXPECT_EQ(SeekFrames(curve, 0.5, 22, 50), 5) << "a fractional delta";
    EXPECT_EQ(SeekFrames(curve, 0, 40, 50), 0);
}

TEST(SeekCurve_Test, SecondsBecomeFramesBeforeRounding)
{
    SeekCurve curve;
    std::string error;
    StateNode params = StateNode::Object();
    params["unit"] = "seconds";
    ASSERT_TRUE(SeekCurveFromParams(params, curve, error)) << error;
    EXPECT_TRUE(curve.seconds);
    EXPECT_DOUBLE_EQ(curve.min, 0.02);
    EXPECT_DOUBLE_EQ(curve.max, 2);
    const double pentagon = 3500000.0 / 71680.0;
    // 0.02 s on a Pentagon is 0.98 frames: one frame, not zero
    EXPECT_EQ(SeekFrames(curve, 1, 0, pentagon), 1);
    EXPECT_EQ(SeekFrames(curve, -1, 40, pentagon), -98) << "2 s = 97.66 frames";

    params["min"] = 1;
    params["max"] = 10;
    params["rate_lo"] = 2;
    params["rate_hi"] = 20.5;
    ASSERT_TRUE(SeekCurveFromParams(params, curve, error)) << error;
    EXPECT_DOUBLE_EQ(curve.rateHi, 20.5);
    EXPECT_EQ(SeekFrames(curve, 1, 0, 50), 50);
}

TEST(SeekCurve_Test, ParametersOutOfRangeAreRefused)
{
    SeekCurve curve;
    std::string error;
    auto refused = [&](const char* name, StateNode value) {
        StateNode params = StateNode::Object();
        params[name] = std::move(value);
        return !SeekCurveFromParams(params, curve, error);
    };
    EXPECT_TRUE(refused("unit", StateNode("minutes")));
    EXPECT_TRUE(refused("min", StateNode(0)));
    EXPECT_TRUE(refused("max", StateNode(0.5)));  // below the default min 1
    EXPECT_TRUE(refused("rate_hi", StateNode(4)));  // not above rate_lo
    EXPECT_TRUE(refused("min", StateNode("x")));
    EXPECT_FALSE(refused("min", StateNode("2")));  // a number as text (the CLI)
}

/// endregion </ttd.seek_by: the curve>

/// region <ttd.seek_by and ctx.set on both backends>

class SurfaceActions_Test : public ::testing::TestWithParam<bool>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::ITimeTravelHooks* _ttd = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;
    std::unique_ptr<TestRegistry> _registry;

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
        _ttd = _context->pTimeTravelHooks;
        _registry = std::make_unique<TestRegistry>(_emulator);
    }

    void TearDown() override
    {
        _registry.reset();
        if (_emulator)
            ContextState::Instance().Drop(_emulator->GetId());
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

    /// Record `frames` and stop: the history's first and end frames
    void Record(int frames, uint64_t& first, uint64_t& end)
    {
        ASSERT_TRUE(Ttd("start").Ok());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        ASSERT_TRUE(Ttd("stop").Ok());
        const ttd::TTDCursor cursor = _ttd->PublishedCursor();
        ASSERT_TRUE(cursor.history);
        first = cursor.first;
        end = cursor.end;
        ASSERT_GE(end, first + 6);
    }
};

INSTANTIATE_TEST_SUITE_P(Backends, SurfaceActions_Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Engine" : "V1"; });

TEST_P(SurfaceActions_Test, SeekByWithoutAHistoryIsNotNowRecordFirst)
{
    EXPECT_EQ(_registry->Unavailable("ttd.seek_by", Delta(-1)), "record first");
    const ActionResult result = _registry->Run("ttd.seek_by", Delta(-1));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "not_now");
    EXPECT_EQ(result.message, "record first");
}

TEST_P(SurfaceActions_Test, SeekByMovesByTheCurveAndClampsToTheHistoryWithAnEdgeMark)
{
    uint64_t first = 0;
    uint64_t end = 0;
    Record(8, first, end);
    ASSERT_TRUE(Ttd("seek", {{"frame", std::to_string(first + 4)}, {"tinframe", "0"}}).Ok());

    // Two frames back at a slow rate: one seek to the frame's start
    ActionResult result = _registry->Run("ttd.seek_by", Delta(-2, 1));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(Int(result.body, "from"), static_cast<int64_t>(first + 4));
    EXPECT_EQ(Int(result.body, "frame"), static_cast<int64_t>(first + 2));
    EXPECT_EQ(Int(result.body, "frames"), -2);
    EXPECT_EQ(Arrived(result), static_cast<int64_t>(first + 2));
    EXPECT_EQ(result.body.find("halt_reason")->s, "target");
    EXPECT_GT(result.body.find("fps")->d, 40.0);
    EXPECT_TRUE(Marks(result).empty()) << "no second boundary within 8 frames of the start";
    EXPECT_EQ(_ttd->PublishedCursor().frame, first + 2) << "the next turn counts from where this one arrived";

    // Far back at full speed: clamped to the first frame, never a target before it
    result = _registry->Run("ttd.seek_by", Delta(-3, 400));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Int(result.body, "frame"), static_cast<int64_t>(first));
    EXPECT_EQ(Arrived(result), static_cast<int64_t>(first));
    EXPECT_EQ(Marks(result), std::vector<std::string>{"edge@" + std::to_string(first)});

    // At the edge already: nothing moves, the edge is felt again
    result = _registry->Run("ttd.seek_by", Delta(-1));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Arrived(result), static_cast<int64_t>(first));
    EXPECT_EQ(Marks(result), std::vector<std::string>{"edge@" + std::to_string(first)});

    // Far forward: clamped to the end, never out_of_range
    result = _registry->Run("ttd.seek_by", Delta(5, 400));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Int(result.body, "frame"), static_cast<int64_t>(end));
    EXPECT_EQ(result.body.find("halt_reason")->s, "target");
    EXPECT_EQ(Arrived(result), static_cast<int64_t>(end));
    EXPECT_EQ(Marks(result), std::vector<std::string>{"edge@" + std::to_string(end)});

    // A fraction of a step moves nothing and is no error
    result = _registry->Run("ttd.seek_by", Delta(0.2));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Int(result.body, "frames"), 0);
}

TEST_P(SurfaceActions_Test, SeekByMarksABookmarkItCrossesAndTakesTheRateAsAParameter)
{
    uint64_t first = 0;
    uint64_t end = 0;
    Record(8, first, end);
    ASSERT_TRUE(Ttd("bookmark-add", {{"label", "here"}, {"frame", std::to_string(first + 3)}}).Ok());
    ASSERT_TRUE(Ttd("seek", {{"frame", std::to_string(first + 1)}, {"tinframe", "0"}}).Ok());

    // The CLI and the scripts pass the rate and the curve as parameters: 1 step of min 4 frames at rate 0
    StateNode params = StateNode::Object();
    params["min"] = 4;
    params["max"] = 8;
    params["rate"] = 0;
    ActionResult result = _registry->Run("ttd.seek_by", Delta(1, 0, params));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Arrived(result), static_cast<int64_t>(first + 5));
    EXPECT_EQ(Marks(result), std::vector<std::string>{"bookmark@" + std::to_string(first + 3)});

    StateNode bad = StateNode::Object();
    bad["unit"] = "minutes";
    result = _registry->Run("ttd.seek_by", Delta(1, 0, bad));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "Bad Request");
}

namespace
{
/// The NC_SURFACE_CONTEXT posts for one instance
struct ContextNotes
{
    std::mutex mutex;
    std::vector<std::string> seen;  ///< "previous>context by"
    std::atomic<int> count{0};
    unreal::UUID instance;
    uint64_t observer = 0;

    explicit ContextNotes(const std::string& id) : instance(id)
    {
        observer = MessageCenter::DefaultMessageCenter().AddObserver(
            NC_SURFACE_CONTEXT, ObserverCallbackFunc([this](int, Message* message) {
                auto* payload = message != nullptr ? dynamic_cast<SurfaceContextPayload*>(message->obj) : nullptr;
                if (payload == nullptr || payload->emulatorId != instance)
                    return;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    seen.push_back(payload->previous + ">" + payload->context + " " + payload->by);
                }
                count++;
            }));
    }
    ~ContextNotes() { MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_SURFACE_CONTEXT, observer); }
    std::string At(size_t i)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return i < seen.size() ? seen[i] : std::string("<none>");
    }
};
}  // namespace

TEST_P(SurfaceActions_Test, CtxSetPostsTheChangeAndTheLastSetterWins)
{
    const std::string id = _emulator->GetId();
    ContextNotes notes(id);
    ContextInfo info = ContextState::Instance().Get(id, _emulator);
    EXPECT_EQ(info.context, "play") << "no history: the default is play";
    EXPECT_FALSE(info.set);

    // A Stream Deck page names its context (a companion; ctx.set needs no scope)
    ActionResult result = _registry->Run("ctx.set", Text("ttd.timecode"), Origin(execution::Origin::Companion, "c-1"));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(result.body.find("context")->s, "ttd.timecode");
    EXPECT_EQ(result.body.find("previous")->s, "play");
    EXPECT_EQ(result.body.find("by")->s, "companion:c-1");
    ASSERT_TRUE(TestWait::ForAtLeast(notes.count, 1));
    EXPECT_EQ(notes.At(0), "play>ttd.timecode companion:c-1");

    // Then a script: the last one wins
    result = _registry->Run("ctx.set", Text("debug.disasm"), Origin(execution::Origin::WebApi, "webapi|x", "pytest"));
    ASSERT_TRUE(result.ok) << result.message;
    info = ContextState::Instance().Get(id, _emulator);
    EXPECT_EQ(info.context, "debug.disasm");
    EXPECT_TRUE(info.set);
    EXPECT_EQ(info.by, "webapi:pytest");
    ASSERT_TRUE(TestWait::ForAtLeast(notes.count, 2));
    EXPECT_EQ(notes.At(1), "ttd.timecode>debug.disasm webapi:pytest");

    // A name that is not one
    result = _registry->Run("ctx.set", Text("Debug Disasm"));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "Bad Request");
    EXPECT_EQ(ContextState::Instance().Get(id, _emulator).context, "debug.disasm");

    // "" returns to the default
    result = _registry->Run("ctx.set", Text(""));
    ASSERT_TRUE(result.ok) << result.message;
    info = ContextState::Instance().Get(id, _emulator);
    EXPECT_EQ(info.context, "play");
    EXPECT_FALSE(info.set);
    ASSERT_TRUE(TestWait::ForAtLeast(notes.count, 3));
    EXPECT_EQ(notes.At(2), "debug.disasm>play gui");
}

TEST_P(SurfaceActions_Test, TheDefaultContextIsTtdOnceAHistoryExists)
{
    uint64_t first = 0;
    uint64_t end = 0;
    Record(6, first, end);
    const ContextInfo info = ContextState::Instance().Get(_emulator->GetId(), _emulator);
    EXPECT_EQ(info.context, "ttd");
    EXPECT_FALSE(info.set);
    EXPECT_EQ(info.fallback, "ttd");
}

/// endregion </ttd.seek_by and ctx.set on both backends>

/// region <run.step_by>

class StepBy_Test : public ::testing::Test
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
    }

    void TearDown() override
    {
        _registry.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Pause()
    {
        _emulator->Pause();
        ASSERT_TRUE(TestWait::For([&]() {
            const execution::Snapshot s = _emulator->GetArbiter().GetSnapshot();
            return s.intent == execution::Intent::Pause && s.loopParked;
        }));
    }

    /// NOPs at $8000, the machine there with interrupts off
    void Nops()
    {
        Memory* memory = _emulator->GetMemory();
        for (uint16_t a = 0x8000; a < 0x8040; a++)
            memory->DirectWriteToZ80Memory(a, 0x00);
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
    }
};

TEST_F(StepBy_Test, ItNeedsAPauseAndBackNeedsAHistory)
{
    ActionResult result = _registry->Run("run.step_by", Delta(1));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "not_now");
    EXPECT_EQ(result.message, "needs a pause");

    Pause();
    result = _registry->Run("run.step_by", Delta(-1));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "not_now");
    EXPECT_EQ(result.message, "record first");
    EXPECT_EQ(_registry->Unavailable("run.step_by", Delta(1)), "") << "forward needs no history";
}

TEST_F(StepBy_Test, ForwardStepsInAtMostSixteenTimes)
{
    Pause();
    Nops();
    ActionResult result = _registry->Run("run.step_by", Delta(3));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(Int(result.body, "steps"), 3);
    EXPECT_EQ(result.body.find("direction")->s, "forward");
    EXPECT_EQ(Int(result.body, "pc"), 0x8003);

    result = _registry->Run("run.step_by", Delta(40));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Int(result.body, "steps"), 16) << "a fast spin: 16 at most";
    EXPECT_EQ(Int(result.body, "pc"), 0x8013);
}

TEST_F(StepBy_Test, ABreakpointEndsTheSteps)
{
    Pause();
    Nops();
    auto* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = 0x8003;
    ASSERT_NE(_emulator->GetBreakpointManager()->AddBreakpoint(breakpoint), 0);

    const ActionResult result = _registry->Run("run.step_by", Delta(10));
    ASSERT_TRUE(result.ok) << result.error << ": " << result.message;
    EXPECT_EQ(Int(result.body, "steps"), 3) << "the step that met the breakpoint executed nothing";
    EXPECT_EQ(result.body.find("stopped_by")->s, "breakpoint");
    EXPECT_EQ(Int(result.body, "pc"), 0x8003);
}

/// endregion </run.step_by>
