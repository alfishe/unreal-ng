#ifndef DEVICESCREEN_GL_WINDOW_H
#define DEVICESCREEN_GL_WINDOW_H

// Mute macOS OpenGL deprecation warnings
#define GL_SILENCE_DEPRECATION

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QWindow>
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

#include "emulator/video/displayrate.h"
#include "emulator/video/screen.h"
#include "emulator/video/smoothtrace.h"
#include "crtbeammodel.h"
#include "crtbeammodelgpu.h"
#include "crtprofiles.h"
#include "crtrenderer.h"
#include "framehistory.h"
#include "framesource.h"
#include "slangpresetrunner.h"
#include "smoothmotiongpu.h"

class Emulator;
class NativePresenter;
class DisplayModeSwitch;
class QTimer;

/// GPU renderer of the emulator's screen (CRT composition, beam models, presets, HUD), on a window of its own with
/// its own GL context. Two ways to the screen (frame presenter design, docs/inprogress/2026-10-26-frame-presenter):
/// - GL: the window's GL surface, swapped on vsync; every refresh draws the frame due when it reaches the screen
///   (b-vsync, section 4.2). Every platform.
/// - Native (macOS, Windows): GL draws each frame into a texture shared with the platform's API, and a
///   NativePresenter shows it at the frame's audio time (c-vrr, section 4.1): on a VRR display every frame is on screen
///   for its own period. macOS: the window is a Metal surface, IOSurfaces. Windows: WGL_NV_DX_interop2 and a DXGI
///   swap chain; without the extension the window falls back to GL. UNREAL_PRESENTER=gl keeps the GL way.
class DeviceScreenGLWindow : public QWindow, protected QOpenGLFunctions
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

    /// The frames by time: every refresh draws the frame due when it reaches the screen. While frames come, the
    /// window repaints on every refresh (requestUpdate), and stops once nothing is pending. With smooth motion
    /// (ScreenFrames::smooth, UNREAL_SMOOTH_MOTION overrides it) and the frames in order, a fixed display gets a
    /// picture for each of its refreshes (smoothmotion.h)
    void setFrameSource(ScreenFrames source);
    void setFrameSource(FrameSourceFn frameSource) { setFrameSource(ScreenFrames{std::move(frameSource)}); }
    /// The game's picture cadence from its profile (nullptr: none): with Mode::Interpolate, the steps between the
    /// game's own pictures
    void setGameVideo(const smoothmotion::GameVideo* game) { _smooth.SetGame(game); }
    /// The smooth motion in effect (Off without frames in order)
    smoothmotion::Mode smoothMotion() const { return smoothActive() ? _smooth.GetMode() : smoothmotion::Mode::Off; }
    const smoothmotion::SmoothMotion::Stats& smoothMotionStats() const { return _smooth.GetStats(); }

    /// Thread-safe cut of every paint path into the emulator's framebuffer
    /// (the tear-free provider captures raw Screen*/EmulatorContext* pointers,
    /// the legacy wrapper borrows the live buffer). Safe to call from the
    /// MessageCenter worker; paints after it simply find no source.
    void clearFrameSource();

    /// The window shows its frames through the native presenter (decided at construction)
    bool nativePresent() const { return _native; }
    /// The presenter and its on-screen timing, one line (empty without the native presenter)
    QString presenterStatus() const;
    /// The presenter's on-screen timing and smooth motion's counters, for the automation
    WindowPresentStats presentStats() const;

    /// The context exists and the GL objects are made (the first paint)
    bool isValid() const { return _context && _glInitialized; }
    /// The window's context current (on the window's surface, or the offscreen one of the native way)
    bool makeCurrent();
    void doneCurrent();
    QOpenGLContext* context() const { return _context.get(); }
    /// A paint soon (requests coalesce); with frames flowing on the native way, the next timed present takes it
    void update();

    static constexpr int kNativeWidth = 352;
    static constexpr int kNativeHeight = 288;

signals:
    void filesDropped(const QStringList& paths);
    void dragEntered(const QString& filePath);  ///< the first dragged file (empty: not a local file)
    void dragLeft();

public slots:
    /// A new frame was latched: the refresh loop runs
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
    /// @p pictureHeight > 0: the picture alone, drawn that many device pixels high (the window recording's own
    /// resolution); else the window as it is
    QImage renderWindowImage(bool hud = true, int pictureHeight = 0);

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
    void initializeGL();
    void resizeGL(int w, int h);
    /// Draws the frame in _texture (updateTexture) and the HUD into the bound framebuffer
    void paintGL();

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// probeGPUAcceleration's work, done once
    static bool probeGPUAccelerationOnce();
    /// The context made and current, the GL objects initialised on the first call
    bool prepareContext();
    /// GL way: draw the frame due at the next refresh and swap
    void renderGL();
    /// Native way: draw the frame due at now + lead into an IOSurface and present it at its target; then the timer
    /// for the next frame. `force`: present even with no new frame (a setting, the HUD, a resize)
    void renderNative(bool force);
    /// Native way: the timer for the next frame (its target - lead), or a poll while frames may come
    void scheduleNative();
    /// The frame due at `hostNs` into _texture, if it is not there yet. Smooth motion: the picture for `hostNs` when
    /// `atRefresh` (a refresh of a fixed display), else for the due frame's own target (VRR: each frame at its time)
    void updateTexture(int64_t hostNs, bool atRefresh = true);
    /// Smooth motion on: a mode set, the frames in order, no temporal blending
    bool smoothActive() const;
    /// Under _frameSourceMutex: the frames latched since the last one into _smooth
    void feedSmooth();
    /// [VIDEO] DisplayRate = match: the display under the window at the machine's rate while the application is
    /// active, its own mode otherwise (frame presenter design, section 4.6)
    void updateDisplayRate();
    void restoreDisplayRate(const char* reason);
    /// When the refresh being drawn reaches the screen: one refresh after now (the swap waits for the vblank)
    int64_t refreshTargetNs() const;
    int64_t refreshPeriodNs() const;  ///< the display's refresh (60 Hz when it does not say)
    /// Native way: how long before a frame's target it is drawn and handed over: the presenter's (NativePresenter::
    /// LeadNs), unless UNREAL_PRESENT_LEAD_MS sets it
    int64_t presentLeadNs() const;
    /// After a swap: the next refresh, while frames are pending
    void onFrameSwapped();
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
    FrameSourceFn _frameSource;
    NextFrameSourceFn _nextFrameSource;
    mutable std::mutex _frameSourceMutex;  // Guards _frameSource, _nextFrameSource + _devicePixels across threads
    PresentFrameInfo _shownInfo;           ///< the frame in _latchedFrame
    uint64_t _uploadedSerial = 0;          ///< its serial in _texture (0: none, or a source without serials)
    int _idleRefreshes = 0;                ///< refreshes in a row with no new frame and none pending

    bool _native;  ///< false again when the platform's presenter cannot be made (Windows: no interop)
    std::unique_ptr<QOpenGLContext> _context;
    std::unique_ptr<QOffscreenSurface> _offscreen;  ///< the native way's surface to make the context current on
    bool _glInitialized = false;
    QSize _glSize;                                ///< the surface size resizeGL saw, device pixels
    std::unique_ptr<NativePresenter> _presenter;  ///< native way, made on the first paint (needs the window)
    PresentPolicy _presentPolicy = PresentPolicy::Auto;  ///< ScreenFrames::policy, UNREAL_PRESENT_POLICY overrides it
    DisplayRate _displayRate = DisplayRate::Keep;         ///< ScreenFrames::displayRate
    std::unique_ptr<DisplayModeSwitch> _displaySwitch;    ///< made on the first match; restores the mode when gone
    FrameRateMeter _frameRate;                            ///< the machine's rate from the shown frames' targets
    std::string _displayState = "keep";                   ///< WindowPresentStats::displayState and the rest
    std::string _displayReason;
    double _displayHz = 0.0;
    double _originalHz = 0.0;
    QTimer* _presentTimer = nullptr;              ///< native way: the next frame's present
    bool _presentPending = false;                 ///< native way: a forced present asked for
    int64_t _presentLeadNs = 0;                   ///< native way: UNREAL_PRESENT_LEAD_MS, 0 = presentLeadNs() picks
    int64_t _statusLogNs = 0;                     ///< native way: the last stats line in the log
    int64_t _presentPlannedNs = 0;                ///< native way: the wake the timer was set for (trace)

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

    // Smooth motion (GUI thread): the frames in order, the picture of the refresh drawn
    smoothmotion::SmoothMotion _smooth;
    SmoothMotionGPU _smoothGpu;  ///< its device unless ScreenFrames::onCpu / UNREAL_EFFECTS_DEVICE=cpu
    bool _smoothOnGpu = true;
    bool _smoothGpuFailed = false;  ///< create() failed once: the CPU from then on
    QImage _nextFrame;    ///< a frame from _nextFrameSource on its way into _smooth
    QImage _smoothFrame;  ///< the picture in _texture
    smoothmotion::SmoothMotion::Result _smoothShown;  ///< what _smoothFrame is made of
    smoothmotion::TraceWriter _smoothTrace;           ///< UNREAL_SMOOTH_TRACE (smoothtrace.h)
    bool _smoothTraced = false;                       ///< UNREAL_SMOOTH_TRACE is set
    bool _smoothUsed = false;     ///< the last updateTexture's picture was smooth motion's
    bool _smoothChanged = false;  ///< and another one than before
    uint64_t _smoothDrained = 0;
    uint32_t _smoothLag = 0;  // the lag (refreshes) of the run of late presents the last drain answered  ///< presents handed over before the last refresh left out (a late one: renderNative)
    int64_t _smoothRefreshNs = 0;  ///< native way on a refresh grid: the refresh drawn for (0: no grid)
    int64_t _smoothPeriodNs = 0;

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
    QSize _renderPicture;  ///< renderWindowImage's picture size in device pixels, while it draws (empty: the window's)
    bool _renderingImage = false;  ///< paintGL draws for renderWindowImage(): no HUD into the window
public:
    void setHudOverlay(class HudOverlay* overlay) { _hudOverlay = overlay; }
    class HudOverlay* hudOverlay() const { return _hudOverlay; }
};

#endif // DEVICESCREEN_GL_WINDOW_H
