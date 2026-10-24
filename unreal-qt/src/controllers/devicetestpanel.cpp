#include "devicetestpanel.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#ifdef ENABLE_CONTROLLERS
#include "hostcontrols.h"
#include "router/analog.h"
#endif

namespace
{
constexpr double kStick = 96.0;    // a stick plot's side, px
constexpr double kTrigger = 18.0;  // a trigger bar's width
constexpr double kLamp = 9.0;      // a lamp's diameter

#ifdef ENABLE_CONTROLLERS
/// The device has @p control: named, or under one of its `prefix.*` entries
bool Has(const std::vector<std::string>& controls, const std::string& control)
{
    for (const std::string& entry : controls)
    {
        if (entry == control)
            return true;
        if (entry.size() > 2 && entry.compare(entry.size() - 2, 2, ".*") == 0 &&
            control.compare(0, entry.size() - 1, entry, 0, entry.size() - 1) == 0)
            return true;
    }
    return false;
}

/// A short lamp label: "south" -> "S", "dpad.up" -> "↑", "paddle.p1" -> "P1"
QString LampLabel(const std::string& control)
{
    static const std::map<std::string, QString> labels = {
        {"south", "S"},      {"east", "E"},        {"west", "W"},        {"north", "N"},
        {"dpad.up", "↑"},    {"dpad.down", "↓"},   {"dpad.left", "←"},   {"dpad.right", "→"},
        {"lb", "LB"},        {"rb", "RB"},         {"view", "View"},     {"menu", "Menu"},
        {"guide", "Guide"},  {"lstick.click", "LS"}, {"rstick.click", "RS"}, {"paddle.p1", "P1"},
        {"paddle.p2", "P2"}, {"paddle.p3", "P3"},  {"paddle.p4", "P4"}};
    const auto it = labels.find(control);
    return it != labels.end() ? it->second : QString::fromStdString(control);
}
#endif
}  // namespace

DeviceTestPanel::DeviceTestPanel(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(static_cast<int>(kStick) + 52);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize DeviceTestPanel::sizeHint() const
{
    return {520, static_cast<int>(kStick) + 56};
}

void DeviceTestPanel::setDevice(const std::string& id, const std::vector<std::string>& controls, const QString& detail)
{
    _device = id;
    _detail = detail;
    _sticks.clear();
    _triggers.clear();
    _buttons.clear();
    _max.clear();
    _live.clear();
#ifdef ENABLE_CONTROLLERS
    for (const char* stick : {"lstick", "rstick"})
        if (Has(controls, std::string(stick) + ".x") || Has(controls, stick))
            _sticks.emplace_back(stick);
    for (const char* trigger : {"lt", "rt"})
        if (Has(controls, trigger))
            _triggers.emplace_back(trigger);
    // The canonical order of the vocabulary: face, D-pad, shoulders, sticks' clicks, system, extras
    for (const std::string& name : controllers::KnownControls())
    {
        controllers::ControlInfo info;
        if (!controllers::ParseControl(name, info) || info.type != controllers::ControlType::Button ||
            !info.base.empty() || name.rfind("mouse.", 0) == 0 || name == "push" || name == "touch")
            continue;
        if (Has(controls, name))
            _buttons.push_back(name);
    }
#else
    (void)controls;
#endif
    updateGeometry();
    update();
}

void DeviceTestPanel::setLive(const std::map<std::string, float>& live)
{
    bool changed = live != _live;
    for (const std::string& trigger : _triggers)
    {
        const auto it = live.find(trigger);
        if (it != live.end() && it->second > _max[trigger])
        {
            _max[trigger] = it->second;
            changed = true;
        }
    }
    if (!changed)
        return;
    _live = live;
    update();
}

float DeviceTestPanel::value(const std::string& control) const
{
    const auto it = _live.find(control);
    return it != _live.end() ? it->second : 0.0f;
}

std::vector<std::pair<std::string, bool>> DeviceTestPanel::lamps() const
{
    std::vector<std::pair<std::string, bool>> out;
    for (const std::string& button : _buttons)
        out.emplace_back(button, value(button) >= 0.5f);
    return out;
}

float DeviceTestPanel::triggerMax(const std::string& trigger) const
{
    const auto it = _max.find(trigger);
    return it != _max.end() ? it->second : 0.0f;
}

bool DeviceTestPanel::hasStick(const std::string& stick) const
{
    return std::find(_sticks.begin(), _sticks.end(), stick) != _sticks.end();
}

void DeviceTestPanel::paintStick(QPainter& painter, const QRectF& box, const std::string& stick,
                                 const QString& label) const
{
    const QColor text = palette().color(QPalette::WindowText);
    QColor faint = palette().color(QPalette::Mid);
    const QColor accent = palette().color(QPalette::Highlight);
    const QPointF centre = box.center();
    const double r = box.width() / 2.0 - 2.0;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(faint, 1.0));
    painter.drawEllipse(centre, r, r);
    painter.drawLine(QPointF(centre.x() - r, centre.y()), QPointF(centre.x() + r, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - r), QPointF(centre.x(), centre.y() + r));
    const float x = value(stick + ".x");
    const float y = value(stick + ".y");
#ifdef ENABLE_CONTROLLERS
    const controllers::AnalogOptions options;
    // The dead zone filled, the on / off thresholds of a direction as two rings (in the stick's own travel)
    faint.setAlphaF(0.25f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(faint);
    painter.drawEllipse(centre, r * options.deadzone, r * options.deadzone);
    const auto travel = [&options](float t) { return options.deadzone + t * (options.saturation - options.deadzone); };
    painter.setBrush(Qt::NoBrush);
    QColor ring = palette().color(QPalette::Mid);
    painter.setPen(QPen(ring, 1.0, Qt::DotLine));
    painter.drawEllipse(centre, r * travel(options.on), r * travel(options.on));
    painter.setPen(QPen(ring, 1.0, Qt::DashLine));
    painter.drawEllipse(centre, r * travel(options.off), r * travel(options.off));
    // The processed position: the direction kept, the deflection rescaled past the dead zone
    const double length = std::hypot(x, y);
    const float deflection = controllers::RadialDeflection(x, y, options);
    if (length > 0.0)
    {
        const QPointF processed(centre.x() + r * deflection * x / length, centre.y() + r * deflection * y / length);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(processed, 4.0, 4.0);
    }
#endif
    // The raw position (y is -1 up)
    painter.setPen(QPen(text, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(centre.x() + r * std::clamp(x, -1.0f, 1.0f), centre.y() + r * std::clamp(y, -1.0f, 1.0f)),
                        5.0, 5.0);
    painter.setPen(text);
    painter.drawText(QRectF(box.left(), box.bottom() + 2, box.width(), 14), Qt::AlignCenter,
                     QString("%1  %2, %3").arg(label).arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
}

void DeviceTestPanel::paintTrigger(QPainter& painter, const QRectF& box, const std::string& trigger,
                                   const QString& label) const
{
    const QColor text = palette().color(QPalette::WindowText);
    const QColor faint = palette().color(QPalette::Mid);
    const QColor accent = palette().color(QPalette::Highlight);
    const float level = std::clamp(value(trigger), 0.0f, 1.0f);
    painter.setPen(QPen(faint, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    QRectF fill = box.adjusted(1, 1, -1, -1);
    fill.setTop(fill.bottom() - fill.height() * level);
    painter.fillRect(fill, accent);
    const auto mark = [&box](float at) { return box.bottom() - box.height() * at; };
#ifdef ENABLE_CONTROLLERS
    const controllers::AnalogOptions options;
    painter.setPen(QPen(text, 1.0));
    painter.drawLine(QPointF(box.left() - 3, mark(options.triggerClick)), QPointF(box.right() + 3, mark(options.triggerClick)));
    painter.setPen(QPen(faint, 1.0, Qt::DashLine));
    painter.drawLine(QPointF(box.left() - 3, mark(options.triggerRelease)),
                     QPointF(box.right() + 3, mark(options.triggerRelease)));
#endif
    // The highest value this session: where the trigger's travel ends on this pad
    const float max = triggerMax(trigger);
    if (max > 0.02f)
    {
        painter.setPen(QPen(accent.darker(130), 1.5));
        painter.drawLine(QPointF(box.left(), mark(max)), QPointF(box.right(), mark(max)));
    }
    painter.setPen(text);
    QString caption = QString("%1 %2").arg(label).arg(level, 0, 'f', 2);
#ifdef ENABLE_CONTROLLERS
    // Pulled past its click and still short of 1: where this pad's travel ends (an Elite's hair-trigger stop)
    if (max >= controllers::AnalogOptions().triggerClick && max < 0.97f)
        caption += "\n" + tr("full %1").arg(max, 0, 'f', 2);
#endif
    painter.drawText(QRectF(box.left() - 30, box.bottom() + 2, box.width() + 60, 28), Qt::AlignHCenter | Qt::AlignTop,
                     caption);
}

void DeviceTestPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    if (_device.empty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("Select a device to test it"));
        return;
    }
    double x = 8;
    const double top = 6;
    for (const std::string& stick : _sticks)
    {
        paintStick(painter, QRectF(x, top, kStick, kStick), stick, stick == "lstick" ? tr("Left") : tr("Right"));
        x += kStick + 24;
    }
    for (const std::string& trigger : _triggers)
    {
        paintTrigger(painter, QRectF(x + 14, top, kTrigger, kStick), trigger, trigger == "lt" ? "LT" : "RT");
        x += kTrigger + 52;
    }
    // The lamps: a grid to the right, in the vocabulary's order
    const QFontMetrics metrics(font());
    const double cell = std::max(34.0, static_cast<double>(metrics.horizontalAdvance("Guide")) + 8.0);
    const int columns = std::max(1, static_cast<int>((width() - x - 8) / cell));
    int index = 0;
    const QColor accent = palette().color(QPalette::Highlight);
    for (const auto& [button, on] : lamps())
    {
        const double cx = x + (index % columns) * cell + cell / 2;
        const double cy = top + (index / columns) * 30.0 + 6;
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(on ? accent : Qt::NoBrush);
        painter.drawEllipse(QPointF(cx, cy), kLamp / 2, kLamp / 2);
        painter.setPen(text);
#ifdef ENABLE_CONTROLLERS
        painter.drawText(QRectF(cx - cell / 2, cy + 6, cell, 14), Qt::AlignHCenter | Qt::AlignTop, LampLabel(button));
#endif
        ++index;
    }
    if (_sticks.empty() && _triggers.empty() && _buttons.empty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(x, top, width() - x - 8, kStick), Qt::AlignLeft | Qt::AlignVCenter,
                         tr("The device database lists no controls for this device"));
    }
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(QRectF(8, height() - 18, width() - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(_detail, Qt::ElideRight, width() - 16));
}
