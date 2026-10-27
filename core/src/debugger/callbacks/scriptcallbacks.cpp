#include "scriptcallbacks.h"

#include <algorithm>
#include <cstdio>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"

struct ScriptCallbackManager::Entry
{
    uint32_t id = 0;
    CallbackSpec spec;
    uint16_t tapId = BRK_INVALID;  ///< Tap: the hidden breakpoint
    std::string target;
    std::atomic<CallbackState> state{CallbackState::Active};

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
    if (tap.type == BRK_IO)
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
        case CallbackHit::kKindStop: return "stop";
        case CallbackHit::kKindEvent: return "event";
        default: return "exec";
    }
}

const char* CallbackPolicyName(CallbackPolicy policy)
{
    switch (policy)
    {
        case CallbackPolicy::Suspend: return "suspend";
        case CallbackPolicy::Disable: return "disable";
        case CallbackPolicy::Ignore: return "ignore";
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
    else
        return false;
    return true;
}

ScriptCallbackManager::ScriptCallbackManager(EmulatorContext* context) : _context(context) {}

ScriptCallbackManager::~ScriptCallbackManager()
{
    // Unregistering the sink removes its taps; the breakpoint manager outlives this (DebugManager's order)
    if (BreakpointManager* breakpoints = Breakpoints(); breakpoints != nullptr && _sink != BRK_NO_TAP)
        breakpoints->UnregisterTapSink(_sink);
    for (auto& slot : _slots)
        slot.reset();
}

BreakpointManager* ScriptCallbackManager::Breakpoints() const
{
    if (_context == nullptr || _context->pDebugManager == nullptr)
        return nullptr;
    return _context->pDebugManager->GetBreakpointsManager();
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

    BreakpointManager* breakpoints = Breakpoints();
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
                if (spec.tap.type != BRK_MEMORY && spec.tap.type != BRK_IO)
                {
                    error = "a tap callback watches memory or ports";
                    return 0;
                }
                if (breakpoints == nullptr)
                {
                    error = "no debugger on this machine";
                    return 0;
                }
                entry->target = "tap " + TapTarget(spec.tap);
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
        entry->id = _nextId++;
        _slots[slot] = entry;  // before the tap is armed: its first hit finds it
    }

    if (spec.trigger == CallbackTrigger::Tap)
    {
        if (_sink == BRK_NO_TAP)
            _sink = breakpoints->RegisterTapSink(this);
        BreakpointSpec tap = spec.tap;
        tap.owner = kOwnerPrefix + std::to_string(entry->id);
        tap.note = spec.label.empty() ? "script callback" : spec.label;
        tap.hidden = true;
        tap.shared = false;
        tap.tapSink = _sink;
        tap.tapCookie = static_cast<uint32_t>(slot);
        const uint16_t id = _sink == BRK_NO_TAP ? BRK_INVALID : breakpoints->AddBreakpoint(tap, error);
        if (id == BRK_INVALID)
        {
            if (_sink == BRK_NO_TAP)
                error = "no tap sink left";
            std::lock_guard<std::mutex> lock(_mutex);
            _slots[slot].reset();
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
    if (entry.spec.trigger == CallbackTrigger::Tap)
    {
        if (entry.tapId != BRK_INVALID)
            if (BreakpointManager* breakpoints = Breakpoints())
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
    if (entry->state.load() != CallbackState::Disabled)
        Unarm(*entry);
    entry->state.store(CallbackState::Disabled);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _slots[slot].reset();
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
        info.breakpointId = entry->spec.trigger == CallbackTrigger::Tap ? entry->tapId : entry->spec.breakpointId;
        info.event = entry->spec.event;
        info.policy = entry->spec.policy;
        info.state = entry->state.load(std::memory_order_acquire);
        info.reason = entry->reason;
        info.budgetUs = entry->spec.budgetUs;
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
        if (us <= entry.spec.budgetUs || entry.state.load() != CallbackState::Active)
            return;
        const uint64_t frame = _context != nullptr ? _context->emulatorState.frame_counter : 0;
        stats.overruns++;
        stats.lastOverrunFrame = frame;
        stats.lastOverrunUs = us;
        if (entry.spec.policy == CallbackPolicy::Ignore)
            return;
        auto& recent = entry.overrunFrames;
        recent.push_back(frame);
        recent.erase(std::remove_if(recent.begin(), recent.end(),
                                    [frame](uint64_t at) { return at + kOverrunWindow < frame; }),
                     recent.end());
        const bool far = static_cast<uint64_t>(us) > static_cast<uint64_t>(entry.spec.budgetUs) * kOverrunFactor;
        if (!far && recent.size() < kOverrunsToSuspend)
            return;
        entry.reason = far ? "one call took " + std::to_string(us) + " us (budget " + std::to_string(entry.spec.budgetUs) + ")"
                           : std::to_string(recent.size()) + " calls over " + std::to_string(entry.spec.budgetUs) +
                                 " us within " + std::to_string(kOverrunWindow) + " frames";
        if (entry.spec.policy == CallbackPolicy::Suspend)
            entry.state.store(CallbackState::Suspended, std::memory_order_release);
        else
            disable = *found;
        changed = true;
    }
    if (disable)
    {
        Unarm(*disable);
        disable->state.store(CallbackState::Disabled, std::memory_order_release);
    }
    if (changed)
        Publish(id);
}

void ScriptCallbackManager::Fill(CallbackHit& hit) const
{
    if (_context == nullptr)
        return;
    hit.frame = _context->emulatorState.frame_counter;
    const Z80* z80 = _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
    if (z80 == nullptr)
        return;
    hit.t = z80->t;
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
    if (head != tail)
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
    Fill(record);
    record.address = hit.address;
    record.breakpointId = hit.breakpointID;
    record.kind = static_cast<uint8_t>(hit.kind);
    record.value = hit.value;
    if (hit.kind == BRK_TAP_WRITE || hit.kind == BRK_TAP_READ)
        if (_context != nullptr && _context->pMemory != nullptr)
            record.value = _context->pMemory->DirectReadFromZ80Memory(hit.address);
    Push(*entry, record);
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
        Push(*entry, record);
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
                Push(*entry, record);
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
