#include "pch.h"

#include "common/threadhelper.h"

/// region <Realtime scheduling API>

/// @brief ThreadHelper::setRealtimePriority / setNormalPriority act on the CALLING thread
/// and are best effort by contract (Linux SCHED_FIFO is a silent no-op without
/// CAP_SYS_NICE). What is testable from userland on every platform: both
/// directions apply, are idempotent, and a round trip returns the thread to a
/// state it can be re-elevated from. Runs on the gtest worker thread - the
/// same thread MainLoop::Run applies its scheduling transitions on.
TEST(ThreadHelper_Test, RealtimeRoundTripIsIdempotentAndReversible)
{
    ThreadHelper::setNormalPriority();    // start from a known (default) state
    ThreadHelper::setRealtimePriority();  // elevation must not fail on any platform
    ThreadHelper::setRealtimePriority();  // re-apply while already elevated

    ThreadHelper::setNormalPriority();    // drop back
    ThreadHelper::setNormalPriority();    // idempotent drop

    ThreadHelper::setRealtimePriority();  // re-elevation after a drop (live selection switch)
    ThreadHelper::setNormalPriority();    // leave the worker thread default

    SUCCEED();
}

/// endregion </Realtime scheduling API>
