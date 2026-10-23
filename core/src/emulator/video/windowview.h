#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emulator/video/screen.h"

/// @file windowview.h
/// @brief The picture as a GUI window draws it, for the automation surfaces (screenshot source "window"), and the
/// window's display settings (GPU acceleration, CRT effects and their parameters, temporal blending, full screen and
/// scale, toolbar / status bar, HUD, on-screen keyboard, the ZX Next layers window).
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
    double blendingDecay = 0.5;            ///< exponential weights: each older frame's share of the newer one, 0.1..0.9
    std::string emulatorId;                ///< the instance the window shows ("" = none)

    bool fullscreen = false;               ///< the window fills the display
    int scale = 0;                         ///< the whole zoom the window was fitted to, 1..4; 0 = none (resized by hand)
    bool toolbar = true;                   ///< View > Toolbar
    bool statusBar = true;                 ///< View > Status Bar

    bool hud = false;                      ///< the HUD overlay shown
    std::string hudTheme;                  ///< "dark_glass", "retro_zx", ...
    std::vector<std::string> hudThemes;    ///< every theme the HUD offers
    std::map<std::string, bool> hudNotices;  ///< the HUD's notice categories ("ram-page", "tape-events", ...): shown

    bool osdKeyboard = false;              ///< the on-screen keyboard window shown
    std::string osdKeyboardPicture;        ///< the keyboard picture it shows ("" = none for this machine)
    std::vector<std::string> osdKeyboardPictures;  ///< the pictures the shown machine offers
    std::string osdKeyboardStyle;          ///< how the pictures are drawn: "photo", "schematic", "hybrid"
    std::vector<std::string> osdKeyboardStyles;    ///< every style the pictures come in

    bool nextLayers = false;               ///< the ZX Next layers window shown
    std::string nextLayersMode;            ///< "layers" / "composed" / "winner"
    std::map<std::string, bool> nextLayersShown;  ///< its layer ticks: ula, layer2, tilemap, sprites
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
    std::optional<double> blendingDecay;
    std::optional<bool> fullscreen;
    std::optional<int> scale;
    std::optional<bool> toolbar;
    std::optional<bool> statusBar;
    std::optional<bool> hud;
    std::optional<std::string> hudTheme;
    std::map<std::string, bool> hudNotices;
    std::optional<bool> osdKeyboard;
    std::optional<std::string> osdKeyboardPicture;
    std::optional<std::string> osdKeyboardStyle;
    std::optional<bool> nextLayers;
    std::optional<std::string> nextLayersMode;
    std::map<std::string, bool> nextLayersShown;

    bool Empty() const
    {
        return !gpu && !crt && !crtProfile && !blending && !blendingFrames && !blendingWeights && !blendingDecay &&
               !fullscreen && !scale && !toolbar && !statusBar && !hud && !hudTheme && hudNotices.empty() &&
               !osdKeyboard && !osdKeyboardPicture && !osdKeyboardStyle && !nextLayers && !nextLayersMode && nextLayersShown.empty();
    }
};

/// One parameter of the CRT picture the window draws: a libretro preset's (its preset.json) or a built-in profile's
struct CrtParameter
{
    std::string name;
    std::string description;
    double value = 0;
    double initial = 0;  ///< the value reset goes back to
    double minimum = 0;
    double maximum = 0;
    double step = 0;     ///< one notch of a dial
};

/// The parameters of the active CRT profile
struct CrtParameters
{
    std::string profile;  ///< as WindowDisplaySettings::crtProfile
    /// "preset" (a libretro preset's own parameters), "profile" (the built-in profile's curvature, scanlines, ...;
    /// also the uniforms of a custom shader), "none" (a profile without parameters: none, the beam models)
    std::string kind = "none";
    std::vector<CrtParameter> parameters;
};

/// A change of the active profile's parameters, applied in this order: reset, values, steps
struct CrtParameterChange
{
    bool resetAll = false;
    std::vector<std::string> reset;                       ///< back to their initial values
    std::vector<std::pair<std::string, double>> values;   ///< set (clamped to the range)
    std::vector<std::pair<std::string, double>> steps;    ///< moved by this many steps (a dial's delta), clamped

    bool Empty() const { return !resetAll && reset.empty() && values.empty() && steps.empty(); }
};

/// A recording of the window as it draws (CRT and all), optionally with the on-screen keyboard under the picture
struct WindowRecordingRequest
{
    std::string path;      ///< the file; the container is its extension (.mov for ProRes)
    std::string keyboard;  ///< "" = the picture only; "default" = the model's keyboard; else a keyboard picture id
    std::string codec = "prores";  ///< "prores" (ProRes 4444, full colour) or "h264" (4:4:4)
    int fps = 0;       ///< 0: the machine's frame rate (3.5 MHz / t-states per frame: 48.83 on a Pentagon)
    bool audio = true; ///< the machine's sound, frame for frame with the picture (needs fps 0)
    int scale = 1;  ///< the window's pixels as scale x scale blocks (1..4); the keyboard drawn at the file's width
    int pictureHeight = 0;  ///< > 0: the CRT drawn at this height in device pixels, not the window's (GPU only)
    uint16_t frameWidth = 0;   ///< the file's frame; 0: the picture with the keyboard under it
    uint16_t frameHeight = 0;
    std::string layout = "below";  ///< the keyboard "below" the picture or "side" (right of it, for 16:9)
};

struct WindowRecordingStatus
{
    bool recording = false;
    std::string path;
    std::string keyboard;  ///< the keyboard picture drawn ("" = none)
    uint64_t frames = 0;   ///< video frames written: one per emulated frame (a frame the window skipped repeats)
    uint16_t width = 0;
    uint16_t height = 0;
    double fps = 0;        ///< the file's frame rate
    bool audio = false;    ///< a sound track
    uint32_t sampleRate = 0;
    uint64_t audioSamples = 0;  ///< sample pairs taken (frames x samples per frame)
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

    /// The active CRT profile's parameters. False with `error` when the window cannot answer now (default: a front end
    /// that offers none)
    virtual bool GetCrtParameters(CrtParameters& out, std::string& error)
    {
        (void)out;
        error = "This window offers no CRT parameters";
        return false;
    }
    /// Apply a change the caller validated (names from GetCrtParameters), then answer the parameters as they are now
    virtual bool SetCrtParameters(const CrtParameterChange& change, CrtParameters& out, std::string& error)
    {
        (void)change;
        (void)out;
        error = "This window offers no CRT parameters";
        return false;
    }
    /// Start / stop recording the window (and the on-screen keyboard). A front end without it says so
    virtual bool StartWindowRecording(const std::string& emulatorId, const WindowRecordingRequest& request,
                                      WindowRecordingStatus& out, std::string& error)
    {
        (void)emulatorId;
        (void)request;
        (void)out;
        error = "This front end does not record its window";
        return false;
    }
    virtual bool StopWindowRecording(WindowRecordingStatus& out, std::string& error)
    {
        (void)out;
        error = "This front end does not record its window";
        return false;
    }
    virtual bool GetWindowRecording(WindowRecordingStatus& out, std::string& error)
    {
        (void)out;
        error = "This front end does not record its window";
        return false;
    }
};

/// The provider a front end registered (none by default). A call in flight keeps the provider it started with alive
class WindowView
{
public:
    static void SetProvider(std::shared_ptr<IWindowViewProvider> provider)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        Slot() = std::move(provider);
        if (!Slot())
        {
            Published().reset();  // no window: nothing published stays
            PublishedParameters().reset();
        }
    }

    static std::shared_ptr<IWindowViewProvider> Provider()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return Slot();
    }

    /// The front end publishes its settings whenever they change (on its own thread): the readouts and the toggles
    /// of the catalogue read this copy, never waiting for the GUI. Withdrawn (nullopt) when the window goes
    static void Publish(std::optional<WindowDisplaySettings> settings, std::optional<CrtParameters> parameters = {})
    {
        std::lock_guard<std::mutex> lock(Mutex());
        Published() = std::move(settings);
        if (parameters || !Published())
            PublishedParameters() = std::move(parameters);
    }

    /// The settings the front end published last; nullopt headless
    static std::optional<WindowDisplaySettings> PublishedSettings()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return Published();
    }

    static std::optional<CrtParameters> PublishedCrtParameters()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return PublishedParameters();
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
    static std::optional<WindowDisplaySettings>& Published()
    {
        static std::optional<WindowDisplaySettings> settings;
        return settings;
    }
    static std::optional<CrtParameters>& PublishedParameters()
    {
        static std::optional<CrtParameters> parameters;
        return parameters;
    }
};
