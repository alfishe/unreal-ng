#pragma once

#ifndef _INCLUDE_SYSDEFS_H_
#define _INCLUDE_SYSDEFS_H_

#ifdef _MSC_VER
	// Microsoft Visual Studio
	#include <intrin.h>			// CPUID capability
	#include <immintrin.h>		// AVX intrinsics
#elif __x86_64__
    // GCC/CLang
	#include <cpuid.h>			// _cpuid()
	#include <x86intrin.h>		// _mm_pause()
	#include <immintrin.h>		// AVX intrinsics
#endif

// Cross-compiler version of force inline
#ifndef __forceinline
    #if defined(__clang__)
        #define __forceinline __attribute__((always_inline)) inline
    #elif defined(__GNUC__) || defined(__GNUG__)
        #define __forceinline __attribute__((always_inline)) inline
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
#endif

// Cross-compiler version for fastcall (Parameters in registers)
// See more: https://en.wikipedia.org/wiki/X86_calling_conventions
#if defined(_MSC_VER)
	#ifndef fastcall
		#define fastcall __fastcall
	#endif
#else
	#define fastcall
#endif

#define countof(a)  (sizeof(a) / sizeof(a[0]))

#ifdef _MSC_VER
	#define ATTR_ALIGN(x) __declspec(align(x))
#endif

// Compatibility between MSVC and GCC function name macros
#ifdef _MSC_VER
	#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

// High precision time counter (CPU HW based)
// TODO: find similar functionality for ARM CPUs. Only Intel 32/64 bits supported for now
#if  defined(__GNUC__)
#if defined(__i386__)
	static __inline__ unsigned long long rdtsc(void)
	{
		unsigned long long int x;
		__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
		return x;
	}
#elif defined(__x86_64__)
	static __inline__ unsigned long long rdtsc(void)
	{
		unsigned hi, lo;
		__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
		return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
	}
#endif
#elif defined(_MSC_VER)
	#define rdtsc __rdtsc
#endif

// Optimized bit rotation
#if (__ICL >= 1000 || defined(__GNUC__)) && defined __x86_64__
	static inline uint8_t rol8(uint8_t val, uint8_t shift)
	{
		__asm__ volatile ("rolb %1,%0" : "=r"(val) : "cI"(shift), "0"(val));
		return val;
	}

	static inline uint8_t ror8(uint8_t val, uint8_t shift)
	{
		__asm__ volatile ("rorb %1,%0" : "=r"(val) : "cI"(shift), "0"(val));
		return val;
	}

	//static inline void _mm_pause() { __asm__("pause"); }
#elif defined(_MSC_VER)
	extern "C" unsigned char __cdecl _rotr8(unsigned char value, unsigned char shift);
	extern "C" unsigned char __cdecl _rotl8(unsigned char value, unsigned char shift);
	#pragma intrinsic(_rotr8)
	#pragma intrinsic(_rotl8)
	static inline uint8_t rol8(uint8_t val, uint8_t shift) { return _rotl8(val, shift); }
	static inline uint8_t ror8(uint8_t val, uint8_t shift) { return _rotr8(val, shift); }

	extern "C" void	_mm_pause(void);
	#pragma intrinsic(_mm_pause)
#endif

#if defined(_MSC_VER) && _MSC_VER < 1300
	static inline u16 _byteswap_ushort(u16 i){return (i>>8)|(i<<8);}
	static inline u32 _byteswap_ulong(u32 i){return _byteswap_ushort((u16)(i>>16))|(_byteswap_ushort((u16)i)<<16);};
#endif

// Inline assembly accelerators for x86
#if defined __GNUC__ && !defined _WIN32 && defined __x86_64__
	static __inline__ void __debugbreak(void)
	{
	  __asm__ __volatile__ ("int $3");
	}

	static __inline__ unsigned short _byteswap_ushort(uint16_t x)
	{
		__asm__("xchgb %b0,%h0" : "=q"(x) :  "0"(x));
		return x;
	}
#endif // __GNUC__ && !defined _WIN32

// ARM architecture fallbacks
#if defined(__arm__) || defined(__aarch64__)
    static inline uint16_t _byteswap_ushort(uint16_t x)
    {
        return __builtin_bswap16(x);
    }

    /*
    static inline uint16_t _byteswap_ushort(uint16_t x)
	{
	    uint8_t hiByte = x >> 8;
	    uint16_t loByte = x & 0x00FF;

	    return (loByte << 8) | hiByte;
	}
    */

    static inline uint64_t rdtsc()
    {
        uint64_t final_tsc;

#if __aarch64__
        // AARCH64 has CNTPCT register for incrementing clock counter
        asm volatile("mrs %0, cntpct_el0" : "=r" (final_tsc));
#else
        // Read PMCCNTR co-processor register
        asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(tsc));
#endif // __aarch64__

        return (uint64_t)final_tsc;
    }

    static inline void _mm_pause()
    {
#ifdef __aarch64__
        asm volatile ("yield");
#else
        asm volatile ("nop");
#endif
    }
#endif

// Cross-platform GCC-related (both x86 and ARM)
#if defined __GNUC__
    #define ATTR_ALIGN(x) __attribute__((aligned(x)))
    #define __assume(x)
#endif // __GNUC__

#if !defined _countof
    #define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif // _countof

#endif // _INCLUDE_SYSDEFS_H_