#include "emulator/actions/builtinactions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <utility>

#include "debugger/ttd/timelinemarks.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/contextstate.h"
#include "emulator/actions/steppingactions.h"
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

/// Time travel with a recorded history (the published cursor: never waits for the machine)
std::string NoHistory(Emulator& emulator, const ActionValue& value)
{
    if (std::string why = NoTtd(emulator, value); !why.empty())
        return why;
    return emulator.GetContext()->pTimeTravelHooks->PublishedCursor().history ? std::string()
                                                                                : std::string("record first");
}

const StateNode* Param(const ActionValue& value, const char* name)
{
    return value.params.find(name);
}

bool NumberOf(const StateNode* node, double& out)
{
    if (node == nullptr)
        return false;
    if (node->kind == StateNode::Kind::Int)
        out = static_cast<double>(node->i);
    else if (node->kind == StateNode::Kind::Double)
        out = node->d;
    else if (node->kind == StateNode::Kind::String)
    {
        char* end = nullptr;
        out = std::strtod(node->s.c_str(), &end);
        if (node->s.empty() || end == nullptr || *end != '\0')
            return false;
    }
    else
        return false;
    return std::isfinite(out);
}

StateNode MarksNode(const std::vector<ttd::TimelineMark>& marks)
{
    StateNode list = StateNode::Array();
    for (const ttd::TimelineMark& mark : marks)
    {
        StateNode node = StateNode::Object();
        node["kind"] = ttd::TimelineMarkKindName(mark.kind);
        node["frame"] = mark.frame;
        list.push(std::move(node));
    }
    return list;
}

/// The bookmark frames of the session (the bookmarks verb)
std::vector<uint64_t> BookmarkFrames(EmulatorContext* context)
{
    std::vector<uint64_t> frames;
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"bookmarks", {}});
    const StateNode* list = reply.Ok() ? reply.body.find("bookmarks") : nullptr;
    if (list == nullptr || list->kind != StateNode::Kind::Array)
        return frames;
    for (const StateNode& bookmark : list->items)
    {
        const StateNode* frame = bookmark.find("frame");
        if (frame != nullptr && frame->kind == StateNode::Kind::Int)
            frames.push_back(static_cast<uint64_t>(frame->i));
    }
    return frames;
}

/// run.step_by (actions.md §4): forward steps in (a breakpoint ends the loop), back steps back through the history
ActionResult StepBy(const ActionCall& call)
{
    // Another unit (Stream Deck D2.8: the unit and N on keys, a dial steps): frames, scanlines, tstates
    if (const StateNode* unit = Param(call.value, "unit"))
    {
        const std::string name = unit->kind == StateNode::Kind::String ? ScrubUnitName(unit->s) : std::string();
        if (name != "instructions")
        {
            if (name != "frames" && name != "scanlines" && name != "tstates")
                return ActionResult::Failure("Bad Request", "unit: instructions, frames, scanlines or tstates");
            double count = 1;
            if (const StateNode* given = Param(call.value, "count"); given != nullptr && (!NumberOf(given, count) || count < 1))
                return ActionResult::Failure("Bad Request", "count: a whole number >= 1");
            return StepByUnit(call, name, static_cast<int64_t>(std::llround(count)));
        }
    }
    const int64_t steps = static_cast<int64_t>(std::llround(call.value.delta));
    const int64_t count = std::min<int64_t>(16, steps < 0 ? -steps : steps);
    StateNode body = StateNode::Object();
    int64_t done = 0;
    std::string stoppedBy;
    for (int64_t i = 0; i < count; i++)
    {
        ActionResult step = steps > 0
                                ? FromRun(RunControl(call.emulator).StepIn(call.requester))
                                : FromTtd(ttd::TTDControl(call.emulator->GetContext())
                                              .Execute({"step-instruction", {{"dir", "back"}}}));
        if (!step.ok)
        {
            if (done == 0)
                return step;
            stoppedBy = step.error;  // the history's start, a refusal: the steps so far stay
            break;
        }
        body = std::move(step.body);
        // An execution breakpoint stops a step in before its instruction: that step executed nothing
        const StateNode* executed = body.find("executed");
        done += executed != nullptr && executed->kind == StateNode::Kind::Int ? executed->i : 1;
        const StateNode* stop = body.find("stop");
        const StateNode* reason = stop != nullptr ? stop->find("reason") : nullptr;
        if (reason != nullptr && reason->kind == StateNode::Kind::String && reason->s == "breakpoint")
        {
            stoppedBy = "breakpoint";
            break;
        }
    }
    body["direction"] = steps < 0 ? "back" : "forward";
    body["steps"] = done;
    if (!stoppedBy.empty())
        body["stopped_by"] = stoppedBy;
    return ActionResult::Success(std::move(body));
}
}  // namespace

/// ttd.seek_by (actions.md §2): the curve, the clamp to the history, one seek to the target frame's start, the marks.
/// The unit (Stream Deck D3.1): the call's `unit` param, else the instance's (ttd.scrub_unit); frames, seconds and
/// batch take the curve, the others SeekByUnit
ActionResult SeekBy(const ActionCall& call)
{
    EmulatorContext* context = call.emulator->GetContext();
    std::string unit = ScrubUnitOf(call.instanceId);
    int64_t count = ScrubCountOf(call.instanceId);
    if (const StateNode* named = Param(call.value, "unit"))
    {
        unit = named->kind == StateNode::Kind::String ? ScrubUnitName(named->s) : std::string();
        count = ScrubDefaultCount(unit);
    }
    if (const StateNode* given = Param(call.value, "count"))
    {
        double number = 0;
        if (!NumberOf(given, number) || number < 1)
            return ActionResult::Failure("Bad Request", "count: a whole number >= 1");
        count = static_cast<int64_t>(std::llround(number));
    }
    if (unit != "frames" && unit != "seconds" && unit != "batch" && !unit.empty())
        return SeekByUnit(call, unit, count);
    SeekCurve curve;
    std::string error;
    StateNode params = call.value.params;
    if (!unit.empty())
        params["unit"] = unit;
    if (unit == "batch" && params.find("min") == nullptr && params.find("max") == nullptr)
    {
        params["min"] = static_cast<double>(count);
        params["max"] = static_cast<double>(count);
    }
    if (!SeekCurveFromParams(params, curve, error))
        return ActionResult::Failure("Bad Request", error);
    // The rate: the value's, else a parameter (the CLI and the scripts pass it as one)
    double rate = std::fabs(call.value.rate);
    if (rate == 0)
        NumberOf(Param(call.value, "rate"), rate);
    rate = std::fabs(rate);

    const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
    if (!cursor.history)
        return ActionResult::Failure("not_now", "record first");
    const double fps = ttd::TTDFrameRate(context);
    const int64_t frames = SeekFrames(curve, call.value.delta, rate, fps);
    const uint64_t from = cursor.frame;
    // The target, clamped to the history: a seek past its end would come back out_of_range without a move
    const int64_t wanted = static_cast<int64_t>(from) + frames;
    const int64_t target = std::clamp<int64_t>(wanted, static_cast<int64_t>(cursor.first),
                                               static_cast<int64_t>(cursor.end));
    const bool clamped = target != wanted;

    StateNode body = StateNode::Object();
    std::vector<ttd::TimelineMark> marks;
    uint64_t arrived = from;
    if (frames != 0 && static_cast<uint64_t>(target) != from)
    {
        ttd::TTDReply reply =
            ttd::TTDControl(context).Execute({"seek", {{"frame", std::to_string(target)}, {"tinframe", "0"}}});
        if (!reply.Ok())
            return FromTtd(std::move(reply));
        body = std::move(reply.body);
        if (const StateNode* at = body.find("arrived_at"))
            if (const StateNode* frame = at->find("frame"); frame != nullptr && frame->kind == StateNode::Kind::Int)
                arrived = static_cast<uint64_t>(frame->i);
        marks = ttd::timeline_marks::Crossed(from, arrived, cursor.first, fps, BookmarkFrames(context));
        const StateNode* halt = body.find("halt_reason");
        if (halt != nullptr && halt->kind == StateNode::Kind::String && halt->s == "external_event")
            marks.push_back({ttd::TimelineMarkKind::Barrier, arrived});
    }
    else
    {
        // Nothing to move (a fraction of a step, or already at the edge): the place it stands
        StateNode at = StateNode::Object();
        at["frame"] = cursor.frame;
        at["tinframe"] = cursor.tinframe;
        body["reached"] = true;
        body["arrived_at"] = std::move(at);
        body["halt_reason"] = "target";
        body["state"] = ttd::TTDSessionStateToString(cursor.state);
    }
    if (clamped)
        marks.push_back({ttd::TimelineMarkKind::Edge, static_cast<uint64_t>(target)});
    ttd::timeline_marks::SortStrongestFirst(marks);
    body["from"] = from;
    body["frame"] = static_cast<uint64_t>(target);
    body["frames"] = frames;
    body["fps"] = fps;
    body["unit"] = unit.empty() ? std::string("frames") : unit;
    body["marks"] = MarksNode(marks);
    return ActionResult::Success(std::move(body));
}

bool SeekCurveFromParams(const StateNode& params, SeekCurve& curve, std::string& error)
{
    curve = SeekCurve{};
    if (const StateNode* unit = params.find("unit"))
    {
        const std::string name = unit->kind == StateNode::Kind::String ? ScrubUnitName(unit->s) : std::string();
        if (name.empty())
        {
            error = "unit: frames, seconds, batch, instructions, tstates, scanlines or bookmarks";
            return false;
        }
        curve.seconds = name == "seconds";
    }
    if (curve.seconds)
    {
        curve.min = 0.02;
        curve.max = 2;
    }
    const struct
    {
        const char* name;
        double* value;
    } fields[] = {{"min", &curve.min}, {"max", &curve.max}, {"rate_lo", &curve.rateLo}, {"rate_hi", &curve.rateHi}};
    for (const auto& field : fields)
    {
        const StateNode* node = params.find(field.name);
        if (node != nullptr && !NumberOf(node, *field.value))
        {
            error = std::string(field.name) + ": a number";
            return false;
        }
    }
    if (!(curve.min > 0) || curve.max < curve.min)
    {
        error = "min and max: 0 < min <= max";
        return false;
    }
    if (curve.rateLo < 0 || curve.rateHi <= curve.rateLo)
    {
        error = "rate_lo and rate_hi: 0 <= rate_lo < rate_hi";
        return false;
    }
    return true;
}

int64_t SeekFrames(const SeekCurve& curve, double delta, double rate, double fps)
{
    if (!std::isfinite(delta) || delta == 0)
        return 0;
    const double t = std::clamp((rate - curve.rateLo) / (curve.rateHi - curve.rateLo), 0.0, 1.0);
    const double per = curve.min * std::pow(curve.max / curve.min, t);
    const double perFrame = curve.seconds ? std::max(1.0, static_cast<double>(std::llround(per * fps)))
                                          : std::max(1.0, static_cast<double>(std::llround(per)));
    return static_cast<int64_t>(std::llround(delta * perFrame));
}

std::string SetterName(const ActionOrigin& origin)
{
    std::string name = execution::OriginName(origin.origin);
    if (origin.origin == execution::Origin::Companion || origin.origin == execution::Origin::HostDevice)
    {
        if (!origin.identity.empty())
            name += ":" + origin.identity;
    }
    else if (!origin.client.empty())
        name += ":" + origin.client;
    return name;
}

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
        handles.push_back(registry.Register({id, label, group, kind, held, false, {}, {}, false}, std::move(run), std::move(rules)));
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
    // The verbs that move through or mark a recorded history: unavailable until one is recorded (running or stopped)
    auto historyRules = [&rules](uint32_t scope, bool needsPause = false) {
        ActionRules r = rules(scope, needsPause);
        r.unavailable = NoHistory;
        return r;
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
    {
        // An encoder both ways (owner 2026-10-17): forward steps in, back steps back through the history
        ActionRules r = rules(ScopeDebug, true);
        r.unavailable = [](Emulator& emulator, const ActionValue& value) {
            return value.delta < 0 ? NoHistory(emulator, value) : std::string();
        };
        add("run.step_by", "Step by (in forward, back through the history)", run, ValueKind::Delta, StepBy,
            std::move(r));
    }
    add("run.turbo_hold", "Turbo while held", run, ValueKind::Press,
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).SetTurbo(call.value.pressed)); },
        rules(ScopeRun), true);

    // 3.2 Time travel: the TTDControl verbs (the ones the MCP time_travel tool calls)
    const char* ttdGroup = "Time travel";
    // The call's params go along as the verb's options (ttd.bookmark_add: label; ttd.reverse_step: count)
    auto verb = [](const char* name, std::map<std::string, std::string> options = {}) {
        return [name, options](const ActionCall& call) {
            std::map<std::string, std::string> all = ParamOptions(call.value.params);
            for (const auto& [key, text] : options)
                all[key] = text;
            return FromTtd(ttd::TTDControl(call.emulator->GetContext()).Execute({name, std::move(all)}));
        };
    };
    add("ttd.record_toggle", "Start / stop recording", ttdGroup, ValueKind::Press,
        [](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            const bool recording = context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->IsRecording();
            return FromTtd(ttd::TTDControl(context).Execute({recording ? "stop" : "start", {}}));
        },
        ttdRules(0));
    add("ttd.step_back_frame", "Back one frame", ttdGroup, ValueKind::Press, verb("step-back"), historyRules(ScopeRewind));
    add("ttd.step_forward_frame", "Forward one frame", ttdGroup, ValueKind::Press, verb("step-forward"),
        historyRules(ScopeRewind));
    add("ttd.step_back_instruction", "Back one instruction", ttdGroup, ValueKind::Press,
        verb("step-instruction", {{"dir", "back"}}), historyRules(ScopeDebug, true));
    add("ttd.step_forward_instruction", "Forward one instruction", ttdGroup, ValueKind::Press,
        verb("step-instruction", {{"dir", "forward"}}), historyRules(ScopeDebug, true));
    add("ttd.reverse_step", "Reverse step", ttdGroup, ValueKind::Press, verb("reverse-step"), historyRules(ScopeDebug));
    add("ttd.reverse_continue", "Reverse continue", ttdGroup, ValueKind::Press, verb("reverse-continue"),
        historyRules(ScopeDebug));
    add("ttd.bookmark_add", "Add a bookmark", ttdGroup, ValueKind::Press, verb("bookmark-add"), historyRules(ScopeDebug));
    add("ttd.resume_live", "Resume live", ttdGroup, ValueKind::Press, verb("resume"), historyRules(ScopeRewind));
    // An encoder's turn (the Surface Dial): a frame or an instruction per step, clockwise forward
    add("ttd.jog_frames", "Jog frames", ttdGroup, ValueKind::Delta,
        [](const ActionCall& call) {
            const auto frames = static_cast<int64_t>(std::llround(call.value.delta));
            if (frames == 0)
                return ActionResult::Success();
            return MoveFrames(call.emulator->GetContext(), frames);
        },
        historyRules(ScopeRewind));
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
        historyRules(ScopeDebug, true));
    {
        // A dial's seek with an acceleration curve, clamped to the history, with the marks it crossed (actions.md §2)
        ActionRules r = rules(ScopeRewind);
        r.unavailable = NoHistory;
        add("ttd.seek_by", "Seek by (accelerated, with marks)", ttdGroup, ValueKind::Delta, SeekBy, std::move(r));
    }
    // ttd.find_last_write, ttd.bookmark_prev / _next, ttd.step_back_scanline: steppingactions.cpp

    // 3.5 Contexts (architecture §6): the instance's context, the last setter wins (Q-5); "" returns to the default
    add("ctx.set", "Set the surface context", "Context", ValueKind::Text,
        [](const ActionCall& call) {
            ContextState& contexts = ContextState::Instance();
            const std::string previous = contexts.Get(call.instanceId, call.emulator.get()).context;
            std::string error;
            if (!contexts.Set(call.instanceId, call.emulator.get(), call.value.text, SetterName(call.origin), error))
                return ActionResult::Failure("Bad Request", error);
            const ContextInfo info = contexts.Get(call.instanceId, call.emulator.get());
            StateNode body = StateNode::Object();
            body["context"] = info.context;
            body["previous"] = previous;
            body["set"] = info.set;
            body["by"] = info.by;
            body["default"] = info.fallback;
            return ActionResult::Success(std::move(body));
        },
        rules(0));

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
    // cap.record_toggle and cap.clip_last: hostactions_machine.cpp (over capture::RecordingControl)
    return handles;
}

}  // namespace actions
