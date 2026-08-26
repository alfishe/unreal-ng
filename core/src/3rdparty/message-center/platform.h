#pragma once

#ifndef MESSAGE_CENTER_PLATFORM_H
#define MESSAGE_CENTER_PLATFORM_H

// Platform and compiler compatibility definitions

// Compiler detection
#if defined(_MSC_VER)
  #define MC_COMPILER_MSVC 1
  #define MC_COMPILER_VERSION _MSC_VER
#elif defined(__clang__)
  #define MC_COMPILER_CLANG 1
  #define MC_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100)
#elif defined(__GNUC__)
  #define MC_COMPILER_GCC 1
  #define MC_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100)
#endif

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
  #define MC_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #define MC_PLATFORM_APPLE 1
#elif defined(__linux__)
  #define MC_PLATFORM_LINUX 1
#endif

// Cache line size (used for alignment to prevent false sharing)
#if defined(MC_COMPILER_MSVC)
  #define MC_CACHE_LINE_SIZE 64
#elif defined(__cpp_lib_hardware_interference_size)
  #include <new>
  #define MC_CACHE_LINE_SIZE std::hardware_destructive_interference_size
#else
  #define MC_CACHE_LINE_SIZE 64
#endif

// Force inline
#if defined(MC_COMPILER_MSVC)
  #define MC_FORCE_INLINE __forceinline
#elif defined(MC_COMPILER_GCC) || defined(MC_COMPILER_CLANG)
  #define MC_FORCE_INLINE inline __attribute__((always_inline))
#else
  #define MC_FORCE_INLINE inline
#endif

// Likely/unlikely branch hints
#if defined(MC_COMPILER_GCC) || defined(MC_COMPILER_CLANG)
  #define MC_LIKELY(x) __builtin_expect(!!(x), 1)
  #define MC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define MC_LIKELY(x) (x)
  #define MC_UNLIKELY(x) (x)
#endif

// Thread-local storage
// Note: C++11 thread_local is supported by all modern compilers
// MSVC 2015+, GCC 4.8+, Clang 3.3+

// Alignment
// Note: C++11 alignas is supported by all modern compilers
// For older MSVC, you might need __declspec(align(N))

// Atomic memory order compatibility
// All modern compilers support C++11 <atomic> with memory orders

#endif // MESSAGE_CENTER_PLATFORM_H
