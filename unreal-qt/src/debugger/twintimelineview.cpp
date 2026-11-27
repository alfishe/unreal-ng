#include "debugger/twintimelineview.h"

#include <QAction>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "debugger/cardpanes.h"
#include "debugger/timeline/twintimeline.h"
#include "emulator/emulator.h"
#include "emulator/state/statenode.h"

namespace
{
constexpr int kLabelWidth = 46;
constexpr int kMarkRow = 12;
constexpr int kLaneHeight = 22;
constexpr int kAxisHeight = 18;
constexpr int kPad = 3;

double Num(const StateNode* node, const std::string& key, double fallback = 0.0)
{
    const StateNode* found = node ? node->find(key) : nullptr;
    if (!found)
        return fallback;
    switch (found->kind)
    {
        case StateNode::Kind::Double: return found->d;
        case StateNode::Kind::Int: return static_cast<double>(found->i);
        case StateNode::Kind::Bool: return found->b ? 1.0 : 0.0;
        default: return fallback;
    }
}

int Int(const StateNode* node, const std::string& key)
{
    return static_cast<int>(Num(node, key));
}

QString Str(const StateNode* node, const std::string& key)
{
    const StateNode* found = node ? node->find(key) : nullptr;
    return found && found->kind == StateNode::Kind::String ? QString::fromStdString(found->s) : QString();
}

bool Flag(const StateNode* node, const std::string& key)
{
    const StateNode* found = node ? node->find(key) : nullptr;
    return found && found->kind == StateNode::Kind::Bool && found->b;
}

QString Hex4(int value)
{
    return QStringLiteral("#%1").arg(value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
}

QString OneDecimal(double value)
{
    return QString::number(value, 'f', 1);
}

/// "34,995.6"
QString Grouped(double value, int decimals)
{
    QString text = QString::number(std::abs(value), 'f', decimals);
    const int dot = decimals ? static_cast<int>(text.indexOf(QLatin1Char('.'))) : static_cast<int>(text.size());
    for (int i = dot - 3; i > 0; i -= 3)
        text.insert(i, QLatin1Char(','));
    return value < 0 ? QLatin1Char('-') + text : text;
}

/// The segment's short name: "OCF", "OD", "MR", "MW", "IO" and "+" for T with nothing on the bus
QString SegmentName(const QString& kind)
{
    if (kind == QLatin1String("pr") || kind == QLatin1String("pw"))
        return QStringLiteral("IO");
    if (kind == QLatin1String("int"))
        return QStringLiteral("+");
    return kind.toUpper();
}

QString SegmentDescription(const QString& kind)
{
    static const std::pair<const char*, const char*> names[] = {{"ocf", "opcode fetch"}, {"od", "operand read"},
                                                                {"mr", "memory read"},   {"mw", "memory write"},
                                                                {"pr", "port read"},     {"pw", "port write"}};
    for (const auto& [k, d] : names)
        if (kind == QLatin1String(k))
            return QString::fromLatin1(d);
    return QStringLiteral("internal (no bus cycle)");
}

/// A nice axis step (1, 2, 5 x 10^n) for about `target` T between ticks
double NiceStep(double target)
{
    const double base = std::pow(10.0, std::floor(std::log10(std::max(target, 1e-9))));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (base * m >= target)
            return base * m;
    return base * 10.0;
}

struct Palette
{
    bool dark = false;
    QColor canvas, laneBg, grid, text, dim;
    QColor mainFill, mainEdge, cardFill, cardEdge, idleFill, idleEdge, intFill, intEdge;
    QColor now, breakpoint, port, clock, elapsed;
};

Palette PaletteOf(const QWidget* w, bool neoGS)
{
    Palette c;
    const QPalette& pal = w->palette();
    c.dark = pal.color(QPalette::Window).lightness() < 128;
    c.text = pal.color(QPalette::WindowText);
    c.dim = c.dark ? QColor(150, 150, 150) : QColor(105, 105, 105);
    if (c.dark)
    {
        c.canvas = QColor(30, 31, 34);
        c.laneBg = QColor(38, 40, 44);
        c.grid = QColor(62, 64, 70);
        c.mainFill = QColor(44, 72, 110);
        c.mainEdge = QColor(110, 150, 210);
        c.cardFill = neoGS ? QColor(72, 52, 106) : QColor(92, 70, 30);
        c.cardEdge = neoGS ? QColor(166, 130, 230) : QColor(214, 166, 70);
        c.idleFill = QColor(56, 56, 58);
        c.idleEdge = QColor(96, 96, 100);
        c.intFill = QColor(110, 40, 40);
        c.intEdge = QColor(230, 90, 90);
        c.now = QColor(255, 90, 70);
        c.breakpoint = QColor(240, 70, 70);
        c.port = QColor(70, 210, 190);
        c.clock = QColor(190, 150, 255);
        c.elapsed = QColor(255, 255, 255, 40);
    }
    else
    {
        c.canvas = QColor(250, 250, 250);
        c.laneBg = QColor(240, 241, 244);
        c.grid = QColor(214, 216, 222);
        c.mainFill = QColor(206, 224, 248);
        c.mainEdge = QColor(92, 130, 190);
        c.cardFill = neoGS ? QColor(228, 214, 246) : QColor(248, 226, 172);
        c.cardEdge = neoGS ? QColor(130, 90, 196) : QColor(190, 136, 30);
        c.idleFill = QColor(226, 226, 228);
        c.idleEdge = QColor(170, 170, 174);
        c.intFill = QColor(246, 200, 200);
        c.intEdge = QColor(200, 60, 60);
        c.now = QColor(220, 40, 30);
        c.breakpoint = QColor(210, 40, 40);
        c.port = QColor(10, 140, 125);
        c.clock = QColor(130, 80, 200);
        c.elapsed = QColor(0, 0, 0, 28);
    }
    return c;
}

void PortGlyph(QPainter& p, QPointF c, const QColor& color)
{
    // Two arrows, one each way: a host port access
    p.setPen(QPen(color, 1.4));
    p.drawLine(QPointF(c.x() - 4, c.y() - 2), QPointF(c.x() + 4, c.y() - 2));
    p.drawLine(QPointF(c.x() + 4, c.y() - 2), QPointF(c.x() + 2, c.y() - 4));
    p.drawLine(QPointF(c.x() - 4, c.y() + 2), QPointF(c.x() + 4, c.y() + 2));
    p.drawLine(QPointF(c.x() - 4, c.y() + 2), QPointF(c.x() - 2, c.y() + 4));
}
}  // namespace

TwinTimelineView::TwinTimelineView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("debuggerTwinTimeline"));
    _client = QStringLiteral("qt-debugger-%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setToolTip(QString());  // tooltips come from event()
    _live = new CardPanes::LiveLine(
        this, [this]() { return _emulator; }, [this](EmulatorContext*) { read(true); }, kRunningIntervalMs);
    _retry = new QTimer(this);
    _retry->setSingleShot(true);
    _retry->setInterval(kRetryMs);
    connect(_retry, &QTimer::timeout, this, [this]() {
        if (isVisible() && _emulator && !_emulator->IsDestroying())
            read(false);
    });
    connect(&TwinTimelineSettings::Instance(), &TwinTimelineSettings::changed, this, [this]() {
        if (_followed)
            applyShown();
    });
}

TwinTimelineView::~TwinTimelineView()
{
    arm(false);
}

QSize TwinTimelineView::sizeHint() const
{
    return {720, minimumSizeHint().height()};
}

QSize TwinTimelineView::minimumSizeHint() const
{
    return {360, 2 * kMarkRow + 2 * kLaneHeight + kAxisHeight + 2 * kPad};
}

// region <Lease and reads>

void TwinTimelineView::setEmulator(std::shared_ptr<Emulator> emulator)
{
    if (emulator == _emulator)
        return;
    arm(false);
    _emulator = std::move(emulator);
    _detail = false;
    _running = false;
    _density.clear();
    for (auto& lane : _blocks)
        lane.clear();
    _markers.clear();
    _segments.clear();
    if (_emulator && isVisible())
    {
        arm(true);
        refresh();
    }
    _live->update();
    update();
}

void TwinTimelineView::follow(std::shared_ptr<Emulator> emulator, DebugCpuId cardCpu)
{
    _followed = std::move(emulator);
    setCardCpu(cardCpu);
    applyShown();
}

void TwinTimelineView::applyShown()
{
    const bool on = _followed && TwinTimelineSettings::Instance().Shown();
    setEmulator(on ? _followed : nullptr);
    if (isHidden() == on)
        setVisible(on);  // shown: it arms and reads; hidden: released
    else if (on)
        refresh();
}

void TwinTimelineView::setCardCpu(DebugCpuId cpu)
{
    _cardCpu = cpu;
}

void TwinTimelineView::arm(bool on)
{
    if (on == _leased)
        return;
    if (!_emulator || _emulator->IsDestroying())
    {
        _leased = false;
        return;
    }
    const timeline::Reply reply = timeline::SetArmed(_emulator.get(), _client.toStdString(), on, 0);
    _leased = on && reply.status == 200;
}

void TwinTimelineView::refresh()
{
    if (!isVisible() || !_emulator || _emulator->IsDestroying())
        return;
    _retriesLeft = kRetries;  // a new stop: a fresh round of tries
    read(false);
}

void TwinTimelineView::read(bool live)
{
    if (!_emulator)
        return;
    if (!_leased)
        arm(true);
    timeline::Query query;
    query.cpu = IsCardCpu(_cardCpu) ? DebugCpuName(_cardCpu) : "";
    query.spanT = std::clamp(_span + std::abs(_center), 1.0, timeline::kMaxSpanT);
    query.recent = timeline::kMaxRecent;
    query.density = 50;
    query.client = _client.toStdString();
    query.leaseMs = 0;
    query.arm = false;  // the lease is the view's (shown: armed), not each read's
    query.waitMs = 0;   // the GUI thread: one try, never a wait on the emulation thread
    // Stepped by another thread (its end refreshes) or a stop not parked yet: not now, a little later
    if (!live && (_emulator->IsDirectStepping() || (_emulator->IsPaused() && !_emulator->IsEmulationParked())))
    {
        retryLater();
        return;
    }
    if (live)
        _runningReads++;
    _reads++;
    timeline::Reply reply = timeline::Read(_emulator.get(), query);
    if (reply.status == 404 && !query.cpu.empty())
    {
        query.cpu.clear();  // the card in front went away: the first one there
        reply = timeline::Read(_emulator.get(), query);
    }
    if (reply.status == 503 && !live)
    {
        retryLater();  // another client holds the machine for a moment: the last picture stays
        return;
    }
    if (reply.status != 200)
    {
        _message = QString::fromStdString(reply.error);
        update();
        return;
    }
    _retry->stop();
    // A running read whose density did not move: nothing to draw again
    const quint64 revision = static_cast<quint64>(Num(&reply.body, "revision", -1));
    if (live && _running && Str(&reply.body, "state") == QLatin1String("running") && revision == _revision)
        return;
    take(reply.body);
    if (!_running)
        _readSpan = query.spanT;
    update();
}

void TwinTimelineView::retryLater()
{
    _busyReads++;
    if (_retriesLeft <= 0)
    {
        _message = tr("The machine is busy (stepped or held by another client): the picture is of an earlier moment");
        update();
        return;
    }
    _retriesLeft--;
    if (!_retry->isActive())
        _retry->start();
}

void TwinTimelineView::take(const StateNode& node)
{
    _message.clear();
    _state = Str(&node, "state");
    _running = _state == QLatin1String("running");
    _detail = !_running && Flag(&node, "available");
    _cardCpuName = Str(&node, "cpu");
    _revision = static_cast<quint64>(Num(&node, "revision"));
    for (int i = 0; i < 2; i++)
    {
        _laneName[i].clear();
        _readout[i].clear();
        _blocks[i].clear();
        _next[i] = Next{};
    }
    _markers.clear();
    _segments.clear();
    _density.clear();
    _neoGS = false;
    if (!Flag(&node, "available"))
        _message = Str(&node, "message");

    if (const StateNode* lanes = node.find("lanes"))
        for (size_t i = 0; i < lanes->items.size() && i < 2; i++)
        {
            const StateNode& lane = lanes->items[i];
            _laneName[i] = i == 0 ? QStringLiteral("MAIN") : Str(&lane, "cpu").toUpper();
            _readout[i] = Str(&lane, "readout");
            if (i == 1)
                _neoGS = Str(&lane, "name").startsWith(QLatin1String("NeoGS"));
        }

    if (const StateNode* density = node.find("density"))
        for (const StateNode& t : density->items)
        {
            Tick tick;
            tick.frame = static_cast<qint64>(Num(&t, "frame"));
            const StateNode* m = t.find("main");
            tick.commands = Int(m, "commands");
            tick.dataWrites = Int(m, "data_writes");
            tick.dataReads = Int(m, "data_reads");
            tick.statusPolls = Int(m, "status_polls");
            tick.accesses = Int(m, "accesses");
            const StateNode* c = t.find("card");
            tick.steps = Int(c, "steps");
            tick.idleSteps = Int(c, "idle_steps");
            tick.busySteps = Int(c, "busy_steps");
            tick.interrupts = Int(c, "interrupts");
            tick.nmis = Int(c, "nmis");
            tick.profiled = Flag(c, "profiled");
            tick.cycles = static_cast<qint64>(Num(c, "cycles"));
            tick.busyCycles = static_cast<qint64>(Num(c, "busy_cycles"));
            _density.push_back(tick);
        }
    if (_running)
        return;

    const StateNode* now = node.find("now");
    _nowT = Num(now, "t");
    _nowFrame = static_cast<qint64>(Num(now, "frame"));
    _gap = Num(now, "gap_t");

    const QString cardKey = _cardCpuName;
    auto lanesOf = [&](const StateNode* parent, Lane lane) -> const StateNode* {
        if (!parent)
            return nullptr;
        return parent->find(lane == MainLane ? std::string("main") : cardKey.toStdString());
    };
    for (Lane lane : {MainLane, CardLane})
    {
        if (lane == CardLane && cardKey.isEmpty())
            continue;
        if (const StateNode* list = lanesOf(node.find("recent"), lane))
            for (const StateNode& item : list->items)
            {
                Block b;
                b.at = Num(&item, "at");
                b.duration = Num(&item, "duration_t");
                b.pc = Int(&item, "pc");
                b.kind = Str(&item, "kind");
                b.text = Str(&item, "text");
                b.label = Str(&item, "label");
                b.open = Flag(&item, "open");
                b.idleIterations = static_cast<qint64>(Num(&item, "idle_iterations"));
                _blocks[lane].push_back(b);
            }
        if (const StateNode* n = lanesOf(node.find("next"), lane))
        {
            _next[lane].known = true;
            _next[lane].at = Num(n, "at");
            _next[lane].pc = Int(n, "pc");
            _next[lane].text = Str(n, "text");
            _next[lane].label = Str(n, "label");
        }
    }
    if (const StateNode* markers = node.find("markers"))
        for (const StateNode& m : markers->items)
        {
            Marker marker;
            marker.lane = Str(&m, "lane") == QLatin1String("main") ? MainLane : CardLane;
            marker.kind = Str(&m, "kind");
            marker.at = Num(&m, "at");
            marker.pc = Int(&m, "pc");
            marker.text = Str(&m, "text");
            _markers.push_back(marker);
        }

    // A card's stop: main's instruction the card's moment (NOW) falls in, in its bus cycles; the part before NOW shaded
    const StateNode* position = node.find("position");
    const StateNode* main = position ? position->find("main") : nullptr;
    const StateNode* cycles = main ? main->find("bus_cycles") : nullptr;
    _segmentBlock = -1;
    if (cycles && !cycles->items.empty())
    {
        const std::vector<Block>& blocks = _blocks[MainLane];
        for (size_t i = blocks.size(); i-- > 0;)
            if (blocks[i].at <= 0.0 && (blocks[i].open || blocks[i].at + blocks[i].duration > 0.0))
            {
                _segmentBlock = static_cast<int>(i);
                break;
            }
    }
    if (_segmentBlock >= 0)
    {
        double at = _blocks[MainLane][static_cast<size_t>(_segmentBlock)].at;
        for (const StateNode& cycle : cycles->items)
        {
            Segment s;
            s.kind = Str(&cycle, "kind");
            s.at = at;
            s.duration = Num(&cycle, "t");
            at += s.duration;
            _segments.push_back(s);
        }
    }
}

void TwinTimelineView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    arm(true);
    refresh();
    _live->update();
}

void TwinTimelineView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _retry->stop();
    _live->update();
    arm(false);  // nothing records for a strip nobody sees
}

// endregion </Lease and reads>

// region <The view>

QString TwinTimelineView::nowReadout() const
{
    if (_running)
        return _density.empty() ? tr("running") : tr("running · frame %1").arg(_density.back().frame);
    if (!_detail)
        return QString();
    QString text = tr("T %1 · frame %2").arg(Grouped(_nowT, 1)).arg(_nowFrame);
    if (_gap != 0.0)
        text += tr(" · gap %1 T").arg(OneDecimal(_gap));
    return text;
}

void TwinTimelineView::setSpan(double span)
{
    _span = std::clamp(span, kMinSpanT, kMaxSpanT);
    viewChanged();
}

void TwinTimelineView::setCenter(double center)
{
    _center = std::clamp(center, -kMaxSpanT, kMaxSpanT);
    viewChanged();
}

void TwinTimelineView::zoomBy(double steps, double anchor)
{
    const double span = std::clamp(_span * std::pow(1.25, -steps), kMinSpanT, kMaxSpanT);
    // The T under the pointer stays where it is
    _center = std::clamp(anchor - (anchor - _center) * span / _span, -kMaxSpanT, kMaxSpanT);
    _span = span;
    viewChanged();
}

void TwinTimelineView::panByPixels(int dx)
{
    const Geometry g = geometry();
    const double width = std::max(1, g.right - g.left);
    setCenter(_center - dx * 2.0 * _span / width);
}

void TwinTimelineView::recenter()
{
    setCenter(0.0);
}

void TwinTimelineView::viewChanged()
{
    // The detail covers what the last read asked: a wider view reads again (paused: no running machine to park)
    if (_detail && _span + std::abs(_center) > _readSpan + 0.5)
        refresh();
    update();
}

TwinTimelineView::Geometry TwinTimelineView::geometry() const
{
    Geometry g;
    const int readout = std::clamp(width() * 30 / 100, 180, 330);
    g.left = kLabelWidth;
    g.right = std::max(g.left + 40, width() - readout);
    g.mainMarkTop = kPad;
    g.mainTop = g.mainMarkTop + kMarkRow;
    g.mainBottom = g.mainTop + kLaneHeight;
    g.axisTop = g.mainBottom;
    g.axisBottom = g.axisTop + kAxisHeight;
    g.cardTop = g.axisBottom;
    g.cardBottom = g.cardTop + kLaneHeight;
    g.cardMarkBottom = g.cardBottom + kMarkRow;
    return g;
}

double TwinTimelineView::pixelsPerT() const
{
    const Geometry g = geometry();
    return static_cast<double>(g.right - g.left) / (2.0 * _span);
}

bool TwinTimelineView::merged() const
{
    return pixelsPerT() < kMergePixelsPerT;
}

double TwinTimelineView::xOfT(double t) const
{
    const Geometry g = geometry();
    return g.left + (t - (_center - _span)) * pixelsPerT();
}

QRectF TwinTimelineView::blockRect(Lane lane, size_t index) const
{
    if (index >= _blocks[lane].size())
        return {};
    const Geometry g = geometry();
    const Block& b = _blocks[lane][index];
    const int top = lane == MainLane ? g.mainTop : g.cardTop;
    const double x0 = xOfT(b.at);
    const double x1 = xOfT(b.at + b.duration);
    return {x0, static_cast<double>(top + 2), std::max(1.0, x1 - x0), static_cast<double>(kLaneHeight - 4)};
}

QRectF TwinTimelineView::nextRect(Lane lane) const
{
    const Next& n = _next[lane];
    if (!n.known)
        return {};
    const Geometry g = geometry();
    const int top = lane == MainLane ? g.mainTop : g.cardTop;
    const QFontMetrics fm(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Its duration is not known yet: as wide as its text, within reason
    const double w = std::clamp(fm.horizontalAdvance(n.text) + 12.0, 24.0, 150.0);
    return {xOfT(n.at), static_cast<double>(top + 2), w, static_cast<double>(kLaneHeight - 4)};
}

DebugCpuId TwinTimelineView::laneCpu(Lane lane) const
{
    if (lane == MainLane)
        return DebugCpuId::Main;
    return _cardCpuName == QLatin1String("ms") ? DebugCpuId::MultiSoundGS : DebugCpuId::GS;
}

int TwinTimelineView::blockAt(Lane lane, const QPoint& pos) const
{
    for (size_t i = 0; i < _blocks[lane].size(); i++)
    {
        const QRectF r = blockRect(lane, i).adjusted(0, -2, 0, 2);
        if (r.contains(pos))
            return static_cast<int>(i);
    }
    return -1;
}

bool TwinTimelineView::clickAt(const QPoint& pos)
{
    if (!_detail)
        return false;
    const Geometry g = geometry();
    if (pos.x() < g.left || pos.x() > g.right)
        return false;
    for (Lane lane : {MainLane, CardLane})
    {
        const int top = lane == MainLane ? g.mainTop : g.cardTop;
        if (pos.y() < top || pos.y() >= top + kLaneHeight)
            continue;
        if (_next[lane].known && nextRect(lane).contains(pos))
        {
            emit cpuTargetRequested(laneCpu(lane), static_cast<uint16_t>(_next[lane].pc));
            return true;
        }
        const int index = blockAt(lane, pos);
        if (index < 0 || _blocks[lane][static_cast<size_t>(index)].kind == QLatin1String("int") ||
            _blocks[lane][static_cast<size_t>(index)].kind == QLatin1String("nmi"))
            return false;
        emit cpuTargetRequested(laneCpu(lane), static_cast<uint16_t>(_blocks[lane][static_cast<size_t>(index)].pc));
        return true;
    }
    return false;
}

int TwinTimelineView::markerAt(const QPoint& pos) const
{
    const Geometry g = geometry();
    int best = -1;
    double bestDistance = 6.0;
    for (size_t i = 0; i < _markers.size(); i++)
    {
        const Marker& m = _markers[i];
        const int y = m.lane == MainLane ? g.mainMarkTop + kMarkRow / 2 : g.cardBottom + kMarkRow / 2;
        if (std::abs(pos.y() - y) > kMarkRow / 2 + 1)
            continue;
        const double d = std::abs(pos.x() - xOfT(m.at));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

QString TwinTimelineView::blockTip(Lane lane, const Block& b) const
{
    QString what = b.kind == QLatin1String("int")   ? tr("INT acknowledge")
                   : b.kind == QLatin1String("nmi") ? tr("NMI acknowledge")
                                                    : b.text;
    QString tip = QStringLiteral("%1 %2 %3").arg(lane == MainLane ? tr("main") : _cardCpuName, Hex4(b.pc), what);
    if (!b.label.isEmpty())
        tip += QStringLiteral("  ; ") + b.label;
    tip += tr("\n%1 T from %2 T (NOW = 0)").arg(OneDecimal(b.duration), OneDecimal(b.at));
    if (b.idleIterations)
        tip += tr("\nthe idle loop, %1 iteration(s) slept through").arg(b.idleIterations);
    if (b.open)
        tip += tr("\nunder way");
    if (b.kind != QLatin1String("int") && b.kind != QLatin1String("nmi"))
        tip += tr("\nClick: this CPU's disassembly here");
    return tip;
}

QString TwinTimelineView::toolTipAt(const QPoint& pos) const
{
    const Geometry g = geometry();
    if (!_detail)
    {
        if (_running && !_density.empty() && pos.x() >= g.left && pos.x() < g.right)
        {
            const size_t n = _density.size();
            const size_t i = std::min(n - 1, static_cast<size_t>((pos.x() - g.left) * static_cast<int>(n) /
                                                                 std::max(1, g.right - g.left)));
            const Tick& t = _density[i];
            return tr("frame %1\nmain: %2 GS port access(es): %3 command, %4 data out, %5 data in, %6 status\n"
                      "card: %7 step(s), %8 idle, %9 interrupt(s)")
                .arg(t.frame)
                .arg(t.accesses)
                .arg(t.commands)
                .arg(t.dataWrites)
                .arg(t.dataReads)
                .arg(t.statusPolls)
                .arg(t.steps)
                .arg(t.idleSteps)
                .arg(t.interrupts);
        }
        return QString();
    }
    const int marker = markerAt(pos);
    if (marker >= 0)
        return _markers[static_cast<size_t>(marker)].text;
    for (const Segment& s : _segments)
    {
        const double x0 = xOfT(s.at);
        const double x1 = xOfT(s.at + s.duration);
        if (pos.x() >= x0 && pos.x() < x1 && pos.y() >= g.mainTop && pos.y() < g.mainBottom)
            return tr("%1, %2 T").arg(SegmentDescription(s.kind)).arg(s.duration);
    }
    for (Lane lane : {MainLane, CardLane})
    {
        if (_next[lane].known && nextRect(lane).contains(pos))
            return tr("%1 %2 %3 (next)\nClick: this CPU's disassembly here")
                .arg(lane == MainLane ? tr("main") : _cardCpuName, Hex4(_next[lane].pc), _next[lane].text);
        const int index = blockAt(lane, pos);
        if (index >= 0)
            return blockTip(lane, _blocks[lane][static_cast<size_t>(index)]);
    }
    if (pos.y() >= g.axisTop && pos.y() < g.axisBottom)
        return tr("Main T relative to NOW. Wheel: zoom · drag: pan · double-click: back to NOW");
    return QString();
}

bool TwinTimelineView::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString text = toolTipAt(help->pos());
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

void TwinTimelineView::wheelEvent(QWheelEvent* event)
{
    if (!_detail)
    {
        QWidget::wheelEvent(event);
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    const double ppt = pixelsPerT();
    const double anchor = (event->position().x() - geometry().left) / ppt + (_center - _span);
    zoomBy(steps, anchor);
    event->accept();
}

void TwinTimelineView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _press = event->position().toPoint();
        _pressCenter = _center;
        _dragged = false;
    }
    QWidget::mousePressEvent(event);
}

void TwinTimelineView::mouseMoveEvent(QMouseEvent* event)
{
    if (_press && (event->buttons() & Qt::LeftButton) && _detail)
    {
        const int dx = event->position().toPoint().x() - _press->x();
        if (std::abs(dx) > 3)
            _dragged = true;
        if (_dragged)
        {
            const Geometry g = geometry();
            setCenter(_pressCenter - dx * 2.0 * _span / std::max(1, g.right - g.left));
            setCursor(Qt::ClosedHandCursor);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TwinTimelineView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && _press)
    {
        if (!_dragged)
            clickAt(event->position().toPoint());
        _press.reset();
        _dragged = false;
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void TwinTimelineView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        recenter();
    QWidget::mouseDoubleClickEvent(event);
}

// endregion </The view>

// region <Painting>

void TwinTimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const Palette c = PaletteOf(this, _neoGS);
    const Geometry g = geometry();
    p.fillRect(rect(), c.canvas);
    p.fillRect(QRect(g.left, g.mainTop, g.right - g.left, kLaneHeight), c.laneBg);
    p.fillRect(QRect(g.left, g.cardTop, g.right - g.left, kLaneHeight), c.laneBg);
    // The left edge accent: the card's colour, as the card line's
    p.fillRect(QRect(0, 0, 4, height()), c.cardEdge);

    // Lane names
    QFont labelFont = font();
    labelFont.setBold(true);
    labelFont.setPointSizeF(std::max(7.0, labelFont.pointSizeF() - 1.5));
    p.setFont(labelFont);
    p.setPen(c.text);
    p.drawText(QRect(8, g.mainTop, kLabelWidth - 10, kLaneHeight), Qt::AlignVCenter | Qt::AlignLeft,
               _laneName[0].isEmpty() ? QStringLiteral("MAIN") : _laneName[0]);
    p.setPen(c.now);
    p.drawText(QRect(8, g.axisTop, kLabelWidth - 10, kAxisHeight), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("NOW"));
    p.setPen(c.text);
    p.drawText(QRect(8, g.cardTop, kLabelWidth - 10, kLaneHeight), Qt::AlignVCenter | Qt::AlignLeft,
               _laneName[1].isEmpty() ? QStringLiteral("CARD") : _laneName[1]);

    if (_detail)
        paintDetail(p, g);
    else if (_running)
        paintDensity(p, g);
    else
    {
        QFont f = font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        p.setFont(f);
        p.setPen(c.dim);
        p.drawText(QRect(g.left, g.axisTop, g.right - g.left, kAxisHeight), Qt::AlignCenter,
                   _message.isEmpty() ? tr("The timeline fills at the next stop") : _message);
    }
    paintReadouts(p, g);
}

void TwinTimelineView::paintDetail(QPainter& p, const Geometry& g)
{
    p.save();
    p.setClipRect(QRect(g.left, 0, g.right - g.left, height()));
    paintAxis(p, g);
    if (merged())
    {
        paintMerged(p, g, MainLane, g.mainTop, g.mainBottom);
        paintMerged(p, g, CardLane, g.cardTop, g.cardBottom);
    }
    else
    {
        paintLane(p, g, MainLane, g.mainTop, g.mainBottom);
        paintLane(p, g, CardLane, g.cardTop, g.cardBottom);
        paintSegments(p, g);
    }
    paintMarkers(p, g);

    // NOW: the shared moment, through both lanes; main's own time (NOW + gap) when it is ahead of the card
    const Palette c = PaletteOf(this, _neoGS);
    const double nowX = xOfT(0.0);
    p.setPen(QPen(c.now, 2));
    p.drawLine(QPointF(nowX, g.mainMarkTop), QPointF(nowX, g.cardMarkBottom));
    if (_gap != 0.0)
    {
        const double gx = xOfT(_gap);
        p.setPen(QPen(c.now, 1, Qt::DashLine));
        p.drawLine(QPointF(gx, g.mainTop), QPointF(gx, g.mainBottom));
        p.drawLine(QPointF(nowX, g.mainBottom - 2), QPointF(gx, g.mainBottom - 2));
    }
    p.restore();
}

void TwinTimelineView::paintLane(QPainter& p, const Geometry& g, Lane lane, int top, int bottom)
{
    const Palette c = PaletteOf(this, _neoGS);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(std::max(7.0, font().pointSizeF() - 2.0));
    p.setFont(mono);
    const QFontMetrics fm(mono);
    const QColor fill = lane == MainLane ? c.mainFill : c.cardFill;
    const QColor edge = lane == MainLane ? c.mainEdge : c.cardEdge;
    const bool split = lane == MainLane && !_segments.empty();
    for (size_t i = 0; i < _blocks[lane].size(); i++)
    {
        const Block& b = _blocks[lane][i];
        if (split && static_cast<int>(i) == _segmentBlock)
            continue;  // main's instruction at NOW: drawn in its bus cycles
        const QRectF r = blockRect(lane, i);
        if (r.right() < g.left || r.left() > g.right)
            continue;
        const bool interrupt = b.kind == QLatin1String("int") || b.kind == QLatin1String("nmi");
        const bool idle = b.kind == QLatin1String("idle");
        const QColor blockEdge = interrupt ? c.intEdge : idle ? c.idleEdge : edge;
        if (r.width() < 4)
        {
            // Too narrow for an edge and a fill: one solid sliver, a gap of the lane between neighbours
            p.fillRect(r.adjusted(0, 0, r.width() >= 2 ? -1 : 0, 0), blockEdge);
            continue;
        }
        p.setPen(QPen(blockEdge, 1));
        p.setBrush(interrupt ? c.intFill : idle ? c.idleFill : fill);
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        QString text = interrupt ? b.kind.toUpper() : b.text;
        if (idle && b.idleIterations)
            text += QStringLiteral(" ×%1").arg(b.idleIterations);
        if (r.width() > fm.horizontalAdvance(QStringLiteral("ld a")) + 6)
        {
            p.setPen(c.text);
            const QRectF inner = r.adjusted(3, 0, -2, 0);
            p.drawText(inner, Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(text, Qt::ElideRight, static_cast<int>(inner.width())));
        }
    }
    p.setBrush(Qt::NoBrush);

    // The next instruction, outlined
    if (_next[lane].known)
    {
        const QRectF r = nextRect(lane);
        p.setPen(QPen(edge, 1.2, Qt::DashLine));
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setPen(c.dim);
        p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(_next[lane].text, Qt::ElideRight, static_cast<int>(r.width()) - 6));
    }
    (void)top;
    (void)bottom;
}

void TwinTimelineView::paintMerged(QPainter& p, const Geometry& g, Lane lane, int top, int bottom)
{
    // Zoomed out: blocks per bucket of pixels as a bar (non-idle instructions high, idle low, interrupts red)
    const Palette c = PaletteOf(this, _neoGS);
    constexpr int kBucket = 3;
    const int buckets = std::max(1, (g.right - g.left) / kBucket + 1);
    std::vector<int> busy(static_cast<size_t>(buckets), 0), idle(static_cast<size_t>(buckets), 0),
        ints(static_cast<size_t>(buckets), 0);
    for (const Block& b : _blocks[lane])
    {
        const int i = static_cast<int>((xOfT(b.at) - g.left) / kBucket);
        if (i < 0 || i >= buckets)
            continue;
        if (b.kind == QLatin1String("int") || b.kind == QLatin1String("nmi"))
            ints[static_cast<size_t>(i)]++;
        else if (b.kind == QLatin1String("idle"))
            idle[static_cast<size_t>(i)]++;
        else
            busy[static_cast<size_t>(i)]++;
    }
    int most = 1;
    for (int i = 0; i < buckets; i++)
        most = std::max(most, busy[static_cast<size_t>(i)] + idle[static_cast<size_t>(i)]);
    const int h = bottom - top - 4;
    const QColor fill = lane == MainLane ? c.mainEdge : c.cardEdge;
    for (int i = 0; i < buckets; i++)
    {
        const int x = g.left + i * kBucket;
        const int nb = busy[static_cast<size_t>(i)];
        const int ni = idle[static_cast<size_t>(i)];
        const int hb = nb ? std::max(2, nb * h / most) : 0;
        const int hi = ni ? std::max(2, ni * h / most) : 0;
        if (hi)
            p.fillRect(QRect(x, bottom - 2 - hi, kBucket - 1, hi), c.idleEdge);
        if (hb)
            p.fillRect(QRect(x, bottom - 2 - hi - hb, kBucket - 1, hb), fill);
        if (ints[static_cast<size_t>(i)])
            p.fillRect(QRect(x, top + 2, kBucket - 1, 3), c.intEdge);
    }
}

void TwinTimelineView::paintSegments(QPainter& p, const Geometry& g)
{
    if (_segments.empty())
        return;
    const Palette c = PaletteOf(this, _neoGS);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(std::max(7.0, font().pointSizeF() - 2.5));
    p.setFont(mono);
    const QFontMetrics fm(mono);
    const double top = g.mainTop + 2;
    const double h = kLaneHeight - 4;
    for (const Segment& s : _segments)
    {
        const double x0 = xOfT(s.at);
        const double x1 = xOfT(s.at + s.duration);
        const QRectF r(x0, top, std::max(1.0, x1 - x0), h);
        const bool port = s.kind == QLatin1String("pr") || s.kind == QLatin1String("pw");
        p.setPen(QPen(port ? c.port : c.mainEdge, 1));
        p.setBrush(c.mainFill);
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        // The part main has run: shaded
        const double done = std::min(x1, xOfT(0.0));
        if (done > x0)
            p.fillRect(QRectF(x0 + 1, top + 1, done - x0 - 1, h - 1), c.elapsed);
        const QString text = QStringLiteral("%1 %2").arg(SegmentName(s.kind)).arg(s.duration);
        if (r.width() > fm.horizontalAdvance(text) + 4)
        {
            p.setPen(port ? c.port : c.text);
            p.drawText(r, Qt::AlignCenter, text);
        }
        else if (r.width() > fm.horizontalAdvance(SegmentName(s.kind)) + 2)
        {
            p.setPen(port ? c.port : c.text);
            p.drawText(r, Qt::AlignCenter, SegmentName(s.kind));
        }
    }
    p.setBrush(Qt::NoBrush);
}

void TwinTimelineView::paintMarkers(QPainter& p, const Geometry& g)
{
    const Palette c = PaletteOf(this, _neoGS);
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 3.0));
    small.setBold(true);
    p.setFont(small);
    const QFontMetrics fm(small);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Marker& m : _markers)
    {
        const double x = xOfT(m.at);
        if (x < g.left - 8 || x > g.right + 8)
            continue;
        const double y = m.lane == MainLane ? g.mainMarkTop + kMarkRow / 2.0 : g.cardBottom + kMarkRow / 2.0;
        const int laneTop = m.lane == MainLane ? g.mainTop : g.cardTop;
        if (m.kind == QLatin1String("breakpoint"))
        {
            p.setPen(Qt::NoPen);
            p.setBrush(c.breakpoint);
            p.drawEllipse(QPointF(x, y), 4.0, 4.0);
        }
        else if (m.kind == QLatin1String("host_port"))
        {
            PortGlyph(p, QPointF(x, y), c.port);
            p.setPen(QPen(c.port, 1));
            p.drawLine(QPointF(x, laneTop), QPointF(x, laneTop + kLaneHeight));
        }
        else if (m.kind == QLatin1String("clock_change"))
        {
            QPainterPath diamond;
            diamond.moveTo(x, y - 5);
            diamond.lineTo(x + 5, y);
            diamond.lineTo(x, y + 5);
            diamond.lineTo(x - 5, y);
            diamond.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(c.clock);
            p.drawPath(diamond);
        }
        else
        {
            const QString text = m.kind.toUpper();  // INT, NMI
            const double w = fm.horizontalAdvance(text);
            p.setPen(c.intEdge);
            p.drawText(QRectF(x - w / 2 - 1, y - kMarkRow / 2.0, w + 2, kMarkRow), Qt::AlignCenter, text);
        }
    }
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void TwinTimelineView::paintAxis(QPainter& p, const Geometry& g)
{
    const Palette c = PaletteOf(this, _neoGS);
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 2.5));
    p.setFont(small);
    const QFontMetrics fm(small);
    const QString unit = merged() ? tr("T · merged") : tr("T");
    const int unitWidth = fm.horizontalAdvance(unit);
    const double step = NiceStep(70.0 / pixelsPerT());
    const double from = _center - _span;
    const double to = _center + _span;
    const int mid = (g.axisTop + g.axisBottom) / 2;
    p.setPen(c.grid);
    p.drawLine(g.left, mid, g.right, mid);
    for (double t = std::ceil(from / step) * step; t <= to; t += step)
    {
        const double x = xOfT(t);
        p.setPen(c.grid);
        p.drawLine(QPointF(x, g.mainTop), QPointF(x, g.mainBottom));
        p.drawLine(QPointF(x, g.cardTop), QPointF(x, g.cardBottom));
        p.drawLine(QPointF(x, mid - 2), QPointF(x, mid + 2));
        if (std::abs(t) < step / 2)
            continue;  // NOW labels itself
        const QString text = (t > 0 ? QStringLiteral("+") : QString()) + QString::number(t, 'g', 6);
        const int w = fm.horizontalAdvance(text);
        if (x - w / 2.0 - 2 < g.left || x + w / 2.0 + 2 > g.right - unitWidth - 6)
            continue;  // a label cut by an edge (or over the unit) is not drawn
        p.setPen(c.dim);
        p.fillRect(QRectF(x - w / 2.0 - 2, g.axisTop + 2, w + 4, kAxisHeight - 4), c.canvas);
        p.drawText(QRectF(x - w / 2.0 - 2, g.axisTop, w + 4, kAxisHeight), Qt::AlignCenter, text);
    }
    // The zoom: T per tick, bottom right of the axis
    p.setPen(c.dim);
    p.fillRect(QRect(g.right - unitWidth - 6, g.axisTop + 2, unitWidth + 6, kAxisHeight - 4), c.canvas);
    p.drawText(QRect(g.right - unitWidth - 4, g.axisTop, unitWidth + 2, kAxisHeight), Qt::AlignVCenter | Qt::AlignRight,
               unit);
}

void TwinTimelineView::paintDensity(QPainter& p, const Geometry& g)
{
    const Palette c = PaletteOf(this, _neoGS);
    if (_density.empty())
    {
        QFont f = font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        p.setFont(f);
        p.setPen(c.dim);
        p.drawText(QRect(g.left, g.axisTop, g.right - g.left, kAxisHeight), Qt::AlignCenter,
                   _leased ? tr("running: the density fills at the frame ends") : tr("running: not armed"));
        return;
    }
    const int n = static_cast<int>(_density.size());
    const double w = static_cast<double>(g.right - g.left) / std::max(n, 50);
    const double x0 = g.right - n * w;  // the newest at the right, as NOW
    int mostAccesses = 1;
    for (const Tick& t : _density)
        mostAccesses = std::max(mostAccesses, t.accesses);
    const int h = kLaneHeight - 4;
    for (int i = 0; i < n; i++)
    {
        const Tick& t = _density[static_cast<size_t>(i)];
        const double x = x0 + i * w;
        const double bw = std::max(1.0, w - 1);
        // MAIN: GS port accesses, commands darker at the bottom
        if (t.accesses)
        {
            const double ha = std::max(2.0, static_cast<double>(t.accesses) * h / mostAccesses);
            const double hc = t.commands ? std::max(1.0, static_cast<double>(t.commands) * h / mostAccesses) : 0.0;
            p.fillRect(QRectF(x, g.mainBottom - 2 - ha, bw, ha), c.mainFill.darker(c.dark ? 70 : 115));
            if (hc > 0)
                p.fillRect(QRectF(x, g.mainBottom - 2 - hc, bw, hc), c.mainEdge);
        }
        // CARD: busy over idle, the share of the frame's steps (cycles when profiled)
        const double total = t.profiled && t.cycles ? static_cast<double>(t.cycles) : static_cast<double>(t.steps);
        const double busy = t.profiled && t.cycles ? static_cast<double>(t.busyCycles) : static_cast<double>(t.busySteps);
        if (total > 0)
        {
            const double hb = busy > 0 ? std::max(1.0, busy * h / total) : 0.0;
            p.fillRect(QRectF(x, g.cardTop + 2, bw, h), c.idleFill);
            if (hb > 0)
                p.fillRect(QRectF(x, g.cardBottom - 2 - hb, bw, hb), c.cardEdge);
        }
        if (t.interrupts || t.nmis)
            p.fillRect(QRectF(x, g.cardBottom, bw, 3), c.intEdge);
    }
    // NOW: the newest frame
    p.setPen(QPen(c.now, 2));
    p.drawLine(QPointF(g.right - 1, g.mainMarkTop), QPointF(g.right - 1, g.cardMarkBottom));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 2.5));
    p.setFont(small);
    p.setPen(c.dim);
    p.drawText(QRect(g.left + 4, g.axisTop, g.right - g.left - 12, kAxisHeight), Qt::AlignVCenter | Qt::AlignLeft,
               tr("density per frame · the last %1 frames").arg(n));
}

void TwinTimelineView::paintReadouts(QPainter& p, const Geometry& g)
{
    const Palette c = PaletteOf(this, _neoGS);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    const QFontMetrics fm(f);
    const int x = g.right + 10;
    const int w = std::max(10, width() - x - 6);
    QString mainText = _readout[0];
    QString cardText = _readout[1];
    if (_running && !_density.empty())
    {
        // Averages over the frames shown
        double accesses = 0, commands = 0, busy = 0, total = 0, ints = 0;
        for (const Tick& t : _density)
        {
            accesses += t.accesses;
            commands += t.commands;
            const bool cycles = t.profiled && t.cycles;
            busy += cycles ? static_cast<double>(t.busyCycles) : t.busySteps;
            total += cycles ? static_cast<double>(t.cycles) : t.steps;
            ints += t.interrupts;
        }
        const double n = static_cast<double>(_density.size());
        mainText = tr("GS ports %1 / frame · commands %2").arg(OneDecimal(accesses / n), OneDecimal(commands / n));
        const QString clock = _readout[1];
        cardText = tr("busy %1% · INT %2 / frame").arg(total > 0 ? std::lround(100.0 * busy / total) : 0).arg(OneDecimal(ints / n));
        if (!clock.isEmpty())
            cardText += QStringLiteral(" · ") + clock;
    }
    p.setPen(c.text);
    p.drawText(QRect(x, g.mainTop, w, kLaneHeight), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(mainText, Qt::ElideRight, w));
    p.setPen(c.dim);
    p.drawText(QRect(x, g.axisTop, w, kAxisHeight), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(nowReadout(), Qt::ElideRight, w));
    p.setPen(c.text);
    p.drawText(QRect(x, g.cardTop, w, kLaneHeight), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(cardText, Qt::ElideRight, w));
}

// endregion </Painting>

// region <TwinTimelineSettings>

namespace
{
const char* kShowGsTimelineKey = "Debugger/ShowGsTimeline";
}  // namespace

TwinTimelineSettings& TwinTimelineSettings::Instance()
{
    static TwinTimelineSettings instance;
    return instance;
}

TwinTimelineSettings::TwinTimelineSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    _shown = settings.value(QLatin1String(kShowGsTimelineKey), false).toBool();
}

void TwinTimelineSettings::SetShown(bool shown)
{
    if (shown == _shown)
        return;
    _shown = shown;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(QLatin1String(kShowGsTimelineKey), shown);
    emit changed(shown);
}

QAction* TwinTimelineSettings::CreateAction(QObject* parent)
{
    QAction* action = new QAction(tr("Show GS &Timeline"), parent);
    action->setObjectName(QStringLiteral("showGsTimelineAction"));
    action->setCheckable(true);
    action->setChecked(_shown);
    action->setStatusTip(tr("Show the twin timeline in the debugger: the main CPU and the sound card's CPU on one "
                            "clock (General Sound, NeoGS, ZX-MultiSound); off, nothing is recorded or read"));
    connect(action, &QAction::triggered, this, [this](bool on) { SetShown(on); });
    connect(this, &TwinTimelineSettings::changed, action, &QAction::setChecked);
    return action;
}

// endregion </TwinTimelineSettings>
