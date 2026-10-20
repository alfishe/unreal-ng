#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emulator/video/screen.h"

/// @file windowview.h
/// @brief The picture as a GUI window draws it, for the automation surfaces (screenshot source "window"), and the
/// window's display settings (GPU acceleration, CRT effects, temporal blending).
///
/// The framebuffer and a screenshot say what the machine shows; how a front end puts that on the host display (the
/// shape it stretches the picture to, the letterbox, the CRT shader, the HUD) only the front end knows. The core has
/// no dependency on any UI: a front end that draws a machine registers an IWindowViewProvider, and the surfaces ask
/// through it. Without one (headless, tests, a front end that does not offer it) the source answers "no window"

/// One grab of a window's picture
struct WindowViewGrab
{
    std::vector<uint8_t> rgba;  ///< the drawn picture, RGBA, letterbox excluded, in the display's device pixels
    uint16_t width = 0;
    uint16_t height = 0;

    PictureRect viewport;       ///< the window's screen area (the picture plus its letterbox), logical pixels
    PictureRect picture;        ///< where the picture is drawn inside the viewport, logical pixels
    double devicePixelRatio = 1.0;
    std::string backend;        ///< how the window draws: "gpu" / "software"
};

/// How a window draws the picture: its display settings as the View menu shows them
struct WindowDisplaySettings
{
    std::string backend;                   ///< "gpu" / "software"
    bool gpuAvailable = false;             ///< the host can draw with the GPU
    bool gpu = false;                      ///< GPU acceleration on
    bool crt = false;                      ///< CRT effects on (a profile other than "none" switches them on)
    std::string crtProfile;                ///< e.g. "none", "basic", "shader:<name>"
    std::vector<std::string> crtProfiles;  ///< every profile the window offers
    bool blending = false;                 ///< temporal blending (gigascreen smoothing) on
    int blendingFrames = 2;                ///< frames blended, 2..5
    std::string blendingWeights;           ///< "equal" / "exponential"
    std::string emulatorId;                ///< the instance the window shows ("" = none)
};

/// A change of the display settings: only the fields set change
struct WindowDisplayChange
{
    std::optional<bool> gpu;
    std::optional<bool> crt;
    std::optional<std::string> crtProfile;
    std::optional<bool> blending;
    std::optional<int> blendingFrames;
    std::optional<std::string> blendingWeights;

    bool Empty() const
    {
        return !gpu && !crt && !crtProfile && !blending && !blendingFrames && !blendingWeights;
    }
};

/// Implemented by a front end (unreal-qt). Called from any thread: the implementation moves to its UI thread itself
class IWindowViewProvider
{
public:
    virtual ~IWindowViewProvider() = default;

    /// Grab the picture of the window that shows this emulator. False with `error` when no window shows it, or the
    /// window cannot be grabbed now
    virtual bool GrabWindowView(const std::string& emulatorId, WindowViewGrab& out, std::string& error) = 0;

    /// The window's display settings. False with `error` when the window cannot answer now
    virtual bool GetWindowDisplay(WindowDisplaySettings& out, std::string& error) = 0;

    /// Apply a change the caller validated (a profile from crtProfiles, gpu only when available, frames 2..5) the
    /// way the View menu does, then answer the settings as they are now
    virtual bool SetWindowDisplay(const WindowDisplayChange& change, WindowDisplaySettings& out, std::string& error) = 0;
};

/// The provider a front end registered (none by default). A call in flight keeps the provider it started with alive
class WindowView
{
public:
    static void SetProvider(std::shared_ptr<IWindowViewProvider> provider)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        Slot() = std::move(provider);
    }

    static std::shared_ptr<IWindowViewProvider> Provider()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return Slot();
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }
    static std::shared_ptr<IWindowViewProvider>& Slot()
    {
        static std::shared_ptr<IWindowViewProvider> provider;
        return provider;
    }
};
