#include "emulator/actions/steppingactions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <utility>

#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/execution/runpacer.h"

namespace actions
{

/// region <DebuggerCursors>

struct DebuggerCursors::Impl
{
    mutable std::mutex mutex;
    std::map<std::pair<std::string, std::string>, uint16_t> cursors;
};

DebuggerCursors& DebuggerCursors::Instance()
{
    static DebuggerCursors* cursors = new DebuggerCursors();  // never destroyed: workers read it at exit
    return *cursors;
}

DebuggerCursors::Impl& DebuggerCursors::impl() const
{
    static Impl* impl = new Impl();
    return *impl;
}

void DebuggerCursors::Set(const std::string& instanceId, const std::string& pane, std::optional<uint16_t> address)
{
    Impl& data = impl();
    std::lock_guard<std::mutex> lock(data.mutex);
    if (address)
        data.cursors[{instanceId, pane}] = *address;
    else
        data.cursors.erase({instanceId, pane});
}

std::optional<uint16_t> DebuggerCursors::Get(const std::string& instanceId, const std::string& pane) const
{
    Impl& data = impl();
    std::lock_guard<std::mutex> lock(data.mutex);
    auto it = data.cursors.find({instanceId, pane});
    if (it == data.cursors.end())
        return std::nullopt;
    return it->second;
}

void DebuggerCursors::Drop(const std::string& instanceId)
{
    Impl& data = impl();
    std::lock_guard<std::mutex> lock(data.mutex);
    for (auto it = data.cursors.begin(); it != data.cursors.end();)
        it = it->first.first == instanceId ? data.cursors.erase(it) : std::next(it);
}

/// endregion </DebuggerCursors>

namespace
{
using automation_gate::ScopeDebug;
using automation_gate::ScopeRewind;
using automation_gate::ScopeRun;
using execution::RunControl;
using execution::RunPacer;
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

ttd::TTDReply Ttd(EmulatorContext* context, const std::string& verb, std::map<std::string, std::string> options = {})
{
    return ttd::TTDControl(context).Execute({verb, std::move(options)});
}

ActionResult BadRequest(std::string message)
{
    return ActionResult::Failure("Bad Request", std::move(message));
}

ActionRules Rules(uint32_t scope, bool needsPause = false)
{
    ActionRules rules;
    rules.scope = ActionRules::Fixed(scope);
    rules.needsPause = needsPause;
    return rules;
}

/// The scope of the REST route the action stands for, as the automation gate decides it
ActionRules RouteRules(const char* method, const char* subpath, bool needsPause = false)
{
    const uint32_t scope = automation_gate::ChangesMachine(method, subpath)
                               ? automation_gate::CompanionScopeOf(method, subpath)
                               : 0u;
    return Rules(scope, needsPause);
}

std::string NoTtd(Emulator& emulator, const ActionValue&)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pTimeTravelHooks != nullptr ? std::string()
                                                                       : std::string("time travel is not available");
}

std::string NoHistory(Emulator& emulator, const ActionValue& value)
{
    if (std::string why = NoTtd(emulator, value); !why.empty())
        return why;
    return emulator.GetContext()->pTimeTravelHooks->PublishedCursor().history ? std::string()
                                                                                : std::string("record first");
}

ActionRules TtdRules(uint32_t scope, bool history)
{
    ActionRules rules = Rules(scope);
    rules.unavailable = history ? NoHistory : NoTtd;
    return rules;
}

/// A count param (>= 1, at most `max`), `fallback` when absent
bool CountParam(const ActionValue& value, const char* name, int64_t fallback, int64_t max, int64_t& count,
                std::string& error)
{
    const std::optional<int64_t> given = IntParam(value, name, error);
    if (!error.empty())
        return false;
    count = given.value_or(fallback);
    if (count < 1 || count > max)
    {
        error = std::string(name) + ": 1.." + std::to_string(max);
        return false;
    }
    return true;
}

/// A t-state budget param (max_tstates; 0 or absent: the verb's default)
bool BudgetParam(const ActionValue& value, uint64_t& budget, std::string& error)
{
    const std::optional<int64_t> given = IntParam(value, "max_tstates", error);
    if (!error.empty())
        return false;
    if (given && *given < 0)
    {
        error = "max_tstates: >= 0";
        return false;
    }
    budget = given ? static_cast<uint64_t>(*given) : 0;
    return true;
}

/// The address a call names (`addr`), else the debugger's cursor of `panes` in order; false with why not
bool AddressOrCursor(const ActionCall& call, std::initializer_list<const char*> panes, uint16_t& address,
                     std::string& error)
{
    if (call.value.params.find("addr") != nullptr)
        return AddressParam(call.value.params, "addr", address, error);
    for (const char* pane : panes)
        if (const std::optional<uint16_t> cursor = DebuggerCursors::Instance().Get(call.instanceId, pane))
        {
            address = *cursor;
            return true;
        }
    error = "addr: an address (no debugger cursor to take it from)";
    return false;
}

struct Point
{
    uint64_t frame = 0;
    uint32_t tinframe = 0;
    bool operator<(const Point& other) const
    {
        return frame != other.frame ? frame < other.frame : tinframe < other.tinframe;
    }
    bool operator==(const Point& other) const { return frame == other.frame && tinframe == other.tinframe; }
};

StateNode PointNode(const Point& point)
{
    StateNode node = StateNode::Object();
    node["frame"] = point.frame;
    node["tinframe"] = static_cast<uint64_t>(point.tinframe);
    return node;
}

Point CursorPoint(const ttd::TTDCursor& cursor)
{
    return {cursor.frame, cursor.tinframe};
}

/// The bookmarks of the session, time-sorted: {point, label}
std::vector<std::pair<Point, std::string>> Bookmarks(EmulatorContext* context)
{
    std::vector<std::pair<Point, std::string>> out;
    const ttd::TTDReply reply = Ttd(context, "bookmarks");
    const StateNode* list = reply.Ok() ? reply.body.find("bookmarks") : nullptr;
    if (list == nullptr || list->kind != StateNode::Kind::Array)
        return out;
    for (const StateNode& bookmark : list->items)
    {
        const StateNode* frame = bookmark.find("frame");
        const StateNode* tinframe = bookmark.find("tinframe");
        const StateNode* label = bookmark.find("label");
        if (frame == nullptr || label == nullptr || label->kind != StateNode::Kind::String)
            continue;
        Point point{static_cast<uint64_t>(frame->i),
                    tinframe != nullptr ? static_cast<uint32_t>(tinframe->i) : 0u};
        out.emplace_back(point, label->s);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

/// `steps` bookmarks on from the position (back < 0): the seek to it, or Conflict at the end of the list
ActionResult JumpBookmark(EmulatorContext* context, int64_t steps)
{
    const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
    const Point here = CursorPoint(cursor);
    const auto bookmarks = Bookmarks(context);
    if (bookmarks.empty())
        return ActionResult::Failure("Conflict", "no bookmarks in the history");
    // The bookmarks before and after the position; one the position stands on counts as neither. A seek to a
    // bookmark lands on the instruction start at or after it: a bookmark up to an instruction behind is "here"
    const uint64_t frameLength = context->config.frame != 0 ? context->config.frame : 69888;
    auto at = [frameLength](const Point& point) { return point.frame * frameLength + point.tinframe; };
    constexpr uint64_t kInstruction = 64;  // the longest instruction with its waits, rounded up
    std::vector<size_t> candidates;
    if (steps < 0)
    {
        for (size_t i = bookmarks.size(); i-- > 0;)
            if (bookmarks[i].first < here && at(here) - at(bookmarks[i].first) > kInstruction)
                candidates.push_back(i);
    }
    else
    {
        for (size_t i = 0; i < bookmarks.size(); i++)
            if (here < bookmarks[i].first)
                candidates.push_back(i);
    }
    if (candidates.empty())
        return ActionResult::Failure("Conflict", steps < 0 ? "no earlier bookmark" : "no later bookmark");
    const size_t pick = candidates[std::min<size_t>(candidates.size(), static_cast<size_t>(std::llabs(steps))) - 1];
    ttd::TTDReply reply = Ttd(context, "seek", {{"bookmark", bookmarks[pick].second}});
    if (!reply.Ok())
        return FromTtd(std::move(reply));
    reply.body["label"] = bookmarks[pick].second;
    reply.body["from"] = PointNode(here);
    StateNode marks = StateNode::Array();
    StateNode mark = StateNode::Object();
    mark["kind"] = "bookmark";
    mark["frame"] = bookmarks[pick].first.frame;
    marks.push(std::move(mark));
    reply.body["marks"] = std::move(marks);
    return ActionResult::Success(std::move(reply.body), "At bookmark " + bookmarks[pick].second);
}

/// Forward through the history by `tstates` from the position, clamped to its end (frames of config.frame t-states)
ActionResult SeekForwardTStates(EmulatorContext* context, uint64_t tstates)
{
    const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
    const uint64_t frameLength = context->config.frame != 0 ? context->config.frame : 69888;
    Point target = CursorPoint(cursor);
    uint64_t t = static_cast<uint64_t>(target.tinframe) + tstates;
    target.frame += t / frameLength;
    target.tinframe = static_cast<uint32_t>(t % frameLength);
    const Point end{cursor.end, cursor.endTinframe};
    bool edge = false;
    if (end < target)
    {
        target = end;
        edge = true;
    }
    StateNode marks = StateNode::Array();
    if (edge)
    {
        StateNode mark = StateNode::Object();
        mark["kind"] = "edge";
        mark["frame"] = end.frame;
        marks.push(std::move(mark));
    }
    if (target == CursorPoint(cursor))
    {
        StateNode body = StateNode::Object();
        body["reached"] = true;
        body["arrived_at"] = PointNode(target);
        body["halt_reason"] = "target";
        body["marks"] = std::move(marks);
        return ActionResult::Success(std::move(body), "At the end of the history");
    }
    ttd::TTDReply reply =
        Ttd(context, "seek", {{"frame", std::to_string(target.frame)}, {"tinframe", std::to_string(target.tinframe)}});
    if (!reply.Ok())
        return FromTtd(std::move(reply));
    reply.body["marks"] = std::move(marks);
    return ActionResult::Success(std::move(reply.body));
}

/// Back through the history: reverse-step by count / tstates / scanlines; an edge mark when it did not get there
ActionResult ReverseBy(EmulatorContext* context, const char* option, uint64_t amount)
{
    ttd::TTDReply reply = Ttd(context, "reverse-step", {{option, std::to_string(amount)}});
    if (!reply.Ok())
        return FromTtd(std::move(reply));
    StateNode marks = StateNode::Array();
    const StateNode* reached = reply.body.find("reached");
    if (reached != nullptr && reached->kind == StateNode::Kind::Bool && !reached->b)
    {
        StateNode mark = StateNode::Object();
        mark["kind"] = "edge";
        mark["frame"] = context->pTimeTravelHooks->PublishedCursor().first;
        marks.push(std::move(mark));
    }
    reply.body["marks"] = std::move(marks);
    return ActionResult::Success(std::move(reply.body));
}

/// The scrub unit chosen per instance
struct ScrubChoice
{
    std::string unit = "frames";
    int64_t count = 1;
};

std::mutex& ScrubMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, ScrubChoice>& ScrubChoices()
{
    static std::map<std::string, ScrubChoice> choices;
    return choices;
}

/// ttd.find_last / ttd.find_last_write: find-last at the address (or the cursor), then the seek there (seek: true)
ActionResult FindLast(const ActionCall& call, const char* access)
{
    EmulatorContext* context = call.emulator->GetContext();
    std::map<std::string, std::string> options = ParamOptions(call.value.params, {"seek", "addr", "access"});
    std::string error;
    const bool range = options.count("addr_from") != 0 || options.count("addr_to") != 0 ||
                       options.count("pc_from") != 0 || options.count("pc_to") != 0 || options.count("value") != 0;
    uint16_t address = 0;
    if (call.value.params.find("addr") != nullptr || !range)
    {
        if (!AddressOrCursor(call, {"memory", "disasm"}, address, error))
            return BadRequest(error);
        options["addr"] = std::to_string(address);
    }
    options["access"] = access != nullptr ? std::string(access) : TextParam(call.value, "access").value_or("write");
    const std::optional<bool> seek = BoolParam(call.value, "seek", error);
    if (!error.empty())
        return BadRequest(error);
    ttd::TTDReply found = Ttd(context, "find-last", options);
    if (!found.Ok())
        return FromTtd(std::move(found));
    const StateNode* hit = found.body.find("found");
    const StateNode* frame = found.body.find("frame");
    const StateNode* tinframe = found.body.find("tinframe");
    const bool ok = hit != nullptr && hit->kind == StateNode::Kind::Bool && hit->b && frame != nullptr;
    if (!ok)
        return ActionResult::Success(std::move(found.body), "No such access in the history before now");
    std::string message = "Found at frame " + std::to_string(frame->i);
    // D5.2: jump there in time (the debugger follows the machine's PC)
    if (seek.value_or(true))
    {
        ttd::TTDReply moved = Ttd(context, "seek",
                                  {{"frame", std::to_string(frame->i)},
                                   {"tinframe", std::to_string(tinframe != nullptr ? tinframe->i : 0)}});
        if (!moved.Ok())
            return FromTtd(std::move(moved));
        if (const StateNode* at = moved.body.find("arrived_at"))
            found.body["arrived_at"] = *at;
        found.body["seeked"] = true;
        message += ", the history is there now";
    }
    return ActionResult::Success(std::move(found.body), std::move(message));
}

/// The value of a run.pace_* reply, the pace state on success
ActionResult Paced(RunReply reply)
{
    return FromRun(std::move(reply));
}

std::shared_ptr<RunPacer> PacerOf(const ActionCall& call)
{
    return RunPacer::For(call.emulator);
}
}  // namespace

/// region <Scrub units>

const std::vector<std::string>& ScrubUnits()
{
    static const std::vector<std::string> units = {"frames", "seconds", "batch", "instructions",
                                                   "tstates", "scanlines", "bookmarks"};
    return units;
}

std::string ScrubUnitName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const std::string& unit : ScrubUnits())
        if (lower == unit || lower + "s" == unit)
            return unit;
    if (lower == "t" || lower == "t-states" || lower == "t-state")
        return "tstates";
    if (lower == "lines" || lower == "line")
        return "scanlines";
    return {};
}

int64_t ScrubDefaultCount(const std::string& unit)
{
    if (unit == "batch")
        return 10;
    if (unit == "tstates")
        return 100;
    return 1;
}

std::string ScrubUnitOf(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(ScrubMutex());
    auto it = ScrubChoices().find(instanceId);
    return it != ScrubChoices().end() ? it->second.unit : std::string("frames");
}

int64_t ScrubCountOf(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(ScrubMutex());
    auto it = ScrubChoices().find(instanceId);
    return it != ScrubChoices().end() ? it->second.count : 1;
}

ActionResult SeekByUnit(const ActionCall& call, const std::string& unit, int64_t count)
{
    EmulatorContext* context = call.emulator->GetContext();
    const int64_t steps = static_cast<int64_t>(std::llround(call.value.delta));
    const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
    if (!cursor.history)
        return ActionResult::Failure("not_now", "record first");
    const Point from = CursorPoint(cursor);
    ActionResult result = ActionResult::Success();
    if (steps == 0)
    {
        result.body["arrived_at"] = PointNode(from);
        result.body["marks"] = StateNode::Array();
    }
    else if (unit == "bookmarks")
        result = JumpBookmark(context, steps);
    else
    {
        const uint64_t amount = static_cast<uint64_t>(std::llabs(steps)) * static_cast<uint64_t>(std::max<int64_t>(1, count));
        if (unit == "instructions")
        {
            if (steps < 0)
                result = ReverseBy(context, "count", std::min<uint64_t>(amount, 100000));
            else
            {
                // Forward: an instruction at a time (64 at most a turn, as ttd.jog_instructions)
                const uint64_t n = std::min<uint64_t>(amount, 64);
                for (uint64_t i = 0; i < n; i++)
                {
                    result = FromTtd(Ttd(context, "step-instruction", {{"dir", "forward"}}));
                    if (!result.ok)
                        break;
                    const StateNode* stepped = result.body.find("stepped");
                    if (stepped != nullptr && stepped->kind == StateNode::Kind::Bool && !stepped->b)
                        break;  // the end of the history
                }
                if (result.ok)
                    result.body["marks"] = StateNode::Array();
            }
        }
        else if (unit == "tstates")
            result = steps < 0 ? ReverseBy(context, "tstates", amount) : SeekForwardTStates(context, amount);
        else if (unit == "scanlines")
        {
            const uint64_t line = context->config.t_line != 0 ? context->config.t_line : 224;
            result = steps < 0 ? ReverseBy(context, "scanlines", amount) : SeekForwardTStates(context, amount * line);
        }
        else
            return BadRequest("unit: frames, seconds, batch, instructions, tstates, scanlines or bookmarks");
    }
    if (!result.ok)
        return result;
    result.body["from"] = PointNode(from);
    result.body["unit"] = unit;
    result.body["count"] = count;
    result.body["steps"] = steps;
    result.body["fps"] = ttd::TTDFrameRate(context);
    return result;
}

ActionResult StepByUnit(const ActionCall& call, const std::string& unit, int64_t count)
{
    const int64_t steps = std::clamp<int64_t>(static_cast<int64_t>(std::llround(call.value.delta)), -16, 16);
    if (steps == 0)
        return ActionResult::Success();
    EmulatorContext* context = call.emulator->GetContext();
    const uint64_t amount = static_cast<uint64_t>(std::llabs(steps)) * static_cast<uint64_t>(count);
    ActionResult result = ActionResult::Success();
    if (steps > 0)
    {
        RunControl control(call.emulator);
        if (unit == "frames")
            result = FromRun(control.RunFrames(call.requester, static_cast<unsigned>(std::min<uint64_t>(amount, 10000))));
        else if (unit == "scanlines")
            result = FromRun(control.RunNScanlines(call.requester, static_cast<unsigned>(std::min<uint64_t>(amount, 1000))));
        else
            result = FromRun(control.RunTStates(call.requester, std::min<uint64_t>(amount, 10000000)));
    }
    else if (unit == "frames")
        result = MoveFrames(context, -static_cast<int64_t>(amount));
    else
        result = ReverseBy(context, unit == "scanlines" ? "scanlines" : "tstates", amount);
    if (!result.ok)
        return result;
    result.body["direction"] = steps < 0 ? "back" : "forward";
    result.body["unit"] = unit;
    result.body["steps"] = static_cast<int64_t>(amount);
    return result;
}

/// endregion </Scrub units>

StateNode PaceReadout(const std::string& instanceId)
{
    const std::shared_ptr<RunPacer> pacer = RunPacer::Find(instanceId);
    StateNode full = pacer ? pacer->StateBody() : RunPacer::DefaultStateBody();
    StateNode node = StateNode::Object();
    for (const char* key : {"preset", "label", "unit", "count", "interval_ms", "effective_ms", "running", "stop_reason"})
        if (const StateNode* value = full.find(key))
            node[key] = *value;
    return node;
}

void DropSteppingState(const std::string& instanceId)
{
    DebuggerCursors::Instance().Drop(instanceId);
    std::lock_guard<std::mutex> lock(ScrubMutex());
    ScrubChoices().erase(instanceId);
}

bool AddressParam(const StateNode& params, const char* name, uint16_t& address, std::string& error)
{
    const StateNode* node = params.find(name);
    if (node == nullptr)
    {
        error = std::string(name) + ": an address";
        return false;
    }
    int64_t value = -1;
    if (node->kind == StateNode::Kind::Int)
        value = node->i;
    else if (node->kind == StateNode::Kind::Double && node->d == std::floor(node->d))
        value = static_cast<int64_t>(node->d);
    else if (node->kind == StateNode::Kind::String && !node->s.empty())
    {
        std::string text = node->s;
        int base = 10;
        if (text.size() > 2 && (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0))
        {
            text = text.substr(2);
            base = 16;
        }
        else if (text[0] == '#' || text[0] == '$')
        {
            text = text.substr(1);
            base = 16;
        }
        else if (text.size() > 1 && (text.back() == 'h' || text.back() == 'H'))
        {
            text.pop_back();
            base = 16;
        }
        char* end = nullptr;
        const long parsed = text.empty() ? -1 : std::strtol(text.c_str(), &end, base);
        if (end != nullptr && *end == '\0')
            value = parsed;
    }
    if (value < 0 || value > 0xFFFF)
    {
        error = std::string(name) + ": an address 0..#FFFF (decimal, 0x.., #.., $.. or ..h)";
        return false;
    }
    address = static_cast<uint16_t>(value);
    return true;
}

std::vector<RegistrationHandle> RegisterSteppingActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, const char* params,
                   Executor run, ActionRules rules, bool held = false) {
        handles.push_back(
            registry.Register({id, label, group, kind, held, false, params, {}, false}, std::move(run), std::move(rules)));
    };

    // region <Run: the pace (D1)>
    const char* run = "Run";
    add("run.pace_set", "Pace", run, ValueKind::Press,
        "preset? (full|25fps|5fps|1fps|frame|100lines|10lines|1line|1op), interval_ms? (1..5000)",
        [](const ActionCall& call) {
            std::optional<size_t> preset;
            if (const std::optional<std::string> name = TextParam(call.value, "preset"))
            {
                size_t index = 0;
                if (!RunPacer::PresetIndex(*name, index))
                    return BadRequest("preset: full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line or 1op");
                preset = index;
            }
            std::string error;
            const std::optional<int64_t> interval = IntParam(call.value, "interval_ms", error);
            if (!error.empty())
                return BadRequest(error);
            if (!preset && !interval)
                return BadRequest("preset or interval_ms");
            std::optional<int> ms;
            if (interval)
                ms = static_cast<int>(std::clamp<int64_t>(*interval, -1, 100000));
            return Paced(PacerOf(call)->Set(call.requester, preset, ms));
        },
        Rules(ScopeRun));
    add("run.pace_next", "Pace finer", run, ValueKind::Press, "",
        [](const ActionCall& call) { return Paced(PacerOf(call)->Shift(call.requester, 1)); }, Rules(ScopeRun));
    add("run.pace_prev", "Pace coarser", run, ValueKind::Press, "",
        [](const ActionCall& call) { return Paced(PacerOf(call)->Shift(call.requester, -1)); }, Rules(ScopeRun));
    add("run.pace_by", "Pace on a dial", run, ValueKind::Delta, "",
        [](const ActionCall& call) {
            const int delta = static_cast<int>(std::clamp<long long>(std::llround(call.value.delta), -16, 16));
            if (delta == 0)
                return ActionResult::Success(PacerOf(call)->StateBody());
            return Paced(PacerOf(call)->Shift(call.requester, delta));
        },
        Rules(ScopeRun));
    {
        ActionRules rules = Rules(ScopeRun);
        rules.state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            const std::shared_ptr<RunPacer> pacer = RunPacer::Find(emulator.GetId());
            return pacer && pacer->State().running;
        };
        add("run.pace_toggle", "Pace run / pause", run, ValueKind::Press, "",
            [](const ActionCall& call) { return Paced(PacerOf(call)->Toggle(call.requester)); }, std::move(rules));
    }
    add("run.pace_stop", "Stop the pace", run, ValueKind::Press, "",
        [](const ActionCall& call) { return Paced(PacerOf(call)->Stop()); }, Rules(0));
    add("run.pace_step", "One step at the pace", run, ValueKind::Press, "",
        [](const ActionCall& call) { return Paced(PacerOf(call)->Step(call.requester)); }, Rules(ScopeRun));
    // endregion </Run: the pace>

    // region <Run: to a place (D2.4, D2.6, D2.7)>
    add("run.to_pixel", "Run to the first paper pixel", run, ValueKind::Press, "",
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).RunToPixel(call.requester)); },
        RouteRules("POST", "/run_to_pixel", true));
    add("run.until_pc", "Run until PC", run, ValueKind::Press,
        "pc, max_tstates? (0: 100 frames), breakpoints? (false: skipped)",
        [](const ActionCall& call) {
            uint16_t pc = 0;
            std::string error;
            if (!AddressParam(call.value.params, "pc", pc, error))
                return BadRequest(error);
            uint64_t budget = 0;
            if (!BudgetParam(call.value, budget, error))
                return BadRequest(error);
            const std::optional<bool> breakpoints = BoolParam(call.value, "breakpoints", error);
            if (!error.empty())
                return BadRequest(error);
            return FromRun(RunControl(call.emulator).RunUntilPc(call.requester, pc, budget, breakpoints.value_or(false)));
        },
        RouteRules("POST", "/skip_until", true));
    add("run.to_cursor", "Run to the cursor", run, ValueKind::Press,
        "addr? (else the disassembly's cursor), max_tstates? (0: 500 frames)",
        [](const ActionCall& call) {
            uint16_t address = 0;
            std::string error;
            if (!AddressOrCursor(call, {"disasm"}, address, error))
                return BadRequest(error);
            uint64_t budget = 0;
            if (!BudgetParam(call.value, budget, error))
                return BadRequest(error);
            if (budget == 0)
            {
                const EmulatorContext* context = call.emulator->GetContext();
                budget = static_cast<uint64_t>(context->config.frame != 0 ? context->config.frame : 69888) * 500;
            }
            // A breakpoint on the way stops it first, as a debugger's run to the cursor
            return FromRun(RunControl(call.emulator).RunUntilPc(call.requester, address, budget, true));
        },
        RouteRules("POST", "/skip_until", true));
    add("run.step_line", "Step a source line", run, ValueKind::Press, "max_tstates? (0: 100 frames)",
        [](const ActionCall& call) {
            uint64_t budget = 0;
            std::string error;
            if (!BudgetParam(call.value, budget, error))
                return BadRequest(error);
            return FromRun(RunControl(call.emulator).StepLine(call.requester, budget));
        },
        RouteRules("POST", "/listing/step_line", true));
    add("run.to_line", "Run to a source line", run, ValueKind::Press, "line, max_tstates? (0: 500 frames)",
        [](const ActionCall& call) {
            std::string error;
            const std::optional<int64_t> line = IntParam(call.value, "line", error);
            if (!error.empty() || !line)
                return BadRequest(error.empty() ? "line: a listing line number" : error);
            uint64_t budget = 0;
            if (!BudgetParam(call.value, budget, error))
                return BadRequest(error);
            return FromRun(RunControl(call.emulator)
                               .RunToLine(call.requester, static_cast<int>(std::clamp<int64_t>(*line, 0, 1 << 30)),
                                          budget));
        },
        RouteRules("POST", "/listing/run_to_line", true));
    // endregion </Run: to a place>

    // region <Time travel: units, bookmarks, find, the history's ends (D3, D5)>
    const char* ttdGroup = "Time travel";
    for (const bool back : {true, false})
        add(back ? "ttd.step_back_scanline" : "ttd.step_forward_scanline",
            back ? "Back one scanline" : "Forward one scanline", ttdGroup, ValueKind::Press, "count? (1..1000, 1)",
            [back](const ActionCall& call) {
                int64_t count = 1;
                std::string error;
                if (!CountParam(call.value, "count", 1, 1000, count, error))
                    return BadRequest(error);
                EmulatorContext* context = call.emulator->GetContext();
                if (back)
                    return ReverseBy(context, "scanlines", static_cast<uint64_t>(count));
                const uint64_t line = context->config.t_line != 0 ? context->config.t_line : 224;
                return SeekForwardTStates(context, static_cast<uint64_t>(count) * line);
            },
            TtdRules(ScopeDebug, true));
    for (const bool back : {true, false})
        add(back ? "ttd.bookmark_prev" : "ttd.bookmark_next", back ? "Previous bookmark" : "Next bookmark", ttdGroup,
            ValueKind::Press, "",
            [back](const ActionCall& call) { return JumpBookmark(call.emulator->GetContext(), back ? -1 : 1); },
            TtdRules(ScopeRewind, true));
    add("ttd.bookmarks", "Bookmarks", ttdGroup, ValueKind::Press, "",
        [](const ActionCall& call) { return FromTtd(Ttd(call.emulator->GetContext(), "bookmarks")); },
        TtdRules(0, false));
    add("ttd.find_last_write", "Last write here", ttdGroup, ValueKind::Press,
        "addr? (else the memory / disassembly cursor), seek? (true), pc_from?, pc_to?, value?, phys_page?, space?",
        [](const ActionCall& call) { return FindLast(call, "write"); }, TtdRules(ScopeDebug, true));
    add("ttd.find_last", "Last access here", ttdGroup, ValueKind::Press,
        "addr? (else the cursor), access? (write|read|execute|io), seek? (true), addr_from?, addr_to?, pc_from?, "
        "pc_to?, value?, phys_page?, space?",
        [](const ActionCall& call) { return FindLast(call, nullptr); }, TtdRules(ScopeDebug, true));
    for (const bool start : {true, false})
        add(start ? "ttd.seek_start" : "ttd.seek_end", start ? "The history's start" : "The history's end", ttdGroup,
            ValueKind::Press, "",
            [start](const ActionCall& call) {
                EmulatorContext* context = call.emulator->GetContext();
                const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
                const Point target = start ? Point{cursor.first, 0} : Point{cursor.end, cursor.endTinframe};
                return FromTtd(Ttd(context, "seek",
                                   {{"frame", std::to_string(target.frame)},
                                    {"tinframe", std::to_string(target.tinframe)}}));
            },
            TtdRules(ScopeRewind, true));
    for (const bool back : {true, false})
        add(back ? "ttd.unit_back" : "ttd.unit_forward", back ? "Back one unit" : "Forward one unit", ttdGroup,
            ValueKind::Press, "unit? (else the chosen one), count?",
            [back](const ActionCall& call) {
                ActionValue value = call.value;
                value.kind = ValueKind::Delta;
                value.delta = back ? -1 : 1;
                value.rate = 0;
                const ActionCall step{call.info, call.emulator, call.instanceId, value, call.origin, call.requester};
                return SeekBy(step);
            },
            TtdRules(ScopeRewind, true));
    {
        ActionRules rules = TtdRules(0, false);
        add("ttd.scrub_unit", "Scrub unit", ttdGroup, ValueKind::Text,
            "count? (a step: batch 10, tstates 100, else 1); the text: a unit, next or prev",
            [](const ActionCall& call) {
                std::string unit = ScrubUnitName(call.value.text);
                const std::vector<std::string>& units = ScrubUnits();
                if (call.value.text == "next" || call.value.text == "prev")
                {
                    const std::string current = ScrubUnitOf(call.instanceId);
                    const auto at = std::find(units.begin(), units.end(), current);
                    const size_t index = at != units.end() ? static_cast<size_t>(at - units.begin()) : 0;
                    const size_t size = units.size();
                    unit = units[(index + (call.value.text == "next" ? 1 : size - 1)) % size];
                }
                if (unit.empty())
                    return BadRequest("a unit: frames, seconds, batch, instructions, tstates, scanlines, bookmarks; "
                                      "or next / prev");
                int64_t count = ScrubDefaultCount(unit);
                std::string error;
                if (!CountParam(call.value, "count", count, 1000000, count, error))
                    return BadRequest(error);
                {
                    std::lock_guard<std::mutex> lock(ScrubMutex());
                    ScrubChoices()[call.instanceId] = {unit, count};
                }
                StateNode body = StateNode::Object();
                body["unit"] = unit;
                body["count"] = count;
                return ActionResult::Success(std::move(body), "Scrub in " + unit);
            },
            std::move(rules));
    }
    {
        ActionRules rules = RouteRules("POST", "/ttd/journal/build");
        rules.unavailable = NoHistory;
        add("ttd.journal_build", "Build the write journal", ttdGroup, ValueKind::Press, "from_frame?, to_frame?",
            [](const ActionCall& call) {
                return FromTtd(Ttd(call.emulator->GetContext(), "journal-build", ParamOptions(call.value.params)));
            },
            std::move(rules));
    }
    {
        ActionRules rules = RouteRules("POST", "/ttd/journal/build/cancel");
        rules.unavailable = NoTtd;
        add("ttd.journal_build_cancel", "Cancel the journal build", ttdGroup, ValueKind::Press, "",
            [](const ActionCall& call) { return FromTtd(Ttd(call.emulator->GetContext(), "journal-build-cancel")); },
            std::move(rules));
    }
    {
        ActionRules rules = RouteRules("POST", "/ttd/export-clip");
        rules.unavailable = NoHistory;
        add("ttd.export_clip", "Export a range as a clip", ttdGroup, ValueKind::Press,
            "path, from? (the history's start), to? (its end), chunk?",
            [](const ActionCall& call) {
                EmulatorContext* context = call.emulator->GetContext();
                std::map<std::string, std::string> options = ParamOptions(call.value.params);
                if (options.count("path") == 0)
                    return BadRequest("path: the clip's directory");
                const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
                if (options.count("from") == 0)
                    options["from"] = std::to_string(cursor.first);
                if (options.count("to") == 0)
                    options["to"] = std::to_string(cursor.end > cursor.first ? cursor.end - 1 : cursor.first);
                return FromTtd(Ttd(context, "export-clip", std::move(options)));
            },
            std::move(rules));
    }
    // endregion </Time travel>
    return handles;
}

}  // namespace actions
