#ifndef FDCSTATUSWIDGET_H
#define FDCSTATUSWIDGET_H

#include <QLabel>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <mutex>

#include "3rdparty/message-center/messagecenter.h"

class DebuggerWindow;
class Emulator;

/// Compact FDC status strip for the debugger window:
///
///   FDC   A:  T40  S09  H1   MOTOR ON   Read
///
/// Push-driven: WD1793 posts diff-gated NC_FDC_STATE_CHANGED snapshots, the observer
/// callback (running on the message_center_worker thread) copies the latest snapshot into
/// a pending slot and schedules a queued flush on the GUI thread at a bounded rate (~20 Hz).
/// Unlike pull-based debugger widgets it keeps updating while the emulator is running.
///
/// Seeding is pull-based and happens only on activation (showEvent) and rebind (reset()) so
/// the strip is not blank until the first FDC activity.
class FdcStatusWidget : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit FdcStatusWidget(QWidget* parent = nullptr);
    virtual ~FdcStatusWidget() override;

    /// Plain value snapshot rendered by the widget. Sourced either from an FDCStatePayload
    /// or from the one-time seed read. Kept free of any WD1793 types so this header does not
    /// have to include wd1793.h (its 'signals' member conflicts with the Qt macro in moc runs).
    struct FdcSnapshot
    {
        bool valid = false;         // False: no emulator / model has no Beta128 FDC
        uint8_t driveId = 0;        // Selected drive index [0..3] (0 = A)
        uint8_t side = 0;           // 0 = bottom, 1 = top
        uint8_t trackRegister = 0;
        uint8_t sectorRegister = 0;
        uint8_t physicalTrack = 0;  // FDD head position (actual cylinder)
        uint8_t status = 0;         // WD1793 status register snapshot
        int command = -1;           // WD1793::WD_COMMANDS value, -1 = unknown
        bool busy = false;
        bool drq = false;
        bool motorOn = false;
        bool diskInserted = false;
    };

    /// region <Event handlers / Slots>
public slots:
    /// Called by DebuggerWindow::reset() and the notReadyForChildren chain - re-seeds
    /// from the currently bound emulator
    void reset();
    /// endregion </Event handlers / Slots>

protected:
    /// Activation on first visibility: subscribe to the message bus and seed from the current emulator
    void showEvent(QShowEvent* event) override;

    /// Observer callback for NC_FDC_STATE_CHANGED.
    /// Runs on the message_center_worker thread - must not touch any widget directly.
    void handleFdcStateChanged(int id, Message* message);

private:
    void subscribeToMessageBus();
    void unsubscribeFromMessageBus();

private slots:
    /// GUI thread only: drains the pending snapshot into the labels (rate-limited)
    void applyPendingState();

private:
    /// One-time pull read via EmulatorContext::pBetaDisk getters
    void seedFromContext();
    void renderSnapshot(const FdcSnapshot& snapshot);
    Emulator* getEmulator();

    DebuggerWindow* _debuggerWindow = nullptr;

    // UI labels (two combined lines)
    QLabel* _driveLabel = nullptr;   // Line 1: FDC A: T00 S01 H0
    QLabel* _motorLabel = nullptr;   // Line 2: MOTOR OFF Idle

    // Latest-wins coalescing between the message_center_worker and the GUI thread.
    // _pendingMutex guards _pendingState, _pendingDirty and _boundEmulatorId.
    std::mutex _pendingMutex;
    FdcSnapshot _pendingState;
    bool _pendingDirty = false;
    std::string _boundEmulatorId;
    std::atomic<bool> _flushScheduled { false };

    // Repaint rate limiting (~20 Hz)
    QTimer* _deferTimer = nullptr;
    QElapsedTimer _sinceLastFlush;

    bool _subscribed = false;
};

#endif // FDCSTATUSWIDGET_H
