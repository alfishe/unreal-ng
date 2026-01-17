/**
 * @file emulatorbinding.h
 * @brief Centralized binding between Emulator Core and Qt UI layer.
 *
 * EmulatorBinding is owned by MainWindow and provides a single source of truth
 * for emulator state. It subscribes to MessageCenter events, marshals callbacks
 * to the main thread, caches emulator state, and emits Qt signals for UI updates.
 *
 * Child windows (DebuggerWindow, LogWindow) connect to these signals.
 * They do NOT subscribe to MessageCenter directly.
 */

#pragma once

#include <QMutex>
#include <QObject>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"

class EmulatorBinding : public QObject, public Observer
{
    Q_OBJECT

public:
    /**
     * @brief Constructs EmulatorBinding.
     *
     * @what Creates binding instance, does NOT subscribe to MessageCenter yet.
     *
     * @triggered_by
     *   - MainWindow constructor
     *
     * @calls Nothing significant
     *
     * @conditions Must be created on main thread
     */
    explicit EmulatorBinding(QObject* parent = nullptr);

    /**
     * @brief Destructor - unsubscribes from MessageCenter.
     */
    virtual ~EmulatorBinding() override;

    // =========================================================================
    // Binding Lifecycle
    // =========================================================================

    /**
     * @brief Binds to an emulator instance for state tracking.
     *
     * @what Stores emulator reference, subscribes to per-emulator MessageCenter
     *       events (NC_STATE_CHANGE, NC_FRAME_REFRESH), checks initial state,
     *       emits bound() and ready()/notReady() signals.
     *
     * @triggered_by
     *   - MainWindow::handleEmulatorInstanceCreated() - new emulator created
     *   - MainWindow::handleEmulatorSelectionChanged() - user switches emulator
     *
     * @calls
     *   - MessageCenter::AddObserver() for NC_EMULATOR_STATE_CHANGE
     *   - MessageCenter::AddObserver() for NC_VIDEO_FRAME_REFRESH
     *   - updateReadyState() - checks if emulator is paused
     *   - emit bound()
     *   - emit ready() or notReady()
     *
     * @conditions
     *   - Must be called on main thread
     *   - emulator must not be nullptr
     *   - Should call unbind() first if switching emulators
     *
     * @special_cases
     *   - If emulator is already paused: caches state, emits ready() immediately
     *   - If emulator is running: emits notReady(), widgets show placeholder
     *   - If called with same emulator: refreshes state but doesn't re-subscribe
     *
     * @behavior_changes
     *   - After bind(): isBound() returns true
     *   - After bind(): isReady() depends on emulator state
     */
    void bind(Emulator* emulator);

    /**
     * @brief Unbinds from current emulator, clears cached state.
     *
     * @what Unsubscribes from MessageCenter, clears emulator reference and
     *       cached state, emits unbound() signal.
     *
     * @triggered_by
     *   - MainWindow::handleEmulatorInstanceDestroyed()
     *   - MainWindow::handleEmulatorSelectionChanged() (before rebinding)
     *   - MainWindow::closeEvent()
     *
     * @calls
     *   - MessageCenter::RemoveObserver() for subscribed events
     *   - emit unbound()
     *
     * @conditions Must be called on main thread
     *
     * @special_cases
     *   - If not bound: no-op
     *
     * @behavior_changes
     *   - After unbind(): isBound() returns false
     *   - After unbind(): isReady() returns false
     *   - After unbind(): emulator() returns nullptr
     */
    void unbind();

    // =========================================================================
    // State Accessors (Always Safe to Call)
    // =========================================================================

    /**
     * @brief Returns true if emulator is bound AND in inspectable state (paused).
     *
     * @what Widgets should check this before accessing z80State() or memory.
     *       When false, widgets should show placeholder/disabled state.
     */
    bool isReady() const;

    /**
     * @brief Returns true if an emulator is currently bound (may not be ready).
     */
    bool isBound() const;

    /**
     * @brief Returns current emulator state enum.
     *
     * @what Always safe to call. Returns StateUnknown if not bound.
     */
    EmulatorStateEnum state() const;

    /**
     * @brief Returns bound emulator pointer.
     *
     * @what May return nullptr if not bound. Use for lifecycle operations only.
     *       For state access, use cached accessors below.
     */
    Emulator* emulator() const;

    /**
     * @brief Returns cached Z80 state (registers, flags, etc).
     *
     * @what Returns nullptr if !isReady(). Cached on last pause/state change.
     */
    const Z80State* z80State() const;

    /**
     * @brief Returns cached program counter.
     *
     * @what Returns 0 if !isReady(). Cached on last pause/state change.
     */
    uint16_t pc() const;

signals:
    // =========================================================================
    // Signals (Connect to these from child windows)
    // =========================================================================

    /**
     * @brief Emitted when an emulator is bound.
     *
     * @what Consumers should prepare UI, store reference if needed.
     *       Does NOT mean emulator is ready to inspect.
     */
    void bound();

    /**
     * @brief Emitted when emulator is unbound.
     *
     * @what Consumers should clear cached state, disable UI.
     */
    void unbound();

    /**
     * @brief Emitted when emulator state changes.
     *
     * @param state New emulator state
     *
     * @what MainWindow uses to update menus. DebuggerWindow dispatches to children.
     */
    void stateChanged(EmulatorStateEnum state);

    /**
     * @brief Emitted when emulator becomes inspectable (paused).
     *
     * @what Consumers can now safely access z80State(), pc(), memory.
     *       DebuggerWindow dispatches cached state to widgets.
     */
    void ready();

    /**
     * @brief Emitted when emulator is no longer inspectable (running/stopped).
     *
     * @what Consumers should show placeholder state.
     */
    void notReady();

    /**
     * @brief Emitted on video frame refresh.
     *
     * @what Connect DeviceScreen to this for screen updates.
     */
    void frameRefresh();

private slots:
    /**
     * @brief Receives MessageCenter callbacks (called from background thread).
     *
     * @what Marshals to main thread, updates cached state, emits appropriate signals.
     */
    void onMessageCenterEvent(int id, Message* message);

private:
    void updateReadyState();
    void cacheEmulatorState();
    void subscribeToMessageCenter();
    void unsubscribeFromMessageCenter();

    Emulator* m_emulator = nullptr;
    EmulatorStateEnum m_state = StateUnknown;
    bool m_isReady = false;
    bool m_isSubscribed = false;

    Z80State m_cachedZ80State{};
    uint16_t m_cachedPC = 0;

    mutable QMutex m_mutex;
};
