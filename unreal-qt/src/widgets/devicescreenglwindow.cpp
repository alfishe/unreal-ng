#include "devicescreenglwindow.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOpenGLPixelTransferOptions>
#include <QSurfaceFormat>
#include <QUrl>
#include <cmath>
#include <cstring>

#include "emulator/emulator.h"
#include "emulator/keyboardmanager.h"
#include "3rdparty/message-center/messagecenter.h"
#include "crtprofiles.h"

static const char* CRT_VERTEX_SHADER = R"(
#version 120
varying vec2 texCoord;
void main() {
    gl_Position = gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;
}
)";

static const char* CRT_FRAGMENT_SHADER = R"(
#version 120
varying vec2 texCoord;
uniform sampler2D tex;
uniform vec2 texSize;
uniform vec2 outputSize;

uniform int maskType;
uniform float curvature;
uniform float cornerRadius;
uniform float scanlineWeight;
uniform float maskStrength;
uniform float maskDotPitch;
uniform float bloomStrength;
uniform float saturation;
uniform float brightness;
uniform float contrast;
uniform float gamma;

vec2 curve(vec2 uv) {
    if (curvature < 0.001) return uv;
    uv = (uv - 0.5) * 2.0;
    uv *= 1.0 + pow(length(uv) * curvature, 2.0);
    return uv * 0.5 + 0.5;
}

float cornerMask(vec2 uv) {
    if (cornerRadius < 0.001) return 1.0;
    vec2 d = abs(uv - 0.5) * 2.0;
    float r = cornerRadius;
    vec2 corner = max(d - (1.0 - r), 0.0);
    return 1.0 - smoothstep(r - 0.01, r, length(corner));
}

vec3 apertureGrille(vec2 pos, float pitch) {
    float x = mod(pos.x, pitch * 3.0);
    vec3 mask = vec3(0.0);
    if (x < pitch)          mask = vec3(1.0, 0.2, 0.2);
    else if (x < pitch*2.0) mask = vec3(0.2, 1.0, 0.2);
    else                    mask = vec3(0.2, 0.2, 1.0);
    return mix(vec3(1.0), mask, maskStrength);
}

vec3 shadowMask(vec2 pos, float pitch) {
    vec2 cell = floor(pos / pitch);
    vec2 offset = mod(pos, pitch) / pitch;
    float phase = mod(cell.y, 2.0) * 0.5;
    float x = mod(offset.x + phase, 1.0);
    vec3 mask = vec3(0.0);
    if (x < 0.333)      mask = vec3(1.0, 0.1, 0.1);
    else if (x < 0.666) mask = vec3(0.1, 1.0, 0.1);
    else                mask = vec3(0.1, 0.1, 1.0);
    float dist = length(offset - 0.5) * 2.0;
    mask *= smoothstep(1.0, 0.5, dist);
    return mix(vec3(1.0), mask + 0.3, maskStrength);
}

vec3 slotMask(vec2 pos, float pitch) {
    float slotHeight = pitch * 1.5;
    vec2 cell = floor(pos / vec2(pitch * 3.0, slotHeight));
    vec2 offset = mod(pos, vec2(pitch * 3.0, slotHeight));
    float phase = mod(cell.y, 2.0) * pitch * 1.5;
    float x = mod(offset.x + phase, pitch * 3.0);
    vec3 mask = vec3(0.0);
    if (x < pitch)          mask = vec3(1.0, 0.15, 0.15);
    else if (x < pitch*2.0) mask = vec3(0.15, 1.0, 0.15);
    else                    mask = vec3(0.15, 0.15, 1.0);
    float yGap = smoothstep(0.0, 0.15, offset.y / slotHeight) *
                 smoothstep(1.0, 0.85, offset.y / slotHeight);
    mask *= yGap;
    return mix(vec3(1.0), mask + 0.2, maskStrength);
}

vec3 blur(vec2 uv, float radius) {
    vec3 sum = vec3(0.0);
    float total = 0.0;
    vec2 texel = 1.0 / texSize;
    for (float x = -2.0; x <= 2.0; x += 1.0) {
        for (float y = -2.0; y <= 2.0; y += 1.0) {
            float weight = 1.0 - length(vec2(x, y)) / 4.0;
            sum += texture2D(tex, uv + vec2(x, y) * texel * radius).rgb * weight;
            total += weight;
        }
    }
    return sum / total;
}

void main() {
    vec2 uv = curve(texCoord);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = texture2D(tex, uv).rgb;

    if (bloomStrength > 0.001) {
        vec3 bloom = blur(uv, 2.0);
        color += bloom * bloomStrength;
    }

    color = pow(color, vec3(gamma));

    if (scanlineWeight > 0.001) {
        float scanY = uv.y * outputSize.y;
        float scanline = sin(scanY * 3.14159 / (outputSize.y / texSize.y)) * 0.5 + 0.5;
        color *= mix(1.0, scanline, scanlineWeight);
    }

    vec2 pixelPos = uv * outputSize;
    float pitch = maskDotPitch;
    if (pitch < 1.0) pitch = max(1.0, outputSize.x / 640.0);

    if (maskType == 1) {
        color *= apertureGrille(pixelPos, pitch);
    } else if (maskType == 2) {
        color *= shadowMask(pixelPos, pitch);
    } else if (maskType == 3) {
        color *= slotMask(pixelPos, pitch);
    }

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation);
    color = (color - 0.5) * contrast + 0.5;
    color *= brightness;

    color = pow(max(color, 0.0), vec3(1.0 / 2.2));

    color *= cornerMask(uv);

    float vignette = 1.0 - pow(length(texCoord - 0.5) * 1.1, 2.0) * 0.15;
    color *= vignette;

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

DeviceScreenGLWindow::DeviceScreenGLWindow(QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent)
{
    setupVSync();
}

void DeviceScreenGLWindow::setupVSync()
{
    QSurfaceFormat fmt;
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);  // Enable vsync: wait for vertical retrace before swap
    setFormat(fmt);
}

DeviceScreenGLWindow::~DeviceScreenGLWindow()
{
    makeCurrent();
    delete _texture;
    delete _crtShader;
    doneCurrent();
    detach();
}

void DeviceScreenGLWindow::init(uint16_t width, uint16_t height, void* buffer)
{
    detach();

    _devicePixelsRect = QRectF(0.0, 0.0, width, height);
    _devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);

    _textureNeedsUpdate = true;
}

void DeviceScreenGLWindow::detach()
{
    if (_devicePixels)
    {
        delete _devicePixels;
        _devicePixels = nullptr;
    }

    _frameSource = nullptr;
    _latchedFrame = QImage();
    _emulator.reset();
    _frameHistory.clear();

    update();
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
    QImage frame;
    if (_frameSource && !_latchedFrame.isNull() &&
        _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
        frame = _latchedFrame;
    }
    else if (_devicePixels != nullptr)
    {
        frame = *_devicePixels;
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

    _crtShader = new QOpenGLShaderProgram(this);
    if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER))
        qWarning() << "CRT vertex shader compilation failed:" << _crtShader->log();
    if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRT_FRAGMENT_SHADER))
        qWarning() << "CRT fragment shader compilation failed:" << _crtShader->log();
    if (!_crtShader->link())
        qWarning() << "CRT shader linking failed:" << _crtShader->log();
}

void DeviceScreenGLWindow::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;
    update();
}

void DeviceScreenGLWindow::setCRTProfile(CRTProfile profile)
{
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);
    update();
}

void DeviceScreenGLWindow::setCRTProfile(const CRTProfileParams& params)
{
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);
    update();
}

bool DeviceScreenGLWindow::loadCustomShader(const QString& name)
{
    QString source = CRTShaderManager::instance().loadShader(name);
    if (source.isEmpty())
        return false;

    _customShaderSource = source;

    makeCurrent();
    if (_crtShader)
    {
        delete _crtShader;
        _crtShader = new QOpenGLShaderProgram(this);
        _crtShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER);
        if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, source))
        {
            qWarning() << "Custom shader compilation failed:" << _crtShader->log();
            _crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRT_FRAGMENT_SHADER);
        }
        _crtShader->link();
    }
    doneCurrent();

    update();
    return true;
}

std::vector<QString> DeviceScreenGLWindow::availableShaders()
{
    CRTShaderManager::instance().scanShaderDirectory();
    return CRTShaderManager::instance().availableShaders();
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

void DeviceScreenGLWindow::updateTexture()
{
    if (!_textureNeedsUpdate.load(std::memory_order_relaxed))
        return;

    QImage* sourceImage = nullptr;

    if (_frameSource && !_latchedFrame.isNull() &&
        _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
        sourceImage = &_latchedFrame;
    }
    else if (_devicePixels != nullptr)
    {
        sourceImage = _devicePixels;
    }

    if (!sourceImage)
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

    _textureNeedsUpdate = false;
}

void DeviceScreenGLWindow::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    updateTexture();

    if (!_texture || !_texture->isCreated())
        return;

    _texture->bind();

    float srcX1 = 0.0f, srcY1 = 0.0f, srcX2 = 1.0f, srcY2 = 1.0f;
    if (_hasViewport && !_devicePixelsRect.isEmpty())
    {
        float texW = _devicePixelsRect.width();
        float texH = _devicePixelsRect.height();
        srcX1 = _displayViewport.cropLeft / texW;
        srcY1 = _displayViewport.cropTop / texH;
        srcX2 = 1.0f - (_displayViewport.cropRight / texW);
        srcY2 = 1.0f - (_displayViewport.cropBottom / texH);
    }

    // Calculate aspect-ratio-preserving quad coordinates
    float windowW = static_cast<float>(width());
    float windowH = static_cast<float>(height());
    float windowRatio = windowW / windowH;

    float quadW, quadH;
    if (windowRatio > ratio)
    {
        // Window is wider than content - pillarbox
        quadH = 1.0f;
        quadW = ratio / windowRatio;
    }
    else
    {
        // Window is taller than content - letterbox
        quadW = 1.0f;
        quadH = windowRatio / ratio;
    }

    glEnable(GL_TEXTURE_2D);

    if (_crtEnabled && _crtShader && _crtShader->isLinked())
    {
        _crtShader->bind();
        _crtShader->setUniformValue("tex", 0);
        _crtShader->setUniformValue("texSize", QVector2D(_devicePixelsRect.width(), _devicePixelsRect.height()));
        _crtShader->setUniformValue("outputSize", QVector2D(windowW * quadW, windowH * quadH));

        int maskType = 0;
        switch (_crtParams.profile)
        {
            case CRTProfile::Aperture:
            case CRTProfile::Megatron:
                maskType = 1;
                break;
            case CRTProfile::ShadowMask:
                maskType = 2;
                break;
            case CRTProfile::SlotMask:
                maskType = 3;
                break;
            default:
                maskType = 0;
                break;
        }

        _crtShader->setUniformValue("maskType", maskType);
        _crtShader->setUniformValue("curvature", _crtParams.curvature);
        _crtShader->setUniformValue("cornerRadius", _crtParams.cornerRadius);
        _crtShader->setUniformValue("scanlineWeight", _crtParams.scanlineWeight);
        _crtShader->setUniformValue("maskStrength", _crtParams.maskStrength);
        _crtShader->setUniformValue("maskDotPitch", _crtParams.maskDotPitch);
        _crtShader->setUniformValue("bloomStrength", _crtParams.bloomStrength);
        _crtShader->setUniformValue("saturation", _crtParams.saturation);
        _crtShader->setUniformValue("brightness", _crtParams.brightness);
        _crtShader->setUniformValue("contrast", _crtParams.contrast);
        _crtShader->setUniformValue("gamma", _crtParams.gamma);

        glBegin(GL_QUADS);
        glTexCoord2f(srcX1, srcY1); glVertex2f(-quadW,  quadH);
        glTexCoord2f(srcX2, srcY1); glVertex2f( quadW,  quadH);
        glTexCoord2f(srcX2, srcY2); glVertex2f( quadW, -quadH);
        glTexCoord2f(srcX1, srcY2); glVertex2f(-quadW, -quadH);
        glEnd();

        _crtShader->release();
    }
    else
    {
        glBegin(GL_QUADS);
        glTexCoord2f(srcX1, srcY1); glVertex2f(-quadW,  quadH);
        glTexCoord2f(srcX2, srcY1); glVertex2f( quadW,  quadH);
        glTexCoord2f(srcX2, srcY2); glVertex2f( quadW, -quadH);
        glTexCoord2f(srcX1, srcY2); glVertex2f(-quadW, -quadH);
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    _texture->release();
}

void DeviceScreenGLWindow::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        if (zxKey != 0)
        {
            KeyboardEvent* keyEvent = nullptr;
            if (_emulator)
            {
                std::string targetId = _emulator->GetUUID();
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED, targetId);
            }
            else
            {
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED);
            }

            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_PRESSED, keyEvent);
        }
    }
}

void DeviceScreenGLWindow::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();

    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        if (zxKey != 0)
        {
            KeyboardEvent* keyEvent = nullptr;
            if (_emulator)
            {
                std::string targetId = _emulator->GetUUID();
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED, targetId);
            }
            else
            {
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED);
            }

            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_RELEASED, keyEvent);
        }
    }
}

void DeviceScreenGLWindow::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
}

bool DeviceScreenGLWindow::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::DragEnter:
        {
            QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
                dragEvent->acceptProposedAction();
                emit dragEntered();
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
                QList<QUrl> urls = mimeData->urls();
                if (!urls.isEmpty())
                {
                    QString filePath = urls.first().toLocalFile();
                    qDebug() << "DeviceScreenGLWindow: File dropped:" << filePath;
                    emit fileDropped(filePath);
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
