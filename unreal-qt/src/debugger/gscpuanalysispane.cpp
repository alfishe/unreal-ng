#include "gscpuanalysispane.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QEvent>
#include <QHelpEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <utility>

#include "debugger/cardpanes.h"
#include "debugger/gsanalysispane.h"
#include "debugger/gsmonitorpane.h"
#include "debugger/history/historycontrol.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsanalysisreport.h"
#include "emulator/sound/chips/gs/gsprofilereport.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/state/statenode.h"

namespace
{
const QColor kCurrent(120, 200, 255, 70);
const QColor kNotExecuted(128, 128, 128);
constexpr int64_t kPage = 0x4000;

const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key, int64_t none = 0)
{
    const StateNode* v = Member(node, key);
    if (v && v->kind == StateNode::Kind::Int)
        return v->i;
    if (v && v->kind == StateNode::Kind::Double)
        return static_cast<int64_t>(v->d);
    if (v && v->kind == StateNode::Kind::Bool)
        return v->b ? 1 : 0;
    return none;
}

double Num(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    if (v && v->kind == StateNode::Kind::Double)
        return v->d;
    if (v && v->kind == StateNode::Kind::Int)
        return static_cast<double>(v->i);
    return 0.0;
}

bool Flag(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::Bool && v->b;
}

QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

const std::vector<StateNode>& Items(const StateNode& node, const char* key)
{
    static const std::vector<StateNode> none;
    const StateNode* v = Member(node, key);
    return v && v->isArray() ? v->items : none;
}

const StateNode& Object(const StateNode& node, const char* key)
{
    static const StateNode none;
    const StateNode* v = Member(node, key);
    return v && v->isObject() ? *v : none;
}

QString Hex(int64_t value, int digits)
{
    return QLatin1Char('#') +
           QString::number(static_cast<qulonglong>(value), 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}

QString Pct(double value)
{
    return QString::number(value, 'f', 1);
}

/// A table cell that sorts by its key (Qt::UserRole) when it has one, else by its text
class SortItem : public QTableWidgetItem
{
public:
    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant a = data(Qt::UserRole);
        const QVariant b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid())
            return a.toDouble() < b.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};

void Put(QTableWidget* table, int row, int column, const QString& text, const QString& tooltip = QString())
{
    QTableWidgetItem* item = table->item(row, column);
    if (!item)
    {
        item = new SortItem();
        table->setItem(row, column, item);
    }
    item->setText(text);
    item->setToolTip(tooltip);
    item->setData(Qt::UserRole, QVariant());
    item->setData(Qt::BackgroundRole, QVariant());
    item->setData(Qt::ForegroundRole, QVariant());
}

void PutNumber(QTableWidget* table, int row, int column, double value, const QString& text)
{
    Put(table, row, column, text);
    table->item(row, column)->setData(Qt::UserRole, value);
}

void PutInt(QTableWidget* table, int row, int column, int64_t value)
{
    PutNumber(table, row, column, static_cast<double>(value), QString::number(value));
}

void Tint(QTableWidget* table, int row, const QColor& background, const QColor& foreground = QColor())
{
    for (int column = 0; column < table->columnCount(); column++)
        if (QTableWidgetItem* item = table->item(row, column))
        {
            if (background.isValid())
                item->setBackground(background);
            if (foreground.isValid())
                item->setForeground(foreground);
        }
}

QPixmap Swatch(const QColor& colour)
{
    QPixmap pixmap(10, 10);
    pixmap.fill(colour);
    return pixmap;
}

/// "#0000-#01FF #0300" of `ranges` ([start, end] each, offsets in the page)
QString RangesText(const std::vector<StateNode>& ranges, bool truncated)
{
    QStringList parts;
    for (const StateNode& r : ranges)
    {
        if (!r.isArray() || r.items.size() < 2)
            continue;
        const int64_t start = r.items[0].i;
        const int64_t end = r.items[1].i;
        parts << (end > start ? Hex(start, 4) + QLatin1Char('-') + Hex(end, 4) : Hex(start, 4));
    }
    QString text = parts.join(QLatin1Char(' '));
    if (truncated)
        text += QStringLiteral(" …");
    return text;
}

QString IsrLine(const QString& when, const StateNode& s, int64_t period)
{
    if (Int(s, "count") == 0)
        return QObject::tr("%1: no interrupt taken · %2 request(s) lost").arg(when).arg(Int(s, "lost"));
    return QObject::tr("%1: %2 ISR(s) · avg %3 / %4 · min %5 · max %6 · headroom %7% · overruns %8 · latency avg %9 "
                       "max %10 · lost %11")
        .arg(when)
        .arg(Int(s, "count"))
        .arg(QString::number(Num(s, "avg"), 'f', 1))
        .arg(period)
        .arg(Int(s, "min"))
        .arg(Int(s, "max"))
        .arg(Pct(Num(s, "headroom_pct")))
        .arg(Int(s, "overruns"))
        .arg(QString::number(Num(s, "latency_avg"), 'f', 1))
        .arg(Int(s, "latency_max"))
        .arg(Int(s, "lost"));
}
}  // namespace

// region <GsCpuAnalysisPane>

GsCpuAnalysisPane::GsCpuAnalysisPane(View view, std::shared_ptr<DebugCpuId> follow, QWidget* parent)
    : QWidget(parent), _view(view), _follow(std::move(follow))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setVisible(false);
    layout->addWidget(_status);

    _body = new QWidget(this);
    auto* body = new QVBoxLayout(_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    layout->addWidget(_body, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    switch (view)
    {
        case View::Profiler:
            addButton(buttons, QStringLiteral("profile_start"), tr("Start"),
                      tr("A new profiling session (cleared, then on): the ISR budget, the commands' cost, the routines, "
                         "the activity per frame. While it records the card runs its slower record loop (about x2)"));
            addButton(buttons, QStringLiteral("profile_stop"), tr("Stop"), tr("Stop recording; the data is kept"));
            addButton(buttons, QStringLiteral("profile_clear"), tr("Clear"), tr("Drop the data recorded so far"));
            break;
        case View::Events:
            addButton(buttons, QStringLiteral("profile_start"), tr("Start profiler"),
                      tr("The frame map and its ISR bars come from the card CPU's profiler (the card then runs its "
                         "slower record loop, about x2)"));
            addButton(buttons, QStringLiteral("trace_start"), tr("Start port trace"),
                      tr("The host / DAC / page / volume / ZX-DMA dots come from the card's port trace: its history to "
                         "ring (costs the card a little on each access)"));
            break;
        case View::Coverage:
        case View::HeatMap:
            addButton(buttons, QStringLiteral("analysis_on"), tr("On"),
                      tr("Mark every byte the card CPU executes, reads and writes, and keep the RAM heat map: the memory "
                         "is allocated now (about 7 MB on a 4 MB NeoGS), the card runs its slower analysis bus (about "
                         "x2.5)"));
            addButton(buttons, QStringLiteral("analysis_off"), tr("Off"), tr("Stop marking; the data is kept"));
            addButton(buttons, QStringLiteral("analysis_reset"), tr("Reset"), tr("Clear the marks and the heat"));
            addButton(buttons, QStringLiteral("analysis_release"), tr("Release"), tr("Drop the data and free its memory"));
            if (view == View::Coverage)
            {
                QPushButton* breakNew = addButton(buttons, QStringLiteral("break_new_code"), tr("Break on new code"),
                                                  tr("Not available yet: the core has no \"first new code\" breakpoint "
                                                     "(X8). Reset the marks and watch the code ranges grow instead"));
                breakNew->setEnabled(false);
            }
            else
            {
                _channel = new QComboBox(_body);
                _channel->addItem(tr("All channels"), -1);
                for (int c = 0; c < GsHeatmapCanvas::kChannels; c++)
                {
                    _channel->addItem(GsHeatmapCanvas::ChannelName(c), c);
                    _channel->setItemData(c + 1, Swatch(GsHeatmapCanvas::ChannelColour(c)), Qt::DecorationRole);
                }
                _channel->setToolTip(tr("The channel the grid shows: the card CPU's fetches, reads, writes, the DAC "
                                        "window's fetches, the NeoGS SD / MP3 DMA, the host's ZX-DMA; or all, blended"));
                buttons->addWidget(_channel);
                buttons->addWidget(new QLabel(tr("Decay"), _body));
                _decay = new QSpinBox(_body);
                _decay->setRange(0, 255);
                _decay->setValue(8);
                _decay->setToolTip(tr("The heat a cell loses per host frame (a touch adds 32; 8: a saturated cell goes "
                                      "dark in 32 frames; 0: never)"));
                connect(_decay, &QSpinBox::editingFinished, this,
                        [this]() { emit actionRequested(QStringLiteral("analysis_decay")); });
                buttons->addWidget(_decay);
                buttons->addWidget(new QLabel(tr("Recent"), _body));
                _frames = new QSpinBox(_body);
                _frames->setRange(1, 3000);
                _frames->setValue(50);
                _frames->setSuffix(tr(" frames"));
                _frames->setToolTip(tr("The DAC ranges listed: the RAM the DAC window fetched in the last N frames"));
                connect(_frames, &QSpinBox::editingFinished, this, [this]() { emit actionRequested(QStringLiteral("refresh")); });
                buttons->addWidget(_frames);
            }
            break;
    }
    buttons->addStretch(1);
    body->addLayout(buttons);

    _headline = new QLabel(_body);
    _headline->setWordWrap(true);
    _headline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(_headline);

    _hintBar = new QWidget(_body);
    auto* hint = new QHBoxLayout(_hintBar);
    hint->setContentsMargins(4, 2, 4, 2);
    _hint = new QLabel(_hintBar);
    _hint->setWordWrap(true);
    hint->addWidget(_hint, 1);
    _hintBar->setStyleSheet(QStringLiteral("background: rgba(255, 200, 60, 40); border-radius: 3px;"));
    _hintBar->setVisible(false);
    body->addWidget(_hintBar);

    switch (view)
    {
        case View::Profiler:
        {
            _gauge = new QProgressBar(_body);
            _gauge->setRange(0, 100);
            _gauge->setTextVisible(true);
            _gauge->setToolTip(tr("The ISR budget: the ISR's average share of an interrupt period (the last frame's, "
                                  "else since start)"));
            body->addWidget(_gauge);
            auto* split = new QSplitter(Qt::Vertical, _body);
            _commands = addTable(split, {tr("Cmd"), tr("Name"), tr("Count"), tr("Avg"), tr("Min"), tr("Max"), tr("Total")},
                                 tr("Commands: the cycles from the dispatch to the next poll"));
            _routines = addTable(split,
                                 {tr("Address"), tr("Page"), tr("Label"), tr("Calls"), tr("Inclusive"), tr("Exclusive"),
                                  tr("Avg"), tr("Max"), tr("%")},
                                 tr("Routines (CALL / RST / interrupt entries; click a header to sort)"));
            _routines->setSortingEnabled(true);
            _routines->sortByColumn(4, Qt::DescendingOrder);
            _activity = addTable(split,
                                 {tr("Frame"), tr("ISR %"), tr("Idle %"), tr("Halt %"), tr("Busy %"), tr("Cmds"), tr("INTs"),
                                  tr("Overruns"), tr("Lost"), tr("ISR max")},
                                 tr("Activity per frame (newest first)"));
            body->addWidget(split, 1);
            break;
        }
        case View::Events:
        {
            auto* legend = new QHBoxLayout();
            legend->setContentsMargins(0, 0, 0, 0);
            _events = new GsEventsCanvas();
            for (int c = 0; c < GsEventsCanvas::CategoryCount; c++)
            {
                const auto category = static_cast<GsEventsCanvas::Category>(c);
                auto* box = new QCheckBox(GsEventsCanvas::CategoryName(category), _body);
                box->setIcon(QIcon(Swatch(GsEventsCanvas::CategoryColour(category))));
                box->setChecked(true);
                box->setToolTip(tr("Show or hide this category on the canvas"));
                connect(box, &QCheckBox::toggled, this, [this, category](bool on) { _events->setShown(category, on); });
                legend->addWidget(box);
                _categories.push_back(box);
            }
            legend->addStretch(1);
            body->addLayout(legend);
            _eventsScroll = new QScrollArea(_body);
            _eventsScroll->setWidgetResizable(true);
            _eventsScroll->setWidget(_events);
            body->addWidget(_eventsScroll, 1);
            break;
        }
        case View::Coverage:
        {
            auto* split = new QSplitter(Qt::Vertical, _body);
            _pages = addTable(split,
                              {tr("Space"), tr("Page"), tr("Exec"), tr("Code"), tr("Read"), tr("Write"), tr("Jump"),
                               tr("Sub"), tr("DAC"), tr("Written"), tr("Code ranges"), tr("Data ranges"), tr("DAC ranges")},
                              tr("Code / data per space and page (bytes; ranges are offsets in the page)"));
            _handlers = addTable(split, {tr("Cmd"), tr("Name"), tr("Handler"), tr("Label"), tr("Executed"), tr("Where")},
                                 tr("The dispatcher's command handlers"));
            _neverWritten = addTable(split, {tr("Address"), tr("RAM"), tr("PC"), tr("Label"), tr("Frame"), tr("Kind")},
                                     tr("Reads of RAM never written"));
            body->addWidget(split, 1);
            break;
        }
        case View::HeatMap:
        {
            auto* split = new QSplitter(Qt::Vertical, _body);
            _heatmap = new GsHeatmapCanvas(split);
            split->addWidget(_heatmap);
            connect(_channel, &QComboBox::activated, this,
                    [this](int index) { _heatmap->setChannel(_channel->itemData(index).toInt()); });
            _recent = addTable(split, {tr("Start"), tr("End"), tr("Bytes"), tr("Page"), tr("Offset")},
                               tr("What the DAC played"));
            split->setStretchFactor(0, 3);
            split->setStretchFactor(1, 1);
            body->addWidget(split, 1);
            break;
        }
    }
}

QPushButton* GsCpuAnalysisPane::addButton(QBoxLayout* row, const QString& action, const QString& text, const QString& tooltip)
{
    auto* button = new QPushButton(text, _body);
    button->setToolTip(tooltip);
    connect(button, &QPushButton::clicked, this, [this, action]() { emit actionRequested(action); });
    row->addWidget(button);
    _buttons.emplace_back(action, button);
    return button;
}

QTableWidget* GsCpuAnalysisPane::addTable(QSplitter* split, const QStringList& columns, const QString& caption)
{
    auto* holder = new QWidget(split);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    auto* label = new QLabel(caption, holder);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* table = new QTableWidget(0, static_cast<int>(columns.size()), holder);
    table->setHorizontalHeaderLabels(columns);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 4);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(false);
    for (int i = 0; i + 1 < static_cast<int>(columns.size()); i++)
        table->setColumnWidth(i, i == 0 ? 64 : 72);
    layout->addWidget(table, 1);
    split->addWidget(holder);
    _captions[table] = label;
    return table;
}

void GsCpuAnalysisPane::setCaption(QTableWidget* table, const QString& text)
{
    if (const auto it = _captions.find(table); it != _captions.end())
        it->second->setText(text);
}

QString GsCpuAnalysisPane::caption(const QTableWidget* table) const
{
    const auto it = _captions.find(table);
    return it != _captions.end() ? it->second->text() : QString();
}

void GsCpuAnalysisPane::reset()
{
    _lastFrame = ~0ull;
}

void GsCpuAnalysisPane::refresh(EmulatorContext* context, bool live)
{
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const CardPanes::Target target = CardPanes::Front(context, front);
    if (target == CardPanes::Target::None)
    {
        showUnavailable(tr("No General Sound card fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs | multisound)"));
        return;
    }
    const bool ms = target == CardPanes::Target::MultiSound;
    const std::string card = ms ? "ms" : "gs";
    if (card != _card)
    {
        _card = card;
        reset();
    }
    // A live refresh within the frame already read shows the same data (the reports change at frame ends): nothing
    const uint64_t frame = context->emulatorState.frame_counter;
    if (live && frame == _lastFrame)
        return;
    _lastFrame = frame;
    const std::string cpu = ms ? "ms" : "";
    const DebugCpuId cardCpu = ms ? DebugCpuId::MultiSoundGS : DebugCpuId::GS;
    switch (_view)
    {
        case View::Profiler: refreshProfiler(context, cpu, cardCpu); break;
        case View::Events: refreshEvents(context, cpu); break;
        case View::Coverage: refreshCoverage(context, cpu, cardCpu); break;
        case View::HeatMap: refreshHeatmap(context, cpu); break;
    }
}

void GsCpuAnalysisPane::refreshProfiler(EmulatorContext* context, const std::string& cpu, DebugCpuId cardCpu)
{
    (void)cardCpu;  // the report carries the routines' labels
    GSProfileQuery query;
    query.cpu = cpu;
    query.routines = kRoutines;
    query.activity = kActivityFrames;
    const StateNode n = DeviceState::GsProfile(context, query);
    if (!Flag(n, "available"))
    {
        showUnavailable(Str(n, "description"));
        return;
    }
    const bool on = Flag(n, "on");
    const int64_t frames = Int(n, "frames");
    const int64_t period = Int(n, "period_cycles");
    button(QStringLiteral("profile_start"))->setEnabled(!on);
    button(QStringLiteral("profile_stop"))->setEnabled(on);
    button(QStringLiteral("profile_clear"))->setEnabled(frames > 0);

    const StateNode& isr = Object(n, "isr");
    const StateNode& last = Object(isr, "last_frame");
    const StateNode& since = Object(isr, "since_start");
    const StateNode& gauge = Int(last, "count") > 0 ? last : since;
    if (Int(gauge, "count") > 0)
    {
        _gauge->setValue(static_cast<int>(std::lround(std::clamp(Num(gauge, "used_pct"), 0.0, 100.0))));
        _gauge->setFormat(tr("ISR %1 / %2 avg · %3 max · %4% headroom")
                              .arg(QString::number(Num(gauge, "avg"), 'f', 0))
                              .arg(period)
                              .arg(Int(gauge, "max"))
                              .arg(QString::number(Num(gauge, "headroom_pct"), 'f', 0)));
    }
    else
    {
        _gauge->setValue(0);
        _gauge->setFormat(tr("ISR budget: no interrupt taken"));
    }

    QStringList head;
    head << (on ? tr("Profiler on") : tr("Profiler off")) +
                tr(" · %1 frame(s) · period %2 cycles · card clock %3 MHz")
                    .arg(frames)
                    .arg(period)
                    .arg(QString::number(Num(n, "clock_hz") / 1e6, 'f', 2));
    head << IsrLine(tr("Last frame"), last, period);
    head << IsrLine(tr("Since start"), since, period);
    const StateNode* current = Member(n, "current_command");
    int currentCode = -1;
    if (current && current->isObject())
    {
        currentCode = static_cast<int>(Int(*current, "code"));
        head << tr("Current command %1 %2: %3 cycles so far")
                    .arg(Hex(currentCode, 2), Str(*current, "name"))
                    .arg(Int(*current, "cycles"));
    }
    QStringList notes;
    for (const char* key : {"stack_overflows", "routines_dropped", "resyncs"})
        if (const int64_t count = Int(n, key); count > 0)
            notes << QStringLiteral("%1 %2").arg(QString::fromLatin1(key)).arg(count);
    if (!notes.isEmpty())
        head << tr("Profiler notes: %1").arg(notes.join(QStringLiteral(", ")));
    showBody(head.join(QLatin1Char('\n')));

    const std::vector<StateNode>& routines = Items(n, "routines");
    QStringList hints;
    if (!on && frames == 0)
        hints << tr("The profiler is off. Start records the card's ISR budget, the commands' cost, the routines and the "
                    "activity per frame; while it records the card runs its slower record loop (about x2 the card's "
                    "CPU time).");
    else if (!on)
        hints << tr("The profiler is stopped: the data of its last session is shown (Start begins a new one).");
    if (frames > 0 && Int(since, "count") == 0)
        hints << tr("Interrupts off: no interrupt taken in %1 frame(s), %2 period request(s) lost. The stock firmware "
                    "idles with interrupts off and enables them to play a module: the ISR budget fills then.")
                     .arg(frames)
                     .arg(Int(since, "lost"));
    if (on && routines.empty())
        hints << tr("No routine called yet: the firmware's idle loop calls nothing. Routines appear on a card reset "
                    "(its POST calls them) or while the card plays.");
    showHint(hints.join(QLatin1Char('\n')));

    // Commands
    const std::vector<StateNode>& commands = Items(n, "commands");
    _commands->setRowCount(static_cast<int>(commands.size()));
    for (size_t i = 0; i < commands.size(); i++)
    {
        const StateNode& c = commands[i];
        const int row = static_cast<int>(i);
        const int code = static_cast<int>(Int(c, "code"));
        Put(_commands, row, 0, Hex(code, 2), Str(c, "handler"));
        Put(_commands, row, 1, Str(c, "name"));
        PutInt(_commands, row, 2, Int(c, "count"));
        PutNumber(_commands, row, 3, Num(c, "avg"), QString::number(Num(c, "avg"), 'f', 1));
        PutInt(_commands, row, 4, Int(c, "min"));
        PutInt(_commands, row, 5, Int(c, "max"));
        PutInt(_commands, row, 6, Int(c, "total"));
        if (code == currentCode)
            Tint(_commands, row, kCurrent);
    }
    setCaption(_commands, commands.empty() ? tr("Commands: none dispatched since Start")
                                           : tr("Commands: the cycles from the dispatch to the next poll (current tinted)"));

    // Routines: sorting held while the rows are filled, then by the header the user picked
    _routines->setSortingEnabled(false);
    _routines->setRowCount(static_cast<int>(routines.size()));
    for (size_t i = 0; i < routines.size(); i++)
    {
        const StateNode& r = routines[i];
        const int row = static_cast<int>(i);
        const int64_t address = Int(r, "address");
        Put(_routines, row, 0, Hex(address, 4));
        _routines->item(row, 0)->setData(Qt::UserRole, static_cast<double>(address));
        const QString kind = Str(r, "page_kind");
        const QString page = kind == QLatin1String("rom")   ? tr("ROM %1").arg(Int(r, "page"))
                             : kind == QLatin1String("ram") ? tr("RAM %1").arg(Int(r, "page"))
                                                            : QString();
        Put(_routines, row, 1, page);
        Put(_routines, row, 2, Str(r, "label"));
        PutInt(_routines, row, 3, Int(r, "calls"));
        _routines->item(row, 3)->setToolTip(tr("%1 of them interrupts").arg(Int(r, "interrupts")));
        PutInt(_routines, row, 4, Int(r, "inclusive"));
        PutInt(_routines, row, 5, Int(r, "exclusive"));
        PutNumber(_routines, row, 6, Num(r, "avg"), QString::number(Num(r, "avg"), 'f', 1));
        _routines->item(row, 6)->setToolTip(tr("min %1").arg(Int(r, "min")));
        PutInt(_routines, row, 7, Int(r, "max"));
        PutNumber(_routines, row, 8, Num(r, "inclusive_pct"), Pct(Num(r, "inclusive_pct")));
    }
    _routines->setSortingEnabled(true);
    const int64_t total = Int(n, "routines_total");
    setCaption(_routines, routines.empty()
                              ? tr("Routines: none called yet")
                              : tr("Routines: %1 of %2 by inclusive cycles (CALL / RST / interrupt entries; interrupts "
                                   "never counted in the caller; click a header to sort)")
                                    .arg(static_cast<int>(routines.size()))
                                    .arg(total));

    // Activity, newest first
    const std::vector<StateNode>& activity = Items(n, "activity");
    const int rows = std::min(kActivityRows, static_cast<int>(activity.size()));
    _activity->setRowCount(rows);
    for (int row = 0; row < rows; row++)
    {
        const StateNode& a = activity[activity.size() - 1 - static_cast<size_t>(row)];
        PutInt(_activity, row, 0, Int(a, "frame"));
        PutNumber(_activity, row, 1, Num(a, "isr_pct"), Pct(Num(a, "isr_pct")));
        PutNumber(_activity, row, 2, Num(a, "idle_pct"), Pct(Num(a, "idle_pct")));
        PutNumber(_activity, row, 3, Num(a, "halt_pct"), Pct(Num(a, "halt_pct")));
        PutNumber(_activity, row, 4, Num(a, "busy_pct"), Pct(Num(a, "busy_pct")));
        PutInt(_activity, row, 5, Int(a, "commands"));
        PutInt(_activity, row, 6, Int(a, "interrupts"));
        PutInt(_activity, row, 7, Int(a, "overruns"));
        PutInt(_activity, row, 8, Int(a, "lost"));
        PutInt(_activity, row, 9, Int(a, "isr_max"));
    }
    setCaption(_activity, tr("Activity per frame (newest first; %1 frame(s) kept)").arg(static_cast<int>(activity.size())));
}

void GsCpuAnalysisPane::refreshEvents(EmulatorContext* context, const std::string& cpu)
{
    GSEventsQuery query;
    query.cpu = cpu;
    query.rows = 0;  // no text: the canvas draws the rows
    const StateNode n = DeviceState::GsEvents(context, query);
    if (!Flag(n, "available"))
    {
        showUnavailable(Str(n, "description"));
        return;
    }
    const bool on = Flag(n, "on");
    const bool capturing = Flag(n, "capturing");
    button(QStringLiteral("profile_start"))->setEnabled(!on);
    button(QStringLiteral("trace_start"))->setEnabled(!capturing);
    const StateNode* frame = Member(n, "frame");
    const bool mapped = frame && frame->kind == StateNode::Kind::Int;

    std::vector<GsEventsCanvas::Row> rows;
    int64_t isrs = 0, events = 0;
    for (const StateNode& r : Items(n, "rows"))
    {
        GsEventsCanvas::Row row;
        row.start = Int(r, "start");
        for (const StateNode& b : Items(r, "isr"))
        {
            GsEventsCanvas::Bar bar;
            bar.accept = Int(b, "accept");
            bar.end = Int(b, "end", -1);
            bar.entry = static_cast<int>(Int(b, "entry"));
            bar.nmi = Flag(b, "nmi");
            row.isrs.push_back(bar);
        }
        for (const StateNode& e : Items(r, "events"))
        {
            if (!e.isArray() || e.items.size() < 4)
                continue;
            GsEventsCanvas::Dot dot;
            dot.cycle = e.items[0].i;
            dot.kind = GsEventsCanvas::KindOf(e.items[1].s);
            dot.value = static_cast<int>(e.items[2].i);
            dot.detail = e.items[3].i;
            row.events.push_back(dot);
        }
        isrs += static_cast<int64_t>(row.isrs.size());
        events += static_cast<int64_t>(row.events.size());
        rows.push_back(std::move(row));
    }
    _events->setFrame(std::move(rows), mapped ? Int(n, "period_cycles") : 0);

    QStringList hints;
    if (!mapped)
        hints << (on ? tr("The profiler is on: the first profiled frame shows at its end.")
                     : tr("No profiled frame: the frame map needs the profiler (Start profiler; the card then runs its "
                          "slower record loop, about x2)."));
    else if (!on)
        hints << tr("The profiler is stopped: the last profiled frame is shown.");
    if (!capturing)
        hints << tr("The port trace is off: no host #B3 / #BB / #33, DAC, page, volume or ZX-DMA dots (Start port trace).");
    if (mapped && isrs == 0)
        hints << tr("No ISR bars: the firmware keeps interrupts off (the stock firmware idles with interrupts off until "
                    "it plays a module).");
    showHint(hints.join(QLatin1Char('\n')));

    if (!mapped)
    {
        showBody(tr("No profiled frame yet"));
        return;
    }
    QString head = tr("Host frame %1 · %2 periods of %3 cycles · %4 ISR(s) · %5 event(s)")
                       .arg(frame->i)
                       .arg(Int(n, "periods"))
                       .arg(Int(n, "period_cycles"))
                       .arg(isrs)
                       .arg(events);
    if (Flag(n, "truncated") || Flag(n, "events_truncated"))
        head += tr(" · truncated (the frame map or the events reached their cap)");
    showBody(head);
}

bool GsCpuAnalysisPane::showAnalysisState(const StateNode& report)
{
    const bool enabled = Flag(report, "enabled");
    const bool allocated = Flag(report, "allocated");
    const bool recording = Flag(report, "recording");
    button(QStringLiteral("analysis_on"))->setEnabled(!enabled);
    button(QStringLiteral("analysis_off"))->setEnabled(enabled);
    button(QStringLiteral("analysis_reset"))->setEnabled(allocated);
    button(QStringLiteral("analysis_release"))->setEnabled(allocated);
    if (!enabled && !allocated)
        showHint(tr("The analysis is off. On marks every byte the card CPU executes, reads and writes and keeps the RAM "
                    "heat map (its memory is allocated then; the card runs its slower analysis bus, about x2.5)."));
    else if (!enabled)
        showHint(tr("The analysis is off: the data so far is kept (On goes on marking, Reset clears, Release frees the "
                    "memory)."));
    else if (!recording)
        showHint(tr("On: the card starts marking at its next run (the machine is paused)."));
    else
        showHint(QString());
    if (_decay && allocated && !_decay->hasFocus())
    {
        const QSignalBlocker block(_decay);
        _decay->setValue(static_cast<int>(Int(report, "decay", 8)));
    }
    return allocated;
}

void GsCpuAnalysisPane::refreshCoverage(EmulatorContext* context, const std::string& cpu, DebugCpuId cardCpu)
{
    GSAnalysisQuery query;
    query.cpu = cpu;
    query.ranges = 16;
    const StateNode n = DeviceState::GsCoverage(context, query);
    if (!Flag(n, "available"))
    {
        showUnavailable(Str(n, "description"));
        return;
    }
    if (!showAnalysisState(n) || !Member(n, "spaces"))
    {
        showBody(tr("No coverage data (%1)").arg(Str(n, "card")));
        _pages->setRowCount(0);
        _handlers->setRowCount(0);
        _neverWritten->setRowCount(0);
        return;
    }

    QStringList head;
    head << tr("Analysis %1 · %2 KB · frame %3 · %4 card reset(s) since created at frame %5")
                .arg(Flag(n, "enabled") ? tr("on") : tr("off"))
                .arg(Int(n, "memory_bytes") / 1024)
                .arg(Int(n, "frame"))
                .arg(Int(n, "card_resets"))
                .arg(Int(n, "created_frame"));

    // Pages: a totals row per space, then each page with a mark
    const std::vector<StateNode>& spaces = Items(n, "spaces");
    int count = 0;
    for (const StateNode& s : spaces)
        count += 1 + static_cast<int>(Items(s, "page_list").size());
    _pages->setRowCount(count);
    int row = 0;
    static const char* const kCounts[] = {"exec", "code", "read", "write", "jump", "sub", "dac", "written"};
    for (const StateNode& s : spaces)
    {
        const QString name = Str(s, "name");
        const StateNode& totals = Object(s, "totals");
        QStringList parts;
        for (const char* key : kCounts)
            if (Member(totals, key))
                parts << QStringLiteral("%1 %2").arg(QString::fromLatin1(key)).arg(Int(totals, key));
        head << tr("%1 (%2 pages of 16 KB): %3").arg(name).arg(Int(s, "pages")).arg(parts.join(QStringLiteral(" · ")));
        Put(_pages, row, 0, name);
        Put(_pages, row, 1, tr("all"));
        for (int c = 0; c < 8; c++)
            Member(totals, kCounts[c]) ? PutInt(_pages, row, 2 + c, Int(totals, kCounts[c])) : Put(_pages, row, 2 + c, QString());
        for (int c = 10; c < 13; c++)
            Put(_pages, row, c, QString());
        QFont bold = _pages->item(row, 0)->font();
        bold.setBold(true);
        for (int c = 0; c < 13; c++)
            _pages->item(row, c)->setFont(bold);
        row++;
        for (const StateNode& p : Items(s, "page_list"))
        {
            Put(_pages, row, 0, name);
            Put(_pages, row, 1, QString::number(Int(p, "page")));
            for (int c = 0; c < 8; c++)
                Member(p, kCounts[c]) ? PutInt(_pages, row, 2 + c, Int(p, kCounts[c])) : Put(_pages, row, 2 + c, QString());
            const bool truncated = Flag(p, "ranges_truncated");
            const QString code = RangesText(Items(p, "code_ranges"), truncated);
            const QString data = RangesText(Items(p, "data_ranges"), truncated);
            const QString dac = RangesText(Items(p, "dac_ranges"), truncated);
            Put(_pages, row, 10, code, code);
            Put(_pages, row, 11, data, data);
            Put(_pages, row, 12, dac, dac);
            for (int c = 0; c < 13; c++)
            {
                QFont plain = _pages->item(row, c)->font();
                plain.setBold(false);
                _pages->item(row, c)->setFont(plain);
            }
            row++;
        }
    }

    // Handlers: executed and not, by command
    const StateNode& handlers = Object(n, "handlers");
    GeneralSoundCard* card = GSCardForCpu(context, cpu);
    const GSFirmwareProfile* profile = card ? GSFirmwareProfileOf(*card) : nullptr;
    GsCardLabels labels(context, cardCpu);
    if (!Flag(handlers, "available"))
    {
        _handlers->setRowCount(0);
        setCaption(_handlers, tr("The dispatcher's handlers: %1").arg(Str(handlers, "description")));
    }
    else
    {
        struct Handler
        {
            QString name, handler, where;
            bool executed = false;
        };
        std::map<int, Handler> byCode;
        for (const StateNode& h : Items(handlers, "executed"))
        {
            Handler& entry = byCode[static_cast<int>(Int(h, "command"))];
            entry.name = Str(h, "name");
            entry.handler = Str(h, "handler");
            entry.where = QStringLiteral("%1 %2").arg(Str(h, "space").toUpper()).arg(Int(h, "page"));
            entry.executed = true;
        }
        bool needDispatch = false;
        for (const StateNode& code : Items(handlers, "not_executed"))
            if (code.kind == StateNode::Kind::Int)
            {
                Handler& entry = byCode[static_cast<int>(code.i)];
                if (profile && profile->Command(static_cast<uint8_t>(code.i)).name)
                    entry.name = QString::fromLatin1(profile->Command(static_cast<uint8_t>(code.i)).name);
                needDispatch = true;
            }
        if (needDispatch)
        {
            // The handler each command not executed would take: the dispatch read the Dispatch tab shows
            GSFirmwareObjectsQuery dispatchQuery;
            dispatchQuery.object = "dispatch";
            dispatchQuery.cpu = cpu;
            const GSFirmwareObjectsReply dispatch = GSFirmwareObjectsRead(context, dispatchQuery);
            if (const StateNode* d = Member(dispatch.body, "dispatch"))
                for (const StateNode& e : Items(*d, "entries"))
                    if (const auto it = byCode.find(static_cast<int>(Int(e, "command"))); it != byCode.end() && !it->second.executed)
                        it->second.handler = Str(e, "handler");
        }
        _handlers->setRowCount(static_cast<int>(byCode.size()));
        int r = 0;
        for (const auto& [code, h] : byCode)
        {
            Put(_handlers, r, 0, Hex(code, 2));
            Put(_handlers, r, 1, h.name);
            Put(_handlers, r, 2, h.handler);
            const bool hex = h.handler.startsWith(QLatin1Char('#'));
            Put(_handlers, r, 3, hex ? labels.at(static_cast<uint16_t>(h.handler.mid(1).toUInt(nullptr, 16))) : QString());
            Put(_handlers, r, 4, h.executed ? tr("yes") : tr("no"));
            Put(_handlers, r, 5, h.where);
            if (!h.executed)
                Tint(_handlers, r, QColor(), kNotExecuted);
            r++;
        }
        setCaption(_handlers, tr("The dispatcher's handlers (%1, table %2): %3 of %4 executed")
                                  .arg(Str(handlers, "profile"), Str(handlers, "table"))
                                  .arg(Int(handlers, "executed_count"))
                                  .arg(Int(handlers, "entries")));
        head << tr("Handlers: %1 of %2 executed").arg(Int(handlers, "executed_count")).arg(Int(handlers, "entries"));
    }

    // Reads of RAM never written
    const StateNode& never = Object(n, "never_written");
    const std::vector<StateNode>& reads = Items(never, "rows");
    _neverWritten->setRowCount(static_cast<int>(reads.size()));
    for (size_t i = 0; i < reads.size(); i++)
    {
        const StateNode& r = reads[i];
        const int at = static_cast<int>(i);
        Put(_neverWritten, at, 0, Hex(Int(r, "address"), 4));
        Put(_neverWritten, at, 1, QStringLiteral("%1:%2").arg(Hex(Int(r, "page"), 2), Hex(Int(r, "offset"), 4)));
        const int64_t pc = Int(r, "pc");
        Put(_neverWritten, at, 2, Hex(pc, 4));
        Put(_neverWritten, at, 3, labels.at(static_cast<uint16_t>(pc)));
        PutInt(_neverWritten, at, 4, Int(r, "frame"));
        Put(_neverWritten, at, 5, Str(r, "kind"));
    }
    setCaption(_neverWritten, tr("Reads of RAM never written since frame %1: %2%3 (the stamps start clear at On and at a "
                                 "card reset)")
                                  .arg(Int(never, "since_frame"))
                                  .arg(Int(never, "count"))
                                  .arg(Int(never, "dropped") > 0 ? tr(", %1 more dropped").arg(Int(never, "dropped"))
                                                                 : QString()));
    showBody(head.join(QLatin1Char('\n')));
}

void GsCpuAnalysisPane::refreshHeatmap(EmulatorContext* context, const std::string& cpu)
{
    GSAnalysisQuery query;
    query.cpu = cpu;
    query.cells = kHeatCells;
    query.format = "base64";
    query.recent = {GSCardAnalysis::DacFetch};
    query.frames = static_cast<uint32_t>(_frames->value());
    const StateNode n = DeviceState::GsHeatmap(context, query);
    if (!Flag(n, "available"))
    {
        showUnavailable(Str(n, "description"));
        return;
    }
    if (!showAnalysisState(n) || !Member(n, "channels"))
    {
        showBody(tr("No heat map (%1)").arg(Str(n, "card")));
        _heatmap->setGrid({}, _channel->currentData().toInt(), 0, 0, 1);
        _recent->setRowCount(0);
        return;
    }
    std::array<std::vector<uint8_t>, GsHeatmapCanvas::kChannels> grids;
    QStringList counts;
    for (const StateNode& c : Items(n, "channels"))
    {
        const QString name = Str(c, "name");
        int index = -1;
        for (int i = 0; i < GsHeatmapCanvas::kChannels; i++)
            if (GsHeatmapCanvas::ChannelName(i) == name)
                index = i;
        if (index < 0)
            continue;
        const QByteArray data = QByteArray::fromBase64(Str(c, "data").toLatin1());
        grids[static_cast<size_t>(index)].assign(data.begin(), data.end());
        counts << tr("%1 %2 / %3 hot").arg(name).arg(Int(c, "accesses")).arg(Int(c, "hot_cells"));
    }
    const auto columns = static_cast<uint32_t>(Int(n, "columns"));
    const auto cellBytes = static_cast<uint32_t>(Int(n, "cell_bytes"));
    const auto group = static_cast<uint32_t>(std::max<int64_t>(1, Int(n, "group")));
    _heatmap->setGrid(std::move(grids), _channel->currentData().toInt(), columns, cellBytes, group);

    QStringList head;
    head << tr("Card RAM %1 KB: %2 cells of %3 bytes, shown %4 cells of %5 bytes · frame %6 · decay %7 per frame")
                .arg(Int(n, "ram_bytes") / 1024)
                .arg(Int(n, "cells"))
                .arg(cellBytes)
                .arg(Int(n, "data_cells"))
                .arg(cellBytes * group)
                .arg(Int(n, "frame"))
                .arg(Int(n, "decay"));
    head << tr("Accesses / hot cells: %1").arg(counts.join(QStringLiteral(" · ")));
    showBody(head.join(QLatin1Char('\n')));

    // What the DAC played in the last N frames
    const StateNode& recent = Object(n, "recent");
    const std::vector<StateNode>& channels = Items(recent, "channels");
    const StateNode* dac = channels.empty() ? nullptr : &channels.front();
    const std::vector<StateNode>& ranges = dac ? Items(*dac, "ranges") : Items(recent, "none");
    _recent->setRowCount(static_cast<int>(ranges.size()));
    int64_t bytes = 0;
    for (size_t i = 0; i < ranges.size(); i++)
    {
        const StateNode& r = ranges[i];
        const int row = static_cast<int>(i);
        Put(_recent, row, 0, Hex(Int(r, "start"), 6));
        Put(_recent, row, 1, Hex(Int(r, "end"), 6));
        PutInt(_recent, row, 2, Int(r, "length"));
        Put(_recent, row, 3, Hex(Int(r, "page"), 2));
        Put(_recent, row, 4, Hex(Int(r, "offset"), 4));
        bytes += Int(r, "length");
    }
    setCaption(_recent, ranges.empty()
                            ? tr("What the DAC played in the last %1 frames: nothing (no DAC window fetch)")
                                  .arg(Int(recent, "frames"))
                            : tr("What the DAC played in the last %1 frames: %2 range(s), %3 bytes of card RAM%4")
                                  .arg(Int(recent, "frames"))
                                  .arg(static_cast<int>(ranges.size()))
                                  .arg(bytes)
                                  .arg(dac && Flag(*dac, "truncated") ? tr(" (more not listed)") : QString()));
}

bool GsCpuAnalysisPane::runAction(EmulatorContext* context, const QString& action, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const CardPanes::Target target = CardPanes::Front(context, front);
    if (target == CardPanes::Target::None)
        return fail(tr("No General Sound card fitted"));
    const std::string cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    _lastFrame = ~0ull;  // read again at once
    if (action == QLatin1String("refresh"))
        return true;
    if (action.startsWith(QLatin1String("profile_")))
    {
        const GSProfileControlReply reply = GSProfileControl(context, action.mid(8).toStdString(), cpu);
        return reply.status == 200 || fail(Str(reply.body, "error"));
    }
    if (action == QLatin1String("trace_start"))
    {
        // The port trace's history: the command log's id without `.commands` ("zxbus.1", "zxbus.2.gs")
        std::string device = GsMonitorPane::HistoryDevice(context, front);
        const std::string suffix = ".commands";
        if (device.size() <= suffix.size())
            return fail(tr("No port trace history for this card"));
        device.resize(device.size() - suffix.size());
        HistoryRequest ring;
        ring.verb = "config";
        ring.device = device;
        ring.mode = "ring";
        const HistoryReply reply = HistoryControl::Execute(context, ring);
        return reply.httpStatus == 200 || fail(Str(reply.body, "error"));
    }
    if (action.startsWith(QLatin1String("analysis_")))
    {
        const QString verb = action.mid(9);
        GSAnalysisControlRequest request;
        request.action = verb == QLatin1String("decay") ? std::string("status") : verb.toStdString();
        request.cpu = cpu;
        if (_decay && (verb == QLatin1String("decay") || verb == QLatin1String("on")))
            request.decay = _decay->value();
        const GSAnalysisReply reply = GSAnalysisControl(context, request);
        return reply.status == 200 || fail(Str(reply.body, "error"));
    }
    return fail(tr("No such action: %1").arg(action));
}

void GsCpuAnalysisPane::showUnavailable(const QString& why)
{
    _status->setText(why);
    _status->setVisible(true);
    _body->setVisible(false);
}

void GsCpuAnalysisPane::showBody(const QString& headline)
{
    _status->clear();
    _status->setVisible(false);
    _body->setVisible(true);
    _headline->setText(headline);
}

void GsCpuAnalysisPane::showHint(const QString& text)
{
    _hint->setText(text);
    _hintBar->setVisible(!text.isEmpty());
}

QString GsCpuAnalysisPane::status() const
{
    return _status->text();
}

QString GsCpuAnalysisPane::headline() const
{
    return _headline->text();
}

QString GsCpuAnalysisPane::hint() const
{
    return _hintBar->isVisibleTo(const_cast<GsCpuAnalysisPane*>(this)) ? _hint->text() : QString();
}

QPushButton* GsCpuAnalysisPane::button(const QString& action) const
{
    for (const auto& [name, button] : _buttons)
        if (name == action)
            return button;
    return nullptr;
}

QString GsCpuAnalysisPane::Cell(const QTableWidget* table, int row, const QString& column)
{
    if (!table)
        return QString();
    for (int c = 0; c < table->columnCount(); c++)
        if (table->horizontalHeaderItem(c) && table->horizontalHeaderItem(c)->text() == column)
        {
            const QTableWidgetItem* item = table->item(row, c);
            return item ? item->text() : QString();
        }
    return QString();
}

int GsCpuAnalysisPane::FindRow(const QTableWidget* table, const QString& column, const QString& text)
{
    for (int row = 0; table && row < table->rowCount(); row++)
        if (Cell(table, row, column) == text)
            return row;
    return -1;
}

// endregion </GsCpuAnalysisPane>

// region <GsEventsCanvas>

GsEventsCanvas::GsEventsCanvas(QWidget* parent) : QWidget(parent)
{
    _shown.fill(true);
    setMouseTracking(false);
    setMinimumHeight(40);
}

void GsEventsCanvas::setFrame(std::vector<Row> rows, int64_t periodCycles)
{
    _rows = std::move(rows);
    _period = periodCycles;
    setMinimumHeight(std::max(40, static_cast<int>(_rows.size()) * kRowPixels));
    updateGeometry();
    update();
}

void GsEventsCanvas::setShown(Category category, bool shown)
{
    _shown[static_cast<size_t>(category)] = shown;
    update();
}

QSize GsEventsCanvas::sizeHint() const
{
    return QSize(320, std::max(40, static_cast<int>(_rows.size()) * kRowPixels));
}

GsEventsCanvas::Kind GsEventsCanvas::KindOf(const std::string& name)
{
    static const char* const kNames[KindCount] = {"host_data_write", "host_data_read", "host_command", "host_status",
                                                  "host_control",    "dac",            "page",         "volume",
                                                  "zxdma_read",      "zxdma_write"};
    for (int k = 0; k < KindCount; k++)
        if (name == kNames[k])
            return static_cast<Kind>(k);
    return KindCount;
}

GsEventsCanvas::Category GsEventsCanvas::CategoryOf(int kind)
{
    switch (kind)
    {
        case HostDataWrite:
        case HostDataRead: return HostData;
        case HostCommand:
        case HostStatus: return HostCommandStatus;
        case HostControl: return HostControlWrites;
        case Dac: return DacFetches;
        case Page: return PageWrites;
        case Volume: return VolumeWrites;
        case ZxDmaRead:
        case ZxDmaWrite: return ZxDma;
        default: return CategoryCount;
    }
}

QColor GsEventsCanvas::Colour(int kind, int64_t detail)
{
    if (kind == Dac)
    {
        // The DAC channels in four greens and teals (8 on a NeoGS: the second four lighter)
        static const QColor kDac[4] = {QColor(70, 220, 90), QColor(30, 190, 170), QColor(170, 230, 60), QColor(40, 150, 70)};
        const QColor base = kDac[static_cast<size_t>(detail & 3)];
        return detail >= 4 ? base.lighter(140) : base;
    }
    if (kind == HostDataRead || kind == HostStatus)
        return CategoryColour(CategoryOf(kind)).darker(130);
    return CategoryColour(CategoryOf(kind));
}

QString GsEventsCanvas::CategoryName(Category category)
{
    switch (category)
    {
        case IsrBars: return QObject::tr("ISR");
        case HostData: return QObject::tr("#B3");
        case HostCommandStatus: return QObject::tr("#BB");
        case HostControlWrites: return QObject::tr("#33");
        case DacFetches: return QObject::tr("DAC");
        case PageWrites: return QObject::tr("Page");
        case VolumeWrites: return QObject::tr("Volume");
        case ZxDma: return QObject::tr("ZX-DMA");
        default: return QString();
    }
}

QColor GsEventsCanvas::CategoryColour(Category category)
{
    switch (category)
    {
        case IsrBars: return QColor(80, 140, 255);
        case HostData: return QColor(255, 205, 0);
        case HostCommandStatus: return QColor(255, 90, 90);
        case HostControlWrites: return QColor(255, 255, 255);
        case DacFetches: return QColor(70, 220, 90);
        case PageWrites: return QColor(200, 120, 255);
        case VolumeWrites: return QColor(255, 140, 40);
        case ZxDma: return QColor(0, 220, 255);
        default: return QColor(128, 128, 128);
    }
}

QString GsEventsCanvas::describe(int row, int64_t cycle, int64_t tolerance) const
{
    if (row < 0 || row >= rowCount())
        return QString();
    const Row& r = _rows[static_cast<size_t>(row)];
    QStringList lines;
    lines << QObject::tr("Period %1 · cycle %2 (cycle %3 of the frame)").arg(row).arg(cycle).arg(r.start + cycle);
    int listed = 0;
    for (const Dot& d : r.events)
    {
        if (std::abs(d.cycle - cycle) > tolerance || !_shown[static_cast<size_t>(CategoryOf(d.kind))])
            continue;
        if (++listed > 8)
        {
            lines << QObject::tr("…");
            break;
        }
        const QString value = Hex(d.value, 2);
        QString text;
        switch (d.kind)
        {
            case HostDataWrite: text = QObject::tr("host writes #B3 = %1").arg(value); break;
            case HostDataRead: text = QObject::tr("host reads #B3 = %1").arg(value); break;
            case HostCommand: text = QObject::tr("host command #BB = %1").arg(value); break;
            case HostStatus: text = QObject::tr("host reads the status #BB = %1").arg(value); break;
            case HostControl: text = QObject::tr("host writes #33 = %1").arg(value); break;
            case Dac: text = QObject::tr("DAC channel %1 fetch = %2").arg(d.detail + 1).arg(value); break;
            case Page: text = QObject::tr("page port %1 = %2").arg(Hex(d.detail, 2), value); break;
            case Volume: text = QObject::tr("volume channel %1 = %2").arg(d.detail + 1).arg(value); break;
            case ZxDmaRead: text = QObject::tr("ZX-DMA reads card %1 = %2").arg(Hex(d.detail, 6), value); break;
            case ZxDmaWrite: text = QObject::tr("ZX-DMA writes card %1 = %2").arg(Hex(d.detail, 6), value); break;
            default: text = QObject::tr("event = %1").arg(value); break;
        }
        lines << QObject::tr("cycle %1: %2").arg(d.cycle).arg(text);
    }
    if (_shown[IsrBars])
        for (const Bar& b : r.isrs)
        {
            const int64_t end = b.end < 0 ? _period : b.end;
            if (cycle < b.accept - tolerance || cycle > end + tolerance)
                continue;
            if (b.end < 0)
                lines << QObject::tr("%1 accepted at cycle %2, still running at the frame's end (entry %3)")
                             .arg(b.nmi ? QStringLiteral("NMI") : QStringLiteral("INT"))
                             .arg(b.accept)
                             .arg(Hex(b.entry, 4));
            else
                lines << QObject::tr("%1 accepted at cycle %2, ISR to %3 (%4 cycles%5), entry %6")
                             .arg(b.nmi ? QStringLiteral("NMI") : QStringLiteral("INT"))
                             .arg(b.accept)
                             .arg(b.end)
                             .arg(b.end - b.accept)
                             .arg(b.end > _period ? QObject::tr(", past the period: an overrun") : QString())
                             .arg(Hex(b.entry, 4));
        }
    return lines.join(QLatin1Char('\n'));
}

void GsEventsCanvas::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(event->rect(), QColor(24, 24, 28));
    if (_rows.empty() || _period <= 0)
    {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, QObject::tr("No profiled frame"));
        return;
    }
    const double sx = static_cast<double>(width()) / static_cast<double>(_period);
    const int first = std::max(0, event->rect().top() / kRowPixels);
    const int last = std::min(rowCount() - 1, event->rect().bottom() / kRowPixels);
    const QColor isr = CategoryColour(IsrBars);
    const QColor overrun(230, 60, 60);
    const double dotWidth = std::max(2.0, sx);
    for (int row = first; row <= last; row++)
    {
        const Row& r = _rows[static_cast<size_t>(row)];
        const double y = row * kRowPixels;
        if (_shown[IsrBars])
            for (const Bar& b : r.isrs)
            {
                const int64_t end = b.end < 0 ? _period : b.end;
                const double x0 = static_cast<double>(b.accept) * sx;
                const double x1 = static_cast<double>(std::min(end, _period)) * sx;
                p.fillRect(QRectF(x0, y, std::max(1.0, x1 - x0), kRowPixels), end > _period ? overrun : isr);
            }
        for (const Dot& d : r.events)
        {
            const Category category = CategoryOf(d.kind);
            if (category == CategoryCount || !_shown[static_cast<size_t>(category)])
                continue;
            p.fillRect(QRectF(static_cast<double>(d.cycle) * sx, y, dotWidth, kRowPixels), Colour(d.kind, d.detail));
        }
    }
}

bool GsEventsCanvas::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        if (_period > 0 && width() > 0)
        {
            const int row = help->pos().y() / kRowPixels;
            const int64_t cycle = static_cast<int64_t>(help->pos().x()) * _period / width();
            const int64_t tolerance = std::max<int64_t>(1, (3 * _period + width() - 1) / width());
            const QString text = describe(row, cycle, tolerance);
            if (!text.isEmpty())
            {
                QToolTip::showText(help->globalPos(), text, this);
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

// endregion </GsEventsCanvas>

// region <GsHeatmapCanvas>

GsHeatmapCanvas::GsHeatmapCanvas(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(128, 128);
}

QString GsHeatmapCanvas::ChannelName(int channel)
{
    static const char* const kNames[kChannels] = {"fetch", "read", "write", "dac", "sd_dma", "zx_dma"};
    return channel >= 0 && channel < kChannels ? QString::fromLatin1(kNames[channel]) : QObject::tr("all");
}

QColor GsHeatmapCanvas::ChannelColour(int channel)
{
    static const QColor kColours[kChannels] = {QColor(60, 220, 90),  QColor(70, 140, 255), QColor(255, 80, 60),
                                               QColor(255, 210, 40), QColor(40, 220, 230), QColor(230, 80, 230)};
    return channel >= 0 && channel < kChannels ? kColours[channel] : QColor(255, 255, 255);
}

void GsHeatmapCanvas::setGrid(std::array<std::vector<uint8_t>, kChannels> grids, int channel, uint32_t columns,
                              uint32_t cellBytes, uint32_t group)
{
    _grids = std::move(grids);
    _columns = columns;
    _cellBytes = cellBytes;
    _group = std::max<uint32_t>(1, group);
    setChannel(channel);
}

void GsHeatmapCanvas::setChannel(int channel)
{
    _channel = channel;
    const int count = cells();
    if (count == 0 || _columns == 0)
    {
        _image = QImage();
        update();
        return;
    }
    const int rows = (count + static_cast<int>(_columns) - 1) / static_cast<int>(_columns);
    _image = QImage(static_cast<int>(_columns), rows, QImage::Format_RGB32);
    _image.fill(QColor(16, 16, 20));
    for (int cell = 0; cell < count; cell++)
    {
        int r = 0, g = 0, b = 0;
        for (int c = 0; c < kChannels; c++)
        {
            if (_channel >= 0 && c != _channel)
                continue;
            const std::vector<uint8_t>& grid = _grids[static_cast<size_t>(c)];
            if (static_cast<size_t>(cell) >= grid.size() || !grid[static_cast<size_t>(cell)])
                continue;
            // A touched cell shows at a quarter at least; full heat the channel's colour
            const int heat = 64 + grid[static_cast<size_t>(cell)] * 191 / 255;
            const QColor colour = ChannelColour(c);
            r += colour.red() * heat / 255;
            g += colour.green() * heat / 255;
            b += colour.blue() * heat / 255;
        }
        if (r || g || b)
            _image.setPixel(cell % static_cast<int>(_columns), cell / static_cast<int>(_columns),
                            qRgb(std::min(r, 255), std::min(g, 255), std::min(b, 255)));
    }
    update();
}

int GsHeatmapCanvas::cells() const
{
    size_t count = 0;
    for (const std::vector<uint8_t>& grid : _grids)
        count = std::max(count, grid.size());
    return static_cast<int>(count);
}

int GsHeatmapCanvas::hotCells() const
{
    int hot = 0;
    for (int cell = 0; cell < cells(); cell++)
        for (int c = 0; c < kChannels; c++)
        {
            if (_channel >= 0 && c != _channel)
                continue;
            const std::vector<uint8_t>& grid = _grids[static_cast<size_t>(c)];
            if (static_cast<size_t>(cell) < grid.size() && grid[static_cast<size_t>(cell)])
            {
                hot++;
                break;
            }
        }
    return hot;
}

QString GsHeatmapCanvas::describe(int cell) const
{
    if (cell < 0 || cell >= cells())
        return QString();
    const uint64_t span = static_cast<uint64_t>(_cellBytes) * _group;
    const uint64_t start = static_cast<uint64_t>(cell) * span;
    const uint64_t end = start + span - 1;
    QStringList lines;
    lines << QObject::tr("RAM %1-%2 (page %3, offset %4)")
                 .arg(Hex(static_cast<int64_t>(start), 6), Hex(static_cast<int64_t>(end), 6),
                      Hex(static_cast<int64_t>(start / kPage), 2), Hex(static_cast<int64_t>(start % kPage), 4));
    QStringList heat;
    for (int c = 0; c < kChannels; c++)
    {
        const std::vector<uint8_t>& grid = _grids[static_cast<size_t>(c)];
        if (static_cast<size_t>(cell) < grid.size())
            heat << QStringLiteral("%1 %2").arg(ChannelName(c)).arg(grid[static_cast<size_t>(cell)]);
    }
    lines << QObject::tr("heat (0-255): %1").arg(heat.join(QStringLiteral(" · ")));
    return lines.join(QLatin1Char('\n'));
}

QRect GsHeatmapCanvas::gridRect() const
{
    if (_image.isNull())
        return QRect();
    // Square cells as large as fit
    const double scale = std::min(static_cast<double>(width()) / _image.width(), static_cast<double>(height()) / _image.height());
    const int w = static_cast<int>(_image.width() * scale);
    const int h = static_cast<int>(_image.height() * scale);
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

int GsHeatmapCanvas::cellAt(const QPoint& point) const
{
    const QRect grid = gridRect();
    if (grid.isEmpty() || !grid.contains(point))
        return -1;
    const int column = (point.x() - grid.left()) * _image.width() / grid.width();
    const int row = (point.y() - grid.top()) * _image.height() / grid.height();
    return row * static_cast<int>(_columns) + column;
}

void GsHeatmapCanvas::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(event->rect(), palette().window());
    if (_image.isNull())
    {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, QObject::tr("No heat map: switch the analysis on"));
        return;
    }
    p.drawImage(gridRect(), _image);  // nearest neighbour: one cell, one block
}

bool GsHeatmapCanvas::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString text = describe(cellAt(help->pos()));
        if (!text.isEmpty())
            QToolTip::showText(help->globalPos(), text, this);
        else
        {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}

// endregion </GsHeatmapCanvas>
