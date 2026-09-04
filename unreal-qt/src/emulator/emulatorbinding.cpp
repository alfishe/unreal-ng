/**
 * @file emulatorbinding.cpp
 * @brief Implementation of EmulatorBinding class.
 */

#include "emulatorbinding.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

namespace
{
/// Pause() -> op -> Resume() bracket, same contract as the CLI/WebAPI tape
/// handlers (design §7.1). Pause only when actually running; resume exactly
/// then. RAII so an early return can never leave the emulator parked.
class EmulatorPauseBracket
{
public:
    explicit EmulatorPauseBracket(Emulator* emulator)
        : _emulator(emulator), _wasRunning(emulator && emulator->IsRunning() && !emulator->IsPaused())
    {
        if (_wasRunning)
        {
            _emulator->Pause();
            QThread::msleep(10);  // Give emulator time to pause
        }
    }

    ~EmulatorPauseBracket()
    {
        if (_wasRunning)
        {
            _emulator->Resume();
        }
    }

private:
    Emulator* _emulator;
    bool _wasRunning;
};
}  // namespace

EmulatorBinding::EmulatorBinding(QObject* parent) : QObject(parent)
{
    // Queued tapeStateChanged(const TapeUiSnapshot&) delivery needs the type
    // registered with the meta-object system
    qRegisterMetaType<TapeUiSnapshot>("TapeUiSnapshot");
}

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

    // Get initial state BEFORE emitting bound() so listeners see correct state
    m_state = m_emulator->GetState();

    // Notify listeners that we're bound (BEFORE ready signal)
    // This allows DebuggerWindow to set _emulator before ready() fires
    emit bound();

    // Check initial state and cache if ready
    // This may emit ready() - but now bound() has already fired
    updateReadyState();

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

    // Reset tape snapshot bookkeeping so a later bind re-ships the catalog
    m_tapeImagePath.clear();
    m_tapeImageFormatId.clear();
    m_tapeCatalogGeneration = 0;
    m_lastTapeState = TapePlaybackState::Idle;
    m_lastTapeSnapshotTime = {};

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

    if (!message)
    {
        return;
    }

    // Handle NC_EXECUTION_CPU_STEP (has no payload object)
    // This is triggered by all automation modules: WebAPI, Python, Lua, CLI
    // When the payload is null, this is a step event
    if (!message->obj)
    {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                // Only emit if we have a bound emulator that's paused
                if (m_emulator && m_emulator->IsPaused())
                {
                    cacheEmulatorState();
                    emit cpuStepComplete();
                }
            },
            Qt::QueuedConnection);
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

    // Tape snapshot (design §9.3): produced HERE, on the frame-end callback
    // thread — the same point the fast-load trap runs at, where plain POD
    // reads of Tape fields are safe. Suppressed ticks ship nothing.
    TapeUiSnapshot tapeSnapshot;
    const bool hasTapeSnapshot = isFrameRefresh && produceTapeSnapshot(tapeSnapshot);

    QMetaObject::invokeMethod(
        this,
        [this, newState, isFrameRefresh, isStateChange, tapeSnapshot, hasTapeSnapshot]() {
            if (isFrameRefresh)
            {
                emit frameRefresh();
                if (hasTapeSnapshot)
                {
                    emit tapeStateChanged(tapeSnapshot);
                }
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
                // Transitioning away from ready state (Paused → Running)
                // Keep stale data visible - don't emit notReady()
                // notReady() is only for unbind or initial bind to running emulator
                m_isReady = false;
                emit stateChanged(newState);
                // Note: Intentionally NOT emitting notReady() here - widgets keep their last data
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

    // Subscribe to CPU step events for automation-triggered steps (WebAPI, Python, Lua, CLI)
    ObserverCallbackMethod stepCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, observerInstance, stepCallback);

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

    // Unsubscribe from CPU step events
    ObserverCallbackMethod stepCallback = static_cast<ObserverCallbackMethod>(&EmulatorBinding::onMessageCenterEvent);
    messageCenter.RemoveObserver(NC_EXECUTION_CPU_STEP, observerInstance, stepCallback);

    m_isSubscribed = false;

    qDebug() << "EmulatorBinding: Unsubscribed from MessageCenter events";
}

// =========================================================================
// Tape: snapshot producer (emulator thread) and transport commands (UI thread)
// =========================================================================

bool EmulatorBinding::produceTapeSnapshot(TapeUiSnapshot& out)
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return false;
    }

    Tape* tape = context->pTape;
    const std::string& path = context->coreState.tapeFilePath;

    // Generation key: path plus the tape's own loaded-format id. The format id
    // transitions ""->"tap"/"tzx" on first parse and back to "" on stopTape()
    // (image drop) — that second transition re-ships the table after Stop,
    // mirroring GET /tape's parse-once semantics.
    const std::string formatId = tape->GetLoadedFormatId();
    const bool generationChanged = (path != m_tapeImagePath) || (formatId != m_tapeImageFormatId);

    const TapePlaybackState state = tape->GetPlaybackState();
    const bool stateChanged = (state != m_lastTapeState);

    // Coalesce to <= 10 Hz; state and generation changes go out immediately
    const auto now = std::chrono::steady_clock::now();
    if (!generationChanged && !stateChanged && (now - m_lastTapeSnapshotTime) < std::chrono::milliseconds(100))
    {
        return false;
    }

    if (generationChanged)
    {
        m_tapeImagePath = path;
        m_tapeImageFormatId = formatId;
        ++m_tapeCatalogGeneration;
    }

    out = TapeUiSnapshot{};
    out.emulatorId = QString::fromStdString(m_emulator->GetId());
    // r8: window-title label — symbolic id when one was assigned, else the
    // "#"-prefixed id tail (mirrors Emulator's short-id display style)
    {
        const std::string symbolicId = m_emulator->GetSymbolicId();
        if (!symbolicId.empty())
        {
            out.emulatorLabel = QString::fromStdString(symbolicId);
        }
        else
        {
            const std::string& id = m_emulator->GetId();
            const size_t tail = id.size() > 12 ? id.size() - 12 : 0;
            out.emulatorLabel = QStringLiteral("#") + QString::fromStdString(id.substr(tail));
        }
    }
    out.filePath = QString::fromStdString(path);
    out.state = state;
    out.position = tape->GetPosition();
    out.cursor = tape->GetConsumptionCursor();
    out.fastTapeEnabled = context->pFeatureManager && context->pFeatureManager->isEnabled(Features::kFastTape);

    if (generationChanged)
    {
        // Parse-once (idempotent, path-keyed): fills the table on insert, not
        // on first Play. Safe at frame end for the same reason the fast-load
        // trap's own EnsureImageLoaded() call is.
        out.catalogChanged = true;
        out.catalogGeneration = m_tapeCatalogGeneration;
        out.catalogValid = !path.empty() && tape->EnsureImageLoaded();
        out.formatId = QString::fromStdString(tape->GetLoadedFormatId());
        m_tapeImageFormatId = tape->GetLoadedFormatId();  // post-parse value keeps the key stable
        if (out.catalogValid)
        {
            out.catalog = tape->GetBlockCatalog();  // the one copy, once per generation
            out.plan = tape->GetFastLoadPlan();
        }
    }

    m_lastTapeState = state;
    m_lastTapeSnapshotTime = now;
    return true;
}

void EmulatorBinding::tapePlay()
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return;
    }

    EmulatorPauseBracket bracket(m_emulator);

    // Parse-once (idempotent); paused -> resume the frozen position IN PLACE
    // (FR-6), otherwise start at the consumption cursor
    if (!context->pTape->EnsureImageLoaded())
    {
        return;
    }
    if (context->pTape->GetPlaybackState() == TapePlaybackState::Paused)
    {
        context->pTape->ResumePlaybackFromPause();
    }
    else
    {
        context->pTape->StartPlaybackAtCursor();
    }
}

void EmulatorBinding::tapePause()
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return;
    }

    EmulatorPauseBracket bracket(m_emulator);

    if (context->pTape->GetPlaybackState() == TapePlaybackState::Playing)
    {
        context->pTape->pausePlayback();
    }
}

void EmulatorBinding::tapeStop()
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return;
    }

    EmulatorPauseBracket bracket(m_emulator);

    // Control-plane semantics, same sequence as the CLI/WebAPI eject handlers:
    // stopTape() drops the parsed image, and clearing the path keeps the next
    // generation snapshot from re-parsing the same file via EnsureImageLoaded()
    context->pTape->stopTape();
    context->coreState.tapeFilePath.clear();
}

void EmulatorBinding::tapeRewind()
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return;
    }

    EmulatorPauseBracket bracket(m_emulator);

    // Rewind keeps the image and catalog (FR-5)
    if (context->pTape->EnsureImageLoaded())
    {
        context->pTape->RewindToStart();
    }
}

void EmulatorBinding::tapeSeekToBlock(size_t index)
{
    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return;
    }

    EmulatorPauseBracket bracket(m_emulator);

    // Seek arms, play delivers (FR-4); forward and backward, consumed included
    if (context->pTape->EnsureImageLoaded())
    {
        context->pTape->SeekToBlock(index);
    }
}

bool EmulatorBinding::tapeGetBlockData(size_t index, std::vector<uint8_t>& out)
{
    out.clear();

    EmulatorContext* context = m_emulator ? m_emulator->GetContext() : nullptr;
    if (!context || !context->pTape)
    {
        return false;
    }

    EmulatorPauseBracket bracket(m_emulator);

    // Raw byte copy of one parsed block (flag + payload + checksum for framed
    // blocks; empty for pulse/control entries). The copy is all that leaves
    // the emulator thread — the block-content dialog never touches Tape*.
    const std::vector<TapeBlock>& blocks = context->pTape->GetBlocks();
    if (index >= blocks.size())
    {
        return false;
    }
    out = blocks[index].data;
    return true;
}
