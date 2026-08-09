#include "EmulatorWidget.h"
#include "KeyboardManager.h"
#include <QDebug>
#include <QFileInfo>

#ifdef HAS_EMULATOR_CORE
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/notifications.h"
#endif

EmulatorWidget::EmulatorWidget(QObject* parent)
    : QObject(parent)
{
#ifdef HAS_EMULATOR_CORE
    m_emulatorManager = EmulatorManager::GetInstance();
#endif
}

EmulatorWidget::~EmulatorWidget()
{
    stop();
}

bool EmulatorWidget::start()
{
#ifdef HAS_EMULATOR_CORE
    qDebug() << "EmulatorWidget::start() - HAS_EMULATOR_CORE defined";
    if (m_emulator) {
        qDebug() << "Emulator already exists";
        return true;
    }

    qDebug() << "Creating emulator via EmulatorManager...";
    m_emulator = m_emulatorManager->CreateEmulator("poc-gui", LoggerLevel::LogWarning);
    if (!m_emulator)
    {
        qWarning() << "Failed to create emulator instance";
        return false;
    }

    m_emulatorId = m_emulator->GetId();
    qDebug() << "Emulator created with ID:" << QString::fromStdString(m_emulatorId);
    m_emulator->DebugOff();

    EmulatorContext* ctx = m_emulator->GetContext();
    qDebug() << "EmulatorContext:" << (void*)ctx;
    if (ctx && ctx->pScreen)
    {
        qDebug() << "Screen exists, getting framebuffer...";
        FramebufferDescriptor desc = ctx->pScreen->GetFramebufferDescriptor();
        m_framebuffer = desc.memoryBuffer;
        m_width = desc.width;
        m_height = desc.height;
        qDebug() << "Framebuffer:" << m_width << "x" << m_height << "ptr:" << m_framebuffer;
    } else {
        qWarning() << "No screen in emulator context!";
    }

    subscribeToMessages();
    qDebug() << "Starting emulator async...";
    m_emulatorManager->StartEmulatorAsync(m_emulatorId);

    emit stateChanged();

    // Emit initial resolution (we missed the notification during creation)
    if (m_width > 0 && m_height > 0) {
        emit resolutionChanged(m_width, m_height);
    }

    return true;
#else
    qWarning() << "Emulator core not available (HAS_EMULATOR_CORE not defined)";
    return false;
#endif
}

void EmulatorWidget::stop()
{
#ifdef HAS_EMULATOR_CORE
    if (!m_emulator)
        return;

    unsubscribeFromMessages();
    m_emulatorManager->StopEmulator(m_emulatorId);
    m_emulatorManager->RemoveEmulator(m_emulatorId);

    m_emulator.reset();
    m_emulatorId.clear();
    m_framebuffer = nullptr;

    emit stateChanged();
#endif
}

void EmulatorWidget::pause()
{
#ifdef HAS_EMULATOR_CORE
    if (m_emulator && m_emulator->IsRunning() && !m_emulator->IsPaused())
    {
        m_emulatorManager->PauseEmulator(m_emulatorId);
        emit stateChanged();
    }
#endif
}

void EmulatorWidget::resume()
{
#ifdef HAS_EMULATOR_CORE
    if (m_emulator && m_emulator->IsPaused())
    {
        m_emulatorManager->ResumeEmulator(m_emulatorId);
        emit stateChanged();
    }
#endif
}

void EmulatorWidget::reset()
{
#ifdef HAS_EMULATOR_CORE
    if (m_emulator)
    {
        m_emulator->Reset();
        emit stateChanged();
    }
#endif
}

bool EmulatorWidget::isRunning() const
{
#ifdef HAS_EMULATOR_CORE
    return m_emulator && m_emulator->IsRunning();
#else
    return false;
#endif
}

bool EmulatorWidget::isPaused() const
{
#ifdef HAS_EMULATOR_CORE
    return m_emulator && m_emulator->IsPaused();
#else
    return false;
#endif
}

bool EmulatorWidget::loadFile(const QString& path)
{
#ifdef HAS_EMULATOR_CORE
    if (!m_emulator)
    {
        if (!start())
            return false;
    }

    QFileInfo info(path);
    if (!info.exists())
    {
        qWarning() << "File not found:" << path;
        return false;
    }

    std::string filePath = path.toStdString();
    QString ext = info.suffix().toLower();

    if (ext == "sna" || ext == "z80")
    {
        return m_emulator->LoadSnapshot(filePath);
    }
    else if (ext == "tap" || ext == "tzx")
    {
        return m_emulator->LoadTape(filePath);
    }
    else if (ext == "trd" || ext == "scl" || ext == "fdi" || ext == "td0" || ext == "udi")
    {
        return m_emulator->LoadDisk(filePath);
    }

    qWarning() << "Unknown file type:" << ext;
    return false;
#else
    Q_UNUSED(path);
    return false;
#endif
}

void EmulatorWidget::handleKeyPress(QKeyEvent* event)
{
    if (event->isAutoRepeat())
        return;

#ifdef HAS_EMULATOR_CORE
    if (!m_emulator)
        return;

    quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());
    if (zxKey != ZXKEY_NONE)
    {
        m_pressedKeys.insert(zxKey);
        std::string targetId = m_emulator->GetUUID();
        KeyboardEvent* keyEvent = new KeyboardEvent(zxKey, KEY_PRESSED, targetId);
        MessageCenter::DefaultMessageCenter().Post(MC_KEY_PRESSED, keyEvent);
    }
#else
    Q_UNUSED(event);
#endif
}

void EmulatorWidget::handleKeyRelease(QKeyEvent* event)
{
    if (event->isAutoRepeat())
        return;

#ifdef HAS_EMULATOR_CORE
    if (!m_emulator)
        return;

    quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());
    if (zxKey != ZXKEY_NONE)
    {
        m_pressedKeys.remove(zxKey);
        std::string targetId = m_emulator->GetUUID();
        KeyboardEvent* keyEvent = new KeyboardEvent(zxKey, KEY_RELEASED, targetId);
        MessageCenter::DefaultMessageCenter().Post(MC_KEY_RELEASED, keyEvent);
    }
#else
    Q_UNUSED(event);
#endif
}

void EmulatorWidget::releaseAllKeys()
{
#ifdef HAS_EMULATOR_CORE
    if (!m_emulator || m_pressedKeys.isEmpty())
        return;

    std::string targetId = m_emulator->GetUUID();
    for (quint8 zxKey : m_pressedKeys)
    {
        KeyboardEvent* keyEvent = new KeyboardEvent(zxKey, KEY_RELEASED, targetId);
        MessageCenter::DefaultMessageCenter().Post(MC_KEY_RELEASED, keyEvent);
    }
    m_pressedKeys.clear();
#endif
}

#ifdef HAS_EMULATOR_CORE

void EmulatorWidget::subscribeToMessages()
{
    if (m_subscribed)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    Observer* obs = static_cast<Observer*>(this);

    mc.AddObserver(NC_VIDEO_FRAME_REFRESH, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleVideoFrameRefresh));
    mc.AddObserver(NC_VIDEO_RESOLUTION_CHANGED, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleResolutionChanged));
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleEmulatorStateChanged));

    m_subscribed = true;
}

void EmulatorWidget::unsubscribeFromMessages()
{
    if (!m_subscribed)
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    Observer* obs = static_cast<Observer*>(this);

    mc.RemoveObserver(NC_VIDEO_FRAME_REFRESH, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleVideoFrameRefresh));
    mc.RemoveObserver(NC_VIDEO_RESOLUTION_CHANGED, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleResolutionChanged));
    mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obs,
        static_cast<ObserverCallbackMethod>(&EmulatorWidget::handleEmulatorStateChanged));

    m_subscribed = false;
}

void EmulatorWidget::handleVideoFrameRefresh(int id, Message* message)
{
    Q_UNUSED(id);

    if (!m_emulator || !message || !message->obj)
        return;

    EmulatorFramePayload* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
    if (!payload)
        return;

    // Filter by emulator UUID
    if (payload->_emulatorId != m_emulator->GetUUID())
        return;

    // MessageCenter callback runs on worker thread - queue to Qt main thread
    QMetaObject::invokeMethod(this, [this]() {
        emit frameReady();
    }, Qt::QueuedConnection);
}

void EmulatorWidget::handleResolutionChanged(int id, Message* message)
{
    Q_UNUSED(id);

    if (!m_emulator || !message || !message->obj)
        return;

    VideoResolutionPayload* payload = dynamic_cast<VideoResolutionPayload*>(message->obj);
    if (!payload)
        return;

    // Filter by emulator UUID
    if (payload->_emulatorId != m_emulator->GetUUID())
        return;

    int newWidth = payload->_width;
    int newHeight = payload->_height;

    // Update framebuffer pointer and dimensions
    if (auto ctx = m_emulator->GetContext()) {
        if (ctx->pScreen) {
            FramebufferDescriptor desc = ctx->pScreen->GetFramebufferDescriptor();
            m_framebuffer = desc.memoryBuffer;
            m_width = desc.width;
            m_height = desc.height;
        }
    }

    // Queue signal to Qt main thread
    QMetaObject::invokeMethod(this, [this, newWidth, newHeight]() {
        emit resolutionChanged(newWidth, newHeight);
    }, Qt::QueuedConnection);
}

void EmulatorWidget::handleEmulatorStateChanged(int id, Message* message)
{
    Q_UNUSED(id);
    Q_UNUSED(message);

    emit stateChanged();
}

#endif
