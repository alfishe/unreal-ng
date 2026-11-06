#pragma once

#include <cstdint>

/// A run of bytes the picture of a frame is read from (Screen::DescribeDisplayedMemory)
struct ScreenMemorySpan
{
    /// The TTD region of a device's own memory ("sprinter.vram"); null: machine RAM, where byte `o` of RAM page `p` is
    /// at p * 16 KB + o
    const char* region = nullptr;
    uint32_t offset = 0;   ///< the first byte, in the region
    uint32_t bytes = 0;
};
