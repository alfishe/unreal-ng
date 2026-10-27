#include "connectorcard.h"

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

#include "controllers/connectorview.h"
#include "controllers/controllerpicture.h"

using namespace controllers;

namespace
{
/// The device a drag carries; empty when it carries none
std::string DraggedDevice(const QMimeData* mime)
{
    return mime != nullptr && mime->hasFormat(kHostDeviceMime) ? mime->data(kHostDeviceMime).toStdString()
                                                                : std::string{};
}

/// A plugged host device on its card: dragged off, it unplugs; onto another card, it moves
class DeviceChip : public QLabel
{
public:
    DeviceChip(const std::string& device, const QString& name, const QColor& colour, QWidget* parent)
        : QLabel(parent), _device(device)
    {
        setText(QStringLiteral("▸ ") + name);
        setToolTip(QObject::tr("%1\nDrag it off the card to unplug, onto another card to move it")
                       .arg(QString::fromStdString(device)));
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(QStringLiteral("QLabel { border-left: 3px solid %1; padding-left: 4px; }").arg(colour.name()));
    }
    std::function<void(const std::string&)> draggedOff;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            _press = event->position().toPoint();
        QLabel::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton) ||
            (event->position().toPoint() - _press).manhattanLength() < QApplication::startDragDistance())
            return;
        auto* drag = new QDrag(this);
        auto* mime = new QMimeData();
        mime->setData(kHostDeviceMime, QByteArray::fromStdString(_device));
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        // Dropped where nothing takes it (empty space, outside the window): the plug goes, with undo
        if (drag->exec(Qt::MoveAction) == Qt::IgnoreAction && draggedOff)
            draggedOff(_device);
    }

private:
    std::string _device;
    QPoint _press;
};
}  // namespace

/// The five ZX keys a Sinclair or Cursor joystick presses: lit in the connector's colour while held (the overlay's
/// presses; the keyboard's own keys are not shown here, ui-ux.md §4)
class KeyStripView : public QWidget
{
public:
    explicit KeyStripView(QWidget* parent) : QWidget(parent)
    {
        setFixedHeight(26);
    }
    std::vector<connectorview::StripKey> keys;
    uint16_t held = 0;
    QColor colour;

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (keys.empty())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double gap = 4;
        const double size = std::min<double>(height() - 2, (width() - gap * (keys.size() - 1)) / keys.size());
        double x = 0;
        for (const connectorview::StripKey& key : keys)
        {
            const QRectF cap(x, 1, size, size);
            const bool on = (held & key.bit) != 0;
            painter.setPen(QPen(palette().color(QPalette::Mid), 1));
            painter.setBrush(on ? colour : palette().color(QPalette::Button));
            painter.drawRoundedRect(cap, 3, 3);
            painter.setPen(on ? QColor(Qt::white) : palette().color(QPalette::ButtonText));
            painter.drawText(cap, Qt::AlignCenter, key.key);
            x += size + gap;
        }
    }
};

DeviceDragList::DeviceDragList(QWidget* parent) : QListWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::MoveAction);
}

QStringList DeviceDragList::mimeTypes() const
{
    return {QString::fromLatin1(kHostDeviceMime)};
}

QMimeData* DeviceDragList::mimeData(const QList<QListWidgetItem*>& items) const
{
    if (items.isEmpty())
        return nullptr;
    auto* mime = new QMimeData();
    mime->setData(kHostDeviceMime, items.front()->data(Qt::UserRole).toString().toUtf8());
    return mime;
}

ConnectorCard::ConnectorCard(QWidget* parent) : QFrame(parent)
{
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 8, 8);
    layout->setSpacing(4);
    _title = new QLabel(this);
    layout->addWidget(_title);
    _device = new QComboBox(this);
    layout->addWidget(_device);
    auto* row = new QHBoxLayout();
    _picture = new ControllerPicture(this);
    _picture->setCompact(true);
    _picture->setFixedSize(120, 90);
    row->addWidget(_picture);
    auto* right = new QVBoxLayout();
    _chips = new QWidget(this);
    _chipsLayout = new QVBoxLayout(_chips);
    _chipsLayout->setContentsMargins(0, 0, 0, 0);
    _chipsLayout->setSpacing(2);
    right->addWidget(_chips);
    right->addStretch(1);
    _heldLabel = new QLabel(this);
    _heldLabel->setWordWrap(true);
    right->addWidget(_heldLabel);
    row->addLayout(right, 1);
    layout->addLayout(row);
    _strip = new KeyStripView(this);
    layout->addWidget(_strip);
    _noteLabel = new QLabel(this);
    _noteLabel->setStyleSheet(QStringLiteral("color: gray;"));
    _noteLabel->hide();
    layout->addWidget(_noteLabel);
    _refusalLabel = new QLabel(this);
    _refusalLabel->setWordWrap(true);
    _refusalLabel->setStyleSheet(QStringLiteral("color: #c0504d;"));
    _refusalLabel->hide();
    layout->addWidget(_refusalLabel);
    connect(_device, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!_updating)
            emit retroDeviceChosen(QString::fromStdString(_connector.id), _device->currentData().toString());
    });
}

void ConnectorCard::setConnector(const Connector& connector)
{
    _connector = connector;
    const connectorview::Style style = connectorview::StyleFor(connector);
    const QString port = connectorview::PortText(connector);
    _title->setText(QStringLiteral("<span style='color:%1'>●</span> <b>%2</b> %3 <span style='color:gray'>%4</span>")
                        .arg(style.colour.name(), style.tag.toHtmlEscaped(),
                             connectorview::DisplayName(connector).toHtmlEscaped(), port.toHtmlEscaped()));
    _title->setToolTip(QString::fromStdString(connector.id));
    _updating = true;
    _device->clear();
    if (!connector.fixed)
        _device->addItem(tr("(empty)"), QString());
    for (RetroDeviceKind kind : connector.accepts)
        _device->addItem(QString::fromLatin1(RetroDeviceKindName(kind)), QString::fromLatin1(RetroDeviceKindName(kind)));
    _device->setCurrentIndex(std::max(0, _device->findData(connector.device == RetroDeviceKind::None
                                                               ? QString()
                                                               : QString::fromLatin1(RetroDeviceKindName(connector.device)))));
    _device->setEnabled(!connector.fixed);
    _device->setToolTip(connector.fixed ? tr("Part of the board") : tr("The retro device in this connector"));
    _updating = false;
    const QString set = connectorview::ImageSetFor(connector.device);
    if (set != _pictureSet)
    {
        _pictureSet = set;
        _picture->load(set);
    }
    _picture->setLitColour(QColor(style.colour.red(), style.colour.green(), style.colour.blue(), 190));
    _picture->setVisible(connector.device != RetroDeviceKind::None);
    _strip->keys = connectorview::KeyStrip(connector);
    _strip->colour = style.colour;
    _strip->setVisible(!_strip->keys.empty());
    // ui-ux.md §4: the Next reads the 6-button pad's extra buttons only through NR #B2
    const bool next = connector.kind == ConnectorKind::NextDb9 && connector.device == RetroDeviceKind::Md6Pad;
    _noteLabel->setText(next ? tr("<small>X Y Z MODE: NR #B2 only</small>") : QString());
    _noteLabel->setToolTip(next ? tr("The guest reads X, Y, Z and MODE through NR #B2; NR #05's joystick modes do "
                                     "not carry them")
                                : QString());
    _noteLabel->setVisible(next);
    rebuildDrivers();
    _held = static_cast<uint16_t>(~_held);  // repaint the held controls for the new device
    setHeld(static_cast<uint16_t>(~_held));
    update();
}

void ConnectorCard::setHeld(uint16_t mask)
{
    if (mask == _held)
        return;
    _held = mask;
    _picture->setLive(connectorview::LiveOf(_connector.device, mask));
    _strip->held = mask;
    _strip->update();
    _heldLabel->setText(_connector.device == RetroDeviceKind::None
                            ? QString()
                            : QStringLiteral("<small>%1</small>")
                                  .arg(connectorview::HeldText(_connector.device, mask).toHtmlEscaped()));
}

void ConnectorCard::setDrivers(const std::vector<std::pair<std::string, QString>>& drivers)
{
    if (drivers == _drivers)
        return;
    _drivers = drivers;
    rebuildDrivers();
}

void ConnectorCard::rebuildDrivers()
{
    while (QLayoutItem* item = _chipsLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    const QColor colour = connectorview::StyleFor(_connector).colour;
    for (const auto& [device, name] : _drivers)
    {
        auto* chip = new DeviceChip(device, name, colour, _chips);
        const QString connector = QString::fromStdString(_connector.id);
        chip->draggedOff = [this, connector](const std::string& id) {
            emit deviceDraggedOff(QString::fromStdString(id), connector);
        };
        _chipsLayout->addWidget(chip);
    }
    if (_drivers.empty())
    {
        auto* empty = new QLabel(_connector.device == RetroDeviceKind::None ? tr("empty: choose a device, then drop a pad")
                                                                            : tr("drop a device"),
                                 _chips);
        empty->setStyleSheet(QStringLiteral("color: gray;"));
        _chipsLayout->addWidget(empty);
    }
}

QPoint ConnectorCard::cordAnchor() const
{
    return {0, _title->geometry().center().y()};
}

void ConnectorCard::showRefusal(const QString& refusal)
{
    _refusal = refusal;
    _refusalLabel->setText(refusal);
    _refusalLabel->setVisible(!refusal.isEmpty());
}

bool ConnectorCard::dropDevice(const std::string& device)
{
    const QString refusal = refusalFor ? refusalFor(device) : QString();
    if (!refusal.isEmpty())
    {
        showRefusal(refusal);
        return false;
    }
    emit deviceDropped(QString::fromStdString(device), QString::fromStdString(_connector.id));
    return true;
}

void ConnectorCard::dragEnterEvent(QDragEnterEvent* event)
{
    const std::string device = DraggedDevice(event->mimeData());
    if (device.empty())
    {
        event->ignore();
        return;
    }
    // Accepted even when refused: the card gets the leave and can take its reason down again
    const QString refusal = refusalFor ? refusalFor(device) : QString();
    showRefusal(refusal);
    _hover = refusal.isEmpty();
    update();
    event->acceptProposedAction();
}

void ConnectorCard::dragMoveEvent(QDragMoveEvent* event)
{
    if (_refusal.isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();  // the forbidden cursor, and no drop
}

void ConnectorCard::dragLeaveEvent(QDragLeaveEvent* event)
{
    showRefusal(QString());
    _hover = false;
    update();
    QFrame::dragLeaveEvent(event);
}

void ConnectorCard::dropEvent(QDropEvent* event)
{
    const std::string device = DraggedDevice(event->mimeData());
    _hover = false;
    update();
    if (device.empty() || !dropDevice(device))
    {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ConnectorCard::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor colour = connectorview::StyleFor(_connector).colour;
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(_hover ? colour : palette().color(QPalette::Mid), _hover ? 2.0 : 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 6, 6);
    // The connector's colour as a band on the side the cords come in from
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF(box.left(), box.top() + 6, 3, box.height() - 12), 1.5, 1.5);
}
