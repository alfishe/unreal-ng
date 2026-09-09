#pragma once

#include <QWidget>
#include <QPointer>
#include <QImage>
#include <functional>
#include <memory>

#include "emulator/video/screen.h"
#include "crtprofiles.h"

class Emulator;
class DeviceScreen;
class DeviceScreenGL;
class DeviceScreenGLWindow;

/// @brief Unified wrapper for DeviceScreen implementations.
/// Automatically selects GPU or software backend based on platform capabilities.
/// All calls delegate to the active implementation transparently.
class DeviceScreenWrapper : public QObject
{
    Q_OBJECT

signals:
    void screenInitialized();
    void fileDropped(const QString& filePath);
    void dragEntered();
    void dragLeft();

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
    void init(uint16_t width, uint16_t height, void* buffer);

    /// @brief Detach from current framebuffer
    void detach();

    /// @brief Request screen refresh
    void refresh();

    /// @brief Set frame copy function for tear-free rendering
    void setFrameSource(std::function<bool(uint8_t*, size_t)> fn);

    /// @brief Associate emulator instance for keyboard routing
    void setEmulator(std::shared_ptr<Emulator> emulator);

    /// @brief Set display viewport cropping
    void setDisplayViewport(const DisplayViewport& viewport);

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

    /// @brief Get the underlying widget for layout purposes
    QWidget* widget() const { return _widget; }

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

    /// @brief Load custom shader from file
    bool loadCustomShader(const QString& name);
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

private:
    QPointer<QWidget> _widget;  // Guarded: the parent may destroy the widget before the wrapper
    DeviceScreen* _software = nullptr;
    DeviceScreenGLWindow* _gpuWindow = nullptr;  // QOpenGLWindow for tear-free vsync
    bool _useGPU = false;

    // Track viewport state for getters
    DisplayViewport _displayViewport;
    bool _hasViewport = false;
};
