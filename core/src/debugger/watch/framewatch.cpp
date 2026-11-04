#include "framewatch.h"

#include <algorithm>
#include <chrono>
#include <sstream>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/breakpoints/breakpointcondition.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/gameevents.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/state/statenode.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"
#include "emulator/video/screendigest.h"

namespace framewatch
{

namespace
{

BreakpointConditionContext ConditionContext(EmulatorContext* context)
{
    BreakpointConditionContext c;
    if (context && context->pCore && context->pMemory)
    {
        c.cpu = context->pCore->GetZ80();
        c.memory = context->pMemory;
        c.state = &context->emulatorState;
        c.machine = context->pConditionMachine;
    }
    return c;
}

std::string Trim(const std::string& s)
{
    const size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    const size_t last = s.find_last_not_of(' ');
    return s.substr(first, last - first + 1);
}

bool ParseUnsigned(const std::string& text, unsigned& value)
{
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    value = static_cast<unsigned>(std::stoul(text));
    return true;
}

bool ParseBool(const std::string& text, bool& value)
{
    if (text == "true" || text == "1" || text == "yes" || text == "on")
        value = true;
    else if (text == "false" || text == "0" || text == "no" || text == "off")
        value = false;
    else
        return false;
    return true;
}

const char* ScreenRuleName(ScreenRule rule)
{
    switch (rule)
    {
        case ScreenRule::Changed:
            return "changed";
        case ScreenRule::Stable:
            return "stable";
        default:
            return "";
    }
}

}  // namespace

// region <Probe>

Probe::Probe(EmulatorContext* context, Spec spec) : _context(context), _spec(std::move(spec)) {}

Probe::~Probe() = default;

std::unique_ptr<Probe> Probe::Create(EmulatorContext* context, const Spec& spec, bool dryRun, std::string& error)
{
    if (!context || !context->pCore || !context->pMemory || !context->pEmulator)
    {
        error = "the emulator is not ready";
        return nullptr;
    }
    if (spec.Empty())
    {
        error = "nothing to wait for: give if, text, screen or event";
        return nullptr;
    }
    if (spec.text.size() > kMaxTextLength)
    {
        error = "text: at most " + std::to_string(kMaxTextLength) + " characters";
        return nullptr;
    }
    if (spec.screen == ScreenRule::Stable && (spec.stableFrames < 1 || spec.stableFrames > kMaxStableFrames))
    {
        error = "stable_frames: 1.." + std::to_string(kMaxStableFrames);
        return nullptr;
    }
    std::unique_ptr<Probe> probe(new Probe(context, spec));
    if (!spec.condition.empty())
    {
        const LabelManager* labels = context->pDebugManager ? context->pDebugManager->GetLabelManager() : nullptr;
        const BreakpointConditionContext check = ConditionContext(context);
        std::string why;
        // At the frame end: no access, so no access operands (trigger 0), as a game event's frame condition
        probe->_condition = BreakpointConditionProgram::Compile(spec.condition, 0, labels, why,
                                                                BreakpointConditionDialect::Unreal,
                                                                dryRun ? &check : nullptr);
        if (!probe->_condition)
        {
            error = "if: " + why;
            return nullptr;
        }
    }
    if (!spec.event.empty())
    {
        uint64_t fires = 0;
        std::vector<std::string> names;
        if (!context->pControllerManager ||
            !context->pControllerManager->GameEvents().Fires(spec.event, fires, &names))
        {
            std::string list;
            for (const std::string& name : names)
                list += (list.empty() ? "" : ", ") + name;
            error = "event: no game event '" + spec.event + "'" +
                    (list.empty() ? std::string(" (no profile with events: is set)") : " (the profile has: " + list + ")");
            return nullptr;
        }
    }
    return probe;
}

uint64_t Probe::Digest() const
{
    // The bytes the picture is read from now (Screen::DescribeDisplayedMemory: a ZX layout is the 6,912 bytes of its
    // page, not the 16 KB page with the system variables and the stack that the ROM's interrupt changes every frame),
    // the border folded in. A device's own video memory (Sprinter VRAM) is hashed as the active-surface digest
    Screen* screen = _context->pScreen;
    Memory* memory = _context->pMemory;
    if (!screen || !memory)
        return 0;
    screen->DescribeDisplayedMemory(_spans);
    uint64_t digest = ScreenDigest::kInitialValue;
    bool device = _spans.empty();
    const uint8_t* ram = memory->RAMBase();
    const size_t ramBytes = MAX_RAM_SIZE;
    for (const ScreenMemorySpan& span : _spans)
    {
        if (span.region || span.offset >= ramBytes)
        {
            device = true;
            continue;
        }
        const size_t bytes = std::min<size_t>(span.bytes, ramBytes - span.offset);
        digest = ScreenDigest::DigestBytes(ram + span.offset, bytes, digest);
    }
    if (device)
    {
        ScreenDigestQuery query;
        query.active = true;
        digest = ScreenDigest::MixDigest(digest, ScreenDigestCompute::Compute(_context, query, false).combined);
    }
    return ScreenDigest::MixValue(digest, screen->GetBorderColor());
}

uint64_t Probe::EventFires() const
{
    uint64_t fires = 0;
    if (_context->pControllerManager)
        _context->pControllerManager->GameEvents().Fires(_spec.event, fires);
    return fires;
}

void Probe::Prime()
{
    if (_spec.screen != ScreenRule::None)
    {
        _startDigest = Digest();
        _lastDigest = _startDigest;
        _sameFrames = 0;
    }
    if (!_spec.event.empty())
        _startFires = EventFires();
    // x@prev takes its value at the start: a change within the first frame holds at its end (else the first check
    // would compare x with itself and miss it)
    if (_condition && _condition->UsesPrevious())
    {
        _condition->ResetPrevious();
        _condition->Evaluate(ConditionContext(_context));
    }
    _primed = true;
}

bool Probe::Check(Hit& hit)
{
    if (!_failure.empty())
        return false;
    if (!_primed)
        Prime();
    hit = Hit{};
    hit.frame = _context->emulatorState.frame_counter;
    unsigned given = 0;
    unsigned held = 0;

    // Every rule is evaluated each frame (not short-circuited): x@prev and the stable count must see every frame end
    if (_condition)
    {
        given++;
        std::string why;
        const BreakpointConditionResult result = _condition->Evaluate(ConditionContext(_context), &why);
        if (result == BreakpointConditionResult::Error)
        {
            _failure = "if: " + why + " (frame " + std::to_string(hit.frame) + ")";
            return false;
        }
        if (result == BreakpointConditionResult::True)
        {
            held++;
            hit.reasons.emplace_back("if");
        }
    }
    if (_spec.screen != ScreenRule::None)
    {
        given++;
        const uint64_t digest = Digest();
        _sameFrames = digest == _lastDigest ? _sameFrames + 1 : 0;
        _lastDigest = digest;
        const bool holds = _spec.screen == ScreenRule::Changed ? digest != _startDigest : _sameFrames >= _spec.stableFrames;
        if (holds)
        {
            held++;
            hit.reasons.emplace_back("screen");
            hit.stableFor = _sameFrames;
        }
    }
    if (!_spec.text.empty())
    {
        given++;
        const std::string screen = ScreenOCR::ocrScreen(_context->pEmulator);
        const size_t at = screen.find(_spec.text);
        if (at != std::string::npos)
        {
            held++;
            hit.reasons.emplace_back("text");
            const size_t lineStart = screen.rfind('\n', at);
            const size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
            const size_t lineEnd = screen.find('\n', at);
            hit.textLine = Trim(screen.substr(from, (lineEnd == std::string::npos ? screen.size() : lineEnd) - from));
        }
    }
    if (!_spec.event.empty())
    {
        given++;
        const uint64_t fires = EventFires();
        if (fires > _startFires)
        {
            held++;
            hit.reasons.emplace_back("event");
            hit.eventFires = fires - _startFires;
        }
    }
    return _spec.all ? held == given : held > 0;
}

// endregion </Probe>

// region <Manager>

struct Manager::Entry
{
    WatchInfo info;
    std::unique_ptr<Probe> probe;
};

Manager::Manager(EmulatorContext* context) : _context(context) {}

Manager::~Manager() = default;

uint32_t Manager::Add(const Spec& spec, std::string& error)
{
    // The machine may run: no dry run of the condition (a failure shows at the first frame end as the watch's error)
    std::unique_ptr<Probe> probe = Probe::Create(_context, spec, false, error);
    if (!probe)
        return 0;
    WatchInfo info;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_watches.size() >= kMaxArmed)
        {
            error = "at most " + std::to_string(kMaxArmed) + " watches";
            return 0;
        }
        auto armed = std::make_unique<Entry>();
        armed->info.id = _nextId++;
        armed->info.spec = spec;
        armed->info.armedFrame = _context->emulatorState.frame_counter;
        armed->probe = std::move(probe);
        info = armed->info;
        _watches.push_back(std::move(armed));
        _armed.store(true, std::memory_order_relaxed);
    }
    auto* payload = new FrameWatchPayload(_context->pEmulator->GetId(), "added", info.id);
    payload->label = info.spec.label;
    MessageCenter::DefaultMessageCenter().Post(NC_FRAME_WATCH, payload, true);
    return info.id;
}

bool Manager::Remove(uint32_t id)
{
    std::string label;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find_if(_watches.begin(), _watches.end(), [id](const std::unique_ptr<Entry>& w) { return w->info.id == id; });
        if (it == _watches.end())
            return false;
        label = (*it)->info.spec.label;
        _watches.erase(it);
        _armed.store(!_watches.empty(), std::memory_order_relaxed);
    }
    auto* payload = new FrameWatchPayload(_context->pEmulator->GetId(), "removed", id);
    payload->label = label;
    MessageCenter::DefaultMessageCenter().Post(NC_FRAME_WATCH, payload, true);
    return true;
}

void Manager::Clear()
{
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& w : _watches)
            ids.push_back(w->info.id);
    }
    for (uint32_t id : ids)
        Remove(id);
}

std::vector<WatchInfo> Manager::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<WatchInfo> out;
    for (const auto& w : _watches)
        out.push_back(w->info);
    return out;
}

bool Manager::LastPause(WatchInfo& info) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_hasLastPause)
        return false;
    info = _lastPause;
    return true;
}

void Manager::Publish(const WatchInfo& info, bool paused, bool removed) const
{
    const bool failed = !info.error.empty();
    auto* payload = new FrameWatchPayload(_context->pEmulator->GetId(), failed ? "error" : "hit", info.id);
    payload->label = info.spec.label;
    payload->frame = failed ? _context->emulatorState.frame_counter : info.lastHit.frame;
    payload->reasons = info.lastHit.reasons;
    payload->textLine = info.lastHit.textLine;
    payload->paused = paused;
    payload->removed = removed;
    payload->error = info.error;
    MessageCenter::DefaultMessageCenter().Post(NC_FRAME_WATCH, payload, true);
}

void Manager::OnFrameEnd(uint64_t frame)
{
    (void)frame;
    std::vector<std::pair<WatchInfo, bool>> events;  // the info, removed
    bool pause = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _watches.begin(); it != _watches.end();)
        {
            Entry& w = **it;
            if (!w.probe->Primed())
            {
                // The first frame end after arming: the baseline (the screen, the event's fires), no check yet
                w.probe->Prime();
                ++it;
                continue;
            }
            Hit hit;
            if (w.probe->Check(hit))
            {
                w.info.hits++;
                w.info.lastHit = hit;
                w.info.hasHit = true;
                pause |= w.info.spec.pause;
                const bool gone = w.info.spec.once;
                events.emplace_back(w.info, gone);
                if (gone)
                {
                    it = _watches.erase(it);
                    continue;
                }
            }
            else if (w.probe->Failed())
            {
                w.info.error = w.probe->Failure();
                events.emplace_back(w.info, true);
                it = _watches.erase(it);
                continue;
            }
            ++it;
        }
        _armed.store(!_watches.empty(), std::memory_order_relaxed);
    }
    if (events.empty())
        return;
    Emulator* emulator = _context->pEmulator;
    // A direct run (run_frames, run_until) ends paused by itself: a pause request would only queue behind it
    const bool paused = pause && emulator && emulator->IsRunning() && !emulator->IsDirectStepping();
    if (paused)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& event : events)
            {
                if (event.first.error.empty() && event.first.spec.pause)
                {
                    _lastPause = event.first;
                    _hasLastPause = true;
                    break;
                }
            }
        }
        emulator->Pause(execution::Requester{execution::Origin::Debugger, {}}, "watch");
    }
    for (const auto& [info, removed] : events)
        Publish(info, paused && info.error.empty() && info.spec.pause, removed);
    if (emulator)
        emulator->NoteDebugChange();  // debug/wait wakes
}

// endregion </Manager>

// region <Options>

bool SpecFromOptions(const std::vector<std::pair<std::string, std::string>>& options, Spec& spec, std::string& error)
{
    for (const auto& [key, value] : options)
    {
        if (key == "if" || key == "condition")
            spec.condition = value;
        else if (key == "text")
            spec.text = value;
        else if (key == "screen")
        {
            if (value == "changed" || value == "change")
                spec.screen = ScreenRule::Changed;
            else if (value == "stable" || value == "still")
                spec.screen = ScreenRule::Stable;
            else if (value.empty() || value == "none")
                spec.screen = ScreenRule::None;
            else
            {
                error = "screen: changed or stable, not '" + value + "'";
                return false;
            }
        }
        else if (key == "stable_frames")
        {
            if (!ParseUnsigned(value, spec.stableFrames) || spec.stableFrames < 1 || spec.stableFrames > kMaxStableFrames)
            {
                error = "stable_frames: 1.." + std::to_string(kMaxStableFrames);
                return false;
            }
            if (spec.screen == ScreenRule::None)
                spec.screen = ScreenRule::Stable;
        }
        else if (key == "event")
            spec.event = value;
        else if (key == "match")
        {
            if (value == "all")
                spec.all = true;
            else if (value == "any" || value.empty())
                spec.all = false;
            else
            {
                error = "match: any or all, not '" + value + "'";
                return false;
            }
        }
        else if (key == "label")
            spec.label = value;
        else if (key == "then")
        {
            if (value == "pause")
                spec.pause = true;
            else if (value == "notify")
                spec.pause = false;
            else
            {
                error = "then: pause or notify, not '" + value + "'";
                return false;
            }
        }
        else if (key == "once")
        {
            if (!ParseBool(value, spec.once))
            {
                error = "once: true or false, not '" + value + "'";
                return false;
            }
        }
        else
        {
            error = "unknown option '" + key + "' (if, text, screen, stable_frames, event, match; run_until: max_frames, "
                    "breakpoints; a watch: label, then, once)";
            return false;
        }
    }
    return true;
}

bool RunOptions(const std::vector<std::pair<std::string, std::string>>& options, Spec& spec, unsigned& maxFrames,
                bool& breakpoints, std::string& error)
{
    std::vector<std::pair<std::string, std::string>> rules;
    maxFrames = kDefaultRunFrames;
    breakpoints = false;
    for (const auto& option : options)
    {
        const std::string& key = option.first;
        if (key == "max_frames" || key == "frames")
        {
            if (!ParseUnsigned(option.second, maxFrames) || maxFrames < 1 || maxFrames > kMaxRunFrames)
            {
                error = "max_frames: 1.." + std::to_string(kMaxRunFrames);
                return false;
            }
        }
        else if (key == "breakpoints")
        {
            if (!ParseBool(option.second, breakpoints))
            {
                error = "breakpoints: true or false, not '" + option.second + "'";
                return false;
            }
        }
        else if (key == "label" || key == "then" || key == "once")
        {
            error = "'" + key + "' is for an armed watch (watch add), not run_until";
            return false;
        }
        else
            rules.push_back(option);
    }
    if (!SpecFromOptions(rules, spec, error))
        return false;
    if (spec.Empty())
    {
        error = "nothing to wait for: give if, text, screen or event";
        return false;
    }
    return true;
}

// endregion </Options>

// region <Verbs>

namespace
{
Manager* WatchesOf(EmulatorContext* context, Outcome& outcome)
{
    Manager* watches = context && context->pDebugManager ? context->pDebugManager->GetFrameWatches() : nullptr;
    if (!watches)
    {
        outcome.status = 503;
        outcome.body["error"] = "Service Unavailable";
        outcome.body["message"] = "no debugger on this emulator";
        outcome.text = "no debugger on this emulator";
    }
    return watches;
}

void Refuse(Outcome& outcome, int status, const std::string& error, const std::string& message)
{
    outcome.status = status;
    outcome.body["error"] = error;
    outcome.body["message"] = message;
    outcome.text = message;
}
}  // namespace

Outcome AddWatch(EmulatorContext* context, const std::vector<std::pair<std::string, std::string>>& options)
{
    Outcome outcome;
    Manager* watches = WatchesOf(context, outcome);
    if (!watches)
        return outcome;
    Spec spec;
    std::string error;
    if (!SpecFromOptions(options, spec, error))
    {
        Refuse(outcome, 400, "Bad Request", error);
        return outcome;
    }
    const uint32_t id = watches->Add(spec, error);
    if (id == 0)
    {
        Refuse(outcome, 400, "Bad Request", error);
        return outcome;
    }
    for (const WatchInfo& info : watches->List())
    {
        if (info.id != id)
            continue;
        outcome.status = 201;
        outcome.body["id"] = id;
        outcome.body["watch"] = WatchNode(info);
        outcome.text = "watch " + std::to_string(id) + (spec.label.empty() ? "" : " " + spec.label) + " armed: " + SpecText(spec) + " -> " +
                       (spec.pause ? "pause" : "notify") + (spec.once ? ", once" : "");
    }
    return outcome;
}

Outcome ListWatches(EmulatorContext* context)
{
    Outcome outcome;
    Manager* watches = WatchesOf(context, outcome);
    if (!watches)
        return outcome;
    const std::vector<WatchInfo> list = watches->List();
    outcome.body["count"] = static_cast<unsigned>(list.size());
    StateNode items = StateNode::Array();
    for (const WatchInfo& info : list)
        items.push(WatchNode(info));
    outcome.body["watches"] = items;
    outcome.text = WatchListText(list);
    return outcome;
}

Outcome RemoveWatch(EmulatorContext* context, const std::string& id)
{
    Outcome outcome;
    Manager* watches = WatchesOf(context, outcome);
    if (!watches)
        return outcome;
    StateNode removed = StateNode::Array();
    if (id == "all" || id == "*")
    {
        for (const WatchInfo& info : watches->List())
            if (watches->Remove(info.id))
                removed.push(info.id);
        outcome.body["removed"] = removed;
        outcome.text = std::to_string(removed.size()) + " watch(es) removed";
        return outcome;
    }
    unsigned number = 0;
    if (!ParseUnsigned(id, number) || !watches->Remove(number))
    {
        Refuse(outcome, 404, "Not Found", "no watch '" + id + "'");
        return outcome;
    }
    removed.push(number);
    outcome.body["removed"] = removed;
    outcome.text = "watch " + id + " removed";
    return outcome;
}

// endregion </Verbs>

// region <Run until>

RunResult RunUntil(EmulatorContext* context, const Spec& spec, unsigned maxFrames, bool breakpoints)
{
    RunResult result;
    if (maxFrames == 0)
        maxFrames = kDefaultRunFrames;
    if (maxFrames > kMaxRunFrames)
    {
        result.stop = "error";
        result.error = "max_frames: at most " + std::to_string(kMaxRunFrames);
        return result;
    }
    // Created (and the condition dry-run) under the run's hold of the CPU: atStart below
    std::unique_ptr<Probe> probe;
    std::string error;
    Emulator* emulator = context ? context->pEmulator : nullptr;
    if (!emulator)
    {
        result.stop = "error";
        result.error = "the emulator is not ready";
        return result;
    }
    const auto started = std::chrono::steady_clock::now();
    bool created = false;
    bool met = false;
    Hit hit;
    result.frames = emulator->RunFramesUntil(
        maxFrames,
        [&]() {
            created = true;
            probe = Probe::Create(context, spec, true, error);
            if (probe)
                probe->Prime();
            return probe != nullptr;  // a refused spec runs nothing
        },
        [&]() {
            if (probe->Check(hit))
                return true;
            return probe->Failed();
        },
        !breakpoints, met);
    result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    result.frame = context->emulatorState.frame_counter;
    result.pc = context->pCore ? context->pCore->GetZ80()->pc : 0;
    if (context->pTimeTravelHooks)
    {
        const ttd::TTDCursor cursor = context->pTimeTravelHooks->PublishedCursor();
        if (cursor.history)
            result.ttdFrame = static_cast<int64_t>(cursor.frame);
    }
    if (created && !probe)
    {
        result.stop = "error";
        result.error = error;
        return result;
    }
    if (probe && probe->Failed())
    {
        result.stop = "error";
        result.error = probe->Failure();
        return result;
    }
    if (met)
    {
        result.met = true;
        result.stop = "met";
        result.hit = hit;
    }
    else if (emulator->LastDirectRunAborted())
        result.stop = "aborted";
    else if (!created)
        result.stop = "refused";
    else if (emulator->LastDirectStop().hit)
        result.stop = "breakpoint";
    else
        result.stop = "budget";
    return result;
}

// endregion </Run until>

// region <Reports>

StateNode SpecNode(const Spec& spec)
{
    StateNode n = StateNode::Object();
    if (!spec.condition.empty())
        n["if"] = spec.condition;
    if (!spec.text.empty())
        n["text"] = spec.text;
    if (spec.screen != ScreenRule::None)
        n["screen"] = ScreenRuleName(spec.screen);
    if (spec.screen == ScreenRule::Stable)
        n["stable_frames"] = spec.stableFrames;
    if (!spec.event.empty())
        n["event"] = spec.event;
    n["match"] = spec.all ? "all" : "any";
    return n;
}

std::string SpecText(const Spec& spec)
{
    std::vector<std::string> parts;
    if (!spec.condition.empty())
        parts.push_back("if " + spec.condition);
    if (!spec.text.empty())
        parts.push_back("text \"" + spec.text + "\"");
    if (spec.screen == ScreenRule::Changed)
        parts.emplace_back("screen changed");
    if (spec.screen == ScreenRule::Stable)
        parts.push_back("screen stable " + std::to_string(spec.stableFrames) + " frames");
    if (!spec.event.empty())
        parts.push_back("event " + spec.event);
    std::string out;
    for (const std::string& part : parts)
        out += (out.empty() ? "" : (spec.all ? " and " : " or ")) + part;
    return out;
}

namespace
{

StateNode HitNode(const Hit& hit)
{
    StateNode n = StateNode::Object();
    n["frame"] = hit.frame;
    StateNode reasons = StateNode::Array();
    for (const std::string& reason : hit.reasons)
        reasons.push(reason);
    n["reasons"] = reasons;
    if (!hit.textLine.empty())
        n["text_line"] = hit.textLine;
    if (hit.stableFor > 0)
        n["stable_for"] = hit.stableFor;
    if (hit.eventFires > 0)
        n["event_fires"] = hit.eventFires;
    return n;
}

std::string HitText(const Hit& hit)
{
    std::string out;
    for (const std::string& reason : hit.reasons)
        out += (out.empty() ? "" : "+") + reason;
    if (!hit.textLine.empty())
        out += ", row \"" + hit.textLine + "\"";
    if (hit.stableFor > 0)
        out += ", still " + std::to_string(hit.stableFor) + " frames";
    if (hit.eventFires > 0)
        out += ", fired " + std::to_string(hit.eventFires) + "x";
    return out;
}

std::string Hex4(uint16_t value)
{
    static const char* digits = "0123456789ABCDEF";
    std::string s = "#0000";
    for (int i = 0; i < 4; i++)
        s[static_cast<size_t>(4 - i)] = digits[(value >> (4 * i)) & 0xF];
    return s;
}

}  // namespace

StateNode RunResultNode(const RunResult& result)
{
    StateNode n = StateNode::Object();
    n["met"] = result.met;
    n["outcome"] = result.stop;
    if (!result.error.empty())
        n["error"] = result.error;
    n["frames"] = result.frames;
    n["frame"] = result.frame;
    n["pc"] = static_cast<unsigned>(result.pc);
    if (result.ttdFrame >= 0)
        n["ttd_frame"] = result.ttdFrame;
    n["ms"] = static_cast<int64_t>(result.ms + 0.5);
    if (result.met)
        n["hit"] = HitNode(result.hit);
    return n;
}

std::string RunResultText(const RunResult& result)
{
    std::ostringstream out;
    if (result.stop == "error")
        return "run_until refused: " + result.error;
    if (result.stop == "refused")
        return "run_until did not get the CPU";
    if (result.met)
        out << "met at frame " << result.frame << " after " << result.frames << " frames (" << HitText(result.hit) << ")";
    else if (result.stop == "budget")
        out << "not met in " << result.frames << " frames (now frame " << result.frame << ")";
    else if (result.stop == "breakpoint")
        out << "stopped by a breakpoint after " << result.frames << " frames (frame " << result.frame << ")";
    else
        out << "aborted after " << result.frames << " frames: the user took control back";
    out << ", PC " << Hex4(result.pc);
    if (result.ttdFrame >= 0)
        out << ", TTD frame " << result.ttdFrame;
    out << ", " << static_cast<int64_t>(result.ms + 0.5) << " ms; paused";
    return out.str();
}

StateNode WatchNode(const WatchInfo& info)
{
    StateNode n = StateNode::Object();
    n["id"] = info.id;
    if (!info.spec.label.empty())
        n["label"] = info.spec.label;
    n["spec"] = SpecNode(info.spec);
    n["then"] = info.spec.pause ? "pause" : "notify";
    n["once"] = info.spec.once;
    n["armed_frame"] = info.armedFrame;
    n["hits"] = info.hits;
    if (info.hasHit)
        n["last_hit"] = HitNode(info.lastHit);
    if (!info.error.empty())
        n["error"] = info.error;
    return n;
}

std::string WatchListText(const std::vector<WatchInfo>& watches)
{
    if (watches.empty())
        return "no watches";
    std::ostringstream out;
    out << watches.size() << " watch(es)";
    for (const WatchInfo& w : watches)
    {
        out << "\n  " << w.id;
        if (!w.spec.label.empty())
            out << " " << w.spec.label;
        out << ": " << SpecText(w.spec) << " -> " << (w.spec.pause ? "pause" : "notify") << (w.spec.once ? ", once" : "")
            << "; armed at frame " << w.armedFrame << ", " << w.hits << " hit(s)";
        if (w.hasHit)
            out << ", last frame " << w.lastHit.frame << " (" << HitText(w.lastHit) << ")";
    }
    return out.str();
}

// endregion </Reports>

}  // namespace framewatch
