#pragma once

#include <cstdint>
#include <string>
#include <vector>


/// @brief Capture mode: screen area only or full framebuffer with border
enum class CaptureMode
{
    ScreenOnly,     ///< Screen area only (dimensions depend on video mode)
    FullFramebuffer ///< Full rendered framebuffer including border
};

/// @brief Legacy entry point of the screenshot, kept for the surfaces that have not moved yet
/// (WebAPI, CLI, Python). It is a thin shim over Screenshotter (screenshotter.h): the frame is the
/// presented one and the screen area is the frame's own working window, whatever the machine.
/// New code uses Screenshotter directly.
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
        std::string savedFile;    // Path if saved directly to file
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
    /// @param filePath Optional file path to save binary image directly to disk
    /// @return CaptureResult with base64-encoded image data or saved file path
    static CaptureResult captureScreen(const std::string& emulatorId,
                                        const std::string& format = "gif",
                                        CaptureMode mode = CaptureMode::ScreenOnly,
                                        const std::string& filePath = "");
};
