#include "hotspoteditor.h"

#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

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
        spot.shape = HotspotEditor::Shape::Rect;
        spot.size = 110;
        spot.aspect = control[1] == 't' ? 0.5 : 0.25;
    }
    else if (control.rfind("dpad.", 0) == 0 || control.rfind("paddle.", 0) == 0)
    {
        spot.shape = HotspotEditor::Shape::Rect;
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

/// A folder name from a device id: [a-z0-9._-] kept, anything else '-'
std::string SetName(const std::string& id)
{
    std::string name;
    for (char c : id)
    {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        name += (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '.' || lower == '_' ||
                        lower == '-'
                    ? lower
                    : '-';
    }
    return name == "." || name == ".." ? std::string{} : name;
}

/// A hotspot's outline in design space
QPainterPath PathOf(const HotspotEditor::Hotspot& spot)
{
    QPainterPath path;
    const double w = spot.size;
    switch (spot.shape)
    {
        case HotspotEditor::Shape::Circle:
            path.addEllipse(spot.centre, w, w);
            break;
        case HotspotEditor::Shape::Ellipse:
            path.addEllipse(spot.centre, w, w * spot.aspect);
            break;
        case HotspotEditor::Shape::Rect:
            path.addRoundedRect(QRectF(spot.centre.x() - w, spot.centre.y() - w * spot.aspect, 2 * w, 2 * w * spot.aspect),
                                8, 8);
            break;
        case HotspotEditor::Shape::Polygon:
            path.addPolygon(spot.polygon);
            path.closeSubpath();
            break;
    }
    return path;
}
}  // namespace

/// The photo with the hotspots over it; a click reports the design-space point, a drag in crop mode the rectangle
class HotspotCanvas : public QWidget
{
public:
    explicit HotspotCanvas(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(480, 360);
        setCursor(Qt::CrossCursor);
    }
    std::function<void(const QPointF&)> clicked;
    std::function<void(const QRectF&)> cropped;
    const QImage* photo = nullptr;
    const std::vector<HotspotEditor::Hotspot>* spots = nullptr;
    const QPolygonF* pending = nullptr;
    std::string selected;
    bool cropMode = false;

    QRectF canvasRect() const
    {
        const double scale = std::min(width() / kDesignWidth, height() / kDesignHeight);
        const double w = kDesignWidth * scale;
        const double h = kDesignHeight * scale;
        return {(width() - w) / 2, (height() - h) / 2, w, h};
    }

protected:
    QPointF toDesign(const QPointF& point) const
    {
        const QRectF canvas = canvasRect();
        const double scale = canvas.width() / kDesignWidth;
        return {(point.x() - canvas.left()) / scale, (point.y() - canvas.top()) / scale};
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF canvas = canvasRect();
        painter.fillRect(canvas, palette().color(QPalette::Base));
        if (photo == nullptr || photo->isNull())
        {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(canvas, Qt::AlignCenter, tr("Choose a photo of this side of the pad, or paste one"));
            return;
        }
        const double scale = canvas.width() / kDesignWidth;
        const QRectF fit = PhotoRect(*photo);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(canvas.left() + fit.left() * scale, canvas.top() + fit.top() * scale,
                                 fit.width() * scale, fit.height() * scale),
                          *photo);
        QTransform toWidget;
        toWidget.translate(canvas.left(), canvas.top());
        toWidget.scale(scale, scale);
        if (spots != nullptr)
            for (const HotspotEditor::Hotspot& spot : *spots)
            {
                if (!spot.placed)
                    continue;
                const bool on = spot.control == selected;
                const QPainterPath path = toWidget.map(PathOf(spot));
                painter.setPen(QPen(QColor(0, 0, 0, 120), 3.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
                painter.setPen(QPen(on ? QColor(0, 140, 255) : QColor(255, 255, 255, 200), on ? 2.0 : 1.2));
                painter.setBrush(on ? QColor(0, 140, 255, 60) : QColor(Qt::transparent));
                painter.drawPath(path);
                // The callout side: a short tick towards its column
                // from the outline where the centre's row meets it (a polygon's corner may stick out past it)
                const QRectF box = path.boundingRect();
                const double y = toWidget.map(spot.centre).y();
                QPainterPath row;
                row.addRect(QRectF(box.left() - 1, y - 0.5, box.width() + 2, 1));
                const QRectF hit = path.intersected(row).boundingRect();
                const QRectF edge = hit.isEmpty() ? box : hit;
                painter.drawLine(QPointF(spot.left ? edge.left() : edge.right(), y),
                                 QPointF(spot.left ? edge.left() - 10 : edge.right() + 10, y));
            }
        // The polygon being drawn: its points and the edges so far
        if (pending != nullptr && !pending->isEmpty())
        {
            const QPolygonF points = toWidget.map(*pending);
            painter.setPen(QPen(QColor(0, 140, 255), 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(points);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 140, 255));
            for (const QPointF& point : points)
                painter.drawEllipse(point, 3.0, 3.0);
        }
        if (_dragging)
        {
            painter.setPen(QPen(QColor(0, 140, 255), 1.5, Qt::DashLine));
            painter.setBrush(QColor(0, 140, 255, 30));
            painter.drawRect(QRectF(_from, _to).normalized());
        }
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        const QRectF canvas = canvasRect();
        if (canvas.isEmpty() || !canvas.contains(event->position()))
            return;
        if (cropMode)
        {
            _dragging = true;
            _from = _to = event->position();
            return;
        }
        if (clicked)
            clicked(toDesign(event->position()));
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!_dragging)
            return;
        _to = event->position();
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!_dragging)
            return;
        _dragging = false;
        _to = event->position();
        const QRectF design = QRectF(toDesign(_from), toDesign(_to)).normalized();
        update();
        if (cropped && design.width() > 16 && design.height() > 16)
            cropped(design);
    }

private:
    bool _dragging = false;
    QPointF _from;
    QPointF _to;
};

const QStringList& HotspotEditor::Views()
{
    static const QStringList views = {QStringLiteral("front"), QStringLiteral("back"), QStringLiteral("top")};
    return views;
}

HotspotEditor::HotspotEditor(const Target& target, Hooks hooks, QWidget* parent)
    : QDialog(parent), _target(target), _hooks(std::move(hooks))
{
    setWindowTitle(tr("Picture of %1").arg(QString::fromStdString(target.name)));
    resize(1040, 660);
    auto* root = new QHBoxLayout(this);
    _canvas = new HotspotCanvas(this);
    _canvas->pending = &_pending;
    _canvas->clicked = [this](const QPointF& point) { placeAt(point); };
    _canvas->cropped = [this](const QRectF& design) {
        cropTo(design);
        _crop->setChecked(false);
    };
    root->addWidget(_canvas, 1);

    auto* side = new QVBoxLayout();
    auto* viewRow = new QHBoxLayout();
    viewRow->addWidget(new QLabel(tr("View"), this));
    _viewBox = new QComboBox(this);
    _viewBox->addItem(tr("Front"), QStringLiteral("front"));
    _viewBox->addItem(tr("Back"), QStringLiteral("back"));
    _viewBox->addItem(tr("Top"), QStringLiteral("top"));
    _viewBox->setToolTip(tr("Front: the face of the pad. Back: paddles and triggers. Top: shoulders and ports"));
    viewRow->addWidget(_viewBox, 1);
    side->addLayout(viewRow);
    auto* pick = new QHBoxLayout();
    auto* choose = new QPushButton(tr("Choose photo…"), this);
    auto* paste = new QPushButton(tr("Paste"), this);
    pick->addWidget(choose);
    pick->addWidget(paste);
    side->addLayout(pick);
    // Straighten and crop: before the spots, as a turn or a crop moves everything on the photo
    auto* adjust = new QHBoxLayout();
    auto* turnLeft = new QPushButton(QStringLiteral("⟲"), this);
    turnLeft->setToolTip(tr("Turn a quarter left"));
    auto* turnRight = new QPushButton(QStringLiteral("⟳"), this);
    turnRight->setToolTip(tr("Turn a quarter right"));
    _angle = new QDoubleSpinBox(this);
    _angle->setRange(-15.0, 15.0);
    _angle->setSingleStep(0.5);
    _angle->setDecimals(1);
    _angle->setSuffix(QStringLiteral("°"));
    _angle->setToolTip(tr("Straighten: level the pad's horizontal axis"));
    _crop = new QPushButton(tr("Crop"), this);
    _crop->setCheckable(true);
    _crop->setToolTip(tr("Drag a rectangle on the photo to keep only it"));
    auto* uncrop = new QPushButton(tr("Uncrop"), this);
    uncrop->setToolTip(tr("Back to the whole photo"));
    for (QPushButton* button : {turnLeft, turnRight})
        button->setFixedWidth(34);
    adjust->addWidget(turnLeft);
    adjust->addWidget(turnRight);
    adjust->addWidget(_angle);
    adjust->addWidget(_crop);
    adjust->addWidget(uncrop);
    side->addLayout(adjust);
    auto* hint = new QLabel(tr("Select a control (or press it on the pad), then click its spot on the photo. "
                               "A polygon: click its corners, then Finish."),
                            this);
    hint->setWordWrap(true);
    side->addWidget(hint);
    _list = new QListWidget(this);
    side->addWidget(_list, 1);
    auto* shapeRow = new QHBoxLayout();
    shapeRow->addWidget(new QLabel(tr("Shape"), this));
    _shapeBox = new QComboBox(this);
    _shapeBox->addItem(tr("Circle"), static_cast<int>(Shape::Circle));
    _shapeBox->addItem(tr("Ellipse"), static_cast<int>(Shape::Ellipse));
    _shapeBox->addItem(tr("Rectangle"), static_cast<int>(Shape::Rect));
    _shapeBox->addItem(tr("Polygon"), static_cast<int>(Shape::Polygon));
    shapeRow->addWidget(_shapeBox, 1);
    _finish = new QPushButton(tr("Finish"), this);
    _finish->setToolTip(tr("Close the polygon"));
    _finish->setEnabled(false);
    shapeRow->addWidget(_finish);
    side->addLayout(shapeRow);
    auto* form = new QHBoxLayout();
    form->addWidget(new QLabel(tr("Size"), this));
    _size = new QSpinBox(this);
    _size->setRange(10, 400);
    _size->setSuffix(QStringLiteral(" px"));
    _size->setToolTip(tr("The radius of a circle or an ellipse, the half width of a rectangle (design space: "
                         "2048 x 1536)"));
    form->addWidget(_size);
    form->addWidget(new QLabel(tr("Height"), this));
    _aspect = new QSpinBox(this);
    _aspect->setRange(10, 400);
    _aspect->setSuffix(QStringLiteral(" %"));
    _aspect->setToolTip(tr("An ellipse's or a rectangle's height as a share of its width"));
    form->addWidget(_aspect);
    side->addLayout(form);
    auto* calloutRow = new QHBoxLayout();
    calloutRow->addWidget(new QLabel(tr("Callout"), this));
    _callout = new QComboBox(this);
    _callout->addItem(tr("Left"), true);
    _callout->addItem(tr("Right"), false);
    calloutRow->addWidget(_callout, 1);
    auto* remove = new QPushButton(tr("Remove spot"), this);
    calloutRow->addWidget(remove);
    side->addLayout(calloutRow);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    side->addWidget(_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* share = buttons->addButton(tr("Share…"), QDialogButtonBox::ActionRole);
    share->setToolTip(tr("Export the set as a folder (photos, maps, a provenance stub to fill in, the device entry) "
                         "for a pull request"));
    side->addWidget(buttons);
    root->addLayout(side);

    // The controls: the database's, the wildcards expanded; a stick is placed by its x axis (its y is the pair)
    for (const std::string& entry : target.controls.empty() ? PadControls() : target.controls)
    {
        if (entry == "dpad.*")
            _controls.insert(_controls.end(), {"dpad.up", "dpad.down", "dpad.left", "dpad.right"});
        else if (entry == "lstick.*" || entry == "rstick.*")
        {
            const std::string stick = entry.substr(0, 6);
            _controls.insert(_controls.end(), {stick + ".x", stick + ".click"});
        }
        else if (entry.find('*') == std::string::npos && entry != "profile")
            _controls.push_back(entry);
    }
    // The picture shown now is the starting point: its positions per view, and its photos when they are ones
    const QString setDir = ControllerPicture::ImageSetDir(QString::fromStdString(target.imageSet));
    for (const QString& view : Views())
        loadView(setDir, view);
    _canvas->photo = &current().photo;
    _canvas->spots = &current().spots;
    rebuildList();

    connect(_viewBox, &QComboBox::currentIndexChanged, this, [this] {
        if (!_updating)
            setView(_viewBox->currentData().toString());
    });
    connect(_list, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
    connect(_shapeBox, &QComboBox::currentIndexChanged, this, [this] {
        if (!_updating)
            setShape(static_cast<Shape>(_shapeBox->currentData().toInt()));
    });
    connect(_finish, &QPushButton::clicked, this, [this] { finishPolygon(); });
    connect(_size, &QSpinBox::valueChanged, this, [this](int value) {
        if (Hotspot* spot = find(selectedControl()); spot != nullptr && !_updating && spot->shape != Shape::Polygon)
        {
            spot->size = value;
            _canvas->update();
        }
    });
    connect(_aspect, &QSpinBox::valueChanged, this, [this](int value) {
        if (Hotspot* spot = find(selectedControl()); spot != nullptr && !_updating && spot->shape != Shape::Polygon)
        {
            spot->aspect = value / 100.0;
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
    connect(turnLeft, &QPushButton::clicked, this, [this] { rotateQuarter(-1); });
    connect(turnRight, &QPushButton::clicked, this, [this] { rotateQuarter(1); });
    connect(_angle, &QDoubleSpinBox::valueChanged, this, [this](double degrees) {
        if (!_updating)
            setStraighten(degrees);
    });
    connect(_crop, &QPushButton::toggled, this, [this](bool on) { _canvas->cropMode = on; });
    connect(uncrop, &QPushButton::clicked, this, [this] { resetCrop(); });
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
    connect(share, &QPushButton::clicked, this, [this] {
        const QString parent = QFileDialog::getExistingDirectory(this, tr("Export the picture set into"));
        if (parent.isEmpty())
            return;
        QString error;
        if (exportBundle(parent, error))
            _status->setText(tr("Exported to %1: fill in provenance.json before the pull request")
                                 .arg(QDir(parent).filePath(QString::fromStdString(imageSet()))));
        else
            _status->setText(error);
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

HotspotEditor::ViewData& HotspotEditor::current()
{
    ViewData& data = _views[_view];
    if (data.spots.empty())
        for (const std::string& control : _controls)
            data.spots.push_back(Default(control));
    return data;
}

const HotspotEditor::ViewData& HotspotEditor::current() const
{
    return const_cast<HotspotEditor*>(this)->current();
}

const QImage& HotspotEditor::photo() const
{
    return current().photo;
}

const std::vector<HotspotEditor::Hotspot>& HotspotEditor::hotspots() const
{
    return current().spots;
}

HotspotEditor::Hotspot* HotspotEditor::find(const std::string& control)
{
    for (Hotspot& spot : current().spots)
        if (spot.control == control)
            return &spot;
    return nullptr;
}

void HotspotEditor::setView(const QString& view)
{
    if (!Views().contains(view))
        return;
    _view = view;
    _pending.clear();
    _canvas->photo = &current().photo;
    _canvas->spots = &current().spots;
    _updating = true;
    _viewBox->setCurrentIndex(std::max(0, _viewBox->findData(view)));
    _angle->setValue(current().angle);
    _updating = false;
    rebuildList();
}

void HotspotEditor::loadView(const QString& setDir, const QString& view)
{
    if (setDir.isEmpty())
        return;
    const QString saved = _view;
    _view = view;
    ViewData& data = current();
    for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg")})
        if (data.source.isNull() && QFile::exists(setDir + view + extension))
            data.source = QImage(setDir + view + extension).convertToFormat(QImage::Format_ARGB32);
    derive(data);
    QFile file(setDir + view + ".hotspots.json");
    if (file.open(QIODevice::ReadOnly))
    {
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
                spot->shape = Shape::Circle;
                spot->centre = QPointF(c.at(0).toDouble() * sx, c.at(1).toDouble() * sy);
                spot->size = c.at(2).toDouble() * sx;
            }
            else if (shape.contains("ellipse"))
            {
                const QJsonArray e = shape.value("ellipse").toArray();
                spot->shape = Shape::Ellipse;
                spot->centre = QPointF(e.at(0).toDouble() * sx, e.at(1).toDouble() * sy);
                spot->size = e.at(2).toDouble() * sx;
                spot->aspect = spot->size > 0 ? e.at(3).toDouble() * sy / spot->size : 1.0;
            }
            else if (shape.contains("rect"))
            {
                const QJsonArray r = shape.value("rect").toArray();
                const double w = r.at(2).toDouble() * sx;
                const double h = r.at(3).toDouble() * sy;
                spot->shape = Shape::Rect;
                spot->centre = QPointF(r.at(0).toDouble() * sx + w / 2, r.at(1).toDouble() * sy + h / 2);
                spot->size = w / 2;
                spot->aspect = w > 0 ? h / w : 1.0;
            }
            else if (shape.contains("polygon"))
            {
                QPolygonF polygon;
                for (const QJsonValue& point : shape.value("polygon").toArray())
                    polygon << QPointF(point.toArray().at(0).toDouble() * sx, point.toArray().at(1).toDouble() * sy);
                if (polygon.size() < 3)
                    continue;
                spot->shape = Shape::Polygon;
                spot->polygon = polygon;
                spot->centre = polygon.boundingRect().center();
            }
            else
                continue;
            spot->label = control.value("label").toString(spot->label);
            spot->left = control.value("callout").toString("left") == "left";
            spot->placed = true;
            data.loaded = true;
        }
    }
    _view = saved;
}

void HotspotEditor::derive(ViewData& data)
{
    if (data.source.isNull())
    {
        data.photo = QImage();
        return;
    }
    QImage turned = data.source;
    if (data.quarter != 0 || std::abs(data.angle) > 1e-6)
    {
        QTransform transform;
        transform.rotate(data.quarter * 90.0 + data.angle);
        turned = data.source.transformed(transform, Qt::SmoothTransformation);
    }
    const QRect crop = QRectF(data.crop.x() * turned.width(), data.crop.y() * turned.height(),
                              data.crop.width() * turned.width(), data.crop.height() * turned.height())
                           .toAlignedRect()
                           .intersected(turned.rect());
    data.photo = crop == turned.rect() || crop.isEmpty() ? turned : turned.copy(crop);
}

void HotspotEditor::dropPlaced(ViewData& data)
{
    const bool any = std::any_of(data.spots.begin(), data.spots.end(), [](const Hotspot& spot) { return spot.placed; });
    for (Hotspot& spot : data.spots)
        spot.placed = false;
    _pending.clear();
    if (any)
        _status->setText(tr("The photo moved: place this view's controls again"));
}

void HotspotEditor::setPhoto(const QImage& photo)
{
    ViewData& data = current();
    data.source = photo.convertToFormat(QImage::Format_ARGB32);
    data.quarter = 0;
    data.angle = 0;
    data.crop = QRectF(0, 0, 1, 1);
    derive(data);
    _updating = true;
    _angle->setValue(0);
    _updating = false;
    _status->setText(tr("%1 x %2 px").arg(photo.width()).arg(photo.height()));
    _canvas->photo = &data.photo;
    _canvas->update();
}

void HotspotEditor::rotateQuarter(int turns)
{
    ViewData& data = current();
    if (data.source.isNull())
        return;
    data.quarter = ((data.quarter + turns) % 4 + 4) % 4;
    data.crop = QRectF(0, 0, 1, 1);  // the crop was of the other orientation
    derive(data);
    dropPlaced(data);
    rebuildList();
}

void HotspotEditor::setStraighten(double degrees)
{
    ViewData& data = current();
    data.angle = std::clamp(degrees, -15.0, 15.0);
    if (data.source.isNull())
        return;
    derive(data);
    dropPlaced(data);
    rebuildList();
}

void HotspotEditor::cropTo(const QRectF& design)
{
    ViewData& data = current();
    if (data.photo.isNull())
        return;
    const QRectF fit = PhotoRect(data.photo);
    const QRectF inside = design.intersected(fit);
    if (inside.width() < 1 || inside.height() < 1)
        return;
    // The part of the current photo, then of the turned image the crop is kept against
    const QRectF part((inside.left() - fit.left()) / fit.width(), (inside.top() - fit.top()) / fit.height(),
                      inside.width() / fit.width(), inside.height() / fit.height());
    data.crop = QRectF(data.crop.left() + part.left() * data.crop.width(),
                       data.crop.top() + part.top() * data.crop.height(), part.width() * data.crop.width(),
                       part.height() * data.crop.height());
    derive(data);
    dropPlaced(data);
    _status->setText(tr("Cropped to %1 x %2 px").arg(data.photo.width()).arg(data.photo.height()));
    rebuildList();
}

void HotspotEditor::resetCrop()
{
    ViewData& data = current();
    if (data.crop == QRectF(0, 0, 1, 1))
        return;
    data.crop = QRectF(0, 0, 1, 1);
    derive(data);
    dropPlaced(data);
    rebuildList();
}

void HotspotEditor::rebuildList()
{
    const std::string kept = selectedControl();
    _updating = true;
    _list->clear();
    for (const Hotspot& spot : current().spots)
    {
        auto* item = new QListWidgetItem(
            QString("%1  %2%3").arg(spot.label, QString::fromStdString(spot.control), spot.placed ? QString() : " —"),
            _list);
        item->setData(Qt::UserRole, QString::fromStdString(spot.control));
        if (!spot.placed)
            item->setForeground(palette().color(QPalette::PlaceholderText));
    }
    _updating = false;
    const std::vector<Hotspot>& spots = current().spots;
    selectControl(kept.empty() && !spots.empty() ? spots.front().control : kept);
    _canvas->update();
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
    _pending.clear();
    _finish->setEnabled(false);
    if (const Hotspot* spot = find(control))
    {
        _updating = true;
        _shape = spot->shape;
        _shapeBox->setCurrentIndex(_shapeBox->findData(static_cast<int>(spot->shape)));
        _size->setValue(static_cast<int>(std::lround(spot->size)));
        _aspect->setValue(static_cast<int>(std::lround(spot->aspect * 100)));
        const bool sized = spot->shape != Shape::Polygon;
        _size->setEnabled(sized);
        _aspect->setEnabled(sized && spot->shape != Shape::Circle);
        _callout->setCurrentIndex(spot->left ? 0 : 1);
        _updating = false;
    }
    _canvas->update();
}

void HotspotEditor::setShape(Shape shape)
{
    _shape = shape;
    _pending.clear();
    _finish->setEnabled(false);
    if (Hotspot* spot = find(selectedControl()))
    {
        if (spot->shape == Shape::Polygon && shape != Shape::Polygon && !spot->polygon.isEmpty())
        {
            // From a polygon: the shape keeps its box
            const QRectF box = spot->polygon.boundingRect();
            spot->centre = box.center();
            spot->size = std::max(10.0, box.width() / 2);
            spot->aspect = box.width() > 0 ? box.height() / box.width() : 1.0;
        }
        if (shape == Shape::Polygon && spot->shape != Shape::Polygon)
            spot->placed = false;  // drawn anew, corner by corner
        if (shape == Shape::Circle)
            spot->aspect = 1.0;
        spot->shape = shape;
    }
    _updating = true;
    _shapeBox->setCurrentIndex(_shapeBox->findData(static_cast<int>(shape)));
    _updating = false;
    showSelected();
    rebuildList();
}

HotspotEditor::Shape HotspotEditor::shape() const
{
    return _shape;
}

void HotspotEditor::placeAt(const QPointF& design)
{
    Hotspot* spot = find(selectedControl());
    if (spot == nullptr)
        return;
    const QPointF point(std::clamp(design.x(), 0.0, kDesignWidth), std::clamp(design.y(), 0.0, kDesignHeight));
    if (spot->shape == Shape::Polygon)
    {
        // A click near the first corner closes it; Finish does too
        if (_pending.size() >= 3 && QLineF(_pending.front(), point).length() < 24.0)
        {
            finishPolygon();
            return;
        }
        _pending << point;
        _finish->setEnabled(_pending.size() >= 3);
        _canvas->update();
        return;
    }
    spot->centre = point;
    spot->left = spot->centre.x() < kDesignWidth / 2;  // proposed; the combo changes it
    spot->placed = true;
    // On to the next control still to place, so a pad is mapped click after click
    const std::vector<Hotspot>& spots = current().spots;
    const auto it = std::find_if(spots.begin(), spots.end(), [](const Hotspot& s) { return !s.placed; });
    const std::string next = it != spots.end() ? it->control : spot->control;
    rebuildList();
    selectControl(next);
}

bool HotspotEditor::placePolygon(const QPolygonF& points)
{
    Hotspot* spot = find(selectedControl());
    if (spot == nullptr || points.size() < 3)
        return false;
    spot->shape = Shape::Polygon;
    spot->polygon = points;
    spot->centre = points.boundingRect().center();
    spot->left = spot->centre.x() < kDesignWidth / 2;
    spot->placed = true;
    _pending.clear();
    const std::vector<Hotspot>& spots = current().spots;
    const auto it = std::find_if(spots.begin(), spots.end(), [](const Hotspot& s) { return !s.placed; });
    const std::string next = it != spots.end() ? it->control : spot->control;
    rebuildList();
    selectControl(next);
    return true;
}

bool HotspotEditor::finishPolygon()
{
    const QPolygonF points = _pending;
    return placePolygon(points);
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
    // device-database.md §9: images/<device id>/, the database entry's id
    return SetName(entryFamily());
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

QByteArray HotspotEditor::mapText(const QString& view) const
{
    QJsonArray controls;
    int order = 0;
    const auto data = _views.find(view);
    const std::vector<Hotspot> none;
    for (const Hotspot& spot : data != _views.end() ? data->second.spots : none)
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
        switch (spot.shape)
        {
            case Shape::Circle:
                shape["circle"] = QJsonArray{x, y, size};
                break;
            case Shape::Ellipse:
                shape["ellipse"] = QJsonArray{x, y, size, std::round(spot.size * spot.aspect)};
                break;
            case Shape::Rect:
            {
                const double h = std::round(spot.size * 2 * spot.aspect);
                shape["rect"] = QJsonArray{x - size, std::round(y - h / 2), 2 * size, h};
                break;
            }
            case Shape::Polygon:
            {
                QJsonArray points;
                for (const QPointF& point : spot.polygon)
                    points.append(QJsonArray{std::round(point.x()), std::round(point.y())});
                shape["polygon"] = points;
                break;
            }
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
    doc["view"] = view;
    doc["size"] = QJsonArray{kDesignWidth, kDesignHeight};
    doc["controls"] = controls;
    return QJsonDocument(doc).toJson(QJsonDocument::Indented);
}

bool HotspotEditor::writeViews(const QString& dir, QString& error) const
{
    if (entryFamily().empty() || imageSet().empty())
    {
        error = tr("This device reports no USB ids: it cannot have an entry of its own");
        return false;
    }
    const auto front = _views.find(QStringLiteral("front"));
    if (front == _views.end() || front->second.photo.isNull())
    {
        error = tr("Choose a photo of the front first");
        return false;
    }
    if (std::none_of(front->second.spots.begin(), front->second.spots.end(),
                     [](const Hotspot& spot) { return spot.placed; }))
    {
        error = tr("Place at least one control on the front");
        return false;
    }
    if (!QDir().mkpath(dir))
    {
        error = tr("Cannot create %1").arg(dir);
        return false;
    }
    for (const auto& [view, data] : _views)
    {
        const bool placed =
            std::any_of(data.spots.begin(), data.spots.end(), [](const Hotspot& spot) { return spot.placed; });
        if (data.photo.isNull() || !placed)
            continue;  // a view without a photo or a spot is not written
        // The photo kept as it is, only a very large one scaled down (the design canvas is 2048 x 1536)
        QImage photo = data.photo;
        if (photo.width() > 2 * kDesignWidth || photo.height() > 2 * kDesignHeight)
            photo = photo.scaled(static_cast<int>(2 * kDesignWidth), static_cast<int>(2 * kDesignHeight),
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QFile::remove(QDir(dir).filePath(view + ".svg"));  // a photo set: <view>.svg would be read first
        QFile::remove(QDir(dir).filePath(view + ".jpg"));
        if (!photo.save(QDir(dir).filePath(view + ".png"), "PNG"))
        {
            error = tr("Cannot write the photo into %1").arg(dir);
            return false;
        }
        QSaveFile map(QDir(dir).filePath(view + ".hotspots.json"));
        if (!map.open(QIODevice::WriteOnly) || map.write(mapText(view)) < 0 || !map.commit())
        {
            error = tr("Cannot write the hotspot map into %1").arg(dir);
            return false;
        }
    }
    return true;
}

bool HotspotEditor::save(QString& error)
{
    const QString userDir = _hooks.userDir;
    if (userDir.isEmpty() || !_hooks.save)
    {
        error = tr("No device database to record the picture in");
        return false;
    }
    if (!writeViews(QDir(userDir).filePath("images/" + QString::fromStdString(imageSet())), error))
        return false;
    return _hooks.save(entryText(), error);
}

bool HotspotEditor::exportBundle(const QString& parent, QString& error) const
{
    const QString dir = QDir(parent).filePath(QString::fromStdString(imageSet()));
    if (!writeViews(dir, error))
        return false;
    // device-database.md §7: the record a pull request needs, with what only the user knows left to fill in
    QJsonArray images;
    for (const auto& [view, data] : _views)
        if (!data.photo.isNull() &&
            std::any_of(data.spots.begin(), data.spots.end(), [](const Hotspot& spot) { return spot.placed; }))
            images.append(QJsonObject{{"view", view},
                                      {"source", "own photograph"},
                                      {"author", ""},
                                      {"licence", ""},
                                      {"terms", ""},
                                      {"retrieved", QDate::currentDate().toString(Qt::ISODate)},
                                      {"retouch", ""},
                                      {"retouchedBy", ""}});
    const QJsonObject provenance{{"family", QString::fromStdString(entryFamily())}, {"images", images}};
    const QByteArray entry =
        QJsonDocument(QJsonDocument::fromJson(QByteArray::fromStdString(entryText())).object()).toJson();
    for (const auto& [name, text] : {std::pair<QString, QByteArray>{"provenance.json", QJsonDocument(provenance).toJson()},
                                     std::pair<QString, QByteArray>{"entry.json", entry}})
    {
        QSaveFile file(QDir(dir).filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        {
            error = tr("Cannot write %1 into %2").arg(name, dir);
            return false;
        }
    }
    return true;
}
