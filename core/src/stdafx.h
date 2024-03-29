#pragma once

#ifndef _INCLUDED_STDAFX_H_
#define _INCLUDED_STDAFX_H_

#include "targetver.h"

#include <cassert>
#include <cstdint>
#include <stdint.h>
#include <stdio.h>
#include <ctime>
#include <string.h>
#include <stdint.h>
#include <memory>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cctype>
#include <memory>
#include <vector>
#include <array>
#include <list>
#include <map>
#include <atomic>
#include <sstream>

#include "common/utf8util.h"

// Host OS/Compiler overrides / customizations
#include "sysdefs.h"

// All emulator customizations defined here
#include "mods.h"

using std::vector;
using std::shared_ptr;
using std::unique_ptr;
using std::weak_ptr;
using std::ios;
using std::istream;
using std::ostream;
using std::stringstream;
using utf8::ifstream;
using utf8::ofstream;
using std::list;
using std::map;
using std::min;
using std::max;
using std::string;
using std::atomic_flag;
using std::atomic;

// MSVC on Windows
#if defined _WIN32 && defined MSVC
	#define WIN32_LEAN_AND_MEAN			// Exclude rarely-used stuff from Windows headers
	#define NOMINMAX					// No min/max macroses defined in <windows.h> - they're conflicting with STL std::min / std::max

	#include <windows.h>
	#include <timeapi.h>				// Used in SystemHelper for time period measurements (timeBeginperiod / timeEndPeriod)

	#include <stdio.h>
	#include <ctype.h>
	#include <string.h>
	#include <stddef.h>					// #include <algorithm> depends on size_t defined here
	#include <stdlib.h>
	#include <limits.h>
	#include <malloc.h>
	#include <conio.h>
	#include <process.h>
	#include <io.h>
	#include <fcntl.h>
	#include <assert.h>
	#include <sys/stat.h>

	#pragma comment(lib, "winmm.lib")	// Contains TimeAPI functions

	#ifdef _MSC_VER
		#if defined (_HAS_STD_BYTE)
			#undef _HAS_STD_BYTE
		#endif
		#define _HAS_STD_BYTE 0	// Has to be so, otherwise MSVC gives weird errors for C++ 17 projects related to "C2872 'byte': ambiguous symbol" in 'rpcndr.h' file. See: https://developercommunity.visualstudio.com/content/problem/93889/error-c2872-byte-ambiguous-symbol.html
	#endif

	#define CACHE_LINE 64

	#if _MSC_VER >= 1300
		#define CACHE_ALIGNED __declspec(align(CACHE_LINE))
    #endif

    struct timeval
    {
        time_t    tv_sec;         // seconds
        time_t    tv_usec;        // and microseconds
    };
    struct timezone
    {
        int tz_minuteswest;
        int tz_dsttime;
    };

    // Emulation for POSIX gettimeofday() using WinAPI
    int gettimeofday(struct timeval *tv, struct timezone *tz);

	// Shut down "LNK4221: The object file does not define any previously undefined public symbols, so it will not be used by any link operation that consumes that library" linker error
	// Error happens with MSVC linker if precompiled header (stdafx.h/.cpp) doesn't bring any symbols into any namespace
	namespace { char dummy; };
#endif // _WIN32 && defined MSVC

// GCC on Windows (MinGW, MSYS2 etc.)
#if defined _WIN32 && defined __GNUC__
    #define WIN32_LEAN_AND_MEAN			// Exclude rarely-used stuff from Windows headers
    #ifndef NOMINMAX
        #define NOMINMAX				// No min/max macroses defined in <windows.h> - they're conflicting with STL std::min / std::max
    #endif // NOMINMAX
    #undef _HAS_STD_BYTE                // Prevent 'reference to 'byte' is ambiguous' errors in MinGW headers

    #include <windows.h>

    #include <unistd.h>		    // readlink()
    #include <locale>
    #include <codecvt>
    #include <minwindef.h>  	// PATH_MAX constant defined here
    #include <climits>		    // LLONG_MAX constant defined here
    #include <sys/time.h>       // gettimeofday()

    #define CACHE_LINE 64
    #define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE)))


#endif // _WIN32 && defined __GNUC__

#ifdef __linux__
	#include <unistd.h>		    // readlink()
	#include <locale>
	#include <codecvt>
	#include <linux/limits.h>	// PATH_MAX constant defined here
	#include <climits>		    // LLONG_MAX constant defined here
    #include <sys/time.h>       // gettimeofday()
    #include <time.h>           // localtime()

	#define CACHE_LINE 64
	#define CACHE_ALIGNED __attribute__ ((aligned (CACHE_LINE)))
#endif // __linux__

#ifdef __APPLE__
	#include <unistd.h>			// readlink()
    #include <mach-o/dyld.h>    // _NSGetExecutablePath()
	#include <locale>
	#include <codecvt>
    #include <sys/sysctl.h>     // sysctlbyname() definition
	#include <sys/syslimits.h>	// PATH_MAX constant defined here
	#include <climits>		    // LLONG_MAX constant defined here

	#define CACHE_LINE 64
	#define CACHE_ALIGNED __attribute__ ((aligned (CACHE_LINE)))
#endif


#endif // _INCLUDED_STDAFX_H_
