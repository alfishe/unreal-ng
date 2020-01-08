#pragma once
#include "stdafx.h"

// Cross-compiler version of force inline
#if defined(__clang__)
	#define __forceinline __attribute__((always_inline))
#elif defined(__GNUC__) || defined(__GNUG__)
	#define __forceinline __attribute__((always_inline))
#elif defined(_MSC_VER)
	#if _MSC_VER < 1900
		#ifndef __forceinline
			#define __forceinline inline
		#endif
	#endif
#elif defined(__MINGW32__)
	#define __forceinline __attribute__((always_inline))
#else
	#define __forceinline inline
#endif

// Cross-compiler version for fastcall (Parameters in registers)
// See more: https://en.wikipedia.org/wiki/X86_calling_conventions
#if defined(_MSC_VER)
	#ifndef __fastcall
		#define __fastcall fastcall
	#endif
#elif
	#define __fastcall
#endif

#define countof(a)  (sizeof(a) / sizeof(a[0]))

#ifdef _MSC_VER
	#define ATTR_ALIGN(x) __declspec(align(x))
#endif

// Compatibility between MSVC and GCC function name macros
#ifdef _MSC_VER
	#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif


#if __ICL >= 1000 || defined(__GNUC__)
	static inline u8 rol8(u8 val, u8 shift)
	{
		__asm__ volatile ("rolb %1,%0" : "=r"(val) : "cI"(shift), "0"(val));
		return val;
	}

	static inline u8 ror8(u8 val, u8 shift)
	{
		__asm__ volatile ("rorb %1,%0" : "=r"(val) : "cI"(shift), "0"(val));
		return val;
	}
	static inline void asm_pause() { __asm__("pause"); }
#else
	extern "C" unsigned char __cdecl _rotr8(unsigned char value, unsigned char shift);
	extern "C" unsigned char __cdecl _rotl8(unsigned char value, unsigned char shift);
	#pragma intrinsic(_rotr8)
	#pragma intrinsic(_rotl8)
	static inline uint8_t rol8(uint8_t val, uint8_t shift) { return _rotl8(val, shift); }
	static inline uint8_t ror8(uint8_t val, uint8_t shift) { return _rotr8(val, shift); }
	
	//static inline void asm_pause() { __asm {rep nop} }
#endif

#if defined(_MSC_VER) && _MSC_VER < 1300
	static inline u16 _byteswap_ushort(u16 i){return (i>>8)|(i<<8);}
	static inline u32 _byteswap_ulong(u32 i){return _byteswap_ushort((u16)(i>>16))|(_byteswap_ushort((u16)i)<<16);};
#endif

#ifdef __GNUC__
	#include <stdint.h>
	#define HANDLE_PRAGMA_PACK_PUSH_POP
	#define __forceinline __attribute__((always_inline))
	#undef forceinline
	#define forceinline __forceinline
	#define override

	#define ATTR_ALIGN(x) __attribute__((aligned(x)))

	static __inline__ void __debugbreak(void)
	{
	  __asm__ __volatile__ ("int $3");
	}

	static __inline__ unsigned short _byteswap_ushort(unsigned short x)
	{
		__asm__("xchgb %b0,%h0" : "=q"(x) :  "0"(x));
		return x;
	}

	#define _byteswap_ulong(x) _bswap(x) 

	#define _countof(x) (sizeof(x)/sizeof((x)[0]))
	#define __assume(x)
#endif // __GNUC__
