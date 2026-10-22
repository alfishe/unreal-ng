#include "devicescreenglwindow.h"
#include "emulator/remotecontrolguard.h"
#include "emulator/mousecapturecontroller.h"
#include "hud/qt/hudoverlay.h"

#include <QDebug>
#include <QPainter>
#include <QDragEnterEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPixelTransferOptions>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVector2D>
#include <QVector4D>
#include <cmath>
#include <cstring>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/keyboardmanager.h"
#include "3rdparty/message-center/messagecenter.h"
#include "crtprofiles.h"

// The vertex shader of every CRT program: the viewport-filling quad from its corners (0, 0) top left .. (1, 1), the
// texture coordinate custom shaders read
static const char* CRT_VERTEX_SHADER = R"(
#version 330
layout(location = 0) in vec2 corner;
uniform vec4 texRect;
out vec2 texCoord;
void main() {
    gl_Position = vec4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    texCoord = mix(texRect.xy, texRect.zw, corner);
}
)";


DeviceScreenGLWindow::DeviceScreenGLWindow(QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent)
{
    setupVSync();
}

void DeviceScreenGLWindow::setupVSync()
{
    setFormat(surfaceFormat());
}

QSurfaceFormat DeviceScreenGLWindow::surfaceFormat()
{
    // 3.3 core: the slang presets' passes need uint, textureSize, texelFetch and float framebuffers. macOS answers a
    // 3.2+ core request with 4.1 core (its compatibility context stops at 2.1)
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);  // Enable vsync: wait for vertical retrace before swap
    return fmt;
}

bool DeviceScreenGLWindow::probeGPUAcceleration()
{
    QOpenGLContext ctx;
    ctx.setFormat(surfaceFormat());
    if (!ctx.create())
    {
        qInfo() << "DeviceScreenGLWindow: OpenGL context creation failed";
        return false;
    }
    const QSurfaceFormat created = ctx.format();
    if (ctx.isOpenGLES() || created.version() < qMakePair(3, 3) || created.profile() == QSurfaceFormat::CompatibilityProfile)
    {
        qInfo() << "DeviceScreenGLWindow: no OpenGL 3.3 core context (got" << created.majorVersion() << "."
                 << created.minorVersion() << "), using the software screen";
        return false;
    }

    QOffscreenSurface surface;
    surface.create();
    if (!ctx.makeCurrent(&surface))
    {
        qInfo() << "DeviceScreenGLWindow: Failed to make context current";
        return false;
    }

    QOpenGLFunctions* gl = ctx.functions();
    const char* renderer = reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(gl->glGetString(GL_VERSION));

    qInfo() << "DeviceScreenGLWindow: GL Renderer:" << (renderer ? renderer : "unknown");
    qInfo() << "DeviceScreenGLWindow: GL Vendor:" << (vendor ? vendor : "unknown");
    qInfo() << "DeviceScreenGLWindow: GL Version:" << (version ? version : "unknown");

    bool isSoftware = false;
    if (renderer)
    {
        isSoftware = (std::strstr(renderer, "llvmpipe") != nullptr) || (std::strstr(renderer, "softpipe") != nullptr) ||
                     (std::strstr(renderer, "swrast") != nullptr) || (std::strstr(renderer, "Software") != nullptr) ||
                     (std::strstr(renderer, "Microsoft Basic") != nullptr) ||
                     (std::strstr(renderer, "GDI Generic") != nullptr);
    }

    ctx.doneCurrent();

    if (isSoftware)
    {
        qInfo() << "DeviceScreenGLWindow: Software renderer detected, using CPU path";
        return false;
    }

    qInfo() << "DeviceScreenGLWindow: Hardware GPU acceleration available";
    return true;
}

DeviceScreenGLWindow::~DeviceScreenGLWindow()
{
    makeCurrent();
    delete _texture;
    delete _gradedTexture;
    delete _indexTexture;
    delete _factorTexture;
    delete _composeShader;
    _modelGpu.destroy();
    delete _customShader;
    if (_slangRunner)
        _slangRunner->release();
    _slangRunner.reset();
    _quadBuffer.destroy();
    _quadVao.destroy();
    doneCurrent();
    detach();
}

void DeviceScreenGLWindow::init(uint16_t width, uint16_t height, void* buffer, float aspect)
{
    detach();
    ratio = aspect > 0.0f ? aspect : kNativeRatio;

    _devicePixelsRect = QRectF(0.0, 0.0, width, height);
    _devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);

    _textureNeedsUpdate = true;
}

void DeviceScreenGLWindow::detach()
{
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        if (_devicePixels)
        {
            delete _devicePixels;
            _devicePixels = nullptr;
        }
        _frameSource = nullptr;
    }
    _latchedFrame = QImage();
    _emulator.reset();
    _frameHistory.clear();

    update();
}

void DeviceScreenGLWindow::clearFrameSource()
{
    // Runs on the MessageCenter worker inside the pre-free drain window:
    // after this returns, a concurrent paint finds both sources empty and
    // touches only owned memory. No Qt calls here (cross-thread safe).
    std::lock_guard<std::mutex> lock(_frameSourceMutex);
    _frameSource = nullptr;
    if (_devicePixels)
    {
        delete _devicePixels;
        _devicePixels = nullptr;
    }
}

void DeviceScreenGLWindow::refresh()
{
    if (_isShuttingDown)
        return;

    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGLWindow::prepareForShutdown()
{
    qDebug() << "DeviceScreenGLWindow::prepareForShutdown()";
    _isShuttingDown = true;
}

void DeviceScreenGLWindow::setDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
    _hasViewport = true;
    _frameHistory.clear();
    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGLWindow::clearDisplayViewport()
{
    _hasViewport = false;
    _frameHistory.clear();
    _textureNeedsUpdate = true;
    update();
}

QImage DeviceScreenGLWindow::grabFramebuffer()
{
    // Selection runs under the frame-source mutex: clearFrameSource() may
    // revoke both sources from the MessageCenter worker during instance
    // teardown, so the legacy fallback deep-copies under the lock to keep
    // every post-lock read on owned memory.
    QImage frame;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        if (_frameSource && !_latchedFrame.isNull() &&
            _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
        {
            frame = _latchedFrame;
        }
        // A frame source is installed: never fall back to _devicePixels. It wraps the emulator's framebuffer, which
        // is freed and reallocated when the guest switches video mode (Profi 352x288 <-> 512x240 hi-res, ATM, AlCo)
        // before the queued re-attach runs - a null or dangling image would be uploaded to the GPU.
        else if (!_frameSource && _devicePixels != nullptr)
        {
            frame = _devicePixels->copy();
        }
    }
    if (frame.isNull())
        return QImage();

    QRect crop = frame.rect();
    if (_hasViewport)
    {
        crop = QRect(_displayViewport.cropLeft, _displayViewport.cropTop,
                     frame.width() - _displayViewport.cropLeft - _displayViewport.cropRight,
                     frame.height() - _displayViewport.cropTop - _displayViewport.cropBottom);
    }
    return frame.copy(crop).convertToFormat(QImage::Format_ARGB32);
}

void DeviceScreenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    _texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    _texture->setMinificationFilter(QOpenGLTexture::Nearest);
    _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    _texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    // The quad: two triangles as a strip, its corners top left, top right, bottom left, bottom right
    static const float kCorners[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    _quadVao.create();
    _quadVao.bind();
    _quadBuffer.create();
    _quadBuffer.bind();
    _quadBuffer.allocate(kCorners, static_cast<int>(sizeof(kCorners)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    _quadBuffer.release();
    _quadVao.release();

    _composeShader = new QOpenGLShaderProgram(this);
    if (!_composeShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER) ||
        !_composeShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRTRenderer::kCompositionShader) ||
        !_composeShader->link())
        qWarning() << "CRT composition shader failed:" << _composeShader->log();
    if (!_modelGpu.create())
        qWarning() << "CRT model shader failed:" << _modelGpu.log();

    // A custom shader or preset chosen before the context existed
    if (!_customShaderSource.isEmpty())
        compileCustomShader(_customShaderSource);
    if (!_slangPresetName.isEmpty() && !applySlangPreset(_slangPresetName))
        _slangPresetName.clear();
}

void DeviceScreenGLWindow::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;
    update();
}

void DeviceScreenGLWindow::setCRTProfile(CRTProfile profile)
{
    if (!customShader().isEmpty())
        loadCustomShader(QString());  // a built-in profile replaces the custom shader
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);
    update();
}

void DeviceScreenGLWindow::setCRTProfile(const CRTProfileParams& params)
{
    if (!customShader().isEmpty())
        loadCustomShader(QString());
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);
    update();
}

bool DeviceScreenGLWindow::loadCustomShader(const QString& name)
{
    // A preset replaces a custom shader and the other way round; the empty name drops both
    const bool preset = SlangPresetRunner::isPreset(name);
    if (preset || name.isEmpty())
    {
        bool ok = true;
        if (context())
        {
            makeCurrent();
            ok = applySlangPreset(name);
            if (ok)
                compileCustomShader(QString());
            doneCurrent();
        }
        if (ok)
        {
            _slangPresetName = name;
            _customShaderName.clear();
            _customShaderSource.clear();
        }
        update();
        return ok;
    }

    QString source;
    if (!name.isEmpty())
    {
        source = CRTShaderManager::instance().loadShader(name);
        if (source.isEmpty())
        {
            CRTShaderManager::instance().scanShaderDirectory();  // a file added since the menu was built
            source = CRTShaderManager::instance().loadShader(name);
        }
        if (source.isEmpty())
            return false;
    }

    bool ok = true;
    if (context())
    {
        makeCurrent();
        ok = compileCustomShader(source);
        doneCurrent();
    }
    if (ok && context())
    {
        makeCurrent();
        applySlangPreset(QString());
        doneCurrent();
    }
    if (ok)
    {
        _customShaderName = name;
        _customShaderSource = source;
        _slangPresetName.clear();
    }
    update();
    return ok;
}

bool DeviceScreenGLWindow::applySlangPreset(const QString& name)
{
    if (_slangRunner)
        _slangRunner->release();
    _slangRunner.reset();
    if (name.isEmpty())
        return true;
    auto runner = std::make_unique<SlangPresetRunner>();
    if (!runner->load(SlangPresetRunner::presetDirectory(name)))
    {
        qWarning() << "CRT preset" << name << "failed:" << runner->error();
        return false;
    }
    _slangRunner = std::move(runner);
    return true;
}

bool DeviceScreenGLWindow::compileCustomShader(const QString& source)
{
    delete _customShader;
    _customShader = nullptr;
    if (source.isEmpty())
        return true;
    _customShader = new QOpenGLShaderProgram(this);
    if (_customShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER) &&
        _customShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRTRenderer::coreFragmentSource(source)) &&
        _customShader->link())
        return true;
    qWarning() << "Custom CRT shader failed:" << _customShader->log();
    delete _customShader;
    _customShader = nullptr;
    _customShaderName.clear();
    _customShaderSource.clear();
    return false;
}

std::vector<QString> DeviceScreenGLWindow::availableShaders()
{
    const QStringList presets = SlangPresetRunner::availablePresets();
    std::vector<QString> names(presets.begin(), presets.end());
    CRTShaderManager::instance().scanShaderDirectory();
    for (const QString& name : CRTShaderManager::instance().availableShaders())
    {
        if (!presets.contains(name))  // a preset's name chooses the preset
            names.push_back(name);
    }
    return names;
}

void DeviceScreenGLWindow::setTemporalBlendingEnabled(bool enabled)
{
    _temporalEnabled = enabled;
    _frameHistory.setBlendingEnabled(enabled);
    update();
}

void DeviceScreenGLWindow::setTemporalHistorySize(int frames)
{
    _frameHistory.setHistorySize(frames);
    update();
}

int DeviceScreenGLWindow::temporalHistorySize() const
{
    return _frameHistory.historySize();
}

void DeviceScreenGLWindow::setTemporalWeightMode(int mode)
{
    _temporalWeightMode = mode;
    if (mode == 0)
        _frameHistory.setEqualWeights();
    else
        _frameHistory.setExponentialWeights(0.5f);
    update();
}

void DeviceScreenGLWindow::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

QImage DeviceScreenGLWindow::renderWindowImage()
{
    if (!isValid())
        return {};
    makeCurrent();
    const qreal ratio = devicePixelRatio();
    const QSize pixels = (QSizeF(size()) * ratio).toSize();
    QImage image;
    {
        QOpenGLFramebufferObject fbo(pixels);
        fbo.bind();
        glViewport(0, 0, pixels.width(), pixels.height());
        // The HUD's QPainter would paint into the window, not this FBO: it goes onto the image below
        _renderingImage = true;
        paintGL();
        _renderingImage = false;
        fbo.release();
        image = fbo.toImage();
    }
    doneCurrent();
    image.setDevicePixelRatio(ratio);
    if (_hudOverlay && _hudOverlay->isVisible())
    {
        QPainter painter(&image);
        _hudOverlay->renderHUD(painter, width(), height(), ratio);
    }
    return image;
}

void DeviceScreenGLWindow::updateTexture()
{
    if (!_textureNeedsUpdate.load(std::memory_order_relaxed))
        return;

    // Selection under the frame-source mutex (see clearFrameSource): the
    // legacy fallback deep-copies so post-lock texture upload only touches
    // owned memory.
    QImage* sourceImage = nullptr;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        if (_frameSource && !_latchedFrame.isNull() &&
            _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
        {
            sourceImage = &_latchedFrame;
        }
        // A frame source is installed: never fall back to _devicePixels. It wraps the emulator's framebuffer, which
        // is freed and reallocated when the guest switches video mode (Profi 352x288 <-> 512x240 hi-res, ATM, AlCo)
        // before the queued re-attach runs - a null or dangling image would be uploaded to the GPU.
        else if (!_frameSource && _devicePixels != nullptr)
        {
            _legacyFrame = _devicePixels->copy();
            sourceImage = &_legacyFrame;
        }
    }

    if (!sourceImage || sourceImage->isNull())
        return;

    QImage* uploadImage = sourceImage;

    if (_temporalEnabled)
    {
        int srcWidth = sourceImage->width();
        int srcHeight = sourceImage->height();

        if (_frameHistory.width() != srcWidth || _frameHistory.height() != srcHeight)
        {
            _frameHistory.init(srcWidth, srcHeight, _frameHistory.historySize());
            _blendedFrame = QImage(srcWidth, srcHeight, QImage::Format_RGBA8888);
        }

        _frameHistory.pushFrame(sourceImage->bits(), sourceImage->sizeInBytes());

        if (_frameHistory.hasMinimumHistory())
        {
            _frameHistory.getBlendedFrame(_blendedFrame.bits(), _blendedFrame.sizeInBytes());
            uploadImage = &_blendedFrame;
        }
    }

    const int w = uploadImage->width();
    const int h = uploadImage->height();
    if (!_texture->isCreated() || _texture->width() != w || _texture->height() != h)
    {
        if (_texture->isCreated())
            _texture->destroy();
        _texture->create();
        _texture->setSize(w, h);
        _texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        _texture->setMipLevels(1);
        _texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        _texture->setMinificationFilter(QOpenGLTexture::Nearest);
        _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        _texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }

    QOpenGLPixelTransferOptions transfer;
    transfer.setAlignment(1);
    transfer.setRowLength(static_cast<int>(uploadImage->bytesPerLine() / 4));
    _texture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, uploadImage->constBits(), &transfer);

    _shownFrame = uploadImage;
    ++_frameSerial;
    _textureNeedsUpdate = false;
}

namespace
{
/// A tight RGBA8 image into a nearest-sampled texture, (re)allocated for its size
void UploadRgba(QOpenGLTexture*& texture, int width, int height, const uint8_t* pixels)
{
    if (!texture)
        texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    if (!texture->isCreated() || texture->width() != width || texture->height() != height)
    {
        if (texture->isCreated())
            texture->destroy();
        texture->create();
        texture->setSize(width, height);
        texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        texture->setMipLevels(1);
        texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    QOpenGLPixelTransferOptions transfer;
    transfer.setAlignment(1);
    texture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, pixels, &transfer);
}

}  // namespace

void DeviceScreenGLWindow::paintGL()
{
    const qreal dpr = devicePixelRatio();
    const int surfaceWidth = qRound(width() * dpr);
    const int surfaceHeight = qRound(height() * dpr);
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    glDisable(GL_DITHER);  // the composition writes exact values
    // The HUD's QPainter of the previous frame may leave its state behind
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // a preset's framebuffers clear to other colours
    glClear(GL_COLOR_BUFFER_BIT);

    updateTexture();

    const QRect picture = pictureDeviceRect();
    if (_texture && _texture->isCreated() && !picture.isEmpty())
    {
        // The viewport is the picture, to the device pixel: the quad covers it, the CRT composition reads its tables
        // per pixel. GL counts rows from the bottom
        const int glY = surfaceHeight - picture.y() - picture.height();
        glViewport(picture.x(), glY, picture.width(), picture.height());

        // With or without CRT effects the composition draws (CRTProfile::None: the plain nearest scale), the software
        // screen's picture bit for bit
        if (_crtEnabled && _slangRunner)
            drawSlangPreset(picture, glY);
        else if (_crtEnabled && _customShader && _customShader->isLinked())
            drawCustomCrt(picture);
        else if (_crtEnabled && CRTBeamModel::isModel(_crtParams.profile) && _modelGpu.isCreated() && _shownFrame)
            drawModelCrt(picture);
        else if (_composeShader && _composeShader->isLinked() && _shownFrame)
            drawComposedCrt(picture);
        glViewport(0, 0, surfaceWidth, surfaceHeight);
    }

    if (_hudOverlay && _hudOverlay->isVisible() && !_renderingImage)
    {
        QPainter painter(this);
        _hudOverlay->renderHUD(painter, width(), height(), devicePixelRatio());
    }
}

void DeviceScreenGLWindow::drawQuad(QOpenGLShaderProgram* program, float srcX1, float srcY1, float srcX2, float srcY2)
{
    program->setUniformValue("texRect", QVector4D(srcX1, srcY1, srcX2, srcY2));
    _quadVao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    _quadVao.release();
}

void DeviceScreenGLWindow::drawComposedCrt(const QRect& picture)
{
    CRTRenderer::Layout layout;
    layout.outWidth = picture.width();
    layout.outHeight = picture.height();
    layout.frameWidth = _shownFrame->width();
    layout.frameHeight = _shownFrame->height();
    layout.source = displaySourceRect().toRect().intersected(_shownFrame->rect());
    if (layout.source.isEmpty())
        return;

    // The tables again only for a new size or profile, the grading once per frame
    static const CRTProfileParams kPlain = CRTProfileParams::None();
    const CRTProfileParams& params = _crtEnabled ? _crtParams : kPlain;
    const bool rebuilt = _crtRenderer.setLayout(layout, params);
    if (rebuilt || !_indexTexture || !_factorTexture)
    {
        UploadRgba(_indexTexture, layout.outWidth, layout.outHeight, _crtRenderer.indexTable().data());
        UploadRgba(_factorTexture, layout.outWidth, layout.outHeight, _crtRenderer.factorTable().data());
    }
    if (rebuilt || !_gradedTexture || _gradedSerial != _frameSerial)
    {
        _crtRenderer.grade(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), params);
        UploadRgba(_gradedTexture, layout.frameWidth, layout.frameHeight, _crtRenderer.graded().data());
        _gradedSerial = _frameSerial;
    }

    _composeShader->bind();
    _gradedTexture->bind(0);
    _indexTexture->bind(1);
    _factorTexture->bind(2);
    _composeShader->setUniformValue("frame", 0);
    _composeShader->setUniformValue("indexTable", 1);
    _composeShader->setUniformValue("factorTable", 2);
    _composeShader->setUniformValue("frameSize", QVector2D(static_cast<float>(layout.frameWidth), static_cast<float>(layout.frameHeight)));
    drawQuad(_composeShader, 0.0f, 0.0f, 1.0f, 1.0f);
    _factorTexture->release(2);
    _indexTexture->release(1);
    _gradedTexture->release(0);
    glActiveTexture(GL_TEXTURE0);
    _composeShader->release();
}

void DeviceScreenGLWindow::drawModelCrt(const QRect& picture)
{
    CRTBeamModel::Layout layout;
    layout.outWidth = picture.width();
    layout.outHeight = picture.height();
    layout.frameWidth = _shownFrame->width();
    layout.frameHeight = _shownFrame->height();
    layout.source = displaySourceRect().toRect().intersected(_shownFrame->rect());
    if (layout.source.isEmpty())
        return;

    // The tables again only for a new size or profile, the frame once per upload
    const bool rebuilt = _crtModel.setLayout(layout, _crtParams.profile);
    if (rebuilt || !_modelTablesUploaded)
    {
        _modelGpu.uploadTables(_crtModel);
        _modelTablesUploaded = true;
    }
    if (rebuilt || _modelSerial != _frameSerial)
    {
        // Not the glow's columns: the shader sums them from the frame itself
        _crtModel.prepare(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), false);
        _modelGpu.uploadFrame(_crtModel);
        _modelSerial = _frameSerial;
    }
    _modelGpu.draw(_crtModel);
    glActiveTexture(GL_TEXTURE0);
}

void DeviceScreenGLWindow::drawCustomCrt(const QRect& picture)
{
    const QRectF source = displaySourceRect();
    const float texW = static_cast<float>(_texture->width());
    const float texH = static_cast<float>(_texture->height());

    _customShader->bind();
    _texture->bind(0);
    _customShader->setUniformValue("tex", 0);
    _customShader->setUniformValue("texSize", QVector2D(texW, texH));
    _customShader->setUniformValue("outputSize", QVector2D(static_cast<float>(picture.width()), static_cast<float>(picture.height())));
    // The profiles have no phosphor mask: the mask uniforms of the documented interface stay, as "none"
    _customShader->setUniformValue("maskType", 0);
    _customShader->setUniformValue("curvature", _crtParams.curvature);
    _customShader->setUniformValue("cornerRadius", _crtParams.cornerRadius);
    _customShader->setUniformValue("scanlineWeight", _crtParams.scanlineWeight);
    _customShader->setUniformValue("maskStrength", 0.0f);
    _customShader->setUniformValue("maskDotPitch", 0.0f);
    _customShader->setUniformValue("bloomStrength", _crtParams.bloomStrength);
    _customShader->setUniformValue("saturation", _crtParams.saturation);
    _customShader->setUniformValue("brightness", _crtParams.brightness);
    _customShader->setUniformValue("contrast", _crtParams.contrast);
    _customShader->setUniformValue("gamma", _crtParams.gamma);
    drawQuad(_customShader, static_cast<float>(source.left()) / texW, static_cast<float>(source.top()) / texH,
             static_cast<float>(source.right()) / texW, static_cast<float>(source.bottom()) / texH);
    _texture->release(0);
    _customShader->release();
}

void DeviceScreenGLWindow::drawSlangPreset(const QRect& picture, int glY)
{
    // The passes render into their own framebuffers; the last one is copied 1:1 into the picture of the target (the
    // window or renderWindowImage's framebuffer object)
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    const QRect source = displaySourceRect().toRect().intersected(QRect(0, 0, _texture->width(), _texture->height()));
    if (source.isEmpty())
        return;
    _slangRunner->render(_texture->textureId(), source, picture.size());
    _slangRunner->drawTo(static_cast<GLuint>(target), QRect(picture.x(), glY, picture.width(), picture.height()));
}

void DeviceScreenGLWindow::keyPressEvent(QKeyEvent* event)
{
    // The full screen key (Cmd+F / Alt+Enter) bubbles up to the application shortcut
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    // The mouse release key never reaches the machine
    if (_mouseCapture && _mouseCapture->handleKeyPress(event))
        return;

    // An automation session holds the machine: no keys from the GUI (releases still go: nothing stays held)
    if (RemoteControlGuard::InputBlocked(_emulator.get()))
        return;

    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? _emulator->GetUUID().toString() : std::string();
        KeyboardManager::postHostKey(event, KEY_PRESSED, targetId);
    }
}

void DeviceScreenGLWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    if (_mouseCapture && _mouseCapture->handleKeyRelease(event))
        return;

    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? _emulator->GetUUID().toString() : std::string();
        KeyboardManager::postHostKey(event, KEY_RELEASED, targetId);
    }
}

void DeviceScreenGLWindow::mousePressEvent(QMouseEvent* event)
{
    // An automation session holds the machine: no capture, no clicks from the GUI
    if (RemoteControlGuard::InputBlocked(_emulator.get()))
    {
        event->accept();
        return;
    }
    if (_mouseCapture && _mouseCapture->handleMousePress(event))
        event->accept();
    else
        QOpenGLWindow::mousePressEvent(event);
}

void DeviceScreenGLWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseRelease(event))
        event->accept();
    else
        QOpenGLWindow::mouseReleaseEvent(event);
}

void DeviceScreenGLWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseMove(event))
        event->accept();
    else
        QOpenGLWindow::mouseMoveEvent(event);
}

void DeviceScreenGLWindow::wheelEvent(QWheelEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleWheel(event))
        event->accept();
    else
        QOpenGLWindow::wheelEvent(event);
}

void DeviceScreenGLWindow::focusOutEvent(QFocusEvent* event)
{
    if (_mouseCapture)
        _mouseCapture->handleFocusOut();
    // Keys held while the focus left never send their release here: let a PS/2 machine see them go up
    KeyboardManager::postHeldKeyReleases(_emulator ? _emulator->GetUUID().toString() : std::string());
    QOpenGLWindow::focusOutEvent(event);
}

QRectF DeviceScreenGLWindow::displaySourceRect() const
{
    if (!_hasViewport)
        return _devicePixelsRect;
    return QRectF(_displayViewport.cropLeft, _displayViewport.cropTop,
                  _devicePixelsRect.width() - _displayViewport.cropLeft - _displayViewport.cropRight,
                  _devicePixelsRect.height() - _displayViewport.cropTop - _displayViewport.cropBottom);
}

QSize DeviceScreenGLWindow::drawnPictureSize() const
{
    // The same fit paintGL draws: the native ratio, pillarboxed or letterboxed
    const double windowW = width();
    const double windowH = height();
    if (windowW <= 0 || windowH <= 0)
        return QSize();
    if (windowW / windowH > ratio)
        return QSize(static_cast<int>(windowH * ratio), static_cast<int>(windowH));
    return QSize(static_cast<int>(windowW), static_cast<int>(windowW / ratio));
}

QRect DeviceScreenGLWindow::pictureDeviceRect() const
{
    const QSize drawn = drawnPictureSize();
    if (drawn.isEmpty())
        return QRect();
    const qreal dpr = devicePixelRatio();
    const int x = (width() - drawn.width()) / 2;
    const int y = (height() - drawn.height()) / 2;
    return QRect(qRound(x * dpr), qRound(y * dpr), qRound(drawn.width() * dpr), qRound(drawn.height() * dpr));
}

bool DeviceScreenGLWindow::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (KeyboardManager::isFullScreenKey(keyEvent))
        {
            event->ignore();
            return false;
        }
        // The machine's keys (KeyboardManager::machineOwnsKey): no shortcut, the key press comes here.
        // Leased: automation may be removing the instance on another thread
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        if (KeyboardManager::machineOwnsKey(keyEvent, lease ? lease->pKeyboard : nullptr))
        {
            event->accept();
            return true;
        }
    }

    switch (event->type())
    {
        case QEvent::DragEnter:
        {
            QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
                dragEvent->acceptProposedAction();
                const QList<QUrl> urls = dragEvent->mimeData()->urls();
                emit dragEntered(urls.isEmpty() ? QString() : urls.first().toLocalFile());
                return true;
            }
            break;
        }
        case QEvent::DragLeave:
            emit dragLeft();
            return true;

        case QEvent::Drop:
        {
            QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mimeData = dropEvent->mimeData();
            if (mimeData->hasUrls())
            {
                QStringList paths;
                for (const QUrl& url : mimeData->urls())
                {
                    if (url.isLocalFile())
                        paths << url.toLocalFile();
                }
                if (!paths.isEmpty())
                {
                    qDebug() << "DeviceScreenGLWindow: files dropped:" << paths;
                    emit filesDropped(paths);
                }
                emit dragLeft();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QOpenGLWindow::event(event);
}

void DeviceScreenGLWindow::handleExternalKeyPress(QKeyEvent* event)
{
    keyPressEvent(event);
}

void DeviceScreenGLWindow::handleExternalKeyRelease(QKeyEvent* event)
{
    keyReleaseEvent(event);
}
