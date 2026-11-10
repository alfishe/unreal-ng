#include "stdafx.h"
#include "gtest/gtest.h"

#include "emulator/io/mouse/touchpointertracker.h"

/// A touchscreen as a relative pointer (the Steam Deck's gamescope hands it over as an absolute pointer: motion and a
/// button press, no touch events): travel from the contact's own last position, a tap is a click. Pure numbers, no Qt.

TEST(TouchPointerTracker_Test, TheFingersTravelMovesTheCursorNotWhereItLands)
{
    TouchPointerTracker tracker;
    // The finger lands far from anywhere: nothing moves, however far from the centre
    tracker.Down(900.0, 600.0, 1000);
    EXPECT_TRUE(tracker.IsDown());

    TouchPointerTracker::Travel t = tracker.Move(905.0, 598.0);
    EXPECT_DOUBLE_EQ(t.dx, 5.0);
    EXPECT_DOUBLE_EQ(t.dy, -2.0);
    t = tracker.Move(925.0, 598.0);
    EXPECT_DOUBLE_EQ(t.dx, 20.0);
    EXPECT_DOUBLE_EQ(t.dy, 0.0);
    EXPECT_FALSE(tracker.Up(1500)) << "a drag is not a tap";
    EXPECT_FALSE(tracker.IsDown());
}

TEST(TouchPointerTracker_Test, ANewContactAnchorsAfreshWithNoLeapFromTheLastOne)
{
    TouchPointerTracker tracker;
    tracker.Down(100.0, 100.0, 0);
    tracker.Move(140.0, 100.0);
    tracker.Up(100);

    tracker.Down(1100.0, 700.0, 2000);  // the other side of the screen
    TouchPointerTracker::Travel t = tracker.Move(1103.0, 700.0);
    EXPECT_DOUBLE_EQ(t.dx, 3.0) << "from where this finger went down, not from the last contact";
    EXPECT_DOUBLE_EQ(t.dy, 0.0);
}

TEST(TouchPointerTracker_Test, MovesWithoutAContactAreIgnored)
{
    TouchPointerTracker tracker;
    TouchPointerTracker::Travel t = tracker.Move(500.0, 300.0);  // the pointer hovering or jumping to a touch
    EXPECT_DOUBLE_EQ(t.dx, 0.0);
    EXPECT_DOUBLE_EQ(t.dy, 0.0);
    t = tracker.Move(900.0, 100.0);
    EXPECT_DOUBLE_EQ(t.dx, 0.0);
    EXPECT_FALSE(tracker.Up(10)) << "an Up without a Down is no tap";
}

TEST(TouchPointerTracker_Test, AReportCatchingUpWithTheTouchMovesTheAnchorNotTheCursor)
{
    TouchPointerTracker tracker;
    // The press arrives at the old pointer position, then the pointer jumps to the finger
    tracker.Down(100.0, 100.0, 0);
    TouchPointerTracker::Travel t = tracker.Move(1000.0, 500.0);
    EXPECT_DOUBLE_EQ(t.dx, 0.0) << "no finger travels a thousand pixels in one report";
    EXPECT_DOUBLE_EQ(t.dy, 0.0);
    t = tracker.Move(1008.0, 500.0);
    EXPECT_DOUBLE_EQ(t.dx, 8.0) << "the travel counts from where the pointer landed";
}

TEST(TouchPointerTracker_Test, AShortStillContactIsATapAndALongOrMovedOneIsNot)
{
    TouchPointerTracker tracker;
    tracker.Down(300.0, 300.0, 1000);
    tracker.Move(304.0, 303.0);  // a finger never lands perfectly still
    EXPECT_TRUE(tracker.Up(1200));

    tracker.Down(300.0, 300.0, 5000);
    EXPECT_FALSE(tracker.Up(5000 + TouchPointerTracker::kTapMaxMs + 1)) << "held too long: not a tap";

    tracker.Down(300.0, 300.0, 9000);
    tracker.Move(330.0, 300.0);  // past the slop
    tracker.Move(300.0, 300.0);  // and back to where it began: still a drag, it moved
    EXPECT_FALSE(tracker.Up(9100));
}

TEST(TouchPointerTracker_Test, ResetForgetsTheContact)
{
    TouchPointerTracker tracker;
    tracker.Down(10.0, 10.0, 0);
    tracker.Reset();
    EXPECT_FALSE(tracker.IsDown());
    EXPECT_DOUBLE_EQ(tracker.Move(30.0, 10.0).dx, 0.0);
    EXPECT_FALSE(tracker.Up(10));
}
