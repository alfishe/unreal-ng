#include "toolbarmanager.h"

#include <QMainWindow>
#include <QPainter>
#include <QRadialGradient>
#include <QSettings>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolTip>
#include <chrono>
#include <cmath>

#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "mainwindow.h"
#include "menumanager.h"
#include "recording/src/recordingmanager.h"
#include "widgets/tintedsvgicon.h"

namespace
{
constexpr const char* kSettingsKey = "View/ToolBarVisible";

QString formatMemorySize(uint64_t bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0)
    {
        return QString::asprintf("%.1f MB", mb);
    }
    if (bytes >= 1024)
    {
        return QString::asprintf("%.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    return QString::asprintf("%llu B", static_cast<unsigned long long>(bytes));
}
}

ToolBarManager::ToolBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent)
    : QObject(parent), _mainWindow(mainWindow), _menuManager(menuManager)
{
    _toolBar = _mainWindow->addToolBar(tr("Transport"));
    _toolBar->setObjectName(QStringLiteral("transportToolBar"));
    _toolBar->setMovable(false);
    _toolBar->setFloatable(false);
    _toolBar->setIconSize(QSize(16, 16));
    _toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // ---- Transport -------------------------------------------------------
    _startAction = new QAction(tintedSvgIcon(QStringLiteral("start")), tr("Start"), this);
    _startAction->setCheckable(true);
    _startAction->setToolTip(tr("Start / resume emulator"));
    connect(_startAction, &QAction::triggered, this, &ToolBarManager::startOrResumeRequested);

    _pauseAction = new QAction(tintedSvgIcon(QStringLiteral("pause")), tr("Pause"), this);
    _pauseAction->setCheckable(true);
    _pauseAction->setToolTip(tr("Pause emulator"));
    connect(_pauseAction, &QAction::triggered, this, &ToolBarManager::pauseRequested);

    _restartAction = new QAction(tintedSvgIcon(QStringLiteral("restart")), tr("Restart"), this);
    _restartAction->setToolTip(tr("Reset machine (starts the emulator if it is not running)"));
    connect(_restartAction, &QAction::triggered, this, &ToolBarManager::restartRequested);

    // ---- View ------------------------------------------------------------
    // Pentagon overscan on/off (384x304 vs. the standard symmetric 352x288). The menu
    // action is hidden by updateMenuStates() for non-Pentagon models, which hides
    // this button as well.
    QAction* overscan = _menuManager->overscanAction();
    overscan->setIcon(tintedSvgIcon(QStringLiteral("videomode")));
    overscan->setIconVisibleInMenu(false);
    overscan->setToolTip(tr("Pentagon overscan mode (384x304)"));

    QAction* fullScreen = _menuManager->fullScreenAction();
    fullScreen->setIcon(tintedSvgIcon(QStringLiteral("fullscreen")));
    fullScreen->setIconVisibleInMenu(false);

    _toolBar->addAction(_startAction);
    _toolBar->addAction(_pauseAction);
    _toolBar->addAction(_restartAction);
    _toolBar->addSeparator();
    _toolBar->addAction(overscan);
    _toolBar->addAction(fullScreen);

#ifdef ENABLE_RECORDING
    _recordAction = _menuManager->videoRecordingAction();
    _normalRecordIcon = tintedSvgIcon(QStringLiteral("record"), /*tint=*/false);
    _recordAction->setIcon(_normalRecordIcon);
    _recordAction->setIconVisibleInMenu(false);
    _recordAction->setCheckable(true);
    _recordAction->setToolTip(tr("Toggle Recording Control Panel"));
    connect(_recordAction, &QAction::toggled, this, &ToolBarManager::recordingToggled);
    _toolBar->addSeparator();
    _toolBar->addAction(_recordAction);
#endif

    // ---- TTD (Time Travel Debugging) -------------------------------------
    _normalTtdIcon = tintedSvgIcon(QStringLiteral("timetravel"));
    _ttdAction = new QAction(_normalTtdIcon, tr("Time Travel Debugging (TTD)"), this);
    _ttdAction->setCheckable(true);
    _ttdAction->setToolTip(tr("Toggle Time Travel Debugging (TTD) Control Panel"));
    connect(_ttdAction, &QAction::toggled, this, &ToolBarManager::ttdToggled);
#ifndef ENABLE_RECORDING
    _toolBar->addSeparator();
#endif
    _toolBar->addAction(_ttdAction);

    // Breathing LED timer for active recording feedback (Apple-style breathing LED)
    _breathingTimer = new QTimer(this);
    _breathingTimer->setInterval(33); // ~30 FPS
    connect(_breathingTimer, &QTimer::timeout, this, &ToolBarManager::onBreathingTick);

    // View -> Toolbar
    connect(_menuManager, &MenuManager::toolBarToggled, this, &ToolBarManager::setVisibleByUser);

    updateState(nullptr);
}

void ToolBarManager::updateState(std::shared_ptr<Emulator> activeEmulator)
{
    _activeEmulator = activeEmulator;

    const bool exists = (activeEmulator != nullptr);
    const bool running = exists && activeEmulator->IsRunning();
    const bool paused = exists && activeEmulator->IsPaused();
    const bool active = running && !paused;

    _startAction->setChecked(active);
    _startAction->setEnabled(!active);
    _pauseAction->setChecked(paused);
    _pauseAction->setEnabled(active);
    _restartAction->setEnabled(true);

    updateRecordingStates();
}

std::shared_ptr<Emulator> ToolBarManager::getActiveEmulator() const
{
    if (auto emu = _activeEmulator.lock())
        return emu;
    if (_mainWindow)
    {
        if (auto emu = _mainWindow->activeEmulator())
            return emu;
    }
    if (auto* mgr = EmulatorManager::GetInstance())
    {
        const auto ids = mgr->GetEmulatorIds();
        if (!ids.empty())
            return mgr->GetEmulator(ids[0]);
    }
    return nullptr;
}

bool ToolBarManager::isVideoRecording() const
{
#ifdef ENABLE_RECORDING
    auto emu = getActiveEmulator();
    if (!emu)
        return false;
    EmulatorContext* context = emu->GetContext();
    if (!context || !context->pRecordingManager)
        return false;
    return context->pRecordingManager->IsRecording() || context->pRecordingManager->IsPaused();
#else
    return false;
#endif
}

bool ToolBarManager::isTtdRecording() const
{
    auto emu = getActiveEmulator();
    if (!emu)
        return false;
    EmulatorContext* context = emu->GetContext();
    if (!context || !context->pTimeTravelManager)
        return false;
    return context->pTimeTravelManager->IsRecording();
}

void ToolBarManager::setVideoRecordingActive(bool active)
{
    if (_videoRecordingActive == active)
        return;

    _videoRecordingActive = active;
    if (_videoRecordingActive)
    {
        updateActiveTooltips();
        startBreathingAnimationIfNeeded();
    }
    else
    {
#ifdef ENABLE_RECORDING
        if (_menuManager && _menuManager->videoRecordingAction())
        {
            _menuManager->videoRecordingAction()->setIcon(_normalRecordIcon);
        }
        if (_recordAction)
        {
            _recordAction->setToolTip(tr("Toggle Recording Control Panel"));
        }
#endif
        stopBreathingAnimationIfIdle();
    }
}

void ToolBarManager::setTtdRecordingActive(bool active)
{
    if (_ttdRecordingActive == active)
        return;

    _ttdRecordingActive = active;
    if (_ttdRecordingActive)
    {
        updateActiveTooltips();
        startBreathingAnimationIfNeeded();
    }
    else
    {
        if (_ttdAction)
        {
            _ttdAction->setIcon(_normalTtdIcon);
            _ttdAction->setToolTip(tr("Time Travel Debugging (Timeline Scrubber)"));
        }
        stopBreathingAnimationIfIdle();
    }
}

void ToolBarManager::updateRecordingStates()
{
    setVideoRecordingActive(isVideoRecording());
    setTtdRecordingActive(isTtdRecording());
}

void ToolBarManager::startBreathingAnimationIfNeeded()
{
    if (_breathingTimer && !_breathingTimer->isActive())
    {
        _breathingPhase = 0.0;
        _tickCount = 0;
        _lastTooltipUpdateTime = std::chrono::steady_clock::now();
        _breathingTimer->start();
    }
}

void ToolBarManager::stopBreathingAnimationIfIdle()
{
    if (!_videoRecordingActive && !_ttdRecordingActive)
    {
        if (_breathingTimer && _breathingTimer->isActive())
        {
            _breathingTimer->stop();
        }
#ifdef ENABLE_RECORDING
        if (_menuManager && _menuManager->videoRecordingAction())
        {
            _menuManager->videoRecordingAction()->setIcon(_normalRecordIcon);
        }
        if (_recordAction)
        {
            _recordAction->setToolTip(tr("Toggle Recording Control Panel"));
        }
#endif
        if (_ttdAction)
        {
            _ttdAction->setIcon(_normalTtdIcon);
            _ttdAction->setToolTip(tr("Time Travel Debugging (Timeline Scrubber)"));
        }
    }
}

void ToolBarManager::onBreathingTick()
{
    // Re-verify actual emulator recording states every ~1 second (30 ticks)
    _tickCount++;
    if (_tickCount >= 30)
    {
        _tickCount = 0;
        if (auto emu = getActiveEmulator())
        {
            const bool actualVideo = isVideoRecording();
            const bool actualTtd = isTtdRecording();
            if (_videoRecordingActive != actualVideo)
                setVideoRecordingActive(actualVideo);
            if (_ttdRecordingActive != actualTtd)
                setTtdRecordingActive(actualTtd);
        }

        if (!_videoRecordingActive && !_ttdRecordingActive)
            return;
    }

    // Period: 2.6 seconds at 33ms interval -> ~79 frames per cycle
    constexpr qreal kPeriodSec = 2.6;
    constexpr qreal kIntervalSec = 0.033;
    _breathingPhase += (2.0 * M_PI * kIntervalSec / kPeriodSec);
    if (_breathingPhase >= 2.0 * M_PI)
    {
        _breathingPhase -= 2.0 * M_PI;
    }

    // Apple Breathing LED curve: f(t) in [0.0, 1.0]
    const qreal sinVal = std::sin(_breathingPhase - M_PI / 2.0);
    const qreal f = (std::exp(sinVal) - std::exp(-1.0)) / (std::exp(1.0) - std::exp(-1.0));
    const qreal intensity = 0.20 + 0.80 * f;
    const qreal bloom = f;

#ifdef ENABLE_RECORDING
    if (_videoRecordingActive && _menuManager && _menuManager->videoRecordingAction())
    {
        _menuManager->videoRecordingAction()->setIcon(createBreathingRecordIcon(intensity, bloom));
    }
#endif

    if (_ttdRecordingActive && _ttdAction)
    {
        _ttdAction->setIcon(createBreathingTtdIcon(intensity, bloom));
    }

    if (_toolBar)
    {
        if (auto* w = _toolBar->widgetForAction(_ttdAction))
        {
            w->update();
        }
#ifdef ENABLE_RECORDING
        if (_recordAction)
        {
            if (auto* rw = _toolBar->widgetForAction(_recordAction))
            {
                rw->update();
            }
        }
#endif
    }

    // Refresh active tooltips no faster than once every 200ms
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastTooltipUpdateTime).count();
    if (elapsedMs >= 200)
    {
        _lastTooltipUpdateTime = now;
        updateActiveTooltips();
    }
}

void ToolBarManager::updateActiveTooltips()
{
    auto emu = getActiveEmulator();
    if (!emu)
        return;
    EmulatorContext* context = emu->GetContext();
    if (!context)
        return;

    // 1. TTD Tooltip (Capturing, timecode, frame range, memory used)
    if (_ttdRecordingActive && _ttdAction && context->pTimeTravelManager)
    {
        auto* ttd = context->pTimeTravelManager;
        ttd::TTDSessionInfo info = ttd->GetSessionInfo();
        const uint64_t startFrame = info.sessionStartFrame;
        const uint64_t curFrame = info.currentEndFrame;
        const uint64_t totalFrames = (curFrame >= startFrame) ? (curFrame - startFrame) : 0;
        const int totalSec = static_cast<int>(totalFrames / 50); // 50 FPS PAL/Spectrum standard
        const int hh = totalSec / 3600;
        const int mm = (totalSec % 3600) / 60;
        const int ss = totalSec % 60;
        const QString timeStr = QString::asprintf("%02d:%02d:%02d", hh, mm, ss);

        const QString memStr = formatMemorySize(info.sessionHeapBytes);

        const QString tip = tr("Capturing\nTime: %1 | Frames: %2 - %3\nMemory: %4")
                                .arg(timeStr)
                                .arg(startFrame)
                                .arg(curFrame)
                                .arg(memStr);

        _ttdAction->setToolTip(tip);

        if (_toolBar)
        {
            if (auto* w = _toolBar->widgetForAction(_ttdAction))
            {
                if (QToolTip::isVisible() && w->underMouse())
                {
                    QToolTip::showText(QCursor::pos(), tip, w);
                }
            }
        }
    }

#ifdef ENABLE_RECORDING
    // 2. Video / Audio Recording Tooltip (Recording, timecode, frame count, memory used)
    if (_videoRecordingActive && _recordAction && context->pRecordingManager)
    {
        auto* rm = context->pRecordingManager;
        auto stats = rm->GetStats();
        const int totalSec = static_cast<int>(stats.recordedDuration);
        const int hh = totalSec / 3600;
        const int mm = (totalSec % 3600) / 60;
        const int ss = totalSec % 60;
        const QString timeStr = QString::asprintf("%02d:%02d:%02d", hh, mm, ss);

        const QString memStr = formatMemorySize(stats.outputFileSize);
        const QString stateStr = rm->IsPaused() ? tr("Recording (Paused)") : tr("Recording");

        const QString tip = tr("%1\nTime: %2 | Frames: %3\nMemory: %4")
                                .arg(stateStr)
                                .arg(timeStr)
                                .arg(stats.framesRecorded)
                                .arg(memStr);

        _recordAction->setToolTip(tip);

        if (_toolBar)
        {
            if (auto* w = _toolBar->widgetForAction(_recordAction))
            {
                if (QToolTip::isVisible() && w->underMouse())
                {
                    QToolTip::showText(QCursor::pos(), tip, w);
                }
            }
        }
    }
#endif
}

QIcon ToolBarManager::createBreathingRecordIcon(qreal intensity, qreal bloom)
{
    QIcon icon;
    for (qreal scale : {1.0, 2.0})
    {
        const int px = static_cast<int>(16 * scale);
        QPixmap pm(px, px);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Radiant bloom halo around LED
        if (bloom > 0.02)
        {
            QRadialGradient glow(QPointF(8.0, 8.0), 7.5);
            glow.setColorAt(0.0, QColor(255, 59, 48, static_cast<int>(180 * bloom)));
            glow.setColorAt(0.5, QColor(255, 59, 48, static_cast<int>(80 * bloom)));
            glow.setColorAt(1.0, QColor(255, 59, 48, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(QPointF(8.0, 8.0), 7.5, 7.5);
        }

        // Core LED red dot
        const int coreAlpha = static_cast<int>(120 + 135 * intensity);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(235, 45, 35, coreAlpha));
        p.drawEllipse(QPointF(8.0, 8.0), 4.8, 4.8);

        p.end();
        icon.addPixmap(pm, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pm, QIcon::Normal, QIcon::On);
    }
    return icon;
}

QIcon ToolBarManager::createBreathingTtdIcon(qreal intensity, qreal bloom)
{
    QIcon icon;
    for (qreal scale : {1.0, 2.0})
    {
        const int px = static_cast<int>(16 * scale);
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);

            // Ambient bloom behind glyph
            if (bloom > 0.02)
            {
                QRadialGradient glow(QPointF(px / 2.0, px / 2.0), px * 0.48);
                glow.setColorAt(0.0, QColor(255, 59, 48, static_cast<int>(180 * bloom)));
                glow.setColorAt(0.5, QColor(255, 59, 48, static_cast<int>(80 * bloom)));
                glow.setColorAt(1.0, QColor(255, 59, 48, 0));
                p.setPen(Qt::NoPen);
                p.setBrush(glow);
                p.drawEllipse(QPointF(px / 2.0, px / 2.0), px * 0.48, px * 0.48);
            }

            // Render and tint timetravel SVG
            QImage glyph(px, px, QImage::Format_ARGB32_Premultiplied);
            glyph.fill(Qt::transparent);
            {
                QPainter gp(&glyph);
                gp.setRenderHint(QPainter::Antialiasing, true);
                static QSvgRenderer ttdRenderer(QStringLiteral(":/icons/timetravel.svg"));
                ttdRenderer.render(&gp, glyph.rect());
                gp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                const int glyphAlpha = static_cast<int>(120 + 135 * intensity);
                gp.fillRect(glyph.rect(), QColor(255, 59, 48, glyphAlpha));
            }

            p.drawImage(0, 0, glyph);
        }

        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(scale);
        icon.addPixmap(pm, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pm, QIcon::Normal, QIcon::On);
    }
    return icon;
}

void ToolBarManager::setVisibleByUser(bool visible)
{
    _visibleByUser = visible;
    _toolBar->setVisible(visible);
    _menuManager->setToolBarChecked(visible);
}

void ToolBarManager::restoreSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    setVisibleByUser(settings.value(QLatin1String(kSettingsKey), true).toBool());
}

void ToolBarManager::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(QLatin1String(kSettingsKey), _visibleByUser);
}

void ToolBarManager::hideForFullScreen()
{
    _toolBar->hide();
}

void ToolBarManager::restoreVisibility()
{
    _toolBar->setVisible(_visibleByUser);
    if (_visibleByUser && _toolBar)
    {
        _toolBar->show();
        _toolBar->raise();
        _toolBar->update();
    }
}
