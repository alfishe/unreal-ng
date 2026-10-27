#pragma once

/// @file heapcounter.h
/// @brief Test helper: the heap the code under test allocates, as the
/// allocator holds it. heapcounter.cpp replaces the global operator new and
/// delete of the test binary (a pass-through while not counting): between
/// Start() and Stop() every allocation adds its real block size
/// (malloc_size / malloc_usable_size) and every release subtracts it, so
/// Net() is what the window left allocated, rounding included. Count one
/// thread's work only (the counters are global). Platforms without a block
/// size query report Available() false.

#include <cstddef>
#include <cstdint>

namespace HeapCounter
{
    bool Available();
    void Start();
    void Stop();
    int64_t Net();
    /// The most the window held at once (Net() at its highest)
    int64_t Peak();
    /// Allocations made in the window, freed or not ("no allocation per call" checks)
    int64_t Allocations();
}  // namespace HeapCounter
