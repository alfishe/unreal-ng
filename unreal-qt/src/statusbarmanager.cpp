#include "statusbarmanager.h"

#include <QCursor>
#include <algorithm>
#include <QDateTime>
#include <QFrame>
#include <QSettings>
#include <QToolTip>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/tape/tape.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

// WD93State has a member named `signals`, which clashes with the Qt keyword macro
#undef signals
#include "emulator/io/fdc/wd1793.h"
#define signals Q_SIGNALS
#include "emulator/sound/soundmanager.h"
#include "mainwindow.h"
#include "menumanager.h"
#include "widgets/statusindicator.h"

namespace
{
constexpr const char* kSettingsKey = "View/StatusBarVisible";
constexpr int kPollIntervalMs = 200;
}

StatusBarManager::StatusBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent)
    : QObject(parent), _mainWindow(mainWindow), _menuManager(menuManager)
{
    qRegisterMetaType<FDDStateInfo>("FDDStateInfo");

    _statusBar = _mainWindow->statusBar();
    _statusBar->setSizeGripEnabled(true);

    _tape = new StatusIndicator(QStringLiteral("tape"), tr("Tape"), _statusBar);
    _disk = new StatusIndicator(QStringLiteral("disk"), tr("Disk"), _statusBar);
    _hdd = new StatusIndicator(QStringLiteral("hdd"), tr("HDD"), _statusBar);
    _sound = new StatusIndicator(QStringLiteral("sound"), tr("Sound"), _statusBar);

    _hdd->setDetail(tr("not emulated"));
    _sound->setBlinking(false);
    _sound->setCursor(Qt::PointingHandCursor);
    _sound->setToolTip(tr("Sound (click to mute / unmute)"));
    connect(_sound, &StatusIndicator::clicked, this, &StatusBarManager::toggleSound);

    auto* separator = new QFrame(_statusBar);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedHeight(13);

    _fps = new QLabel(QStringLiteral("-- FPS"), _statusBar);
    _fps->setToolTip(tr("Emulated frames per second"));

    // Order as in the new-gui mockup: tape, square (HDD), round (floppy), sound
    _statusBar->addPermanentWidget(_tape);
    _statusBar->addPermanentWidget(_hdd);
    _statusBar->addPermanentWidget(_disk);
    _statusBar->addPermanentWidget(_sound);
    _statusBar->addPermanentWidget(separator);
    _statusBar->addPermanentWidget(_fps);

    connect(_menuManager, &MenuManager::statusBarToggled, this, &StatusBarManager::setVisibleByUser);

    // FDD state arrives by notification (posted by the FDC on change) - no polling
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod fddCallback = static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDStateChanged);
    messageCenter.AddObserver(NC_FDD_STATE_CHANGED, observerInstance, fddCallback);
    messageCenter.AddObserver(NC_FDD_DISK_INSERTED, observerInstance,
                              static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDDiskInserted));
    messageCenter.AddObserver(NC_FDD_DISK_EJECTED, observerInstance,
                              static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDDiskEjected));

    _pollTimer.setInterval(kPollIntervalMs);
    connect(&_pollTimer, &QTimer::timeout, this, &StatusBarManager::refresh);
    _pollTimer.start();

    updateDiskToolTip();
    refresh();
}

StatusBarManager::~StatusBarManager()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod fddCallback = static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDStateChanged);
    messageCenter.RemoveObserver(NC_FDD_STATE_CHANGED, observerInstance, fddCallback);
    messageCenter.RemoveObserver(NC_FDD_DISK_INSERTED, observerInstance,
                                 static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDDiskInserted));
    messageCenter.RemoveObserver(NC_FDD_DISK_EJECTED, observerInstance,
                                 static_cast<ObserverCallbackMethod>(&StatusBarManager::handleFDDDiskEjected));
}

void StatusBarManager::handleFDDDiskInserted(int id, Message* message)
{
    Q_UNUSED(id);
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    auto* payload = (emulator && message) ? dynamic_cast<FDDDiskPayload*>(message->obj) : nullptr;
    if (!payload || payload->_emulatorId.toString() != emulator->GetId())
        return;
    const uint8_t driveId = payload->_driveId;
    QMetaObject::invokeMethod(this, [this, driveId]() { applyDiskMediaChange(driveId, true); }, Qt::QueuedConnection);
}

void StatusBarManager::handleFDDDiskEjected(int id, Message* message)
{
    Q_UNUSED(id);
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    auto* payload = (emulator && message) ? dynamic_cast<FDDDiskPayload*>(message->obj) : nullptr;
    if (!payload || payload->_emulatorId.toString() != emulator->GetId())
        return;
    const uint8_t driveId = payload->_driveId;
    QMetaObject::invokeMethod(this, [this, driveId]() { applyDiskMediaChange(driveId, false); }, Qt::QueuedConnection);
}

void StatusBarManager::applyDiskMediaChange(uint8_t driveId, bool inserted)
{
    // Only the selected drive is shown; media changes on other drives will be picked up
    // by the next FDC state notification after they are selected
    if (_fddStateValid && driveId == _fddState.driveId)
    {
        _fddState.diskInserted = inserted;
        updateDiskToolTip();
    }
}

void StatusBarManager::handleFDDStateChanged(int id, Message* message)
{
    Q_UNUSED(id);

    // Emulator thread: filter by active emulator, copy the payload, marshal to the GUI thread
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator || !message || !message->obj)
        return;

    auto* payload = dynamic_cast<FDDStatePayload*>(message->obj);
    if (!payload || payload->_emulatorId.toString() != emulator->GetId())
        return;

    const FDDStateInfo state = payload->_state;
    QMetaObject::invokeMethod(this, [this, state]() { applyFddState(state); }, Qt::QueuedConnection);
}

void StatusBarManager::applyFddState(FDDStateInfo state)
{
    _fddState = state;
    _fddStateValid = true;
    _disk->setActive(state.motorOn);
    updateDiskToolTip();
}

void StatusBarManager::updateDiskToolTip()
{
    if (!_fddStateValid)
    {
        _disk->setLiveToolTip(tr("Floppy: no emulator"));
        return;
    }

    // Fixed layout: monospace, every field padded to a constant width so the tooltip
    // never changes size while values update under the cursor
    const QString drive = QString(QChar(_fddState.getDriveLetter()));
    QString disk = _fddState.diskInserted ? (_fddState.writeProtected ? tr("yes (RO)") : tr("yes")) : tr("no");
    const QString head = _fddState.diskInserted ? QString::number(_fddState.side) : QStringLiteral("-");
    const QString track = _fddState.diskInserted ? QString::number(_fddState.track) : QStringLiteral("-");
    const QString sector = _fddState.diskInserted ? QString::number(_fddState.sector) : QStringLiteral("-");
    const QString motor = _fddState.motorOn ? tr("on") : tr("off");
    const QString busy = _fddState.busy ? tr("yes") : tr("no");

    QStringList lines;
    lines << tr("Drive: %1   Disk: %2").arg(drive).arg(disk.leftJustified(8));
    lines << tr("Head: %1   Track: %2   Sector: %3")
                 .arg(head.leftJustified(1))
                 .arg(track.rightJustified(2))
                 .arg(sector.rightJustified(2));
    lines << tr("Motor: %1   Busy: %2").arg(motor.leftJustified(3)).arg(busy.leftJustified(3));

    int width = 0;
    for (const QString& line : lines)
        width = std::max(width, static_cast<int>(line.size()));
    for (QString& line : lines)
        line = line.leftJustified(width).toHtmlEscaped();

    _disk->setLiveToolTip(QStringLiteral("<pre style=\"margin:0\">%1</pre>").arg(lines.join(QLatin1Char('\n'))));
}

void StatusBarManager::setActiveEmulator(std::shared_ptr<Emulator> emulator)
{
    if (_emulator.lock() == emulator)
        return;

    _emulator = emulator;
    _frameCounter.store(0, std::memory_order_relaxed);
    _lastFrameCounter = 0;
    _haveFrameSample = false;
    _lastFpsSampleMs = QDateTime::currentMSecsSinceEpoch();
    _fps->setText(QStringLiteral("-- FPS"));

    // Rebind the floppy LED: drop the previous emulator's cache and read the new one's
    // state once; every later change arrives via NC_FDD_STATE_CHANGED
    _fddStateValid = false;
    _fddState = FDDStateInfo{};
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;

    // Machine model name for the FPS tooltip
    _modelName.clear();
    _measuredFps = 0.0;
    if (context)
    {
        if (EmulatorManager* manager = EmulatorManager::GetInstance())
        {
            for (const TMemModel& model : manager->GetAvailableModels())
            {
                if (model.Model == context->config.mem_model)
                {
                    _modelName = QString::fromUtf8(model.FullName);
                    break;
                }
            }
        }
    }
    if (context && context->pBetaDisk)
    {
        _fddState = context->pBetaDisk->getFDDState();
        _fddStateValid = true;
    }
    _disk->setActive(_fddStateValid && _fddState.motorOn);
    updateDiskToolTip();
    refresh();
}

void StatusBarManager::refresh()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;

    bool tapePlaying = false;
    bool soundOn = false;

    if (context)
    {
        if (context->pTape)
            tapePlaying = context->pTape->IsPlaying();

        if (context->pSoundManager)
            soundOn = !context->pSoundManager->isMuted();
    }

    _tape->setActive(tapePlaying);
    // Disk LED is driven by NC_FDD_STATE_CHANGED (see applyFddState); the tooltip is
    // re-rendered from the cache on every tick (200 ms) so an open tooltip stays current
    updateDiskToolTip();
    _hdd->setActive(false);  // HDD is a stub in the core
    _sound->setActive(soundOn);

    // FPS: average over the last ~1 s of rendered frames
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - _lastFpsSampleMs;
    if (elapsed >= 1000)
    {
        const uint32_t frames = _frameCounter.load(std::memory_order_relaxed);
        const uint32_t delta = _haveFrameSample ? frames - _lastFrameCounter : 0;
        _lastFrameCounter = frames;
        _haveFrameSample = true;
        _lastFpsSampleMs = now;

        if (emulator && emulator->IsRunning() && !emulator->IsPaused() && delta > 0)
        {
            _measuredFps = delta * 1000.0 / elapsed;
            _fps->setText(QStringLiteral("%1 FPS").arg(_measuredFps, 0, 'f', 1));
        }
        else
        {
            _measuredFps = 0.0;
            _fps->setText(QStringLiteral("-- FPS"));
        }
    }
    updateFpsToolTip(emulator);
}

void StatusBarManager::updateFpsToolTip(std::shared_ptr<Emulator> emulator)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    QString text;
    if (!context)
    {
        text = tr("Emulated frames per second\nNo emulator");
    }
    else
    {
        // Target rate follows the machine timing (t-states per frame at the base CPU clock)
        const unsigned frameUs = context->config.frame_duration_us;
        const double targetFps = frameUs > 0 ? 1000000.0 / frameUs : 0.0;
        const bool turbo = emulator->IsTurboMode();

        QStringList lines;
        lines << tr("Model: %1").arg(_modelName.isEmpty() ? tr("unknown") : _modelName);
        lines << tr("Target: %1 FPS").arg(targetFps, 0, 'f', 2);
        if (_measuredFps > 0.0)
        {
            QString measured = tr("Emulated: %1 FPS").arg(_measuredFps, 0, 'f', 1);
            if (turbo && targetFps > 0.0)
                measured += tr(" (turbo, x%1)").arg(_measuredFps / targetFps, 0, 'f', 1);
            lines << measured;
        }
        else
        {
            lines << (turbo ? tr("Emulated: measuring (turbo)") : tr("Emulated: --"));
        }
        text = lines.join(QLatin1Char('\n'));
    }

    if (_fps->toolTip() == text)
        return;
    _fps->setToolTip(text);
    if (QToolTip::isVisible() && _fps->underMouse())
        QToolTip::showText(QCursor::pos(), text, _fps);
}

void StatusBarManager::toggleSound()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pSoundManager)
        return;

    if (context->pSoundManager->isMuted())
        context->pSoundManager->unmute();
    else
        context->pSoundManager->mute();
    refresh();
}

void StatusBarManager::setVisibleByUser(bool visible)
{
    _visibleByUser = visible;
    _statusBar->setVisible(visible);
    _menuManager->setStatusBarChecked(visible);
}

void StatusBarManager::restoreSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    setVisibleByUser(settings.value(QLatin1String(kSettingsKey), true).toBool());
}

void StatusBarManager::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(QLatin1String(kSettingsKey), _visibleByUser);
}

void StatusBarManager::hideForFullScreen()
{
    _statusBar->hide();
}

void StatusBarManager::restoreVisibility()
{
    _statusBar->setVisible(_visibleByUser);
}
