#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Emulator;
struct FramebufferDescriptor;

/// @brief Capture mode: screen area only or full framebuffer with border
enum class CaptureMode
{
    ScreenOnly,     ///< Screen area only (dimensions depend on video mode)
    FullFramebuffer ///< Full rendered framebuffer including border
};

/// @brief Screen capture utility for exporting screen to image formats
/// Located in video module since it works with framebuffer data directly
class ScreenCapture
{
public:
    /// Capture result with base64-encoded image data
    struct CaptureResult
    {
        bool success = false;
        std::string format;       // "gif" or "png"
        size_t originalSize = 0;  // Unencoded byte count
        uint16_t width = 0;
        uint16_t height = 0;
        std::string base64Data;   // Base64-encoded image
        std::string errorMessage;
    };

    /// @brief Capture current screen as GIF (single frame)
    /// @param emulatorId Emulator UUID
    /// @param mode ScreenOnly (mode-dependent) or FullFramebuffer (with border)
    /// @return CaptureResult with base64-encoded GIF data
    static CaptureResult captureAsGif(const std::string& emulatorId,
                                       CaptureMode mode = CaptureMode::ScreenOnly);

    /// @brief Capture current screen as PNG
    /// @param emulatorId Emulator UUID
    /// @param mode ScreenOnly (mode-dependent) or FullFramebuffer (with border)
    /// @return CaptureResult with base64-encoded PNG data
    static CaptureResult captureAsPng(const std::string& emulatorId,
                                       CaptureMode mode = CaptureMode::ScreenOnly);

    /// @brief Capture current screen with specified format
    /// @param emulatorId Emulator UUID
    /// @param format "gif" or "png" (default: gif)
    /// @param mode ScreenOnly (mode-dependent) or FullFramebuffer (with border)
    /// @return CaptureResult with base64-encoded image data
    static CaptureResult captureScreen(const std::string& emulatorId,
                                        const std::string& format = "gif",
                                        CaptureMode mode = CaptureMode::ScreenOnly);

private:
    /// @brief Encode image data to GIF format
    static std::vector<uint8_t> encodeToGif(const uint8_t* data, uint16_t width, uint16_t height);
    
    /// @brief Encode image data to PNG format
    static std::vector<uint8_t> encodeToPng(const uint8_t* data, uint16_t width, uint16_t height);
    
    /// @brief Extract screen area from framebuffer
    /// @param fb Full framebuffer
    /// @param outData Output buffer (resized to screenWidth*screenHeight*4)
    /// @param screenWidth Width of screen area to extract
    /// @param screenHeight Height of screen area to extract
    /// @param offsetX X offset from framebuffer origin
    /// @param offsetY Y offset from framebuffer origin
    /// @return true if extraction succeeded
    static bool extractScreenArea(const FramebufferDescriptor& fb, std::vector<uint8_t>& outData,
                                   uint16_t screenWidth, uint16_t screenHeight,
                                   uint16_t offsetX, uint16_t offsetY);
    
    /// @brief Base64 encode binary data
    static std::string base64Encode(const std::vector<uint8_t>& data);
};
