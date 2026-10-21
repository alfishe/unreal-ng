#ifndef DEVICESCREEN_GL_WINDOW_H
#define DEVICESCREEN_GL_WINDOW_H

// Mute macOS OpenGL deprecation warnings
#define GL_SILENCE_DEPRECATION

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QSurfaceFormat>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>

#include "emulator/video/screen.h"
#include "crtbeammodel.h"
#include "crtbeammodelgpu.h"
#include "crtprofiles.h"
#include "crtrenderer.h"
#include "framehistory.h"
#include "slangpresetrunner.h"

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

    /// A hardware OpenGL 3.3 core context (4.1 on macOS) can be created, not llvmpipe, GDI Generic, ...: otherwise
    /// the software screen
    static bool probeGPUAcceleration();
    /// The window's surface format: 3.3 core, double buffered, vsync
    static QSurfaceFormat surfaceFormat();

    /// `aspect`: width : height the picture is drawn in; 0 = the CRT frame's 352:288 (every machine raster).
    /// A video card's own picture (the VDAC2's FT812) has square pixels and gives its own
    void init(uint16_t width, uint16_t height, void* buffer, float aspect = 0.0f);
    void detach();

    using FrameCopyFn = std::function<bool(uint8_t* dst, size_t dstSize)>;
    void setFrameSource(FrameCopyFn frameSource)
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        _frameSource = std::move(frameSource);
    }

    /// Thread-safe cut of every paint path into the emulator's framebuffer
    /// (the tear-free provider captures raw Screen*/EmulatorContext* pointers,
    /// the legacy wrapper borrows the live buffer). Safe to call from the
    /// MessageCenter worker; paints after it simply find no source.
    void clearFrameSource();

    static constexpr int kNativeWidth = 352;
    static constexpr int kNativeHeight = 288;

signals:
    void filesDropped(const QStringList& paths);
    void dragEntered(const QString& filePath);  ///< the first dragged file (empty: not a local file)
    void dragLeft();

public slots:
    void refresh();
    void handleExternalKeyPress(QKeyEvent* event);
    void handleExternalKeyRelease(QKeyEvent* event);
    void prepareForShutdown();

public:
    void setEmulator(std::shared_ptr<Emulator> emulator) { _emulator = emulator; }

    /// Host mouse capture (owned by the screen wrapper): mouse, wheel, focus and
    /// the release key go there first
    void setMouseCapture(class MouseCaptureController* capture) { _mouseCapture = capture; }
    /// Framebuffer area currently drawn (viewport-cropped), in emulated pixels
    QRectF displaySourceRect() const;
    /// Size of the drawn picture inside the window (the quad paintGL letterboxes), logical pixels
    QSize drawnPictureSize() const;
    /// Where paintGL draws the picture: centered, device pixels, rows top first. The CRT composition is made for
    /// exactly this size
    QRect pictureDeviceRect() const;
    void setDisplayViewport(const DisplayViewport& viewport);
    void clearDisplayViewport();
    QImage grabFramebuffer();
    /// The window as it draws now (picture, letterbox, CRT shader, HUD), rendered afresh into an image in device
    /// pixels. QOpenGLWindow::grabFramebuffer only reads the back buffer, which after a swap may hold an older frame
    QImage renderWindowImage();

    void setCRTEffectsEnabled(bool enabled);
    bool crtEffectsEnabled() const { return _crtEnabled; }

    void setCRTProfile(CRTProfile profile);
    void setCRTProfile(const CRTProfileParams& params);
    CRTProfile crtProfile() const { return _crtParams.profile; }
    const CRTProfileParams& crtParams() const { return _crtParams; }
    /// New values of the profile's parameters (curvature, scanlines, ...), the custom shader kept (its uniforms)
    void setCRTParams(const CRTProfileParams& params);

    /// The active libretro preset's parameters with their values (none without a preset). Before the context
    /// exists they come from its preset.json, the values set kept
    std::vector<SlangPresetRunner::Parameter> presetParameters() const;
    /// One parameter of the active preset, clamped to its range; kept across a reload of the same preset. False: no
    /// such parameter
    bool setPresetParameter(const QString& name, float value);
    /// Every parameter of the active preset back to its initial value
    void resetPresetParameters();
    /// The values set on the active preset (name -> value), carried across a backend switch and kept in the settings
    const std::map<QString, float>& presetOverrides() const { return _slangOverrides; }
    void setPresetOverrides(const std::map<QString, float>& overrides);

    /// A libretro preset built in (SlangPresetRunner::availablePresets, multi-pass on SlangPresetRunner) or a fragment
    /// shader from CRTShaderManager (legacy uniforms: tex, texSize, outputSize, maskType, curvature, ...) in place of
    /// the built-in CRT. An empty name goes back to the built-in one. @return false: not found or not compiled
    bool loadCustomShader(const QString& name);
    QString customShader() const { return _slangPresetName.isEmpty() ? _customShaderName : _slangPresetName; }
    /// The presets, then the custom shaders
    static std::vector<QString> availableShaders();

    void setTemporalBlendingEnabled(bool enabled);
    bool temporalBlendingEnabled() const { return _temporalEnabled; }
    void setTemporalHistorySize(int frames);
    int temporalHistorySize() const;
    void setTemporalWeightMode(int mode);
    int temporalWeightMode() const { return _temporalWeightMode; }
    /// Exponential weights: each older frame's share of the newer one (0.1..0.9)
    void setTemporalDecay(float decay);
    float temporalDecay() const { return _temporalDecay; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    void updateTexture();
    void setupVSync();
    bool compileCustomShader(const QString& source);  ///< the context current; empty: none
    bool applySlangPreset(const QString& name);       ///< the context current; empty: none
    /// The viewport-filling quad with the bound program, the texture coordinates from (x1, y1) top left to (x2, y2)
    void drawQuad(QOpenGLShaderProgram* program, float srcX1, float srcY1, float srcX2, float srcY2);
    void drawComposedCrt(const QRect& picture);
    void drawModelCrt(const QRect& picture);
    void drawCustomCrt(const QRect& picture);
    void drawSlangPreset(const QRect& picture, int glY);

    QRectF _devicePixelsRect;
    QImage* _devicePixels = nullptr;
    QImage _latchedFrame;
    FrameCopyFn _frameSource;
    mutable std::mutex _frameSourceMutex;  // Guards _frameSource + _devicePixels across threads

    QOpenGLTexture* _texture = nullptr;
    std::atomic<bool> _textureNeedsUpdate{true};

    const QImage* _shownFrame = nullptr;  ///< the frame in _texture: _latchedFrame, _blendedFrame or _legacyFrame
    QImage _legacyFrame;
    uint64_t _frameSerial = 0;           ///< counts uploads into _texture
    uint64_t _gradedSerial = ~0ull;      ///< the upload _gradedTexture was graded from

    // CRT: the composition of CRTRenderer (the software screen's picture, bit for bit), or a custom shader
    QOpenGLShaderProgram* _composeShader = nullptr;
    QOpenGLShaderProgram* _customShader = nullptr;
    QOpenGLTexture* _gradedTexture = nullptr;
    QOpenGLTexture* _indexTexture = nullptr;
    QOpenGLTexture* _factorTexture = nullptr;
    QOpenGLVertexArrayObject _quadVao;
    QOpenGLBuffer _quadBuffer{QOpenGLBuffer::VertexBuffer};
    CRTRenderer _crtRenderer;
    // The shader models (crt-lottes, ...): CRTBeamModel's tables as integer textures, its kShader
    CRTBeamModel _crtModel;
    CRTBeamModelGPU _modelGpu;
    bool _modelTablesUploaded = false;
    uint64_t _modelSerial = ~0ull;  ///< the upload the model's frame was prepared from
    bool _crtEnabled = false;
    CRTProfileParams _crtParams;
    QString _customShaderName;
    QString _customShaderSource;
    std::unique_ptr<SlangPresetRunner> _slangRunner;  ///< the chosen preset, loaded (its GL objects need the context)
    QString _slangPresetName;
    std::map<QString, float> _slangOverrides;  ///< the values set on _slangPresetName, applied after every load

    FrameHistory _frameHistory;
    QImage _blendedFrame;
    bool _temporalEnabled = false;
    int _temporalWeightMode = 0;
    float _temporalDecay = 0.5f;

    static constexpr float kNativeRatio = static_cast<float>(kNativeWidth) / static_cast<float>(kNativeHeight);
    float ratio = kNativeRatio;  // the shape the picture is drawn in (init)

    std::shared_ptr<Emulator> _emulator = nullptr;
    class MouseCaptureController* _mouseCapture = nullptr;  // owned by DeviceScreenWrapper
    bool _isShuttingDown = false;

    DisplayViewport _displayViewport;
    bool _hasViewport = false;

    class HudOverlay* _hudOverlay = nullptr;
    bool _renderingImage = false;  ///< paintGL draws for renderWindowImage(): no HUD into the window
public:
    void setHudOverlay(class HudOverlay* overlay) { _hudOverlay = overlay; }
    class HudOverlay* hudOverlay() const { return _hudOverlay; }
};

#endif // DEVICESCREEN_GL_WINDOW_H
