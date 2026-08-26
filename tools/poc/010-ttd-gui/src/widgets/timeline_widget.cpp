//
// timeline_widget.cpp — Video-editor-style timeline implementation.
//

#include "timeline_widget.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QWheelEvent>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace ttd {

// ---------------------------------------------------------------------------
// Theme color helpers — pick colors that work in both light and dark modes
// ---------------------------------------------------------------------------
struct ThemeColors {
    QColor bg;           // main background
    QColor rulerBg;      // ruler lane background
    QColor laneBg;       // section/usermarks lane background
    QColor playheadBg;   // playhead track background
    QColor tickMinor;    // minor tick marks
    QColor tickMajor;    // major tick marks
    QColor labelText;    // tick labels
    QColor playheadLine; // playhead vertical line
    QColor timeMarkDash; // dashed time mark lines
};

static ThemeColors resolveTheme() {
    // Detect dark mode by checking if the window background is dark
    const QPalette pal = qApp->palette();
    const bool isDark = pal.color(QPalette::Window).lightness() < 128;

    if (isDark) {
        return {
            QColor(32, 32, 32),     // bg
            QColor(48, 48, 48),     // rulerBg
            QColor(40, 40, 40),     // laneBg
            QColor(24, 24, 24),     // playheadBg
            QColor(80, 80, 80),     // tickMinor
            QColor(110, 110, 110),  // tickMajor
            QColor(200, 200, 200),  // labelText
            QColor(255, 200, 0),    // playheadLine
            QColor(60, 60, 60),     // timeMarkDash
        };
    } else {
        return {
            QColor(245, 245, 245),  // bg
            QColor(230, 230, 230),  // rulerBg
            QColor(238, 238, 238),  // laneBg
            QColor(250, 250, 250),  // playheadBg
            QColor(170, 170, 170),  // tickMinor
            QColor(100, 100, 100),  // tickMajor
            QColor(50, 50, 50),     // labelText
            QColor(220, 140, 0),    // playheadLine
            QColor(190, 190, 190),  // timeMarkDash
        };
    }
}

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);

    // Auto-fill background so the widget respects theme
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);

    connect(&_playTimer, &QTimer::timeout, this, &TimelineWidget::onPlayTimer);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TimelineWidget::setSessionRange(uint64_t start, uint64_t end) {
    _rangeStart = start;
    _rangeEnd = end;
    _frame = start;
    _zoomFactor = 1.0;
    update();
}

void TimelineWidget::setFrame(uint64_t frame) {
    if (frame < _rangeStart) frame = _rangeStart;
    if (frame > _rangeEnd)   frame = _rangeEnd;
    if (frame == _frame)     return;
    _frame = frame;
    update();
}

void TimelineWidget::setMarksModel(MarksModel* model) {
    _marksModel = model;
    update();
}

void TimelineWidget::zoomIn() {
    _zoomFactor = std::max(0.01, _zoomFactor * 0.8);
    update();
}

void TimelineWidget::zoomOut() {
    _zoomFactor = std::min(1.0, _zoomFactor * 1.25);
    update();
}

// ---------------------------------------------------------------------------
// Frame-to-pixel conversions
// ---------------------------------------------------------------------------

double TimelineWidget::framesPerPx() const {
    uint64_t range = (_rangeEnd > _rangeStart) ? (_rangeEnd - _rangeStart) : 1;
    double usableW = std::max(1, width() - kLeftMargin - kRightMargin);
    return (static_cast<double>(range) / usableW) * _zoomFactor;
}

int TimelineWidget::frameToX(uint64_t frame) const {
    if (frame <= _rangeStart) return kLeftMargin;
    double offset = static_cast<double>(frame - _rangeStart) / framesPerPx();
    return kLeftMargin + static_cast<int>(std::round(offset));
}

uint64_t TimelineWidget::xToFrame(int x) const {
    if (x <= kLeftMargin) return _rangeStart;
    double offset = static_cast<double>(x - kLeftMargin) * framesPerPx();
    uint64_t frame = _rangeStart + static_cast<uint64_t>(std::round(offset));
    if (frame > _rangeEnd) frame = _rangeEnd;
    return frame;
}

int TimelineWidget::pickTickInterval() const {
    double fpp = framesPerPx();
    double targetFrames = fpp * 80.0;
    static const int intervals[] = {1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
    for (int iv : intervals) {
        if (iv >= targetFrames) return iv;
    }
    return 50000;
}

// ---------------------------------------------------------------------------
// Lane geometry
// ---------------------------------------------------------------------------

QRect TimelineWidget::rulerRect() const {
    return QRect(kLeftMargin, 0, width() - kLeftMargin - kRightMargin, kRulerHeight);
}

QRect TimelineWidget::sectionRect() const {
    return QRect(kLeftMargin, kRulerHeight, width() - kLeftMargin - kRightMargin, kSectionHeight);
}

QRect TimelineWidget::userMarksRect() const {
    int y = kRulerHeight + kSectionHeight;
    return QRect(kLeftMargin, y, width() - kLeftMargin - kRightMargin, kUserMarksHeight);
}

QRect TimelineWidget::playheadRect() const {
    int y = kRulerHeight + kSectionHeight + kUserMarksHeight;
    return QRect(kLeftMargin, y, width() - kLeftMargin - kRightMargin, kPlayheadHeight);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    ThemeColors tc = resolveTheme();

    // Background
    p.fillRect(rect(), tc.bg);

    // Lanes
    drawRuler(p);
    drawSections(p);
    drawUserMarks(p);
    drawPlayhead(p);
}

void TimelineWidget::drawRuler(QPainter& p) {
    QRect rr = rulerRect();
    ThemeColors tc = resolveTheme();
    p.fillRect(rr, tc.rulerBg);

    int tickInterval = pickTickInterval();
    double fpp = framesPerPx();

    // Minor ticks
    p.setPen(tc.tickMinor);
    for (uint64_t f = _rangeStart; f <= _rangeEnd; f += static_cast<uint64_t>(tickInterval)) {
        int x = frameToX(f);
        if (x < kLeftMargin || x > width() - kRightMargin) continue;
        p.drawLine(x, rr.bottom() - 6, x, rr.bottom());
    }

    // Major ticks + labels
    int labelInterval = (tickInterval >= 1000) ? tickInterval * 2 :
                        (tickInterval * 5 > static_cast<int>(fpp * 200) ? tickInterval * 5 : tickInterval * 10);
    if (labelInterval < tickInterval) labelInterval = tickInterval;

    QFont smallFont = font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);
    p.setPen(tc.labelText);

    for (uint64_t f = _rangeStart; f <= _rangeEnd; f += static_cast<uint64_t>(labelInterval)) {
        int x = frameToX(f);
        if (x < kLeftMargin || x > width() - kRightMargin) continue;
        p.drawLine(x, rr.top() + 2, x, rr.bottom());
        QString label = QString::number(f);
        p.drawText(x + 3, rr.top() + 14, label);
    }
}

void TimelineWidget::drawSections(QPainter& p) {
    QRect sr = sectionRect();
    ThemeColors tc = resolveTheme();
    p.fillRect(sr, tc.laneBg);

    if (!_marksModel) return;
    for (const auto& sec : _marksModel->sections()) {
        int x1 = frameToX(sec.start);
        int x2 = frameToX(sec.end);
        if (x2 < kLeftMargin || x1 > width() - kRightMargin) continue;

        x1 = std::max(x1, kLeftMargin);
        x2 = std::min(x2, width() - kRightMargin);
        if (x2 <= x1) continue;

        QColor fill = sec.color;
        fill.setAlpha(100);
        p.fillRect(x1, sr.top() + 2, x2 - x1, sr.height() - 4, fill);

        QFont smallFont = font();
        smallFont.setPointSize(7);
        p.setFont(smallFont);
        p.setPen(sec.color.darker(150));
        p.drawText(QRect(x1 + 4, sr.top() + 2, x2 - x1 - 8, sr.height() - 4),
                   Qt::AlignLeft | Qt::AlignVCenter, sec.label);
    }
}

void TimelineWidget::drawUserMarks(QPainter& p) {
    QRect ur = userMarksRect();
    ThemeColors tc = resolveTheme();
    p.fillRect(ur, tc.laneBg);

    if (!_marksModel) return;

    for (const auto& m : _marksModel->marks()) {
        int x = frameToX(m.frame);
        if (x < kLeftMargin || x > width() - kRightMargin) continue;

        QPainterPath triangle;
        int tipY = ur.top();
        int baseY = ur.top() + 10;
        triangle.moveTo(x, tipY);
        triangle.lineTo(x - 5, baseY);
        triangle.lineTo(x + 5, baseY);
        triangle.closeSubpath();

        p.fillPath(triangle, m.color);
    }
}

void TimelineWidget::drawTimeMarks(QPainter& p) {
    if (framesPerPx() > 2.0) return;

    ThemeColors tc = resolveTheme();
    QPen dashPen(tc.timeMarkDash, 1, Qt::DashLine);
    p.setPen(dashPen);
    int tickInterval = pickTickInterval();
    for (uint64_t f = _rangeStart; f <= _rangeEnd; f += static_cast<uint64_t>(tickInterval)) {
        int x = frameToX(f);
        if (x < kLeftMargin || x > width() - kRightMargin) continue;
        p.drawLine(x, rulerRect().top(), x, playheadRect().bottom());
    }
}

void TimelineWidget::drawPlayhead(QPainter& p) {
    drawTimeMarks(p);

    ThemeColors tc = resolveTheme();
    QRect pr = playheadRect();
    p.fillRect(pr, tc.playheadBg);

    int px = playheadX();

    // Playhead line through all lanes
    QColor lineColor = tc.playheadLine;
    lineColor.setAlpha(200);
    p.setPen(lineColor);
    p.drawLine(px, 0, px, height());

    // Playhead handle
    QPainterPath handle;
    int tipY = pr.top() + 2;
    int baseY = pr.bottom() - 2;
    handle.moveTo(px, tipY);
    handle.lineTo(px - kPlayheadWidth / 2, tipY + 8);
    handle.lineTo(px - kPlayheadWidth / 2, baseY);
    handle.lineTo(px + kPlayheadWidth / 2, baseY);
    handle.lineTo(px + kPlayheadWidth / 2, tipY + 8);
    handle.closeSubpath();

    p.fillPath(handle, tc.playheadLine);

    // Frame number bubble above the playhead
    QString frameStr = QString::number(_frame);
    QFontMetrics fm(font());
    int textW = fm.horizontalAdvance(frameStr) + 8;
    int bubbleX = px - textW / 2;
    int bubbleY = pr.top() - 18;
    if (bubbleY < 0) bubbleY = pr.top() + 2;

    QRect bubble(bubbleX, bubbleY, textW, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(tc.playheadLine);
    p.drawRoundedRect(bubble, 3, 3);
    // Bubble text — always dark on the yellow/amber bubble for contrast
    p.setPen(QColor(0, 0, 0));
    p.drawText(bubble, Qt::AlignCenter, frameStr);
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        _dragging = true;
        uint64_t f = xToFrame(event->position().x());
        setFrame(f);
        emit frameChanged(_frame);
        setFocus();
        event->accept();
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (_dragging) {
        uint64_t f = xToFrame(event->position().x());
        setFrame(f);
        emit frameChanged(_frame);
        event->accept();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        _dragging = false;
        event->accept();
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        uint64_t f = xToFrame(event->position().x());
        emit userMarkRequested(f);
        event->accept();
    }
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    int delta = event->angleDelta().y();
    if (delta > 0) {
        _zoomFactor = std::max(0.01, _zoomFactor * 0.8);
    } else if (delta < 0) {
        _zoomFactor = std::min(1.0, _zoomFactor * 1.25);
    }
    update();
    event->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Left:
            if (_frame > _rangeStart) {
                setFrame(_frame - (event->modifiers() & Qt::ShiftModifier ? 10 : 1));
                emit frameChanged(_frame);
            }
            break;
        case Qt::Key_Right:
            if (_frame < _rangeEnd) {
                setFrame(_frame + (event->modifiers() & Qt::ShiftModifier ? 10 : 1));
                emit frameChanged(_frame);
            }
            break;
        case Qt::Key_Home:
            setFrame(_rangeStart);
            emit frameChanged(_frame);
            break;
        case Qt::Key_End:
            setFrame(_rangeEnd);
            emit frameChanged(_frame);
            break;
        case Qt::Key_Space:
            _playing = !_playing;
            if (_playing) {
                _playTimer.start(1000);
            } else {
                _playTimer.stop();
            }
            event->accept();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event) {
    uint64_t frame = xToFrame(event->pos().x());

    QMenu menu(this);
    QAction* addMarkAct = menu.addAction(QStringLiteral("Add mark at frame %1").arg(frame));
    QAction* selected = menu.exec(event->globalPos());

    if (selected == addMarkAct) {
        emit userMarkRequested(frame);
    }
}

void TimelineWidget::onPlayTimer() {
    if (_frame < _rangeEnd) {
        setFrame(_frame + 1);
        emit frameChanged(_frame);
    } else {
        _playing = false;
        _playTimer.stop();
    }
}

} // namespace ttd
