/// @file presentslot_test.cpp
/// @brief Back-pressure for the queuing presenters (platform/presentslot.h): one present per refresh, a present for a
/// refresh already taken held until the next free one and given up for a newer frame due by then. The negative cases
/// hold or drop nothing: frames slower than the display, one present per refresh asked (the display link, smooth
/// motion), a compositor showing everything a refresh late, no refresh grid, the grid's phase anywhere.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include "platform/presentslot.h"

namespace
{
constexpr int64_t kPeriod = 16'666'667;  // 60 Hz
constexpr int64_t kLead = 8'000'000;
constexpr int64_t kStart = 1'000'000'000;

/// The window on the timer path over `targets` (each frame's own time), driving PresentSlot::Decide / Presented as
/// DeviceScreenGLWindow::renderNative does: it wakes a lead before each frame's time and before a held frame's retry,
/// uploads the newest frame due a lead ahead (the frame source; fresh when its serial is another), hands over what
/// Decide says. The presenter queues: a present is shown at the first refresh at or after its time that is after the
/// refresh shown before (Metal, Vulkan FIFO), `compositorLag` refreshes after that (a compositor behind, D13).
/// `pool` > 0: the presenter's images (Metal's drawables), full while that many presents wait for the screen; a present
/// handed over then blocks the window until the next present goes on screen (`blockedNs` in all). `slot` false: no
/// back-pressure (every fresh frame handed over at once)
struct SimRun
{
    uint64_t handed = 0;
    uint64_t held = 0;
    uint64_t gaveWay = 0;
    uint64_t dropped = 0;   ///< frames never handed over
    int64_t maxLag = 0;     ///< refreshes a frame was shown after its own
    int64_t lastLag = 0;
    int64_t maxQueued = 0;  ///< presents handed over and not shown yet at a hand-over
    uint64_t heldForPool = 0;
    int64_t blockedNs = 0;  ///< the window blocked by the pool full
};

SimRun Simulate(const std::vector<int64_t>& targets, bool slot, int64_t gridPhase = 0, int64_t period = kPeriod,
               int64_t pool = 0, int64_t compositorLag = 0, int64_t leadNs = kLead)
{
    SimRun run;
    PresentSlot slots;
    std::priority_queue<int64_t, std::vector<int64_t>, std::greater<>> wakes;
    for (const int64_t t : targets)
        wakes.push(t - leadNs);
    const int64_t grid = kStart + gridPhase;
    std::vector<bool> handed(targets.size(), false);
    uint64_t uploaded = 0;  // serial = index + 1
    size_t next = 0;
    int64_t lastShown = 0;
    std::vector<int64_t> shownAt;
    int64_t blockedUntil = 0;
    const auto queuedAt = [&](int64_t t) {
        int64_t queued = 0;  // the newest ones are the latest shown
        for (auto it = shownAt.rbegin(); it != shownAt.rend() && *it > t; ++it)
            queued++;
        return queued;
    };
    while (!wakes.empty())
    {
        const int64_t now = std::max(wakes.top(), blockedUntil);
        wakes.pop();
        while (next < targets.size() && targets[next] <= now + leadNs)
            next++;
        if (next == 0)
            continue;
        // A frame held for the pool keeps the texture: no newer one uploaded
        const size_t due = slots.HoldingForPool() ? uploaded - 1 : next - 1;
        const uint64_t serial = due + 1;
        const bool fresh = serial != uploaded;
        uploaded = serial;
        PresentSlot::Turn turn{fresh, false, 0};
        if (slot)
        {
            const bool full = pool > 0 && queuedAt(now) >= pool;
            turn = slots.Decide({fresh, false, false, false, serial, false, targets[due], full}, now, leadNs, grid,
                                period);
        }
        if (!turn.present)
        {
            if (slots.Holding())
                wakes.push(std::max(slots.WakeNs(), now + 1));
            continue;
        }
        if (pool > 0 && queuedAt(now) >= pool)
        {
            // nextDrawable: until the oldest waiting present goes on screen
            const int64_t freed = *(shownAt.end() - pool);
            run.blockedNs += freed - now;
            blockedUntil = freed;
        }
        const int64_t at = turn.retry ? turn.atNs : targets[due];
        const int64_t handedAt = std::max(now, blockedUntil);
        slots.Presented(at, handedAt, grid, period);
        handed[due] = true;
        run.handed++;
        run.maxQueued = std::max<int64_t>(run.maxQueued, queuedAt(handedAt));
        const int64_t own = PresentSlot::RefreshOf(targets[due], grid, period);
        int64_t shown = PresentSlot::RefreshOf(std::max(at, handedAt), grid, period) + compositorLag * period;
        if (lastShown != 0 && shown <= lastShown)
            shown = lastShown + period;
        lastShown = shown;
        shownAt.push_back(shown);
        run.lastLag = (shown - own) / period;
        run.maxLag = std::max(run.maxLag, run.lastLag);
    }
    run.dropped = static_cast<uint64_t>(std::count(handed.begin(), handed.end(), false));
    run.held = slots.Held();
    run.gaveWay = slots.GaveWay();
    run.heldForPool = slots.HeldForPool();
    return run;
}

/// `count` frames at `hz`, the first at kStart + 100 ms, each moved by up to +-`jitterNs` (a fixed pattern)
std::vector<int64_t> Frames(double hz, int count, int64_t jitterNs = 0)
{
    std::vector<int64_t> targets;
    const double period = 1e9 / hz;
    for (int i = 0; i < count; i++)
    {
        const int64_t jitter = jitterNs == 0 ? 0 : ((i * 7919) % 2001 - 1000) * jitterNs / 1000;
        targets.push_back(kStart + 100'000'000 + static_cast<int64_t>(i * period) + jitter);
    }
    for (size_t i = 1; i < targets.size(); i++)
        targets[i] = std::max(targets[i], targets[i - 1] + 1);
    return targets;
}
}  // namespace

TEST(PresentSlot_Test, TheRefreshOfATimeIsTheFirstAtOrAfterIt)
{
    EXPECT_EQ(PresentSlot::RefreshOf(kStart, kStart, kPeriod), kStart);
    EXPECT_EQ(PresentSlot::RefreshOf(kStart + 1, kStart, kPeriod), kStart + kPeriod);
    EXPECT_EQ(PresentSlot::RefreshOf(kStart + kPeriod, kStart, kPeriod), kStart + kPeriod);
    EXPECT_EQ(PresentSlot::RefreshOf(kStart - 1, kStart, kPeriod), kStart);
    EXPECT_EQ(PresentSlot::RefreshOf(kStart - kPeriod, kStart, kPeriod), kStart - kPeriod);
    EXPECT_EQ(PresentSlot::RefreshOf(kStart - kPeriod - 1, kStart, kPeriod), kStart - kPeriod);
}

TEST(PresentSlot_Test, ASecondPresentForTheSameRefreshIsHeldForTheNext)
{
    PresentSlot slot;
    const int64_t at = kStart + kPeriod / 4;
    EXPECT_FALSE(slot.Ask(at, kStart, kStart, kPeriod).hold) << "nothing taken yet";
    slot.Handed(at, kStart, kStart, kPeriod);
    EXPECT_EQ(slot.TakenNs(), kStart + kPeriod);
    const PresentSlot::Plan plan = slot.Ask(at + kPeriod / 2, kStart, kStart, kPeriod);
    EXPECT_TRUE(plan.hold);
    EXPECT_EQ(plan.retryAtNs, kStart + 2 * kPeriod - kPeriod / 2) << "half a refresh before the next free one";
    EXPECT_FALSE(slot.Ask(plan.retryAtNs, kStart, kStart, kPeriod).hold) << "its retry asks for a free refresh";
    // An earlier time than the one taken is held as well (the queue would show it after the one taken)
    EXPECT_TRUE(slot.Ask(at - kPeriod, kStart, kStart, kPeriod).hold);
}

TEST(PresentSlot_Test, PresentsAtOnceTakeTheRefreshAfterNow)
{
    PresentSlot slot;
    const int64_t now = kStart + 3'000'000;
    slot.Handed(0, now, kStart, kPeriod);
    EXPECT_EQ(slot.TakenNs(), kStart + kPeriod);
    EXPECT_TRUE(slot.Ask(0, now + 5'000'000, kStart, kPeriod).hold) << "another at once in the same refresh";
    EXPECT_FALSE(slot.Ask(0, now + kPeriod, kStart, kPeriod).hold) << "the next refresh's";
}

/// Negative: a present for a refresh after the one taken, a later handed-over time never moves the refresh taken back
TEST(PresentSlot_Test, ALaterRefreshIsFreeAndTheTakenOneNeverGoesBack)
{
    PresentSlot slot;
    slot.Handed(kStart + 3 * kPeriod, kStart, kStart, kPeriod);
    EXPECT_FALSE(slot.Ask(kStart + 3 * kPeriod + 1, kStart, kStart, kPeriod).hold);
    slot.Handed(kStart + kPeriod, kStart, kStart, kPeriod);
    EXPECT_EQ(slot.TakenNs(), kStart + 3 * kPeriod);
}

/// Negative: no refresh grid (VRR: each frame at its own time; none seen yet) holds nothing and forgets the refresh
TEST(PresentSlot_Test, WithoutAGridNothingIsHeld)
{
    PresentSlot slot;
    slot.Handed(kStart, kStart, kStart, kPeriod);
    EXPECT_FALSE(slot.Ask(kStart, kStart, kStart, 0).hold);
    slot.Handed(kStart, kStart, kStart, 0);
    EXPECT_EQ(slot.TakenNs(), 0);
    EXPECT_FALSE(slot.Ask(kStart, kStart, kStart, kPeriod).hold);
}

TEST(PresentSlot_Test, ResetForgetsTheRefreshTaken)
{
    PresentSlot slot;
    slot.Handed(kStart, kStart, kStart, kPeriod);
    slot.Reset();
    EXPECT_FALSE(slot.Ask(kStart, kStart, kStart, kPeriod).hold);
}

/// The pile: frames faster than the display (turbo, a machine above the display's rate). Queued as handed over, the
/// delay grows a refresh at a time; held, the newest frame takes each refresh, the delay stays within a refresh
TEST(PresentSlot_Test, FramesFasterThanTheDisplayKeepTheirDelay)
{
    for (const double hz : {61.0, 75.0, 100.0, 120.0})
    {
        const std::vector<int64_t> frames = Frames(hz, static_cast<int>(hz * 10));
        const SimRun queued = Simulate(frames, false);
        const SimRun held = Simulate(frames, true);
        EXPECT_GT(queued.lastLag, 5) << hz << " Hz: without it the delay grows";
        EXPECT_LE(held.maxLag, 1) << hz << " Hz";
        EXPECT_LE(held.maxQueued, 2) << hz << " Hz: no pile in the queue";
        // About one frame a refresh shown, the rest given up
        const double refreshes = 10.0 * 1e9 / static_cast<double>(kPeriod);
        EXPECT_NEAR(static_cast<double>(held.handed), refreshes, refreshes * 0.03) << hz << " Hz";
        EXPECT_EQ(held.handed + held.dropped, frames.size());
        EXPECT_GT(held.gaveWay, 0u) << hz << " Hz";
        EXPECT_LE(held.gaveWay, held.dropped) << hz << " Hz: a frame that gave way is never shown";
    }
}

/// Negative: frames slower than the display are always more than a refresh apart: none held, none dropped, the delay
/// as without it, wherever the grid's phase lies and with the frame clock's jitter
TEST(PresentSlot_Test, FramesSlowerThanTheDisplayAreNeverHeld)
{
    for (const double hz : {48.83, 50.0, 50.08, 59.0})
        for (const int64_t phase : {int64_t{0}, kPeriod / 3, kPeriod / 2, kPeriod - 1})
        {
            const std::vector<int64_t> frames = Frames(hz, 3000, hz < 59.0 ? 500'000 : 0);
            const SimRun run = Simulate(frames, true, phase);
            EXPECT_EQ(run.held, 0u) << hz << " Hz, phase " << phase;
            EXPECT_EQ(run.gaveWay, 0u) << hz << " Hz, phase " << phase;
            EXPECT_EQ(run.dropped, 0u) << hz << " Hz, phase " << phase;
            EXPECT_EQ(run.maxLag, Simulate(frames, false, phase).maxLag) << hz << " Hz, phase " << phase;
        }
}

/// Negative: frames at the display's rate with the frame clock's jitter: one now and then lands on the refresh of the
/// one before. Held, it goes at the next refresh, where the queue showed it: nothing dropped, no more delay
TEST(PresentSlot_Test, JitterAtTheDisplaysRateLosesNoFrame)
{
    for (const int64_t jitter : {int64_t{500'000}, int64_t{2'000'000}, int64_t{4'000'000}})
    {
        const std::vector<int64_t> frames = Frames(1e9 / static_cast<double>(kPeriod), 3000, jitter);
        const SimRun held = Simulate(frames, true, kPeriod / 2);
        const SimRun queued = Simulate(frames, false, kPeriod / 2);
        EXPECT_EQ(held.dropped, 0u) << "jitter " << jitter;
        EXPECT_LE(held.maxLag, queued.maxLag) << "jitter " << jitter;
    }
}

/// Negative: one present asked per refresh (the display link's ticks, smooth motion's pictures: half a refresh before
/// each), whatever the screen shows them at: a compositor a refresh behind (D13: every present 25 ms after its time)
/// is not seen by the times asked, nothing is held
TEST(PresentSlot_Test, OnePresentPerRefreshIsNeverHeld)
{
    PresentSlot slot;
    for (int k = 1; k <= 10000; k++)
    {
        const int64_t refresh = kStart + k * kPeriod;
        const int64_t now = refresh - kPeriod / 2 - kLead;
        const int64_t at = refresh - kPeriod / 2;
        ASSERT_FALSE(slot.Ask(at, now, kStart, kPeriod).hold) << "refresh " << k;
        slot.Handed(at, now, kStart, kPeriod);
    }
}

/// The grid as the presenter last saw it moves on (a newer screen time each refresh, the period measured anew): the
/// refreshes stay where they were, a frame a refresh on keeps its own
TEST(PresentSlot_Test, AGridSeenLaterAgreesWithTheOneBefore)
{
    PresentSlot slot;
    slot.Handed(kStart + kPeriod / 2, kStart, kStart, kPeriod);
    const int64_t laterGrid = kStart + 5 * kPeriod;
    EXPECT_FALSE(slot.Ask(kStart + kPeriod + kPeriod / 2, kStart, laterGrid, kPeriod).hold);
    EXPECT_TRUE(slot.Ask(kStart + kPeriod - 1, kStart, laterGrid, kPeriod).hold);
}

namespace
{
/// A slot whose refresh at kStart + kPeriod is taken; the grid at kStart
PresentSlot Taken()
{
    PresentSlot slot;
    slot.Presented(kStart + kPeriod / 4, kStart, kStart, kPeriod);
    return slot;
}
PresentSlot::Tick Fresh(uint64_t serial, int64_t targetNs)
{
    return {true, false, false, false, serial, false, targetNs};
}
PresentSlot::Tick Again(uint64_t serial)
{
    return {false, false, false, false, serial, false, 0};
}
constexpr int64_t kSecond = kStart + kPeriod / 2;  // a frame for the refresh taken
}  // namespace

TEST(PresentSlot_Test, AHeldFrameWaitsForItsWakeThenGoesAtTheFreeRefresh)
{
    PresentSlot slot = Taken();
    EXPECT_FALSE(slot.Decide(Fresh(2, kSecond), kStart, kLead, kStart, kPeriod).present);
    ASSERT_TRUE(slot.Holding());
    EXPECT_EQ(slot.Held(), 1u);
    const int64_t wake = slot.HeldAtNs() - kLead;
    EXPECT_FALSE(slot.Decide(Again(2), wake - 1, kLead, kStart, kPeriod).present) << "not before its wake";
    EXPECT_TRUE(slot.Holding());
    EXPECT_EQ(slot.Held(), 1u) << "the same frame is counted once";
    const PresentSlot::Turn turn = slot.Decide(Again(2), wake, kLead, kStart, kPeriod);
    EXPECT_TRUE(turn.present);
    EXPECT_TRUE(turn.retry);
    EXPECT_EQ(turn.atNs, kStart + 2 * kPeriod - kPeriod / 2);
    slot.Presented(turn.atNs, wake, kStart, kPeriod);
    EXPECT_FALSE(slot.Holding());
    EXPECT_EQ(slot.GaveWay(), 0u);
}

TEST(PresentSlot_Test, ANewerFrameTakesTheRefreshAndTheHeldOneGivesWay)
{
    PresentSlot slot = Taken();
    slot.Decide(Fresh(2, kSecond), kStart, kLead, kStart, kPeriod);
    // A newer frame for the next free refresh: planned at its own time, the one held never shown
    const PresentSlot::Turn turn =
        slot.Decide(Fresh(3, kStart + 2 * kPeriod - 1), kStart + 5'000'000, kLead, kStart, kPeriod);
    EXPECT_TRUE(turn.present);
    EXPECT_FALSE(turn.retry);
    EXPECT_EQ(slot.GaveWay(), 1u);
    EXPECT_FALSE(slot.Holding());
    // A newer frame for the refresh taken as well: held in its place
    PresentSlot again = Taken();
    again.Decide(Fresh(2, kSecond), kStart, kLead, kStart, kPeriod);
    EXPECT_FALSE(again.Decide(Fresh(3, kSecond + 1), kStart + 1'000'000, kLead, kStart, kPeriod).present);
    EXPECT_EQ(again.GaveWay(), 1u);
    EXPECT_EQ(again.Held(), 2u);
    EXPECT_TRUE(again.Holding());
}

/// Negative: a forced repaint goes at once as always (Decide does not stop it) and takes the frame held with it
TEST(PresentSlot_Test, AForcedRepaintEndsTheHold)
{
    PresentSlot slot = Taken();
    slot.Decide(Fresh(2, kSecond), kStart, kLead, kStart, kPeriod);
    const PresentSlot::Turn turn =
        slot.Decide({false, true, false, false, 2, false, kSecond}, kStart + 1'000'000, kLead, kStart, kPeriod);
    EXPECT_FALSE(turn.retry);
    EXPECT_FALSE(slot.Holding());
    EXPECT_EQ(slot.GaveWay(), 0u);
    // A fresh frame with the repaint: handed over with it, never held
    PresentSlot fresh = Taken();
    EXPECT_TRUE(fresh.Decide({true, true, false, false, 2, false, kSecond}, kStart, kLead, kStart, kPeriod).present);
    EXPECT_FALSE(fresh.Holding());
}

/// Negative: the display link's tick (one refresh each) and smooth motion's pictures are never held; a frame held
/// when the ticks come again goes at the next tick
TEST(PresentSlot_Test, TheLinksTicksAndSmoothMotionAreNeverHeld)
{
    PresentSlot slot = Taken();
    EXPECT_TRUE(slot.Decide({true, false, true, false, 2, false, kSecond}, kStart, kLead, kStart, kPeriod).present);
    EXPECT_TRUE(slot.Decide({true, false, false, true, 2, false, kSecond}, kStart, kLead, kStart, kPeriod).present);
    EXPECT_EQ(slot.Held(), 0u);
    slot.Decide(Fresh(3, kSecond), kStart, kLead, kStart, kPeriod);
    ASSERT_TRUE(slot.Holding());
    const PresentSlot::Turn tick =
        slot.Decide({false, false, true, false, 3, false, kSecond}, kStart + 1'000'000, kLead, kStart, kPeriod);
    EXPECT_TRUE(tick.present) << "the tick shows the frame held";
    EXPECT_FALSE(tick.retry) << "at the tick's own refresh";
    EXPECT_FALSE(slot.Holding());
}

/// Negative: a source without serials cannot name a frame held, no grid (VRR) holds nothing: handed over as before
TEST(PresentSlot_Test, NoSerialsOrNoGridNeverHold)
{
    PresentSlot slot = Taken();
    EXPECT_TRUE(slot.Decide(Fresh(0, kSecond), kStart, kLead, kStart, kPeriod).present);
    EXPECT_TRUE(slot.Decide(Fresh(2, kSecond), kStart, kLead, kStart, 0).present);
    EXPECT_EQ(slot.Held(), 0u);
}

/// A low-latency frame (at once) for the refresh taken is held as well; its retry asks for the next free refresh
TEST(PresentSlot_Test, AFrameAtOnceForATakenRefreshRetriesAtTheNextOne)
{
    PresentSlot slot;
    slot.Presented(0, kStart + 2'000'000, kStart, kPeriod);
    EXPECT_FALSE(slot.Decide({true, false, false, false, 2, true, 0}, kStart + 6'000'000, kLead, kStart, kPeriod).present);
    const PresentSlot::Turn turn =
        slot.Decide({false, false, false, false, 2, true, 0}, slot.HeldAtNs() - kLead, kLead, kStart, kPeriod);
    EXPECT_TRUE(turn.retry);
    EXPECT_EQ(turn.atNs, kStart + 2 * kPeriod - kPeriod / 2);
}

/// Hidden, a presenter made anew: nothing taken, nothing held, the counts kept
TEST(PresentSlot_Test, ResetEndsTheHoldAndKeepsTheCounts)
{
    PresentSlot slot = Taken();
    slot.Decide(Fresh(2, kSecond), kStart, kLead, kStart, kPeriod);
    slot.Reset();
    EXPECT_FALSE(slot.Holding());
    EXPECT_EQ(slot.TakenNs(), 0);
    EXPECT_EQ(slot.Held(), 1u);
    EXPECT_TRUE(slot.Decide(Fresh(3, kSecond), kStart, kLead, kStart, kPeriod).present);
}

/// A compositor a refresh behind keeps one more drawable: frames faster than the display then fill Metal's pool, and
/// every present blocked the window until the next refresh (the owner's Mac, 2026-11-27: 12-16 ms a present). Held
/// for the pool instead, the window never blocks and the delay stays the compositor's
TEST(PresentSlot_Test, FramesFasterThanTheDisplayNeverBlockOnAFullPool)
{
    for (const double hz : {61.0, 75.0, 120.0})
    {
        const std::vector<int64_t> frames = Frames(hz, static_cast<int>(hz * 4));
        const SimRun blocked = Simulate(frames, false, 0, kPeriod, 3, 1, 36'000'000);
        const SimRun held = Simulate(frames, true, 0, kPeriod, 3, 1, 36'000'000);
        EXPECT_GT(blocked.blockedNs, 1'000'000'000) << hz << " Hz: the unguarded window blocks";
        EXPECT_EQ(held.blockedNs, 0) << hz << " Hz";
        EXPECT_LE(held.maxLag, 2) << hz << " Hz: the frame's refresh, and the compositor's";
        EXPECT_LE(held.maxQueued, 2) << hz << " Hz";
        EXPECT_GT(held.gaveWay, 0u) << hz << " Hz";
    }
}

/// Negative: frames slower than the display with the compositor behind, and frames at the display's rate on time:
/// no frame lost, no window blocked, the delay of every frame as without the guard
TEST(PresentSlot_Test, APoolThatNeverFillsHoldsNothing)
{
    for (const double hz : {48.83, 50.0, 59.0})
        for (const int64_t lag : {int64_t{0}, int64_t{1}})
        {
            const std::vector<int64_t> frames = Frames(hz, static_cast<int>(hz * 4), 2'000'000);
            const SimRun plain = Simulate(frames, false, 0, kPeriod, 3, lag);
            const SimRun guarded = Simulate(frames, true, 0, kPeriod, 3, lag);
            EXPECT_EQ(guarded.dropped, 0u) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.heldForPool, 0u) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.blockedNs, 0) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.maxLag, plain.maxLag) << hz << " Hz, lag " << lag;
        }
}

/// Negative, the owner's Mac: a lead over two refreshes (36 ms) and the compositor a refresh behind fill the pool with
/// frames slower than the display too. Held for the pool, no frame is lost and none shown later than the blocking
/// window showed it; the window never blocks
TEST(PresentSlot_Test, AWideLeadFillsThePoolAndLosesNoFrame)
{
    constexpr int64_t kWideLead = 36'000'000;
    for (const double hz : {48.83, 50.0, 59.0, 59.94})
        for (const int64_t lag : {int64_t{0}, int64_t{1}})
        {
            const std::vector<int64_t> frames = Frames(hz, static_cast<int>(hz * 4), 2'000'000);
            const SimRun plain = Simulate(frames, false, 0, kPeriod, 3, lag, kWideLead);
            const SimRun guarded = Simulate(frames, true, 0, kPeriod, 3, lag, kWideLead);
            EXPECT_EQ(plain.dropped, 0u) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.dropped, 0u) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.gaveWay, 0u) << hz << " Hz, lag " << lag;
            EXPECT_EQ(guarded.blockedNs, 0) << hz << " Hz, lag " << lag;
            EXPECT_LE(guarded.maxLag, plain.maxLag) << hz << " Hz, lag " << lag;
            if (lag == 1 && hz > 59)
                EXPECT_GT(plain.blockedNs, 0) << hz << " Hz: the case is the one measured (the pool full)";
        }
}

TEST(PresentSlot_Test, AFrameHeldForThePoolGoesJustAfterTheNextRefreshAtItsOwnTime)
{
    PresentSlot slot;
    const int64_t now = kStart + 3'000'000;
    const int64_t own = kStart + 2 * kPeriod + 1'000'000;
    PresentSlot::Tick tick = Fresh(2, own);
    tick.poolFull = true;
    EXPECT_FALSE(slot.Decide(tick, now, kLead, kStart, kPeriod).present);
    ASSERT_TRUE(slot.Holding());
    EXPECT_EQ(slot.HeldForPool(), 1u);
    EXPECT_EQ(slot.WakeNs(), kStart + kPeriod + PresentSlot::kPoolWakeNs);
    // Still full at the wake: held again, counted once
    PresentSlot::Tick again = Again(2);
    again.poolFull = true;
    EXPECT_FALSE(slot.Decide(again, slot.WakeNs(), kLead, kStart, kPeriod).present);
    EXPECT_EQ(slot.Held(), 1u);
    EXPECT_EQ(slot.WakeNs(), kStart + 2 * kPeriod + PresentSlot::kPoolWakeNs);
    // A drawable back: the frame goes, asking for its own time
    const PresentSlot::Turn turn = slot.Decide(Again(2), slot.WakeNs(), kLead, kStart, kPeriod);
    EXPECT_TRUE(turn.present);
    EXPECT_TRUE(turn.retry);
    EXPECT_EQ(turn.atNs, own);
}

/// Negative: a forced repaint, the link's tick, smooth motion, no grid or no serials: never held for the pool (they
/// block as before)
TEST(PresentSlot_Test, OnlyFramesAtTheirOwnTimesAreHeldForThePool)
{
    const auto full = [](PresentSlot::Tick tick) {
        tick.poolFull = true;
        return tick;
    };
    PresentSlot slot;
    EXPECT_TRUE(slot.Decide(full({true, true, false, false, 2, false, kSecond}), kStart, kLead, kStart, kPeriod).present);
    EXPECT_TRUE(slot.Decide(full({true, false, true, false, 3, false, kSecond}), kStart, kLead, kStart, kPeriod).present);
    EXPECT_TRUE(slot.Decide(full({true, false, false, true, 4, false, kSecond}), kStart, kLead, kStart, kPeriod).present);
    EXPECT_TRUE(slot.Decide(full(Fresh(5, kSecond)), kStart, kLead, kStart, 0).present);
    EXPECT_TRUE(slot.Decide(full(Fresh(0, kSecond)), kStart, kLead, kStart, kPeriod).present);
    EXPECT_EQ(slot.Held(), 0u);
    EXPECT_FALSE(slot.Holding());
}
