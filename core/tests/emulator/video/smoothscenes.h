#pragma once

#include <cstdint>
#include <vector>

#include "emulator/video/smoothmotion.h"

/// Smooth motion's worst-case scenes, shared by the CPU tests (smoothmotion_test.cpp) and the GPU ones
/// (unreal-qt/tests/qt/smoothmotiongpu_test.cpp): the content that costs LayerFlow the most per pixel
namespace smoothscenes
{
inline uint32_t Hash(uint32_t x, uint32_t y, uint32_t s)
{
    uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ s * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

/// Frame k of sparse dots over black paper. Blocks: per 8 x 8 block a layer of its own, moving by its own vector (48
/// vectors, neighbours differ): more vectors than LayerFlow::kMaxVectors. Layered: 16 layers over every pixel, each
/// its own vector, so every still pixel asks the cover rule about many moving vectors
inline std::vector<uint32_t> Dots(int width, int height, int k, bool layered)
{
    using V = smoothmotion::MotionField::Vector;
    std::vector<V> vectors;
    for (int y = -3; y <= 3; y++)
        for (int x = -3; x <= 3; x++)
            if (x != 0 || y != 0)
                vectors.push_back({x * 2, y});
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height, 0xFF000000u);
    const uint32_t ink[4] = {0xFFFFFFFFu, 0xFF00D7D7u, 0xFFD700D7u, 0xFFD7D700u};
    const auto dot = [&](int x, int y, size_t layer, uint32_t every) {
        const V v = vectors[layer];
        const uint32_t h = Hash(static_cast<uint32_t>(x - v.x * k + 4096), static_cast<uint32_t>(y - v.y * k + 4096),
                                static_cast<uint32_t>(layer));
        if (h % every == 0)
            frame[static_cast<size_t>(y) * width + x] = ink[(h >> 8) & 3];
    };
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (layered)
                for (size_t layer = 0; layer < 16; layer++)
                    dot(x, y, layer * 3 % vectors.size(), 40);
            else
                dot(x, y, static_cast<size_t>(x / 8 * 7 + y / 8 * 3) % vectors.size(), 9);
    return frame;
}
}  // namespace smoothscenes
