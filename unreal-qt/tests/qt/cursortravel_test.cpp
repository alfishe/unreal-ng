// CursorTravel: the Windows capture backend's travel from absolute cursor samples.
// Local Windows sees its own warp at once; a remote client sends absolute positions of
// its own cursor and follows a warp late or never - neither may report the warp as
// travel, and over RDP there is no warp at all (the guest pointer jumped diagonally).

#include <gtest/gtest.h>

#include "platform/windows/cursortravel.h"

namespace
{
using P = CursorTravel::Point;

class CursorTravel_Test : public ::testing::Test
{
protected:
    void SetUp() override { _travel.Start(_centre, P{100, 100}); }

    /// Feed a sample; sums the travel, counts the warps
    CursorTravel::Step Feed(int x, int y)
    {
        const CursorTravel::Step step = _travel.Feed(P{x, y}, _now);
        _sumX += step.dx;
        _sumY += step.dy;
        _warps += step.warp ? 1 : 0;
        _now += 8;  // 125 Hz
        return step;
    }

    const P _centre{1000, 500};
    CursorTravel _travel;
    int64_t _now = 1000;
    int _sumX = 0;
    int _sumY = 0;
    int _warps = 0;
};
}  // namespace

/// Within the zone: consecutive differences, no warp
TEST_F(CursorTravel_Test, TravelIsTheDifferenceBetweenSamples)
{
    Feed(1010, 500);
    Feed(1025, 495);
    Feed(1020, 470);
    EXPECT_EQ(_sumX, 20);
    EXPECT_EQ(_sumY, -30);
    EXPECT_EQ(_warps, 0);
}

/// Local Windows: the warp takes effect at once, the next sample is the centre plus new travel
TEST_F(CursorTravel_Test, LocalWarpIsSeamless)
{
    for (int x = 1040; x <= 1120; x += 40)
        Feed(x, 500);  // 1120 strays beyond the zone: warp
    EXPECT_EQ(_warps, 1);
    EXPECT_EQ(_sumX, 120);

    Feed(1000, 500);  // our own warp's echo
    Feed(1030, 500);
    Feed(1060, 500);
    EXPECT_EQ(_sumX, 180) << "every pixel once, nothing for the warp";
    EXPECT_EQ(_warps, 1);
}

/// RDP: after the warp the client keeps sending positions of its own cursor for a round trip
/// (pre-warp, still moving on from 1120), then catches up with the centre
TEST_F(CursorTravel_Test, RemoteClientCatchingUpIsNotTravel)
{
    Feed(1060, 500);
    Feed(1120, 500);  // warp
    ASSERT_EQ(_warps, 1);
    ASSERT_EQ(_sumX, 120);

    Feed(1000, 500);  // the server-side echo of SetCursorPos
    Feed(1125, 500);  // the client has not seen the warp: +5 real travel
    Feed(1130, 500);  // +5
    Feed(1135, 500);  // +5
    Feed(1010, 500);  // the client caught up: centre + its own +10 of travel since
    Feed(1020, 500);  // +10
    EXPECT_EQ(_sumX, 120 + 10 + 10) << "the jumps to and from the centre are dropped; the travel in between and after counts";
    EXPECT_EQ(_sumY, 0);
    EXPECT_EQ(_warps, 1) << "no second warp while the client catches up";
}

/// A client that never follows the warp: travel goes on from its positions, and a warp is
/// tried again after the hold instead of on every sample
TEST_F(CursorTravel_Test, ClientThatNeverFollowsIsWarpedAgainOnlyAfterTheHold)
{
    Feed(1120, 500);  // warp
    for (int i = 0; i < 10; ++i)
        Feed(1121 + i, 500);  // 80 ms of pre-warp positions
    EXPECT_EQ(_warps, 1);
    // The first sample after the warp jumps from the centre back by about the warp vector: dropped
    // with its one pixel of travel; the other nine count
    EXPECT_EQ(_sumX, 120 + 9) << "travel goes on from where the client is";

    _now += CursorTravel::kRewarpHoldMs;
    Feed(1131, 500);
    EXPECT_EQ(_warps, 2) << "tried again after the hold";
}

/// A real fast move right after a warp is not mistaken for the warp unless it is about the warp vector
TEST_F(CursorTravel_Test, FastMoveAfterWarpCounts)
{
    Feed(1120, 500);  // warp vector (-120, 0)
    Feed(1000, 500);  // echo
    Feed(1000, 560);  // a fast move down, nothing like the warp
    EXPECT_EQ(_sumY, 60);
    Feed(1000, 500);
    EXPECT_EQ(_sumY, 0);
}

/// RDP: the client never follows a warp, so there is none. Its cursor is where the capture
/// click was, not at the centre: that offset must not show up as travel (it did, on both
/// axes of every sample, so a straight horizontal move jumped diagonally)
TEST_F(CursorTravel_Test, RemoteSessionNeverWarpsAndIgnoresTheClickOffset)
{
    _travel.Start(_centre, P{100, 100}, true);
    Feed(870, 590);  // the client cursor where the click was: the baseline
    EXPECT_EQ(_sumX, 0);
    EXPECT_EQ(_sumY, 0);

    for (int x = 880; x <= 1200; x += 10)
        Feed(x, 590);  // a straight horizontal move, far beyond the zone
    EXPECT_EQ(_sumX, 330);
    EXPECT_EQ(_sumY, 0) << "horizontal stays horizontal";
    EXPECT_EQ(_warps, 0) << "never warped";
}
