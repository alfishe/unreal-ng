#include "pch.h"

#include "common/throttler.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

/// Injectable std::function<uint64_t()> clock backed by a mutable tick variable: every timed
/// test drives time explicitly, so the whole suite is deterministic (zero sleeps, no wall clock).
class MutableClock
{
public:
    MutableClock() = default;

    std::function<uint64_t()> AsFunction()
    {
        return [this] { return _tick; };
    }

    void Set(uint64_t tick) { _tick = tick; }
    void Advance(uint64_t delta) { _tick += delta; }
    uint64_t Now() const { return _tick; }

private:
    uint64_t _tick = 0;
};

/// Strategies, the core binding and the KeepLast latch. CUT access asserts state transitions
/// directly (_seen, _units, _lastPassedTick) instead of inferring them from call counts.
class Throttler_Test : public ::testing::Test
{
};

class TimedThrottler_Test : public ::testing::Test
{
};

class ThreadSafeThrottler_Test : public ::testing::Test
{
};

/// @brief Calls 1..N-1 are rejected, the Nth passes, and the counter resets after the pass.
TEST_F(Throttler_Test, EveryNCallsPassesExactlyOnNthCall)
{
    EveryNCallsCUT strategy(3);

    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_EQ(strategy._seen, 2u);

    EXPECT_TRUE(strategy.ShouldExecute(0));
    EXPECT_EQ(strategy._seen, 0u);  // the pass restarted the group

    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_TRUE(strategy.ShouldExecute(0));
}

/// @brief N = 1 is a documented pass-through: every call passes.
TEST_F(Throttler_Test, EveryNCallsPassThroughAtLimitOne)
{
    EveryNCallsCUT strategy(1);

    for (int i = 0; i < 5; i++)
        EXPECT_TRUE(strategy.ShouldExecute(i));
}

/// @brief The tick argument is ignored: wild tick values produce the identical pass pattern.
TEST_F(Throttler_Test, EveryNCallsIgnoresTick)
{
    const uint64_t wildTicks[] = {0, 9'000'000'000'000ull, 5, 77, UINT64_MAX - 1};

    EveryNCallsCUT wild(4);
    bool wildPattern[5];
    for (int i = 0; i < 5; i++)
        wildPattern[i] = wild.ShouldExecute(wildTicks[i]);

    EveryNCallsCUT zero(4);
    for (int i = 0; i < 5; i++)
        EXPECT_EQ(wildPattern[i], zero.ShouldExecute(0));
}

/// @brief A mid-cycle Reset() restarts the group: a full N calls are needed again.
TEST_F(Throttler_Test, EveryNCallsResetZeroesCounter)
{
    EveryNCallsCUT strategy(4);

    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_EQ(strategy._seen, 2u);

    strategy.Reset();
    EXPECT_EQ(strategy._seen, 0u);

    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(0));
    EXPECT_TRUE(strategy.ShouldExecute(0));
}

/// @brief Immediate start: the first call passes with no wait - the first repaint must not wait.
TEST_F(Throttler_Test, MinIntervalFirstCallPasses)
{
    MinIntervalCUT strategy(1000);

    EXPECT_TRUE(strategy.ShouldExecute(0));
    EXPECT_TRUE(strategy._anchored);
}

/// @brief A call interval-1 ticks after the last pass is rejected.
TEST_F(Throttler_Test, MinIntervalRejectsBeforeBoundary)
{
    MinIntervalCUT strategy(16);

    EXPECT_TRUE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(15));
}

/// @brief >= semantics: a call exactly `interval` ticks after the last pass passes.
TEST_F(Throttler_Test, MinIntervalPassesAtExactBoundary)
{
    int calls = 0;
    auto throttler = MakeThrottler(MinInterval(50), [&calls] { ++calls; });

    EXPECT_TRUE(throttler.ExecuteAt(0));
    EXPECT_FALSE(throttler.ExecuteAt(49));
    EXPECT_TRUE(throttler.ExecuteAt(50));
    EXPECT_EQ(calls, 2);
}

/// @brief A rejection does not move _lastPassedTick (no drift, no catch-up burst). If it did,
/// a pass at t=16 after a rejection at t=15 would be rejected as "1 tick since the anchor".
TEST_F(Throttler_Test, MinIntervalRejectionKeepsAnchor)
{
    MinIntervalCUT strategy(16);

    EXPECT_TRUE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(15));
    EXPECT_EQ(strategy._lastPassedTick, 0u);  // the rejection left the anchor alone

    EXPECT_TRUE(strategy.ShouldExecute(16));  // 16 - 0 >= 16
    EXPECT_EQ(strategy._lastPassedTick, 16u);  // re-anchored to the actual pass time
}

/// @brief FullInterval start: the first evaluation only anchors the clock; a full interval must
/// elapse before the first pass (required inside combinators).
TEST_F(Throttler_Test, MinIntervalFullIntervalStartWaits)
{
    MinIntervalCUT strategy(100, IntervalStart::FullInterval);

    EXPECT_FALSE(strategy.ShouldExecute(0));  // anchors only
    EXPECT_EQ(strategy._lastPassedTick, 0u);
    EXPECT_TRUE(strategy._anchored);

    EXPECT_FALSE(strategy.ShouldExecute(99));
    EXPECT_TRUE(strategy.ShouldExecute(100));
}

/// @brief After Reset() the anchor is forgotten: the first call passes again.
TEST_F(Throttler_Test, MinIntervalResetForgetsAnchor)
{
    MinIntervalCUT strategy(10);

    EXPECT_TRUE(strategy.ShouldExecute(0));
    EXPECT_FALSE(strategy.ShouldExecute(5));

    strategy.Reset();
    EXPECT_FALSE(strategy._anchored);

    EXPECT_TRUE(strategy.ShouldExecute(7));  // first call after Reset passes immediately
}

/// @brief The bucket starts full: B passes at one tick, the B+1th is rejected.
TEST_F(Throttler_Test, TokenBucketInitialBurstEqualsCapacity)
{
    TokenBucketCUT bucket(5, 10, 1000);
    EXPECT_EQ(bucket._units, bucket._capacityUnits);  // 5000 units = 5 tokens

    for (int i = 0; i < 5; i++)
        EXPECT_TRUE(bucket.ShouldExecute(0));
    EXPECT_EQ(bucket._units, 0u);

    EXPECT_FALSE(bucket.ShouldExecute(0));  // burst exhausted, no time has passed
}

/// @brief R passes per P ticks at integer-exact boundaries: capacity 1, 2 ops per 10 ticks
/// passes at t=0, 5, 10... and rejects one tick before each boundary.
TEST_F(Throttler_Test, TokenBucketSustainedCadenceExact)
{
    TokenBucketCUT bucket(1, 2, 10);

    EXPECT_TRUE(bucket.ShouldExecute(0));   // full: 10 units, consume 10 -> 0
    EXPECT_FALSE(bucket.ShouldExecute(4));  // refill 4 * 2 = 8 units < 10
    EXPECT_TRUE(bucket.ShouldExecute(5));   // + 10 units -> exactly one token again
    EXPECT_FALSE(bucket.ShouldExecute(9));  // + 8 units < 10
    EXPECT_TRUE(bucket.ShouldExecute(10));  // + 2 units -> exactly 10
}

/// @brief A long idle gap refills the bucket to full, capped at capacity.
TEST_F(Throttler_Test, TokenBucketIdleSnapToFull)
{
    TokenBucketCUT bucket(2, 1, 100);
    EXPECT_EQ(bucket._capacityUnits, 200u);

    EXPECT_TRUE(bucket.ShouldExecute(0));
    EXPECT_TRUE(bucket.ShouldExecute(0));  // burst drained
    EXPECT_EQ(bucket._units, 0u);

    bucket.Observe(10'000);                 // long idle
    EXPECT_EQ(bucket._units, bucket._capacityUnits);  // snapped to full, not beyond
    EXPECT_TRUE(bucket.WouldExecute(10'000));
}

/// @brief A UINT64_MAX-scale delta saturates without overflow or UB: the delta is clamped to
/// UINT64_MAX / ops before multiplying, and the refill saturates at capacity.
TEST_F(Throttler_Test, TokenBucketHugeDeltaClamps)
{
    TokenBucketCUT bucket(3, 7, 1000);

    for (int i = 0; i < 3; i++)
        EXPECT_TRUE(bucket.ShouldExecute(0));
    EXPECT_EQ(bucket._units, 0u);

    bucket.Observe(UINT64_MAX);
    EXPECT_EQ(bucket._units, bucket._capacityUnits);  // saturated: clamp lost nothing observable

    EXPECT_TRUE(bucket.WouldExecute(UINT64_MAX));
    bucket.Commit(UINT64_MAX);
    EXPECT_EQ(bucket._units, bucket._capacityUnits - 1000u);
}

/// @brief Reset() refills the bucket to full and clears the time anchor.
TEST_F(Throttler_Test, TokenBucketResetRefills)
{
    TokenBucketCUT bucket(2, 5, 100);

    EXPECT_TRUE(bucket.ShouldExecute(0));
    EXPECT_TRUE(bucket.ShouldExecute(0));
    EXPECT_EQ(bucket._units, 0u);

    bucket.Reset();
    EXPECT_EQ(bucket._units, bucket._capacityUnits);
    EXPECT_EQ(bucket._lastTick, 0u);
    EXPECT_TRUE(bucket.ShouldExecute(0));
}

/// @brief WouldExecute is a pure query: repeated calls return the same value and change
/// nothing - only Commit consumes.
TEST_F(Throttler_Test, QueryDoesNotConsume)
{
    MinIntervalCUT strategy(10);

    EXPECT_TRUE(strategy.ShouldExecute(0));

    EXPECT_TRUE(strategy.WouldExecute(10));
    EXPECT_TRUE(strategy.WouldExecute(10));  // repeatable: the query consumed nothing
    EXPECT_TRUE(strategy.WouldExecute(10));

    strategy.Commit(10);                      // only now the evaluation is consumed
    EXPECT_FALSE(strategy.WouldExecute(10));
    EXPECT_TRUE(strategy.WouldExecute(20));
}

/// @brief Observe(t) alone advances time-derived state (refills, anchors), visible via
/// subsequent WouldExecute calls without any Commit.
TEST_F(Throttler_Test, ObserveAdvancesTimeState)
{
    TokenBucketCUT bucket(1, 1, 100);

    EXPECT_TRUE(bucket.ShouldExecute(0));    // bucket now empty
    EXPECT_FALSE(bucket.WouldExecute(50));

    bucket.Observe(100);                     // refill only
    EXPECT_TRUE(bucket.WouldExecute(100));

    bucket.Commit(100);
    EXPECT_EQ(bucket._units, 0u);
}

/// @brief 1 call/ms through AnyOf(count-1000, interval-100 FullInterval): the time arm wins at
/// tick 100 (the §3.4 walkthrough).
TEST_F(Throttler_Test, AnyOfTimeArmWinsAtInterval)
{
    int flushes = 0;
    auto gate = MakeThrottler(
        AnyOf(EveryNCalls(1000), MinInterval(100, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; });

    for (uint64_t tick = 0; tick <= 100; ++tick)
        EXPECT_EQ(gate.ExecuteAt(tick), tick == 100);

    EXPECT_EQ(flushes, 1);
}

/// @brief 1 000 calls in one burst at t=0: the count arm wins on call 1 000 while the interval
/// arm (FullInterval) has not elapsed.
TEST_F(Throttler_Test, AnyOfCountArmWinsOnBurst)
{
    int flushes = 0;
    auto gate = MakeThrottler(
        AnyOf(EveryNCalls(1000), MinInterval(100, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; });

    for (int i = 0; i < 999; i++)
        EXPECT_FALSE(gate.ExecuteAt(0));
    EXPECT_TRUE(gate.ExecuteAt(0));  // call 1 000
    EXPECT_EQ(flushes, 1);
}

/// @brief A time-triggered pass commits BOTH arms: the count arm restarts, so a full 1 000
/// calls are needed after the interval flush.
TEST_F(Throttler_Test, AnyOfCommitsBothArms)
{
    auto gate = MakeThrottler(
        AnyOf(EveryNCalls(1000), MinInterval(100, IntervalStart::FullInterval)),
        [] {});

    for (uint64_t tick = 0; tick <= 100; ++tick)
        gate.ExecuteAt(tick);  // interval arm passes at tick 100 and commits the count arm too

    for (int i = 0; i < 999; i++)
        EXPECT_FALSE(gate.ExecuteAt(100));  // count restarted from zero; interval re-anchored
    EXPECT_TRUE(gate.ExecuteAt(100));       // count arm reached 1 000 again
}

/// @brief Guard test for the documented pitfall: an Immediate interval arm inside AnyOf makes
/// the very first call pass, defeating the combined criterion.
TEST_F(Throttler_Test, AnyOfImmediateArmFiresOnFirstCall)
{
    auto gate = MakeThrottler(AnyOf(EveryNCalls(10), MinInterval(100)), [] {});

    EXPECT_TRUE(gate.ExecuteAt(0));
}

/// @brief AllOf fires only once the LAST condition is met: the count arm is satisfied on call 3,
/// but the overall pass waits for the interval arm at tick 50.
TEST_F(Throttler_Test, AllOfFiresWhenLastConditionMet)
{
    int flushes = 0;
    auto gate = MakeThrottler(
        AllOf(EveryNCalls(3), MinInterval(50, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; });

    EXPECT_FALSE(gate.ExecuteAt(0));   // neither: 1st call, anchor only
    EXPECT_FALSE(gate.ExecuteAt(25));  // count 2, interval not elapsed
    EXPECT_TRUE(gate.ExecuteAt(50));   // count 3 AND interval 50
    EXPECT_EQ(flushes, 1);
}

/// @brief Monotone conditions between commits: the count arm became satisfied at call 2 and
/// STAYED satisfied until the interval arm allowed the pass at tick 30.
TEST_F(Throttler_Test, AllOfEarlierArmStaysSatisfied)
{
    auto gate = MakeThrottler(
        AllOf(EveryNCalls(2), MinInterval(30, IntervalStart::FullInterval)),
        [] {});

    EXPECT_FALSE(gate.ExecuteAt(0));   // count 1
    EXPECT_FALSE(gate.ExecuteAt(10));  // count satisfied (2 >= 2), interval not yet
    EXPECT_FALSE(gate.ExecuteAt(20));  // count still satisfied (monotone), interval not yet
    EXPECT_TRUE(gate.ExecuteAt(30));   // the last condition became true
}

/// @brief Combinators are strategies themselves and nest: here EveryNCalls(4) fires the outer
/// AnyOf at t=0, then the inner AllOf fires at t=10 after the count arm restarts.
TEST_F(Throttler_Test, CombinatorsNest)
{
    int flushes = 0;
    auto nested = MakeThrottler(
        AnyOf(AllOf(EveryNCalls(2), MinInterval(10, IntervalStart::FullInterval)), EveryNCalls(4)),
        [&flushes] { ++flushes; });

    EXPECT_FALSE(nested.ExecuteAt(0));
    EXPECT_FALSE(nested.ExecuteAt(0));
    EXPECT_FALSE(nested.ExecuteAt(0));
    EXPECT_TRUE(nested.ExecuteAt(0));   // outer count arm: call 4

    EXPECT_FALSE(nested.ExecuteAt(10));  // inner AllOf: count 1 after the restart
    EXPECT_TRUE(nested.ExecuteAt(10));   // inner AllOf: count 2 AND interval 10 - 0 >= 10
    EXPECT_EQ(flushes, 2);
}

/// @brief Arguments of arity 0/1/3 are forwarded exactly - captured values match what was passed.
TEST_F(Throttler_Test, ArgumentsForwardedExactly)
{
    int calls0 = 0;
    auto throttler0 = MakeThrottler(EveryNCalls(1), [&calls0] { ++calls0; });
    EXPECT_TRUE(throttler0.Execute());
    EXPECT_EQ(calls0, 1);

    int arg1 = 0;
    auto throttler1 = MakeThrottler(EveryNCalls(1), [&arg1](int value) { arg1 = value; });
    EXPECT_TRUE(throttler1.Execute(42));
    EXPECT_EQ(arg1, 42);

    int a3 = 0;
    std::string s3;
    double d3 = 0.0;
    auto throttler3 = MakeThrottler(
        EveryNCalls(1),
        [&](int value, const std::string& text, double ratio) { a3 = value; s3 = text; d3 = ratio; });
    EXPECT_TRUE(throttler3.Execute(7, std::string("hi"), 0.5));
    EXPECT_EQ(a3, 7);
    EXPECT_EQ(s3, "hi");
    EXPECT_DOUBLE_EQ(d3, 0.5);
}

/// @brief A void callback yields bool: true on pass, false on reject.
TEST_F(Throttler_Test, VoidCallbackReturnsBool)
{
    int calls = 0;
    auto throttler = MakeThrottler(MinInterval(10), [&calls] { ++calls; });

    EXPECT_TRUE(throttler.ExecuteAt(0));
    EXPECT_FALSE(throttler.ExecuteAt(5));
    EXPECT_EQ(calls, 1);
}

/// @brief A value callback yields std::optional<R>: the value on pass, nullopt on reject -
/// a skipped call has no honest R value.
TEST_F(Throttler_Test, ValueCallbackReturnsOptional)
{
    auto throttler = MakeThrottler(EveryNCalls(2), [] { return 7; });

    const auto first = throttler.Execute();
    EXPECT_FALSE(first.has_value());

    const auto second = throttler.Execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), 7);
}

/// @brief A rejected call never reaches the callback: the invocation count is unchanged.
TEST_F(Throttler_Test, RejectedCallNeverInvokesCallback)
{
    int calls = 0;
    auto throttler = MakeThrottler(MinInterval(100), [&calls] { ++calls; });

    EXPECT_TRUE(throttler.ExecuteAt(0));
    for (uint64_t tick = 1; tick < 100; ++tick)
        EXPECT_FALSE(throttler.ExecuteAt(tick));

    EXPECT_EQ(calls, 1);  // only the accepted first call
}

/// @brief The tickless Execute(args...) overload is exactly ExecuteAt(0, args...).
TEST_F(Throttler_Test, TicklessOverloadMatchesZeroTick)
{
    int ticklessCalls = 0;
    int zeroedCalls = 0;
    auto tickless = MakeThrottler(MinInterval(16), [&ticklessCalls] { ++ticklessCalls; });
    auto zeroed = MakeThrottler(MinInterval(16), [&zeroedCalls] { ++zeroedCalls; });

    EXPECT_EQ(tickless.Execute(), zeroed.ExecuteAt(0));  // both true: first call
    EXPECT_EQ(tickless.Execute(), zeroed.ExecuteAt(0));  // both false: the tick never advances
    EXPECT_EQ(ticklessCalls, 1);
    EXPECT_EQ(zeroedCalls, 1);
}

/// @brief KeepLast supersede rule: every newer rejected call replaces the latched one - the
/// last arguments win, matching coalescing semantics.
TEST_F(Throttler_Test, KeepLastLatchesNewestRejectedCall)
{
    std::vector<int> delivered;
    auto throttler = MakeThrottler(
        MinInterval(10), [&delivered](int value) { delivered.push_back(value); },
        ThrottlePolicy::KeepLast);

    EXPECT_TRUE(throttler.ExecuteAt(0, 1));
    EXPECT_FALSE(throttler.ExecuteAt(5, 2));
    EXPECT_FALSE(throttler.ExecuteAt(8, 3));
    EXPECT_TRUE(throttler.HasPending());

    EXPECT_TRUE(throttler.FlushPending());
    EXPECT_FALSE(throttler.HasPending());

    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_EQ(delivered[0], 1);  // the accepted call
    EXPECT_EQ(delivered[1], 3);  // the newest rejected call won the latch, not 2
}

/// @brief A successful ExecuteAt drops the latch: the fresh call already delivered newer state
/// than the stale one.
TEST_F(Throttler_Test, KeepLastSuccessDropsLatch)
{
    std::vector<int> delivered;
    auto throttler = MakeThrottler(
        MinInterval(10), [&delivered](int value) { delivered.push_back(value); },
        ThrottlePolicy::KeepLast);

    EXPECT_TRUE(throttler.ExecuteAt(0, 1));
    EXPECT_FALSE(throttler.ExecuteAt(5, 2));
    EXPECT_TRUE(throttler.HasPending());

    EXPECT_TRUE(throttler.ExecuteAt(10, 3));  // accepted: clears the latch
    EXPECT_FALSE(throttler.HasPending());
    EXPECT_FALSE(throttler.FlushPending());

    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_EQ(delivered[1], 3);
}

/// @brief FlushPending() delivers exactly the latched call once; a second flush is a no-op.
TEST_F(Throttler_Test, KeepLastFlushExecutesAndClears)
{
    int calls = 0;
    auto throttler = MakeThrottler(
        MinInterval(10), [&calls] { ++calls; }, ThrottlePolicy::KeepLast);

    EXPECT_TRUE(throttler.ExecuteAt(0));
    EXPECT_FALSE(throttler.ExecuteAt(5));
    EXPECT_TRUE(throttler.HasPending());

    EXPECT_TRUE(throttler.FlushPending());
    EXPECT_FALSE(throttler.HasPending());
    EXPECT_FALSE(throttler.FlushPending());
    EXPECT_EQ(calls, 2);
}

/// @brief A move-only argument type (unique_ptr) survives latching: rvalues are moved into the
/// thunk's decayed copies.
TEST_F(Throttler_Test, KeepLastMovesRvaluesIntoLatch)
{
    int value = 0;
    auto throttler = MakeThrottler(
        MinInterval(10), [&value](std::unique_ptr<int> payload) { value = *payload; },
        ThrottlePolicy::KeepLast);

    EXPECT_TRUE(throttler.ExecuteAt(0, std::make_unique<int>(1)));
    EXPECT_FALSE(throttler.ExecuteAt(5, std::make_unique<int>(41)));
    EXPECT_TRUE(throttler.HasPending());

    EXPECT_TRUE(throttler.FlushPending());
    EXPECT_EQ(value, 41);
}

/// @brief Reset clears both the strategy state and the pending latch.
TEST_F(Throttler_Test, ResetClearsStrategyAndLatch)
{
    int calls = 0;
    auto throttler = MakeThrottler(
        MinInterval(10), [&calls] { ++calls; }, ThrottlePolicy::KeepLast);

    EXPECT_TRUE(throttler.ExecuteAt(0));
    EXPECT_FALSE(throttler.ExecuteAt(5));
    EXPECT_TRUE(throttler.HasPending());

    throttler.Reset();
    EXPECT_FALSE(throttler.HasPending());
    EXPECT_FALSE(throttler.FlushPending());

    EXPECT_TRUE(throttler.ExecuteAt(7));  // strategy is fresh: first call passes again
    EXPECT_EQ(calls, 2);
}

/// @brief The injected clock drives the core 1:1: MutableClock reproduces the manual-tick
/// sequences exactly, including the >= boundary.
TEST_F(TimedThrottler_Test, InjectedClockDrivesCore)
{
    MutableClock clock;
    int calls = 0;
    auto throttler = MakeTimedThrottler(MinInterval(50), [&calls] { ++calls; },
                                         ThrottlePolicy::Drop, clock.AsFunction());

    clock.Set(0);
    EXPECT_TRUE(throttler.Execute());

    clock.Advance(49);
    EXPECT_FALSE(throttler.Execute());

    clock.Advance(1);
    EXPECT_TRUE(throttler.Execute());  // exactly 50 ms since the last pass
    EXPECT_EQ(calls, 2);
}

/// @brief Smoke test for the default clock: successive SteadyClockMs reads are non-decreasing
/// (monotonic on Windows/macOS/Linux).
TEST_F(TimedThrottler_Test, DefaultClockIsMonotonic)
{
    SteadyClockMs clock;
    const uint64_t first = clock();
    const uint64_t second = clock();
    EXPECT_GE(second, first);
}

/// @brief FlushPending and Reset forward to the core from the timed wrapper.
TEST_F(TimedThrottler_Test, FlushPendingAndResetForward)
{
    MutableClock clock;
    int calls = 0;
    auto throttler = MakeTimedThrottler(
        MinInterval(10), [&calls] { ++calls; }, ThrottlePolicy::KeepLast, clock.AsFunction());

    clock.Set(0);
    EXPECT_TRUE(throttler.Execute());
    clock.Set(5);
    EXPECT_FALSE(throttler.Execute());

    EXPECT_TRUE(throttler.HasPending());
    EXPECT_TRUE(throttler.FlushPending());
    EXPECT_FALSE(throttler.HasPending());

    clock.Set(6);
    EXPECT_FALSE(throttler.Execute());
    throttler.Reset();
    EXPECT_FALSE(throttler.HasPending());

    EXPECT_TRUE(throttler.Execute());  // fresh strategy: first call passes
    EXPECT_EQ(calls, 3);
}

/// @brief Single-threaded behavior of the wrapper is identical to the inner throttler.
TEST_F(ThreadSafeThrottler_Test, SequentialMatchesInner)
{
    int wrappedCalls = 0;
    int plainCalls = 0;
    auto wrapped = MakeThreadSafe(
        MakeThrottler(EveryNCalls(3), [&wrappedCalls] { ++wrappedCalls; }));
    auto plain = MakeThrottler(EveryNCalls(3), [&plainCalls] { ++plainCalls; });

    for (int i = 0; i < 6; i++)
        EXPECT_EQ(wrapped.Execute(), plain.Execute());

    EXPECT_EQ(wrappedCalls, 2);
    EXPECT_EQ(plainCalls, 2);
}

/// @brief N threads x M calls: the lock serializes evaluation, so the total pass count equals
/// the serial-order count exactly - total / N with EveryNCalls.
TEST_F(ThreadSafeThrottler_Test, ConcurrentCallsRespectStrategy)
{
    constexpr int kThreads = 4;
    constexpr int kCallsPerThread = 1000;
    constexpr int kExpectedPasses = kThreads * kCallsPerThread / 50;

    int passes = 0;  // incremented under the wrapper's lock only
    auto wrapped = MakeThreadSafe(
        MakeThrottler(EveryNCalls(50), [&passes] { ++passes; }));

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++)
    {
        threads.emplace_back([&wrapped] {
            for (int i = 0; i < kCallsPerThread; i++)
                wrapped.Execute();
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(passes, kExpectedPasses);  // 4000 / 50 = 80, regardless of interleaving
}

/// @brief The pattern the throttler exists for: 100 frame events at 1/ms through
/// MinInterval(16) + KeepLast land 7 repaints at t=0,16,...,96, and the idle-pump flush delivers
/// the trailing state - the final frame is never lost.
TEST(ThrottlerIntegration_Test, UiRepaintPattern)
{
    MutableClock clock;
    int repaints = 0;
    auto repaint = MakeTimedThrottler(
        MinInterval(16), [&repaints] { ++repaints; }, ThrottlePolicy::KeepLast,
        clock.AsFunction());

    for (int event = 0; event < 100; ++event)
    {
        clock.Set(static_cast<uint64_t>(event));
        repaint.Execute();
    }
    EXPECT_EQ(repaints, 7);

    EXPECT_TRUE(repaint.HasPending());   // the t=99 event is latched
    EXPECT_TRUE(repaint.FlushPending()); // the idle pump lands the final frame
    EXPECT_EQ(repaints, 8);
    EXPECT_FALSE(repaint.HasPending());
}

/// @brief Flush the sink every 1 000 lines or 100 ms, whichever comes first: a steady 1/ms
/// stream flushes on the time arm at tick 100; a 1 000-line burst right after flushes on the
/// count arm exactly at line 1 000; trailing lines drain via FlushPending().
TEST(ThrottlerIntegration_Test, LogFlushPattern)
{
    MutableClock clock;
    int flushes = 0;
    auto flushSink = MakeTimedThrottler(
        AnyOf(EveryNCalls(1000), MinInterval(100, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; }, ThrottlePolicy::KeepLast, clock.AsFunction());

    for (uint64_t tick = 0; tick <= 100; ++tick)
    {
        clock.Set(tick);
        flushSink.Execute();
    }
    EXPECT_EQ(flushes, 1);  // the time arm fired at tick 100

    // 1 000-line burst at the same tick: the interval arm re-anchored to 100 cannot fire, so
    // only the count arm can.
    for (int line = 1; line < 1000; line++)
    {
        flushSink.Execute();
        EXPECT_EQ(flushes, 1);
    }
    EXPECT_TRUE(flushSink.Execute());
    EXPECT_EQ(flushes, 2);  // exactly at burst line 1 000
    EXPECT_FALSE(flushSink.HasPending());  // the pass dropped the latch

    for (int line = 0; line < 5; line++)
        flushSink.Execute();
    EXPECT_TRUE(flushSink.HasPending());
    EXPECT_TRUE(flushSink.FlushPending());
    EXPECT_EQ(flushes, 3);
}

/// @brief Egress shaping: burst of 5, then sustain 10 per 1000 ms. Three batches of 10 queued
/// requests at t=0/500/1000 send exactly 5 each - 15 sent, 15 parked.
TEST(ThrottlerIntegration_Test, EgressRatePattern)
{
    MutableClock clock;
    int sent = 0;
    std::vector<int> parked;
    auto egress = MakeTimedThrottler(
        TokenBucket(5, 10, 1000), [&sent](int /*request*/) { ++sent; },
        ThrottlePolicy::Drop, clock.AsFunction());

    for (const uint64_t tick : {0u, 500u, 1000u})
    {
        clock.Set(tick);
        for (int request = 0; request < 10; request++)
        {
            if (!egress.Execute(request))
                parked.push_back(request);
        }
    }

    EXPECT_EQ(sent, 15);          // each 500 ms gap refills exactly 5 tokens (500 * 10 units)
    EXPECT_EQ(parked.size(), 15u);
}

/// @brief Contract equivalence: the Throttler drives Observe -> WouldExecute -> Commit exactly
/// like the strategy's ShouldExecute sugar - identical pass/reject patterns on the same ticks.
TEST(ThrottlerIntegration_Test, PhasesMatchSugarEndToEnd)
{
    const uint64_t intervalTicks[] = {0, 5, 16, 16, 20, 32, 31, 47, 48, 100};
    int throttlerCalls = 0;
    auto throttler = MakeThrottler(MinInterval(16), [&throttlerCalls] { ++throttlerCalls; });
    MinInterval sugar(16);
    int sugarPasses = 0;

    for (const uint64_t tick : intervalTicks)
    {
        const bool viaThrottler = throttler.ExecuteAt(tick);
        const bool viaSugar = sugar.ShouldExecute(tick);
        EXPECT_EQ(viaThrottler, viaSugar);
        if (viaSugar)
            ++sugarPasses;
    }
    EXPECT_EQ(throttlerCalls, sugarPasses);

    const uint64_t bucketTicks[] = {0, 0, 0, 0, 0, 0, 400, 500, 900, 1000};
    int bucketThrottlerCalls = 0;
    auto bucketThrottler = MakeThrottler(
        TokenBucket(5, 10, 1000), [&bucketThrottlerCalls] { ++bucketThrottlerCalls; });
    TokenBucket sugarBucket(5, 10, 1000);
    int bucketSugarPasses = 0;

    for (const uint64_t tick : bucketTicks)
    {
        const bool viaThrottler = bucketThrottler.ExecuteAt(tick);
        const bool viaSugar = sugarBucket.ShouldExecute(tick);
        EXPECT_EQ(viaThrottler, viaSugar);
        if (viaSugar)
            ++bucketSugarPasses;
    }
    EXPECT_EQ(bucketThrottlerCalls, bucketSugarPasses);
}

/// @brief No state leaks across a session switch (TTD-style): after a mid-stream Reset(), the
/// throttler behaves exactly like a freshly constructed twin on the same subsequent stream.
TEST(ThrottlerIntegration_Test, ResetAcrossSessions)
{
    MutableClock clock;
    int flushes = 0;
    auto session = MakeTimedThrottler(
        AnyOf(EveryNCalls(10), MinInterval(10, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; }, ThrottlePolicy::KeepLast, clock.AsFunction());

    for (uint64_t tick = 0; tick <= 11; ++tick)
    {
        clock.Set(tick);
        session.Execute();
    }
    EXPECT_EQ(flushes, 1);              // both arms fire at tick 10
    EXPECT_TRUE(session.HasPending());  // the tick-11 line is latched

    session.Reset();
    EXPECT_FALSE(session.HasPending());

    auto fresh = MakeTimedThrottler(
        AnyOf(EveryNCalls(10), MinInterval(10, IntervalStart::FullInterval)),
        [] {}, ThrottlePolicy::KeepLast);

    for (uint64_t tick = 12; tick <= 30; ++tick)
    {
        clock.Set(tick);
        EXPECT_EQ(session.Execute(), fresh.Execute());
        EXPECT_EQ(session.HasPending(), fresh.HasPending());
    }
}

/// @brief ThreadSafe(Timed(AnyOf)) with a counter-based fake clock and 4 threads: the clock
/// hands out strictly increasing ticks and the lock serializes whole evaluations, so the
/// concurrent flush count and final latch state match a serial tick-by-tick replay exactly.
TEST(ThrottlerIntegration_Test, ThreadSafeTimedFlush)
{
    constexpr uint64_t kThreads = 4;
    constexpr uint64_t kCallsPerThread = 500;
    constexpr uint64_t kTotalCalls = kThreads * kCallsPerThread;

    std::atomic<uint64_t> tickSource(0);
    int flushes = 0;  // incremented under the wrapper's lock only
    auto wrapped = MakeThreadSafe(MakeTimedThrottler(
        AnyOf(EveryNCalls(100), MinInterval(10, IntervalStart::FullInterval)),
        [&flushes] { ++flushes; }, ThrottlePolicy::KeepLast,
        [&tickSource] { return tickSource.fetch_add(1, std::memory_order_relaxed) + 1; }));

    AnyOf oracle(EveryNCalls(100), MinInterval(10, IntervalStart::FullInterval));
    int expectedFlushes = 0;
    bool expectedPending = false;
    for (uint64_t tick = 1; tick <= kTotalCalls; ++tick)
    {
        if (oracle.ShouldExecute(tick))
        {
            ++expectedFlushes;
            expectedPending = false;
        }
        else
        {
            expectedPending = true;  // KeepLast: every newer rejected call supersedes
        }
    }

    std::vector<std::thread> threads;
    for (uint64_t t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&wrapped] {
            for (uint64_t i = 0; i < kCallsPerThread; i++)
                wrapped.Execute();
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(flushes, expectedFlushes);
    EXPECT_EQ(wrapped.HasPending(), expectedPending);
    EXPECT_EQ(wrapped.FlushPending(), expectedPending);
    EXPECT_EQ(flushes, expectedFlushes + (expectedPending ? 1 : 0));
}
