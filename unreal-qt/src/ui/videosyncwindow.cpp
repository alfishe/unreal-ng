/**
 * @file videosyncwindow.cpp
 * @brief The video synchronization window: the window's present stats, live, as a head card over tiles.
 */

#include "videosyncwindow.h"

#include <algorithm>
#include <chrono>

#include <QCheckBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    constexpr int kRefreshMs = 500;
    constexpr int64_t kRateWindowNs = 1'000'000'000;
    constexpr int kTileWidth = 280;
    constexpr int kGap = 10;
    constexpr int kGraphPoints = 60;
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

    QString ClockName(const std::string& clock)
    {
        if (clock == "audio")
            return QObject::tr("audio clock");
        if (clock == "emulated")
            return QObject::tr("emulated clock");
        if (clock == "latch")
            return QObject::tr("latch clock");
        return QObject::tr("no clock");
    }

    QString ClockText(const std::string& clock, bool lowLatency)
    {
        if (clock == "latch" && lowLatency)
            return QObject::tr("latch: low latency, each frame due when it is made (not held for its sound)");
        if (clock == "audio")
            return QObject::tr("audio: when each frame's sound is heard");
        if (clock == "emulated")
            return QObject::tr("emulated: no audio device, the machine's own times");
        if (clock == "latch")
            return QObject::tr("latch: turbo or stepping, not paced to real time");
        return QObject::tr("none: the frames are not timed");
    }

    QColor Mix(const QColor& a, const QColor& b, double t)
    {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    }

    /// A one-line label that cuts a long text with "..." (the whole text on hover); its width never follows the text
    class ElideLabel : public QLabel
    {
    public:
        using QLabel::QLabel;
        QSize minimumSizeHint() const override { return {1, QLabel::minimumSizeHint().height()}; }
        QSize sizeHint() const override { return {1, QLabel::sizeHint().height()}; }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.setPen(palette().color(foregroundRole()));
            const QRect area = contentsRect();
            p.drawText(area, static_cast<int>(alignment()) | Qt::TextSingleLine,
                       fontMetrics().elidedText(text(), Qt::ElideRight, area.width()));
        }
    };

    void SetColor(QWidget* widget, const QColor& color)
    {
        QPalette palette = widget->palette();
        palette.setColor(QPalette::WindowText, color);
        widget->setPalette(palette);
    }
}  // namespace

/// The last values of a rate as a line, the newest at the right: a steady rate is flat, a stutter a notch
class VideoSyncSparkline : public QWidget
{
public:
    explicit VideoSyncSparkline(QWidget* parent) : QWidget(parent) { setFixedSize(96, 30); }

    void Add(double value)
    {
        _values.push_back(value);
        if (_values.size() > kGraphPoints)
            _values.erase(_values.begin());
        update();
    }

    void Clear()
    {
        _values.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (_values.size() < 2)
            return;
        const double top = std::max(1.0, *std::max_element(_values.begin(), _values.end()));
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const double step = static_cast<double>(width() - 2) / (kGraphPoints - 1);
        const double x0 = width() - 1 - step * static_cast<double>(_values.size() - 1);
        for (size_t i = 0; i < _values.size(); i++)
        {
            const QPointF point(x0 + step * static_cast<double>(i), 1 + (height() - 2) * (1.0 - _values[i] / top));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
        p.drawPath(path);
    }

private:
    std::vector<double> _values;
};

VideoSyncWindow::VideoSyncWindow(std::function<WindowDisplaySettings()> settings, QWidget* parent)
    : QWidget(parent), _settings(std::move(settings))
{
    setWindowTitle(tr("Video Synchronization"));
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(kGap);
    layout->setSizeConstraint(QLayout::SetFixedSize);  // the window is as large as the tiles of the mode in effect

    _sync = new QCheckBox(tr("Synchronize to the display (VRR, else a fitting display rate, else interpolation)"), this);
    connect(_sync, &QCheckBox::toggled, this, &VideoSyncWindow::syncToggled);
    layout->addWidget(_sync);

    // The head card: the mode in effect and the state, why, the clock and the presenter
    _head = new QFrame(this);
    _head->setObjectName(QStringLiteral("syncHead"));
    _head->setFixedWidth(2 * kTileWidth + kGap);
    auto* head = new QVBoxLayout(_head);
    head->setContentsMargins(16, 12, 16, 12);
    head->setSpacing(8);
    auto* top = new QHBoxLayout();
    auto* mode = new ElideLabel(_head);
    QFont title = font();
    title.setPointSizeF(title.pointSizeF() * 1.3);
    title.setBold(true);
    mode->setFont(title);
    mode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _cells["mode"] = mode;
    top->addWidget(mode, 1);
    _state = new QLabel(_head);
    _state->setAlignment(Qt::AlignCenter);
    _cells["state"] = _state;
    top->addWidget(_state);
    head->addLayout(top);
    auto* why = new QLabel(_head);
    why->setWordWrap(true);
    why->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    why->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _cells["why"] = why;
    head->addWidget(why);
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    for (const char* key : {"clock", "presenter", "smooth", "low_latency"})
        chips->addWidget(chip(key));
    chips->addStretch(1);
    head->addLayout(chips);
    layout->addWidget(_head);

    auto* tiles = new QWidget(this);
    _tileGrid = new QGridLayout(tiles);
    _tileGrid->setContentsMargins(0, 0, 0, 0);
    _tileGrid->setSpacing(kGap);
    layout->addWidget(tiles);

    addTile("display", tr("Display"), "refresh", {{tr("Machine"), "machine"}, {tr("Display rate"), "display_rate"}});
    addTile("frames", tr("Frames on screen"), "shown",
            {{tr("Dropped"), "dropped"}, {tr("A refresh late"), "late"}, {tr("Off the beat"), "off_beat"}}, true);
    _errorCaption = addTile("timing", tr("On screen vs asked"), "error_p50",
                            {{tr("p99"), "error_p99"}, {tr("Max"), "error_max"}, {tr("Lead"), "lead"}})
                        .caption;
    addTile("sound", tr("Screen to sound"), "sound_p50",
            {{tr("p99"), "sound_p99"}, {tr("Clock breaks"), "cuts"}, {tr("Clock carry"), "carry"}});
    addTile("smooth", tr("Interpolation"), "pictures",
            {{tr("Frames in"), "frames"}, {tr("Interpolated"), "interpolated"}, {tr("Blended"), "blended"}});
    addTile("motion", tr("Motion fields"), "motion_fields", {{tr("Held as flicker"), "held"}});
    showTiles({"display"});
    applyColors();

    _timer = new QTimer(this);
    _timer->setInterval(kRefreshMs);
    connect(_timer, &QTimer::timeout, this, &VideoSyncWindow::refresh);
}

QLabel* VideoSyncWindow::chip(const char* key)
{
    auto* label = new QLabel(_head);
    label->setAlignment(Qt::AlignCenter);
    _cells[key] = label;
    return label;
}

VideoSyncWindow::Tile& VideoSyncWindow::addTile(const char* id, const QString& caption, const char* bigKey,
                                                const std::vector<std::pair<QString, const char*>>& lines,
                                                bool sparkline)
{
    Tile tile;
    tile.id = id;
    tile.frame = new QFrame(_tileGrid->parentWidget());
    tile.frame->setObjectName(QStringLiteral("syncTile"));
    tile.frame->setFixedWidth(kTileWidth);
    auto* box = new QVBoxLayout(tile.frame);
    box->setContentsMargins(16, 12, 16, 12);
    box->setSpacing(4);

    tile.caption = new ElideLabel(tile.frame);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    small.setBold(true);
    small.setCapitalization(QFont::AllUppercase);
    tile.caption->setFont(small);
    tile.caption->setText(caption);
    box->addWidget(tile.caption);

    auto* bigRow = new QHBoxLayout();
    auto* big = new ElideLabel(tile.frame);
    QFont large = font();
    large.setPointSizeF(large.pointSizeF() * 1.7);
    big->setFont(large);
    big->setTextInteractionFlags(Qt::TextSelectableByMouse);
    big->setFixedHeight(QFontMetrics(large).lineSpacing());
    _cells[bigKey] = big;
    bigRow->addWidget(big, 1);
    if (sparkline)
    {
        _shownGraph = new VideoSyncSparkline(tile.frame);
        bigRow->addWidget(_shownGraph);
    }
    box->addLayout(bigRow);

    // Three lines in every tile: the tiles of a row are as tall as each other, a line not shown leaves its place
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(0, 1);
    const QFontMetrics metrics(font());
    const int valueWidth = std::max(metrics.horizontalAdvance(QStringLiteral("0,000,000 (00.00 %)")),
                                    metrics.horizontalAdvance(QStringLiteral("000.00 Hz · 00.00 ms"))) + 4;
    const int lineHeight = metrics.lineSpacing();
    for (int row = 0; row < 3; row++)
    {
        grid->setRowMinimumHeight(row, lineHeight);
        if (static_cast<size_t>(row) >= lines.size())
            continue;
        auto* name = new ElideLabel(tile.frame);
        name->setText(lines[row].first);
        auto* value = new ElideLabel(tile.frame);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setFixedWidth(valueWidth);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(name, row, 0);
        grid->addWidget(value, row, 1);
        _cells[lines[row].second] = value;
        _lineCaptions[lines[row].second] = name;
    }
    box->addLayout(grid);
    box->addStretch(1);  // a taller neighbour in the row: the content stays at the top
    tile.frame->hide();
    _tiles.push_back(tile);
    return _tiles.back();
}

void VideoSyncWindow::applyColors()
{
    // The palette's own colours, so a light and a dark theme both work: a card a step off the window, dimmed captions
    const QColor window = palette().color(QPalette::Window);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor cardColor = Mix(window, text, 0.07);
    const QColor chipColor = Mix(window, text, 0.16);
    const QColor dim = Mix(text, window, 0.42);
    const QString card = QStringLiteral("QFrame#%1 { background: %2; border-radius: 8px; }");
    _head->setStyleSheet(card.arg(QStringLiteral("syncHead"), cardColor.name()));
    for (const Tile& tile : _tiles)
    {
        tile.frame->setStyleSheet(card.arg(QStringLiteral("syncTile"), cardColor.name()));
        SetColor(tile.caption, dim);
    }
    for (const auto& [key, label] : _lineCaptions)
        SetColor(label, dim);
    SetColor(_cells["why"], dim);
    // Round ends: the radius is half the label's height (a larger one is not drawn at all)
    const int line = QFontMetrics(font()).height();
    const QString chipSheet = QStringLiteral("background: %1; color: %2; padding: 2px 10px; border-radius: ") +
                              QString::number((line + 4) / 2) + QStringLiteral("px;");
    for (const char* key : {"clock", "presenter", "smooth", "low_latency"})
        _cells[key]->setStyleSheet(chipSheet.arg(chipColor.name(), text.name()));
    const QString pill = QStringLiteral("background: %1; color: %2; padding: 3px 14px; font-weight: bold; border-radius: ") +
                         QString::number((line + 6) / 2) + QStringLiteral("px;");
    if (_uneven)
        _state->setStyleSheet(pill.arg(QStringLiteral("#c77700"), QStringLiteral("#ffffff")));
    else if (_state->text() == tr("Steady"))
        _state->setStyleSheet(pill.arg(palette().color(QPalette::Highlight).name(),
                                       palette().color(QPalette::HighlightedText).name()));
    else
        _state->setStyleSheet(pill.arg(chipColor.name(), text.name()));
}

void VideoSyncWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && _head != nullptr)
        applyColors();
}

void VideoSyncWindow::set(const char* key, const QString& text, const QString& tip)
{
    const auto it = _cells.find(key);
    if (it == _cells.end())
        return;
    if (it->second->text() != text)
        it->second->setText(text);
    // The whole text on hover where the cell cuts it; `tip`: the long form of a short value
    const QString hover = tip.isEmpty() ? text : tip;
    if (it->second->toolTip() != hover)
        it->second->setToolTip(hover);
}

void VideoSyncWindow::showLine(const char* key, bool shown)
{
    const auto caption = _lineCaptions.find(key);
    const auto value = _cells.find(key);
    if (caption == _lineCaptions.end() || value == _cells.end())
        return;
    caption->second->setVisible(shown);
    value->second->setVisible(shown);
    if (!shown)
        set(key, kNone);
}

void VideoSyncWindow::showTiles(const std::vector<const char*>& ids)
{
    std::string joined;
    for (const char* id : ids)
        joined += std::string(id) + ",";
    if (joined == _tilesShown)
        return;
    _tilesShown = joined;
    for (const Tile& tile : _tiles)
    {
        _tileGrid->removeWidget(tile.frame);
        tile.frame->hide();
    }
    int place = 0;
    for (const char* id : ids)
        for (const Tile& tile : _tiles)
            if (tile.id == id)
            {
                _tileGrid->addWidget(tile.frame, place / 2, place % 2);
                tile.frame->show();
                place++;
            }
}

bool VideoSyncWindow::tileShown(const char* id) const
{
    return _tilesShown.find(std::string(id) + ",") != std::string::npos;
}

bool VideoSyncWindow::lineShown(const char* key) const
{
    const auto it = _lineCaptions.find(key);
    return it != _lineCaptions.end() && !it->second->isHidden();
}

void VideoSyncWindow::clearValues()
{
    for (auto& [key, label] : _cells)
        label->setText(key == "mode" ? tr("— (no machine shown)") : QString());
    for (const char* key : {"why", "state", "clock", "presenter", "smooth", "low_latency"})
        _cells[key]->hide();
    if (_shownGraph)
        _shownGraph->Clear();
    showTiles({"display"});
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
    {
        const int refreshes = p.machineHz > 0.0 ? std::max(1, qRound(displayHz / p.machineHz)) : 1;
        mode = refreshes == 1
                   ? tr("Display rate: %1, each frame for a refresh").arg(Hz(displayHz))
                   : tr("Display rate: %1, each frame for %n refreshes", nullptr, refreshes).arg(Hz(displayHz));
    }
    else if (p.sync == "interpolate")
        mode = tr("Interpolation: a picture per refresh on the fixed %1").arg(Hz(p.maxRefreshHz));
    else
        mode = tr("Off: the frame due on each refresh");
    // Low-latency video takes over the timing: no lead, no interpolation, the frame at once (what VRR or a switched
    // display rate does stays: the refresh it lands on)
    if (p.lowLatency)
        mode = p.vrr ? tr("Low latency: each frame at once (VRR)")
                     : tr("Low latency: each frame at the first refresh after it is made (%1)").arg(Hz(displayHz));
    set("mode", mode);
    QString why;
    if (p.lowLatency)
        why = tr("The feature lowlatency is on (Machine > Low Latency): frames are not held for their sound, led or "
                 "interpolated, so the picture comes sooner and leads the sound");
    else if (p.sync == "off")
        why = tr("View > Video Synchronization is off: smooth motion %1, present policy %2, display rate %3")
                  .arg(QString::fromStdString(s.smoothMotion), QString::fromStdString(s.presentPolicy),
                       QString::fromStdString(s.displayRate));
    else
        why = QString::fromStdString(p.syncReason);  // VRR: nothing came before it, nothing to explain
    set("why", why);
    _cells["why"]->setVisible(!why.isEmpty());

    // Screen times: a native presenter's (Metal, DXGI, the timed GL swap, Vulkan). The software screen times its
    // paints: when the window draws a new frame, not when the screen shows it. The plain GL swap has neither
    const bool screen = p.timedAt == "screen";
    const bool painted = p.timedAt == "paint";
    set("clock", ClockName(p.clock), ClockText(p.clock, p.lowLatency));
    set("presenter", QString::fromStdString(p.presenter),
        QString::fromStdString(p.presenter) +
            (screen    ? (p.vrr ? tr(" - each frame at its time") : tr(" - on the refresh grid"))
             : painted ? tr(" - the window's own swap: times are its paints, not the screen")
                       : tr(" - the window's own swap, no screen times")));
    set("low_latency", tr("low latency"));
    _cells["clock"]->show();
    _cells["presenter"]->show();
    _cells["low_latency"]->setVisible(p.lowLatency);

    // The display and the machine
    set("refresh", p.fixedDisplay           ? tr("%1 fixed").arg(Hz(p.maxRefreshHz))
                   : p.minRefreshHz <= 0.0 ? tr("up to %1").arg(Hz(p.maxRefreshHz))
                                           : tr("%1 – %2").arg(Hz(p.minRefreshHz).chopped(3), Hz(p.maxRefreshHz)));
    // The emulated machine's own frame rate (its frame length at its CPU clock: a Pentagon 48.83 Hz), not a measure
    set("machine", p.machineHz > 0.0 ? tr("%1 · %2 ms").arg(Hz(p.machineHz)).arg(1000.0 / p.machineHz, 0, 'f', 2)
                                     : tr("not known yet"),
        p.machineHz > 0.0 ? tr("%1: a frame of the emulated machine every %2 ms")
                                .arg(Hz(p.machineHz))
                                .arg(1000.0 / p.machineHz, 0, 'f', 2)
                          : tr("not known yet (paused, turbo)"));
    // VRR follows the machine's frames: no display rate to fit
    const bool rateJudged = !p.vrr || p.displayState != "keep";
    showLine("display_rate", rateJudged);
    if (rateJudged)
    {
        if (p.displayState == "matched" && p.originalHz > 0.0)
            set("display_rate", tr("%1, from %2").arg(Hz(p.displayHz), Hz(p.originalHz)));
        else if (p.displayState == "unchanged")
            set("display_rate", tr("unchanged"), tr("unchanged: %1").arg(QString::fromStdString(p.displayReason)));
        else
            set("display_rate", tr("the display's own"));
    }

    // Rates over the last second: the cells change twice a second, the rates once
    const int64_t now = NowNs();
    const bool rateDue = _lastNs == 0 || now - _lastNs >= kRateWindowNs - kRefreshMs * 500'000LL;
    const double seconds = _lastNs != 0 ? static_cast<double>(now - _lastNs) / 1e9 : 0.0;
    // What judges something here: low latency asks for each frame at once (nothing to be late against, no lead); VRR
    // has no beat with the display; a paint is not a refresh (no grid to be late or off the beat on, no lead)
    const bool lateJudged = screen && !p.lowLatency;
    const bool beatJudged = screen && !p.lowLatency && !p.vrr;
    if (screen || painted)
    {
        if (rateDue)
        {
            const double rate = PerSecond(p.shown, _last.shown, seconds);
            set("shown", _lastNs != 0 ? Rate(rate) : kNone);
            if (_lastNs != 0 && _shownGraph)
                _shownGraph->Add(rate);
        }
        set("dropped", Count(p.dropped));
        showLine("late", lateJudged);
        if (lateJudged)
            set("late", Count(p.late));
        showLine("off_beat", beatJudged);
        if (beatJudged)
            set("off_beat", p.beatFrames > 0 ? tr("%1 (%2 %)")
                                                   .arg(Count(p.offBeat))
                                                   .arg(100.0 * static_cast<double>(p.offBeat) /
                                                            static_cast<double>(p.beatFrames),
                                                        0, 'f', 2)
                                             : Count(p.offBeat),
                tr("%1 of %2 pictures on screen for a refresh count outside the beat")
                    .arg(Count(p.offBeat), Count(p.beatFrames)));
        set("error_p50", Ms(p.errorP50Ms));
        set("error_p99", Ms(p.errorP99Ms));
        set("error_max", Ms(p.errorMaxMs));
        showLine("lead", lateJudged);
        if (lateJudged)
            set("lead", Ms(p.leadMs));
    }
    if (screen)
    {
        set("sound_p50", Ms(p.soundOffsetP50Ms));
        set("sound_p99", Ms(p.soundOffsetP99Ms));
        set("cuts", Count(p.cuts), tr("Breaks of the frame sequence: a pause, a seek, a stall, turbo"));
        set("carry", Ms(p.carryMs), tr("A step of the frames' clock being paid back slowly (a device came or went)"));
    }
    else
    {
        for (const char* key : {"sound_p50", "sound_p99", "cuts", "carry"})
            set(key, kNone);
    }
    if (!screen && !painted)
    {
        for (const char* key : {"shown", "dropped", "late", "off_beat", "lead", "error_p50", "error_p99", "error_max"})
            set(key, kNone);
    }

    // What the error measures: the screen against the time asked for; low latency asks for each frame at once (its
    // way from the hand-over to the screen); the software screen has its paints, the time asked for its target (low
    // latency: its latch)
    _errorCaption->setText(painted        ? (p.lowLatency ? tr("Made to painted") : tr("Painted vs asked"))
                           : p.lowLatency ? tr("Handed over to screen")
                                          : tr("On screen vs asked"));

    const bool smoothOn = p.smooth != "off";
    if (smoothOn)
    {
        set("smooth", tr("%1 · %2%3")
                          .arg(QString::fromStdString(p.smooth), QString::fromStdString(p.smoothDevice).toUpper(),
                               p.game ? tr(" · game") : QString()),
            tr("%1 on the %2%3")
                .arg(QString::fromStdString(p.smooth), QString::fromStdString(p.smoothDevice).toUpper(),
                     p.game ? tr(", the game's cadence") : QString()));
        if (rateDue && _lastNs == 0)
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
    }
    else
    {
        for (const char* key : {"frames", "pictures", "interpolated", "blended", "motion_fields", "held", "smooth"})
            set(key, kNone);
    }
    _cells["smooth"]->setVisible(smoothOn);

    // The state: are frames lost or off their refresh over the last second
    if (rateDue)
    {
        const uint64_t lost = (p.dropped >= _last.dropped ? p.dropped - _last.dropped : 0) +
                              (lateJudged && p.late >= _last.late ? p.late - _last.late : 0) +
                              (beatJudged && p.offBeat >= _last.offBeat ? p.offBeat - _last.offBeat : 0);
        _uneven = (screen || painted) && _lastNs != 0 && lost > 0;
        set("state", !(screen || painted) ? tr("Not measured") : _uneven ? tr("Uneven") : tr("Steady"),
            !(screen || painted) ? tr("This screen gives no times to judge the frames by")
            : _uneven           ? tr("%n frame(s) dropped, late or off the beat over the last second", nullptr,
                                     static_cast<int>(lost))
                                : tr("No frame dropped, late or off the beat over the last second"));
        _state->show();
        applyColors();
        _last = p;
        _lastNs = now;
    }

    std::vector<const char*> tiles{"display"};
    if (screen || painted)
    {
        tiles.push_back("frames");
        tiles.push_back("timing");
    }
    if (screen)
        tiles.push_back("sound");
    if (smoothOn)
    {
        tiles.push_back("smooth");
        tiles.push_back("motion");
    }
    showTiles(tiles);
}
