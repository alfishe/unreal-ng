#include "devicescreenwrapper.h"
#include "devicescreen.h"
#include "devicescreen_gl.h"

#include <QDebug>

DeviceScreenWrapper::DeviceScreenWrapper(QWidget* parent)
    : DeviceScreenWrapper(parent, DeviceScreenFactory::probeGPUAcceleration())
{
}

DeviceScreenWrapper::DeviceScreenWrapper(QWidget* parent, bool useGPU)
    : QObject(parent)
{
    // Try GPU-accelerated rendering if requested
    if (useGPU && DeviceScreenFactory::probeGPUAcceleration())
    {
        try
        {
            _gpu = new DeviceScreenGL(parent);
            _widget = _gpu;
            _useGPU = true;
            qInfo() << "DeviceScreenWrapper: Using GPU-accelerated rendering";
        }
        catch (...)
        {
            qWarning() << "DeviceScreenWrapper: GPU widget creation failed";
            delete _gpu;
            _gpu = nullptr;
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
    // Widgets are owned by Qt parent, but we should clean up
}

void DeviceScreenWrapper::init(uint16_t width, uint16_t height, void* buffer)
{
    if (_useGPU && _gpu)
        _gpu->init(width, height, buffer);
    else if (_software)
        _software->init(width, height, buffer);
}

void DeviceScreenWrapper::detach()
{
    if (_useGPU && _gpu)
        _gpu->detach();
    else if (_software)
        _software->detach();
}

void DeviceScreenWrapper::refresh()
{
    if (_useGPU && _gpu)
        _gpu->refresh();
    else if (_software)
        _software->refresh();
}

void DeviceScreenWrapper::setFrameSource(std::function<bool(uint8_t*, size_t)> fn)
{
    if (_useGPU && _gpu)
        _gpu->setFrameSource(fn);
    else if (_software)
        _software->setFrameSource(fn);
}

void DeviceScreenWrapper::setEmulator(std::shared_ptr<Emulator> emulator)
{
    if (_useGPU && _gpu)
        _gpu->setEmulator(emulator);
    else if (_software)
        _software->setEmulator(emulator);
}

void DeviceScreenWrapper::setDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
    _hasViewport = true;

    if (_useGPU && _gpu)
        _gpu->setDisplayViewport(viewport);
    else if (_software)
        _software->setDisplayViewport(viewport);
}

void DeviceScreenWrapper::clearDisplayViewport()
{
    _hasViewport = false;

    if (_useGPU && _gpu)
        _gpu->clearDisplayViewport();
    else if (_software)
        _software->clearDisplayViewport();
}

void DeviceScreenWrapper::prepareForShutdown()
{
    if (_useGPU && _gpu)
        _gpu->prepareForShutdown();
    else if (_software)
        _software->prepareForShutdown();
}

void DeviceScreenWrapper::handleExternalKeyPress(QKeyEvent* event)
{
    if (_useGPU && _gpu)
        _gpu->handleExternalKeyPress(event);
    else if (_software)
        _software->handleExternalKeyPress(event);
}

void DeviceScreenWrapper::handleExternalKeyRelease(QKeyEvent* event)
{
    if (_useGPU && _gpu)
        _gpu->handleExternalKeyRelease(event);
    else if (_software)
        _software->handleExternalKeyRelease(event);
}

QImage DeviceScreenWrapper::grabFramebuffer()
{
    if (_useGPU && _gpu)
        return _gpu->grabFramebuffer();
    else if (_software)
        return _software->grabFramebuffer();
    return QImage();
}

QSize DeviceScreenWrapper::sizeHint() const
{
    if (_useGPU && _gpu)
        return _gpu->sizeHint();
    else if (_software)
        return _software->sizeHint();
    return QSize(352, 288);
}

void DeviceScreenWrapper::setCRTEffectsEnabled(bool enabled)
{
    if (_useGPU && _gpu)
        _gpu->setCRTEffectsEnabled(enabled);
    // No-op for software renderer
}

bool DeviceScreenWrapper::crtEffectsEnabled() const
{
    if (_useGPU && _gpu)
        return _gpu->crtEffectsEnabled();
    return false;
}

void DeviceScreenWrapper::setCRTProfile(CRTProfile profile)
{
    if (_useGPU && _gpu)
        _gpu->setCRTProfile(profile);
}

void DeviceScreenWrapper::setCRTProfile(const CRTProfileParams& params)
{
    if (_useGPU && _gpu)
        _gpu->setCRTProfile(params);
}

CRTProfile DeviceScreenWrapper::crtProfile() const
{
    if (_useGPU && _gpu)
        return _gpu->crtProfile();
    return CRTProfile::None;
}

CRTProfileParams DeviceScreenWrapper::crtParams() const
{
    if (_useGPU && _gpu)
        return _gpu->crtParams();
    return CRTProfileParams::None();
}

bool DeviceScreenWrapper::loadCustomShader(const QString& name)
{
    if (_useGPU && _gpu)
        return _gpu->loadCustomShader(name);
    return false;
}

std::vector<QString> DeviceScreenWrapper::availableShaders()
{
    return DeviceScreenGL::availableShaders();
}

bool DeviceScreenWrapper::isGPUAvailable()
{
    return DeviceScreenFactory::probeGPUAcceleration();
}

void DeviceScreenWrapper::setTemporalBlendingEnabled(bool enabled)
{
    if (_useGPU && _gpu)
        _gpu->setTemporalBlendingEnabled(enabled);
    else if (_software)
        _software->setTemporalBlendingEnabled(enabled);
}

bool DeviceScreenWrapper::temporalBlendingEnabled() const
{
    if (_useGPU && _gpu)
        return _gpu->temporalBlendingEnabled();
    else if (_software)
        return _software->temporalBlendingEnabled();
    return false;
}

void DeviceScreenWrapper::setTemporalHistorySize(int frames)
{
    if (_useGPU && _gpu)
        _gpu->setTemporalHistorySize(frames);
    else if (_software)
        _software->setTemporalHistorySize(frames);
}

int DeviceScreenWrapper::temporalHistorySize() const
{
    if (_useGPU && _gpu)
        return _gpu->temporalHistorySize();
    else if (_software)
        return _software->temporalHistorySize();
    return 2;
}

void DeviceScreenWrapper::setTemporalWeightMode(int mode)
{
    if (_useGPU && _gpu)
        _gpu->setTemporalWeightMode(mode);
    else if (_software)
        _software->setTemporalWeightMode(mode);
}

int DeviceScreenWrapper::temporalWeightMode() const
{
    if (_useGPU && _gpu)
        return _gpu->temporalWeightMode();
    else if (_software)
        return _software->temporalWeightMode();
    return 0;
}
