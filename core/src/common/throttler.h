#pragma once
#include "stdafx.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

/// Universal throttling helper: wraps any callback or lambda and limits how often it runs.
///
/// Layered architecture (docs/emulator/design/common/universal-throttling-helper.md):
///   - Strategies: pure decision state machines driven by an explicit uint64_t tick.
///     No clock, no callback, no side effects beyond their own counters.
///   - Throttler: binds a strategy to a callback (Execute / ExecuteAt, KeepLast latch, Reset).
///   - TimedThrottler: adds an injectable steady_clock-milliseconds clock.
///   - ThreadSafeThrottler: opt-in mutex shell around any throttler-like object.
///
/// The decision core never reads a clock - ticks are caller-supplied, monotonic non-decreasing
/// uint64_t values - so every test is deterministic. Integer arithmetic only; <chrono> is touched
/// solely by SteadyClockMs.

/// Anything that can decide, from a monotonic tick, whether a call may execute.
///
/// Three phases per evaluation:
///   Observe(tick)      - record the attempt; advance time-derived state (anchors, refills)
///   WouldExecute(tick) - pure query on const state: is the condition met?
///   Commit(tick)       - consume: advance state as-if the call passed
///
/// The phase split is what makes combinators possible: a compositor can query any arm without
/// consuming it, and commit every arm on an overall pass.
template<typename S>
concept ThrottleStrategy = requires(S strategy, const S constStrategy, uint64_t tick)
{
    { strategy.Observe(tick) } -> std::same_as<void>;
    { constStrategy.WouldExecute(tick) } -> std::convertible_to<bool>;
    { strategy.Commit(tick) } -> std::same_as<void>;
    { strategy.Reset() } -> std::same_as<void>;
};

/// Strategy: pass every Nth invocation. Tickless - the tick argument is ignored.
///
/// The first pass happens on call N, not on call 1: "every N calls" must observe a full group
/// before firing (metrics correctness). N = 1 is a pass-through.
class EveryNCalls
{
public:
    explicit EveryNCalls(size_t limit)
        : _limit(limit)
    {
        assert(limit >= 1);
    }

    void Observe(uint64_t /*tick*/) { ++_seen; }

    bool WouldExecute(uint64_t /*tick*/) const { return _seen >= _limit; }

    /// Legal after Observe(tick) even when WouldExecute(tick) returned false (combinators commit
    /// every arm on an overall pass): zeroing the counter restarts the group.
    void Commit(uint64_t /*tick*/) { _seen = 0; }

    void Reset() { _seen = 0; }

    /// Sugar for standalone use: Observe -> WouldExecute -> Commit in one call.
    bool ShouldExecute(uint64_t tick)
    {
        Observe(tick);
        if (!WouldExecute(tick))
            return false;
        Commit(tick);
        return true;
    }

protected:
    size_t _limit;
    size_t _seen = 0;
};

/// Start policy for MinInterval, and for interval arms inside combinators.
enum class IntervalStart
{
    Immediate,     // The first call always passes (solo UI use: the first repaint must not wait).
    FullInterval   // The first evaluation only anchors; a full interval must elapse before the
                   // first pass (required inside combinators, where "first call passes" would
                   // defeat the combined criterion).
};

/// Strategy: at most one pass per `interval` ticks. Leading edge, >= boundary, re-anchored to the
/// actual pass time - an idle gap cannot produce a catch-up burst.
class MinInterval
{
public:
    explicit MinInterval(uint64_t interval, IntervalStart start = IntervalStart::Immediate)
        : _interval(interval), _start(start)
    {
        assert(interval >= 1);
    }

    /// Anchors the clock when cold under FullInterval. A decreasing tick is a caller bug -
    /// asserted post-anchor where detectable, undefined behavior in release.
    void Observe(uint64_t tick)
    {
        if (_anchored)
        {
            assert(tick >= _lastPassedTick);
        }
        else if (_start == IntervalStart::FullInterval)
        {
            _lastPassedTick = tick;
            _anchored = true;
        }
    }

    bool WouldExecute(uint64_t tick) const
    {
        return _anchored ? (tick - _lastPassedTick >= _interval) : (_start == IntervalStart::Immediate);
    }

    /// Re-anchors to the actual pass time: each pass buys a full fresh interval. Legal after
    /// Observe(tick) even when WouldExecute(tick) returned false - combinators commit every arm
    /// on an overall pass, which re-anchors the interval cycle.
    void Commit(uint64_t tick)
    {
        _lastPassedTick = tick;
        _anchored = true;
    }

    void Reset() { _anchored = false; }

    /// Sugar for standalone use: Observe -> WouldExecute -> Commit in one call.
    bool ShouldExecute(uint64_t tick)
    {
        Observe(tick);
        if (!WouldExecute(tick))
            return false;
        Commit(tick);
        return true;
    }

protected:
    uint64_t _interval;
    IntervalStart _start;
    uint64_t _lastPassedTick = 0;
    bool _anchored = false;
};

/// Strategy: burst of `capacity` passes, then a sustained `ops` passes per `period` ticks.
/// The bucket starts full (the initial burst is allowed).
///
/// Tokens are integer fixed-point: 1 token = `period` units, one execution costs `period` units,
/// refill is `ops` units per tick. ops units/tick / period units per call = ops/period calls per
/// tick - exactly `ops` passes per `period` ticks, with integer-exact boundaries on every
/// compiler and platform.
class TokenBucket
{
public:
    TokenBucket(uint64_t capacity, uint64_t ops, uint64_t period)
        : _capacityUnits(capacity * period), _units(capacity * period), _ops(ops), _period(period)
    {
        assert(capacity >= 1);
        assert(ops >= 1);
        assert(period >= 1);
        assert(capacity <= UINT64_MAX / period);  // capacity * period must be representable
    }

    /// Refills delta * ops units. The delta is clamped so the multiply cannot overflow: after
    /// the clamp the bucket saturates anyway, so the precision loss is unobservable. A
    /// decreasing tick is a caller bug - asserted, undefined behavior in release.
    void Observe(uint64_t tick)
    {
        assert(tick >= _lastTick);
        uint64_t delta = tick - _lastTick;
        _lastTick = tick;
        if (delta > UINT64_MAX / _ops)
            delta = UINT64_MAX / _ops;
        const uint64_t refill = delta * _ops;
        _units = (refill >= _capacityUnits - _units) ? _capacityUnits : _units + refill;
    }

    bool WouldExecute(uint64_t /*tick*/) const { return _units >= _period; }

    /// Consumes one token. Legal after Observe(tick) even when WouldExecute(tick) returned false
    /// (combinators commit every arm on an overall pass): without a full token available the
    /// bucket drains to zero instead of underflowing.
    void Commit(uint64_t /*tick*/) { _units = (_units >= _period) ? _units - _period : 0; }

    void Reset()
    {
        _units = _capacityUnits;
        _lastTick = 0;
    }

    /// Sugar for standalone use: Observe -> WouldExecute -> Commit in one call.
    bool ShouldExecute(uint64_t tick)
    {
        Observe(tick);
        if (!WouldExecute(tick))
            return false;
        Commit(tick);
        return true;
    }

protected:
    uint64_t _capacityUnits;  // capacity * period: the full bucket in units
    uint64_t _units;          // current tokens in units; 1 token = _period units
    uint64_t _ops;            // refill rate: ops units per tick
    uint64_t _period;         // cost of one execution in units
    uint64_t _lastTick = 0;
};

/// Combinator: passes when either arm's condition is met (whichever happens earlier).
/// Every arm commits on an overall pass, so a timer-triggered pass restarts the count arm's
/// cycle - the intended flush-gate semantics ("the timer fired, restart the batch").
template<typename A, typename B>
    requires ThrottleStrategy<A> && ThrottleStrategy<B>
class AnyOf
{
public:
    AnyOf(A first, B second)
        : _first(std::move(first)), _second(std::move(second))
    {
    }

    void Observe(uint64_t tick)
    {
        _first.Observe(tick);
        _second.Observe(tick);
    }

    bool WouldExecute(uint64_t tick) const
    {
        return _first.WouldExecute(tick) || _second.WouldExecute(tick);
    }

    /// Commits EVERY arm - also the non-firing one: uniform cycle-restart semantics.
    void Commit(uint64_t tick)
    {
        _first.Commit(tick);
        _second.Commit(tick);
    }

    void Reset()
    {
        _first.Reset();
        _second.Reset();
    }

    /// Sugar for standalone use: Observe -> WouldExecute -> Commit in one call.
    bool ShouldExecute(uint64_t tick)
    {
        Observe(tick);
        if (!WouldExecute(tick))
            return false;
        Commit(tick);
        return true;
    }

protected:
    A _first;
    B _second;
};

/// Combinator: passes when both arms' conditions are met (whichever happens later).
/// Built-in conditions are monotone between commits (counters only grow, elapsed time only
/// grows, bucket units only refill), so an arm that has become satisfied stays satisfied until
/// the overall pass - AllOf fires exactly when its last condition becomes true.
template<typename A, typename B>
    requires ThrottleStrategy<A> && ThrottleStrategy<B>
class AllOf
{
public:
    AllOf(A first, B second)
        : _first(std::move(first)), _second(std::move(second))
    {
    }

    void Observe(uint64_t tick)
    {
        _first.Observe(tick);
        _second.Observe(tick);
    }

    bool WouldExecute(uint64_t tick) const
    {
        return _first.WouldExecute(tick) && _second.WouldExecute(tick);
    }

    /// Commits EVERY arm - also the non-firing one: uniform cycle-restart semantics.
    void Commit(uint64_t tick)
    {
        _first.Commit(tick);
        _second.Commit(tick);
    }

    void Reset()
    {
        _first.Reset();
        _second.Reset();
    }

    /// Sugar for standalone use: Observe -> WouldExecute -> Commit in one call.
    bool ShouldExecute(uint64_t tick)
    {
        Observe(tick);
        if (!WouldExecute(tick))
            return false;
        Commit(tick);
        return true;
    }

protected:
    A _first;
    B _second;
};

/// Move-only type-erased thunk for the KeepLast latch. Unlike std::function (whose callables
/// must be copy-constructible before C++23), it can hold lambdas that captured move-only
/// argument copies. The single virtual dispatch is paid only on the rejected / flushed path.
class PendingCall
{
public:
    PendingCall() = default;

    template<typename F>
    PendingCall(F fn)
        : _holder(std::make_unique<Holder<F>>(std::move(fn)))
    {
    }

    explicit operator bool() const { return _holder != nullptr; }

    void Invoke() { if (_holder) _holder->Invoke(); }

private:
    struct Base
    {
        virtual ~Base() = default;
        virtual void Invoke() = 0;
    };

    template<typename F>
    struct Holder : Base
    {
        explicit Holder(F fn)
            : fn(std::move(fn))
        {
        }

        void Invoke() override { fn(); }

        F fn;
    };

    std::unique_ptr<Base> _holder;
};

/// What happens to a rejected call.
enum class ThrottlePolicy
{
    Drop,     // Rejected calls vanish (metrics sampling, rate limiting: excess is dropped).
    KeepLast  // The most recent rejected call is latched as a type-erased thunk and can be
              // delivered by FlushPending() from an idle pump / timer, so the final state
              // always lands. Every newer rejected call supersedes the latch; a successful
              // call drops it.
};

/// Binds a strategy to a callback. Drives the three phases on every call:
/// Observe -> WouldExecute -> Commit + invoke on pass, latch on reject under KeepLast.
///
/// Callback is stored by value and invoked through std::invoke, so every callable category
/// works (lambdas, std::function, function pointers, functors, member functions via
/// std::bind_front); move-only callbacks make the throttler move-only.
template<typename Strategy, typename Callback>
    requires ThrottleStrategy<Strategy>
class Throttler
{
public:
    Throttler(Strategy strategy, Callback callback, ThrottlePolicy policy = ThrottlePolicy::Drop)
        : _strategy(std::move(strategy)), _callback(std::move(callback)), _policy(policy)
    {
    }

    /// Tickless entry point (natural for EveryNCalls). Forwards as ExecuteAt(0, ...).
    /// Calling it on a time-based strategy is misuse: the tick never advances, so after the
    /// first pass nothing passes again.
    template<typename... Args>
        requires std::invocable<Callback&, Args...>
    auto Execute(Args&&... args)
    {
        return ExecuteAt(0, std::forward<Args>(args)...);
    }

    /// Tick-driven entry point (natural for MinInterval / TokenBucket).
    /// Returns bool for void callbacks, std::optional<R> otherwise; a rejected call returns
    /// false / nullopt.
    template<typename... Args>
        requires std::invocable<Callback&, Args...>
    auto ExecuteAt(uint64_t tick, Args&&... args)
    {
        using Result = std::invoke_result_t<Callback&, Args...>;

        _strategy.Observe(tick);
        if (!_strategy.WouldExecute(tick))
        {
            LatchRejected(std::forward<Args>(args)...);
            if constexpr (std::is_void_v<Result>)
                return false;
            else
                return std::optional<Result>(std::nullopt);
        }

        _strategy.Commit(tick);
        _pending = {};  // the fresh call delivered newer state than the stale latch
        if constexpr (std::is_void_v<Result>)
        {
            std::invoke(_callback, std::forward<Args>(args)...);
            return true;
        }
        else
        {
            return std::optional<Result>(std::invoke(_callback, std::forward<Args>(args)...));
        }
    }

    /// Executes the latched pending call, if any. Returns true if one ran.
    bool FlushPending()
    {
        if (!static_cast<bool>(_pending))
            return false;
        PendingCall pending = std::move(_pending);  // moved-from _holder is null: a re-entrant
        pending.Invoke();                            // call must see no pending
        return true;
    }

    bool HasPending() const { return static_cast<bool>(_pending); }

    /// Resets strategy state (counters zeroed, bucket full, anchors forgotten) and drops any
    /// pending call. Used on emulator reset / session switch so throttles never leak state
    /// across timelines.
    void Reset()
    {
        _strategy.Reset();
        _pending = {};
    }

private:
    /// KeepLast: latches the newest rejected call as a move-only type-erased thunk holding
    /// decayed argument copies (rvalues moved). The thunk captures this by reference, so it
    /// must be flushed on the same Throttler object. Type erasure is paid only on the rejected
    /// path, never on the hot accept path.
    template<typename... Args>
    void LatchRejected(Args&&... args)
    {
        if (_policy != ThrottlePolicy::KeepLast)
            return;
        _pending = PendingCall([this, ...args = std::forward<Args>(args)]() mutable
        {
            std::invoke(_callback, std::forward<Args>(args)...);
        });
    }

    Strategy _strategy;
    Callback _callback;
    ThrottlePolicy _policy;
    PendingCall _pending;
};

/// The default clock and the only <chrono> touchpoint in the component. Uses steady_clock
/// (monotonic on Windows/macOS/Linux - never system_clock, which can jump).
struct SteadyClockMs
{
    uint64_t operator()() const
    {
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
};

/// Throttler plus an injectable clock: Execute() supplies steady_clock milliseconds as the tick.
/// The clock is a std::function member, not a hardcoded call - tests inject a mutable tick
/// variable and keep even this wrapper deterministic.
template<typename Strategy, typename Callback>
class TimedThrottler
{
public:
    TimedThrottler(Strategy strategy, Callback callback,
                   ThrottlePolicy policy = ThrottlePolicy::Drop,
                   std::function<uint64_t()> clock = {})
        : _core(std::move(strategy), std::move(callback), policy), _clock(std::move(clock))
    {
    }

    /// Clock-driven entry point: core.ExecuteAt(clock(), args...).
    /// Returns bool for void callbacks, std::optional<R> otherwise.
    template<typename... Args>
        requires std::invocable<Callback&, Args...>
    auto Execute(Args&&... args)
    {
        const uint64_t tick = static_cast<bool>(_clock) ? _clock() : SteadyClockMs{}();
        return _core.ExecuteAt(tick, std::forward<Args>(args)...);
    }

    /// Executes the latched pending call, if any. Returns true if one ran.
    bool FlushPending() { return _core.FlushPending(); }

    bool HasPending() const { return _core.HasPending(); }

    /// Resets strategy state and drops any pending call.
    void Reset() { _core.Reset(); }

protected:
    Throttler<Strategy, Callback> _core;
    std::function<uint64_t()> _clock;  // empty -> SteadyClockMs
};

/// Opt-in mutex shell around any throttler-like inner object (Throttler or TimedThrottler).
/// The core stays lock-free - single-threaded hot paths pay nothing; cross-thread use wraps
/// explicitly. The callback runs under the lock: the callback must not re-enter the same
/// throttler (deadlock) - releasing the lock around the callback would let two threads evaluate
/// the strategy concurrently and break rate guarantees.
template<typename Inner>
class ThreadSafeThrottler
{
public:
    explicit ThreadSafeThrottler(Inner inner)
        : _inner(std::move(inner))
    {
    }

    /// Locks. Forwards to the inner Execute (clock-driven on a TimedThrottler inner).
    template<typename... Args>
    auto Execute(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _inner.Execute(std::forward<Args>(args)...);
    }

    /// Locks. Forwards to the inner ExecuteAt (tick-driven on a Throttler inner).
    template<typename... Args>
    auto ExecuteAt(uint64_t tick, Args&&... args)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _inner.ExecuteAt(tick, std::forward<Args>(args)...);
    }

    /// Locks. Executes the latched pending call, if any.
    bool FlushPending()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _inner.FlushPending();
    }

    bool HasPending() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _inner.HasPending();
    }

    /// Locks. Resets strategy state and drops any pending call.
    void Reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _inner.Reset();
    }

protected:
    Inner _inner;
    mutable std::mutex _mutex;
};

/// Factories: call sites never spell template arguments - deduction does the work.
/// Makes a tick-driven Throttler (use ExecuteAt; or Execute for tickless strategies).
template<typename S, typename C>
auto MakeThrottler(S strategy, C callback, ThrottlePolicy policy = ThrottlePolicy::Drop)
{
    return Throttler<S, C>(std::move(strategy), std::move(callback), policy);
}

/// Makes a clock-driven TimedThrottler (use Execute; the clock defaults to SteadyClockMs).
template<typename S, typename C>
auto MakeTimedThrottler(S strategy, C callback,
                        ThrottlePolicy policy = ThrottlePolicy::Drop,
                        std::function<uint64_t()> clock = {})
{
    return TimedThrottler<S, C>(std::move(strategy), std::move(callback), policy, std::move(clock));
}

/// Wraps any throttler-like object into its thread-safe shell.
template<typename Inner>
auto MakeThreadSafe(Inner inner)
{
    return ThreadSafeThrottler<Inner>(std::move(inner));
}

//
// Code Under Test (CUT) wrapper to allow access to protected properties and methods for unit
// testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class EveryNCallsCUT : public EveryNCalls
{
public:
    using EveryNCalls::EveryNCalls;

    using EveryNCalls::_limit;
    using EveryNCalls::_seen;
};

class MinIntervalCUT : public MinInterval
{
public:
    using MinInterval::MinInterval;

    using MinInterval::_anchored;
    using MinInterval::_interval;
    using MinInterval::_lastPassedTick;
    using MinInterval::_start;
};

class TokenBucketCUT : public TokenBucket
{
public:
    using TokenBucket::TokenBucket;

    using TokenBucket::_capacityUnits;
    using TokenBucket::_lastTick;
    using TokenBucket::_ops;
    using TokenBucket::_period;
    using TokenBucket::_units;
};
#endif  // _CODE_UNDER_TEST
