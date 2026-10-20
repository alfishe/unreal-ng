// POC 032 stage 2, step S1: TooJpeg with a buffered output and a 64-bit bit buffer (toojpegstep1.cpp).
#pragma once
#include <cstdint>
#include <vector>

namespace poc032::step1
{
// Baseline JPEG, 4:4:4, from packed RGB (3 bytes per pixel, rows without padding). Empty on bad arguments.
[[nodiscard]] std::vector<std::uint8_t> Encode(const std::uint8_t* rgb, int width, int height, int quality);
}  // namespace poc032::step1
