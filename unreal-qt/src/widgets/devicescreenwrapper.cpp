#include "devicescreenwrapper.h"
#include "devicescreen.h"
#include "devicescreen_gl.h"
#include "devicescreenglwindow.h"

#include <QDebug>
#include <QWidget>

DeviceScreenWrapper::DeviceScreenWrapper(QWidget* parent)
    : DeviceScreenWrapper(parent, DeviceScreenFactory::probeGPUAcceleration())
{
}

DeviceScreenWrapper::DeviceScreenWrapper(QWidget* parent, bool useGPU)
    : QObject(parent)
{
    // Try GPU-accelerated rendering with QOpenGLWindow for tear-free vsync
    if (useGPU && DeviceScreenFactory::probeGPUAcceleration())
    {
        try
        {
            _gpuWindow = new DeviceScreenGLWindow();
            _widget = QWidget::createWindowContainer(_gpuWindow, parent);
            _widget->setMinimumSize(352, 288);
            _widget->setFocusPolicy(Qt::StrongFocus);
            _useGPU = true;
            qInfo() << "DeviceScreenWrapper: Using GPU-accelerated rendering (QOpenGLWindow)";
        }
        catch (...)
        {
            qWarning() << "DeviceScreenWrapper: GPU window creation failed";
            delete _gpuWindow;
            _gpuWindow = nullptr;
        }
    }

    // Fall back to software rendering
    if (!_useGPU)
    {
        _software = new DeviceScreen(parent);
        _widget = _software;
        qInfo() << "DeviceScreenWrapper: Using software rendering";
    }
}

DeviceScreenWrapper::~DeviceScreenWrapper()
{
    // The screen widget is parented to the content frame, so Qt would only destroy it
    // together with the frame. When the wrapper is replaced at runtime (GPU <-> software
    // switch) the orphaned widget would otherwise stay alive and visible at its old
    // geometry, leaving a stale copy of the screen behind after every resize.
    delete _widget;
    _widget = nullptr;
    _gpuWindow = nullptr;  // Owned by the container widget
    _software = nullptr;
}

void DeviceScreenWrapper::init(uint16_t width, uint16_t height, void* buffer)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->init(width, height, buffer);
    else if (_software)
        _software->init(width, height, buffer);

    emit screenInitialized();
}

void DeviceScreenWrapper::detach()
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->detach();
    else if (_software)
        _software->detach();
}

void DeviceScreenWrapper::refresh()
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->refresh();
    else if (_software)
        _software->refresh();
}

void DeviceScreenWrapper::setFrameSource(std::function<bool(uint8_t*, size_t)> fn)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setFrameSource(fn);
    else if (_software)
        _software->setFrameSource(fn);
}

void DeviceScreenWrapper::setEmulator(std::shared_ptr<Emulator> emulator)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setEmulator(emulator);
    else if (_software)
        _software->setEmulator(emulator);
}

void DeviceScreenWrapper::setDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
    _hasViewport = true;

    if (_useGPU && _gpuWindow)
        _gpuWindow->setDisplayViewport(viewport);
    else if (_software)
        _software->setDisplayViewport(viewport);
}

void DeviceScreenWrapper::clearDisplayViewport()
{
    _hasViewport = false;

    if (_useGPU && _gpuWindow)
        _gpuWindow->clearDisplayViewport();
    else if (_software)
        _software->clearDisplayViewport();
}

void DeviceScreenWrapper::prepareForShutdown()
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->prepareForShutdown();
    else if (_software)
        _software->prepareForShutdown();
}

void DeviceScreenWrapper::handleExternalKeyPress(QKeyEvent* event)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->handleExternalKeyPress(event);
    else if (_software)
        _software->handleExternalKeyPress(event);
}

void DeviceScreenWrapper::handleExternalKeyRelease(QKeyEvent* event)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->handleExternalKeyRelease(event);
    else if (_software)
        _software->handleExternalKeyRelease(event);
}

QImage DeviceScreenWrapper::grabFramebuffer()
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->grabFramebuffer();
    else if (_software)
        return _software->grabFramebuffer();
    return QImage();
}

QSize DeviceScreenWrapper::sizeHint() const
{
    if (_software)
        return _software->sizeHint();
    return QSize(352, 288);
}

void DeviceScreenWrapper::setCRTEffectsEnabled(bool enabled)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setCRTEffectsEnabled(enabled);
    else if (_software)
        _software->setCRTEffectsEnabled(enabled);
}

bool DeviceScreenWrapper::crtEffectsEnabled() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->crtEffectsEnabled();
    else if (_software)
        return _software->crtEffectsEnabled();
    return false;
}

void DeviceScreenWrapper::setCRTProfile(CRTProfile profile)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setCRTProfile(profile);
    else if (_software)
        _software->setCRTProfile(profile);
}

void DeviceScreenWrapper::setCRTProfile(const CRTProfileParams& params)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setCRTProfile(params);
    else if (_software)
        _software->setCRTProfile(params);
}

CRTProfile DeviceScreenWrapper::crtProfile() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->crtProfile();
    else if (_software)
        return _software->crtProfile();
    return CRTProfile::None;
}

CRTProfileParams DeviceScreenWrapper::crtParams() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->crtParams();
    else if (_software)
        return _software->crtParams();
    return CRTProfileParams::None();
}

bool DeviceScreenWrapper::loadCustomShader(const QString& name)
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->loadCustomShader(name);
    return false;
}

std::vector<QString> DeviceScreenWrapper::availableShaders()
{
    return DeviceScreenGLWindow::availableShaders();
}

bool DeviceScreenWrapper::isGPUAvailable()
{
    return DeviceScreenFactory::probeGPUAcceleration();
}

void DeviceScreenWrapper::setTemporalBlendingEnabled(bool enabled)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setTemporalBlendingEnabled(enabled);
    else if (_software)
        _software->setTemporalBlendingEnabled(enabled);
}

bool DeviceScreenWrapper::temporalBlendingEnabled() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->temporalBlendingEnabled();
    else if (_software)
        return _software->temporalBlendingEnabled();
    return false;
}

void DeviceScreenWrapper::setTemporalHistorySize(int frames)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setTemporalHistorySize(frames);
    else if (_software)
        _software->setTemporalHistorySize(frames);
}

int DeviceScreenWrapper::temporalHistorySize() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->temporalHistorySize();
    else if (_software)
        return _software->temporalHistorySize();
    return 2;
}

void DeviceScreenWrapper::setTemporalWeightMode(int mode)
{
    if (_useGPU && _gpuWindow)
        _gpuWindow->setTemporalWeightMode(mode);
    else if (_software)
        _software->setTemporalWeightMode(mode);
}

int DeviceScreenWrapper::temporalWeightMode() const
{
    if (_useGPU && _gpuWindow)
        return _gpuWindow->temporalWeightMode();
    else if (_software)
        return _software->temporalWeightMode();
    return 0;
}
