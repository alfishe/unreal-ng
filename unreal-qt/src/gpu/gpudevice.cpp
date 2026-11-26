#include "gpudevice.h"

#include <cstring>

namespace gpu
{
const float kQuadVertices[24] = {
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  //
    1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,  //
    0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  //
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

const float kQuadMvp[16] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f,  0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f};

int BytesPerTexel(Format format)
{
    switch (format)
    {
        case Format::R8UI:
            return 1;
        case Format::R16UI:
        case Format::R16I:
            return 2;
        case Format::RGBA8:
        case Format::SRGBA8:
        case Format::R32UI:
        case Format::R32I:
        case Format::RG16UI:
        case Format::RGBA8UI:
            return 4;
        case Format::RGBA16F:
        case Format::RG32I:
        case Format::RG32UI:
        case Format::RGBA16UI:
        case Format::RGBA16I:
            return 8;
        case Format::RGBA32F:
        case Format::RGBA32UI:
        case Format::RGBA32I:
            return 16;
    }
    return 4;
}

bool IsInteger(Format format)
{
    switch (format)
    {
        case Format::RGBA8:
        case Format::SRGBA8:
        case Format::RGBA16F:
        case Format::RGBA32F:
            return false;
        default:
            return true;
    }
}

const char* FormatName(Format format)
{
    switch (format)
    {
        case Format::RGBA8:
            return "RGBA8";
        case Format::SRGBA8:
            return "SRGBA8";
        case Format::RGBA16F:
            return "RGBA16F";
        case Format::RGBA32F:
            return "RGBA32F";
        case Format::R8UI:
            return "R8UI";
        case Format::R16UI:
            return "R16UI";
        case Format::R16I:
            return "R16I";
        case Format::R32UI:
            return "R32UI";
        case Format::R32I:
            return "R32I";
        case Format::RG16UI:
            return "RG16UI";
        case Format::RG32I:
            return "RG32I";
        case Format::RG32UI:
            return "RG32UI";
        case Format::RGBA8UI:
            return "RGBA8UI";
        case Format::RGBA16UI:
            return "RGBA16UI";
        case Format::RGBA16I:
            return "RGBA16I";
        case Format::RGBA32UI:
            return "RGBA32UI";
        case Format::RGBA32I:
            return "RGBA32I";
    }
    return "?";
}
bool Device::BeginRead(int slot, Texture& texture, Rect rect, bool bgra)
{
    if (slot < 0 || slot >= kReadSlots || rect.Empty())
        return false;
    ReadSlot& read = _readSlots[slot];
    read.texel = BytesPerTexel(texture.GetFormat());
    read.pixels.resize(static_cast<size_t>(rect.width) * rect.height * read.texel);
    read.rect = rect;
    read.bgra = bgra && read.texel == 4;
    read.started = Read(texture, rect, read.pixels.data());
    return read.started;
}

bool Device::FinishRead(int slot, uint8_t* out, size_t outStride, bool flip)
{
    if (slot < 0 || slot >= kReadSlots || !_readSlots[slot].started || !out)
        return false;
    ReadSlot& read = _readSlots[slot];
    read.started = false;
    CopyRows(read.pixels.data(), static_cast<size_t>(read.rect.width) * read.texel, read.rect.height, out, outStride,
             flip, read.bgra);
    return true;
}

void Device::CopyRows(const uint8_t* from, size_t rowBytes, int rows, uint8_t* out, size_t outStride, bool flip,
                      bool swap)
{
    for (int y = 0; y < rows; ++y)
    {
        uint8_t* to = out + static_cast<size_t>(flip ? rows - 1 - y : y) * outStride;
        const uint8_t* row = from + static_cast<size_t>(y) * rowBytes;
        if (!swap)
        {
            std::memcpy(to, row, rowBytes);
            continue;
        }
        const size_t texels = rowBytes / 4;
        for (size_t x = 0; x < texels; ++x)
        {
            uint32_t p;
            std::memcpy(&p, row + x * 4, 4);
            p = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
            std::memcpy(to + x * 4, &p, 4);
        }
    }
}
}  // namespace gpu
