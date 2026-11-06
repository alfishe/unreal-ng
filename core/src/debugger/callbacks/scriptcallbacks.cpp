#include "scriptcallbacks.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/chipregister.h"
#include "debugger/callbacks/scripthookhost.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"
#include "emulator/sound/soundmanager.h"

namespace
{
ScriptHookHostFactory g_hookHostFactory = nullptr;
}  // namespace

void SetScriptHookHostFactory(ScriptHookHostFactory factory)
{
    g_hookHostFactory = factory;
}

ScriptHookHostFactory GetScriptHookHostFactory()
{
    return g_hookHostFactory;
}

struct ScriptCallbackManager::Entry
{
    uint32_t id = 0;
    CallbackSpec spec;
    uint16_t tapId = BRK_INVALID;  ///< Tap: the hidden breakpoint
    std::string target;
    std::atomic<CallbackState> state{CallbackState::Active};
    std::atomic<bool> armed{true};  ///< its tap or its counter is in place (Unarm once)

    // The ring: one producer (the emulation thread), one consumer (the script's thread)
    std::vector<CallbackHit> ring;
    uint32_t mask = 0;
    std::atomic<uint32_t> head{0};  ///< written by the producer
    std::atomic<uint32_t> tail{0};  ///< written by the consumer
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> dropped{0};

    // The consumer's side, under the manager's mutex
    CallbackStats stats;
    std::array<uint64_t, 32> histogram{};  ///< calls by the bit length of their µs
    std::vector<uint64_t> overrunFrames;   ///< the frames of the recent overruns
    std::string reason;
};

/// The emulation thread's view of the frame (OnFrameEnd)
struct ScriptCallbackManager::FrameState
{
    uint64_t hookUs = 0;     ///< the hooks' time in this frame
    uint64_t unpacedUs = 0;  ///< of it, the unpaced callbacks'
    uint64_t lastEndUs = 0;
    unsigned slowFrames = 0;
    unsigned calmFrames = 0;
    SoundManager::HostOutputHold hold;
};

namespace
{
uint32_t RingSize(uint32_t wanted)
{
    wanted = std::clamp(wanted, ScriptCallbackManager::kMinRing, ScriptCallbackManager::kMaxRing);
    uint32_t size = 1;
    while (size < wanted)
        size <<= 1;
    return size;
}

std::string Hex(unsigned value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%04X", value & 0xFFFF);
    return text;
}

std::string TapTarget(const BreakpointSpec& tap)
{
    std::string text;
    if (tap.type == BRK_EVENT)
        text = std::string("event ") + GSCardEventName(static_cast<GSCardEvent>(tap.address));
    else if (tap.type == BRK_CHIPREG)
    {
        text = std::string("chip ") + SoundChipKindName(static_cast<SoundChipKind>(tap.chip));
        if (tap.chipUnit != kSoundChipAnyUnit)
            text += " unit " + std::to_string(tap.chipUnit);
        text += " reg " + Hex(tap.address);
    }
    else if (tap.type == BRK_IO)
        text = std::string(tap.access & BRK_IO_IN ? "in" : "") + (tap.access & BRK_IO_OUT ? "out" : "") + " " + Hex(tap.address);
    else
        text = std::string(tap.access & BRK_MEM_EXECUTE ? "exec" : "") + (tap.access & BRK_MEM_READ ? "read" : "") +
               (tap.access & BRK_MEM_WRITE ? "write" : "") + " " + Hex(tap.address);
    if (tap.hasEnd && tap.addressEnd != tap.address)
        text += "-" + Hex(tap.addressEnd);
    if (!tap.condition.empty())
        text += " if " + tap.condition;
    return text;
}

/// The percentile of a histogram of bit lengths: the upper edge of the bucket that holds it
uint32_t Percentile(const std::array<uint64_t, 32>& histogram, uint64_t calls, double fraction)
{
    if (calls == 0)
        return 0;
    const uint64_t rank = static_cast<uint64_t>(static_cast<double>(calls) * fraction);
    uint64_t seen = 0;
    for (size_t bucket = 0; bucket < histogram.size(); bucket++)
    {
        seen += histogram[bucket];
        if (seen > rank)
            return bucket == 0 ? 0 : static_cast<uint32_t>((1ull << bucket) - 1);
    }
    return 0xFFFF'FFFFu;
}
}  // namespace

const char* CallbackTriggerName(CallbackTrigger trigger)
{
    switch (trigger)
    {
        case CallbackTrigger::Tap: return "tap";
        case CallbackTrigger::Breakpoint: return "breakpoint";
        case CallbackTrigger::GameEvent: return "game_event";
    }
    return "tap";
}

const char* CallbackHitKindName(uint8_t kind)
{
    switch (kind)
    {
        case BRK_TAP_EXEC: return "exec";
        case BRK_TAP_READ: return "read";
        case BRK_TAP_WRITE: return "write";
        case BRK_TAP_PORT_IN: return "port_in";
        case BRK_TAP_PORT_OUT: return "port_out";
        case BRK_TAP_EVENT: return "card_event";
        case BRK_TAP_MACHINE_EVENT: return "machine_event";
        case BRK_TAP_CHIPREG: return "chip_register";
        case CallbackHit::kKindStop: return "stop";
        case CallbackHit::kKindEvent: return "event";
        default: return "exec";
    }
}

const char* CallbackModeName(CallbackMode mode)
{
    switch (mode)
    {
        case CallbackMode::Deferred: return "deferred";
        case CallbackMode::Frame: return "frame";
        case CallbackMode::Sync: return "sync";
    }
    return "deferred";
}

bool ParseCallbackMode(const std::string& text, CallbackMode& mode)
{
    if (text == "deferred")
        mode = CallbackMode::Deferred;
    else if (text == "frame")
        mode = CallbackMode::Frame;
    else if (text == "sync")
        mode = CallbackMode::Sync;
    else
        return false;
    return true;
}

const char* CallbackRoleName(CallbackRole role)
{
    return role == CallbackRole::Logic ? "logic" : "view";
}

bool ParseCallbackRole(const std::string& text, CallbackRole& role)
{
    if (text == "view")
        role = CallbackRole::View;
    else if (text == "logic")
        role = CallbackRole::Logic;
    else
        return false;
    return true;
}

const char* CallbackPolicyName(CallbackPolicy policy)
{
    switch (policy)
    {
        case CallbackPolicy::Suspend: return "suspend";
        case CallbackPolicy::Disable: return "disable";
        case CallbackPolicy::Ignore: return "ignore";
        case CallbackPolicy::Unpaced: return "unpaced";
    }
    return "suspend";
}

const char* CallbackStateName(CallbackState state)
{
    switch (state)
    {
        case CallbackState::Active: return "active";
        case CallbackState::Suspended: return "suspended";
        case CallbackState::Disabled: return "disabled";
    }
    return "active";
}

bool ParseCallbackPolicy(const std::string& text, CallbackPolicy& policy)
{
    if (text == "suspend")
        policy = CallbackPolicy::Suspend;
    else if (text == "disable")
        policy = CallbackPolicy::Disable;
    else if (text == "ignore")
        policy = CallbackPolicy::Ignore;
    else if (text == "unpaced")
        policy = CallbackPolicy::Unpaced;
    else
        return false;
    return true;
}

ScriptCallbackManager::ScriptCallbackManager(EmulatorContext* context)
    : _context(context), _frame(std::make_unique<FrameState>())
{
}

ScriptCallbackManager::~ScriptCallbackManager()
{
    // Unregistering the sink removes its taps; the breakpoint managers outlive this (DebugManager's order)
    for (DebugCpuId cpu : {DebugCpuId::Main, DebugCpuId::GS, DebugCpuId::MultiSoundGS})
        if (BreakpointManager* breakpoints = Breakpoints(cpu);
            breakpoints != nullptr && _sinks[static_cast<size_t>(cpu)] != BRK_NO_TAP)
            breakpoints->UnregisterTapSink(_sinks[static_cast<size_t>(cpu)]);
    for (auto& slot : _slots)
        slot.reset();
    _frame->hold.Release();
    if (_context != nullptr)
        _context->SetStepWork(EmulatorContext::kStepWorkScriptEdits, false);
    _host.reset();
}

BreakpointManager* ScriptCallbackManager::Breakpoints(DebugCpuId cpu) const
{
    if (_context == nullptr || _context->pDebugManager == nullptr)
        return nullptr;
    return _context->pDebugManager->GetBreakpointsManager(cpu);
}

uint32_t ScriptCallbackManager::Register(const CallbackSpec& spec, std::string& error)
{
    auto entry = std::make_shared<Entry>();
    entry->spec = spec;
    const uint32_t size = RingSize(spec.ring);
    entry->ring.resize(size);
    entry->mask = size - 1;
    entry->spec.ring = size;
    if (entry->spec.budgetUs == 0)
        entry->spec.budgetUs = 1;
    const bool hook = spec.mode != CallbackMode::Deferred;
    if (hook && entry->spec.instructions == 0)
        entry->spec.instructions = kDefaultInstructions;
    if (!hook && spec.policy == CallbackPolicy::Unpaced)
    {
        error = "the unpaced policy is for frame and sync callbacks";
        return 0;
    }
    if (!hook && spec.role == CallbackRole::Logic)
    {
        error = "the logic role is for frame and sync callbacks (a deferred callback's writes are tool edits)";
        return 0;
    }
    if (hook && spec.chunk.empty())
    {
        error = "a frame or sync callback is Lua source that returns its function";
        return 0;
    }

    const bool card = spec.trigger == CallbackTrigger::Tap && spec.cpu != DebugCpuId::Main;
    if (!card)
        entry->spec.cpu = DebugCpuId::Main;
    BreakpointManager* breakpoints = Breakpoints(card ? spec.cpu : DebugCpuId::Main);
    // The hit's `event`: the event's name, the chip's
    if (spec.tap.type == BRK_EVENT)
        entry->spec.event = GSCardEventName(static_cast<GSCardEvent>(spec.tap.address));
    else if (spec.tap.type == BRK_CHIPREG)
        entry->spec.event = SoundChipKindName(static_cast<SoundChipKind>(spec.tap.chip));
    size_t slot = kMaxCallbacks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t index = 0; index < _slots.size(); index++)
            if (!_slots[index])
            {
                slot = index;
                break;
            }
        if (slot == kMaxCallbacks)
        {
            error = "at most " + std::to_string(kMaxCallbacks) + " callbacks";
            return 0;
        }
        switch (spec.trigger)
        {
            case CallbackTrigger::Tap:
                if (spec.tap.type != BRK_MEMORY && spec.tap.type != BRK_IO && spec.tap.type != BRK_EVENT &&
                    !(!card && spec.tap.type == BRK_CHIPREG))
                {
                    error = card ? "a card tap callback watches memory, ports or card events"
                                 : "a tap callback watches memory, ports, machine events or chip registers";
                    return 0;
                }
                if (spec.tap.type == BRK_EVENT &&
                    GSCardEventIsHostSide(static_cast<GSCardEvent>(spec.tap.address)) == card)
                {
                    error = card ? "a card tap callback watches the card's events (int, nmi, reset, page, ...)"
                                 : "a main CPU tap callback watches the machine's events (cpu_int, fdc_command, ...)";
                    return 0;
                }
                if (breakpoints == nullptr)
                {
                    error = "no debugger on this machine";
                    return 0;
                }
                entry->target = "tap " + TapTarget(spec.tap);
                if (card)
                    entry->target += std::string(" (") + DebugCpuName(spec.cpu) + " CPU)";
                break;
            case CallbackTrigger::Breakpoint:
            {
                if (breakpoints == nullptr || !breakpoints->GetBreakpointById(spec.breakpointId))
                {
                    error = "no breakpoint " + std::to_string(spec.breakpointId);
                    return 0;
                }
                entry->target = "breakpoint " + std::to_string(spec.breakpointId);
                break;
            }
            case CallbackTrigger::GameEvent:
                if (spec.event.empty())
                {
                    error = "a game event callback names its event";
                    return 0;
                }
                entry->target = "event " + spec.event;
                break;
        }
        if (hook && !_host)
        {
            if (const ScriptHookHostFactory factory = GetScriptHookHostFactory())
                _host = factory(_context);
            if (!_host)
            {
                error = "frame and sync callbacks need the Lua hook state (Lua automation is not built in)";
                return 0;
            }
        }
        entry->id = _nextId++;
        // Compiled before the slot is filled, so no hit runs a callback without its function. Under the lock (the
        // slot is not taken yet); a hook's call never takes it, so this waits at most for one running call
        if (hook && !_host->Compile(entry->id, spec.chunk, error))
        {
            error = "hook: " + error;
            return 0;
        }
        _slots[slot] = entry;  // before the tap is armed: its first hit finds it
    }
    if (hook)
        _hookCallbacks.fetch_add(1, std::memory_order_acq_rel);

    if (spec.trigger == CallbackTrigger::Tap)
    {
        uint8_t& sink = _sinks[static_cast<size_t>(card ? spec.cpu : DebugCpuId::Main)];
        if (sink == BRK_NO_TAP)
            sink = breakpoints->RegisterTapSink(this);
        BreakpointSpec tap = spec.tap;
        tap.owner = kOwnerPrefix + std::to_string(entry->id);
        tap.note = spec.label.empty() ? "script callback" : spec.label;
        tap.hidden = true;
        tap.shared = false;
        tap.tapSink = sink;
        tap.tapCookie = static_cast<uint32_t>(slot);
        const uint16_t id = sink == BRK_NO_TAP ? BRK_INVALID : breakpoints->AddBreakpoint(tap, error);
        if (id == BRK_INVALID)
        {
            if (sink == BRK_NO_TAP)
                error = "no tap sink left";
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _slots[slot].reset();
            }
            if (hook)
            {
                _host->Forget(entry->id);
                _hookCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            }
            return 0;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        entry->tapId = id;
    }
    else if (spec.trigger == CallbackTrigger::Breakpoint)
        _breakpointCallbacks.fetch_add(1, std::memory_order_acq_rel);
    else
        _gameEventCallbacks.fetch_add(1, std::memory_order_acq_rel);
    Publish(entry->id);
    return entry->id;
}

void ScriptCallbackManager::Unarm(Entry& entry)
{
    if (!entry.armed.exchange(false, std::memory_order_acq_rel))
        return;
    if (entry.spec.trigger == CallbackTrigger::Tap)
    {
        if (entry.tapId != BRK_INVALID)
            if (BreakpointManager* breakpoints = Breakpoints(entry.spec.cpu))
                breakpoints->RemoveBreakpointByID(entry.tapId);
        entry.tapId = BRK_INVALID;
    }
    else if (entry.spec.trigger == CallbackTrigger::Breakpoint)
        _breakpointCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    else
        _gameEventCallbacks.fetch_sub(1, std::memory_order_acq_rel);
}

bool ScriptCallbackManager::Remove(uint32_t id)
{
    std::shared_ptr<Entry> entry;
    size_t slot = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (; slot < _slots.size(); slot++)
            if (_slots[slot] && _slots[slot]->id == id)
                break;
        if (slot == _slots.size())
            return false;
        entry = _slots[slot];
    }
    // The tap goes first (with the CPU held), so no hit is in flight when the slot is freed
    Unarm(*entry);
    entry->state.store(CallbackState::Disabled);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _slots[slot].reset();
    }
    if (entry->spec.mode != CallbackMode::Deferred)
    {
        // A frame or event hit already taken holds the entry, not the function: Forget waits for a running call
        _host->Forget(id);
        _hookCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    Publish(id);
    return true;
}

void ScriptCallbackManager::RemoveOwner(const std::string& owner)
{
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _slots)
            if (entry && entry->spec.owner == owner)
                ids.push_back(entry->id);
    }
    for (uint32_t id : ids)
        Remove(id);
}

void ScriptCallbackManager::RemoveAll()
{
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _slots)
            if (entry)
                ids.push_back(entry->id);
    }
    for (uint32_t id : ids)
        Remove(id);
}

bool ScriptCallbackManager::Resume(uint32_t id)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = std::find_if(_slots.begin(), _slots.end(), [id](const auto& entry) { return entry && entry->id == id; });
        if (found == _slots.end() || (*found)->state.load() == CallbackState::Disabled)
            return false;
        Entry& entry = **found;
        entry.stats = CallbackStats{};
        entry.histogram.fill(0);
        entry.overrunFrames.clear();
        entry.reason.clear();
        entry.hits.store(0);
        entry.dropped.store(0);
        entry.state.store(CallbackState::Active, std::memory_order_release);
    }
    Publish(id);
    return true;
}

bool ScriptCallbackManager::SetPolicy(uint32_t id, CallbackPolicy policy)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = std::find_if(_slots.begin(), _slots.end(), [id](const auto& entry) { return entry && entry->id == id; });
        if (found == _slots.end())
            return false;
        if (policy == CallbackPolicy::Unpaced && (*found)->spec.mode == CallbackMode::Deferred)
            return false;
        (*found)->spec.policy = policy;
    }
    Publish(id);
    return true;
}

std::vector<CallbackInfo> ScriptCallbackManager::List() const
{
    std::vector<CallbackInfo> list;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& entry : _slots)
    {
        if (!entry)
            continue;
        CallbackInfo info;
        info.id = entry->id;
        info.trigger = entry->spec.trigger;
        info.owner = entry->spec.owner;
        info.label = entry->spec.label;
        info.target = entry->target;
        info.cpu = entry->spec.trigger == CallbackTrigger::Tap ? entry->spec.cpu : DebugCpuId::Main;
        info.breakpointId = entry->spec.trigger == CallbackTrigger::Tap ? entry->tapId : entry->spec.breakpointId;
        info.event = entry->spec.event;
        info.mode = entry->spec.mode;
        info.policy = entry->spec.policy;
        info.state = entry->state.load(std::memory_order_acquire);
        info.reason = entry->reason;
        info.budgetUs = entry->spec.budgetUs;
        info.instructions = entry->spec.mode != CallbackMode::Deferred ? entry->spec.instructions : 0;
        info.role = entry->spec.role;
        info.ring = entry->spec.ring;
        info.pending = entry->head.load(std::memory_order_acquire) - entry->tail.load(std::memory_order_acquire);
        info.stats = entry->stats;
        info.stats.hits = entry->hits.load(std::memory_order_relaxed);
        info.stats.dropped = entry->dropped.load(std::memory_order_relaxed);
        info.stats.p99Us = Percentile(entry->histogram, entry->stats.calls, 0.99);
        list.push_back(std::move(info));
    }
    std::sort(list.begin(), list.end(), [](const CallbackInfo& a, const CallbackInfo& b) { return a.id < b.id; });
    return list;
}

bool ScriptCallbackManager::Get(uint32_t id, CallbackInfo& info) const
{
    for (CallbackInfo& item : List())
        if (item.id == id)
        {
            info = std::move(item);
            return true;
        }
    return false;
}

void ScriptCallbackManager::SetWake(const std::string& owner, std::function<void()> wake)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = std::find_if(_wakes.begin(), _wakes.end(), [&](const auto& item) { return item.first == owner; });
    if (!wake)
    {
        if (found != _wakes.end())
            _wakes.erase(found);
        return;
    }
    if (found != _wakes.end())
        found->second = std::move(wake);
    else
        _wakes.emplace_back(owner, std::move(wake));
}

size_t ScriptCallbackManager::Drain(uint32_t id, std::vector<CallbackHit>& out, size_t max)
{
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& item : _slots)
            if (item && item->id == id)
                entry = item;
    }
    if (!entry)
        return 0;
    uint32_t tail = entry->tail.load(std::memory_order_relaxed);
    const uint32_t head = entry->head.load(std::memory_order_acquire);
    size_t taken = 0;
    while (tail != head && taken < max)
    {
        out.push_back(entry->ring[tail & entry->mask]);
        tail++;
        taken++;
    }
    entry->tail.store(tail, std::memory_order_release);
    return taken;
}

std::vector<uint32_t> ScriptCallbackManager::Pending(const std::string& owner) const
{
    std::vector<uint32_t> ids;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& entry : _slots)
        if (entry && entry->spec.owner == owner &&
            entry->head.load(std::memory_order_acquire) != entry->tail.load(std::memory_order_acquire))
            ids.push_back(entry->id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ScriptCallbackManager::Report(uint32_t id, uint32_t us, bool failed, const std::string& error)
{
    Account(id, us, failed, error, false, true);
}

void ScriptCallbackManager::Account(uint32_t id, uint32_t us, bool failed, const std::string& error, bool aborted,
                                    bool mayUnarm)
{
    bool changed = false;
    std::shared_ptr<Entry> disable;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = std::find_if(_slots.begin(), _slots.end(), [id](const auto& entry) { return entry && entry->id == id; });
        if (found == _slots.end())
            return;
        Entry& entry = **found;
        CallbackStats& stats = entry.stats;
        stats.calls++;
        stats.totalUs += us;
        stats.maxUs = std::max(stats.maxUs, us);
        size_t bucket = 0;
        while (bucket + 1 < entry.histogram.size() && (us >> bucket) != 0)
            bucket++;
        entry.histogram[bucket]++;
        if (failed)
        {
            stats.errors++;
            stats.lastError = error;
        }
        if (aborted)
            stats.aborted++;
        const bool over = us > entry.spec.budgetUs;
        if ((!over && !aborted) || entry.state.load() != CallbackState::Active)
            return;
        const uint64_t frame = _context != nullptr ? _context->emulatorState.frame_counter : 0;
        if (over)
        {
            stats.overruns++;
            stats.lastOverrunFrame = frame;
            stats.lastOverrunUs = us;
        }
        // Unpaced: the emulator slows down instead (OnFrameEnd)
        if (entry.spec.policy == CallbackPolicy::Ignore || entry.spec.policy == CallbackPolicy::Unpaced)
            return;
        if (aborted)
            entry.reason = "a call ran out of its " + std::to_string(entry.spec.instructions) + " instructions";
        else
        {
            auto& recent = entry.overrunFrames;
            recent.push_back(frame);
            recent.erase(std::remove_if(recent.begin(), recent.end(),
                                        [frame](uint64_t at) { return at + kOverrunWindow < frame; }),
                         recent.end());
            const bool tooLong = static_cast<uint64_t>(us) > static_cast<uint64_t>(entry.spec.budgetUs) * kOverrunFactor;
            if (!tooLong && recent.size() < kOverrunsToSuspend)
                return;
            entry.reason = tooLong ? "one call took " + std::to_string(us) + " us (budget " +
                                     std::to_string(entry.spec.budgetUs) + ")"
                               : std::to_string(recent.size()) + " calls over " + std::to_string(entry.spec.budgetUs) +
                                     " us within " + std::to_string(kOverrunWindow) + " frames";
        }
        if (entry.spec.policy == CallbackPolicy::Suspend)
            entry.state.store(CallbackState::Suspended, std::memory_order_release);
        else
            disable = *found;
        changed = true;
    }
    if (disable)
    {
        // A tap removed from the emulation thread would wait for the CPU it runs on: it stays armed, and silent
        if (mayUnarm || disable->spec.trigger != CallbackTrigger::Tap)
            Unarm(*disable);
        disable->state.store(CallbackState::Disabled, std::memory_order_release);
    }
    if (changed)
        Publish(id);
}

CallbackValue ScriptCallbackManager::SharedGet(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(_sharedMutex);
    auto found = _shared.find(key);
    return found != _shared.end() ? found->second : CallbackValue{};
}

void ScriptCallbackManager::SharedSet(const std::string& key, const CallbackValue& value)
{
    std::lock_guard<std::mutex> lock(_sharedMutex);
    if (value.kind == CallbackValue::Kind::Nil)
        _shared.erase(key);
    else
        _shared[key] = value;
}

CallbackHookStatus ScriptCallbackManager::HookStatus() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    CallbackHookStatus status = _status;
    status.unpaced = _unpaced.load(std::memory_order_acquire);
    return status;
}

void ScriptCallbackManager::RunHook(Entry& entry, const CallbackHit& hit, uint8_t* replace)
{
    if (entry.state.load(std::memory_order_acquire) != CallbackState::Active || !_host)
        return;
    const bool logic = entry.spec.role == CallbackRole::Logic;
    // A card's writes, of either role, wait for its next instruction boundary (GSCardTarget)
    GSCardTarget* card = _context != nullptr && _context->pDebugManager != nullptr
                             ? _context->pDebugManager->GetCardTarget(static_cast<DebugCpuId>(hit.cpu))
                             : nullptr;
    HookEdits edits;
    edits.view = !logic;
    const auto start = std::chrono::steady_clock::now();
    HookCallResult result = _host->Call(entry.id, hit, entry.spec.event, entry.spec.instructions,
                                        logic || card != nullptr ? &edits : nullptr);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    const auto us = static_cast<uint32_t>(std::clamp<int64_t>(elapsed, 0, 0xFFFF'FFFF));
    _frame->hookUs += us;
    if (entry.spec.policy == CallbackPolicy::Unpaced)
        _frame->unpacedUs += us;
    // A card read's byte: the one the function returned (design §8A.7). A recording keeps a logic hook's byte as an
    // input of the track; a replay applies the recorded one (GSCardTarget), never the hook's
    const ttd::ITimeTravelHooks* ttd = _context != nullptr ? _context->pTimeTravelHooks : nullptr;
    if (replace != nullptr && result.ok && result.returned >= 0)
    {
        if (result.returned > 0xFF)
        {
            result.ok = false;
            result.error = "a card read's callback returns a byte (0..255) or nothing";
        }
        else if (ttd != nullptr && ttd->IsReplayActive())
        {
            // the track's byte is the one this read sees
        }
        else if (const std::string refusal = EditRefusal(_context, entry.spec.role); !refusal.empty())
        {
            result.ok = false;
            result.error = "the byte a card read sees: " + refusal;
        }
        else
        {
            *replace = static_cast<uint8_t>(result.returned);
            if (card != nullptr && ttd != nullptr && ttd->IsRecording())
                card->RecordReadEdit(entry.id, hit.kind == BRK_TAP_PORT_IN, hit.address, *replace);
        }
    }
    // A call that failed leaves no writes: a half-done edit would be worse than none
    if (card != nullptr && result.ok && !edits.Empty())
        card->QueueHookEdits(entry.id, !logic, std::move(edits));
    else if (logic && result.ok && !edits.Empty())
    {
        _edits.emplace_back(entry.id, std::move(edits));
        if (_context != nullptr)
            _context->SetStepWork(EmulatorContext::kStepWorkScriptEdits, true);
    }
    Account(entry.id, us, !result.ok, result.error, result.aborted, false);
}

std::string ScriptCallbackManager::EditRefusal(const EmulatorContext* context, CallbackRole role)
{
    const ttd::ITimeTravelHooks* ttd = context != nullptr ? context->pTimeTravelHooks : nullptr;
    if (ttd == nullptr || ttd->GetState() == ttd::TTDSessionState::Idle)
        return {};
    if (role == CallbackRole::View)
        return "a view callback does not change the machine while time travel records or browses (role logic does)";
    if (ttd->GetState() != ttd::TTDSessionState::Recording)
        return "a logic callback does not change the machine while time travel browses";
    if (!ttd->RecordsScriptEdits())
        return "this time travel backend does not record a logic callback's writes";
    return {};
}

void ScriptCallbackManager::ApplyEdits()
{
    if (_context != nullptr)
        _context->SetStepWork(EmulatorContext::kStepWorkScriptEdits, false);
    std::vector<std::pair<uint32_t, HookEdits>> edits;
    edits.swap(_edits);
    if (_context == nullptr || edits.empty())
        return;
    ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks;
    // A replay applies what the recording kept; a session that opened since the call takes no new writes
    if ((ttd != nullptr && ttd->IsReplayActive()) || !EditRefusal(_context, CallbackRole::Logic).empty())
        return;
    const bool record = ttd != nullptr && ttd->IsRecording();
    Z80* z80 = _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
    for (const auto& [id, edit] : edits)
    {
        if (_context->pMemory != nullptr)
            for (const auto& [address, value] : edit.pokes)
                _context->pMemory->ToolWriteToZ80Memory(address, value);
        if (z80 != nullptr)
            for (const auto& [name, value] : edit.regs)
                Z80::SetRegisterValue(z80, name, value);
        if (record)
        {
            const std::string source = "callback " + std::to_string(id);
            ttd->RecordScriptEdit(source.c_str(), edit.pokes, !edit.regs.empty());
        }
    }
}

void ScriptCallbackManager::SetUnpaced(bool unpaced)
{
    if (_unpaced.load(std::memory_order_acquire) == unpaced)
        return;
    if (unpaced)
    {
        _frame->hold = SoundManager::HostOutputHold(_context != nullptr ? _context->pSoundManager : nullptr,
                                                    SoundManager::HostHoldReason::Unpaced);
        std::lock_guard<std::mutex> lock(_mutex);
        _status.unpacedSpans++;
    }
    else
        _frame->hold.Release();
    _unpaced.store(unpaced, std::memory_order_release);
    _frame->slowFrames = 0;
    _frame->calmFrames = 0;
    Publish(0);  // the manager's state, not one callback's
}

void ScriptCallbackManager::OnFrameEnd(uint64_t nowUs, uint32_t periodUs)
{
    std::vector<std::shared_ptr<Entry>> frameCallbacks;
    bool unpacedCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _slots)
        {
            if (!entry || entry->spec.mode == CallbackMode::Deferred)
                continue;
            if (entry->spec.mode == CallbackMode::Frame)
                frameCallbacks.push_back(entry);
            if (entry->spec.policy == CallbackPolicy::Unpaced && entry->state.load() == CallbackState::Active)
                unpacedCallbacks = true;
        }
    }
    // The frame's hits, oldest first; a callback suspended on the way drops the rest of its ring
    for (const auto& entry : frameCallbacks)
    {
        uint32_t tail = entry->tail.load(std::memory_order_relaxed);
        const uint32_t head = entry->head.load(std::memory_order_acquire);
        while (tail != head)
        {
            const CallbackHit hit = entry->ring[tail & entry->mask];
            tail++;
            entry->tail.store(tail, std::memory_order_release);
            RunHook(*entry, hit);
        }
    }

    FrameState& frame = *_frame;
    const uint64_t hookUs = frame.hookUs;
    const uint64_t unpacedUs = frame.unpacedUs;
    frame.hookUs = 0;
    frame.unpacedUs = 0;
    const uint64_t wallUs = frame.lastEndUs != 0 && nowUs > frame.lastEndUs ? nowUs - frame.lastEndUs : 0;
    frame.lastEndUs = nowUs;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status.lastFrameUs = static_cast<uint32_t>(std::min<uint64_t>(hookUs, 0xFFFF'FFFF));
        _status.maxFrameUs = std::max(_status.maxFrameUs, _status.lastFrameUs);
        if (hookUs * 100 > static_cast<uint64_t>(periodUs) * kFrameSharePercent)
            _status.framesOverShare++;
    }

    if (!_unpaced.load(std::memory_order_acquire))
    {
        // The hooks keep the frame over its period: 120 %, so a host hiccup alone does not count
        frame.slowFrames = unpacedUs > 0 && wallUs * 10 > static_cast<uint64_t>(periodUs) * 12 ? frame.slowFrames + 1 : 0;
        if (frame.slowFrames >= kUnpacedSlowFrames)
            SetUnpaced(true);
        return;
    }
    frame.calmFrames = unpacedUs * 4 <= periodUs ? frame.calmFrames + 1 : 0;
    if (!unpacedCallbacks || frame.calmFrames >= kUnpacedCalmFrames)
        SetUnpaced(false);
}

void ScriptCallbackManager::Fill(CallbackHit& hit, DebugCpuId cpu) const
{
    if (_context == nullptr)
        return;
    hit.frame = _context->emulatorState.frame_counter;
    const Z80* z80 = _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
    if (z80 != nullptr)
        hit.t = z80->t;
    hit.cpu = static_cast<uint8_t>(cpu);
    if (IsCardCpu(cpu))
    {
        // The card's registers at its access (the frame and the T-state stay the machine's)
        GSCardTarget* target = _context->pDebugManager != nullptr ? _context->pDebugManager->GetCardTarget(cpu) : nullptr;
        GeneralSoundCard* card = target != nullptr ? target->Card() : nullptr;
        Z80CpuRegisters regs{};
        if (card == nullptr || !card->cardRegisters(regs))
            return;
        hit.af = regs.af;
        hit.bc = regs.bc;
        hit.de = regs.de;
        hit.hl = regs.hl;
        hit.af2 = regs.afAlt;
        hit.bc2 = regs.bcAlt;
        hit.de2 = regs.deAlt;
        hit.hl2 = regs.hlAlt;
        hit.ix = regs.ix;
        hit.iy = regs.iy;
        hit.sp = regs.sp;
        hit.pc = regs.pc;
        hit.i = regs.i;
        hit.r = regs.r;
        return;
    }
    if (z80 == nullptr)
        return;
    hit.af = z80->af;
    hit.bc = z80->bc;
    hit.de = z80->de;
    hit.hl = z80->hl;
    hit.af2 = z80->alt.af;
    hit.bc2 = z80->alt.bc;
    hit.de2 = z80->alt.de;
    hit.hl2 = z80->alt.hl;
    hit.ix = z80->ix;
    hit.iy = z80->iy;
    hit.sp = z80->sp;
    hit.pc = z80->pc;
    hit.i = z80->i;
    hit.r = static_cast<uint8_t>((z80->r_low & 0x7F) | (z80->r_hi & 0x80));
}

void ScriptCallbackManager::Push(Entry& entry, CallbackHit& hit)
{
    if (entry.state.load(std::memory_order_acquire) != CallbackState::Active)
        return;
    const uint32_t head = entry.head.load(std::memory_order_relaxed);
    const uint32_t tail = entry.tail.load(std::memory_order_acquire);
    if (head - tail > entry.mask)
    {
        entry.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    entry.ring[head & entry.mask] = hit;
    entry.head.store(head + 1, std::memory_order_release);
    entry.hits.fetch_add(1, std::memory_order_relaxed);
    // A frame callback's ring waits for the frame end, on this thread
    if (head != tail || entry.spec.mode != CallbackMode::Deferred)
        return;
    // Empty to not empty: the script's thread may be waiting
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& item : _wakes)
            if (item.first == entry.spec.owner)
                wake = item.second;
    }
    if (wake)
        wake();
}

void ScriptCallbackManager::OnTap(const BreakpointTapHit& hit)
{
    if (hit.cookie >= _slots.size())
        return;
    // The slot is written before its tap is armed and cleared after it is removed (the CPU held): no lock here
    Entry* entry = _slots[hit.cookie].get();
    if (entry == nullptr)
        return;
    CallbackHit record;
    const DebugCpuId cpu = entry->spec.cpu;
    Fill(record, cpu);
    record.address = hit.address;
    record.breakpointId = hit.breakpointID;
    record.kind = static_cast<uint8_t>(hit.kind);
    record.value = hit.value;
    record.arg = hit.arg;
    // The card reports the byte of every access; the main CPU's memory holds it
    if (cpu == DebugCpuId::Main && (hit.kind == BRK_TAP_WRITE || hit.kind == BRK_TAP_READ))
        if (_context != nullptr && _context->pMemory != nullptr)
            record.value = _context->pMemory->DirectReadFromZ80Memory(hit.address);
    Dispatch(*entry, record, false, hit.replace);
}

void ScriptCallbackManager::Dispatch(Entry& entry, CallbackHit& hit, bool atOnce, uint8_t* replace)
{
    if (entry.spec.mode == CallbackMode::Deferred || (entry.spec.mode == CallbackMode::Frame && !atOnce))
    {
        Push(entry, hit);
        return;
    }
    if (entry.state.load(std::memory_order_acquire) != CallbackState::Active)
        return;
    entry.hits.fetch_add(1, std::memory_order_relaxed);
    RunHook(entry, hit, entry.spec.mode == CallbackMode::Sync ? replace : nullptr);
}

void ScriptCallbackManager::OnBreakpointStop(uint16_t breakpointId, uint16_t address, uint8_t accessKind)
{
    std::vector<std::shared_ptr<Entry>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _slots)
            if (entry && entry->spec.trigger == CallbackTrigger::Breakpoint && entry->spec.breakpointId == breakpointId)
                targets.push_back(entry);
    }
    if (targets.empty())
        return;
    CallbackHit record;
    Fill(record);
    record.address = address;
    record.breakpointId = breakpointId;
    record.kind = CallbackHit::kKindStop;
    record.value = accessKind;
    for (const auto& entry : targets)
        Dispatch(*entry, record, true);
}

void ScriptCallbackManager::OnGameEvents(uint64_t frame, uint32_t fired, const std::vector<std::string>& names,
                                         const int32_t* values)
{
    if (fired == 0)
        return;
    std::vector<std::shared_ptr<Entry>> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _slots)
            if (entry && entry->spec.trigger == CallbackTrigger::GameEvent)
                targets.push_back(entry);
    }
    CallbackHit base;
    Fill(base);
    base.frame = frame;
    base.kind = CallbackHit::kKindEvent;
    for (size_t index = 0; index < names.size() && index < 32; index++)
    {
        if (!((fired >> index) & 1u))
            continue;
        for (const auto& entry : targets)
            if (entry->spec.event == names[index])
            {
                CallbackHit record = base;
                record.value = values != nullptr ? values[index] : -1;
                Dispatch(*entry, record, true);
            }
    }
}

void ScriptCallbackManager::Publish(uint32_t id) const
{
    if (_context == nullptr || _context->pEmulator == nullptr)
        return;
    MessageCenter::DefaultMessageCenter().Post(
        NC_SCRIPT_CALLBACKS_CHANGED, new ScriptCallbacksChangedPayload(_context->pEmulator->GetId(), id), true);
}
