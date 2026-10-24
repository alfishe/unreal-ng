#include "hotspoteditor.h"

#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "controllers/controllerpicture.h"

namespace
{
constexpr double kDesignWidth = 2048.0;
constexpr double kDesignHeight = 1536.0;

/// The photo letterboxed into the design canvas (design space)
QRectF PhotoRect(const QImage& photo)
{
    if (photo.isNull())
        return {};
    const double scale = std::min(kDesignWidth / photo.width(), kDesignHeight / photo.height());
    const double w = photo.width() * scale;
    const double h = photo.height() * scale;
    return {(kDesignWidth - w) / 2, (kDesignHeight - h) / 2, w, h};
}

/// A pad's controls when the database lists none (a device it does not know)
const std::vector<std::string>& PadControls()
{
    static const std::vector<std::string> controls = {"south", "east",    "west",    "north", "dpad.*", "lb",
                                                      "rb",    "lt",      "rt",      "lstick.*", "rstick.*",
                                                      "view",  "menu",    "guide"};
    return controls;
}

QString LabelOf(const std::string& control)
{
    static const std::map<std::string, QString> labels = {
        {"south", "A"},       {"east", "B"},        {"west", "X"},       {"north", "Y"},     {"lb", "LB"},
        {"rb", "RB"},         {"lt", "LT"},         {"rt", "RT"},        {"lstick.x", "LS"}, {"lstick.click", "LS"},
        {"rstick.x", "RS"},   {"rstick.click", "RS"}, {"view", "View"},  {"menu", "Menu"},   {"guide", "Guide"},
        {"dpad.up", "D-pad"}, {"dpad.down", "D-pad"}, {"dpad.left", "D-pad"}, {"dpad.right", "D-pad"},
        {"paddle.p1", "P1"},  {"paddle.p2", "P2"},  {"paddle.p3", "P3"}, {"paddle.p4", "P4"}};
    const auto it = labels.find(control);
    return it != labels.end() ? it->second : QString::fromStdString(control);
}

/// The hotspot a control starts with: a stick's area, a shoulder's bar, a button's circle
HotspotEditor::Hotspot Default(const std::string& control)
{
    HotspotEditor::Hotspot spot;
    spot.control = control;
    spot.label = LabelOf(control);
    if (control == "lb" || control == "rb" || control == "lt" || control == "rt")
    {
        spot.circle = false;
        spot.size = 110;
        spot.aspect = control[1] == 't' ? 0.5 : 0.25;
    }
    else if (control.rfind("dpad.", 0) == 0 || control.rfind("paddle.", 0) == 0)
    {
        spot.circle = false;
        spot.size = 40;
    }
    else if (control == "lstick.x" || control == "rstick.x")
        spot.size = 110;
    else if (control == "lstick.click" || control == "rstick.click")
        spot.size = 60;
    else
        spot.size = 58;
    return spot;
}

/// A device id's USB ids: "bt:045e:0b22:..." -> "045e", "0b22"
bool UsbIds(const std::string& device, std::string& vid, std::string& pid)
{
    const QStringList parts = QString::fromStdString(device).split(':');
    if (parts.size() < 3 || parts[1].size() != 4 || parts[2].size() != 4)
        return false;
    bool okVid = false;
    bool okPid = false;
    parts[1].toUShort(&okVid, 16);
    parts[2].toUShort(&okPid, 16);
    if (!okVid || !okPid)
        return false;
    vid = parts[1].toLower().toStdString();
    pid = parts[2].toLower().toStdString();
    return true;
}
}  // namespace

/// The photo with the hotspots over it; a click reports the design-space point
class HotspotCanvas : public QWidget
{
public:
    explicit HotspotCanvas(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(480, 360);
        setCursor(Qt::CrossCursor);
    }
    std::function<void(const QPointF&)> clicked;
    const QImage* photo = nullptr;
    const std::vector<HotspotEditor::Hotspot>* spots = nullptr;
    std::string selected;

    QRectF canvasRect() const
    {
        const double scale = std::min(width() / kDesignWidth, height() / kDesignHeight);
        const double w = kDesignWidth * scale;
        const double h = kDesignHeight * scale;
        return {(width() - w) / 2, (height() - h) / 2, w, h};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF canvas = canvasRect();
        painter.fillRect(canvas, palette().color(QPalette::Base));
        if (photo == nullptr || photo->isNull())
        {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(canvas, Qt::AlignCenter, tr("Choose a photo of the pad, or paste one"));
            return;
        }
        const double scale = canvas.width() / kDesignWidth;
        const QRectF fit = PhotoRect(*photo);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(canvas.left() + fit.left() * scale, canvas.top() + fit.top() * scale,
                                 fit.width() * scale, fit.height() * scale),
                          *photo);
        if (spots == nullptr)
            return;
        for (const HotspotEditor::Hotspot& spot : *spots)
        {
            if (!spot.placed)
                continue;
            const bool on = spot.control == selected;
            const QPointF c(canvas.left() + spot.centre.x() * scale, canvas.top() + spot.centre.y() * scale);
            const double w = spot.size * scale;
            const double h = spot.circle ? w : w * spot.aspect;
            QColor pen = on ? QColor(0, 140, 255) : QColor(255, 255, 255, 200);
            painter.setPen(QPen(QColor(0, 0, 0, 120), 3.0));
            painter.setBrush(Qt::NoBrush);
            const QRectF box(c.x() - w, c.y() - h, 2 * w, 2 * h);
            spot.circle ? painter.drawEllipse(box) : painter.drawRoundedRect(box, 4, 4);
            painter.setPen(QPen(pen, on ? 2.0 : 1.2));
            if (on)
                painter.setBrush(QColor(0, 140, 255, 60));
            spot.circle ? painter.drawEllipse(box) : painter.drawRoundedRect(box, 4, 4);
            // The callout side: a short tick towards its column
            painter.drawLine(QPointF(spot.left ? box.left() : box.right(), c.y()),
                             QPointF(spot.left ? box.left() - 10 : box.right() + 10, c.y()));
        }
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        const QRectF canvas = canvasRect();
        if (!clicked || canvas.isEmpty() || !canvas.contains(event->position()))
            return;
        const double scale = canvas.width() / kDesignWidth;
        clicked(QPointF((event->position().x() - canvas.left()) / scale, (event->position().y() - canvas.top()) / scale));
    }
};

HotspotEditor::HotspotEditor(const Target& target, Hooks hooks, QWidget* parent)
    : QDialog(parent), _target(target), _hooks(std::move(hooks))
{
    setWindowTitle(tr("Picture of %1").arg(QString::fromStdString(target.name)));
    resize(980, 620);
    auto* root = new QHBoxLayout(this);
    _canvas = new HotspotCanvas(this);
    _canvas->photo = &_photo;
    _canvas->spots = &_hotspots;
    _canvas->clicked = [this](const QPointF& point) { placeAt(point); };
    root->addWidget(_canvas, 1);

    auto* side = new QVBoxLayout();
    auto* pick = new QHBoxLayout();
    auto* choose = new QPushButton(tr("Choose photo…"), this);
    auto* paste = new QPushButton(tr("Paste"), this);
    pick->addWidget(choose);
    pick->addWidget(paste);
    side->addLayout(pick);
    auto* hint = new QLabel(tr("Select a control (or press it on the pad), then click its spot on the photo."), this);
    hint->setWordWrap(true);
    side->addWidget(hint);
    _list = new QListWidget(this);
    side->addWidget(_list, 1);
    auto* form = new QHBoxLayout();
    form->addWidget(new QLabel(tr("Size"), this));
    _size = new QSpinBox(this);
    _size->setRange(10, 400);
    _size->setSuffix(QStringLiteral(" px"));
    _size->setToolTip(tr("The radius of a circle, the half width of a bar (design space: 2048 x 1536)"));
    form->addWidget(_size);
    form->addWidget(new QLabel(tr("Callout"), this));
    _callout = new QComboBox(this);
    _callout->addItem(tr("Left"), true);
    _callout->addItem(tr("Right"), false);
    form->addWidget(_callout);
    side->addLayout(form);
    auto* remove = new QPushButton(tr("Remove spot"), this);
    side->addWidget(remove);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    side->addWidget(_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    side->addWidget(buttons);
    root->addLayout(side);

    // The controls: the database's, the wildcards expanded; a stick is placed by its x axis (its y is the pair)
    std::vector<std::string> controls;
    for (const std::string& entry : target.controls.empty() ? PadControls() : target.controls)
    {
        if (entry == "dpad.*")
            controls.insert(controls.end(), {"dpad.up", "dpad.down", "dpad.left", "dpad.right"});
        else if (entry == "lstick.*" || entry == "rstick.*")
        {
            const std::string stick = entry.substr(0, 6);
            controls.insert(controls.end(), {stick + ".x", stick + ".click"});
        }
        else if (entry.find('*') == std::string::npos && entry != "profile")
            controls.push_back(entry);
    }
    for (const std::string& control : controls)
        _hotspots.push_back(Default(control));
    // The picture shown now is the starting point: its positions, and its photo when it is one
    loadMap(ControllerPicture::ImageSetDir(QString::fromStdString(target.imageSet)));
    rebuildList();

    connect(_list, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
    connect(_size, &QSpinBox::valueChanged, this, [this](int value) {
        if (Hotspot* spot = find(selectedControl()); spot != nullptr && !_updating)
        {
            spot->size = value;
            _canvas->update();
        }
    });
    connect(_callout, &QComboBox::currentIndexChanged, this, [this] {
        if (Hotspot* spot = find(selectedControl()); spot != nullptr && !_updating)
        {
            spot->left = _callout->currentData().toBool();
            _canvas->update();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (Hotspot* spot = find(selectedControl()))
        {
            spot->placed = false;
            rebuildList();
            _canvas->update();
        }
    });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Photo of the pad"), QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (file.isEmpty())
            return;
        QImageReader reader(file);
        reader.setAutoTransform(true);  // a phone photo's EXIF rotation
        const QImage image = reader.read();
        if (image.isNull())
            _status->setText(tr("Cannot read %1: %2").arg(file, reader.errorString()));
        else
            setPhoto(image);
    });
    connect(paste, &QPushButton::clicked, this, [this] {
        const QImage image = QGuiApplication::clipboard()->image();
        if (image.isNull())
            _status->setText(tr("The clipboard holds no image"));
        else
            setPhoto(image);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        if (save(error))
            accept();
        else
            _status->setText(error);
    });

    // Press-to-select: the pad's controls at 30 Hz while the editor is open
    _live = new QTimer(this);
    _live->setInterval(33);
    if (_hooks.live)
    {
        connect(_live, &QTimer::timeout, this, [this] { setLive(_hooks.live()); });
        _live->start();
    }
}

HotspotEditor::Hotspot* HotspotEditor::find(const std::string& control)
{
    for (Hotspot& spot : _hotspots)
        if (spot.control == control)
            return &spot;
    return nullptr;
}

void HotspotEditor::loadMap(const QString& setDir)
{
    if (setDir.isEmpty())
        return;
    for (const char* name : {"front.png", "front.jpg"})
        if (_photo.isNull() && QFile::exists(setDir + name))
            _photo = QImage(setDir + name);
    QFile file(setDir + "front.hotspots.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray size = doc.value("size").toArray();
    const double sx = size.size() == 2 && size.at(0).toDouble() > 0 ? kDesignWidth / size.at(0).toDouble() : 1.0;
    const double sy = size.size() == 2 && size.at(1).toDouble() > 0 ? kDesignHeight / size.at(1).toDouble() : 1.0;
    for (const QJsonValue& value : doc.value("controls").toArray())
    {
        const QJsonObject control = value.toObject();
        Hotspot* spot = find(control.value("control").toString().toStdString());
        if (spot == nullptr)
            continue;
        const QJsonObject shape = control.value("shape").toObject();
        if (shape.contains("circle"))
        {
            const QJsonArray c = shape.value("circle").toArray();
            spot->circle = true;
            spot->centre = QPointF(c.at(0).toDouble() * sx, c.at(1).toDouble() * sy);
            spot->size = c.at(2).toDouble() * sx;
        }
        else if (shape.contains("rect"))
        {
            const QJsonArray r = shape.value("rect").toArray();
            const double w = r.at(2).toDouble() * sx;
            const double h = r.at(3).toDouble() * sy;
            spot->circle = false;
            spot->centre = QPointF(r.at(0).toDouble() * sx + w / 2, r.at(1).toDouble() * sy + h / 2);
            spot->size = w / 2;
            spot->aspect = w > 0 ? h / w : 1.0;
        }
        else
            continue;
        spot->label = control.value("label").toString(spot->label);
        spot->left = control.value("callout").toString("left") == "left";
        spot->placed = true;
    }
}

void HotspotEditor::rebuildList()
{
    const std::string current = selectedControl();
    _updating = true;
    _list->clear();
    for (const Hotspot& spot : _hotspots)
    {
        auto* item = new QListWidgetItem(
            QString("%1  %2%3").arg(spot.label, QString::fromStdString(spot.control), spot.placed ? QString() : " —"),
            _list);
        item->setData(Qt::UserRole, QString::fromStdString(spot.control));
        if (!spot.placed)
            item->setForeground(palette().color(QPalette::PlaceholderText));
    }
    _updating = false;
    selectControl(current.empty() && !_hotspots.empty() ? _hotspots.front().control : current);
}

std::string HotspotEditor::selectedControl() const
{
    const QListWidgetItem* item = _list->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString().toStdString() : std::string{};
}

void HotspotEditor::selectControl(const std::string& control)
{
    for (int row = 0; row < _list->count(); ++row)
        if (_list->item(row)->data(Qt::UserRole).toString().toStdString() == control)
        {
            _list->setCurrentRow(row);
            break;
        }
    showSelected();
}

void HotspotEditor::showSelected()
{
    const std::string control = selectedControl();
    _canvas->selected = control;
    if (const Hotspot* spot = find(control))
    {
        _updating = true;
        _size->setValue(static_cast<int>(std::lround(spot->size)));
        _callout->setCurrentIndex(spot->left ? 0 : 1);
        _updating = false;
    }
    _canvas->update();
}

void HotspotEditor::setPhoto(const QImage& photo)
{
    _photo = photo.convertToFormat(QImage::Format_ARGB32);
    _status->setText(tr("%1 x %2 px").arg(photo.width()).arg(photo.height()));
    _canvas->update();
}

void HotspotEditor::placeAt(const QPointF& design)
{
    Hotspot* spot = find(selectedControl());
    if (spot == nullptr)
        return;
    spot->centre = QPointF(std::clamp(design.x(), 0.0, kDesignWidth), std::clamp(design.y(), 0.0, kDesignHeight));
    spot->left = spot->centre.x() < kDesignWidth / 2;  // proposed; the combo changes it
    spot->placed = true;
    // On to the next control still to place, so a pad is mapped click after click
    const auto it = std::find_if(_hotspots.begin(), _hotspots.end(), [](const Hotspot& s) { return !s.placed; });
    const std::string next = it != _hotspots.end() ? it->control : spot->control;
    rebuildList();
    selectControl(next);
}

void HotspotEditor::setLive(const std::map<std::string, float>& live)
{
    for (const auto& [control, value] : live)
    {
        const auto before = _lastLive.find(control);
        const float was = before != _lastLive.end() ? before->second : 0.0f;
        if (std::abs(value) < 0.5f || std::abs(was) >= 0.5f)
            continue;
        // A stick's axes select the stick (placed by its x); a direction of the D-pad its own row
        std::string name = control;
        if (name == "lstick.y" || name == "rstick.y")
            name = name.substr(0, 6) + ".x";
        if (find(name) != nullptr && name != selectedControl())
        {
            selectControl(name);
            break;
        }
    }
    _lastLive = live;
}

std::string HotspotEditor::entryFamily() const
{
    if (_target.family != "generic" && !_target.family.empty())
        return _target.family;
    std::string vid;
    std::string pid;
    return UsbIds(_target.device, vid, pid) ? "user-" + vid + pid : std::string{};
}

std::string HotspotEditor::imageSet() const
{
    const std::string family = entryFamily();
    if (family.empty())
        return {};
    return family.rfind("user-", 0) == 0 ? family : "user-" + family;
}

std::string HotspotEditor::entryText() const
{
    QJsonObject entry;
    const std::string family = entryFamily();
    entry["id"] = QString::fromStdString(family);
    entry["images"] = QString::fromStdString(imageSet());
    if (_target.family == "generic" || _target.family.empty())
    {
        // A device of its own: matched by its USB ids from the next connection on
        std::string vid;
        std::string pid;
        UsbIds(_target.device, vid, pid);
        entry["name"] = QString::fromStdString(_target.name);
        QJsonObject match;
        match["vid"] = QString::fromStdString(vid);
        match["pid"] = QJsonArray{QString::fromStdString(pid)};
        entry["match"] = QJsonArray{match};
        QJsonArray controls;
        for (const std::string& control : _target.controls.empty() ? PadControls() : _target.controls)
            controls.append(QString::fromStdString(control));
        entry["controls"] = controls;
    }
    return QJsonDocument(entry).toJson(QJsonDocument::Compact).toStdString();
}

QByteArray HotspotEditor::mapText() const
{
    QJsonArray controls;
    int order = 0;
    for (const Hotspot& spot : _hotspots)
    {
        if (!spot.placed)
            continue;
        QJsonObject control;
        control["control"] = QString::fromStdString(spot.control);
        control["label"] = spot.label;
        QJsonObject shape;
        const double x = std::round(spot.centre.x());
        const double y = std::round(spot.centre.y());
        const double size = std::round(spot.size);
        if (spot.circle)
            shape["circle"] = QJsonArray{x, y, size};
        else
        {
            const double h = std::round(spot.size * 2 * spot.aspect);
            shape["rect"] = QJsonArray{x - size, std::round(y - h / 2), 2 * size, h};
        }
        control["shape"] = shape;
        control["anchor"] = QJsonArray{x, y};
        control["callout"] = spot.left ? "left" : "right";
        if (spot.control == "lt" || spot.control == "rt")
            control["analog"] = QJsonObject{{"kind", "trigger"}, {"fill", "down"}};
        else if (spot.control == "lstick.x" || spot.control == "rstick.x")
            control["analog"] = QJsonObject{{"kind", "stick"},
                                            {"pair", QString::fromStdString(spot.control.substr(0, 6) + ".y")},
                                            {"radius", std::round(spot.size * 0.65)}};
        const std::string group = spot.control.rfind("dpad.", 0) == 0 ? "dpad"
                                  : spot.control.rfind("lstick.", 0) == 0 ? "lstick"
                                  : spot.control.rfind("rstick.", 0) == 0 ? "rstick"
                                                                          : std::string{};
        if (!group.empty())
            control["group"] = QString::fromStdString(group);
        control["order"] = order++;
        controls.append(control);
    }
    QJsonObject doc;
    doc["schema"] = 1;
    doc["family"] = QString::fromStdString(entryFamily());
    doc["view"] = "front";
    doc["size"] = QJsonArray{kDesignWidth, kDesignHeight};
    doc["controls"] = controls;
    return QJsonDocument(doc).toJson(QJsonDocument::Indented);
}

bool HotspotEditor::save(QString& error)
{
    const QString userDir = _hooks.userDir;
    if (userDir.isEmpty() || !_hooks.save)
    {
        error = tr("No device database to record the picture in");
        return false;
    }
    if (_photo.isNull())
    {
        error = tr("Choose a photo first");
        return false;
    }
    if (entryFamily().empty())
    {
        error = tr("This device reports no USB ids: it cannot have an entry of its own");
        return false;
    }
    if (std::none_of(_hotspots.begin(), _hotspots.end(), [](const Hotspot& spot) { return spot.placed; }))
    {
        error = tr("Place at least one control");
        return false;
    }
    const QString dir = QDir(userDir).filePath("images/" + QString::fromStdString(imageSet()));
    if (!QDir().mkpath(dir))
    {
        error = tr("Cannot create %1").arg(dir);
        return false;
    }
    // The photo kept as it is, only a very large one scaled down (the design canvas is 2048 x 1536)
    QImage photo = _photo;
    if (photo.width() > 2 * kDesignWidth || photo.height() > 2 * kDesignHeight)
        photo = photo.scaled(static_cast<int>(2 * kDesignWidth), static_cast<int>(2 * kDesignHeight),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QFile::remove(QDir(dir).filePath("front.svg"));  // a photo set: front.svg would be read first
    QFile::remove(QDir(dir).filePath("front.jpg"));
    if (!photo.save(QDir(dir).filePath("front.png"), "PNG"))
    {
        error = tr("Cannot write the photo into %1").arg(dir);
        return false;
    }
    QSaveFile map(QDir(dir).filePath("front.hotspots.json"));
    if (!map.open(QIODevice::WriteOnly) || map.write(mapText()) < 0 || !map.commit())
    {
        error = tr("Cannot write the hotspot map into %1").arg(dir);
        return false;
    }
    return _hooks.save(entryText(), error);
}
