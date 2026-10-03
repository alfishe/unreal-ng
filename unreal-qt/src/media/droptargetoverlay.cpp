#include "droptargetoverlay.h"

#include "media/core/droptargetlayout.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace
{
    const QColor kAccent(61, 174, 233);
    const QColor kPanel(12, 16, 26, 210);
    const QColor kRefusal(165, 28, 28, 220);
    const QColor kRefusalBorder(255, 107, 107);
    const QColor kSubtle(170, 180, 195);

    constexpr int kTileWidth = 164;
    constexpr int kTileHeight = 184;
    constexpr int kGap = 16;
    constexpr int kMargin = 22;
    constexpr int kHeader = 52;
    constexpr int kFooter = 34;

    /// A rounded badge ("AUTOSTART") at `x`, centred on `y`; returns its width
    qreal Badge(QPainter& p, qreal x, qreal y, const QString& text, const QColor& color, qreal scale)
    {
        QFont font = p.font();
        font.setBold(true);
        font.setPointSizeF(std::max(6.5, 7.5 * scale));
        p.setFont(font);
        const qreal w = QFontMetricsF(font).horizontalAdvance(text) + 10 * scale;
        const qreal h = 15 * scale;
        const QRectF r(x, y - h / 2, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(r, h / 2, h / 2);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, text);
        return w;
    }
}  // namespace

DropTargetOverlay::DropTargetOverlay(QWidget* owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);  // a drag from the Finder leaves the app inactive
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    _confirmHold.setSingleShot(true);
    connect(&_confirmHold, &QTimer::timeout, this, [this]() { _fade.start(); });
    _fade.setTargetObject(this);
    _fade.setPropertyName("windowOpacity");
    _fade.setDuration(kConfirmFadeMs);
    _fade.setStartValue(1.0);
    _fade.setEndValue(0.0);
    connect(&_fade, &QPropertyAnimation::finished, this, [this]() {
        if (_mode == Mode::Confirm)
            dismiss();
    });

    _autoHide.setSingleShot(true);
    connect(&_autoHide, &QTimer::timeout, this, [this]() {
        dismiss();
        emit cancelled();
    });
}

QString DropTargetOverlay::IconPath(FileKind kind)
{
    switch (kind)
    {
        case FileKind::Floppy: return QStringLiteral(":/icons/media/floppy.svg");
        case FileKind::Tape: return QStringLiteral(":/icons/media/tape.svg");
        case FileKind::Hdd: return QStringLiteral(":/icons/media/hdd.svg");
        case FileKind::SdCard: return QStringLiteral(":/icons/media/sdcard.svg");
        case FileKind::Optical: return QStringLiteral(":/icons/media/cdrom.svg");
        default: return QStringLiteral(":/icons/media/floppy.svg");
    }
}

void DropTargetOverlay::showZones(const QRect& globalArea, const QString& fileName, const MediaPlan& plan)
{
    open(Mode::Zones, globalArea, fileName, plan);
}

void DropTargetOverlay::showChooser(const QRect& globalArea, const QString& fileName, const MediaPlan& plan)
{
    open(Mode::Chooser, globalArea, fileName, plan);
    activateWindow();
    setFocus(Qt::OtherFocusReason);
}

void DropTargetOverlay::showRefusal(const QRect& globalArea, const QString& fileName, const QString& reason, int autoHideMs)
{
    _reason = reason;
    open(Mode::Refusal, globalArea, fileName, MediaPlan());
    if (autoHideMs > 0)
        _autoHide.start(autoHideMs);
}

void DropTargetOverlay::showConfirm(const QRect& globalArea, const QString& fileName, const MediaPlan& plan, int index)
{
    open(Mode::Confirm, globalArea, fileName, plan);
    _hovered = index;  // the glow
    update();
    _confirmHold.start(kConfirmHoldMs);
    // Whatever happens to the animation, the overlay is gone shortly after it should have been
    _autoHide.start(kConfirmHoldMs + kConfirmFadeMs + 150);
}

void DropTargetOverlay::dismiss()
{
    _autoHide.stop();
    _confirmHold.stop();
    _fade.stop();
    setWindowOpacity(1.0);
    _mode = Mode::Hidden;
    _hovered = -1;
    hide();
}

void DropTargetOverlay::open(Mode mode, const QRect& globalArea, const QString& fileName, const MediaPlan& plan)
{
    _autoHide.stop();
    _confirmHold.stop();
    _fade.stop();
    setWindowOpacity(1.0);
    // Confirm takes no input: a following drop or click reaches the window below
    const bool transparent = mode == Mode::Confirm;
    if (windowFlags().testFlag(Qt::WindowTransparentForInput) != transparent)
        setWindowFlag(Qt::WindowTransparentForInput, transparent);
    setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    _mode = mode;
    _plan = plan;
    _fileName = fileName;
    _hovered = -1;
    if (mode == Mode::Chooser && !_plan.targets.empty())
        _hovered = _plan.defaultTarget >= 0 ? _plan.defaultTarget : 0;
    setGeometry(globalArea);
    layoutTiles();
    show();
    raise();
    update();
}

void DropTargetOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutTiles();
}

void DropTargetOverlay::layoutTiles()
{
    _tiles.clear();
    const QRect area = rect().adjusted(kMargin, kMargin + kHeader, -kMargin, -kMargin - kFooter);
    DropTileMetrics metrics;
    metrics.tileWidth = kTileWidth;
    metrics.tileHeight = kTileHeight;
    metrics.gap = kGap;
    for (const DropTile& tile : LayoutDropTiles(static_cast<int>(_plan.targets.size()), area.left(), area.top(), area.width(),
                                                area.height(), metrics))
        _tiles.emplace_back(tile.x, tile.y, tile.width, tile.height);
}

int DropTargetOverlay::tileAt(const QPoint& pos) const
{
    for (size_t i = 0; i < _tiles.size(); i++)
    {
        if (_tiles[i].contains(pos))
            return static_cast<int>(i);
    }
    return -1;
}

void DropTargetOverlay::setHovered(int index)
{
    if (index == _hovered)
        return;
    _hovered = index;
    update();
}

const QPixmap& DropTargetOverlay::iconFor(FileKind kind, int size)
{
    const auto key = std::make_pair(static_cast<int>(kind), size);
    auto it = _icons.find(key);
    if (it != _icons.end())
        return it->second;

    const qreal ratio = devicePixelRatioF();
    QPixmap pixmap(static_cast<int>(size * ratio), static_cast<int>(size * ratio));
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(IconPath(kind));
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    pixmap.setDevicePixelRatio(ratio);
    return _icons.emplace(key, pixmap).first->second;
}

void DropTargetOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);

    if (_mode == Mode::Refusal)
    {
        p.setPen(QPen(kRefusalBorder, 2));
        p.setBrush(kRefusal);
        p.drawRoundedRect(frame, 12, 12);

        // A "no entry" sign above the file's name
        const qreal sign = std::min<qreal>(56, frame.height() / 5);
        const QRectF circle(frame.center().x() - sign / 2, frame.center().y() - 52 - sign, sign, sign);
        p.setPen(QPen(Qt::white, sign / 9));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circle);
        const qreal d = sign / 2 * 0.7071;
        p.drawLine(QPointF(circle.center().x() - d, circle.center().y() - d), QPointF(circle.center().x() + d, circle.center().y() + d));

        QFont title = font();
        title.setBold(true);
        title.setPointSizeF(16);
        p.setFont(title);
        p.setPen(Qt::white);
        const QRectF text = frame.adjusted(24, 0, -24, 0);
        const QFontMetricsF metrics(title);
        const QString name = metrics.elidedText(_fileName, Qt::ElideMiddle, text.width());
        p.drawText(QRectF(text.left(), frame.center().y() - 40, text.width(), 30), Qt::AlignCenter, name);

        QFont body = font();
        body.setPointSizeF(12.5);
        p.setFont(body);
        p.drawText(QRectF(text.left(), frame.center().y() - 4, text.width(), 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   _reason);
        return;
    }

    p.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 190), 2));
    p.setBrush(kPanel);
    p.drawRoundedRect(frame, 12, 12);

    // Header: what is being placed
    QFont header = font();
    header.setBold(true);
    header.setPointSizeF(14);
    p.setFont(header);
    p.setPen(Qt::white);
    const QString heading = _mode == Mode::Zones     ? tr("Drop %1 on a slot")
                            : _mode == Mode::Confirm ? tr("%1 went to")
                                                     : tr("Insert %1 into");
    const QRectF headerRect(kMargin, kMargin, width() - 2 * kMargin, kHeader - 12);
    p.drawText(headerRect, Qt::AlignCenter,
               QFontMetricsF(header).elidedText(heading.arg(_fileName), Qt::ElideMiddle, headerRect.width()));

    for (size_t i = 0; i < _tiles.size() && i < _plan.targets.size(); i++)
    {
        const MediaTarget& target = _plan.targets[i];
        const QRectF tile = _tiles[i];
        const qreal scale = tile.width() / kTileWidth;
        const bool hot = static_cast<int>(i) == _hovered;

        if (hot && _mode == Mode::Confirm)
        {
            // The glow: soft rings growing out of the tile
            for (int ring = 4; ring >= 1; ring--)
            {
                p.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 36 + (4 - ring) * 12), 2.0));
                p.setBrush(Qt::NoBrush);
                const qreal grow = ring * 3.0 * scale;
                p.drawRoundedRect(tile.adjusted(-grow, -grow, grow, grow), 12 * scale + grow, 12 * scale + grow);
            }
        }
        p.setPen(hot ? QPen(kAccent, 2.5) : QPen(QColor(255, 255, 255, 70), 1.2));
        p.setBrush(hot ? QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 105) : QColor(255, 255, 255, 24));
        p.drawRoundedRect(tile, 12 * scale, 12 * scale);

        // The device
        const int iconSize = static_cast<int>(64 * scale);
        p.drawPixmap(QPointF(tile.center().x() - iconSize / 2.0, tile.top() + 12 * scale), iconFor(target.as, iconSize));

        // Its name, the slot id, what it holds
        const qreal textTop = tile.top() + 12 * scale + iconSize + 6 * scale;
        const qreal textWidth = tile.width() - 14 * scale;
        // The name takes up to two lines ("SD card" / "(Z-Controller)")
        QFont label = font();
        label.setBold(true);
        label.setPointSizeF(std::max(8.0, 11.5 * scale));
        p.setFont(label);
        p.setPen(Qt::white);
        const QRectF nameRect(tile.left() + 7 * scale, textTop, textWidth, 34 * scale);
        p.drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, QString::fromStdString(target.label));

        QFont small = font();
        small.setPointSizeF(std::max(7.0, 9.5 * scale));
        p.setFont(small);
        p.setPen(kSubtle);
        p.drawText(QRectF(tile.left() + 7 * scale, textTop + 34 * scale, textWidth, 15 * scale), Qt::AlignCenter,
                   QString::fromStdString(target.slotId));
        const QString holds = target.occupiedBy.empty() ? tr("empty") : tr("occupied");
        p.setPen(target.occupiedBy.empty() ? QColor(140, 220, 150) : QColor(255, 165, 40));
        p.drawText(QRectF(tile.left() + 7 * scale, textTop + 49 * scale, textWidth, 15 * scale), Qt::AlignCenter,
                   QFontMetricsF(small).elidedText(holds, Qt::ElideMiddle, textWidth));

        // Badges along the bottom edge
        std::vector<std::pair<QString, QColor>> badges;
        if (target.autostart)
            badges.emplace_back(tr("AUTOSTART"), QColor(47, 158, 68));
        if (target.dirty)
            badges.emplace_back(tr("UNSAVED!"), QColor(230, 119, 0));
        if (!badges.empty())
        {
            qreal total = 0;
            QFont badgeFont = font();
            badgeFont.setBold(true);
            badgeFont.setPointSizeF(std::max(6.5, 7.5 * scale));
            for (const auto& badge : badges)
                total += QFontMetricsF(badgeFont).horizontalAdvance(badge.first) + 10 * scale + 4 * scale;
            qreal x = tile.center().x() - (total - 4 * scale) / 2;
            for (const auto& badge : badges)
                x += Badge(p, x, tile.bottom() - 13 * scale, badge.first, badge.second, scale) + 4 * scale;
        }

        // The key that picks it
        if (_mode == Mode::Chooser && i < 9)
        {
            const QRectF key(tile.left() + 7 * scale, tile.top() + 7 * scale, 18 * scale, 18 * scale);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, hot ? 230 : 120));
            p.drawEllipse(key);
            QFont digit = font();
            digit.setBold(true);
            digit.setPointSizeF(std::max(7.0, 9.0 * scale));
            p.setFont(digit);
            p.setPen(QColor(20, 24, 34));
            p.drawText(key, Qt::AlignCenter, QString::number(i + 1));
        }
    }

    if (_mode == Mode::Confirm)
        return;  // nothing to pick: it is done

    // How to pick
    QFont hint = font();
    hint.setPointSizeF(10.5);
    p.setFont(hint);
    p.setPen(kSubtle);
    const QString how = _mode == Mode::Zones ? tr("Release over a slot, anywhere else to choose")
                                             : tr("Click or press 1-%1, Enter, Esc to cancel").arg(std::min<size_t>(9, _plan.targets.size()));
    const QRectF howRect(kMargin, height() - kMargin - kFooter + 8, width() - 2 * kMargin, kFooter - 8);
    p.drawText(howRect, Qt::AlignCenter, QFontMetricsF(hint).elidedText(how, Qt::ElideRight, howRect.width()));
}

void DropTargetOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (_mode == Mode::Chooser)
    {
        const int index = tileAt(event->position().toPoint());
        if (index >= 0)
            setHovered(index);
    }
    QWidget::mouseMoveEvent(event);
}

void DropTargetOverlay::mousePressEvent(QMouseEvent* event)
{
    if (_mode == Mode::Chooser)
    {
        const int index = tileAt(event->position().toPoint());
        dismiss();
        if (index >= 0)
            emit targetChosen(index);
        else
            emit cancelled();
        return;
    }
    if (_mode == Mode::Refusal)
    {
        dismiss();
        emit cancelled();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DropTargetOverlay::keyPressEvent(QKeyEvent* event)
{
    if (_mode != Mode::Chooser)
    {
        if (event->key() == Qt::Key_Escape && _mode != Mode::Hidden)
        {
            dismiss();
            emit cancelled();
        }
        return;
    }
    const int count = static_cast<int>(_plan.targets.size());
    const int key = event->key();
    if (key == Qt::Key_Escape)
    {
        dismiss();
        emit cancelled();
    }
    else if ((key == Qt::Key_Return || key == Qt::Key_Enter) && _hovered >= 0)
    {
        const int index = _hovered;
        dismiss();
        emit targetChosen(index);
    }
    else if (key >= Qt::Key_1 && key <= Qt::Key_9 && key - Qt::Key_1 < count)
    {
        dismiss();
        emit targetChosen(key - Qt::Key_1);
    }
    else if (count > 0 && (key == Qt::Key_Right || key == Qt::Key_Down || key == Qt::Key_Tab))
    {
        setHovered((_hovered + 1) % count);
    }
    else if (count > 0 && (key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_Backtab))
    {
        setHovered((_hovered + count - 1) % count);
    }
}

void DropTargetOverlay::dragEnterEvent(QDragEnterEvent* event)
{
    if (_mode == Mode::Zones || _mode == Mode::Refusal)
        event->acceptProposedAction();
    else
        event->ignore();
}

void DropTargetOverlay::dragMoveEvent(QDragMoveEvent* event)
{
    if (_mode == Mode::Zones)
        setHovered(tileAt(event->position().toPoint()));
    if (_mode == Mode::Zones || _mode == Mode::Refusal)
        event->acceptProposedAction();
}

void DropTargetOverlay::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    if (_mode == Mode::Zones || (_mode == Mode::Refusal && !_autoHide.isActive()))
    {
        dismiss();
        emit cancelled();
    }
}

void DropTargetOverlay::dropEvent(QDropEvent* event)
{
    event->acceptProposedAction();
    if (_mode == Mode::Zones)
    {
        const int index = tileAt(event->position().toPoint());
        if (index >= 0)
        {
            dismiss();
            emit targetChosen(index);
        }
        else
        {
            emit droppedOutside();
        }
    }
    else if (_mode == Mode::Refusal)
    {
        _autoHide.start(1500);  // the reason stays a moment after the drop, then goes
    }
}
