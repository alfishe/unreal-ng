#pragma once

#ifndef _INCLUDED_STDAFX_H_
#define _INCLUDED_STDAFX_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/utf8util.h"
#include "targetver.h"

// Host OS/Compiler overrides / customizations
#include "sysdefs.h"

// All emulator customizations defined here
#include "mods.h"

using std::atomic;
using std::atomic_flag;
using std::ios;
using std::istream;
using std::list;
using std::map;
using std::max;
using std::min;
using std::ostream;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::vector;
using std::weak_ptr;
using utf8::ifstream;
using utf8::ofstream;

// MSVC on Windows
#if defined _WIN32 && defined _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#endif
#ifndef NOMINMAX
#define NOMINMAX  // No min/max macroses defined in <windows.h> - they're conflicting with STL std::min / std::max
#endif

// clang-format off
	// CRITICAL: winsock2.h MUST be included BEFORE windows.h to prevent type conflicts
	// DO NOT REORDER THESE INCLUDES!
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#include <timeapi.h>				// Used in SystemHelper for time period measurements (timeBeginperiod / timeEndPeriod)
// clang-format on

#include <assert.h>
#include <conio.h>
#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <malloc.h>
#include <process.h>
#include <stddef.h>  // #include <algorithm> depends on size_t defined here
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")  // Windows Sockets library
#pragma comment(lib, "winmm.lib")   // Contains TimeAPI functions

#ifdef _MSC_VER
// This workaround is only needed for C++17 to avoid "C2872 'byte': ambiguous symbol" errors.
// In C++20, the issue is resolved natively, and Qt6 requires std::byte to be enabled.
// See: https://developercommunity.visualstudio.com/content/problem/93889/error-c2872-byte-ambiguous-symbol.html
#if __cplusplus < 202002L && _MSVC_LANG < 202002L
#if defined(_HAS_STD_BYTE)
#undef _HAS_STD_BYTE
#endif
#define _HAS_STD_BYTE 0
#endif
#endif

#define CACHE_LINE 64

#if _MSC_VER >= 1300
#define CACHE_ALIGNED __declspec(align(CACHE_LINE))
#endif

// timeval is defined in winsock2.h, only define if not already present
// Check for _WINSOCK2API_ which is defined when winsock2.h is included
#ifndef _WINSOCK2API_
struct timeval
{
    time_t tv_sec;   // seconds
    time_t tv_usec;  // and microseconds
};
#endif  // _WINSOCK2API_

// timezone is NOT defined in winsock2.h, always define for MSVC
struct timezone
{
    int tz_minuteswest;
    int tz_dsttime;
};

// Emulation for POSIX gettimeofday() using WinAPI
int gettimeofday(struct timeval* tv, struct timezone* tz);

// Shut down "LNK4221: The object file does not define any previously undefined public symbols, so it will not be used
// by any link operation that consumes that library" linker error Error happens with MSVC linker if precompiled header
// (stdafx.h/.cpp) doesn't bring any symbols into any namespace
namespace
{
char dummy;
};
#endif  // _WIN32 && defined MSVC

// GCC on Windows (MinGW, MSYS2 etc.)
#if defined _WIN32 && defined __GNUC__
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#ifndef NOMINMAX
#define NOMINMAX      // No min/max macroses defined in <windows.h> - they're conflicting with STL std::min / std::max
#endif                // NOMINMAX
#undef _HAS_STD_BYTE  // Prevent 'reference to 'byte' is ambiguous' errors in MinGW headers

#include <minwindef.h>  // PATH_MAX constant defined here
#include <sys/time.h>   // gettimeofday()
#include <unistd.h>     // readlink()
#include <windows.h>

#include <climits>  // LLONG_MAX constant defined here
#include <codecvt>
#include <locale>

#define CACHE_LINE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE)))

#endif  // _WIN32 && defined __GNUC__

#ifdef __linux__
#include <linux/limits.h>  // PATH_MAX constant defined here
#include <sys/time.h>      // gettimeofday()
#include <time.h>          // localtime()
#include <unistd.h>        // readlink()

#include <climits>  // LLONG_MAX constant defined here
#include <codecvt>
#include <locale>

#define CACHE_LINE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE)))
#endif  // __linux__

#ifdef __APPLE__
#include <mach-o/dyld.h>    // _NSGetExecutablePath()
#include <sys/sysctl.h>     // sysctlbyname() definition
#include <sys/syslimits.h>  // PATH_MAX constant defined here
#include <unistd.h>         // readlink()

#include <climits>  // LLONG_MAX constant defined here
#include <codecvt>
#include <locale>

#define CACHE_LINE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE)))
#endif

#endif  // _INCLUDED_STDAFX_H_
