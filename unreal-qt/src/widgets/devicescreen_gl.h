#ifndef DEVICESCREEN_GL_H
#define DEVICESCREEN_GL_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <functional>
#include <memory>

#include "emulator/video/screen.h"
#include "crtprofiles.h"
#include "framehistory.h"

class Emulator;

class DeviceScreenGL : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit DeviceScreenGL(QWidget* parent = nullptr);
    ~DeviceScreenGL() override;

    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

    using FrameCopyFn = std::function<bool(uint8_t* dst, size_t dstSize)>;
    void setFrameSource(FrameCopyFn frameSource) { _frameSource = std::move(frameSource); }

    static constexpr int kNativeWidth = 352;
    static constexpr int kNativeHeight = 288;

    QSize sizeHint() const override { return QSize(kNativeWidth, kNativeHeight); }

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

    // Temporal blending (gigascreen flicker smoothing)
    void setTemporalBlendingEnabled(bool enabled);
    bool temporalBlendingEnabled() const { return _temporalEnabled; }
    void setTemporalHistorySize(int frames);  // 2-5 frames
    int temporalHistorySize() const;
    void setTemporalWeightMode(int mode);  // 0=equal, 1=exponential
    int temporalWeightMode() const { return _temporalWeightMode; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateTexture();

    QRectF _devicePixelsRect;
    QImage* _devicePixels = nullptr;
    QImage _latchedFrame;
    FrameCopyFn _frameSource;

    QOpenGLTexture* _texture = nullptr;
    bool _textureNeedsUpdate = true;

    // CRT shader
    QOpenGLShaderProgram* _crtShader = nullptr;
    bool _crtEnabled = false;
    CRTProfileParams _crtParams;
    QString _customShaderSource;

    // Temporal blending (uses CPU-side blending before texture upload)
    FrameHistory _frameHistory;
    QImage _blendedFrame;
    bool _temporalEnabled = false;
    int _temporalWeightMode = 0;  // 0=equal, 1=exponential

    static constexpr float ratio = static_cast<float>(kNativeWidth) / static_cast<float>(kNativeHeight);

    std::shared_ptr<Emulator> _emulator = nullptr;
    bool _isShuttingDown = false;

    DisplayViewport _displayViewport;
    bool _hasViewport = false;
};

namespace DeviceScreenFactory
{
    bool probeGPUAcceleration();
    QWidget* create(QWidget* parent);
}

#endif // DEVICESCREEN_GL_H
