// unreal-debugger (not in ecat): the GS analysis tabs on the card CPU's profiler and analysis (gui-card-debugger
// A5.2a Profiler, Events, Coverage, Heat map)

#include "gsprofiling.h"

#include <QBrush>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "addressmenu.h"
#include "debugstyle.h"

namespace
{
QString hex2(unsigned int v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X", v & 0xFF);
    return QString(b);
}

QString hex4(unsigned int v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%04X", v & 0xFFFF);
    return QString(b);
}

QString hex6(uint64_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "#%06llX", (unsigned long long)v);
    return QString(b);
}

QString grouped(int64_t v)
{
    QString digits = QString::number((qlonglong)(v < 0 ? -v : v));
    for (int i = digits.size() - 3; i > 0; i -= 3) digits.insert(i, ',');
    return v < 0 ? "-" + digits : digits;
}

QString pct(double v)
{
    return QString::number(v, 'f', 1) + "%";
}

const QString kDot = QString(" ") + QChar(0x00B7) + " ";

QTableWidgetItem * cell(const QString &text, bool right = false)
{
    QTableWidgetItem * item = new QTableWidgetItem(text);
    if (right) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

int64_t num(const QJsonObject &o, const char * key, int64_t def = 0)
{
    const QJsonValue v = o.value(key);
    return v.isDouble() ? (int64_t)v.toDouble() : def;
}

double real(const QJsonObject &o, const char * key)
{
    return o.value(key).toDouble();
}

//A section of a splitter: its title over its widgets
QWidget * section(QWidget * parent, QLabel * title, const std::vector<QWidget *> &widgets)
{
    QWidget * w = new QWidget(parent);
    QVBoxLayout * l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    if (title != nullptr) {
        title->setParent(w);
        l->addWidget(title);
    }
    for (QWidget * x : widgets) {
        x->setParent(w);
        l->addWidget(x, x->inherits("QTableWidget") ? 1 : 0);
    }
    return w;
}

void fit(QTableWidget * t)
{
    t->resizeColumnsToContents();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setUpdatesEnabled(true);
}

//"#0000-#00FF #0123" of [[start, end], ...] (offsets in a page)
QString ranges_text(const QJsonArray &ranges, bool truncated)
{
    QStringList out;
    for (const QJsonValue &v : ranges) {
        const QJsonArray r = v.toArray();
        const unsigned int s = (unsigned int)r.at(0).toDouble(), e = (unsigned int)r.at(1).toDouble();
        out << (e > s ? hex4(s) + "-" + hex4(e) : hex4(s));
    }
    if (truncated && !out.isEmpty()) out << QString(QChar(0x2026));
    return out.join(' ');
}
}

//------------------------------------------------------------------- GsReportTab

GsReportTab::GsReportTab(Emulator * e, const char * name, QWidget *parent):
    GsCardTab(e, name, parent)
{
    m_answer = new QLabel(this);
    m_answer->setObjectName(QString(name) + ":answer");
    m_answer->setWordWrap(true);
    m_answer->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_answer->hide();
}

GsReportTab::~GsReportTab()
{
    delete m_label_disasm;
}

void GsReportTab::restart()
{
    m_report = QJsonObject();
    m_answer->hide();
    delete m_label_disasm;
    m_label_disasm = nullptr;
    m_label_cpu = nullptr;
}

bool GsReportTab::read(const char * report, std::map<std::string, std::string> query)
{
    if (m_card.empty() || !e->loaded) return false;
    query["cpu"] = m_card;
    const Emulator::GsJson r = e->read_gs_report(report, query);
    if (!r.ok) {
        m_report = QJsonObject();
        m_line->setText(tr("Not read: %1").arg(QString::fromStdString(r.message)));
        return false;
    }
    m_report = QJsonDocument::fromJson(QByteArray::fromStdString(r.json)).object();
    if (!m_report.value("available").toBool(false)) {
        const QString why = m_report.value("description").toString();
        m_line->setText(why.isEmpty() ? tr("Not available on this card") : why);
        return false;
    }
    return true;
}

void GsReportTab::say(bool ok, const std::string &message)
{
    m_answer->setText(ok ? QString::fromStdString(message) : tr("refused: %1").arg(QString::fromStdString(message)));
    m_answer->show();
}

QString GsReportTab::label_at(unsigned int address)
{
    CPU * cpu = nullptr;
    if (e->dm != nullptr)
        for (CPU * c : e->dm->get_cpus())
            if (c->name == m_card) cpu = c;
    if (cpu == nullptr) return QString();
    if (cpu != m_label_cpu) {
        delete m_label_disasm;
        emulator::Result res;
        m_label_disasm = cpu->create_disasm(e->data_path, res);
        m_label_cpu = cpu;
    }
    if (m_label_disasm == nullptr) return QString();
    uint8_t b[15] = {0};
    const unsigned int n = std::min<unsigned int>(m_label_disasm->max_command_length, sizeof(b));
    for (unsigned int i = 0; i < n; i++) b[i] = (uint8_t)cpu->peek_mem((address + i) & 0xFFFF);
    DisasmLine l;
    m_label_disasm->decode(&b, address, n, &l);
    return QString::fromStdString(l.label);
}

//------------------------------------------------------------------- the budget bar, the activity graph

GsBudgetBar::GsBudgetBar(QWidget *parent):
    QWidget(parent)
{
    setObjectName("gsprofiler:bar");
    setMinimumHeight(14);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GsBudgetBar::set(double avg, double max, double period, bool any)
{
    m_avg = avg;
    m_max = max;
    m_period = period;
    m_any = any;
    update();
}

QSize GsBudgetBar::sizeHint() const
{
    return QSize(240, std::max(14, fontMetrics().height()));
}

void GsBudgetBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 1, -1, -2);
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(r, palette().base());
    p.setPen(c.separator);
    p.drawRect(r);
    if (!m_any || m_period <= 0) {
        p.setPen(c.dim);
        p.drawText(r, Qt::AlignCenter, tr("no ISR measured"));
        return;
    }
    const double used = std::min(1.0, m_avg / m_period);
    const QColor fill = used < 0.6 ? QColor(76, 175, 80) : used < 0.85 ? QColor(255, 167, 38) : QColor(229, 57, 53);
    p.fillRect(QRect(r.left() + 1, r.top() + 1, (int)std::lround((r.width() - 1) * used), r.height() - 1), fill);
    const int x = r.left() + (int)std::lround(r.width() * std::min(1.0, m_max / m_period));
    p.setPen(QPen(QColor(229, 57, 53), 2));
    p.drawLine(x, r.top(), x, r.bottom());
    p.setPen(c.text);
    p.drawText(r, Qt::AlignCenter, tr("%1 / %2 avg").arg(QString::number(m_avg, 'f', 0)).arg(QString::number(m_period, 'f', 0)));
}

GsActivityGraph::GsActivityGraph(QWidget *parent):
    QWidget(parent)
{
    setObjectName("gsprofiler:graph");
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GsActivityGraph::set(const std::vector<double> &busy, const std::vector<double> &isr)
{
    m_busy = busy;
    m_isr = isr;
    update();
}

QSize GsActivityGraph::sizeHint() const
{
    return QSize(240, 56);
}

void GsActivityGraph::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(r, palette().base());
    p.setPen(c.separator);
    p.drawRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
    auto line = [&](const std::vector<double> &v, const QColor &color) {
        if (v.size() < 2) return;
        QPolygonF poly;
        for (size_t i = 0; i < v.size(); i++)
            poly << QPointF(r.left() + 1 + (r.width() - 2) * (double)i / (double)(v.size() - 1),
                            r.bottom() - 1 - (r.height() - 2) * std::clamp(v[i], 0.0, 100.0) / 100.0);
        p.setPen(QPen(color, 1.5));
        p.drawPolyline(poly);
    };
    line(m_busy, QColor(66, 165, 245));
    line(m_isr, QColor(255, 152, 0));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(c.dim);
    p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft,
               m_busy.empty() ? tr("no profiled frame") : tr("busy % (blue), ISR % (orange), %1 frames").arg(m_busy.size()));
}

//------------------------------------------------------------------- Profiler

GsProfilerView::GsProfilerView(Emulator * e, QWidget *parent):
    GsReportTab(e, "card:gs-profiler", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_start = new QPushButton(tr("Start"), this);
    m_start->setObjectName("gsprofiler:start");
    m_start->setToolTip(tr("A new profiling session (cleared, then on). The card runs about twice as slow while it profiles"));
    m_stop = new QPushButton(tr("Stop"), this);
    m_stop->setObjectName("gsprofiler:stop");
    m_stop->setToolTip(tr("Stop profiling; the data stays"));
    m_clear = new QPushButton(tr("Clear"), this);
    m_clear->setObjectName("gsprofiler:clear");
    m_clear->setToolTip(tr("Clear the data (profiling goes on if it is on)"));
    connect(m_start, &QPushButton::clicked, this, [this]() { control("start"); });
    connect(m_stop, &QPushButton::clicked, this, [this]() { control("stop"); });
    connect(m_clear, &QPushButton::clicked, this, [this]() { control("clear"); });
    for (QPushButton * b : {m_start, m_stop, m_clear}) row->addWidget(b, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_layout->addWidget(m_answer);
    m_bar = new GsBudgetBar(this);
    m_layout->addWidget(m_bar);
    m_budget = new QLabel(this);
    m_budget->setObjectName("gsprofiler:budget");
    m_budget->setFont(dbg::font());
    m_budget->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_budget->setWordWrap(true);
    m_layout->addWidget(m_budget);
    m_state = new QLabel(this);
    m_state->setObjectName("gsprofiler:state");
    m_state->setWordWrap(true);
    m_state->hide();
    m_layout->addWidget(m_state);

    m_commands = make_table("gsprofiler:commands", QStringList() << tr("Command") << tr("Name") << tr("Handler") << tr("Count")
                                                                  << tr("Avg") << tr("Min") << tr("Max") << tr("Total"));
    m_routines = make_table("gsprofiler:routines", QStringList() << tr("Address") << tr("Page") << tr("Label") << tr("Calls")
                                                                  << tr("Inclusive") << tr("Exclusive") << tr("Avg") << tr("Max") << tr("%"));
    m_graph = new GsActivityGraph(this);
    m_activity = make_table("gsprofiler:activity", QStringList() << tr("Frame") << tr("ISR %") << tr("Idle %") << tr("Halt %")
                                                                  << tr("Busy %") << tr("Commands") << tr("INT") << tr("Overruns")
                                                                  << tr("Lost") << tr("ISR max"));
    QSplitter * split = new QSplitter(Qt::Vertical, this);
    split->setObjectName("gsprofiler:split");
    split->setChildrenCollapsible(false);
    split->addWidget(section(split, new QLabel(tr("Host commands (card cycles from the card's read to its next status poll, "
                                                  "interrupts apart)")), {m_commands}));
    split->addWidget(section(split, new QLabel(tr("Routines by inclusive cycles (interrupts never leak in; the ISR is its own "
                                                  "root)")), {m_routines}));
    split->addWidget(section(split, new QLabel(tr("Activity per frame, newest first")), {m_graph, m_activity}));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 1);
    m_layout->addWidget(split, 1);
}

void GsProfilerView::control(const char * action)
{
    if (m_card.empty() || AddressRouter::read_only_at(this)) return;
    const Emulator::GsJson a = e->gs_profiler(m_card, action);
    say(a.ok, a.message);
    refresh();
}

void GsProfilerView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    if (!read("profile", {{"routines", "64"}, {"activity", "128"}})) {
        for (QPushButton * b : {m_start, m_stop, m_clear}) b->hide();
        m_bar->set(0, 0, 0, false);
        m_budget->clear();
        m_state->hide();
        for (QTableWidget * t : {m_commands, m_routines, m_activity}) t->setRowCount(0);
        m_graph->set({}, {});
        return;
    }
    const QJsonObject &r = m_report;
    const bool on = r.value("on").toBool();
    const int64_t frames = num(r, "frames");
    const int64_t period = num(r, "period_cycles");
    m_start->setVisible(!on);
    m_stop->setVisible(on);
    m_clear->setVisible(true);
    QString line = tr("%1 (%2): profiler %3").arg(r.value("card").toString(), r.value("cpu").toString(), on ? tr("on") : tr("off"));
    line += kDot + tr("%1 frames profiled").arg(grouped(frames));
    if (period > 0) line += kDot + tr("interrupt period %1 cycles").arg(period);
    if (num(r, "clock_hz") > 0) line += kDot + tr("%1 MHz").arg(QString::number(num(r, "clock_hz") / 1e6, 'f', 2));
    if (!on) line += kDot + tr("off: Start profiles the card (it runs about twice as slow while on)");
    m_line->setText(line);

    const QJsonObject isr = r.value("isr").toObject();
    const QJsonObject last = isr.value("last_frame").toObject(), since = isr.value("since_start").toObject();
    auto isr_line = [period](const QString &what, const QJsonObject &s) {
        if (num(s, "count") == 0)
            return QObject::tr("%1: no interrupt ended").arg(what) +
                   (num(s, "lost") > 0 ? kDot + QObject::tr("%1 lost").arg(grouped(num(s, "lost"))) : QString()) +
                   (num(s, "nmis") > 0 ? kDot + QObject::tr("%1 NMIs").arg(grouped(num(s, "nmis"))) : QString());
        return QObject::tr("%1: %2 / %3 avg").arg(what, QString::number(real(s, "avg"), 'f', 0)).arg(period) + kDot +
               QObject::tr("%1 max").arg(num(s, "max")) + kDot + QObject::tr("%1 min").arg(num(s, "min")) + kDot +
               QObject::tr("%1 headroom").arg(QString::number(real(s, "headroom_pct"), 'f', 0) + "%") + kDot +
               QObject::tr("%1 ISRs").arg(grouped(num(s, "count"))) + kDot +
               QObject::tr("%1 overruns").arg(grouped(num(s, "overruns"))) + kDot +
               QObject::tr("latency %1 avg, %2 max").arg(QString::number(real(s, "latency_avg"), 'f', 1)).arg(num(s, "latency_max")) +
               kDot + QObject::tr("%1 lost").arg(grouped(num(s, "lost")));
    };
    m_budget->setText(tr("ISR budget") + "\n" + isr_line(tr("last frame"), last) + "\n" + isr_line(tr("since start"), since));
    const QJsonObject &gauge = num(last, "count") > 0 ? last : since;
    m_bar->set(real(gauge, "avg"), (double)num(gauge, "max"), (double)period, num(gauge, "count") > 0);
    m_bar->setToolTip(tr("The ISR's cycles per interrupt period: filled the average, the red mark the most (%1)")
                          .arg(num(last, "count") > 0 ? tr("last frame") : tr("since start")));

    //Why a part is empty, said rather than shown as an empty table
    const QJsonArray commands = r.value("commands").toArray();
    const QJsonArray routines = r.value("routines").toArray();
    QStringList state;
    if (frames > 0 && num(since, "count") == 0)
        state << tr("Interrupts off: the firmware idles with interrupts disabled (the stock GS v1.05a and NeoGS do until "
                    "they play), so no ISR has run: %1 interrupt requests lost. The budget fills when it plays.")
                     .arg(grouped(num(since, "lost")));
    if (frames > 0 && routines.isEmpty())
        state << tr("No routine called yet: the idle loop calls nothing. Routines show after a card reset (it CALLs its "
                    "set-up), a command whose handler calls, or while it plays.");
    if (frames > 0 && commands.isEmpty()) state << tr("No host command taken while profiling.");
    if (frames == 0 && on) state << tr("Profiling: the numbers come once the card has run a frame.");
    if (!r.value("current_command").isNull() && r.value("current_command").isObject()) {
        const QJsonObject cc = r.value("current_command").toObject();
        state << tr("Now in command %1 %2, %3 cycles so far").arg(hex2((unsigned int)num(cc, "code")), cc.value("name").toString(),
                                                                  grouped(num(cc, "cycles")));
    }
    m_state->setText(state.join("\n"));
    m_state->setVisible(!state.isEmpty());

    m_commands->setUpdatesEnabled(false);
    m_commands->setRowCount((int)commands.size());
    for (int i = 0; i < (int)commands.size(); i++) {
        const QJsonObject c = commands.at(i).toObject();
        m_commands->setItem(i, CColCode, cell(hex2((unsigned int)num(c, "code"))));
        m_commands->setItem(i, CColName, cell(c.value("name").toString()));
        m_commands->setItem(i, CColHandler, cell(c.value("handler").toString()));
        m_commands->setItem(i, CColCount, cell(grouped(num(c, "count")), true));
        m_commands->setItem(i, CColAvg, cell(grouped((int64_t)std::lround(real(c, "avg"))), true));
        m_commands->setItem(i, CColMin, cell(grouped(num(c, "min")), true));
        m_commands->setItem(i, CColMax, cell(grouped(num(c, "max")), true));
        m_commands->setItem(i, CColTotal, cell(grouped(num(c, "total")), true));
    }
    fit(m_commands);

    m_routines->setUpdatesEnabled(false);
    m_routines->setRowCount((int)routines.size());
    for (int i = 0; i < (int)routines.size(); i++) {
        const QJsonObject x = routines.at(i).toObject();
        const QString kind = x.value("page_kind").toString();
        QString label = x.value("label").toString();
        if (num(x, "interrupts") > 0) label += (label.isEmpty() ? "" : " ") + tr("(interrupt entry)");
        m_routines->setItem(i, RColAddress, cell(hex4((unsigned int)num(x, "address"))));
        m_routines->setItem(i, RColPage, cell(kind == "none" ? tr("-") : kind.toUpper() + " " + QString::number(num(x, "page"))));
        m_routines->setItem(i, RColLabel, cell(label));
        m_routines->setItem(i, RColCalls, cell(grouped(num(x, "calls")), true));
        m_routines->setItem(i, RColInclusive, cell(grouped(num(x, "inclusive")), true));
        m_routines->setItem(i, RColExclusive, cell(grouped(num(x, "exclusive")), true));
        m_routines->setItem(i, RColAvg, cell(grouped((int64_t)std::lround(real(x, "avg"))), true));
        m_routines->setItem(i, RColMax, cell(grouped(num(x, "max")), true));
        m_routines->setItem(i, RColPct, cell(pct(real(x, "inclusive_pct")), true));
        m_routines->item(i, RColAddress)->setData(Qt::UserRole, (int)num(x, "address"));
    }
    if (num(r, "routines_total") > (int64_t)routines.size())
        m_routines->setToolTip(tr("The top %1 of %2 routines").arg(routines.size()).arg(num(r, "routines_total")));
    fit(m_routines);

    const QJsonArray activity = r.value("activity").toArray();
    std::vector<double> busy, isr_pct;
    for (const QJsonValue &v : activity) {
        busy.push_back(real(v.toObject(), "busy_pct"));
        isr_pct.push_back(real(v.toObject(), "isr_pct"));
    }
    m_graph->set(busy, isr_pct);
    const int shown = std::min<int>((int)activity.size(), 64);
    m_activity->setUpdatesEnabled(false);
    m_activity->setRowCount(shown);
    for (int i = 0; i < shown; i++) {
        const QJsonObject a = activity.at(activity.size() - 1 - i).toObject();
        const QString cells[] = {QString::number(num(a, "frame")), pct(real(a, "isr_pct")), pct(real(a, "idle_pct")),
                                 pct(real(a, "halt_pct")), pct(real(a, "busy_pct")), QString::number(num(a, "commands")),
                                 QString::number(num(a, "interrupts")), QString::number(num(a, "overruns")),
                                 QString::number(num(a, "lost")), QString::number(num(a, "isr_max"))};
        for (int k = 0; k < 10; k++) m_activity->setItem(i, k, cell(cells[k], true));
    }
    fit(m_activity);
}

//------------------------------------------------------------------- Events

GsEventsCanvas::GsEventsCanvas(QWidget *parent):
    QWidget(parent)
{
    setObjectName("gsevents:canvas");
    setMouseTracking(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

const std::vector<const char *> &GsEventsCanvas::kinds()
{
    static const std::vector<const char *> k = {"host_data_write", "host_data_read", "host_command", "host_status", "host_control",
                                                "dac", "page", "volume", "zxdma_read", "zxdma_write"};
    return k;
}

QColor GsEventsCanvas::color(int kind, int64_t detail)
{
    switch (kind) {
    case 0: return QColor(255, 159, 28);
    case 1: return QColor(255, 209, 102);
    case 2: return QColor(230, 57, 70);
    case 3: return QColor(244, 163, 168);
    case 4: return QColor(214, 42, 214);
    case 5: return QColor::fromHsv((int)(100 + 22 * (detail % 8)) % 360, 200, 210);
    case 6: return QColor(0, 188, 212);
    case 7: return QColor(161, 110, 87);
    case 8: return QColor(200, 200, 200);
    case 9: return QColor(130, 130, 130);
    default: return QColor(128, 128, 128);
    }
}

QString GsEventsCanvas::kind_text(int kind)
{
    switch (kind) {
    case 0: return tr("host #B3 write");
    case 1: return tr("host #B3 read");
    case 2: return tr("host #BB command");
    case 3: return tr("host #BB status read");
    case 4: return tr("host #33");
    case 5: return tr("DAC fetch");
    case 6: return tr("page write");
    case 7: return tr("volume write");
    case 8: return tr("ZX-DMA read");
    case 9: return tr("ZX-DMA write");
    default: return tr("event");
    }
}

void GsEventsCanvas::set(int64_t period_cycles, std::vector<Row> rows)
{
    m_period = period_cycles;
    m_rows = std::move(rows);
    setFixedHeight(std::max<int>(row_height(), (int)m_rows.size() * row_height()));
    update();
}

QSize GsEventsCanvas::sizeHint() const
{
    return QSize((int)std::max<int64_t>(320, m_period), std::max<int>(row_height(), (int)m_rows.size() * row_height()));
}

double GsEventsCanvas::scale() const
{
    return m_period > 0 ? (double)width() / (double)m_period : 1.0;
}

void GsEventsCanvas::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    const QRect area = event->rect();
    p.fillRect(area, QColor(20, 20, 24));
    if (m_period <= 0 || m_rows.empty()) return;
    const double s = scale();
    const int h = row_height();
    //A faint line every 64 cycles, and every 50 periods
    p.setPen(QColor(48, 48, 56));
    for (int64_t c = 64; c < m_period; c += 64) p.drawLine((int)(c * s), area.top(), (int)(c * s), area.bottom());
    const int first = std::max(0, area.top() / h), last = std::min<int>((int)m_rows.size() - 1, area.bottom() / h);
    for (int i = (first / 50) * 50; i <= last; i += 50)
        if (i > 0) p.drawLine(area.left(), i * h, area.right(), i * h);
    for (int i = first; i <= last; i++) {
        const Row &r = m_rows[(size_t)i];
        const int y = i * h;
        for (const Bar &b : r.bars) {
            const int64_t end = b.end < 0 ? m_period : std::min(b.end, m_period);
            const int x0 = (int)(b.accept * s), x1 = std::max(x0 + 1, (int)(end * s));
            QColor c = b.nmi ? QColor(142, 68, 173) : QColor(58, 123, 213);
            if (b.end < 0) c = c.lighter(140);
            p.fillRect(QRect(x0, y, x1 - x0, h), c);
            if (b.end > m_period) p.fillRect(QRect(width() - 3, y, 3, h), QColor(229, 57, 53));
        }
        for (const Dot &d : r.dots) p.fillRect(QRect((int)(d.cycle * s) - 1, y, 3, h), color(d.kind, d.detail));
    }
}

QString GsEventsCanvas::describe(const QPoint &at) const
{
    if (m_period <= 0 || m_rows.empty()) return QString();
    const int i = at.y() / row_height();
    if (i < 0 || i >= (int)m_rows.size()) return QString();
    const Row &r = m_rows[(size_t)i];
    const double s = scale();
    const int64_t cycle = (int64_t)(at.x() / s);
    QStringList out;
    out << tr("period %1 (starts at cycle %2 of the frame) · cycle %3 of %4").arg(r.period).arg(grouped(r.start)).arg(cycle).arg(m_period);
    for (const Bar &b : r.bars)
        out << (b.end < 0 ? tr("%1 accepted at %2, still running at the frame's end").arg(b.nmi ? QStringLiteral("NMI") : QStringLiteral("INT")).arg(b.accept)
                          : tr("%1 accepted at %2, ends at %3: %4 cycles%5").arg(b.nmi ? QStringLiteral("NMI") : QStringLiteral("INT")).arg(b.accept).arg(b.end)
                                .arg(b.end - b.accept).arg(b.end > m_period ? tr(" (overrun: past its period)") : QString()));
    const int64_t reach = std::max<int64_t>(2, (int64_t)std::ceil(3.0 / s));
    int shown = 0;
    for (const Dot &d : r.dots)
        if (std::llabs(d.cycle - cycle) <= reach && shown++ < 6) {
            QString what = kind_text(d.kind);
            if (d.kind == 5 || d.kind == 7) what += tr(" ch %1").arg(d.detail);
            else if (d.kind == 6) what += tr(" port %1").arg(hex2((unsigned int)d.detail));
            else if (d.kind >= 8) what += tr(" card %1").arg(hex6((uint64_t)d.detail));
            out << tr("%1 = %2 at %3").arg(what, hex2((unsigned int)d.value)).arg(d.cycle);
        }
    if (r.bars.empty() && r.dots.empty()) out << tr("nothing in this period");
    return out.join("\n");
}

bool GsEventsCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent * he = static_cast<QHelpEvent *>(event);
        const QString text = describe(he->pos());
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

GsEventsView::GsEventsView(Emulator * e, QWidget *parent):
    GsReportTab(e, "card:gs-events", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_profiler = new QPushButton(tr("Start the profiler"), this);
    m_profiler->setObjectName("gsevents:profiler");
    m_profiler->setToolTip(tr("The frame map and the ISR bars come from the card's profiler (the card runs about twice as slow while on)"));
    m_profiler->hide();
    connect(m_profiler, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        const Emulator::GsJson a = this->e->gs_profiler(m_card, "start");
        say(a.ok, a.message);
        refresh();
    });
    row->addWidget(m_profiler, 0, Qt::AlignTop);
    m_trace = new QPushButton(tr("Start the trace"), this);
    m_trace->setObjectName("gsevents:trace");
    m_trace->setToolTip(tr("Start the GS port trace: the host's #B3 / #BB, the DAC fetches, the page and volume writes, ZX-DMA"));
    m_trace->hide();
    connect(m_trace, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        const Emulator::BoardAnswer a = this->e->gs_port_trace(m_card, "start");
        say(a.ok, a.message);
        refresh();
    });
    row->addWidget(m_trace, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_layout->addWidget(m_answer);
    m_hint = new QLabel(this);
    m_hint->setObjectName("gsevents:hint");
    m_hint->setWordWrap(true);
    m_hint->hide();
    m_layout->addWidget(m_hint);
    m_legend = new QLabel(this);
    m_legend->setObjectName("gsevents:legend");
    m_legend->setWordWrap(true);
    QString legend = QString("<span style='color:#3a7bd5'>&#9632;</span> ") + tr("ISR (INT)") + " &nbsp;" +
                     "<span style='color:#8e44ad'>&#9632;</span> " + tr("NMI") + " &nbsp;" +
                     "<span style='color:#e53935'>&#9632;</span> " + tr("overrun") + " &nbsp;";
    for (int k = 0; k < (int)GsEventsCanvas::kinds().size(); k++)
        legend += QString("<span style='color:%1'>&#9632;</span> ").arg(GsEventsCanvas::color(k, 0).name()) +
                  GsEventsCanvas::kind_text(k).toHtmlEscaped() + (k == 5 ? tr(" (hue by channel)") : QString()) + " &nbsp;";
    m_legend->setText(legend);
    m_layout->addWidget(m_legend);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("gsevents:scroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_canvas = new GsEventsCanvas(m_scroll);
    m_canvas->setToolTip(QString(" "));
    m_scroll->setWidget(m_canvas);
    m_layout->addWidget(m_scroll, 1);
}

void GsEventsView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    if (!read("events", {{"rows", "0"}})) {
        m_profiler->hide();
        m_trace->hide();
        m_hint->hide();
        m_canvas->set(0, {});
        return;
    }
    const QJsonObject &r = m_report;
    const bool on = r.value("on").toBool();
    const bool capturing = r.value("capturing").toBool();
    const bool framed = !r.value("frame").isNull() && r.value("frame").isDouble();
    m_profiler->setVisible(!on);
    m_trace->setVisible(!capturing);
    const int64_t period = num(r, "period_cycles");
    std::vector<GsEventsCanvas::Row> rows;
    int64_t dots = 0;
    for (const QJsonValue &v : r.value("rows").toArray()) {
        const QJsonObject o = v.toObject();
        GsEventsCanvas::Row row;
        row.period = num(o, "period");
        row.start = num(o, "start");
        for (const QJsonValue &b : o.value("isr").toArray()) {
            const QJsonObject x = b.toObject();
            GsEventsCanvas::Bar bar;
            bar.accept = num(x, "accept");
            bar.end = num(x, "end", -1);
            bar.nmi = x.value("nmi").toBool();
            row.bars.push_back(bar);
        }
        for (const QJsonValue &d : o.value("events").toArray()) {
            const QJsonArray a = d.toArray();
            GsEventsCanvas::Dot dot;
            dot.cycle = (int64_t)a.at(0).toDouble();
            const std::string kind = a.at(1).toString().toStdString();
            dot.kind = -1;
            for (int k = 0; k < (int)GsEventsCanvas::kinds().size(); k++)
                if (kind == GsEventsCanvas::kinds()[(size_t)k]) dot.kind = k;
            if (dot.kind < 0) continue;
            dot.value = (int)a.at(2).toDouble();
            dot.detail = (int64_t)a.at(3).toDouble();
            row.dots.push_back(dot);
            dots++;
        }
        rows.push_back(std::move(row));
    }
    const QJsonObject totals = r.value("totals").toObject();
    const int64_t isrs = num(totals, "isr");
    QString line = tr("%1 (%2)").arg(r.value("card").toString(), r.value("cpu").toString());
    if (framed)
        line += kDot + tr("host frame %1").arg(num(r, "frame")) + kDot + tr("%1 periods of %2 cycles").arg(num(r, "periods")).arg(period) +
                kDot + tr("%1 ISRs").arg(grouped(isrs)) + kDot + tr("%1 dots").arg(grouped(dots)) +
                (r.value("truncated").toBool() || r.value("events_truncated").toBool() ? kDot + tr("truncated") : QString());
    else
        line += kDot + tr("no profiled frame yet");
    m_line->setText(line);

    QStringList hint;
    if (!on)
        hint << tr("The profiler is off: the frame map and the ISR bars come with it (Start the profiler; the card runs about "
                   "twice as slow while on).");
    else if (!framed)
        hint << tr("No complete frame profiled yet: run the machine a frame.");
    if (!capturing)
        hint << tr("The GS port trace is off: no host #B3 / #BB, DAC, page, volume or ZX-DMA dots (Start the trace).");
    if (framed && isrs == 0)
        hint << tr("No ISR bars: the card took no interrupt in this frame (the stock firmware idles with interrupts off until "
                   "it plays).");
    m_hint->setText(hint.join("\n"));
    m_hint->setVisible(!hint.isEmpty());
    m_canvas->set(period, std::move(rows));
}

//------------------------------------------------------------------- Coverage

GsCoverageView::GsCoverageView(Emulator * e, QWidget *parent):
    GsReportTab(e, "card:gs-coverage", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_on = new QPushButton(tr("On"), this);
    m_on->setObjectName("gscoverage:on");
    m_on->setToolTip(tr("Mark the card's code and data (coverage, heat map, never-written reads): bound at the card's next "
                        "run; the card runs about 2.5 times as slow while on, the memory allocated now"));
    m_off = new QPushButton(tr("Off"), this);
    m_off->setObjectName("gscoverage:off");
    m_off->setToolTip(tr("Stop marking; the data stays"));
    m_reset = new QPushButton(tr("Reset"), this);
    m_reset->setObjectName("gscoverage:reset");
    m_reset->setToolTip(tr("Clear the marks"));
    m_release = new QPushButton(tr("Release"), this);
    m_release->setObjectName("gscoverage:release");
    m_release->setToolTip(tr("Drop the data and free its memory"));
    m_break_new = new QPushButton(tr("Break on new code"), this);
    m_break_new->setObjectName("gscoverage:break-new");
    m_break_new->setEnabled(false);
    m_break_new->setToolTip(tr("Not available: the core has no breakpoint on the first execution of new code yet "
                               "(gs-debugger X8)"));
    connect(m_on, &QPushButton::clicked, this, [this]() { control("on"); });
    connect(m_off, &QPushButton::clicked, this, [this]() { control("off"); });
    connect(m_reset, &QPushButton::clicked, this, [this]() { control("reset"); });
    connect(m_release, &QPushButton::clicked, this, [this]() { control("release"); });
    for (QPushButton * b : {m_on, m_off, m_reset, m_release, m_break_new}) row->addWidget(b, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_layout->addWidget(m_answer);
    m_pages = make_table("gscoverage:pages", QStringList() << tr("Space") << tr("Page") << tr("Exec") << tr("Code") << tr("Read")
                                                            << tr("Write") << tr("DAC") << tr("Written") << tr("Code ranges")
                                                            << tr("Data ranges") << tr("DAC ranges"));
    m_handlers_line = new QLabel(this);
    m_handlers_line->setObjectName("gscoverage:handlers-line");
    m_handlers_line->setWordWrap(true);
    m_handlers = make_table("gscoverage:handlers", QStringList() << tr("Command") << tr("Name") << tr("Handler") << tr("Where")
                                                                  << tr("State"));
    m_never_line = new QLabel(this);
    m_never_line->setObjectName("gscoverage:never-line");
    m_never_line->setWordWrap(true);
    m_never = make_table("gscoverage:never", QStringList() << tr("Address") << tr("Place") << tr("PC") << tr("Label")
                                                           << tr("Frame") << tr("Kind"));
    QSplitter * split = new QSplitter(Qt::Vertical, this);
    split->setObjectName("gscoverage:split");
    split->setChildrenCollapsible(false);
    split->addWidget(section(split, new QLabel(tr("Code / data per space and page (byte counts; ranges are offsets in the "
                                                  "16 KB page)")), {m_pages}));
    split->addWidget(section(split, m_handlers_line, {m_handlers}));
    split->addWidget(section(split, m_never_line, {m_never}));
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 1);
    m_layout->addWidget(split, 1);
}

void GsCoverageView::control(const char * action)
{
    if (m_card.empty() || AddressRouter::read_only_at(this)) return;
    const Emulator::GsJson a = e->gs_analysis(m_card, action, -1);
    say(a.ok, a.ok ? std::string("analysis ") + action + (a.message.empty() ? std::string() : ": " + a.message) : a.message);
    refresh();
}

void GsCoverageView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    if (!read("coverage", {{"ranges", "16"}})) {
        for (QPushButton * b : {m_on, m_off, m_reset, m_release, m_break_new}) b->hide();
        for (QTableWidget * t : {m_pages, m_handlers, m_never}) t->setRowCount(0);
        m_handlers_line->clear();
        m_never_line->clear();
        return;
    }
    const QJsonObject &r = m_report;
    const bool enabled = r.value("enabled").toBool(), recording = r.value("recording").toBool();
    const bool allocated = r.value("allocated").toBool();
    m_on->setVisible(!enabled);
    m_off->setVisible(enabled);
    m_reset->setVisible(true);
    m_reset->setEnabled(allocated);
    m_release->setVisible(true);
    m_release->setEnabled(allocated);
    m_break_new->setVisible(true);
    QString line = tr("%1 (%2): analysis %3").arg(r.value("card").toString(), r.value("cpu").toString(),
                                                  enabled ? (recording ? tr("on") : tr("on (binds at the card's next run)")) : tr("off"));
    if (num(r, "memory_bytes") > 0) line += kDot + tr("%1 KB").arg(grouped(num(r, "memory_bytes") / 1024));
    if (allocated)
        line += kDot + tr("%1 card resets").arg(num(r, "card_resets")) + kDot + tr("frame %1").arg(num(r, "frame"));
    if (!allocated)
        line += kDot + tr("no data: On marks the card's code and data (the card runs about 2.5 times as slow while on; Release "
                          "frees the memory)");
    m_line->setText(line);

    //Pages: a row per page with a mark, a total row per space first
    const dbg::Colors c = dbg::colors(palette());
    m_pages->setUpdatesEnabled(false);
    m_pages->setRowCount(0);
    for (const QJsonValue &sv : r.value("spaces").toArray()) {
        const QJsonObject s = sv.toObject();
        const QString space = s.value("name").toString();
        auto add = [&](const QString &page, const QJsonObject &n, bool total) {
            const int i = m_pages->rowCount();
            m_pages->insertRow(i);
            m_pages->setItem(i, PColSpace, cell(space));
            m_pages->setItem(i, PColPage, cell(page, true));
            const char * keys[] = {"exec", "code", "read", "write", "dac", "written"};
            for (int k = 0; k < 6; k++)
                m_pages->setItem(i, PColExec + k, cell(n.contains(keys[k]) ? grouped(num(n, keys[k])) : QString(), true));
            const bool cut = n.value("ranges_truncated").toBool();
            m_pages->setItem(i, PColCodeRanges, cell(ranges_text(n.value("code_ranges").toArray(), cut)));
            m_pages->setItem(i, PColDataRanges, cell(ranges_text(n.value("data_ranges").toArray(), cut)));
            m_pages->setItem(i, PColDacRanges, cell(ranges_text(n.value("dac_ranges").toArray(), cut)));
            if (total) {
                QFont bold = m_pages->font();
                bold.setBold(true);
                for (int k = 0; k < PColCount; k++) m_pages->item(i, k)->setFont(bold);
            }
        };
        add(tr("all %1").arg(num(s, "pages")), s.value("totals").toObject(), true);
        for (const QJsonValue &pv : s.value("page_list").toArray()) {
            const QJsonObject p = pv.toObject();
            add(QString::number(num(p, "page")), p, false);
        }
    }
    fit(m_pages);

    //Handlers: the executed ones, then the others dimmed
    const QJsonObject h = r.value("handlers").toObject();
    m_handlers->setUpdatesEnabled(false);
    m_handlers->setRowCount(0);
    if (!allocated) {
        m_handlers_line->setText(tr("Command handlers: no data"));
    } else if (!h.value("available").toBool()) {
        m_handlers_line->setText(tr("Command handlers: %1").arg(h.value("description").toString()));
    } else {
        m_handlers_line->setText(tr("Command handlers executed: %1 of %2").arg(num(h, "executed_count")).arg(num(h, "entries")) +
                                 kDot + tr("profile %1, table %2").arg(h.value("profile").toString(), h.value("table").toString()));
        for (const QJsonValue &v : h.value("executed").toArray()) {
            const QJsonObject x = v.toObject();
            const int i = m_handlers->rowCount();
            m_handlers->insertRow(i);
            m_handlers->setItem(i, HColCommand, cell(hex2((unsigned int)num(x, "command"))));
            m_handlers->setItem(i, HColName, cell(x.value("name").toString()));
            m_handlers->setItem(i, HColHandler, cell(x.value("handler").toString()));
            m_handlers->setItem(i, HColWhere, cell(x.value("space").toString() + " " + QString::number(num(x, "page"))));
            m_handlers->setItem(i, HColState, cell(tr("executed")));
        }
        for (const QJsonValue &v : h.value("not_executed").toArray()) {
            const int i = m_handlers->rowCount();
            m_handlers->insertRow(i);
            m_handlers->setItem(i, HColCommand, cell(hex2((unsigned int)v.toDouble())));
            for (int k : {(int)HColName, (int)HColHandler, (int)HColWhere}) m_handlers->setItem(i, k, cell(QString()));
            m_handlers->setItem(i, HColState, cell(tr("not executed")));
            for (int k = 0; k < HColCount; k++) m_handlers->item(i, k)->setForeground(QBrush(c.dim));
        }
    }
    fit(m_handlers);

    //Reads of RAM never written since the card's reset
    const QJsonObject nw = r.value("never_written").toObject();
    const QJsonArray rows = nw.value("rows").toArray();
    m_never_line->setText(allocated ? tr("Reads of RAM never written since frame %1: %2").arg(num(nw, "since_frame")).arg(grouped(num(nw, "count"))) +
                                          (num(nw, "dropped") > 0 ? kDot + tr("%1 more not kept").arg(grouped(num(nw, "dropped"))) : QString()) +
                                          kDot + tr("the written stamps start at On: reset the card for a clean list")
                                    : tr("Reads of RAM never written: no data"));
    const int shown = std::min<int>((int)rows.size(), 200);
    m_never->setUpdatesEnabled(false);
    m_never->setRowCount(shown);
    for (int i = 0; i < shown; i++) {
        const QJsonObject x = rows.at(i).toObject();
        const unsigned int pc = (unsigned int)num(x, "pc");
        m_never->setItem(i, NColAddress, cell(hex4((unsigned int)num(x, "address"))));
        m_never->setItem(i, NColPlace, cell(tr("ram %1:%2").arg(num(x, "page")).arg(hex4((unsigned int)num(x, "offset")))));
        m_never->setItem(i, NColPc, cell(hex4(pc)));
        m_never->setItem(i, NColLabel, cell(label_at(pc)));
        m_never->setItem(i, NColFrame, cell(QString::number(num(x, "frame")), true));
        m_never->setItem(i, NColKind, cell(x.value("kind").toString()));
    }
    fit(m_never);
}

//------------------------------------------------------------------- Heat map

GsHeatCanvas::GsHeatCanvas(QWidget *parent):
    QWidget(parent)
{
    setObjectName("gsheatmap:canvas");
    setMinimumSize(128, 128);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(QString(" "));
}

QColor GsHeatCanvas::color(const std::string &channel)
{
    if (channel == "fetch") return QColor(79, 195, 247);
    if (channel == "read") return QColor(129, 199, 132);
    if (channel == "write") return QColor(229, 115, 115);
    if (channel == "dac") return QColor(255, 213, 79);
    if (channel == "sd_dma") return QColor(186, 104, 200);
    if (channel == "zx_dma") return QColor(77, 182, 172);
    return QColor(200, 200, 200);
}

void GsHeatCanvas::set(int columns, int64_t cells, int64_t cell_bytes, std::vector<std::vector<uint8_t>> grids,
                       std::vector<std::string> names, int shown)
{
    m_columns = columns;
    m_cells = cells;
    m_cell_bytes = cell_bytes;
    m_grids = std::move(grids);
    m_names = std::move(names);
    if (columns <= 0 || cells <= 0) {
        m_image = QImage();
        update();
        return;
    }
    const int rows = (int)((cells + columns - 1) / columns);
    m_image = QImage(columns, rows, QImage::Format_RGB32);
    m_image.fill(palette().window().color());
    for (int64_t i = 0; i < cells; i++) {
        double r = 0, g = 0, b = 0;
        for (size_t k = 0; k < m_grids.size(); k++) {
            if (shown >= 0 && (int)k != shown) continue;
            if (i >= (int64_t)m_grids[k].size() || m_grids[k][(size_t)i] == 0) continue;
            const double heat = m_grids[k][(size_t)i] / 255.0;
            const QColor c = color(m_names[k]);
            r += c.red() * heat;
            g += c.green() * heat;
            b += c.blue() * heat;
        }
        const int base = 18;
        m_image.setPixel((int)(i % columns), (int)(i / columns),
                         qRgb(std::min(255, base + (int)r), std::min(255, base + (int)g), std::min(255, base + (int)b)));
    }
    update();
}

QSize GsHeatCanvas::sizeHint() const
{
    return QSize(256, 256);
}

QRect GsHeatCanvas::target() const
{
    if (m_image.isNull()) return QRect();
    const double side = std::min((double)width() / m_image.width(), (double)height() / m_image.height());
    const int w = (int)(m_image.width() * side), h = (int)(m_image.height() * side);
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void GsHeatCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (m_image.isNull()) return;
    p.drawImage(target(), m_image);
}

QString GsHeatCanvas::describe(const QPoint &at) const
{
    const QRect t = target();
    if (!t.contains(at) || m_columns <= 0) return QString();
    const int x = (int)((at.x() - t.left()) * (double)m_image.width() / t.width());
    const int y = (int)((at.y() - t.top()) * (double)m_image.height() / t.height());
    const int64_t i = (int64_t)y * m_columns + x;
    if (i < 0 || i >= m_cells) return QString();
    const uint64_t from = (uint64_t)(i * m_cell_bytes), to = from + (uint64_t)m_cell_bytes - 1;
    QString out = tr("RAM %1-%2 (ram %3:%4)").arg(hex6(from), hex6(to)).arg(from / 0x4000).arg(hex4((unsigned int)(from % 0x4000)));
    for (size_t k = 0; k < m_grids.size(); k++)
        if (i < (int64_t)m_grids[k].size() && m_grids[k][(size_t)i] > 0)
            out += "\n" + QString::fromStdString(m_names[k]) + " " + QString::number(m_grids[k][(size_t)i]);
    return out;
}

bool GsHeatCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent * he = static_cast<QHelpEvent *>(event);
        const QString text = describe(he->pos());
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

GsHeatmapView::GsHeatmapView(Emulator * e, QWidget *parent):
    GsReportTab(e, "card:gs-heatmap", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_on = new QPushButton(tr("On"), this);
    m_on->setObjectName("gsheatmap:on");
    m_on->setToolTip(tr("Switch the card analysis on (shared with GS coverage; the card runs about 2.5 times as slow while on)"));
    m_on->hide();
    connect(m_on, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        const Emulator::GsJson a = this->e->gs_analysis(m_card, "on", -1);
        say(a.ok, a.ok ? std::string("analysis on") : a.message);
        refresh();
    });
    row->addWidget(m_on, 0, Qt::AlignTop);
    row->addWidget(new QLabel(tr("Channel"), this), 0, Qt::AlignTop);
    m_channel = new QComboBox(this);
    m_channel->setObjectName("gsheatmap:channel");
    m_channel->addItem(tr("all (added)"), -1);
    connect(m_channel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refresh(); });
    row->addWidget(m_channel, 0, Qt::AlignTop);
    row->addWidget(new QLabel(tr("Decay"), this), 0, Qt::AlignTop);
    m_decay = new QSpinBox(this);
    m_decay->setObjectName("gsheatmap:decay");
    m_decay->setRange(0, 255);
    m_decay->setToolTip(tr("The heat a cell loses per frame (a touch adds 32, up to 255)"));
    connect(m_decay, &QSpinBox::editingFinished, this, [this]() {
        if (m_card.empty() || AddressRouter::read_only_at(this)) return;
        const Emulator::GsJson a = this->e->gs_analysis(m_card, "status", m_decay->value());
        say(a.ok, a.ok ? "decay " + std::to_string(m_decay->value()) + " per frame" : a.message);
        refresh();
    });
    row->addWidget(m_decay, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_layout->addWidget(m_answer);
    m_canvas = new GsHeatCanvas(this);
    m_recent_line = new QLabel(this);
    m_recent_line->setObjectName("gsheatmap:recent-line");
    m_recent_line->setWordWrap(true);
    m_recent = make_table("gsheatmap:recent", QStringList() << tr("Channel") << tr("Start") << tr("End") << tr("Length")
                                                             << tr("Place"));
    QSplitter * split = new QSplitter(Qt::Vertical, this);
    split->setObjectName("gsheatmap:split");
    split->setChildrenCollapsible(false);
    split->addWidget(m_canvas);
    split->addWidget(section(split, m_recent_line, {m_recent}));
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    m_layout->addWidget(split, 1);
}

void GsHeatmapView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    m_hot = 0;
    if (!read("heatmap", {{"cells", "16384"}, {"format", "base64"}, {"recent", "dac"}, {"frames", "50"}})) {
        m_on->hide();
        m_canvas->set(0, 0, 0, {}, {}, -1);
        m_recent->setRowCount(0);
        m_recent_line->clear();
        return;
    }
    const QJsonObject &r = m_report;
    const bool allocated = r.value("allocated").toBool();
    m_on->setVisible(!r.value("enabled").toBool());
    if (!m_decay->hasFocus() && r.contains("decay")) {
        const QSignalBlocker block(m_decay);
        m_decay->setValue((int)num(r, "decay"));
    }
    m_decay->setEnabled(allocated);
    QString line = tr("%1 (%2): analysis %3").arg(r.value("card").toString(), r.value("cpu").toString(),
                                                  r.value("enabled").toBool() ? tr("on") : tr("off"));
    if (!allocated) {
        m_line->setText(line + kDot + tr("no data: On marks the card's memory (shared with GS coverage)"));
        m_canvas->set(0, 0, 0, {}, {}, -1);
        m_recent->setRowCount(0);
        m_recent_line->clear();
        return;
    }
    const int64_t group = std::max<int64_t>(1, num(r, "group", 1));
    const int64_t cell_bytes = num(r, "cell_bytes", 16) * group;
    std::vector<std::vector<uint8_t>> grids;
    std::vector<std::string> names;
    QStringList parts;
    for (const QJsonValue &v : r.value("channels").toArray()) {
        const QJsonObject c = v.toObject();
        const QByteArray data = QByteArray::fromBase64(c.value("data").toString().toLatin1());
        grids.emplace_back(data.begin(), data.end());
        names.push_back(c.value("name").toString().toStdString());
        m_hot += num(c, "hot_cells");
        parts << tr("%1 %2 hot").arg(c.value("name").toString(), grouped(num(c, "hot_cells")));
    }
    //The channel picker filled once with the report's channels
    if (m_channel->count() == 1)
        for (size_t k = 0; k < names.size(); k++) m_channel->addItem(QString::fromStdString(names[k]), (int)k);
    const int shown = m_channel->currentData().toInt();
    m_canvas->set((int)num(r, "columns"), num(r, "data_cells"), cell_bytes, std::move(grids), names, shown);
    m_line->setText(line + kDot + tr("%1 cells of %2 B (%3 columns)").arg(grouped(num(r, "data_cells")), grouped(cell_bytes)).arg(num(r, "columns")) +
                    kDot + tr("decay %1 per frame").arg(num(r, "decay")) + kDot + tr("frame %1").arg(num(r, "frame")) + kDot +
                    parts.join(", "));

    const QJsonObject recent = r.value("recent").toObject();
    m_recent->setUpdatesEnabled(false);
    m_recent->setRowCount(0);
    int count = 0;
    for (const QJsonValue &cv : recent.value("channels").toArray()) {
        const QJsonObject c = cv.toObject();
        for (const QJsonValue &rv : c.value("ranges").toArray()) {
            const QJsonObject x = rv.toObject();
            const int i = m_recent->rowCount();
            m_recent->insertRow(i);
            m_recent->setItem(i, RColChannel, cell(c.value("name").toString()));
            m_recent->setItem(i, RColStart, cell(hex6((uint64_t)num(x, "start"))));
            m_recent->setItem(i, RColEnd, cell(hex6((uint64_t)num(x, "end"))));
            m_recent->setItem(i, RColLength, cell(grouped(num(x, "length")), true));
            m_recent->setItem(i, RColPlace, cell(tr("ram %1:%2").arg(num(x, "page")).arg(hex4((unsigned int)num(x, "offset")))));
            count++;
        }
    }
    fit(m_recent);
    m_recent_line->setText(count > 0 ? tr("The ranges the DAC fetched in the last %1 frames (the samples playing): %2").arg(num(recent, "frames")).arg(count)
                                     : tr("No DAC fetch in the last %1 frames (nothing playing)").arg(num(recent, "frames")));
}
