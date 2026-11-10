/// @file presenterrecovery_test.cpp
/// @brief The screen window's native presenter through its failures (platform/presenterrecovery.h): restarts after a
/// while of failures or at once when the device is gone, retries of making it, and GL once restarts or tries are used
/// up.

#include <gtest/gtest.h>

#include "platform/presenterrecovery.h"

namespace
{
using Create = PresenterRecovery::CreateFailure;
using Begin = PresenterRecovery::BeginFailure;
constexpr int64_t kSecond = 1'000'000'000;
}  // namespace

TEST(PresenterRecovery_Test, TheFirstPresenterNotMadeFallsBackAtOnce)
{
    PresenterRecovery recovery;
    EXPECT_FALSE(recovery.GlOnly());
    EXPECT_EQ(recovery.CreateFailed(), Create::FallBack);
    EXPECT_TRUE(recovery.GlOnly());
}

TEST(PresenterRecovery_Test, FailingAWhileRestartsThenPresentingEndsTheRun)
{
    PresenterRecovery recovery;
    recovery.Created();
    EXPECT_EQ(recovery.BeginFailed(10 * kSecond, false), Begin::First);
    EXPECT_EQ(recovery.FailingSinceNs(), 10 * kSecond);
    EXPECT_EQ(recovery.BeginFailed(11 * kSecond, false), Begin::Wait);
    EXPECT_EQ(recovery.Presented(11 * kSecond + 500), kSecond + 500);
    EXPECT_EQ(recovery.Presented(12 * kSecond), 0);

    EXPECT_EQ(recovery.BeginFailed(20 * kSecond, false), Begin::First);
    EXPECT_EQ(recovery.BeginFailed(20 * kSecond + PresenterRecovery::kRestartAfterNs, false), Begin::Wait);
    EXPECT_EQ(recovery.BeginFailed(20 * kSecond + PresenterRecovery::kRestartAfterNs + 1, false), Begin::Restart);
    EXPECT_EQ(recovery.Failures(), 3u);
    EXPECT_EQ(recovery.FailingSinceNs(), 0);
    EXPECT_EQ(recovery.RetriesLeft(), PresenterRecovery::kRetries);
    EXPECT_FALSE(recovery.GlOnly());
}

TEST(PresenterRecovery_Test, ADeviceGoneRestartsAtOnce)
{
    PresenterRecovery recovery;
    recovery.Created();
    EXPECT_EQ(recovery.BeginFailed(kSecond, true), Begin::Restart);
    EXPECT_EQ(recovery.RetriesLeft(), PresenterRecovery::kRetries);
}

TEST(PresenterRecovery_Test, AfterARestartMakingItIsTriedThenGlPresents)
{
    PresenterRecovery recovery;
    recovery.Created();
    ASSERT_TRUE(recovery.Restart(kSecond));
    int newWindows = 0;
    for (int i = 0; i < PresenterRecovery::kRetries; i++)
    {
        const Create next = recovery.CreateFailed();
        ASSERT_NE(next, Create::FallBack) << i;
        if (next == Create::RetryNewWindow)
        {
            newWindows++;
            EXPECT_EQ(i + 1, PresenterRecovery::kNewWindowAfter);
        }
    }
    EXPECT_EQ(newWindows, 1);
    EXPECT_EQ(recovery.CreateFailed(), Create::FallBack);
    EXPECT_TRUE(recovery.GlOnly());
}

TEST(PresenterRecovery_Test, MadeAgainTheTriesStartAfreshWithTheNextRestart)
{
    PresenterRecovery recovery;
    ASSERT_TRUE(recovery.Restart(kSecond));
    EXPECT_EQ(recovery.CreateFailed(), Create::Retry);
    recovery.Created();
    EXPECT_EQ(recovery.RetriesLeft(), 0);
    ASSERT_TRUE(recovery.Restart(2 * kSecond));
    EXPECT_EQ(recovery.RetriesLeft(), PresenterRecovery::kRetries);
}

TEST(PresenterRecovery_Test, TooManyRestartsWithinTheWindowFallBack)
{
    PresenterRecovery recovery;
    recovery.Created();
    // A presenter made each time whose BeginFrame keeps failing: restarted every kRestartAfterNs, not forever
    int64_t now = kSecond;
    int restarts = 0;
    Begin result = Begin::Wait;
    for (int round = 0; round < 10 && result != Begin::FallBack; round++)
    {
        EXPECT_EQ(recovery.BeginFailed(now, false), Begin::First);
        now += PresenterRecovery::kRestartAfterNs + 1;
        result = recovery.BeginFailed(now, false);
        if (result == Begin::Restart)
        {
            restarts++;
            recovery.Created();
        }
    }
    EXPECT_EQ(result, Begin::FallBack);
    EXPECT_EQ(restarts, PresenterRecovery::kMaxRestarts);
    EXPECT_TRUE(recovery.GlOnly());
}

TEST(PresenterRecovery_Test, RestartsOutsideTheWindowAreForgotten)
{
    PresenterRecovery recovery;
    int64_t now = 0;
    for (int i = 0; i < 3 * PresenterRecovery::kMaxRestarts; i++)
    {
        now += PresenterRecovery::kRestartWindowNs / PresenterRecovery::kMaxRestarts + 1;
        ASSERT_TRUE(recovery.Restart(now)) << i;
    }
    EXPECT_FALSE(recovery.GlOnly());
}
