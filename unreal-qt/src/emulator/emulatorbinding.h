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

#include <chrono>

#include <QMutex>
#include <QObject>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "tape/tapeuisnapshot.h"

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

    // =========================================================================
    // Tape transport commands (design §9.3 — window commands go through here,
    // never straight to Tape*; each one runs inside the established
    // Pause() -> op -> Resume() bracket, mirroring the CLI/WebAPI handlers)
    // =========================================================================

    void tapePlay();               // parse-once, then resume-in-place or start at cursor
    void tapePause();              // freeze the head in place (no-op unless Playing)
    void tapeStop();               // stop playback and drop the image (control-plane semantics)
    void tapeRewind();             // seek to block 0, image and catalog kept
    void tapeSeekToBlock(size_t index);  // double-click / context-menu rewind (FR-10): seek arms, play delivers

    /// Copy of one parsed block's raw data (flag + payload + checksum for
    /// framed blocks; empty for pulse/control entries). Returns false when no
    /// image is loaded or the index is out of range. Feeds the block-content
    /// popup (r7).
    bool tapeGetBlockData(size_t index, std::vector<uint8_t>& out);

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

    /**
     * @brief Emitted when a CPU step completes (from external sources like WebAPI/Python/Lua).
     *
     * @what DebuggerWindow connects to this to update UI when step commands
     *       are triggered via automation interfaces, not the Qt UI directly.
     */
    void cpuStepComplete();

    /**
     * @brief Emitted with a coalesced tape state snapshot (≤ 10 Hz, plus
     *        immediate on tape-state or catalog-generation change).
     *
     * @what Produced on the emulator thread in the frame-end hook (plain POD
     *       reads of Tape fields), delivered to the UI thread queued. The
     *        catalog copy rides only when catalogGeneration changed.
     */
    void tapeStateChanged(const TapeUiSnapshot& snapshot);

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

    /// Builds the next TapeUiSnapshot on the emulator thread (frame-end hook).
    /// Returns false when the coalescer suppressed this tick.
    bool produceTapeSnapshot(TapeUiSnapshot& out);

    // Tape snapshot bookkeeping — accessed ONLY from the MessageCenter callback
    // (emulator thread): no locks needed by construction (design §10).
    std::string m_tapeImagePath;              // last path seen by the producer
    std::string m_tapeImageFormatId;          // last loaded format id ("" after image drop)
    uint64_t m_tapeCatalogGeneration = 0;
    TapePlaybackState m_lastTapeState = TapePlaybackState::Idle;
    std::chrono::steady_clock::time_point m_lastTapeSnapshotTime{};

    Emulator* m_emulator = nullptr;
    EmulatorStateEnum m_state = StateUnknown;
    bool m_isReady = false;
    bool m_isSubscribed = false;

    Z80State m_cachedZ80State{};
    uint16_t m_cachedPC = 0;

    mutable QMutex m_mutex;
};
