/// @file presentlead_test.cpp
/// @brief The native presenters' adaptive lead (platform/presentlead.h): a present a refresh late widens the margin
/// only when it was handed over less than a refresh ahead, a long run on time narrows it, both within the bounds.

#include <gtest/gtest.h>

#include "platform/presentlead.h"

namespace
{
constexpr int64_t kPeriod = 16'666'667;  // 60 Hz
// A refresh late whatever offset the screen times have (the lead allows kMaxOffsetNs until it has measured it)
constexpr int64_t kLate = kPeriod + PresentLead::kMaxOffsetNs + PresentLead::kJitterNs + 1;
}  // namespace

TEST(PresentLead_Test, TheStartIsKeptWithinTheBounds)
{
    EXPECT_EQ(PresentLead(0).MarginNs(), PresentLead::kMinNs);
    EXPECT_EQ(PresentLead(1'000'000'000).MarginNs(), PresentLead::kMaxNs);
    EXPECT_EQ(PresentLead(8'000'000).Margin(kPeriod), 8'000'000);
}

TEST(PresentLead_Test, NothingCountsBeforeTheWindowAsksWithItsRefresh)
{
    PresentLead lead(8'000'000);
    lead.Shown(kLate, 1, 0);
    EXPECT_EQ(lead.Late(), 0u);
    EXPECT_EQ(lead.MarginNs(), 8'000'000);
}

TEST(PresentLead_Test, ALatePresentHandedOverLateWidens)
{
    PresentLead lead(8'000'000);
    lead.Margin(kPeriod);
    lead.Shown(kLate, 1, kPeriod - 1);
    EXPECT_EQ(lead.Late(), 1u);
    EXPECT_EQ(lead.MarginNs(), 8'000'000 + PresentLead::kStepNs);
    EXPECT_EQ(lead.LastLate(), 1u);
    EXPECT_EQ(lead.LastLateLag(), 1u);
    // Not known how far ahead: widens too
    lead.Shown(2 * kPeriod + kLate, 2);
    EXPECT_EQ(lead.MarginNs(), 8'000'000 + 2 * PresentLead::kStepNs);
    EXPECT_EQ(lead.LastLate(), 2u);
    EXPECT_EQ(lead.LastLateLag(), 3u);
}

TEST(PresentLead_Test, ALatePresentHandedOverARefreshAheadWasHeldAndDoesNotWiden)
{
    PresentLead lead(8'000'000);
    lead.Margin(kPeriod);
    lead.Shown(kLate, 1, kPeriod);
    EXPECT_EQ(lead.Late(), 1u);
    EXPECT_EQ(lead.LastLate(), 1u);
    EXPECT_EQ(lead.MarginNs(), 8'000'000);
}

TEST(PresentLead_Test, ALongRunOnTimeNarrowsDownToTheBound)
{
    PresentLead lead(PresentLead::kMinNs + PresentLead::kStepNs);
    lead.Margin(kPeriod);
    for (uint32_t i = 1; i < PresentLead::kOnTimeRun; i++)
        lead.Shown(kPeriod / 2, i, 0);
    EXPECT_EQ(lead.MarginNs(), PresentLead::kMinNs + PresentLead::kStepNs);
    EXPECT_EQ(lead.LastOnTime(), PresentLead::kOnTimeRun - 1);
    lead.Shown(kPeriod / 2, PresentLead::kOnTimeRun, 0);
    EXPECT_EQ(lead.MarginNs(), PresentLead::kMinNs);
    for (uint32_t i = 0; i < PresentLead::kOnTimeRun; i++)
        lead.Shown(0, 0, 0);
    EXPECT_EQ(lead.MarginNs(), PresentLead::kMinNs);
    EXPECT_EQ(lead.Late(), 0u);
}

TEST(PresentLead_Test, ALateOneStartsTheRunOnTimeAfreshAndWideningStopsAtTheBound)
{
    PresentLead lead(PresentLead::kMaxNs);
    lead.Margin(kPeriod);
    for (uint32_t i = 0; i + 1 < PresentLead::kOnTimeRun; i++)
        lead.Shown(0);
    lead.Shown(kLate);
    EXPECT_EQ(lead.MarginNs(), PresentLead::kMaxNs);
    for (uint32_t i = 0; i + 1 < PresentLead::kOnTimeRun; i++)
        lead.Shown(0);
    EXPECT_EQ(lead.MarginNs(), PresentLead::kMaxNs);
    // Uncounted presents (0) leave the numbers alone
    EXPECT_EQ(lead.LastLate(), 0u);
    EXPECT_EQ(lead.LastOnTime(), 0u);
}

TEST(PresentLead_Test, ScreenTimesAConstantLittleAfterTheVBlankAreNotLate)
{
    // The Steam Deck under gamescope (2026-11-22): frames shown at the first refresh after their time read 0.3 to
    // 18.7 ms at 16.72 ms a refresh, gamescope's times 1.5-2 ms after the vblank. None is a refresh late
    constexpr int64_t period = 16'720'000;
    PresentLead lead(8'000'000);
    lead.Margin(period);
    for (uint64_t i = 0; i < 400; i++)
        lead.Shown(1'900'000 + static_cast<int64_t>((i * 7919) % 1000) * 16'700, i + 1, 0);
    EXPECT_EQ(lead.Late(), 0u);
    EXPECT_EQ(lead.MarginNs(), 8'000'000);
    EXPECT_NEAR(static_cast<double>(lead.OffsetNs()), 2'700'000.0, 900'000.0);

    // One really a refresh late is still told
    lead.Shown(period + 9'000'000, 401, 0);
    EXPECT_EQ(lead.Late(), 1u);
    EXPECT_EQ(lead.MarginNs(), 9'000'000);
}

TEST(PresentLead_Test, WithoutAnOffsetARefreshLateIsARefreshAndAMillisecond)
{
    // Screen times on the vblank (Metal, DXGI): once measured, the offset is none and the mark is the old one
    PresentLead lead(8'000'000);
    lead.Margin(kPeriod);
    for (uint64_t i = 0; i < 200; i++)
        lead.Shown(static_cast<int64_t>((i * 7919) % 1000) * 16'000, i + 1, 0);
    EXPECT_LT(lead.OffsetNs(), 1'000'000);
    lead.Shown(kPeriod + lead.OffsetNs() + PresentLead::kJitterNs + 1, 201, 0);
    EXPECT_EQ(lead.Late(), 1u);
}

TEST(PresentLead_Test, APresentHeldBySleepIsCountedButNeitherWidensNorEndsTheRunOnTime)
{
    // Before: a present shown 30 s late after a wake widened the margin a step, two or three queued ones two or three,
    // and each step took 600 presents on time to give back
    PresentLead lead(8'000'000);
    lead.Margin(kPeriod);
    for (uint64_t i = 1; i < PresentLead::kOnTimeRun; i++)
        lead.Shown(0, i, 5'000'000);
    lead.Shown(30'000'000'000, PresentLead::kOnTimeRun, 5'000'000);
    lead.Shown(PresentLead::kGapNs + 1, PresentLead::kOnTimeRun + 1, 5'000'000);
    EXPECT_EQ(lead.Late(), 2u);
    EXPECT_EQ(lead.LastLate(), 0u);
    EXPECT_EQ(lead.MarginNs(), 8'000'000);
    lead.Shown(0, PresentLead::kOnTimeRun + 2, 5'000'000);  // the run on time goes on: the 600th narrows
    EXPECT_EQ(lead.MarginNs(), 8'000'000 - PresentLead::kStepNs);
}
