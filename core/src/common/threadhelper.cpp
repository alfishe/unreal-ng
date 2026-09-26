#include "threadhelper.h"

// Platform scheduling / naming headers live at file scope: some SDK headers
// (mach/mach.h) expand __BEGIN_DECLS to extern "C" {, which cannot open
// inside a function body
#ifdef __APPLE__
    #include <pthread.h>
    #include <mach/mach.h>
    #include <mach/mach_time.h>
    #include <mach/thread_policy.h>
    #include <pthread/qos.h>
#endif
#ifdef __linux__
    #include <pthread.h>
    #include <sched.h>
#endif

void ThreadHelper::setThreadName(const char* name)
{
    size_t len = strlen(name);
    if (len == 0)
        return;

#ifdef __APPLE__
#include <pthread.h>
    pthread_setname_np(name);
#endif
#ifdef __linux__
    #include <pthread.h>
	pthread_setname_np(pthread_self(), name);
#endif
#if defined _WIN32 && defined _MSC_VER
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
        GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
        wchar_t wname[128];
        size_t retval;
        mbstowcs_s(&retval, wname, sizeof(wname) / sizeof(wname[0]), name, len);
        setThreadDescription(GetCurrentThread(), wname);
    }
#elif defined _WIN32 && defined __GNUC__
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription")));
    if (setThreadDescription != nullptr)
    {
        wchar_t wname[128];
        size_t retval;
        mbstowcs_s(&retval, wname, sizeof(wname) / sizeof(wname[0]), name, len);
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif
}

void ThreadHelper::setRealtimePriority()
{
#ifdef __APPLE__
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    auto ns2abs = [&](uint64_t ns) {
        return static_cast<uint32_t>(ns * tb.denom / tb.numer);
    };

    thread_time_constraint_policy_data_t policy;
    policy.period      = ns2abs(20000000ULL);  // ns: one 50 Hz frame
    policy.computation = ns2abs(4000000ULL);   // ns: worst-case frame budget
    policy.constraint  = ns2abs(10000000ULL);  // ns: must start within half a period
    policy.preemptible = 1;                    // audio device threads may preempt us

    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_TIME_CONSTRAINT_POLICY,
                      (thread_policy_t)&policy,
                      THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
#ifdef __linux__
    // SCHED_FIFO is a privilege on Linux (CAP_SYS_NICE). Without it the call
    // fails with EPERM and the thread stays at default scheduling - the
    // expected case for an unprivileged desktop application
    sched_param param;
    param.sched_priority = 10;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
#ifdef _WIN32
    // The emulation producer sits BELOW the audio path: miniaudio's device
    // thread runs under MMCSS ("Audio"), which outranks any plain thread
    // priority. TIME_CRITICAL here would let a busy producer starve the
    // mixer it is supposed to feed
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}

void ThreadHelper::setInteractivePriority()
{
#ifdef __APPLE__
    // User-interactive QoS: the class the main thread of a foreground app
    // runs at - highest timeshare band, P-cores preferred, minimal timer
    // coalescing. Not real-time: no computation budget to violate
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    // Linux / Windows: stock scheduling (see the role table in the header)
}

void ThreadHelper::setNormalPriority()
{
#ifdef __APPLE__
    // Back to timeshare scheduling: THREAD_STANDARD_POLICY is what releases a
    // time-constraint policy. A zeroed THREAD_TIME_CONSTRAINT_POLICY (used
    // here before) is rejected as invalid, so the thread silently kept the
    // real-time policy with its 4 ms / 20 ms budget - and a real-time thread
    // that runs longer than its computation budget is throttled by the
    // kernel: every "dropped" thread doing continuous work (a de-selected
    // emulator instance in turbo, the test runner after ThreadHelper_Test)
    // ran at a fraction of the CPU for the rest of its life
    thread_standard_policy_data_t policy;
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_STANDARD_POLICY,
                      (thread_policy_t)&policy,
                      THREAD_STANDARD_POLICY_COUNT);
#endif
#ifdef __linux__
    // Dropping back to the default policy is always permitted
    sched_param param;
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
}