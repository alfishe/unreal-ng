// eve-emu - interface of the built-in PNG / JPEG decoder (eve-vendor-stb.cpp).
#pragma once

#include <cstddef>

namespace EveLib
{

// Decode a whole PNG or JPEG file into 8-bit pixels with the given channel count
// (1 gray, 3 RGB, 4 RGBA). Returns nullptr on invalid data. Free with StbFree.
unsigned char* StbDecode(const unsigned char* data, size_t size, int channels, int* width, int* height);
void StbFree(unsigned char* pixels);

} // namespace EveLib
