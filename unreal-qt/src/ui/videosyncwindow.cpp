/**
 * @file videosyncwindow.cpp
 * @brief The video synchronization window: the window's present stats, live.
 */

#include "videosyncwindow.h"

#include <chrono>

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    QString Hz(double hz)
    {
        return QString::number(hz, 'f', hz == static_cast<int>(hz) ? 0 : 2) + " Hz";
    }

    int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /// A counter's rate per second since the last refresh (0 after a reset: the counter went back)
    double PerSecond(uint64_t now, uint64_t before, double seconds)
    {
        return seconds > 0.0 && now >= before ? static_cast<double>(now - before) / seconds : 0.0;
    }

    QLabel* Readout(QWidget* parent)
    {
        auto* label = new QLabel(parent);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    }
}  // namespace

VideoSyncWindow::VideoSyncWindow(std::function<WindowDisplaySettings()> settings, QWidget* parent)
    : QWidget(parent), _settings(std::move(settings))
{
    setWindowTitle(tr("Video Synchronization"));
    auto* layout = new QVBoxLayout(this);

    _sync = new QCheckBox(tr("Synchronize to the display (VRR, else a fitting display rate, else interpolation)"), this);
    connect(_sync, &QCheckBox::toggled, this, &VideoSyncWindow::syncToggled);
    layout->addWidget(_sync);

    auto* state = new QGroupBox(tr("In effect"), this);
    auto* stateForm = new QFormLayout(state);
    _mode = Readout(state);
    _why = Readout(state);
    stateForm->addRow(tr("Mode:"), _mode);
    stateForm->addRow(tr("Why:"), _why);
    layout->addWidget(state);

    auto* display = new QGroupBox(tr("Display"), this);
    auto* displayForm = new QFormLayout(display);
    _display = Readout(display);
    _timing = Readout(display);
    displayForm->addRow(tr("Rates:"), _display);
    displayForm->addRow(tr("Presents:"), _timing);
    layout->addWidget(display);

    auto* smooth = new QGroupBox(tr("Interpolation"), this);
    auto* smoothForm = new QFormLayout(smooth);
    _smooth = Readout(smooth);
    smoothForm->addRow(tr("Work:"), _smooth);
    layout->addWidget(smooth);
    layout->addStretch(1);

    _timer = new QTimer(this);
    _timer->setInterval(250);
    connect(_timer, &QTimer::timeout, this, &VideoSyncWindow::refresh);
    resize(520, 360);
}

void VideoSyncWindow::setSyncChecked(bool on)
{
    const QSignalBlocker blocker(_sync);
    _sync->setChecked(on);
}

void VideoSyncWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    _lastNs = 0;
    refresh();
    _timer->start();
}

void VideoSyncWindow::hideEvent(QHideEvent* event)
{
    _timer->stop();
    QWidget::hideEvent(event);
}

QString VideoSyncWindow::readout(const char* name) const
{
    const QString key = QString::fromLatin1(name);
    const QLabel* label = key == "mode"      ? _mode
                          : key == "why"     ? _why
                          : key == "display" ? _display
                          : key == "timing"  ? _timing
                          : key == "smooth"  ? _smooth
                                             : nullptr;
    return label ? label->text() : QString();
}

void VideoSyncWindow::refresh()
{
    if (!_settings)
        return;
    const WindowDisplaySettings s = _settings();
    const WindowPresentStats& p = s.present;
    setSyncChecked(p.sync != "off");

    if (s.emulatorId.empty())
    {
        _mode->setText(tr("— (no machine shown)"));
        _why->clear();
        _display->clear();
        _timing->clear();
        _smooth->clear();
        _lastNs = 0;
        return;
    }

    // The mode in effect and why the steps before it are not
    QString mode;
    if (p.sync == "vrr")
        mode = tr("VRR: each frame at its own time");
    else if (p.sync == "display_rate")
        mode = tr("Display rate: %1 for the machine's %2, each frame for %3 refresh(es)")
                   .arg(Hz(p.displayHz > 0.0 ? p.displayHz : p.maxRefreshHz))
                   .arg(Hz(p.machineHz))
                   .arg(p.machineHz > 0.0 ? qRound((p.displayHz > 0.0 ? p.displayHz : p.maxRefreshHz) / p.machineHz) : 1);
    else if (p.sync == "interpolate")
        mode = tr("Interpolation: a picture per refresh on the fixed %1").arg(Hz(p.maxRefreshHz));
    else
        mode = tr("Off: the frame due on each refresh (smooth motion %1, present policy %2, display rate %3)")
                   .arg(QString::fromStdString(s.smoothMotion), QString::fromStdString(s.presentPolicy),
                        QString::fromStdString(s.displayRate));
    _mode->setText(mode);
    _why->setText(QString::fromStdString(p.syncReason));

    // The display: its range, its rate against the machine's
    QString display = p.fixedDisplay ? tr("fixed %1").arg(Hz(p.maxRefreshHz))
                                     : tr("variable %1 - %2").arg(Hz(p.minRefreshHz), Hz(p.maxRefreshHz));
    if (p.displayState == "matched" && p.originalHz > 0.0)
        display += tr(", switched from %1").arg(Hz(p.originalHz));
    display += tr("; machine %1").arg(p.machineHz > 0.0 ? Hz(p.machineHz) : tr("not steady yet"));
    display += tr("; presenter %1").arg(QString::fromStdString(p.presenter));
    _display->setText(display);

    const int64_t now = NowNs();
    const double seconds = _lastNs != 0 ? static_cast<double>(now - _lastNs) / 1e9 : 0.0;
    if (p.presenter == "metal" || p.presenter == "dxgi")
        _timing->setText(tr("%1 shown/s, %2 dropped, %3 late; on screen vs asked p50 %4 ms, p99 %5 ms, max %6 ms; "
                            "lead %7 ms")
                             .arg(PerSecond(p.shown, _last.shown, seconds), 0, 'f', 1)
                             .arg(p.dropped)
                             .arg(p.late)
                             .arg(p.errorP50Ms, 0, 'f', 2)
                             .arg(p.errorP99Ms, 0, 'f', 2)
                             .arg(p.errorMaxMs, 0, 'f', 2)
                             .arg(p.leadMs, 0, 'f', 1));
    else
        _timing->setText(tr("the window's own swap (no present-at-time timing)"));

    if (p.smooth == "off")
        _smooth->setText(tr("idle"));
    else
    {
        const double renders = PerSecond(p.renders, _last.renders, seconds);
        const double interpolated = PerSecond(p.interpolated, _last.interpolated, seconds);
        const double frames = PerSecond(p.frames, _last.frames, seconds);
        QString text = tr("%1 on the %2%3: %4 frames/s in, %5 pictures/s drawn (%6 interpolated, %7 blended); "
                          "%8 motion fields/s; %9 held as flicker/s")
                           .arg(QString::fromStdString(p.smooth), QString::fromStdString(p.smoothDevice).toUpper(),
                                p.game ? tr(" with the game's cadence") : QString())
                           .arg(frames, 0, 'f', 1)
                           .arg(renders, 0, 'f', 1)
                           .arg(interpolated, 0, 'f', 1)
                           .arg(PerSecond(p.blended, _last.blended, seconds), 0, 'f', 1)
                           .arg(PerSecond(p.motionFields, _last.motionFields, seconds), 0, 'f', 1)
                           .arg(PerSecond(p.heldFlicker, _last.heldFlicker, seconds), 0, 'f', 1);
        _smooth->setText(text);
    }
    _last = p;
    _lastNs = now;
}
