/// @file presentlead_test.cpp
/// @brief The native presenters' adaptive lead (platform/presentlead.h): a present a refresh late widens the margin
/// only when it was handed over less than a refresh ahead, a long run on time narrows it, both within the bounds.

#include <gtest/gtest.h>

#include "platform/presentlead.h"

namespace
{
constexpr int64_t kPeriod = 16'666'667;  // 60 Hz
constexpr int64_t kLate = kPeriod + PresentLead::kJitterNs + 1;
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
