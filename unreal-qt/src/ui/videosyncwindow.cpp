/**
 * @file videosyncwindow.cpp
 * @brief The video synchronization window: the window's present stats, live, in fixed cells.
 */

#include "videosyncwindow.h"

#include <algorithm>
#include <chrono>

#include <QCheckBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    constexpr int kRefreshMs = 500;
    constexpr int64_t kRateWindowNs = 1'000'000'000;
    const QString kNone = QStringLiteral("—");

    QString Hz(double hz)
    {
        return QString::number(hz, 'f', hz == static_cast<int>(hz) ? 0 : 2) + " Hz";
    }

    QString Ms(double ms)
    {
        return QString::number(ms, 'f', 2) + " ms";
    }

    int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /// A counter's rate per second since the last snapshot (0 after a reset: the counter went back)
    double PerSecond(uint64_t now, uint64_t before, double seconds)
    {
        return seconds > 0.0 && now >= before ? static_cast<double>(now - before) / seconds : 0.0;
    }

    QString Count(uint64_t n)
    {
        return QLocale().toString(static_cast<qulonglong>(n));
    }

    QString Rate(double perSecond)
    {
        return QString::number(perSecond, 'f', 1) + " /s";
    }

    QString ClockText(const std::string& clock)
    {
        if (clock == "audio")
            return QObject::tr("audio: when each frame's sound is heard");
        if (clock == "emulated")
            return QObject::tr("emulated: no audio device, the machine's own times");
        if (clock == "latch")
            return QObject::tr("latch: turbo, not paced to real time");
        return QObject::tr("none: the frames are not timed");
    }

    QGroupBox* Group(const QString& title, QWidget* parent, QGridLayout*& grid)
    {
        auto* box = new QGroupBox(title, parent);
        grid = new QGridLayout(box);
        grid->setHorizontalSpacing(12);
        grid->setColumnStretch(4, 1);
        return box;
    }

    void Caption(QGridLayout* grid, int row, const QString& text, int column = 0)
    {
        auto* label = new QLabel(text, grid->parentWidget());
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(label, row, column);
    }

    /// A column heading over number cells
    void Heading(QGridLayout* grid, int row, int column, const QString& text)
    {
        auto* label = new QLabel(text, grid->parentWidget());
        label->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        label->setEnabled(false);  // the palette's dimmed text
        grid->addWidget(label, row, column);
    }
}  // namespace

VideoSyncWindow::VideoSyncWindow(std::function<WindowDisplaySettings()> settings, QWidget* parent)
    : QWidget(parent), _settings(std::move(settings))
{
    setWindowTitle(tr("Video Synchronization"));
    _numberWidth = QFontMetrics(font()).horizontalAdvance(QStringLiteral("00000.00 ms"));
    auto* layout = new QVBoxLayout(this);

    _sync = new QCheckBox(tr("Synchronize to the display (VRR, else a fitting display rate, else interpolation)"), this);
    connect(_sync, &QCheckBox::toggled, this, &VideoSyncWindow::syncToggled);
    layout->addWidget(_sync);

    QGridLayout* grid = nullptr;

    // What runs and why
    layout->addWidget(Group(tr("In effect"), this, grid));
    Caption(grid, 0, tr("Mode"));
    cell(grid, "mode", 0, 1, Align::Text, 4);
    Caption(grid, 1, tr("Why"));
    QLabel* why = cell(grid, "why", 1, 1, Align::Text, 4);
    why->setWordWrap(true);
    why->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    why->setFixedHeight(QFontMetrics(font()).lineSpacing() * 4);  // four lines kept: the rows below stay put
    Caption(grid, 2, tr("Clock"));
    cell(grid, "clock", 2, 1, Align::Text, 4);

    // The display and the machine
    layout->addWidget(Group(tr("Display"), this, grid));
    Caption(grid, 0, tr("Refresh"));
    cell(grid, "refresh", 0, 1, Align::Text, 4);
    Caption(grid, 1, tr("Machine"));
    cell(grid, "machine", 1, 1, Align::Text, 4);
    Caption(grid, 2, tr("Display rate"));
    cell(grid, "display_rate", 2, 1, Align::Text, 4);
    Caption(grid, 3, tr("Presenter"));
    cell(grid, "presenter", 3, 1, Align::Text, 4);

    // How the presents land: counts in one column, percentiles in three
    layout->addWidget(Group(tr("Presents"), this, grid));
    Caption(grid, 0, tr("Shown"));
    cell(grid, "shown", 0, 1, Align::Number);
    Caption(grid, 1, tr("Dropped"));
    cell(grid, "dropped", 1, 1, Align::Number);
    Caption(grid, 2, tr("A refresh late"));
    cell(grid, "late", 2, 1, Align::Number);
    Caption(grid, 3, tr("Off the beat"));
    cell(grid, "off_beat", 3, 1, Align::Number);
    QLabel* beatShare = cell(grid, "off_beat_share", 3, 2, Align::Text, 3);
    beatShare->setEnabled(false);
    Caption(grid, 4, tr("Lead"));
    cell(grid, "lead", 4, 1, Align::Number);
    Heading(grid, 5, 1, tr("p50"));
    Heading(grid, 5, 2, tr("p99"));
    Heading(grid, 5, 3, tr("max"));
    Caption(grid, 6, tr("On screen vs asked"));
    cell(grid, "error_p50", 6, 1, Align::Number);
    cell(grid, "error_p99", 6, 2, Align::Number);
    cell(grid, "error_max", 6, 3, Align::Number);
    Caption(grid, 7, tr("Screen to sound"));
    cell(grid, "sound_p50", 7, 1, Align::Number);
    cell(grid, "sound_p99", 7, 2, Align::Number);

    // Smooth motion's work, per second
    layout->addWidget(Group(tr("Interpolation"), this, grid));
    Caption(grid, 0, tr("State"));
    cell(grid, "smooth", 0, 1, Align::Text, 4);
    Caption(grid, 1, tr("Frames in"));
    cell(grid, "frames", 1, 1, Align::Number);
    Caption(grid, 1, tr("Pictures drawn"), 2);
    cell(grid, "pictures", 1, 3, Align::Number);
    Caption(grid, 2, tr("Interpolated"));
    cell(grid, "interpolated", 2, 1, Align::Number);
    Caption(grid, 2, tr("Blended"), 2);
    cell(grid, "blended", 2, 3, Align::Number);
    Caption(grid, 3, tr("Motion fields"));
    cell(grid, "motion_fields", 3, 1, Align::Number);
    Caption(grid, 3, tr("Held as flicker"), 2);
    cell(grid, "held", 3, 3, Align::Number);
    layout->addStretch(1);

    // One caption column for all groups: the values start at the same place in each
    int captionWidth = 0;
    const QList<QGridLayout*> grids = findChildren<QGridLayout*>();
    for (QGridLayout* g : grids)
        for (int row = 0; row < g->rowCount(); row++)
            if (QLayoutItem* item = g->itemAtPosition(row, 0); item && item->widget())
                captionWidth = std::max(captionWidth, item->widget()->sizeHint().width());
    for (QGridLayout* g : grids)
        g->setColumnMinimumWidth(0, captionWidth);

    _timer = new QTimer(this);
    _timer->setInterval(kRefreshMs);
    connect(_timer, &QTimer::timeout, this, &VideoSyncWindow::refresh);
    resize(560, sizeHint().height());
}

QLabel* VideoSyncWindow::cell(QGridLayout* grid, const char* key, int row, int column, Align align, int span)
{
    auto* label = new QLabel(grid->parentWidget());
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (align == Align::Number)
    {
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setFixedWidth(_numberWidth);
    }
    else
    {
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumWidth(1);  // long text is cut by the cell, never widens the window
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    grid->addWidget(label, row, column, 1, span);
    _cells[key] = label;
    return label;
}

void VideoSyncWindow::set(const char* key, const QString& text)
{
    const auto it = _cells.find(key);
    if (it == _cells.end())
        return;
    if (it->second->text() != text)
        it->second->setText(text);
    // The whole text on hover where the cell cuts it
    if (it->second->alignment() & Qt::AlignLeft)
        it->second->setToolTip(text);
}

void VideoSyncWindow::clearValues()
{
    for (auto& [key, label] : _cells)
        label->setText(key == "mode" ? tr("— (no machine shown)") : QString());
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
    const auto it = _cells.find(name);
    return it != _cells.end() ? it->second->text() : QString();
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
        clearValues();
        _lastNs = 0;
        return;
    }

    // The mode in effect and why the steps before it are not
    const double displayHz = p.displayHz > 0.0 ? p.displayHz : p.maxRefreshHz;
    QString mode;
    if (p.sync == "vrr")
        mode = tr("VRR: each frame at its own time");
    else if (p.sync == "display_rate")
        mode = tr("Display rate: %1, each frame for %2 refresh(es)")
                   .arg(Hz(displayHz))
                   .arg(p.machineHz > 0.0 ? qRound(displayHz / p.machineHz) : 1);
    else if (p.sync == "interpolate")
        mode = tr("Interpolation: a picture per refresh on the fixed %1").arg(Hz(p.maxRefreshHz));
    else
        mode = tr("Off: the frame due on each refresh");
    set("mode", mode);
    if (p.sync == "off")
        set("why", tr("View > Video Synchronization is off: smooth motion %1, present policy %2, display rate %3")
                       .arg(QString::fromStdString(s.smoothMotion), QString::fromStdString(s.presentPolicy),
                            QString::fromStdString(s.displayRate)));
    else
        set("why", p.syncReason.empty() ? kNone : QString::fromStdString(p.syncReason));
    set("clock", ClockText(p.clock));

    // The display and the machine
    set("refresh", p.fixedDisplay           ? tr("fixed %1").arg(Hz(p.maxRefreshHz))
                   : p.minRefreshHz <= 0.0 ? tr("variable, up to %1").arg(Hz(p.maxRefreshHz))
                                           : tr("variable %1 - %2").arg(Hz(p.minRefreshHz), Hz(p.maxRefreshHz)));
    // The emulated machine's own frame rate (its frame length at its CPU clock: a Pentagon 48.83 Hz), not a measure
    set("machine", p.machineHz > 0.0 ? tr("%1: a frame of the emulated machine every %2 ms")
                                           .arg(Hz(p.machineHz))
                                           .arg(1000.0 / p.machineHz, 0, 'f', 2)
                                     : tr("not known yet (paused, turbo)"));
    if (p.displayState == "matched" && p.originalHz > 0.0)
        set("display_rate", tr("%1, switched from %2").arg(Hz(p.displayHz), Hz(p.originalHz)));
    else if (p.displayState == "unchanged")
        set("display_rate", tr("unchanged: %1").arg(QString::fromStdString(p.displayReason)));
    else
        set("display_rate", tr("the display's own"));
    const bool native = p.presenter == "metal" || p.presenter == "dxgi";
    set("presenter", QString::fromStdString(p.presenter) +
                         (native ? (p.vrr ? tr(" - each frame at its time") : tr(" - on the refresh grid"))
                                 : tr(" - the window's own swap, no screen times")));

    // Rates over the last second: the cells change twice a second, the rates once
    const int64_t now = NowNs();
    const bool rateDue = _lastNs == 0 || now - _lastNs >= kRateWindowNs - kRefreshMs * 500'000LL;
    const double seconds = _lastNs != 0 ? static_cast<double>(now - _lastNs) / 1e9 : 0.0;
    if (native)
    {
        if (rateDue)
            set("shown", _lastNs != 0 ? Rate(PerSecond(p.shown, _last.shown, seconds)) : kNone);
        set("dropped", Count(p.dropped));
        set("late", Count(p.late));
        set("off_beat", Count(p.offBeat));
        set("off_beat_share", p.beatFrames > 0 ? tr("of %1 frames (%2 %)")
                                                     .arg(Count(p.beatFrames))
                                                     .arg(100.0 * static_cast<double>(p.offBeat) /
                                                              static_cast<double>(p.beatFrames),
                                                          0, 'f', 2)
                                               : QString());
        set("lead", Ms(p.leadMs));
        set("error_p50", Ms(p.errorP50Ms));
        set("error_p99", Ms(p.errorP99Ms));
        set("error_max", Ms(p.errorMaxMs));
        set("sound_p50", Ms(p.soundOffsetP50Ms));
        set("sound_p99", Ms(p.soundOffsetP99Ms));
    }
    else
    {
        for (const char* key : {"shown", "dropped", "late", "off_beat", "lead", "error_p50", "error_p99", "error_max",
                                "sound_p50", "sound_p99"})
            set(key, kNone);
        set("off_beat_share", QString());
    }

    const bool smoothOn = p.smooth != "off";
    set("smooth", smoothOn ? tr("%1 on the %2%3")
                                 .arg(QString::fromStdString(p.smooth),
                                      QString::fromStdString(p.smoothDevice).toUpper(),
                                      p.game ? tr(", the game's cadence") : QString())
                           : tr("idle"));
    if (!smoothOn)
    {
        for (const char* key : {"frames", "pictures", "interpolated", "blended", "motion_fields", "held"})
            set(key, kNone);
    }
    else if (rateDue && _lastNs == 0)
    {
        for (const char* key : {"frames", "pictures", "interpolated", "blended", "motion_fields", "held"})
            set(key, kNone);
    }
    else if (rateDue)
    {
        set("frames", Rate(PerSecond(p.frames, _last.frames, seconds)));
        set("pictures", Rate(PerSecond(p.renders, _last.renders, seconds)));
        set("interpolated", Rate(PerSecond(p.interpolated, _last.interpolated, seconds)));
        set("blended", Rate(PerSecond(p.blended, _last.blended, seconds)));
        set("motion_fields", Rate(PerSecond(p.motionFields, _last.motionFields, seconds)));
        set("held", Rate(PerSecond(p.heldFlicker, _last.heldFlicker, seconds)));
    }
    if (rateDue)
    {
        _last = p;
        _lastNs = now;
    }
}
