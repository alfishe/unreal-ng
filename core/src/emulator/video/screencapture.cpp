#include "screencapture.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/video/screen.h"
#include "3rdparty/gif/gif.h"
#include "3rdparty/lodepng/lodepng.h"

#include <cstdio>
#include <cstring>

// ZX Spectrum screen dimensions
static constexpr uint16_t ZX_SCREEN_WIDTH = 256;
static constexpr uint16_t ZX_SCREEN_HEIGHT = 192;

// ============================================================================
// Public API
// ============================================================================

ScreenCapture::CaptureResult ScreenCapture::captureAsGif(const std::string& emulatorId, CaptureMode mode)
{
    return captureScreen(emulatorId, "gif", mode);
}

ScreenCapture::CaptureResult ScreenCapture::captureAsPng(const std::string& emulatorId, CaptureMode mode)
{
    return captureScreen(emulatorId, "png", mode);
}

ScreenCapture::CaptureResult ScreenCapture::captureScreen(const std::string& emulatorId, 
                                                           const std::string& format,
                                                           CaptureMode mode)
{
    CaptureResult result;
    
    // Get emulator
    auto* manager = EmulatorManager::GetInstance();
    if (!manager)
    {
        result.errorMessage = "EmulatorManager not available";
        return result;
    }

    auto emulator = manager->GetEmulator(emulatorId);
    if (!emulator)
    {
        result.errorMessage = "Emulator not found: " + emulatorId;
        return result;
    }

    // Get framebuffer
    FramebufferDescriptor fb = emulator->GetFramebuffer();
    if (fb.memoryBuffer == nullptr || fb.width == 0 || fb.height == 0)
    {
        result.errorMessage = "Framebuffer not available";
        return result;
    }

    // Determine what to capture
    const uint8_t* imageData = nullptr;
    uint16_t imageWidth = 0;
    uint16_t imageHeight = 0;
    std::vector<uint8_t> screenOnlyData;

    if (mode == CaptureMode::ScreenOnly)
    {
        // Extract just the 256x192 screen area
        if (!extractScreenArea(fb, screenOnlyData))
        {
            result.errorMessage = "Failed to extract screen area";
            return result;
        }
        imageData = screenOnlyData.data();
        imageWidth = ZX_SCREEN_WIDTH;
        imageHeight = ZX_SCREEN_HEIGHT;
    }
    else  // FullFramebuffer
    {
        imageData = fb.memoryBuffer;
        imageWidth = fb.width;
        imageHeight = fb.height;
    }

    result.width = imageWidth;
    result.height = imageHeight;

    // Encode to requested format
    std::vector<uint8_t> encodedData;
    
    if (format == "png")
    {
        encodedData = encodeToPng(imageData, imageWidth, imageHeight);
        result.format = "png";
    }
    else  // Default to GIF
    {
        encodedData = encodeToGif(imageData, imageWidth, imageHeight);
        result.format = "gif";
    }

    if (encodedData.empty())
    {
        result.errorMessage = "Failed to encode image";
        return result;
    }

    result.originalSize = encodedData.size();
    result.base64Data = base64Encode(encodedData);
    result.success = true;

    return result;
}

// ============================================================================
// Screen Extraction
// ============================================================================

bool ScreenCapture::extractScreenArea(const FramebufferDescriptor& fb, std::vector<uint8_t>& outData)
{
    // Validate framebuffer has enough data
    if (fb.width < ZX_SCREEN_WIDTH || fb.height < ZX_SCREEN_HEIGHT)
    {
        return false;
    }

    // Calculate offset to screen area (centered in framebuffer)
    uint16_t offsetX = (fb.width - ZX_SCREEN_WIDTH) / 2;
    uint16_t offsetY = (fb.height - ZX_SCREEN_HEIGHT) / 2;

    // Allocate output buffer (RGBA)
    outData.resize(ZX_SCREEN_WIDTH * ZX_SCREEN_HEIGHT * 4);

    // Copy screen area line by line
    const uint8_t* src = fb.memoryBuffer;
    uint8_t* dst = outData.data();
    
    for (uint16_t y = 0; y < ZX_SCREEN_HEIGHT; y++)
    {
        const uint8_t* srcLine = src + ((offsetY + y) * fb.width + offsetX) * 4;
        uint8_t* dstLine = dst + (y * ZX_SCREEN_WIDTH) * 4;
        std::memcpy(dstLine, srcLine, ZX_SCREEN_WIDTH * 4);
    }

    return true;
}

// ============================================================================
// GIF Encoding
// ============================================================================

std::vector<uint8_t> ScreenCapture::encodeToGif(const uint8_t* data, uint16_t width, uint16_t height)
{
    std::vector<uint8_t> result;

    // GIF library writes to file, so we need a temp file approach
    char tempPath[256];
#ifdef _WIN32
    snprintf(tempPath, sizeof(tempPath), "%s\\unreal_capture_%p.gif", 
             std::getenv("TEMP") ? std::getenv("TEMP") : ".", data);
#else
    snprintf(tempPath, sizeof(tempPath), "/tmp/unreal_capture_%p.gif", static_cast<const void*>(data));
#endif

    GifWriter writer = {};
    
    // Start GIF (single frame, delay=0 for static image)
    if (!GifBegin(&writer, tempPath, width, height, 0))
    {
        return result;
    }

    // Write single frame
    if (!GifWriteFrame(&writer, data, width, height, 0))
    {
        GifEnd(&writer);
        std::remove(tempPath);
        return result;
    }

    // Finalize
    GifEnd(&writer);

    // Read file into memory
    FILE* f = fopen(tempPath, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (size > 0)
        {
            result.resize(size);
            size_t read = fread(result.data(), 1, size, f);
            (void)read;  // Suppress unused warning
        }
        fclose(f);
    }

    // Clean up temp file
    std::remove(tempPath);

    return result;
}

// ============================================================================
// PNG Encoding
// ============================================================================

std::vector<uint8_t> ScreenCapture::encodeToPng(const uint8_t* data, uint16_t width, uint16_t height)
{
    std::vector<uint8_t> result;

    // lodepng encodes to memory directly
    unsigned error = lodepng::encode(result, data, width, height);
    
    if (error)
    {
        result.clear();
    }

    return result;
}

// ============================================================================
// Base64 Encoding
// ============================================================================

std::string ScreenCapture::base64Encode(const std::vector<uint8_t>& data)
{
    static const char* chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        
        if (i + 1 < data.size())
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size())
            n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(chars[(n >> 18) & 0x3F]);
        result.push_back(chars[(n >> 12) & 0x3F]);
        
        if (i + 1 < data.size())
            result.push_back(chars[(n >> 6) & 0x3F]);
        else
            result.push_back('=');
            
        if (i + 2 < data.size())
            result.push_back(chars[n & 0x3F]);
        else
            result.push_back('=');
    }

    return result;
}
