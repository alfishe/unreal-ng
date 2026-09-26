#include "pch.h"

#include "common/threadhelper.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>

namespace
{
/// Whether the calling thread currently runs under a real-time
/// (time-constraint) policy: thread_policy_get reports get_default = TRUE
/// when the policy is not set on the thread
bool HasTimeConstraintPolicy()
{
    thread_time_constraint_policy_data_t policy{};
    mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    boolean_t getDefault = FALSE;
    thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                      (thread_policy_t)&policy, &count, &getDefault);
    return !getDefault;
}
}  // namespace
#endif

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

#ifdef __APPLE__
/// @brief The drop really releases the real-time policy. A thread left under
/// it keeps a 4 ms / 20 ms computation budget and is throttled by the kernel
/// whenever it runs longer - a de-selected emulator instance running turbo,
/// or this very test runner (every test after this one ran 4-5x slower while
/// the release used an invalid zeroed time-constraint policy that the kernel
/// rejected)
TEST(ThreadHelper_Test, NormalPriorityReleasesTheRealtimePolicy)
{
    ThreadHelper::setRealtimePriority();
    EXPECT_TRUE(HasTimeConstraintPolicy()) << "elevation must apply the time-constraint policy";

    ThreadHelper::setNormalPriority();
    EXPECT_FALSE(HasTimeConstraintPolicy()) << "the drop must return the thread to timeshare scheduling";
}
#endif

/// endregion </Realtime scheduling API>
