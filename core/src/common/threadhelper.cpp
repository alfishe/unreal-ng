#include "threadhelper.h"

void ThreadHelper::setThreadName(const char* name)
{
    size_t len = strlen(name);

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
    if (setThreadDescription == nullptr)
    {
	    wchar_t wname[128];
	    size_t retval;
        mbstowcs_s(&retval, wname, threadName, len);
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif

#if defined _WIN32 && defined __GNUC__
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
            GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription == nullptr)
    {
        wchar_t wname[24];
        size_t retval;
        mbstate_t conversion;
        mbsrtowcs_s(&retval, wname, (size_t)(sizeof (wname) / sizeof (wname[0])), &name, len), &conversion);
    }
#endif
}