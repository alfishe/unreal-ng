#pragma once

#ifdef _MSC_VER
	#if defined (_HAS_STD_BYTE)
		#undef _HAS_STD_BYTE
	#endif
	#define _HAS_STD_BYTE 0	// Has to be so, otherwise MSVC gives weird errors for C++ 17 projects related to "C2872 'byte': ambiguous symbol" in 'rpcndr.h' file. See: https://developercommunity.visualstudio.com/content/problem/93889/error-c2872-byte-ambiguous-symbol.html
#endif

#ifdef _WIN32
#endif