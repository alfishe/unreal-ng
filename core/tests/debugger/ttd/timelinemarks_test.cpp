// Timeline marks (control surfaces actions.md §3): the whole seconds and ten seconds a seek crossed, counted from the
// history's first frame at the machine's frame rate (not an integer), the bookmarks in the interval, strongest first

#include <gtest/gtest.h>

#include <vector>

#include "debugger/ttd/timelinemarks.h"

using ttd::TimelineMark;
using ttd::TimelineMarkKind;
namespace marks = ttd::timeline_marks;

namespace
{
constexpr double kPentagon = 3500000.0 / 71680.0;  // 48.83 frames per second
constexpr double k128K = 3546900.0 / 70908.0;      // 50.02
}  // namespace

TEST(TimelineMarks_Test, SecondsStartAtTheFloorOfTheFrameRateAndNeverDrift)
{
    // 48.83 frames per second: seconds start at frames 0, 49, 98, 147, ... from the first frame
    EXPECT_EQ(marks::SecondOf(48, 0, kPentagon), 0u);
    EXPECT_EQ(marks::SecondOf(49, 0, kPentagon), 1u);
    EXPECT_EQ(marks::SecondOf(97, 0, kPentagon), 1u);
    EXPECT_EQ(marks::SecondOf(98, 0, kPentagon), 2u);
    EXPECT_EQ(marks::SecondOf(147, 0, kPentagon), 3u);
    // An hour in, still the floor of frames / fps
    EXPECT_EQ(marks::SecondOf(175781, 0, kPentagon), 3599u);
    EXPECT_EQ(marks::SecondOf(175782, 0, kPentagon), 3600u);
    // Counted from the history's first frame; before it, second 0
    EXPECT_EQ(marks::SecondOf(1049, 1000, kPentagon), 1u);
    EXPECT_EQ(marks::SecondOf(10, 1000, kPentagon), 0u);
}

TEST(TimelineMarks_Test, ASecondCrossedForwardAndBackIsMarkedAtTheBoundaryNearestTheTarget)
{
    std::vector<TimelineMark> m = marks::Crossed(40, 60, 0, kPentagon, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::Second);
    EXPECT_EQ(m[0].frame, 49u);

    // Back from 100 to 40: seconds 2 and 1 started at 98 and 49; nearest the target is 49
    m = marks::Crossed(100, 40, 0, kPentagon, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::Second);
    EXPECT_EQ(m[0].frame, 49u);

    // Forward over two boundaries: the one nearest the target
    m = marks::Crossed(40, 100, 0, kPentagon, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].frame, 98u);
}

TEST(TimelineMarks_Test, ExactlyOnABoundaryAndInsideASecond)
{
    // Onto the boundary frame forward crosses it; back from it onto the frame before crosses it too
    EXPECT_EQ(marks::Crossed(48, 49, 0, kPentagon, {}).size(), 1u);
    EXPECT_EQ(marks::Crossed(49, 48, 0, kPentagon, {}).size(), 1u);
    // From the boundary further into its second: nothing
    EXPECT_TRUE(marks::Crossed(49, 60, 0, kPentagon, {}).empty());
    EXPECT_TRUE(marks::Crossed(60, 49, 0, kPentagon, {}).empty());
    // No move, no marks
    EXPECT_TRUE(marks::Crossed(49, 49, 0, kPentagon, {}).empty());
}

TEST(TimelineMarks_Test, TenSecondsReplaceTheSecondThere)
{
    // Second 10 starts at ceil(488.28) = 489
    std::vector<TimelineMark> m = marks::Crossed(480, 495, 0, kPentagon, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::TenSeconds);
    EXPECT_EQ(m[0].frame, 489u);
    m = marks::Crossed(495, 480, 0, kPentagon, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::TenSeconds);
    EXPECT_EQ(m[0].frame, 489u);
    // 128K frame period: second 10 at ceil(500.2) = 501
    m = marks::Crossed(495, 505, 0, k128K, {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::TenSeconds);
    EXPECT_EQ(m[0].frame, 501u);
}

TEST(TimelineMarks_Test, BookmarksInTheIntervalAndTheStrongestFirst)
{
    // Forward (from, to]: the bookmark nearest the target; the second boundary 49 is weaker
    std::vector<TimelineMark> m = marks::Crossed(40, 60, 0, kPentagon, {41, 55, 61, 40});
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::Bookmark);
    EXPECT_EQ(m[0].frame, 55u);
    EXPECT_EQ(m[1].kind, TimelineMarkKind::Second);

    // Back [to, from): 40 is the target and counts, 60 is where it left and does not
    m = marks::Crossed(60, 40, 0, kPentagon, {60, 40});
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::Bookmark);
    EXPECT_EQ(m[0].frame, 40u);

    // Within a second, a bookmark alone
    m = marks::Crossed(50, 60, 0, kPentagon, {52});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].kind, TimelineMarkKind::Bookmark);

    // Edge and barrier beat everything
    std::vector<TimelineMark> all = {{TimelineMarkKind::Second, 1},
                                     {TimelineMarkKind::Bookmark, 2},
                                     {TimelineMarkKind::Edge, 3}};
    marks::SortStrongestFirst(all);
    EXPECT_EQ(all[0].kind, TimelineMarkKind::Edge);
    EXPECT_EQ(all[1].kind, TimelineMarkKind::Bookmark);
    EXPECT_EQ(all[2].kind, TimelineMarkKind::Second);
    EXPECT_STREQ(ttd::TimelineMarkKindName(TimelineMarkKind::TenSeconds), "ten_seconds");
    EXPECT_EQ(ttd::TimelineMarkStrength(TimelineMarkKind::Barrier), ttd::TimelineMarkStrength(TimelineMarkKind::Edge));
}
