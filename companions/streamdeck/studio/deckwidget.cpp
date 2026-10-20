#include "deckwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "virtualdeck.h"

using namespace sdb;

namespace
{
const QColor kChassis(24, 24, 28);
const QColor kBackground(43, 43, 48);
constexpr double kPi = std::numbers::pi;
bool IsPedal(const Model& m)
{
    return !m.HasKeyScreens();
}
}  // namespace

DeckWidget::DeckWidget(VirtualDeck* deck, QWidget* parent) : QWidget(parent), _deck(deck)
{
    setMouseTracking(true);
    setMinimumSize(480, 300);
    _anim = new QTimer(this);
    _anim->setInterval(16);
    connect(_anim, &QTimer::timeout, this, &DeckWidget::Animate);
    connect(_deck, &VirtualDeck::Changed, this, [this] {
        update();
        if (!_anim->isActive() && Animating())
            _anim->start();
    });
    connect(_deck, &VirtualDeck::ModelChanged, this, [this] {
        _shownAngle.clear();
        update();
    });
}

void DeckWidget::SetBanner(const QString& text)
{
    if (text == _banner)
        return;
    _banner = text;
    update();
}

bool DeckWidget::Animating() const
{
    const qint64 now = _deck->ClockMs();
    for (int k = 0; k < _deck->GetModel().keys; ++k)
    {  // a key's frame lighting up or fading out
        const double target = _deck->KeyDown(k) ? 1.0 : 0.0, glow = k < _keyGlow.size() ? _keyGlow[k] : 0.0;
        if (std::abs(glow - target) > 0.001)
            return true;
    }
    if (now - _deck->TouchMark().at < 700)
        return true;
    for (int i = 0; i < _deck->GetModel().dials; ++i)
    {
        if (now - _deck->TurnMark(i).at < 400 || now - _deck->PressMark(i).at < 500)
            return true;
        if (i < _shownAngle.size() && std::abs(_shownAngle[i] - _deck->DialAngle(i)) > 0.2)
            return true;
    }
    return false;
}

void DeckWidget::Animate()
{
    const int n = _deck->GetModel().dials;
    if (_shownAngle.size() != n)
    {
        _shownAngle.resize(n);
        for (int i = 0; i < n; ++i)
            _shownAngle[i] = _deck->DialAngle(i);
    }
    for (int i = 0; i < n; ++i)
        _shownAngle[i] += (_deck->DialAngle(i) - _shownAngle[i]) * 0.35;  // ease toward the detent
    // key frames: light up at once on a press, fade out over ~300 ms after the release
    const int keys = _deck->GetModel().keys;
    if (_keyGlow.size() != keys)
    {
        _keyGlow.fill(0.0, keys);
        _keyGlowRemote.fill(false, keys);
    }
    for (int k = 0; k < keys; ++k)
    {
        if (_deck->KeyDown(k))
        {
            _keyGlow[k] = 1.0;
            _keyGlowRemote[k] = _deck->RemoteKeyDown(k);
        }
        else
            _keyGlow[k] = std::max(0.0, _keyGlow[k] - 16.0 / 300.0);
    }
    update();
    if (!Animating())
    {
        for (int i = 0; i < n; ++i)
            _shownAngle[i] = _deck->DialAngle(i);
        _anim->stop();
        update();
    }
}

double DeckWidget::AngleAround(int dial, const QPointF& wp) const
{
    const QPointF c = DialRectWidget(dial).center();
    return std::atan2(wp.y() - c.y(), wp.x() - c.x()) * 180.0 / kPi;  // screen y down: clockwise = increasing
}

QRectF DeckWidget::BodyRect() const
{
    const auto& lay = _deck->GetLayout();
    const Model& m = _deck->GetModel();
    if (IsPedal(m))
        return {0, 0, 640, 300};
    const double margin = std::max(24.0, lay.key * 0.35);
    const double dialArea = m.dials ? lay.key * 1.3 : 0;
    return {-margin, -margin, lay.w + 2 * margin, lay.h + 2 * margin + dialArea};
}

QRectF DeckWidget::DialRect(int i) const
{
    const auto& lay = _deck->GetLayout();
    const Model& m = _deck->GetModel();
    const double area = lay.key * 1.3, slot = static_cast<double>(lay.stripW) / m.dials;
    const double r = std::min(area * 0.38, slot * 0.36);
    const QPointF c(lay.stripX + (i + 0.5) * slot, lay.h + std::max(24.0, lay.key * 0.35) * 0.4 + area / 2);
    return {c.x() - r, c.y() - r, 2 * r, 2 * r};
}

QTransform DeckWidget::ToWidget() const
{
    const QRectF b = BodyRect();
    const double s = std::min(width() / b.width(), height() / b.height()) * 0.96;
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(s, s);
    t.translate(-b.center().x(), -b.center().y());
    return t;
}

DeckWidget::Hit DeckWidget::HitTest(const QPointF& wp) const
{
    const QPointF p = ToWidget().inverted().map(wp);
    const auto& lay = _deck->GetLayout();
    const Model& m = _deck->GetModel();
    Hit h;
    h.lcd = p;
    if (IsPedal(m))
    {
        for (int i = 0; i < m.keys; ++i)
            if (QRectF(40 + i * 200, 60, 160, 200).contains(p))
            {
                h.kind = Hit::Key;
                h.index = i;
            }
        return h;
    }
    for (int k = 0; k < m.keys; ++k)
    {
        const auto r = lay.KeyRect(k, m.cols);
        if (QRectF(r.x, r.y, r.w, r.h).contains(p))
        {
            h.kind = Hit::Key;
            h.index = k;
            return h;
        }
    }
    for (int i = 0; i < static_cast<int>(lay.sensors.size()); ++i)
    {
        const auto& s = lay.sensors[static_cast<size_t>(i)];
        if (QRectF(s.x, s.y - 6, s.w, s.h + 12).contains(p))
        {
            h.kind = Hit::Sensor;
            h.index = i;
            return h;
        }
    }
    if (lay.stripW > 0 && QRectF(lay.stripX, lay.stripY, lay.stripW, lay.stripH).contains(p))
    {
        h.kind = Hit::Strip;
        return h;
    }
    for (int i = 0; i < m.dials; ++i)
    {
        const QRectF d = DialRect(i);
        if (QLineF(d.center(), p).length() <= d.width() / 2)
        {
            h.kind = Hit::Dial;
            h.index = i;
            return h;
        }
    }
    return h;
}

QRectF DeckWidget::KeyRectWidget(int key) const
{
    const auto& lay = _deck->GetLayout();
    const Model& m = _deck->GetModel();
    if (key < 0 || key >= m.keys || lay.colX.empty())
        return {};
    const auto r = lay.KeyRect(key, m.cols);
    return ToWidget().mapRect(QRectF(r.x, r.y, r.w, r.h));
}

QRectF DeckWidget::StripRectWidget() const
{
    const auto& lay = _deck->GetLayout();
    return lay.stripW > 0 ? ToWidget().mapRect(QRectF(lay.stripX, lay.stripY, lay.stripW, lay.stripH)) : QRectF();
}

QRectF DeckWidget::DialRectWidget(int dial) const
{
    return dial >= 0 && dial < _deck->GetModel().dials ? ToWidget().mapRect(DialRect(dial)) : QRectF();
}

namespace
{
// Black anodized aluminium, as Elgato's knobs: a straight cylinder seen from above. The top is milled (fine circular
// tool marks), so the light shows as two broad, soft crossed sheens, not sharp lines; a narrow chamfer rims it.
constexpr double kFace = 0.925;  // radius fractions: the flat top ends here, the chamfer runs to the edge
constexpr double kLight = -135.0 * kPi / 180.0;  // light from the top left

double Clamp01(double v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
// stable per-groove noise for the tool marks
double RingNoise(int i)
{
    auto h = static_cast<uint32_t>(i) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (h & 0xffff) / 32767.5 - 1.0;
}
QRgb Premultiplied(double r, double g, double b, double a)
{
    a = Clamp01(a);
    auto c = [a](double v) { return static_cast<int>(Clamp01(v / 255.0) * a * 255.0 + 0.5); };
    return qRgba(c(r), c(g), c(b), static_cast<int>(a * 255.0 + 0.5));
}
}  // namespace

void DeckWidget::EnsureKnob(int px)
{
    px = std::clamp(px, 16, 1024);
    if (px == _knobPx)
        return;
    _knobPx = px;
    _knobCap = QImage(px, px, QImage::Format_ARGB32_Premultiplied);
    const double radius = px / 2.0;
    for (int y = 0; y < px; ++y)
    {
        auto* cp = reinterpret_cast<QRgb*>(_knobCap.scanLine(y));
        for (int x = 0; x < px; ++x)
        {
            const double dx = x + 0.5 - radius, dy = y + 0.5 - radius, r = std::hypot(dx, dy), rr = r / radius,
                         th = std::atan2(dy, dx);
            const double toLight = std::cos(th - kLight);  // +1 facing the light, -1 away
            const double a = Clamp01(radius - r + 0.5);    // anti-aliased edge
            if (a <= 0)
            {
                cp[x] = 0;
                continue;
            }
            double c;
            if (rr < kFace)
            {
                // circular tool marks catch the light along the line through the centre towards it: a broad soft
                // sheen per light (the main one and a weaker second), brightest mid-radius; the marks' varying depth
                // gives the fine rings
                const double main = std::pow(std::abs(std::cos(th - kLight)), 9.0);
                const double second = 0.38 * std::pow(std::abs(std::cos(th + 52.0 * kPi / 180.0)), 12.0);
                const double t = rr / kFace;
                const double along = 0.35 + 0.65 * std::sin(std::min(1.0, t * 1.1) * kPi * 0.8);
                const double marks = RingNoise(static_cast<int>(r * 2.2));
                c = 27 + 72 * (main + second) * along + 6 * toLight * t + 5 * marks * (0.4 + main + second);
            }
            else
            {
                // the chamfer slopes outwards: a bright edge on the light side, a dark one away from it, the side
                // wall's dark rim at the very edge
                const double t = (rr - kFace) / (1 - kFace);
                const double lit = std::max(0.0, toLight), shade = std::max(0.0, -toLight);
                c = 40 + 110 * lit * std::exp(-std::pow((t - 0.3) / 0.35, 2)) - 22 * shade -
                    30 * Clamp01((t - 0.75) / 0.25);
            }
            cp[x] = Premultiplied(c, c, c * 1.03 + 2, a);
        }
    }
}

void DeckWidget::DrawKnob(QPainter& p, const QRectF& d, double angleDeg, bool down)
{
    const double scale = std::hypot(p.worldTransform().m11(), p.worldTransform().m12());
    EnsureKnob(static_cast<int>(std::ceil(d.width() * scale * devicePixelRatioF())));
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setPen(Qt::NoPen);
    // contact shadow in the bay
    QRadialGradient sh(d.center() + QPointF(d.width() * 0.04, d.height() * 0.07), d.width() * 0.62);
    sh.setColorAt(0.75, QColor(0, 0, 0, 170));
    sh.setColorAt(1, QColor(0, 0, 0, 0));
    p.setBrush(sh);
    p.drawEllipse(d.adjusted(-d.width() * 0.12, -d.height() * 0.08, d.width() * 0.16, d.height() * 0.2));
    const QRectF local(-d.width() / 2, -d.height() / 2, d.width(), d.height());
    p.translate(d.center());
    if (down)
        p.scale(0.975, 0.975);  // pushed in
    p.drawImage(local, _knobCap);
    // the zero mark: neon orange, straight up at angle 0, turning with the dial
    {
        const double radius = d.width() / 2;
        p.save();
        p.rotate(angleDeg);
        const QPointF a(0, -radius * 0.30), b(0, -radius * 0.80);
        p.setPen(QPen(QColor(255, 110, 20, 60), radius * 0.22, Qt::SolidLine, Qt::RoundCap));  // glow
        p.drawLine(a, b);
        p.setPen(QPen(QColor(255, 120, 25, 110), radius * 0.13, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setPen(QPen(QColor(255, 140, 40), radius * 0.07, Qt::SolidLine, Qt::RoundCap));  // the tube
        p.drawLine(a, b);
        p.setPen(QPen(QColor(255, 215, 150), radius * 0.025, Qt::SolidLine, Qt::RoundCap));  // hot core
        p.drawLine(a, b);
        p.restore();
    }
    if (down)
    {
        p.setBrush(QColor(0, 0, 0, 50));
        p.drawEllipse(local);
    }
    p.restore();
}

void DeckWidget::DrawBanner(QPainter& p)
{
    if (_banner.isEmpty())
        return;
    p.resetTransform();
    QFont f = font();
    f.setPixelSize(std::max(12, height() / 28));
    f.setBold(true);
    p.setFont(f);
    const QRectF box = QRectF(rect()).adjusted(width() * 0.1, height() * 0.42, -width() * 0.1, -height() * 0.42);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(box, 10, 10);
    p.setPen(QColor(235, 235, 245));
    p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, _banner);
}

void DeckWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), kBackground);
    const Model& m = _deck->GetModel();
    const auto& lay = _deck->GetLayout();
    p.setTransform(ToWidget());
    const QRectF body = BodyRect();
    p.setPen(QPen(QColor(60, 60, 68), 2));
    p.setBrush(kChassis);
    p.drawRoundedRect(body, body.height() * 0.05, body.height() * 0.05);

    if (IsPedal(m))
    {  // three foot switches, no screen
        for (int i = 0; i < m.keys; ++i)
        {
            const QRectF r(40 + i * 200, 60, 160, 200);
            p.setBrush(_deck->KeyDown(i) ? QColor(90, 90, 100) : QColor(50, 50, 58));
            p.drawRoundedRect(r, 18, 18);
        }
        DrawBanner(p);
        return;
    }

    const QRectF lcd(0, 0, lay.w, lay.h);
    if (_deck->Asleep())
    {
        p.fillRect(lcd, Qt::black);
    }
    else
    {
        // nearest neighbour: device pixels stay crisp at any zoom (no bilinear blur); antialiasing off too: with it on,
        // an enlarged image goes through Qt's filtered path and comes out soft
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.drawImage(lcd, _deck->Lcd());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // The backlight: the picture scales with it, perceptually (70 % looks nearly full, as on the device), and an
        // LCD's black is never black: the backlight leaks through, a faint blue-grey glow that follows its level.
        const double light = std::pow(_deck->Brightness() / 100.0, 0.5);
        p.save();
        if (light < 0.999)
        {
            p.setCompositionMode(QPainter::CompositionMode_Multiply);
            const int v = static_cast<int>(light * 255 + 0.5);
            p.fillRect(lcd, QColor(v, v, v));
        }
        p.setCompositionMode(QPainter::CompositionMode_Lighten);
        p.fillRect(lcd,
                   QColor(static_cast<int>(11 * light), static_cast<int>(12 * light), static_cast<int>(16 * light)));
        p.restore();
    }

    // Bezel: everything except the key apertures, the window and the sensors is covered by the chassis.
    const double radius = lay.key * 0.16;
    if (bezelMask)
    {
        QPainterPath mask;
        mask.addRect(lcd);
        QPainterPath holes;
        for (int k = 0; k < m.keys; ++k)
        {
            const auto r = lay.KeyRect(k, m.cols);
            holes.addRoundedRect(QRectF(r.x, r.y, r.w, r.h), radius, radius);
        }
        if (lay.stripW > 0)
            holes.addRoundedRect(QRectF(lay.stripX, lay.stripY, lay.stripW, lay.stripH), 8, 8);
        p.fillPath(mask.subtracted(holes), kChassis);
    }
    // Neo sensors are LEDs, drawn in the colour the host set
    for (int i = 0; i < static_cast<int>(lay.sensors.size()); ++i)
    {
        const auto& s = lay.sensors[static_cast<size_t>(i)];
        QColor c = _deck->SensorColor(i);
        if (c == Qt::black)
            c = QColor(40, 40, 46);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(s.x, s.y, s.w, s.h), 6, 6);
        if (_deck->KeyDown(m.keys + i))
        {
            p.setBrush(QColor(255, 255, 255, 90));
            p.drawRoundedRect(QRectF(s.x, s.y, s.w, s.h), 6, 6);
        }
    }
    // pressed / hover feedback and the visible-area guide
    for (int k = 0; k < m.keys; ++k)
    {
        const auto r = lay.KeyRect(k, m.cols);
        const QRectF kr(r.x, r.y, r.w, r.h);
        if (const double glow = k < _keyGlow.size() ? _keyGlow[k] : 0.0; glow > 0.0 && !_deck->HostDrawsPresses())
        {
            // the key's frame lights up: cyan = this window, blue-violet = the real deck; a glow onto the bezel round a
            // bright edge, a faint tint inside
            const QColor base = _keyGlowRemote[k] ? QColor(120, 90, 255) : QColor(56, 200, 224);
            const double unit = lay.key / 60.0;  // device px per "frame px": the glow scales with the key
            p.setBrush(Qt::NoBrush);
            for (int i = 3; i >= 0; --i)
            {
                QColor c = base;
                c.setAlphaF(static_cast<float>(glow * (0.10 + 0.08 * (3 - i))));
                const double out = (1.5 + i * 2.2) * unit;
                p.setPen(QPen(c, (2.0 + i * 2.0) * unit));
                p.drawRoundedRect(kr.adjusted(-out, -out, out, out), radius + out, radius + out);
            }
            QColor edge = base.lighter(130);
            edge.setAlphaF(static_cast<float>(glow * 0.95));
            p.setPen(QPen(edge, 1.6 * unit));
            p.drawRoundedRect(kr.adjusted(-0.8 * unit, -0.8 * unit, 0.8 * unit, 0.8 * unit), radius, radius);
            QColor tint = base;
            tint.setAlphaF(static_cast<float>(glow * 0.12));
            p.setPen(Qt::NoPen);
            p.setBrush(tint);
            p.drawRoundedRect(kr, radius, radius);
        }
        if (!_deck->KeyDown(k) && _hover.kind == Hit::Key && _hover.index == k)
        {
            p.setPen(QPen(QColor(255, 255, 255, 90), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(kr, radius, radius);
        }
        if (showSafeArea && m.keySafeInset > 0)
        {
            const double in = m.keySafeInset * static_cast<double>(lay.key) / m.key.w;
            p.setPen(QPen(QColor(255, 220, 0, 160), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(kr.adjusted(in, in, -in, -in));
        }
    }
    // touch trace on the strip, fading (cyan = this window, orange = the real deck)
    const qint64 now = _deck->ClockMs();
    if (const auto& t = _deck->TouchMark(); now - t.at < 700 && lay.stripW > 0)
    {
        const double age = static_cast<double>(now - t.at) / 700.0;
        QColor c = t.remote ? QColor(255, 154, 60) : QColor(56, 200, 224);
        c.setAlphaF(static_cast<float>(0.85 * (1.0 - age)));
        const QPointF a(lay.stripX + t.a.x(), lay.stripY + t.a.y()), b(lay.stripX + t.b.x(), lay.stripY + t.b.y());
        p.setBrush(Qt::NoBrush);
        if (t.kind == 3)
        {
            p.setPen(QPen(c, 6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, b);
            const QPointF dir = (b - a) / std::max(1.0, QLineF(a, b).length());
            const QPointF n(-dir.y(), dir.x());
            p.drawLine(b, b - dir * 18 + n * 10);
            p.drawLine(b, b - dir * 18 - n * 10);
        }
        else
        {
            p.setPen(QPen(c, t.kind == 2 ? 6 : 4));
            const double r = 10 + age * (t.kind == 2 ? 26 : 40);
            p.drawEllipse(a, r, r);
            if (t.kind == 2)
            {
                p.setBrush(c);
                p.drawEllipse(a, 8, 8);
            }
        }
    }
    // dials: a recessed bay, the knobs in it
    if (_shownAngle.size() != m.dials)
    {
        _shownAngle.resize(m.dials);
        for (int i = 0; i < m.dials; ++i)
            _shownAngle[i] = _deck->DialAngle(i);
    }
    if (m.dials > 0)
    {
        const QRectF first = DialRect(0), last = DialRect(m.dials - 1);
        const double pad = first.width() * 0.28;
        const QRectF bay(first.left() - pad, first.top() - pad * 0.55, last.right() - first.left() + 2 * pad,
                         first.height() + pad * 1.1);
        const double br = bay.height() * 0.32;
        QLinearGradient bg(bay.topLeft(), bay.bottomLeft());
        bg.setColorAt(0, QColor(7, 7, 9));
        bg.setColorAt(0.35, QColor(13, 13, 16));
        bg.setColorAt(1, QColor(20, 20, 24));
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(bay, br, br);
        // the lip: shadow under the top edge, a faint light on the bottom edge
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 150), pad * 0.25));
        p.drawRoundedRect(bay.adjusted(pad * 0.12, pad * 0.12, -pad * 0.12, -pad * 0.12), br, br);
        p.setPen(QPen(QColor(255, 255, 255, 26), 1.5));
        p.drawLine(QPointF(bay.left() + br, bay.bottom() - 0.75), QPointF(bay.right() - br, bay.bottom() - 0.75));
    }
    for (int i = 0; i < m.dials; ++i)
    {
        const QRectF d = DialRect(i);
        DrawKnob(p, d, _shownAngle[i], _deck->DialDown(i));
        if (_deck->DialDown(i))
        {  // held: a thin ring (cyan = this window, orange = the real deck)
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(_deck->RemoteDialDown(i) ? QColor(255, 154, 60, 200) : QColor(56, 200, 224, 200), 3));
            p.drawEllipse(d.adjusted(-3, -3, 3, 3));
        }
        if (_hover.kind == Hit::Dial && _hover.index == i)
        {
            p.setPen(QPen(QColor(255, 255, 255, 80), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(d.adjusted(-4, -4, 4, 4));
        }
        if (const auto pm = _deck->PressMark(i); now - pm.at < 500)
        {  // a press: a ring that widens and fades
            const double age = static_cast<double>(now - pm.at) / 500.0;
            QColor c = pm.remote ? QColor(255, 154, 60) : QColor(56, 200, 224);
            c.setAlphaF(static_cast<float>(0.95 * (1.0 - age)));
            p.setPen(QPen(c, 5 - 3 * age));
            p.setBrush(Qt::NoBrush);
            const double grow = 2 + 14 * age;
            p.drawEllipse(d.adjusted(-grow, -grow, grow, grow));
        }
        if (const auto tm = _deck->TurnMark(i); now - tm.at < 400)
        {  // a glowing arc on the side it turned to
            QColor c = tm.remote ? QColor(255, 154, 60) : QColor(56, 200, 224);
            c.setAlphaF(static_cast<float>(0.9 * (1.0 - static_cast<double>(now - tm.at) / 400.0)));
            p.setPen(QPen(c, 5, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            const double start = 90 - _shownAngle[i];  // Qt arcs: degrees counter-clockwise from 3 o'clock
            p.drawArc(d.adjusted(-8, -8, 8, 8), static_cast<int>(start * 16),
                      static_cast<int>((tm.dir > 0 ? -50 : 50) * 16));
        }
    }
    // the model name on the chassis, under the dial bay when there is one
    p.setPen(QColor(120, 120, 130));
    QFont f = font();
    f.setPixelSize(std::max(10, lay.key / 6));
    const double nameH = m.dials ? lay.key * 0.16 : lay.key * 0.25,
                 nameY = body.bottom() - (m.dials ? lay.key * 0.17 : lay.key * 0.3);
    if (m.dials)
        f.setPixelSize(std::max(9, lay.key / 8));
    p.setFont(f);
    p.drawText(QRectF(body.left(), nameY, body.width(), nameH), Qt::AlignCenter,
               QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size())));
    DrawBanner(p);
}

void DeckWidget::mousePressEvent(QMouseEvent* e)
{
    _pressed = HitTest(e->position());
    const Model& m = _deck->GetModel();
    switch (_pressed.kind)
    {
        case Hit::Key:
            _deck->PressButton(_pressed.index, true);
            break;
        case Hit::Sensor:
            _deck->PressButton(m.keys + _pressed.index, true);
            break;
        case Hit::Dial: {  // decided on move / release: turn, click or hold
            _drag = {_pressed.index, AngleAround(_pressed.index, e->position()), 0, 0, false, false};
            setCursor(Qt::ClosedHandCursor);
            const int idx = _pressed.index;
            QTimer::singleShot(350, this, [this, idx] {
                if (_drag.index == idx && !_drag.rotated && !_drag.holdSent)
                {
                    _drag.holdSent = true;
                    _deck->PressDial(idx, true);
                }
            });
            break;
        }
        case Hit::Strip:
            _stripStart = _pressed.lcd;
            _stripTimer.start();
            break;
        default:
            break;
    }
}

void DeckWidget::mouseReleaseEvent(QMouseEvent* e)
{
    const Model& m = _deck->GetModel();
    const auto& lay = _deck->GetLayout();
    switch (_pressed.kind)
    {
        case Hit::Key:
            _deck->PressButton(_pressed.index, false);
            break;
        case Hit::Sensor:
            _deck->PressButton(m.keys + _pressed.index, false);
            break;
        case Hit::Dial:
            if (_drag.holdSent)
                _deck->PressDial(_pressed.index, false);
            else if (!_drag.rotated)
            {  // a click: a press, and the indicator back to its default (as on the deck)
                _deck->PressDial(_pressed.index, true);
                _deck->PressDial(_pressed.index, false);
                _deck->ResetDial(_pressed.index);
            }
            _drag = {};
            unsetCursor();
            break;
        case Hit::Strip: {
            if (m.lcdKind != LcdKind::Region)
                break;  // only + / + XL windows are touch screens
            const QPointF end = ToWidget().inverted().map(e->position());
            auto local = [&](QPointF q) {
                return QPoint(std::clamp(static_cast<int>(q.x() - lay.stripX), 0, lay.stripW - 1),
                              std::clamp(static_cast<int>(q.y() - lay.stripY), 0, lay.stripH - 1));
            };
            const QPoint a = local(_stripStart), b = local(end);
            // Like the firmware: one event after the gesture - flick, long press (hold) or tap.
            if (QLineF(a, b).length() > 20)
                _deck->TouchFlick(a.x(), a.y(), b.x(), b.y());
            else if (_stripTimer.elapsed() >= 500)
                _deck->TouchPress(a.x(), a.y());
            else
                _deck->TouchTap(a.x(), a.y());
            break;
        }
        default:
            break;
    }
    _pressed = {};
}

void DeckWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (_drag.index >= 0 && (e->buttons() & Qt::LeftButton))
    {  // turning a dial with the mouse
        const double a = AngleAround(_drag.index, e->position());
        double d = a - _drag.lastAngle;
        if (d > 180)
            d -= 360;
        if (d < -180)
            d += 360;
        _drag.lastAngle = a;
        _drag.accum += d;
        _drag.total += std::abs(d);
        if (_drag.total > 8 && !_drag.holdSent)
            _drag.rotated = true;
        while (_drag.accum >= 15)
        {
            _deck->TurnDial(_drag.index, 1);
            _drag.accum -= 15;
        }
        while (_drag.accum <= -15)
        {
            _deck->TurnDial(_drag.index, -1);
            _drag.accum += 15;
        }
        return;
    }
    const Hit h = HitTest(e->position());
    setCursor(h.kind == Hit::Dial ? Qt::OpenHandCursor : Qt::ArrowCursor);
    if (h.kind != _hover.kind || h.index != _hover.index)
    {
        _hover = h;
        update();
    }
}

// Double-click on a dial: the first click already sent the press; the second returns the indicator to its default.
void DeckWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    const Hit h = HitTest(e->position());
    if (h.kind == Hit::Dial)
    {
        _deck->ResetDial(h.index);
        return;
    }
    mousePressEvent(e);  // keys and the strip: a second press as usual
}

void DeckWidget::leaveEvent(QEvent*)
{
    _hover = {};
    update();
}

void DeckWidget::wheelEvent(QWheelEvent* e)
{
    const Hit h = HitTest(e->position());
    if (h.kind != Hit::Dial)
        return;
    _wheelAccum += e->angleDelta().y();
    const int ticks = static_cast<int>(_wheelAccum / 120.0);  // one notch = one detent; trackpads accumulate
    if (ticks != 0)
    {
        _wheelAccum -= ticks * 120.0;
        _deck->TurnDial(h.index, ticks);
    }
    e->accept();
}
