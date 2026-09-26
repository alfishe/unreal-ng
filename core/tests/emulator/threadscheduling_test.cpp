/// @file threadscheduling_test.cpp
/// @brief Thread scheduling roles (ThreadHelper role table), observed on the
/// live threads of the process: only the active instance's emulation loop
/// runs real-time, only during cadenced playback; the MessageCenter worker
/// that hands every frame to the UI runs interactive. macOS only - the one
/// platform where the roles are observable from userland without privileges.

#include <gtest/gtest.h>

#ifdef __APPLE__

#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>

#include <chrono>
#include <string>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/mainloop.h"

namespace
{
/// Scheduling facts of the first live thread whose name starts with @p prefix
struct ThreadFacts
{
    bool found = false;
    bool timeConstraint = false;
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
};

ThreadFacts FindThread(const std::string& prefix)
{
    ThreadFacts facts;
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
        return facts;

    for (mach_msg_type_number_t i = 0; i < count; i++)
    {
        pthread_t pthread = pthread_from_mach_thread_np(threads[i]);
        char name[64] = {};
        if (!facts.found && pthread && pthread_getname_np(pthread, name, sizeof(name)) == 0 &&
            std::string(name).rfind(prefix, 0) == 0)
        {
            facts.found = true;

            thread_time_constraint_policy_data_t policy{};
            mach_msg_type_number_t policyCount = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
            boolean_t getDefault = FALSE;
            thread_policy_get(threads[i], THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy, &policyCount,
                              &getDefault);
            facts.timeConstraint = !getDefault;

            int relative = 0;
            pthread_get_qos_class_np(pthread, &facts.qos, &relative);
        }
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_act_t));
    return facts;
}

/// Let the loop run a few frames so it samples its scheduling request
void LetFramesRun()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}
}  // namespace

class ThreadScheduling_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    std::string _threadName;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        const std::string id = _emulator->GetId();
        _threadName = "emulator-" + (id.length() > 12 ? id.substr(id.length() - 12) : id);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }
};

/// An instance nobody made active never runs real-time. Its thread used to be
/// elevated unconditionally at StartAsync while MainLoop believed it was not,
/// so it stayed time-constraint for life - throttled by the kernel whenever it
/// ran past its 4 ms budget (turbo, test runs, heavy frames)
TEST_F(ThreadScheduling_Test, InactiveInstanceIsNotRealtime)
{
    _emulator->StartAsync();
    LetFramesRun();

    const ThreadFacts facts = FindThread(_threadName);
    ASSERT_TRUE(facts.found) << "emulation thread '" << _threadName << "' not found";
    EXPECT_FALSE(facts.timeConstraint);
}

/// The active instance runs real-time during cadenced playback and drops it
/// for turbo (frames back to back would violate the declared budget)
TEST_F(ThreadScheduling_Test, ActiveInstanceIsRealtimeOnlyWhileCadenced)
{
    _emulator->GetMainLoop()->SetRealtimeRequested(true);
    _emulator->StartAsync();
    LetFramesRun();

    ThreadFacts facts = FindThread(_threadName);
    ASSERT_TRUE(facts.found);
    EXPECT_TRUE(facts.timeConstraint) << "active instance, cadenced playback";

    _emulator->EnableTurboMode();
    LetFramesRun();
    facts = FindThread(_threadName);
    EXPECT_FALSE(facts.timeConstraint) << "turbo drops real-time";

    _emulator->DisableTurboMode();
    _emulator->GetMainLoop()->SetRealtimeRequested(false);
    LetFramesRun();
    facts = FindThread(_threadName);
    EXPECT_FALSE(facts.timeConstraint) << "no longer active";
}

/// The worker that delivers every frame to the UI is interactive, not default
TEST(ThreadSchedulingRoles_Test, MessageCenterWorkerIsInteractive)
{
    MessageCenter::DefaultMessageCenter(true);  // make sure the worker runs
    const ThreadFacts facts = FindThread("message_center_worker");
    ASSERT_TRUE(facts.found);
    EXPECT_EQ(facts.qos, QOS_CLASS_USER_INTERACTIVE);
    EXPECT_FALSE(facts.timeConstraint);
}

#endif  // __APPLE__
