#include "stdafx.h"

// Fake export for stdafx.o to get rid of MSVC linker error.
// LNK4221: This object file does not define any previously undefined public symbols,
// so it will not be used by any link operation that consumes this library
#if defined _WIN32 || defined __CYGWIN__ || defined __MINGW32__
#ifdef __cplusplus
extern "C" {
#endif

__declspec( dllexport ) void getRidOfLNK4221(){}

#ifdef __cplusplus
}
#endif

#endif

#if defined _WIN32

//
// Emulation for POSIX gettimeofday() using WinAPI
//
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (tv)
    {
        FILETIME               filetime; // 64-bit value representing the number of 100-nanosecond intervals since January 1, 1601 00:00 UTC
        ULARGE_INTEGER         x;
        ULONGLONG              usec;
        static const ULONGLONG epoch_offset_us = 11644473600000000ULL; // microseconds betweeen Jan 1,1601 and Jan 1,1970

#if _WIN32_WINNT >= _WIN32_WINNT_WIN8
        GetSystemTimePreciseAsFileTime(&filetime);
#else
        GetSystemTimeAsFileTime(&filetime);
#endif
        x.LowPart =  filetime.dwLowDateTime;
        x.HighPart = filetime.dwHighDateTime;
        usec = x.QuadPart / 10  -  epoch_offset_us;
        tv->tv_sec  = (time_t)(usec / 1000000ULL);
        tv->tv_usec = (long)(usec % 1000000ULL);
    }

    if (tz)
    {
        TIME_ZONE_INFORMATION timezone;
        GetTimeZoneInformation(&timezone);
        tz->tz_minuteswest = timezone.Bias;
        tz->tz_dsttime = 0;
    }

    return 0;
}
#endif