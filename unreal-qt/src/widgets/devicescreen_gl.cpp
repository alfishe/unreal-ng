#include "devicescreen_gl.h"

#include <QDebug>
#include <QKeyEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
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

// Comprehensive CRT shader supporting multiple mask types and resolution-adaptive effects
static const char* CRT_FRAGMENT_SHADER = R"(
#version 120
varying vec2 texCoord;
uniform sampler2D tex;
uniform vec2 texSize;       // Source texture size
uniform vec2 outputSize;    // Target output size

// Profile parameters
uniform int maskType;       // 0=none, 1=aperture, 2=shadow, 3=slot
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

// Curvature distortion
vec2 curve(vec2 uv) {
    if (curvature < 0.001) return uv;
    uv = (uv - 0.5) * 2.0;
    uv *= 1.0 + pow(length(uv) * curvature, 2.0);
    return uv * 0.5 + 0.5;
}

// Rounded corner mask
float cornerMask(vec2 uv) {
    if (cornerRadius < 0.001) return 1.0;
    vec2 d = abs(uv - 0.5) * 2.0;
    float r = cornerRadius;
    vec2 corner = max(d - (1.0 - r), 0.0);
    return 1.0 - smoothstep(r - 0.01, r, length(corner));
}

// Aperture grille (vertical RGB stripes like Trinitron)
vec3 apertureGrille(vec2 pos, float pitch) {
    float x = mod(pos.x, pitch * 3.0);
    vec3 mask = vec3(0.0);
    if (x < pitch)          mask = vec3(1.0, 0.2, 0.2);
    else if (x < pitch*2.0) mask = vec3(0.2, 1.0, 0.2);
    else                    mask = vec3(0.2, 0.2, 1.0);
    return mix(vec3(1.0), mask, maskStrength);
}

// Shadow mask (traditional CRT dots)
vec3 shadowMask(vec2 pos, float pitch) {
    vec2 cell = floor(pos / pitch);
    vec2 offset = mod(pos, pitch) / pitch;
    float phase = mod(cell.y, 2.0) * 0.5;
    float x = mod(offset.x + phase, 1.0);
    vec3 mask = vec3(0.0);
    if (x < 0.333)      mask = vec3(1.0, 0.1, 0.1);
    else if (x < 0.666) mask = vec3(0.1, 1.0, 0.1);
    else                mask = vec3(0.1, 0.1, 1.0);
    // Circular falloff within cell
    float dist = length(offset - 0.5) * 2.0;
    mask *= smoothstep(1.0, 0.5, dist);
    return mix(vec3(1.0), mask + 0.3, maskStrength);
}

// Slot mask (arcade style)
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
    // Vertical gaps between slots
    float yGap = smoothstep(0.0, 0.15, offset.y / slotHeight) *
                 smoothstep(1.0, 0.85, offset.y / slotHeight);
    mask *= yGap;
    return mix(vec3(1.0), mask + 0.2, maskStrength);
}

// Simple box blur for bloom
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

    // Clip outside screen area
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Sample texture
    vec3 color = texture2D(tex, uv).rgb;

    // Add bloom
    if (bloomStrength > 0.001) {
        vec3 bloom = blur(uv, 2.0);
        color += bloom * bloomStrength;
    }

    // Apply gamma (linearize)
    color = pow(color, vec3(gamma));

    // Scanlines (adaptive to output resolution when mask is active)
    if (scanlineWeight > 0.001) {
        float scanY = uv.y * outputSize.y;
        float scanline = sin(scanY * 3.14159 / (outputSize.y / texSize.y)) * 0.5 + 0.5;
        color *= mix(1.0, scanline, scanlineWeight);
    }

    // Phosphor mask
    vec2 pixelPos = uv * outputSize;
    float pitch = maskDotPitch;
    if (pitch < 1.0) pitch = max(1.0, outputSize.x / 640.0);  // Auto dot pitch

    if (maskType == 1) {
        color *= apertureGrille(pixelPos, pitch);
    } else if (maskType == 2) {
        color *= shadowMask(pixelPos, pitch);
    } else if (maskType == 3) {
        color *= slotMask(pixelPos, pitch);
    }

    // Color adjustments
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation);
    color = (color - 0.5) * contrast + 0.5;
    color *= brightness;

    // De-gamma
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));

    // Corner mask
    color *= cornerMask(uv);

    // Vignette
    float vignette = 1.0 - pow(length(texCoord - 0.5) * 1.1, 2.0) * 0.15;
    color *= vignette;

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

DeviceScreenGL::DeviceScreenGL(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
}

DeviceScreenGL::~DeviceScreenGL()
{
    makeCurrent();
    delete _texture;
    delete _crtShader;
    doneCurrent();
    detach();
}

void DeviceScreenGL::init(uint16_t width, uint16_t height, void* buffer)
{
    detach();

    _devicePixelsRect = QRectF(0.0, 0.0, width, height);
    _devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);

    _textureNeedsUpdate = true;
}

void DeviceScreenGL::detach()
{
    if (_devicePixels)
    {
        delete _devicePixels;
        _devicePixels = nullptr;
    }

    _frameSource = nullptr;
    _latchedFrame = QImage();
    _emulator.reset();

    // Clear temporal history
    _frameHistory.clear();

    update();
}

void DeviceScreenGL::refresh()
{
    if (_isShuttingDown)
        return;

    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGL::prepareForShutdown()
{
    qDebug() << "DeviceScreenGL::prepareForShutdown()";
    _isShuttingDown = true;
}

void DeviceScreenGL::setDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
    _hasViewport = true;

    // Flush temporal cache - old frames had different geometry
    _frameHistory.clear();

    // Force texture recreation on next frame
    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGL::clearDisplayViewport()
{
    _hasViewport = false;

    // Flush temporal cache - geometry changed
    _frameHistory.clear();

    // Force texture recreation on next frame
    _textureNeedsUpdate = true;
    update();
}

QImage DeviceScreenGL::grabFramebuffer()
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

void DeviceScreenGL::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Create texture for framebuffer
    _texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    _texture->setMinificationFilter(QOpenGLTexture::Nearest);
    _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    _texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    // Create CRT shader
    _crtShader = new QOpenGLShaderProgram(this);
    if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER))
    {
        qWarning() << "CRT vertex shader compilation failed:" << _crtShader->log();
    }
    if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRT_FRAGMENT_SHADER))
    {
        qWarning() << "CRT fragment shader compilation failed:" << _crtShader->log();
    }
    if (!_crtShader->link())
    {
        qWarning() << "CRT shader linking failed:" << _crtShader->log();
    }
}

void DeviceScreenGL::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;
    if (enabled && _crtParams.profile == CRTProfile::None)
    {
        _crtParams = CRTProfileParams::Basic();
    }
    update();
}

void DeviceScreenGL::setCRTProfile(CRTProfile profile)
{
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);
    update();
}

void DeviceScreenGL::setCRTProfile(const CRTProfileParams& params)
{
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);
    update();
}

bool DeviceScreenGL::loadCustomShader(const QString& name)
{
    QString source = CRTShaderManager::instance().loadShader(name);
    if (source.isEmpty())
        return false;

    _customShaderSource = source;

    // Recompile shader
    makeCurrent();
    if (_crtShader)
    {
        delete _crtShader;
        _crtShader = new QOpenGLShaderProgram(this);
        _crtShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER);
        if (!_crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, source))
        {
            qWarning() << "Custom shader compilation failed:" << _crtShader->log();
            // Fall back to built-in
            _crtShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRT_FRAGMENT_SHADER);
        }
        _crtShader->link();
    }
    doneCurrent();

    update();
    return true;
}

std::vector<QString> DeviceScreenGL::availableShaders()
{
    CRTShaderManager::instance().scanShaderDirectory();
    return CRTShaderManager::instance().availableShaders();
}

void DeviceScreenGL::setTemporalBlendingEnabled(bool enabled)
{
    _temporalEnabled = enabled;
    _frameHistory.setBlendingEnabled(enabled);

    // Frame history will be initialized lazily in updateTexture() with actual frame dimensions
    // This handles overscan mode where framebuffer size differs from native display size

    update();
}

void DeviceScreenGL::setTemporalHistorySize(int frames)
{
    _frameHistory.setHistorySize(frames);
}

int DeviceScreenGL::temporalHistorySize() const
{
    return _frameHistory.historySize();
}

void DeviceScreenGL::setTemporalWeightMode(int mode)
{
    _temporalWeightMode = mode;
    if (mode == 0)
        _frameHistory.setEqualWeights();
    else
        _frameHistory.setExponentialWeights(0.5f);
}

void DeviceScreenGL::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void DeviceScreenGL::updateTexture()
{
    if (!_textureNeedsUpdate)
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

    // Temporal blending: push frame to history and get blended result
    if (_temporalEnabled)
    {
        int srcWidth = sourceImage->width();
        int srcHeight = sourceImage->height();

        // Initialize or re-initialize frame history if dimensions changed
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

    // Upload texture (GPU handles scaling)
    if (_texture->isCreated())
    {
        _texture->destroy();
    }
    _texture->setData(*uploadImage);

    // setData() resets filter modes - restore nearest-neighbor for pixel-perfect scaling
    _texture->setMinificationFilter(QOpenGLTexture::Nearest);
    _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    _texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    _textureNeedsUpdate = false;
}

void DeviceScreenGL::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    updateTexture();

    if (!_texture || !_texture->isCreated())
        return;

    _texture->bind();

    // Calculate source texture coordinates for viewport cropping
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

    glEnable(GL_TEXTURE_2D);

    if (_crtEnabled && _crtShader && _crtShader->isLinked())
    {
        // CRT shader path
        _crtShader->bind();
        _crtShader->setUniformValue("tex", 0);
        _crtShader->setUniformValue("texSize", QVector2D(_devicePixelsRect.width(), _devicePixelsRect.height()));
        _crtShader->setUniformValue("outputSize", QVector2D(width(), height()));

        // Map profile to mask type for shader
        int maskType = 0;
        switch (_crtParams.profile)
        {
            case CRTProfile::Aperture:
            case CRTProfile::Megatron:
                maskType = 1;  // Aperture grille
                break;
            case CRTProfile::ShadowMask:
                maskType = 2;  // Shadow mask
                break;
            case CRTProfile::SlotMask:
                maskType = 3;  // Slot mask
                break;
            default:
                maskType = 0;  // No mask
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
        glTexCoord2f(srcX1, srcY1); glVertex2f(-1.0f,  1.0f);
        glTexCoord2f(srcX2, srcY1); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(srcX2, srcY2); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(srcX1, srcY2); glVertex2f(-1.0f, -1.0f);
        glEnd();

        _crtShader->release();
    }
    else
    {
        // Simple textured quad (no effects)
        glBegin(GL_QUADS);
        glTexCoord2f(srcX1, srcY1); glVertex2f(-1.0f,  1.0f);
        glTexCoord2f(srcX2, srcY1); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(srcX2, srcY2); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(srcX1, srcY2); glVertex2f(-1.0f, -1.0f);
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    _texture->release();
}

void DeviceScreenGL::keyPressEvent(QKeyEvent* event)
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

void DeviceScreenGL::keyReleaseEvent(QKeyEvent* event)
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

void DeviceScreenGL::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
}

void DeviceScreenGL::resizeEvent(QResizeEvent* event)
{
    float width = static_cast<float>(event->size().width());
    float height = static_cast<float>(event->size().height());
    int newWidth, newHeight;

    if (height * ratio < width)
    {
        newWidth = static_cast<int>(height * ratio);
        newHeight = static_cast<int>(height);
    }
    else
    {
        newWidth = static_cast<int>(width);
        newHeight = static_cast<int>(width / ratio);
    }

    resize(newWidth, newHeight);
    QOpenGLWidget::resizeEvent(event);
}

void DeviceScreenGL::handleExternalKeyPress(QKeyEvent* event)
{
    keyPressEvent(event);
}

void DeviceScreenGL::handleExternalKeyRelease(QKeyEvent* event)
{
    keyReleaseEvent(event);
}

// Factory implementation
namespace DeviceScreenFactory
{

bool probeGPUAcceleration()
{
    QOpenGLContext ctx;
    if (!ctx.create())
    {
        qInfo() << "DeviceScreenFactory: OpenGL context creation failed";
        return false;
    }

    QOffscreenSurface surface;
    surface.create();
    if (!ctx.makeCurrent(&surface))
    {
        qInfo() << "DeviceScreenFactory: Failed to make context current";
        return false;
    }

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    qInfo() << "DeviceScreenFactory: GL Renderer:" << (renderer ? renderer : "unknown");
    qInfo() << "DeviceScreenFactory: GL Vendor:" << (vendor ? vendor : "unknown");
    qInfo() << "DeviceScreenFactory: GL Version:" << (version ? version : "unknown");

    bool isSoftware = false;
    if (renderer)
    {
        isSoftware = (strstr(renderer, "llvmpipe") != nullptr) ||
                     (strstr(renderer, "softpipe") != nullptr) ||
                     (strstr(renderer, "swrast") != nullptr) ||
                     (strstr(renderer, "Software") != nullptr) ||
                     (strstr(renderer, "Microsoft Basic") != nullptr) ||
                     (strstr(renderer, "GDI Generic") != nullptr);
    }

    ctx.doneCurrent();

    if (isSoftware)
    {
        qInfo() << "DeviceScreenFactory: Software renderer detected, using CPU path";
        return false;
    }

    qInfo() << "DeviceScreenFactory: Hardware GPU acceleration available";
    return true;
}

QWidget* create(QWidget* parent)
{
    if (probeGPUAcceleration())
    {
        qInfo() << "DeviceScreenFactory: Using GPU-accelerated renderer";
        return new DeviceScreenGL(parent);
    }

    qInfo() << "DeviceScreenFactory: Using software renderer";
    // Return nullptr to signal caller should use DeviceScreen
    return nullptr;
}

} // namespace DeviceScreenFactory
