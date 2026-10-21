#pragma once

#include <QWidget>
#include <QPointer>
#include <QImage>
#include <functional>
#include <map>
#include <memory>

#include "emulator/video/screen.h"
#include "emulator/video/windowview.h"
#include "crtprofiles.h"
#include "slangpresetrunner.h"

class Emulator;
class DeviceScreen;
class DeviceScreenGLWindow;
class MouseCaptureController;

/// @brief Unified wrapper for DeviceScreen implementations.
/// Automatically selects GPU or software backend based on platform capabilities.
/// All calls delegate to the active implementation transparently.
class DeviceScreenWrapper : public QObject
{
    Q_OBJECT

signals:
    void screenInitialized();
    /// The native picture size changed: a new framebuffer (video mode) or another crop
    void pictureGeometryChanged();
    void filesDropped(const QStringList& paths);
    void dragEntered(const QString& filePath);
    void dragLeft();
    /// After every refresh(): windows that must show the same moment as the
    /// screen (FT812 Debug) refresh with it
    void refreshed();

public:

public:
    /// @brief Create wrapper with auto-detection (GPU if available, else software)
    explicit DeviceScreenWrapper(QWidget* parent = nullptr);

    /// @brief Create wrapper with explicit mode selection
    /// @param parent Parent widget
    /// @param useGPU true = force GPU (falls back to software if unavailable), false = force software
    DeviceScreenWrapper(QWidget* parent, bool useGPU);

    ~DeviceScreenWrapper();

    /// @brief Initialize with framebuffer dimensions and memory
    /// `aspect`: width : height the picture is drawn in; 0 = the CRT frame's 352:288 (every machine raster).
    /// A video card's own picture (the VDAC2's FT812) has square pixels and gives its own
    void init(uint16_t width, uint16_t height, void* buffer, float aspect = 0.0f);
    /// @brief Initialize with an emulator's framebuffer, in the shape the screen shows it (see PictureAspect)
    void init(const FramebufferDescriptor& framebuffer, const Screen& screen);
    /// @brief Width : height a screen's current picture is drawn in: its own for a video card's picture
    /// (square pixels), 0 (the CRT frame's 352:288) for the machine's raster
    static float PictureAspect(const FramebufferDescriptor& framebuffer, const Screen& screen);
    /// @brief The shape init() was given (0 = the CRT frame's 352:288)
    float pictureAspect() const { return _pictureAspect; }

    /// @brief Detach from current framebuffer
    void detach();

    /// @brief Request screen refresh
    void refresh();

    /// @brief Set frame copy function for tear-free rendering
    void setFrameSource(std::function<bool(uint8_t*, size_t)> fn);

    /// @brief Thread-safe cut of the frame source and legacy live-buffer path.
    /// Safe to call from the MessageCenter worker while an emulator instance
    /// is being destroyed (cuts every paint path into freed framebuffer memory).
    void clearFrameSource();

    /// @brief Associate emulator instance for keyboard routing
    void setEmulator(std::shared_ptr<Emulator> emulator);

    /// @brief Set display viewport cropping
    void setDisplayViewport(const DisplayViewport& viewport);

    /// @brief Associate HUD overlay for GPU/software rendering
    void setHudOverlay(class HudOverlay* overlay);

    /// @brief Clear display viewport (show full frame)
    void clearDisplayViewport();

    /// @brief Check if a viewport is currently set
    bool hasViewport() const { return _hasViewport; }

    /// @brief Get current viewport (valid only if hasViewport() is true)
    const DisplayViewport& displayViewport() const { return _displayViewport; }

    /// @brief Prepare for application shutdown
    void prepareForShutdown();

    /// @brief Handle external key press
    void handleExternalKeyPress(QKeyEvent* event);

    /// @brief Handle external key release
    void handleExternalKeyRelease(QKeyEvent* event);

    /// @brief Grab current framebuffer as QImage
    QImage grabFramebuffer();

    /// @brief Get native size hint
    QSize sizeHint() const;

    /// @brief Largest ratio-conforming geometry inside the parent widget, centered.
    /// Software backend: delegates to DeviceScreen::fitToParent() (grow-capable).
    /// GPU backend: the container fills the parent completely; the GL shader
    /// letterboxes to the native aspect ratio internally, so no resize dance is needed.
    void fitToParent();

    /// @brief Get the underlying widget for layout purposes
    QWidget* widget() const { return _widget; }

    /// @brief Where the picture is drawn, in global screen coordinates (letterbox
    /// excluded), and which framebuffer area it shows: windows drawn in the
    /// screen's scale (FT812 Debug) line their rows up with it
    QRect pictureGlobalRect() const;
    QRectF pictureSourceRect() const;
    /// @brief The picture as this screen draws it, for the automation's screenshot source "window" (WindowView):
    /// letterbox excluded, CRT shader and HUD included, in device pixels. GUI thread only
    bool grabWindowView(WindowViewGrab& out, std::string& error);

    /// @brief Host mouse capture of this screen (both backends); owned by the wrapper
    MouseCaptureController* mouseCapture() const { return _mouseCapture; }

    /// @brief Returns true if GPU-accelerated rendering is active
    bool isGPUAccelerated() const { return _useGPU; }

    /// @brief Check if GPU acceleration is available on this system
    static bool isGPUAvailable();

    /// @brief Enable/disable CRT effects (GPU mode only)
    void setCRTEffectsEnabled(bool enabled);
    bool crtEffectsEnabled() const;

    /// @brief Set CRT profile by enum
    void setCRTProfile(CRTProfile profile);
    /// @brief Set CRT profile with custom parameters
    void setCRTProfile(const CRTProfileParams& params);
    /// @brief Get current CRT profile
    CRTProfile crtProfile() const;
    /// @brief Get current CRT profile parameters
    CRTProfileParams crtParams() const;
    /// New values of the profile's parameters (curvature, scanlines, ...): the profile, the switch and a custom shader
    /// stay
    void setCRTParams(const CRTProfileParams& params);

    /// The active libretro preset's parameters (GPU only; none without a preset)
    std::vector<SlangPresetRunner::Parameter> presetParameters() const;
    bool setPresetParameter(const QString& name, float value);
    void resetPresetParameters();
    /// The values set on the active preset, for a backend switch and the settings
    std::map<QString, float> presetOverrides() const;
    void setPresetOverrides(const std::map<QString, float>& overrides);

    /// @brief Load custom shader from file
    bool loadCustomShader(const QString& name);
    /// The custom shader shown in place of the built-in CRT (empty: none; always empty without the GPU)
    QString customShader() const;
    /// @brief Get list of available custom shaders
    static std::vector<QString> availableShaders();

    /// @brief Enable/disable temporal blending (gigascreen smoothing)
    void setTemporalBlendingEnabled(bool enabled);
    bool temporalBlendingEnabled() const;
    /// @brief Set temporal history size (2-5 frames)
    void setTemporalHistorySize(int frames);
    int temporalHistorySize() const;
    /// @brief Set temporal weight mode (0=equal, 1=exponential)
    void setTemporalWeightMode(int mode);
    int temporalWeightMode() const;
    /// @brief Exponential weights: each older frame's share of the newer one (0.1..0.9)
    void setTemporalDecay(float decay);
    float temporalDecay() const;

protected:
    /// GPU backend: Tab / Shift+Tab that reach the window container (the container has the Qt focus while the
    /// native focus is still in the main window) go to the GL window instead of moving the focus
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QWidget> _widget;  // Guarded: the parent may destroy the widget before the wrapper
    DeviceScreen* _software = nullptr;
    DeviceScreenGLWindow* _gpuWindow = nullptr;  // QOpenGLWindow for tear-free vsync
    HudOverlay* _hudOverlay = nullptr;
    bool _useGPU = false;

    void setupMouseCapture();
    MouseCaptureController* _mouseCapture = nullptr;  // QObject child of the wrapper
    std::weak_ptr<Emulator> _emulator;                // the machine the mouse settings come from

    // Track viewport state for getters
    DisplayViewport _displayViewport;
    bool _hasViewport = false;
    float _pictureAspect = 0.0f;
};
