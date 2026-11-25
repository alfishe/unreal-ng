#include "gpudevice.h"

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
}  // namespace gpu
