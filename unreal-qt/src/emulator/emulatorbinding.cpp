/**
 * @file emulatorbinding.cpp
 * @brief Implementation of EmulatorBinding class.
 */

#include "emulatorbinding.h"

#include <QDebug>
#include <QMetaObject>

#include "emulator/notifications.h"

EmulatorBinding::EmulatorBinding(QObject* parent) : QObject(parent) {}

EmulatorBinding::~EmulatorBinding()
{
    unbind();
}

void EmulatorBinding::bind(Emulator* emulator)
{
    if (!emulator)
    {
        qWarning() << "EmulatorBinding::bind() called with nullptr";
        return;
    }

    // If already bound to same emulator, just refresh state
    if (m_emulator == emulator)
    {
        updateReadyState();
        return;
    }

    // Unbind previous if any
    if (m_emulator)
    {
        unbind();
    }

    m_emulator = emulator;

    // Subscribe to per-emulator MessageCenter events
    subscribeToMessageCenter();

    // Check initial state and cache if ready
    updateReadyState();

    emit bound();

    qDebug() << "EmulatorBinding: Bound to emulator" << QString::fromStdString(emulator->GetId());
}

void EmulatorBinding::unbind()
{
    if (!m_emulator)
    {
        return;
    }

    qDebug() << "EmulatorBinding: Unbinding from emulator" << QString::fromStdString(m_emulator->GetId());

    unsubscribeFromMessageCenter();

    m_emulator = nullptr;
    m_state = StateUnknown;
    m_isReady = false;
    m_cachedPC = 0;
    m_cachedZ80State = Z80State{};

    emit unbound();
}

bool EmulatorBinding::isReady() const
{
    return m_isReady && m_emulator && (m_state == StatePaused || m_state == StateStopped);
}

bool EmulatorBinding::isBound() const
{
    return m_emulator != nullptr;
}

EmulatorStateEnum EmulatorBinding::state() const
{
    return m_state;
}

Emulator* EmulatorBinding::emulator() const
{
    return m_emulator;
}

const Z80State* EmulatorBinding::z80State() const
{
    if (!isReady())
    {
        return nullptr;
    }
    return &m_cachedZ80State;
}

uint16_t EmulatorBinding::pc() const
{
    if (!isReady())
    {
        return 0;
    }
    return m_cachedPC;
}

void EmulatorBinding::onMessageCenterEvent(int id, Message* message)
{
    Q_UNUSED(id);

    if (!message || !message->obj)
    {
        return;
    }

    // This may be called from background thread - marshal to main thread
    // Extract data we need before message goes out of scope
    EmulatorStateEnum newState = StateUnknown;
    bool isFrameRefresh = false;
    bool isStateChange = false;
    std::string emulatorId;

    if (auto* numberPayload = dynamic_cast<SimpleNumberPayload*>(message->obj))
    {
        // NC_STATE_CHANGE uses SimpleNumberPayload
        isStateChange = true;
        newState = static_cast<EmulatorStateEnum>(numberPayload->_payloadNumber);
        // Note: SimpleNumberPayload does not carry emulator ID, so we verify by checking m_emulator->GetState()
    }
    else if (auto* framePayload = dynamic_cast<EmulatorFramePayload*>(message->obj))
    {
        isFrameRefresh = true;
        emulatorId = framePayload->_emulatorId;
        // Filter: only process frames from our bound emulator
        if (m_emulator && emulatorId != m_emulator->GetId())
        {
            return;
        }
    }
    else
    {
        return;  // Unknown payload type
    }

    QMetaObject::invokeMethod(
        this,
        [this, newState, isFrameRefresh, isStateChange]() {
            if (isFrameRefresh)
            {
                emit frameRefresh();
                return;
            }

            if (!isStateChange || !m_emulator)
            {
                return;
            }

            // Verify this state change matches our emulator's actual state
            // (since SimpleNumberPayload doesn't carry emulator ID)
            if (m_emulator->GetState() != newState)
            {
                return;  // This state change is from a different emulator
            }
            EmulatorStateEnum previousState = m_state;
            m_state = newState;

            bool wasReady = m_isReady;
            bool isNowReady = (newState == StatePaused || newState == StateStopped);

            if (isNowReady && !wasReady)
            {
                // Transitioning to ready state - cache emulator data
                cacheEmulatorState();
                m_isReady = true;
                emit stateChanged(newState);
                emit ready();
            }
            else if (!isNowReady && wasReady)
            {
                // Transitioning away from ready state
                m_isReady = false;
                emit stateChanged(newState);
                emit notReady();
            }
            else
            {
                // State changed but ready status unchanged
                if (isNowReady)
                {
                    cacheEmulatorState();
                }
                emit stateChanged(newState);
            }
        },
        Qt::QueuedConnection);
}

void EmulatorBinding::updateReadyState()
{
    if (!m_emulator)
    {
        m_isReady = false;
        return;
    }

    m_state = m_emulator->GetState();
    bool isNowReady = (m_state == StatePaused || m_state == StateStopped);

    if (isNowReady)
    {
        cacheEmulatorState();
        m_isReady = true;
        emit stateChanged(m_state);
        emit ready();
    }
    else
    {
        m_isReady = false;
        emit stateChanged(m_state);
        emit notReady();
    }
}

void EmulatorBinding::cacheEmulatorState()
{
    if (!m_emulator)
    {
        return;
    }

    // Cache Z80 state
    if (Z80State* z80 = m_emulator->GetZ80State())
    {
        m_cachedZ80State = *z80;
        m_cachedPC = z80->pc;
    }
}

void EmulatorBinding::subscribeToMessageCenter()
{
    if (m_isSubscribed)
    {
        return;
    }

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    ObserverCallbackMethod stateCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    ObserverCallbackMethod frameCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, frameCallback);

    m_isSubscribed = true;

    qDebug() << "EmulatorBinding: Subscribed to MessageCenter events";
}

void EmulatorBinding::unsubscribeFromMessageCenter()
{
    if (!m_isSubscribed)
    {
        return;
    }

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    ObserverCallbackMethod stateCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    ObserverCallbackMethod frameCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, frameCallback);

    m_isSubscribed = false;

    qDebug() << "EmulatorBinding: Unsubscribed from MessageCenter events";
}
