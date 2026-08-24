#include "threadhelper.h"

#ifdef __APPLE__
#include <pthread/qos.h>
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
#if defined _WIN32 && defined MSVC
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
        GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
	    wchar_t wname[128];
	    size_t retval;
        mbstowcs_s(&retval, wname, threadName, len);
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif

#if defined _WIN32 && defined __GNUC__
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

void ThreadHelper::setInteractiveQoS()
{
#ifdef __APPLE__
    // USER_INTERACTIVE tells the scheduler this thread's deadlines gate
    // user-perceivable output (video frames / audio): its wake-ups are
    // exempt from the timer coalescing applied to default-class threads
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
#ifdef __linux__
    // SCHED_OTHER with default niceness keeps wake-up latency low enough;
    // real-time classes would need privilege handling - intentionally no-op
#endif
}