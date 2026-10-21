#include "emulator/actions/builtinactions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/runcontrol.h"

namespace actions
{
namespace
{
using automation_gate::ScopeDebug;
using automation_gate::ScopeRewind;
using automation_gate::ScopeRun;
using execution::RunControl;
using execution::RunReply;

ActionResult FromRun(RunReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(std::move(reply.error), std::move(reply.message), std::move(reply.body));
}

ActionResult FromTtd(ttd::TTDReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(ttd::TTDControlErrorPhrase(reply.error), std::move(reply.message),
                                 std::move(reply.body));
}

int64_t IntParam(const ActionValue& value, const char* name, int64_t fallback)
{
    const StateNode* node = value.params.find(name);
    if (node == nullptr)
        return fallback;
    if (node->kind == StateNode::Kind::Int)
        return node->i;
    if (node->kind == StateNode::Kind::Double)
        return static_cast<int64_t>(std::llround(node->d));
    return fallback;
}

struct Place
{
    int64_t frame = 0;
    int64_t tinframe = 0;
    bool operator==(const Place& other) const { return frame == other.frame && tinframe == other.tinframe; }
};

/// The position verb's place and the history's end frame; false without a session
bool Position(EmulatorContext* context, Place& place, int64_t& end)
{
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"position", {}});
    const StateNode* current = reply.body.find("current");
    const StateNode* last = reply.body.find("session_end");
    const StateNode* frame = current != nullptr ? current->find("frame") : nullptr;
    const StateNode* tinframe = current != nullptr ? current->find("tinframe") : nullptr;
    const StateNode* lastFrame = last != nullptr ? last->find("frame") : nullptr;
    if (!reply.Ok() || frame == nullptr || tinframe == nullptr || lastFrame == nullptr)
        return false;
    place = {frame->i, tinframe->i};
    end = lastFrame->i;
    return true;
}

/// The last move per instance (MoveFrames): the place it left and the frame it meant
struct LastMove
{
    Place place;
    int64_t frame = 0;
};

std::mutex& MovesMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<const EmulatorContext*, LastMove>& Moves()
{
    static std::map<const EmulatorContext*, LastMove> moves;
    return moves;
}

/// A held reset fires on its release after 1 s, so it is never an accident: the press time per instance and holder
std::mutex& ResetMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::pair<std::string, std::string>, std::chrono::steady_clock::time_point>& ResetPresses()
{
    static std::map<std::pair<std::string, std::string>, std::chrono::steady_clock::time_point> presses;
    return presses;
}

std::string NoTtd(Emulator& emulator, const ActionValue&)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pTimeTravelHooks != nullptr ? std::string()
                                                                       : std::string("time travel is not available");
}
}  // namespace

ActionResult MoveFrames(EmulatorContext* context, int64_t frames)
{
    Place place;
    int64_t end = 0;
    if (!Position(context, place, end))
        return ActionResult::Failure("Conflict", "no time travel history");
    int64_t from = place.frame;
    {
        std::lock_guard<std::mutex> lock(MovesMutex());
        auto it = Moves().find(context);
        if (it != Moves().end() && it->second.place == place)
            from = it->second.frame;
    }
    // The end of frame end - 1 is the end of the history: frames past it are the same place
    const int64_t target = std::clamp<int64_t>(from + frames, 0, std::max<int64_t>(0, end - 1));
    const char* edge = frames < 0 ? "the start of the history" : "the end of the history";
    if (target == from)
        return ActionResult::Failure("Conflict", edge);
    ttd::TTDReply reply = ttd::TTDControl(context).Execute({"seek", {{"frame", std::to_string(target)}}});
    if (!reply.Ok())
        return FromTtd(std::move(reply));
    const StateNode* reached = reply.body.find("reached");
    if (reached != nullptr && reached->kind == StateNode::Kind::Bool && !reached->b)
        return ActionResult::Failure("Conflict", edge, std::move(reply.body));
    if (Position(context, place, end))
    {
        std::lock_guard<std::mutex> lock(MovesMutex());
        Moves()[context] = {place, target};
    }
    reply.body["frame"] = target;
    return ActionResult::Success(std::move(reply.body));
}

std::vector<RegistrationHandle> RegisterBuiltinActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, Executor run,
                   ActionRules rules, bool held = false) {
        handles.push_back(registry.Register({id, label, group, kind, held, false}, std::move(run), std::move(rules)));
    };
    auto rules = [](uint32_t scope, bool needsPause = false) {
        ActionRules r;
        r.scope = ActionRules::Fixed(scope);
        r.needsPause = needsPause;
        return r;
    };
    auto ttdRules = [&rules](uint32_t scope, bool needsPause = false) {
        ActionRules r = rules(scope, needsPause);
        r.unavailable = NoTtd;
        return r;
    };
    auto stub = [&](const char* id, const char* group) {
        ActionRules r;
        r.unavailable = [](Emulator&, const ActionValue&) { return std::string("not available yet"); };
        r.unavailableError = "Not Implemented";
        add(id, id, group, ValueKind::Press, nullptr, std::move(r));
    };

    // 3.1 Run control
    const char* run = "Run";
    {
        ActionRules r;
        // Pausing is free (as POST /pause); resuming needs the run scope (as POST /resume)
        r.scope = [](Emulator& emulator, const ActionValue&) { return emulator.IsPaused() ? ScopeRun : 0u; };
        add("run.pause_toggle", "Pause / continue", run, ValueKind::Press,
            [](const ActionCall& call) {
                RunControl control(call.emulator);
                return FromRun(call.emulator->IsPaused() ? control.Resume(call.requester)
                                                         : control.Pause(call.requester));
            },
            std::move(r));
    }
    add("run.step_in", "Step in", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).StepIn(call.requester)); },
        rules(ScopeDebug, true));
    add("run.step_over", "Step over", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).StepOver(call.requester)); },
        rules(ScopeDebug, true));
    add("run.step_out", "Step out", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).StepOut(call.requester)); },
        rules(ScopeDebug, true));
    add("run.frame_step", "Frame step", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).FrameStep(call.requester)); },
        rules(ScopeDebug, true));
    add("run.wait_int", "Run to interrupt", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).RunToInterrupt(call.requester)); },
        rules(ScopeDebug, true));
    add("run.to_scanline", "Run to scanline", run, ValueKind::Press,
        [](const ActionCall& call) {
            const int64_t line = IntParam(call.value, "line", 0);
            if (line < 0 || line > 1023)
                return ActionResult::Failure("Bad Request", "line: 0..1023");
            return FromRun(RunControl(call.emulator).RunToScanline(call.requester, static_cast<unsigned>(line)));
        },
        rules(ScopeDebug, true));
    add("run.n_scanlines", "Run N scanlines", run, ValueKind::Press,
        [](const ActionCall& call) {
            const int64_t count = std::clamp<int64_t>(IntParam(call.value, "count", 1), 1, 1000);
            return FromRun(RunControl(call.emulator).RunNScanlines(call.requester, static_cast<unsigned>(count)));
        },
        rules(ScopeDebug, true));
    add("run.turbo_hold", "Turbo while held", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).SetTurbo(call.value.pressed)); },
        rules(ScopeRun), true);

    // 3.2 Time travel: the TTDControl verbs (the ones the MCP time_travel tool calls)
    const char* ttdGroup = "Time travel";
    auto verb = [](const char* name, std::map<std::string, std::string> options = {}) {
        return [name, options](const ActionCall& call) {
            return FromTtd(ttd::TTDControl(call.emulator->GetContext()).Execute({name, options}));
        };
    };
    add("ttd.record_toggle", "Start / stop recording", ttdGroup, ValueKind::Press,
        [](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            const bool recording = context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->IsRecording();
            return FromTtd(ttd::TTDControl(context).Execute({recording ? "stop" : "start", {}}));
        },
        ttdRules(0));
    add("ttd.step_back_frame", "Back one frame", ttdGroup, ValueKind::Press, verb("step-back"), ttdRules(ScopeRewind));
    add("ttd.step_forward_frame", "Forward one frame", ttdGroup, ValueKind::Press, verb("step-forward"),
        ttdRules(ScopeRewind));
    add("ttd.step_back_instruction", "Back one instruction", ttdGroup, ValueKind::Press,
        verb("step-instruction", {{"dir", "back"}}), ttdRules(ScopeDebug, true));
    add("ttd.step_forward_instruction", "Forward one instruction", ttdGroup, ValueKind::Press,
        verb("step-instruction", {{"dir", "forward"}}), ttdRules(ScopeDebug, true));
    add("ttd.reverse_step", "Reverse step", ttdGroup, ValueKind::Press, verb("reverse-step"), ttdRules(ScopeDebug));
    add("ttd.reverse_continue", "Reverse continue", ttdGroup, ValueKind::Press, verb("reverse-continue"),
        ttdRules(ScopeDebug));
    add("ttd.bookmark_add", "Add a bookmark", ttdGroup, ValueKind::Press, verb("bookmark-add"), ttdRules(ScopeDebug));
    add("ttd.resume_live", "Resume live", ttdGroup, ValueKind::Press, verb("resume"), ttdRules(ScopeRewind));
    // An encoder's turn (the Surface Dial): a frame or an instruction per step, clockwise forward
    add("ttd.jog_frames", "Jog frames", ttdGroup, ValueKind::Delta,
        [](const ActionCall& call) {
            const auto frames = static_cast<int64_t>(std::llround(call.value.delta));
            if (frames == 0)
                return ActionResult::Success();
            return MoveFrames(call.emulator->GetContext(), frames);
        },
        ttdRules(ScopeRewind));
    add("ttd.jog_instructions", "Jog instructions", ttdGroup, ValueKind::Delta,
        [](const ActionCall& call) {
            const auto steps = static_cast<int64_t>(std::llround(call.value.delta));
            const int64_t count = std::min<int64_t>(64, steps < 0 ? -steps : steps);  // a fast spin: 64 at most
            ActionResult result = ActionResult::Success();
            for (int64_t i = 0; i < count; i++)
            {
                result = FromTtd(ttd::TTDControl(call.emulator->GetContext())
                                     .Execute({"step-instruction", {{"dir", steps < 0 ? "back" : "forward"}}}));
                if (!result.ok)
                    return result;
            }
            result.body["steps"] = count;
            return result;
        },
        ttdRules(ScopeDebug, true));
    for (const char* id : {"ttd.find_last_write", "ttd.bookmark_prev", "ttd.bookmark_next", "ttd.step_back_scanline"})
        stub(id, ttdGroup);

    // 3.4 Capture and machine
    const char* machine = "Capture and machine";
    add("mach.reset", "Reset (hold 1 s)", machine, ValueKind::Press,
        [](const ActionCall& call) {
            const auto key = std::make_pair(call.instanceId, call.origin.identity);
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(ResetMutex());
            if (call.value.pressed)
            {
                ResetPresses()[key] = now;
                StateNode body = StateNode::Object();
                body["armed"] = true;
                return ActionResult::Success(std::move(body), "Hold for 1 s, then release to reset");
            }
            auto it = ResetPresses().find(key);
            const bool longEnough = it != ResetPresses().end() && now - it->second >= std::chrono::seconds(1);
            if (it != ResetPresses().end())
                ResetPresses().erase(it);
            if (!longEnough)
                return ActionResult::Failure("not_now", "hold the reset for 1 s");
            return FromRun(RunControl(call.emulator).Reset());
        },
        rules(ScopeRun), true);
    add("mach.nmi", "NMI", machine, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).Nmi()); }, rules(ScopeRun));
    add("mach.turbo_toggle", "Turbo", machine, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).ToggleTurbo()); }, rules(ScopeRun));
    for (const char* id : {"cap.record_toggle", "cap.clip_last"})
        stub(id, machine);
    return handles;
}

}  // namespace actions
