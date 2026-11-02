/// @file ttdtimeline_tape_test.cpp
/// @brief The `tape` regime of CutTimelinePhases: a loader's edge loop reads the ULA thousands of times a frame, and
/// so does a program polling the keyboard in a tight loop (the ZX Next firmware at its menu); the reads make a tape
/// phase only while a tape is in the deck (docs/inprogress/2026-10-31-ttd-timeline-summary).

#include <gtest/gtest.h>

#include <vector>

#include "debugger/ttd/ttdtimeline.h"

namespace
{
std::vector<ttd::TTDTimelinePhase> TightUlaLoop(bool tapeIn)
{
    std::vector<ttd::TTDTimelineFrame> frames(20);
    for (ttd::TTDTimelineFrame& f : frames)
    {
        f.ulaReads = 3000;
        f.tapeIn = tapeIn;
    }
    return ttd::CutTimelinePhases(frames, 100, 10);
}
}  // namespace

TEST(TTDTimelineTape_Test, UlaReadsWithATapeInAreATapePhase)
{
    const auto phases = TightUlaLoop(true);
    ASSERT_EQ(phases.size(), 1u);
    EXPECT_TRUE(phases[0].tags & ttd::kTimelineTape);
}

TEST(TTDTimelineTape_Test, UlaReadsWithNoTapeAreNoTapePhase)
{
    const auto phases = TightUlaLoop(false);
    ASSERT_EQ(phases.size(), 1u);
    EXPECT_FALSE(phases[0].tags & ttd::kTimelineTape) << "a keyboard poll, not a loader";
}
