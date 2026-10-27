#pragma once

/// @file scriptcallbacks.h
/// @brief Script callbacks, deferred mode (docs/inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md §3-§5,
/// docs/features/script-callbacks.md): a script asks to be called on a tap, on a breakpoint's stop or on a game
/// event. The emulation thread only notes the hit in the callback's ring (a snapshot of the registers, the access and
/// the frame); the script's own thread drains the ring and runs its function, then reports the call's time. The
/// manager keeps the statistics and applies the over-budget policy. The emulator never waits for a script: a full
/// ring drops the hit and counts it.
///
/// Threads: Register / Remove / Resume / SetPolicy / List from any thread; OnTap, OnBreakpointStop and OnGameEvents
/// on the emulation thread; Drain and Report on the script's thread (one consumer per callback).

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "debugger/breakpoints/breakpointmanager.h"

class EmulatorContext;

enum class CallbackTrigger : uint8_t
{
    Tap,         ///< a tap of its own (a hidden breakpoint that never stops), with an optional condition
    Breakpoint,  ///< a stop of an existing breakpoint (it still stops)
    GameEvent    ///< a game event of the profile (game.events) fired on a frame
};

enum class CallbackPolicy : uint8_t
{
    Suspend,  ///< the default: suspended until resumed (callback resume)
    Disable,  ///< removed for the session
    Ignore    ///< counted only
};

enum class CallbackState : uint8_t
{
    Active,
    Suspended,
    Disabled
};

/// What the script asks for
struct CallbackSpec
{
    CallbackTrigger trigger = CallbackTrigger::Tap;
    BreakpointSpec tap;          ///< Tap: memory or port, its condition included (owner and tap fields are set here)
    uint16_t breakpointId = 0;   ///< Breakpoint
    std::string event;           ///< GameEvent: the event's name
    std::string owner;           ///< the script: "lua", "python" (its callbacks go when it stops)
    std::string label;           ///< for the lists: the function's name
    CallbackPolicy policy = CallbackPolicy::Suspend;
    uint32_t budgetUs = 1000;    ///< per call
    uint32_t ring = 256;         ///< hits kept for the script (16 .. 4096)
};

/// One hit as the callback sees it: a snapshot, never live state
struct CallbackHit
{
    uint64_t frame = 0;
    uint32_t t = 0;             ///< T-state in the frame
    uint16_t address = 0;       ///< the access's address or port; a game event: 0
    int32_t value = -1;         ///< the byte read, written or OUT (-1: none); a game event: its value
    uint16_t breakpointId = 0;  ///< the tap or the breakpoint
    uint8_t kind = 0;           ///< BreakpointTapKind; kKindStop for a breakpoint stop, kKindEvent for a game event
    uint16_t af = 0, bc = 0, de = 0, hl = 0, af2 = 0, bc2 = 0, de2 = 0, hl2 = 0, ix = 0, iy = 0, sp = 0, pc = 0;
    uint8_t i = 0, r = 0;

    static constexpr uint8_t kKindStop = 0x80;
    static constexpr uint8_t kKindEvent = 0x81;
};

/// The statistics of a callback (§4)
struct CallbackStats
{
    uint64_t hits = 0;      ///< noted in the ring
    uint64_t dropped = 0;   ///< the ring was full
    uint64_t calls = 0;
    uint64_t errors = 0;    ///< the function raised
    uint64_t overruns = 0;  ///< calls over the budget
    uint64_t totalUs = 0;
    uint32_t maxUs = 0;
    uint32_t p99Us = 0;     ///< from a histogram of powers of two
    uint64_t lastOverrunFrame = 0;
    uint32_t lastOverrunUs = 0;
    std::string lastError;
};

struct CallbackInfo
{
    uint32_t id = 0;
    CallbackTrigger trigger = CallbackTrigger::Tap;
    std::string owner;
    std::string label;
    std::string target;     ///< "tap #12 write #5C3A if …", "breakpoint 3", "event died"
    uint16_t breakpointId = 0;
    std::string event;
    CallbackPolicy policy = CallbackPolicy::Suspend;
    CallbackState state = CallbackState::Active;
    std::string reason;     ///< why suspended or disabled
    uint32_t budgetUs = 0;
    uint32_t ring = 0;
    uint32_t pending = 0;   ///< hits in the ring
    CallbackStats stats;
};

const char* CallbackTriggerName(CallbackTrigger trigger);
/// CallbackHit::kind as the scripts see it: exec, read, write, port_in, port_out, stop, event
const char* CallbackHitKindName(uint8_t kind);
const char* CallbackPolicyName(CallbackPolicy policy);
const char* CallbackStateName(CallbackState state);
bool ParseCallbackPolicy(const std::string& text, CallbackPolicy& policy);

class ScriptCallbackManager : public IBreakpointTapSink
{
public:
    static constexpr size_t kMaxCallbacks = 256;
    static constexpr uint32_t kMinRing = 16;
    static constexpr uint32_t kMaxRing = 4096;
    /// §5: suspend on this many overruns within kOverrunWindow frames, or one call over kOverrunFactor x the budget
    static constexpr unsigned kOverrunsToSuspend = 3;
    static constexpr uint64_t kOverrunWindow = 50;
    static constexpr uint32_t kOverrunFactor = 10;
    static constexpr const char* kOwnerPrefix = "callback:";

    explicit ScriptCallbackManager(EmulatorContext* context);
    ~ScriptCallbackManager() override;
    ScriptCallbackManager(const ScriptCallbackManager&) = delete;
    ScriptCallbackManager& operator=(const ScriptCallbackManager&) = delete;

    /// The callback's id (from 1), or 0 with the reason. A tap's condition is checked as a breakpoint's
    uint32_t Register(const CallbackSpec& spec, std::string& error);
    bool Remove(uint32_t id);
    /// Every callback of a script (it stopped or reset)
    void RemoveOwner(const std::string& owner);
    void RemoveAll();
    /// Active again with fresh statistics (a suspended one); false for an unknown or disabled one
    bool Resume(uint32_t id);
    bool SetPolicy(uint32_t id, CallbackPolicy policy);
    std::vector<CallbackInfo> List() const;
    bool Get(uint32_t id, CallbackInfo& info) const;

    /// The script's thread: called with no lock held when one of the owner's rings goes from empty to not empty
    void SetWake(const std::string& owner, std::function<void()> wake);
    /// The script's thread: the hits of callback @p id so far, at most @p max (oldest first)
    size_t Drain(uint32_t id, std::vector<CallbackHit>& out, size_t max = 64);
    /// The ids of the owner's callbacks with hits waiting
    std::vector<uint32_t> Pending(const std::string& owner) const;
    /// The script's thread: one call ran for @p us (failed: it raised @p error). Applies the policy
    void Report(uint32_t id, uint32_t us, bool failed, const std::string& error = {});

    /// Emulation thread
    void OnTap(const BreakpointTapHit& hit) override;
    void OnBreakpointStop(uint16_t breakpointId, uint16_t address, uint8_t accessKind);
    /// Emulation thread, at the frame end: the events that fired (bit i: names[i])
    void OnGameEvents(uint64_t frame, uint32_t fired, const std::vector<std::string>& names, const int32_t* values);
    bool WantsGameEvents() const
    {
        return _gameEventCallbacks.load(std::memory_order_acquire) > 0;
    }
    bool WantsBreakpointStops() const
    {
        return _breakpointCallbacks.load(std::memory_order_acquire) > 0;
    }

private:
    struct Entry;
    BreakpointManager* Breakpoints() const;
    void Push(Entry& entry, CallbackHit& hit);
    void Fill(CallbackHit& hit) const;
    void Publish(uint32_t id) const;
    void Unarm(Entry& entry);

    EmulatorContext* _context = nullptr;
    mutable std::mutex _mutex;  ///< the slots and every non-hot field
    std::array<std::shared_ptr<Entry>, kMaxCallbacks> _slots{};  ///< by slot; a tap's cookie is its slot
    uint32_t _nextId = 1;
    uint8_t _sink = BRK_NO_TAP;
    std::atomic<int> _gameEventCallbacks{0};
    std::atomic<int> _breakpointCallbacks{0};
    std::vector<std::pair<std::string, std::function<void()>>> _wakes;
};
