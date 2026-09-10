#include "fdcstatuswidget.h"

// Avoid Qt 'signals' macro conflict with WD1793State::signals member
#undef signals
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#define signals Q_SIGNALS

#include "debugger/debuggerwindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>

FdcStatusWidget::FdcStatusWidget(QWidget* parent) : QWidget(parent)
{
    // Two-line compact widget using single labels per line for guaranteed alignment:
    // Line 1: FDC A: T00 S01 H0
    // Line 2: MOTOR OFF Idle
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(0);

    // Use monospace font for fixed-width display (14pt matches other debugger widgets)
    QFont monoFont("Consolas", 14);
    monoFont.setStyleHint(QFont::Monospace);

    // Line 1: Combined label for drive info
    _driveLabel = new QLabel("FDC A: H0 T00 S01", this);
    _driveLabel->setFont(monoFont);
    mainLayout->addWidget(_driveLabel);

    // Line 2: Combined label for motor + activity
    _motorLabel = new QLabel("MOTOR OFF  Idle ", this);
    _motorLabel->setFont(monoFont);
    mainLayout->addWidget(_motorLabel);

    setLayout(mainLayout);

    // Defer timer bounds the flush rate to ~20 Hz regardless of the notification rate
    _deferTimer = new QTimer(this);
    _deferTimer->setSingleShot(true);
    connect(_deferTimer, SIGNAL(timeout()), this, SLOT(applyPendingState()));
    _sinceLastFlush.start();

    _debuggerWindow = static_cast<DebuggerWindow*>(parent);

    renderSnapshot(FdcSnapshot());  // Initial placeholder until the first seed / notification
}

FdcStatusWidget::~FdcStatusWidget()
{
    // Remove the observer first: RemoveObserver() waits for in-flight dispatches to complete,
    // so no callback can race the destruction that follows
    unsubscribeFromMessageBus();
}

// Helper methods
Emulator* FdcStatusWidget::getEmulator()
{
    return _debuggerWindow ? _debuggerWindow->getEmulator() : nullptr;
}

void FdcStatusWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Activation on visibility (feature is off until the debugger window is actually shown)
    subscribeToMessageBus();
    seedFromContext();
}

void FdcStatusWidget::reset()
{
    // Binding changed (or children invalidated): discard stale pending state and re-seed
    seedFromContext();
}

void FdcStatusWidget::subscribeToMessageBus()
{
    if (_subscribed)
        return;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&FdcStatusWidget::handleFdcStateChanged);
    messageCenter.AddObserver(NC_FDC_STATE_CHANGED, observerInstance, callback);

    _subscribed = true;
}

void FdcStatusWidget::unsubscribeFromMessageBus()
{
    if (!_subscribed)
        return;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&FdcStatusWidget::handleFdcStateChanged);
    messageCenter.RemoveObserver(NC_FDC_STATE_CHANGED, observerInstance, callback);

    _subscribed = false;
}

void FdcStatusWidget::handleFdcStateChanged(int id, Message* message)
{
    (void)id;

    // message_center_worker thread: only plain data copies here, no widget access
    if (!message || !message->obj)
        return;

    FDCStatePayload* payload = dynamic_cast<FDCStatePayload*>(message->obj);
    if (!payload)
        return;

    // Instance filter: only snapshots from the emulator this debugger window is bound to
    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        if (_boundEmulatorId.empty() || payload->_emulatorId.toString() != _boundEmulatorId)
            return;
    }

    FdcSnapshot snapshot;
    snapshot.valid = true;
    snapshot.driveId = payload->_driveId;
    snapshot.side = payload->_side;
    snapshot.trackRegister = payload->_trackRegister;
    snapshot.sectorRegister = payload->_sectorRegister;
    snapshot.physicalTrack = payload->_physicalTrack;
    snapshot.status = payload->_status;
    snapshot.command = static_cast<int>(WD1793::decodeWD93Command(payload->_command));
    snapshot.busy = payload->_busy;
    snapshot.drq = payload->_drq;
    snapshot.motorOn = payload->_motorOn;
    snapshot.diskInserted = payload->_diskInserted;

    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        _pendingState = snapshot;  // Latest-wins coalescing
        _pendingDirty = true;
    }

    // Schedule a GUI-thread flush if none is queued yet
    if (!_flushScheduled.exchange(true))
    {
        QMetaObject::invokeMethod(this, "applyPendingState", Qt::QueuedConnection);
    }
}

void FdcStatusWidget::applyPendingState()
{
    _flushScheduled = false;

    // Rate limit: postpone the drain so label updates stay bounded at ~20 Hz
    const qint64 minIntervalMs = 50;
    qint64 sinceLast = _sinceLastFlush.elapsed();
    if (sinceLast < minIntervalMs)
    {
        if (!_deferTimer->isActive())
            _deferTimer->start(static_cast<int>(minIntervalMs - sinceLast));
        return;
    }

    FdcSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        if (!_pendingDirty)
            return;
        snapshot = _pendingState;
        _pendingDirty = false;
    }

    _sinceLastFlush.restart();
    renderSnapshot(snapshot);
}

void FdcStatusWidget::seedFromContext()
{
    // Refresh the instance filter for the worker-thread callback and drop stale snapshots
    Emulator* emulator = getEmulator();
    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        _boundEmulatorId = emulator ? emulator->GetId() : std::string();
        _pendingDirty = false;
    }

    FdcSnapshot snapshot;

    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (context && context->pBetaDisk)
    {
        const WD1793* fdc = context->pBetaDisk;
        snapshot.valid = true;
        snapshot.driveId = fdc->getSelectedDriveIndex();
        snapshot.side = fdc->getSideUp() ? 1 : 0;
        snapshot.trackRegister = fdc->getTrackRegister();
        snapshot.sectorRegister = fdc->getSectorRegister();
        snapshot.status = fdc->getStatusRegister();
        snapshot.busy = (snapshot.status & WD1793::WDS_BUSY) != 0;
        snapshot.command = static_cast<int>(fdc->getLastDecodedCommand());

        FDD* drive = const_cast<WD1793*>(fdc)->getDrive();
        if (drive)
        {
            snapshot.physicalTrack = static_cast<uint8_t>(drive->getTrack());
            snapshot.motorOn = drive->getMotor();
            snapshot.diskInserted = drive->isDiskInserted();
        }
    }

    renderSnapshot(snapshot);
}

void FdcStatusWidget::renderSnapshot(const FdcSnapshot& snapshot)
{
    if (!snapshot.valid)
    {
        _driveLabel->setText("FDC -: H- T-- S--");
        _motorLabel->setText("MOTOR ---  -----");
        setToolTip("Model has no Beta128 FDC");
        return;
    }

    // Line 1: FDC drive info (H T S order, fixed width)
    static const char* driveLetters[] = {"A", "B", "C", "D"};
    QString line1 = QString("FDC %1: H%2 T%3 S%4")
        .arg(driveLetters[snapshot.driveId & 0x03])
        .arg(snapshot.side)
        .arg(snapshot.trackRegister, 2, 10, QChar('0'))
        .arg(snapshot.sectorRegister, 2, 10, QChar('0'));
    _driveLabel->setText(line1);

    // Line 2: Motor + activity
    int command = snapshot.command;
    bool reading = snapshot.busy && (command == WD1793::WD_CMD_READ_SECTOR || command == WD1793::WD_CMD_READ_TRACK ||
                                     command == WD1793::WD_CMD_READ_ADDRESS);
    bool writing = snapshot.busy && (command == WD1793::WD_CMD_WRITE_SECTOR || command == WD1793::WD_CMD_WRITE_TRACK);

    QString activity = "Idle";
    QString activityStyle;
    if (reading)
    {
        activity = "Read";
        activityStyle = "color: blue; font-weight: bold;";
    }
    else if (writing)
    {
        activity = "Write";
        activityStyle = "color: red; font-weight: bold;";
    }
    else if (snapshot.busy)
    {
        activity = "Busy";
        activityStyle = "color: orange; font-weight: bold;";
    }

    // Line 2: Motor + activity (fixed width, separate colors via rich text)
    QString motorText = snapshot.motorOn ? "ON " : "OFF";
    QString motorColor = snapshot.motorOn ? "green" : "gray";

    QString activityColor;
    if (writing)
        activityColor = "red";
    else if (reading)
        activityColor = "green";
    else if (snapshot.busy)
        activityColor = "blue";
    else
        activityColor = "orange";

    QString line2 = QString("<span style='color:%1'>MOTOR %2</span>  <span style='color:%3'>%4</span>")
        .arg(motorColor).arg(motorText)
        .arg(activityColor).arg(activity, -5);
    _motorLabel->setText(line2);
    _motorLabel->setStyleSheet("");

    QString commandName = command >= 0 ? WD1793::getWD_COMMANDName(static_cast<WD1793::WD_COMMANDS>(command)) : "Unknown";
    QString tooltip = QString("WD1793 last command: %1\nPhysical track: %2\nStatus: 0x%3%4%5")
                          .arg(commandName)
                          .arg(snapshot.physicalTrack)
                          .arg(snapshot.status, 2, 16, QChar('0'))
                          .arg(snapshot.drq ? "\nDRQ: active" : "")
                          .arg(snapshot.diskInserted ? "" : "\nNo disk in drive");
    setToolTip(tooltip);
}
