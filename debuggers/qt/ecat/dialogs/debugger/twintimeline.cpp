// unreal-debugger (not in ecat): the twin timeline, see twintimeline.h

#include "twintimeline.h"

#include <QEvent>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "debugstyle.h"

namespace {

//The lease renewed while shown: well inside a remote lease's timeout
const int RENEW_MS = 5000;
//A wheel notch zooms by this much
const double ZOOM_STEP = 1.25;
//Blocks narrower than this on average merge into a density bar
const double MERGE_PX = 4.0;

QString grouped(double v, int decimals)
{
    QString s = QString::number(std::fabs(v), 'f', decimals);
    int point = s.indexOf('.');
    if (point < 0) point = s.size();
    for (int i = point - 3; i > 0; i -= 3) s.insert(i, ',');
    return (v < 0 ? "-" : "") + s;
}

QString hex4(unsigned int v)
{
    return QString("#%1").arg(v & 0xFFFF, 4, 16, QChar('0')).toUpper();
}

QString dot()
{
    return QString(" ") + QChar(0x00B7) + " ";
}

//A step of the axis: 1, 2 or 5 times a power of ten, at least `min` main T
double nice_step(double min)
{
    const double p = std::pow(10.0, std::floor(std::log10(std::max(min, 1e-9))));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= min) return m * p;
    return 10 * p;
}

QColor with_alpha(QColor c, int alpha)
{
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}

} // namespace

TwinTimeline::TwinTimeline(Emulator * e, QWidget *parent):
    QWidget(parent),
    e(e)
{
    setObjectName("twin_timeline");
    setMouseTracking(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QString());  //tooltips per element (event())
    m_reread = new QTimer(this);
    m_reread->setSingleShot(true);
    m_reread->setInterval(120);
    connect(m_reread, &QTimer::timeout, this, [this]() { refresh(); });
    m_retry = new QTimer(this);
    m_retry->setSingleShot(true);
    m_retry->setInterval(RETRY_MS);
    connect(m_retry, &QTimer::timeout, this, [this]() { read(); });
    m_renew = new QTimer(this);
    m_renew->setInterval(RENEW_MS);
    connect(m_renew, &QTimer::timeout, this, [this]() { arm(true); });
}

TwinTimeline::~TwinTimeline()
{
    release();
}

void TwinTimeline::set_card(const std::string &card)
{
    m_card = card;
}

void TwinTimeline::arm(bool on)
{
    if (!e->loaded || !m_online) {
        m_armed = false;
        return;
    }
    if (!on && !m_armed) return;
    const Emulator::GsJson r = e->timeline_arm(on);
    m_armed = on && r.ok;
}

void TwinTimeline::release()
{
    m_renew->stop();
    arm(false);
}

void TwinTimeline::set_online(bool on)
{
    if (on == m_online) return;
    m_online = on;
    if (!on) {
        //The lease runs out by itself over there: nothing can be said to a machine that is gone
        m_renew->stop();
        m_armed = false;
        update();
        return;
    }
    if (isVisible()) {
        arm(true);
        m_renew->start();
        refresh();
    }
}

void TwinTimeline::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!e->loaded) return;
    arm(true);
    m_renew->start();
    refresh();
}

void TwinTimeline::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_retry->stop();
    release();
}

double TwinTimeline::wanted_span() const
{
    return std::clamp(std::max(kDefaultSpan, std::fabs(m_center) + m_span), kMinSpan, kMaxSpan);
}

void TwinTimeline::refresh()
{
    m_retries_left = RETRIES;  //a new moment: a fresh round of tries
    read();
}

void TwinTimeline::read()
{
    if (!isVisible() || !e->loaded || !m_online) return;
    const double span = wanted_span();
    const unsigned int recent = (unsigned int)std::clamp(span, 64.0, 512.0);
    m_reads++;
    const Emulator::GsJson r = e->read_timeline(m_card, span, recent, 50, true);
    if (!r.ok && r.busy) {
        //Another client holds the machine for a moment, or its stop is on its way: the last picture stays
        m_busy_reads++;
        if (m_retries_left-- > 0) {
            m_retry->start();
            return;
        }
    }
    m_retry->stop();
    if (!r.ok) {
        //The last picture stays; the line says why it is not newer
        m_message = QString::fromStdString(r.message);
        update();
        return;
    }
    m_message.clear();
    m_node = QJsonDocument::fromJson(QByteArray::fromStdString(r.json)).object();
    m_armed = m_node.value("armed").toBool();
    m_read_span = span;
    parse();
    update();
}

void TwinTimeline::parse()
{
    m_main.clear();
    m_card_blocks.clear();
    m_markers.clear();
    m_segments.clear();
    m_density.clear();
    m_readout_main.clear();
    m_readout_now.clear();
    m_readout_card.clear();
    m_gap = 0;
    m_card_cpu = m_node.value("cpu").toString();
    m_detail = m_node.value("available").toBool() && m_node.value("state").toString() != "running";

    const QJsonArray lanes = m_node.value("lanes").toArray();
    m_card_title.clear();
    for (const QJsonValue &v : lanes) {
        const QJsonObject l = v.toObject();
        if (l.value("cpu").toString() == "main") {
            m_readout_main = l.value("readout").toString();
        } else {
            m_readout_card = l.value("readout").toString();
            m_card_title = l.value("name").toString();
            const double hz = l.value("clock_hz").toDouble();
            if (hz > 0) m_card_title += dot() + QString::number(hz / 1e6, 'f', 1) + " MHz";
        }
    }

    if (!m_detail) {
        //Running: the density per frame, oldest first
        for (const QJsonValue &v : m_node.value("density").toArray()) {
            const QJsonObject d = v.toObject();
            const QJsonObject mn = d.value("main").toObject();
            const QJsonObject cd = d.value("card").toObject();
            Tick t;
            t.frame = (qint64)d.value("frame").toDouble();
            t.commands = mn.value("commands").toInt();
            t.data_writes = mn.value("data_writes").toInt();
            t.data_reads = mn.value("data_reads").toInt();
            t.status_polls = mn.value("status_polls").toInt();
            t.steps = cd.value("steps").toInt();
            t.idle_steps = cd.value("idle_steps").toInt();
            t.busy_steps = cd.value("busy_steps").toInt();
            t.interrupts = cd.value("interrupts").toInt();
            t.nmis = cd.value("nmis").toInt();
            m_density.push_back(t);
        }
        if (!m_density.empty()) {
            double accesses = 0, busy = 0, steps = 0, ints = 0;
            for (const Tick &t : m_density) {
                accesses += t.commands + t.data_writes + t.data_reads + t.status_polls;
                busy += t.busy_steps;
                steps += t.steps;
                ints += t.interrupts;
            }
            const double n = (double)m_density.size();
            m_readout_main = tr("%1 GS port accesses / frame").arg(QString::number(accesses / n, 'f', 1));
            m_readout_card = tr("busy %1%").arg(steps > 0 ? QString::number(100.0 * busy / steps, 'f', 0) : QString("0")) +
                             dot() + tr("%1 INT / frame").arg(QString::number(ints / n, 'f', 1)) +
                             (m_readout_card.isEmpty() ? QString() : dot() + m_readout_card);
            m_readout_now = tr("running") + dot() + tr("frame %1").arg(grouped((double)m_density.back().frame, 0)) + dot() +
                            tr("%1 frames").arg(m_density.size());
        } else {
            m_readout_now = m_node.value("available").toBool()
                                ? tr("running") + dot() + (m_armed ? tr("no frame sampled yet") : tr("not armed"))
                                : QString();
        }
        return;
    }

    const QJsonObject now = m_node.value("now").toObject();
    m_gap = now.value("gap_t").toDouble();
    m_readout_now = tr("T %1").arg(grouped(now.value("t").toDouble(), 1)) + dot() +
                    tr("frame %1").arg(grouped(now.value("frame").toDouble(), 0));
    if (m_gap != 0) m_readout_now += dot() + tr("gap %1 T").arg(QString::number(m_gap, 'f', 1));

    const QJsonObject recent = m_node.value("recent").toObject();
    const QJsonObject next = m_node.value("next").toObject();
    auto read_lane = [&](const QString &key, bool card, std::vector<Block> &out) {
        for (const QJsonValue &v : recent.value(key).toArray()) {
            const QJsonObject o = v.toObject();
            Block b;
            b.at = o.value("at").toDouble();
            b.duration = o.value("duration_t").toDouble();
            b.pc = (unsigned int)o.value("pc").toInt();
            b.kind = o.value("kind").toString();
            b.open = o.value("open").toBool();
            const QString label = o.value("label").toString();
            const QString text = o.value("text").toString();
            if (b.kind == "int" || b.kind == "nmi") b.text = b.kind.toUpper();
            else b.text = text;
            QString tip = hex4(b.pc) + (label.isEmpty() ? QString() : "  " + label) + "  " + (text.isEmpty() ? b.text : text);
            if (card && o.contains("duration_cycles"))
                tip += "\n" + tr("%1 cycles at %2 MHz (%3 main T)")
                                  .arg(QString::number(o.value("duration_cycles").toDouble(), 'f', 1))
                                  .arg(QString::number(o.value("clock_hz").toDouble() / 1e6, 'f', 1))
                                  .arg(QString::number(b.duration, 'f', 1));
            else
                tip += "\n" + tr("%1 T").arg(QString::number(b.duration, 'f', b.duration == std::floor(b.duration) ? 0 : 1));
            if (b.kind == "idle") tip += dot() + tr("idle loop, %1 iterations").arg(o.value("idle_iterations").toInt());
            if (b.open) tip += dot() + tr("running now");
            tip += "\n" + tr("starts at %1 T from NOW, frame %2").arg(QString::number(b.at, 'f', 1)).arg(o.value("frame").toInt());
            b.tip = tip;
            out.push_back(b);
        }
        if (next.contains(key)) {
            const QJsonObject o = next.value(key).toObject();
            Block b;
            b.next = true;
            b.at = o.value("at").toDouble();
            b.pc = (unsigned int)o.value("pc").toInt();
            b.kind = "instruction";
            b.text = o.value("text").toString();
            const QString label = o.value("label").toString();
            b.tip = tr("next") + ": " + hex4(b.pc) + (label.isEmpty() ? QString() : "  " + label) + "  " + b.text;
            out.push_back(b);
        }
    };
    read_lane("main", false, m_main);
    if (!m_card_cpu.isEmpty()) read_lane(m_card_cpu, true, m_card_blocks);

    for (const QJsonValue &v : m_node.value("markers").toArray()) {
        const QJsonObject o = v.toObject();
        Marker m;
        m.at = o.value("at").toDouble();
        m.lane = o.value("lane").toString();
        m.kind = o.value("kind").toString();
        m.text = o.value("text").toString();
        if (o.contains("pc") && m.kind == "host_port") m.text += "  " + tr("at %1").arg(hex4((unsigned int)o.value("pc").toInt()));
        m_markers.push_back(m);
    }

    //The main CPU inside an instruction (a card's stop): its bus cycles from its start
    const QJsonObject main_position = m_node.value("position").toObject().value("main").toObject();
    const QJsonArray cycles = main_position.value("bus_cycles").toArray();
    //(At a boundary the cycles are the next instruction's: the outlined block says that one)
    if (!cycles.isEmpty() && !main_position.value("on_boundary").toBool() && main_position.value("elapsed_t").toDouble() > 0) {
        double at = m_gap - main_position.value("elapsed_t").toDouble();
        const int current = main_position.contains("bus_cycle_index") ? main_position.value("bus_cycle_index").toInt() : -1;
        for (int i = 0; i < cycles.size(); i++) {
            const QJsonObject c = cycles[i].toObject();
            Segment s;
            s.at = at;
            s.duration = c.value("t").toDouble();
            s.kind = c.value("kind").toString();
            s.current = i == current;
            m_segments.push_back(s);
            at += s.duration;
        }
    }
}

void TwinTimeline::set_view(double center, double span)
{
    m_span = std::clamp(span, kMinSpan, kMaxSpan);
    m_center = std::clamp(center, -kMaxSpan + m_span, kMaxSpan - m_span);
    if (m_detail && wanted_span() > m_read_span + 1e-6) m_reread->start();
    update();
}

void TwinTimeline::zoom_at(double at, int notches)
{
    const double span = std::clamp(m_span / std::pow(ZOOM_STEP, notches), kMinSpan, kMaxSpan);
    set_view(at - (at - m_center) * (span / m_span), span);
}

void TwinTimeline::recenter()
{
    set_view(0.0, m_span);
}

//Rows: main markers, MAIN, NOW (the axis), CARD, card markers
int TwinTimeline::row_height() const
{
    return QFontMetrics(dbg::font()).height() + 4;
}

int TwinTimeline::label_width() const
{
    return dbg::text_width(QFontMetrics(dbg::font()), "CARD") + 14;
}

int TwinTimeline::readout_width() const
{
    const QFontMetrics fm(dbg::font());
    const int want = std::max({dbg::text_width(fm, m_readout_main), dbg::text_width(fm, m_readout_now),
                               dbg::text_width(fm, m_readout_card), dbg::text_width(fm, "T 99,999.9 · frame 9,999")}) + 16;
    return std::min(want, (int)(width() * 0.42));
}

QRectF TwinTimeline::lanes_area() const
{
    const double left = label_width();
    const double right = width() - readout_width();
    return QRectF(left, 0, std::max(10.0, right - left), height());
}

double TwinTimeline::x_of(double t, const QRectF &area) const
{
    return area.left() + (t - (m_center - m_span)) / (2 * m_span) * area.width();
}

double TwinTimeline::t_of(double x, const QRectF &area) const
{
    return m_center - m_span + (x - area.left()) / area.width() * 2 * m_span;
}

int TwinTimeline::now_x() const
{
    return (int)std::lround(x_of(0.0, lanes_area()));
}

QSize TwinTimeline::sizeHint() const
{
    const int h = row_height();
    return QSize(800, (int)std::lround(h * (0.8 + 1.5 + 1.1 + 1.5 + 0.8)) + 4);
}

QSize TwinTimeline::minimumSizeHint() const
{
    return QSize(300, sizeHint().height());
}

const std::vector<TwinTimeline::Block> &TwinTimeline::lane_blocks(const QString &lane) const
{
    return lane == "main" ? m_main : m_card_blocks;
}

int TwinTimeline::block_count(const QString &lane) const
{
    int n = 0;
    for (const Block &b : lane_blocks(lane)) n += !b.next;
    return n;
}

int TwinTimeline::marker_count(const QString &kind) const
{
    int n = 0;
    for (const Marker &m : m_markers) n += kind.isEmpty() || m.kind == kind;
    return n;
}

bool TwinTimeline::merged(const QString &lane) const
{
    const std::vector<Block> &blocks = lane_blocks(lane);
    double sum = 0;
    int n = 0;
    for (const Block &b : blocks)
        if (!b.next) {
            sum += b.duration;
            n++;
        }
    if (n == 0) return false;
    const double px_per_t = lanes_area().width() / (2 * m_span);
    return sum / n * px_per_t < MERGE_PX;
}

QPoint TwinTimeline::block_point(const QString &lane) const
{
    const QString cpu = lane == "main" ? QString("main") : m_card_cpu;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
        if (it->cpu == cpu && it->rect.width() >= 2) return it->rect.center().toPoint();
    return QPoint();
}

void TwinTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const dbg::Colors c = dbg::colors(palette());
    const QFont font = dbg::font();
    QFont small = font;
    small.setPointSizeF(font.pointSizeF() * 0.85);
    p.setFont(font);
    m_hits.clear();

    const int h = row_height();
    const double r0 = 2, r1 = r0 + h * 0.8, r2 = r1 + h * 1.5, r3 = r2 + h * 1.1, r4 = r3 + h * 1.5, r5 = r4 + h * 0.8;
    p.fillRect(rect(), c.back);
    const QRectF area = lanes_area();
    const QRectF main_markers(area.left(), r0, area.width(), r1 - r0);
    const QRectF main_lane(area.left(), r1, area.width(), r2 - r1);
    const QRectF axis(area.left(), r2, area.width(), r3 - r2);
    const QRectF card_lane(area.left(), r3, area.width(), r4 - r3);
    const QRectF card_markers(area.left(), r4, area.width(), r5 - r4);
    p.fillRect(QRectF(0, r1, width(), r2 - r1), c.gutter);
    p.fillRect(QRectF(0, r3, width(), r4 - r3), c.gutter);
    p.setPen(c.separator);
    p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));

    //The labels and the readouts
    const QColor main_tint = c.call;
    const bool neogs = m_card_title.contains("NeoGS", Qt::CaseInsensitive);
    const QColor card_tint = neogs ? QColor(150, 90, 220) : QColor(215, 140, 20);
    p.setPen(main_tint);
    p.drawText(QRectF(6, r1, label_width() - 8, r2 - r1), Qt::AlignVCenter | Qt::AlignLeft, "MAIN");
    p.setPen(c.text);
    p.drawText(QRectF(6, r2, label_width() - 8, r3 - r2), Qt::AlignVCenter | Qt::AlignLeft, "NOW");
    p.setPen(card_tint);
    p.drawText(QRectF(6, r3, label_width() - 8, r4 - r3), Qt::AlignVCenter | Qt::AlignLeft, "CARD");
    m_hits.push_back({QRectF(0, r1, label_width(), r2 - r1), tr("The main CPU (Z80): its instructions by their measured T"), QString(), 0});
    m_hits.push_back({QRectF(0, r3, label_width(), r4 - r3),
                      tr("The card CPU (%1): its instructions on the main CPU's clock")
                          .arg(m_card_title.isEmpty() ? m_card_cpu : m_card_title), QString(), 0});
    const QRectF readouts(area.right() + 8, 0, width() - area.right() - 10, height());
    auto readout = [&](double top, double bottom, const QString &text, const QColor &color) {
        p.setPen(color);
        const QRectF r(readouts.left(), top, readouts.width(), bottom - top);
        const QString shown = QFontMetrics(font).elidedText(text, Qt::ElideRight, (int)r.width());
        p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, shown);
        if (shown != text) m_hits.push_back({r, text, QString(), 0});
    };
    readout(r1, r2, m_readout_main, c.text);
    readout(r2, r3, m_readout_now, c.text);
    readout(r3, r4, m_readout_card, c.text);

    p.save();
    p.setClipRect(area);
    if (!m_node.isEmpty() && !m_node.value("available").toBool()) {
        p.setPen(c.dim);
        p.drawText(area, Qt::AlignCenter, m_node.value("message").toString(tr("No card CPU: no twin timeline")));
    } else if (m_detail) {
        paint_axis(p, axis, area);
        if (merged("main")) paint_merged(p, m_main, main_lane, area, main_tint);
        else paint_blocks(p, m_main, main_lane, area, "main", main_tint);
        if (merged("card")) paint_merged(p, m_card_blocks, card_lane, area, card_tint);
        else paint_blocks(p, m_card_blocks, card_lane, area, m_card_cpu, card_tint);
        //The current main instruction's bus cycles, under its block
        if (!m_segments.empty()) {
            p.setFont(small);
            const QRectF band(area.left(), main_lane.bottom() - main_lane.height() * 0.42, area.width(), main_lane.height() * 0.42 - 1);
            for (const Segment &s : m_segments) {
                const double x0 = x_of(s.at, area), x1 = x_of(s.at + s.duration, area);
                const QRectF r(x0, band.top(), std::max(1.0, x1 - x0), band.height());
                p.fillRect(r, with_alpha(main_tint, s.current ? 150 : 60));
                p.setPen(with_alpha(main_tint.darker(150), 255));
                p.drawRect(r.adjusted(0, 0, -1, -1));
                const QString text = s.kind + " " + QString::number(s.duration, 'f', 0);
                p.setPen(s.current ? c.selection_text : c.text);
                if (dbg::text_width(QFontMetrics(small), text) + 4 <= r.width()) p.drawText(r, Qt::AlignCenter, text);
                else if (dbg::text_width(QFontMetrics(small), s.kind) + 2 <= r.width()) p.drawText(r, Qt::AlignCenter, s.kind);
                m_hits.push_back({r, tr("bus cycle %1, %2 T").arg(s.kind).arg(s.duration) + (s.current ? dot() + tr("the main CPU is in it") : QString()),
                                  QString(), 0});
            }
            p.setFont(font);
        }
        //Main's own moment when it is not NOW: the gap
        if (m_gap != 0) {
            const double x = x_of(m_gap, area);
            p.setPen(QPen(c.text, 1, Qt::DashLine));
            p.drawLine(QPointF(x, main_lane.top()), QPointF(x, main_lane.bottom()));
            m_hits.push_back({QRectF(x - 3, main_lane.top(), 6, main_lane.height()),
                              tr("the main CPU stands %1 T past NOW (the gap)").arg(QString::number(m_gap, 'f', 1)), QString(), 0});
        }
        paint_markers(p, "main", main_markers, area);
        paint_markers(p, m_card_cpu, card_markers, area);
    } else {
        paint_density(p, main_lane, card_lane, area);
    }
    //NOW: the shared moment
    if (m_detail || m_node.isEmpty()) {
        const double x = std::round(x_of(0.0, area)) + 0.5;
        p.setPen(QPen(c.text, 2));
        p.drawLine(QPointF(x, r0), QPointF(x, r5));
        m_hits.push_back({QRectF(x - 3, r2, 6, r3 - r2), tr("NOW: the moment both CPUs share"), QString(), 0});
    }
    p.restore();

    if (!m_message.isEmpty()) {
        p.setFont(small);
        p.setPen(c.changed);
        p.drawText(QRectF(area.left() + 4, r2, area.width() - 8, r3 - r2), Qt::AlignVCenter | Qt::AlignRight,
                   tr("not read: %1").arg(m_message));
    } else if (!m_online) {
        p.setFont(small);
        p.setPen(c.dim);
        p.drawText(QRectF(area.left() + 4, r2, area.width() - 8, r3 - r2), Qt::AlignVCenter | Qt::AlignRight, tr("offline"));
    }
}

void TwinTimeline::paint_axis(QPainter &p, const QRectF &row, const QRectF &area)
{
    //A ruler: the line along the top of the row, ticks down from it, their T under them
    const dbg::Colors c = dbg::colors(palette());
    QFont small = dbg::font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const double line = std::round(row.top() + 3) + 0.5;
    p.setPen(c.separator);
    p.drawLine(QPointF(area.left(), line), QPointF(area.right(), line));
    const double px_per_t = area.width() / (2 * m_span);
    const QFontMetrics fm(small);
    const double step = nice_step((dbg::text_width(fm, "-99999") + 24) / px_per_t);
    const double from = std::ceil((m_center - m_span) / step) * step;
    for (double t = from; t <= m_center + m_span; t += step) {
        const double x = std::round(x_of(t, area)) + 0.5;
        p.setPen(c.dim);
        p.drawLine(QPointF(x, line), QPointF(x, line + 4));
        if (std::fabs(t) < step / 2) continue;  //NOW is the line itself
        const QString text = (t > 0 ? "+" : "") + QString::number(t, 'f', step < 1 ? 1 : 0);
        const double w = dbg::text_width(fm, text);
        if (x - w / 2 < area.left() || x + w / 2 > area.right()) continue;
        p.drawText(QRectF(x - w / 2 - 2, line + 3, w + 4, row.bottom() - line - 3), Qt::AlignCenter, text);
    }
    m_hits.push_back({QRectF(area.left(), row.top(), area.width(), row.height()),
                      tr("main T from NOW, the view ± %1 T (the wheel zooms, a drag pans, a double-click recenters)")
                          .arg(QString::number(m_span, 'f', m_span < 10 ? 1 : 0)),
                      QString(), 0});
    p.setFont(dbg::font());
}

void TwinTimeline::paint_blocks(QPainter &p, const std::vector<Block> &blocks, const QRectF &lane, const QRectF &area,
                                const QString &cpu, const QColor &tint)
{
    const dbg::Colors c = dbg::colors(palette());
    const QFontMetrics fm(dbg::font());
    const double top = lane.top() + 2, height = lane.height() - 4;
    int parity = 0;
    for (const Block &b : blocks) {
        double x0 = x_of(b.at, area);
        double x1 = b.next ? x0 : x_of(b.at + b.duration, area);
        if (b.next) {
            //No duration yet: as wide as its text, at least 4 T
            x1 = x0 + std::max<double>(dbg::text_width(fm, b.text) + 10, 4 * area.width() / (2 * m_span));
        }
        if (x1 < area.left() - 1 || x0 > area.right() + 1) continue;
        const QRectF r(std::round(x0), top, std::max(1.0, std::round(x1) - std::round(x0)), height);
        QColor fill = tint;
        if (b.kind == "idle") fill = c.dim;
        else if (b.kind == "int" || b.kind == "nmi") fill = c.jump;
        if (b.next) {
            p.setPen(QPen(tint, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        } else {
            p.fillRect(r, with_alpha(fill, (parity++ & 1) ? 70 : 40));
            p.setPen(with_alpha(fill.darker(130), 220));
            p.drawLine(r.topLeft(), r.bottomLeft());
            if (b.open) {
                p.setPen(QPen(fill.darker(130), 1, Qt::DotLine));
                p.drawLine(r.topRight(), r.bottomRight());
            }
        }
        if (std::min(r.right(), area.right()) - std::max(r.left(), area.left()) > 12) {
            //A block cut by the left edge: its text from the edge, its start readable
            QRectF inner = r.adjusted(3, 0, -2, 0);
            if (inner.left() < area.left() + 2) inner.setLeft(area.left() + 2);
            //The bus cycles take the main lane's lower part: the text over them
            if (cpu == "main" && !m_segments.empty()) inner.setBottom(r.top() + r.height() * 0.58);
            const QString shown = fm.elidedText(b.text, Qt::ElideRight, (int)inner.width());
            if (shown.size() > 1 || b.text.size() <= 1) {
                p.setPen(b.next ? c.dim : c.text);
                p.drawText(inner, Qt::AlignVCenter | Qt::AlignLeft, shown);
            }
        }
        m_hits.push_back({r, b.tip, cpu, b.pc});
    }
}

void TwinTimeline::paint_merged(QPainter &p, const std::vector<Block> &blocks, const QRectF &lane, const QRectF &area, const QColor &tint)
{
    //Bins of 2 px: how much of each the lane's busy and idle instructions fill
    const dbg::Colors c = dbg::colors(palette());
    const int bins = std::max(1, (int)(area.width() / 2));
    std::vector<double> busy(bins, 0.0), idle(bins, 0.0);
    const double t_per_bin = 2 * m_span / bins;
    for (const Block &b : blocks) {
        if (b.next) continue;
        const double from = (b.at - (m_center - m_span)) / t_per_bin, to = (b.at + b.duration - (m_center - m_span)) / t_per_bin;
        for (int i = std::max(0, (int)std::floor(from)); i < std::min(bins, (int)std::ceil(to)); i++) {
            const double part = std::min(to, i + 1.0) - std::max(from, (double)i);
            (b.kind == "idle" ? idle : busy)[i] += std::max(0.0, part);
        }
    }
    const double top = lane.top() + 2, height = lane.height() - 4;
    for (int i = 0; i < bins; i++) {
        const double x = area.left() + i * 2;
        if (busy[i] > 0) p.fillRect(QRectF(x, top, 2, height), with_alpha(tint, 40 + (int)(180 * std::min(1.0, busy[i]))));
        else if (idle[i] > 0) p.fillRect(QRectF(x, top, 2, height), with_alpha(c.dim, 30 + (int)(90 * std::min(1.0, idle[i]))));
    }
    m_hits.push_back({lane, tr("zoomed out: the instructions merge into a density bar (the wheel zooms in)"), QString(), 0});
}

void TwinTimeline::paint_markers(QPainter &p, const QString &lane_cpu, const QRectF &row, const QRectF &area)
{
    const dbg::Colors c = dbg::colors(palette());
    QFont glyphs = dbg::font();
    glyphs.setBold(true);
    QFont words = dbg::font();
    words.setPointSizeF(words.pointSizeF() * 0.8);
    words.setBold(true);
    for (const Marker &m : m_markers) {
        if (m.lane != lane_cpu) continue;
        const double x = x_of(m.at, area);
        if (x < area.left() - 8 || x > area.right() + 8) continue;
        QString glyph;
        QColor color;
        if (m.kind == "breakpoint") {
            glyph = QChar(0x25CF);
            color = c.breakpoint;
        } else if (m.kind == "host_port") {
            glyph = QChar(0x21C4);
            color = c.call;
        } else if (m.kind == "clock_change") {
            glyph = QChar(0x25C6);
            color = QColor(150, 90, 220);
        } else {
            glyph = m.kind.toUpper();
            color = c.jump;
        }
        const bool word = glyph.size() > 1;
        p.setFont(word ? words : glyphs);
        const double w = dbg::text_width(QFontMetrics(word ? words : glyphs), glyph) + 4;
        const QRectF r(x - w / 2, row.top(), w, row.height());
        p.setPen(color);
        p.drawText(r, Qt::AlignCenter, glyph);
        //A tick down to (or up to) the lane it is of
        const bool above = row.bottom() < height() / 2.0;
        p.drawLine(QPointF(std::round(x) + 0.5, above ? row.bottom() - 1 : row.top()),
                   QPointF(std::round(x) + 0.5, above ? row.bottom() + 2 : row.top() - 3));
        m_hits.insert(m_hits.begin(), Hit{r, m.text, QString(), 0});
    }
    p.setFont(dbg::font());
}

void TwinTimeline::paint_density(QPainter &p, const QRectF &main_lane, const QRectF &card_lane, const QRectF &area)
{
    const dbg::Colors c = dbg::colors(palette());
    if (m_density.empty()) {
        p.setPen(c.dim);
        p.drawText(area, Qt::AlignCenter, m_node.value("available").toBool() ? tr("running: waiting for the first frames")
                                                                              : tr("no timeline yet"));
        return;
    }
    const bool neogs = m_card_title.contains("NeoGS", Qt::CaseInsensitive);
    const QColor card_tint = neogs ? QColor(150, 90, 220) : QColor(215, 140, 20);
    //A column a frame, the newest at the right edge
    const int n = (int)m_density.size();
    const double col = area.width() / std::max(n, 50);
    int max_accesses = 1, max_steps = 1;
    for (const Tick &t : m_density) {
        max_accesses = std::max(max_accesses, t.commands + t.data_writes + t.data_reads + t.status_polls);
        max_steps = std::max(max_steps, t.steps);
    }
    for (int i = 0; i < n; i++) {
        const Tick &t = m_density[i];
        const double x = area.right() - (n - i) * col;
        const QRectF cell(x + 1, 0, std::max(1.0, col - 2), 0);
        //Main: the accesses stacked: commands, data writes, data reads, status polls
        double bottom = main_lane.bottom() - 2;
        const double scale = (main_lane.height() - 4) / max_accesses;
        const int parts[4] = {t.commands, t.data_writes, t.data_reads, t.status_polls};
        const int alpha[4] = {230, 170, 110, 60};
        for (int k = 0; k < 4; k++) {
            const double hgt = parts[k] * scale;
            if (hgt <= 0) continue;
            p.fillRect(QRectF(cell.left(), bottom - hgt, cell.width(), hgt), with_alpha(c.call, alpha[k]));
            bottom -= hgt;
        }
        m_hits.push_back({QRectF(x, main_lane.top(), col, main_lane.height()),
                          tr("frame %1: %2 commands (OUT #BB), %3 data writes (OUT #B3), %4 data reads (IN #B3), %5 status polls (IN #BB)")
                              .arg(t.frame).arg(t.commands).arg(t.data_writes).arg(t.data_reads).arg(t.status_polls),
                          QString(), 0});
        //Card: the steps, busy (tinted) over idle (gray); interrupts as dots on top
        const double cscale = (card_lane.height() - 6) / max_steps;
        double cb = card_lane.bottom() - 2;
        const double busy = t.busy_steps * cscale, idle = t.idle_steps * cscale;
        p.fillRect(QRectF(cell.left(), cb - busy, cell.width(), busy), with_alpha(card_tint, 200));
        cb -= busy;
        p.fillRect(QRectF(cell.left(), cb - idle, cell.width(), idle), with_alpha(c.dim, 70));
        if (t.interrupts > 0) p.fillRect(QRectF(cell.left(), card_lane.top() + 1, cell.width(), 3), with_alpha(c.jump, 220));
        m_hits.push_back({QRectF(x, card_lane.top(), col, card_lane.height()),
                          tr("frame %1: %2 card instructions, %3 busy, %4 in the idle loop; %5 interrupts, %6 NMI")
                              .arg(t.frame).arg(t.steps).arg(t.busy_steps).arg(t.idle_steps).arg(t.interrupts).arg(t.nmis),
                          QString(), 0});
    }
    QFont small = dbg::font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    p.setPen(c.dim);
    const QRectF axis(area.left(), main_lane.bottom(), area.width(), card_lane.top() - main_lane.bottom());
    p.drawText(axis.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
               tr("density per frame, the newest at the right: GS port accesses above, card busy / idle below"));
    p.setFont(dbg::font());
}

void TwinTimeline::wheelEvent(QWheelEvent *event)
{
    //Ctrl + the wheel is the window's scale (DebuggerWindow::eventFilter takes it first)
    m_wheel += event->angleDelta().y();
    const int notches = m_wheel / 120;
    m_wheel -= notches * 120;
    if (notches != 0 && m_detail) {
        const double x = event->position().x();
        const QRectF area = lanes_area();
        zoom_at(t_of(std::clamp(x, area.left(), area.right()), area), notches);
    }
    event->accept();
}

void TwinTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
    m_press = event->pos();
    m_press_center = m_center;
    m_dragging = false;
}

void TwinTimeline::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || !m_detail) return;
    const int dx = event->pos().x() - m_press.x();
    if (!m_dragging && std::abs(dx) < 4) return;
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
    set_view(m_press_center - dx * (2 * m_span) / lanes_area().width(), m_span);
}

void TwinTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(event);
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
        return;
    }
    for (const Hit &h : m_hits)
        if (!h.cpu.isEmpty() && h.rect.contains(event->pos())) {
            emit block_activated(h.cpu, h.pc);
            return;
        }
}

void TwinTimeline::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) recenter();
}

bool TwinTimeline::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent * help = static_cast<QHelpEvent *>(event);
        for (const Hit &h : m_hits)
            if (h.rect.contains(help->pos()) && !h.tip.isEmpty()) {
                QToolTip::showText(help->globalPos(), h.tip, this, h.rect.toRect());
                return true;
            }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    if (event->type() == QEvent::FontChange) updateGeometry();
    return QWidget::event(event);
}

#include "moc_twintimeline.cpp"
