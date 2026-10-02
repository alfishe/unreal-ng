// eve-emu - the built-in PNG / JPEG decoder: stb_image with internal linkage
// (STB_IMAGE_STATIC), PNG and JPEG only (arch §6.3). Warnings are off for this
// translation unit: the code is vendored.

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS

#include "stb/stb_image.h"

#include "eve-vendor-stb.h"

namespace EveLib
{

unsigned char* StbDecode(const unsigned char* data, size_t size, int channels, int* width, int* height)
{
    if (size > 0x7FFFFFFF)
        return nullptr;
    int fileChannels = 0;
    return stbi_load_from_memory(data, static_cast<int>(size), width, height, &fileChannels, channels);
}

void StbFree(unsigned char* pixels)
{
    stbi_image_free(pixels);
}

} // namespace EveLib
