#ifndef DEVICESCREEN_GL_WINDOW_H
#define DEVICESCREEN_GL_WINDOW_H

// Mute macOS OpenGL deprecation warnings
#define GL_SILENCE_DEPRECATION

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <functional>
#include <memory>
#include <atomic>

#include "emulator/video/screen.h"
#include "crtprofiles.h"
#include "framehistory.h"

class Emulator;

/// GPU-accelerated renderer using QOpenGLWindow for tear-free direct buffer swapping.
/// Unlike QOpenGLWidget (which renders to FBO then gets composited), this renders
/// directly to the window surface with proper vsync control.
class DeviceScreenGLWindow : public QOpenGLWindow, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit DeviceScreenGLWindow(QWindow* parent = nullptr);
    ~DeviceScreenGLWindow() override;

    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

    using FrameCopyFn = std::function<bool(uint8_t* dst, size_t dstSize)>;
    void setFrameSource(FrameCopyFn frameSource) { _frameSource = std::move(frameSource); }

    static constexpr int kNativeWidth = 352;
    static constexpr int kNativeHeight = 288;

signals:
    void fileDropped(const QString& filePath);
    void dragEntered();
    void dragLeft();

public slots:
    void refresh();
    void handleExternalKeyPress(QKeyEvent* event);
    void handleExternalKeyRelease(QKeyEvent* event);
    void prepareForShutdown();

public:
    void setEmulator(std::shared_ptr<Emulator> emulator) { _emulator = emulator; }
    void setDisplayViewport(const DisplayViewport& viewport);
    void clearDisplayViewport();
    QImage grabFramebuffer();

    void setCRTEffectsEnabled(bool enabled);
    bool crtEffectsEnabled() const { return _crtEnabled; }

    void setCRTProfile(CRTProfile profile);
    void setCRTProfile(const CRTProfileParams& params);
    CRTProfile crtProfile() const { return _crtParams.profile; }
    const CRTProfileParams& crtParams() const { return _crtParams; }

    bool loadCustomShader(const QString& name);
    static std::vector<QString> availableShaders();

    void setTemporalBlendingEnabled(bool enabled);
    bool temporalBlendingEnabled() const { return _temporalEnabled; }
    void setTemporalHistorySize(int frames);
    int temporalHistorySize() const;
    void setTemporalWeightMode(int mode);
    int temporalWeightMode() const { return _temporalWeightMode; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    void updateTexture();
    void setupVSync();

    QRectF _devicePixelsRect;
    QImage* _devicePixels = nullptr;
    QImage _latchedFrame;
    FrameCopyFn _frameSource;

    QOpenGLTexture* _texture = nullptr;
    std::atomic<bool> _textureNeedsUpdate{true};

    QOpenGLShaderProgram* _crtShader = nullptr;
    bool _crtEnabled = false;
    CRTProfileParams _crtParams;
    QString _customShaderSource;

    FrameHistory _frameHistory;
    QImage _blendedFrame;
    bool _temporalEnabled = false;
    int _temporalWeightMode = 0;

    static constexpr float ratio = static_cast<float>(kNativeWidth) / static_cast<float>(kNativeHeight);

    std::shared_ptr<Emulator> _emulator = nullptr;
    bool _isShuttingDown = false;

    DisplayViewport _displayViewport;
    bool _hasViewport = false;

    class HudOverlay* _hudOverlay = nullptr;
public:
    void setHudOverlay(class HudOverlay* overlay) { _hudOverlay = overlay; }
    class HudOverlay* hudOverlay() const { return _hudOverlay; }
};

#endif // DEVICESCREEN_GL_WINDOW_H
