#include "controllerpicture.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSvgRenderer>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include "common/filehelper.h"

namespace
{
constexpr int kCalloutWidth = 170;  // each side column, widget pixels
constexpr float kPressed = 0.5f;
constexpr double kPi = 3.14159265358979323846;  // M_PI is not standard (MSVC)

bool Pressed(float value)
{
    return value >= kPressed || value <= -kPressed;
}

QString& UserDir()
{
    static QString dir;
    return dir;
}
}  // namespace

void ControllerPicture::SetUserDir(const QString& dir)
{
    UserDir() = dir;
}

QString ControllerPicture::UserDirectory()
{
    return UserDir();
}

QStringList ControllerPicture::Views(const QString& imageSet)
{
    const QString root = ImageSetDir(imageSet);
    QStringList views;
    if (root.isEmpty())
        return views;
    for (const char* view : {"front", "back", "top", "three-quarter"})
        if (QFile::exists(root + view + ".hotspots.json"))
            views << QString::fromLatin1(view);
    return views;
}

void ControllerPicture::setCompact(bool compact)
{
    _compact = compact;
    setMinimumSize(compact ? QSize(96, 64) : QSize(420, 240));
    updateGeometry();
    update();
}

void ControllerPicture::setLitColour(const QColor& colour)
{
    _lit = colour;
    update();
}

int ControllerPicture::calloutWidth() const
{
    return _compact ? 0 : kCalloutWidth;
}

QString ControllerPicture::ImageSetDir(const QString& imageSet)
{
    if (imageSet.isEmpty() || imageSet.contains("..") || imageSet.contains('/') || imageSet.contains('\\'))
        return {};
    // The user's own sets (the hotspot editor's photos) first: a set of the same name replaces the shipped one
    QStringList roots;
    if (!UserDir().isEmpty())
        roots << UserDir() + "/images/";
    roots << QString::fromStdString(FileHelper::GetResourcesPath()) + "/controllers/images/";
    for (const QString& root : roots)
        if (QFile::exists(root + imageSet + "/front.hotspots.json"))
            return root + imageSet + "/";
    return {};
}

ControllerPicture::ControllerPicture(QWidget* parent) : QWidget(parent), _svg(new QSvgRenderer(this))
{
    setMinimumSize(420, 240);
}

bool ControllerPicture::load(const QString& imageSet, const QString& view)
{
    _hotspots.clear();
    _size = {};
    _raster = QImage();
    _svg->load(QByteArray());
    _view = view.isEmpty() ? QStringLiteral("front") : view;
    if (_view.contains('/') || _view.contains('\\') || _view.contains(QStringLiteral("..")))
    {
        update();
        return false;  // a view is a name, never a path
    }
    const QString root = ImageSetDir(imageSet);
    QFile json(root + _view + ".hotspots.json");
    // A drawn set is <view>.svg; a photo (the hotspot editor's) <view>.png or <view>.jpg
    bool picture = !root.isEmpty() && QFile::exists(root + _view + ".svg") && _svg->load(root + _view + ".svg");
    for (const char* extension : {".png", ".jpg"})
        if (!picture && !root.isEmpty() && QFile::exists(root + _view + extension))
        {
            _raster = QImage(root + _view + extension);
            picture = !_raster.isNull();
        }
    if (!picture || !json.open(QIODevice::ReadOnly))
    {
        update();
        return false;
    }
    const QJsonObject doc = QJsonDocument::fromJson(json.readAll()).object();
    const QJsonArray size = doc.value("size").toArray();
    _size = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
    for (const QJsonValue& value : doc.value("controls").toArray())
    {
        const QJsonObject control = value.toObject();
        Hotspot spot;
        spot.control = control.value("control").toString().toStdString();
        spot.label = control.value("label").toString(QString::fromStdString(spot.control));
        const QJsonObject shape = control.value("shape").toObject();
        if (shape.contains("circle"))
        {
            const QJsonArray c = shape.value("circle").toArray();  // cx, cy, r
            const double r = c.at(2).toDouble();
            spot.circle = true;
            spot.rect = QRectF(c.at(0).toDouble() - r, c.at(1).toDouble() - r, 2 * r, 2 * r);
            spot.path.addEllipse(spot.rect);
        }
        else if (shape.contains("ellipse"))
        {
            const QJsonArray e = shape.value("ellipse").toArray();  // cx, cy, rx, ry
            const double rx = e.at(2).toDouble();
            const double ry = e.at(3).toDouble();
            spot.circle = true;
            spot.rect = QRectF(e.at(0).toDouble() - rx, e.at(1).toDouble() - ry, 2 * rx, 2 * ry);
            spot.path.addEllipse(spot.rect);
        }
        else if (shape.contains("polygon"))
        {
            QPolygonF polygon;
            for (const QJsonValue& point : shape.value("polygon").toArray())
                polygon << QPointF(point.toArray().at(0).toDouble(), point.toArray().at(1).toDouble());
            if (polygon.size() < 3)
                continue;
            spot.path.addPolygon(polygon);
            spot.path.closeSubpath();
            spot.rect = polygon.boundingRect();
        }
        else
        {
            const QJsonArray r = shape.value("rect").toArray();  // x, y, w, h
            spot.rect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble());
            const double radius = std::min(12.0, std::min(spot.rect.width(), spot.rect.height()) / 4);
            spot.path.addRoundedRect(spot.rect, radius, radius);
        }
        const QJsonArray anchor = control.value("anchor").toArray();
        spot.anchor = anchor.isEmpty() ? spot.rect.center() : QPointF(anchor.at(0).toDouble(), anchor.at(1).toDouble());
        spot.left = control.value("callout").toString("left") == "left";
        const QJsonObject analog = control.value("analog").toObject();
        spot.analog = analog.value("kind").toString();
        spot.pair = analog.value("pair").toString().toStdString();
        spot.radius = analog.value("radius").toDouble();
        spot.steps = analog.value("steps").toDouble();
        spot.quiet = control.value("quiet").toBool();
        _hotspots.push_back(spot);
    }
    if (_size.isEmpty())
        _size = _raster.isNull() ? QSizeF(_svg->defaultSize()) : QSizeF(_raster.size());
    update();
    return !_hotspots.empty();
}

void ControllerPicture::setCaptions(const std::map<std::string, QString>& captions)
{
    _captions = captions;
    update();
}

void ControllerPicture::setLive(const std::map<std::string, float>& live)
{
    if (live == _live)
        return;
    _live = live;
    update();
}

void ControllerPicture::setSelected(const std::string& control)
{
    if (control == _selected)
        return;
    _selected = control;
    update();
}

QSize ControllerPicture::sizeHint() const
{
    return _compact ? QSize(160, 120) : QSize(2 * kCalloutWidth + 420, 330);
}

QRectF ControllerPicture::imageRect() const
{
    const int callout = calloutWidth();
    const QRectF area(callout, _compact ? 0 : 4, width() - 2 * callout, height() - (_compact ? 0 : 8));
    if (_size.isEmpty() || area.width() <= 0 || area.height() <= 0)
        return {};
    const double scale = std::min(area.width() / _size.width(), area.height() / _size.height());
    const QSizeF drawn = _size * scale;
    return {area.center().x() - drawn.width() / 2, area.center().y() - drawn.height() / 2, drawn.width(),
            drawn.height()};
}

QPointF ControllerPicture::toWidget(const QPointF& point) const
{
    const QRectF image = imageRect();
    return {image.left() + point.x() * image.width() / _size.width(),
            image.top() + point.y() * image.height() / _size.height()};
}

QPainterPath ControllerPicture::shapeOf(const Hotspot& spot) const
{
    const QRectF image = imageRect();
    if (image.isEmpty() || _size.isEmpty())
        return {};
    QTransform transform;
    transform.translate(image.left(), image.top());
    transform.scale(image.width() / _size.width(), image.height() / _size.height());
    return transform.map(spot.path);
}

void ControllerPicture::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF image = imageRect();
    if (_hotspots.empty() || image.isEmpty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                         _compact ? tr("No picture") : tr("No picture for this device"));
        return;
    }
    if (_raster.isNull())
        _svg->render(&painter, image);
    else
    {
        // A photo is letterboxed into the design canvas (device-database.md §9), as the hotspot editor placed it
        const double fit = std::min(image.width() / _raster.width(), image.height() / _raster.height());
        const QSizeF drawn(_raster.width() * fit, _raster.height() * fit);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(image.center().x() - drawn.width() / 2, image.center().y() - drawn.height() / 2,
                                 drawn.width(), drawn.height()),
                          _raster);
    }
    const double scale = image.width() / _size.width();
    const QColor lit = _lit;
    const QColor selected(0, 140, 255, 120);

    auto valueOf = [this](const std::string& control) {
        auto it = _live.find(control);
        float value = it != _live.end() ? it->second : 0.0f;
        // A key lights while held with modifiers too: key.ctrl.f10 is the f10 key
        if (control.rfind("key.", 0) == 0)
            for (const auto& [name, level] : _live)
                if (name.rfind("key.", 0) == 0 && name.size() > control.size() &&
                    name.compare(name.size() - control.size() + 3, std::string::npos, control, 3) == 0)
                    value = std::max(value, level);
        return value;
    };

    for (const Hotspot& spot : _hotspots)
    {
        const QRectF box(toWidget(spot.rect.topLeft()), spot.rect.size() * scale);
        const float value = valueOf(spot.control);
        if (spot.analog == "trigger")
        {
            if (value > 0.02f)
            {
                QRectF fill = box;
                fill.setHeight(box.height() * std::clamp(value, 0.0f, 1.0f));
                painter.fillRect(fill, lit);
            }
        }
        else if (spot.analog == "stick")
        {
            // One dot per stick, drawn by its x axis: pushed by x and its pair
            const QPointF centre = box.center();
            const double reach = spot.radius > 0 ? spot.radius * scale : box.width() / 2;
            const QPointF dot(centre.x() + value * reach, centre.y() + valueOf(spot.pair) * reach);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Pressed(value) || Pressed(valueOf(spot.pair)) ? lit : QColor(255, 255, 255, 90));
            painter.drawEllipse(dot, 6, 6);
        }
        else if (spot.analog == "encoder")
        {
            // A dot on the ring where the knob has turned to (the live value is the sum of its steps)
            if (spot.control == _selected)
            {
                painter.setPen(QPen(selected, 8));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(box);
            }
            const double turn = spot.steps > 0 ? value / spot.steps : 0.0;
            const double angle = 2.0 * kPi * (turn - std::floor(turn));
            const double reach = spot.radius > 0 ? spot.radius * scale : box.width() / 2;
            const QPointF dot(box.center().x() + reach * std::sin(angle), box.center().y() - reach * std::cos(angle));
            painter.setPen(Qt::NoPen);
            painter.setBrush(lit);
            painter.drawEllipse(dot, 8, 8);
        }
        else if (Pressed(value) || spot.control == _selected)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Pressed(value) ? lit : selected);
            painter.drawPath(shapeOf(spot));
        }
    }
    if (_compact)
        return;  // a connector card: the picture alone

    // Callouts: each side sorted by the anchor's height, spread evenly over the widget's height
    const QFontMetrics metrics(font());
    const int line = metrics.height() + 2;
    for (bool left : {true, false})
    {
        std::vector<const Hotspot*> side;
        for (const Hotspot& spot : _hotspots)
            if (spot.left == left &&
                (!spot.quiet || _captions.count(spot.control) || spot.control == _selected ||
                 Pressed(valueOf(spot.control))))
                side.push_back(&spot);
        std::stable_sort(side.begin(), side.end(),
                         [](const Hotspot* a, const Hotspot* b) { return a->anchor.y() < b->anchor.y(); });
        if (side.empty())
            continue;
        const double step = std::max<double>(line, (height() - 8.0) / static_cast<double>(side.size()));
        double y = 4 + step / 2;
        for (const Hotspot* spot : side)
        {
            auto caption = _captions.find(spot->control);
            const QString text = spot->label + (caption != _captions.end() && !caption->second.isEmpty()
                                                    ? "  " + caption->second
                                                    : QString());
            const bool active =
                (spot->analog != "encoder" && Pressed(valueOf(spot->control))) || spot->control == _selected;
            const int callout = calloutWidth();
            const QRectF label = left ? QRectF(2, y - line / 2.0, callout - 10, line)
                                      : QRectF(width() - callout + 8, y - line / 2.0, callout - 10, line);
            const QPointF anchor = toWidget(spot->anchor);
            const QPointF end(left ? label.right() + 2 : label.left() - 2, y);
            QColor pen = palette().color(QPalette::Mid);
            if (active)
                pen = QColor(255, 140, 0);
            painter.setPen(QPen(pen, active ? 1.6 : 0.8));
            painter.drawLine(end, anchor);
            painter.setPen(palette().color(active ? QPalette::Highlight : QPalette::WindowText));
            painter.drawText(label, (left ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                             metrics.elidedText(text, Qt::ElideRight, static_cast<int>(label.width())));
            y += step;
        }
    }
}

void ControllerPicture::mousePressEvent(QMouseEvent* event)
{
    const QRectF image = imageRect();
    if (image.isEmpty())
        return;
    for (const Hotspot& spot : _hotspots)
    {
        if (shapeOf(spot).contains(event->position()))
        {
            emit controlClicked(QString::fromStdString(spot.control));
            return;
        }
    }
    QWidget::mousePressEvent(event);
}
