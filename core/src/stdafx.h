#pragma once

#include "targetver.h"

#include <cstdint>
#include <stdint.h>
#include <stdio.h>
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
#include <sstream>
#include <list>
#include <atomic>

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
using std::max;
using std::string;
using std::atomic_flag;
using std::atomic;

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN			// Exclude rarely-used stuff from Windows headers
	#include <windows.h>
	#include <timeapi.h>				// Used in SystemHelper for time period measurements (timeBeginperiod / timeEndPeriod)

	#include <stdio.h>
	#include <ctype.h>
	#include <string.h>
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

	#define CACHE_LINE 64

	#if _MSC_VER >= 1300
		#define CACHE_ALIGNED __declspec(align(CACHE_LINE))
	#else
		#define CACHE_ALIGNED
	#endif

#endif // _WIN32


// Shut down "LNK4221: The object file does not define any previously undefined public symbols, so it will not be used by any link operation that consumes that library" linker error
// Error happens with MSVC linker if precompiled header (stdafx.h/.cpp) doesn't bring any symbols into any namespace
namespace { char dummy; };
