#pragma once

#ifdef _MSC_VER
	#if defined (_HAS_STD_BYTE)
		#undef _HAS_STD_BYTE
	#endif
	#define _HAS_STD_BYTE 0	// Has to be so, otherwise MSVC gives weird errors for C++ 17 projects related to "C2872 'byte': ambiguous symbol" in 'rpcndr.h' file. See: https://developercommunity.visualstudio.com/content/problem/93889/error-c2872-byte-ambiguous-symbol.html

	#if _MSC_VER >= 1300
		#define CACHE_ALIGNED __declspec(align(CACHE_LINE))
	#endif
#endif

#define CACHE_LINE 64

#ifdef _WIN32
	#if _MSC_VER >= 1300
		#define CACHE_ALIGNED __declspec(align(CACHE_LINE))
	#endif
#endif

#ifdef __linux__
	#define CACHE_ALIGNED __attribute__ ((aligned (CACHE_LINE)))
#endif

#ifdef __APPLE__
	#define CACHE_ALIGNED __attribute__ ((aligned (CACHE_LINE)))
#endif