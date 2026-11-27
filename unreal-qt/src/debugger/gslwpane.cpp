#include "gslwpane.h"

#include <QBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>

#include <algorithm>

#include "debugger/cardpanes.h"
#include "emulator/state/devicestate.h"

namespace
{
// The lit state chip: the debugger's change colour (reportpane.cpp, boardpane.cpp)
const char* const kLitStyle = "background-color: rgb(255, 220, 90); color: black; border: 1px solid rgb(160, 120, 0); "
                              "border-radius: 3px; padding: 2px 6px; font-weight: bold;";
const char* const kChipStyle = "color: palette(mid); border: 1px solid palette(mid); border-radius: 3px; padding: 2px 6px;";

const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    if (!v)
        return 0;
    if (v->kind == StateNode::Kind::Int)
        return v->i;
    if (v->kind == StateNode::Kind::Double)
        return static_cast<int64_t>(v->d);
    return 0;
}

double Real(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    if (!v)
        return 0.0;
    return v->kind == StateNode::Kind::Double ? v->d : v->kind == StateNode::Kind::Int ? static_cast<double>(v->i) : 0.0;
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
    static const std::vector<StateNode> kNone;
    const StateNode* v = Member(node, key);
    return v && v->isArray() ? v->items : kNone;
}

QString Grouped(int64_t value)
{
    QString digits = QString::number(value < 0 ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(i, QLatin1Char(','));
    return value < 0 ? QLatin1Char('-') + digits : digits;
}

QString Hex2(int64_t value)
{
    return QLatin1Char('#') + QString::number(static_cast<qulonglong>(value & 0xFF), 16).toUpper().rightJustified(2, QLatin1Char('0'));
}

QString Rate(double hz)
{
    return hz > 0.0 ? QString::number(hz, 'f', 0) : QStringLiteral("–");
}

QTableWidget* MakeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setWordWrap(false);
    return table;
}

void FillRow(QTableWidget* table, int row, const QStringList& cells)
{
    for (int column = 0; column < cells.size() && column < table->columnCount(); column++)
    {
        QTableWidgetItem* item = table->item(row, column);
        if (!item)
            table->setItem(row, column, item = new QTableWidgetItem());
        if (item->text() != cells[column])
            item->setText(cells[column]);
    }
}

void SetBar(QProgressBar* bar, int64_t value, int64_t maximum, const QString& format)
{
    const int top = static_cast<int>(std::max<int64_t>(maximum, 1));
    bar->setRange(0, top);
    bar->setValue(static_cast<int>(std::clamp<int64_t>(value, 0, top)));
    bar->setFormat(format);
}
}  // namespace

const QStringList& GsLwPane::States()
{
    static const QStringList kStates{"idle", "command", "params", "upload", "reply", "covox"};
    return kStates;
}

GsLwPane::GsLwPane(View view, QWidget* parent) : QWidget(parent), _view(view)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setVisible(false);
    layout->addWidget(_status);
    _body = new QWidget(this);
    auto* body = new QVBoxLayout(_body);
    body->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_body, 1);
    if (view == View::Interpreter)
        BuildInterpreter();
    else
        BuildPlayer();
}

void GsLwPane::BuildInterpreter()
{
    auto* body = static_cast<QVBoxLayout*>(_body->layout());
    // The state diagram: Idle -> Command -> Params -> Upload / Reply -> Idle, the current state lit (Covox: the stream mode)
    auto* diagram = new QHBoxLayout();
    diagram->setSpacing(4);
    const QStringList titles{tr("Idle"), tr("Command"), tr("Params"), tr("Upload"), tr("Reply"), tr("Covox")};
    const QStringList arrows{QStringLiteral("→"), QStringLiteral("→"), QStringLiteral("→"), QStringLiteral("/"),
                             QStringLiteral("→ Idle   ·")};
    for (int i = 0; i < titles.size(); i++)
    {
        auto* chip = new QLabel(titles[i], _body);
        chip->setStyleSheet(kChipStyle);
        chip->setToolTip(tr("Interpreter state \"%1\"").arg(States()[i]));
        _chips.push_back(chip);
        diagram->addWidget(chip);
        if (i < arrows.size())
            diagram->addWidget(new QLabel(arrows[i], _body));
    }
    diagram->addStretch(1);
    body->addLayout(diagram);

    _headline = new QLabel(_body);
    QFont bold = _headline->font();
    bold.setBold(true);
    _headline->setFont(bold);
    _headline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(_headline);

    _form = new QFormLayout();
    body->addLayout(_form);
    AddField(tr("Command"));
    AddField(tr("Bytes"));
    AddField(tr("Reply"));
    AddField(tr("Selector"));
    _params = new QProgressBar(_body);
    _form->addRow(tr("Params queue"), _params);
    _replies = new QProgressBar(_body);
    _form->addRow(tr("Replies queue"), _replies);
    AddField(tr("Upload"));
    _upload = new QProgressBar(_body);
    _form->addRow(QString(), _upload);
    body->addStretch(1);
}

void GsLwPane::BuildPlayer()
{
    auto* body = static_cast<QVBoxLayout*>(_body->layout());
    _headline = new QLabel(_body);
    QFont bold = _headline->font();
    bold.setBold(true);
    _headline->setFont(bold);
    _headline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(_headline);

    _form = new QFormLayout();
    body->addLayout(_form);
    AddField(tr("Position"));
    AddField(tr("Pattern"));
    AddField(tr("Row"));
    AddField(tr("Tick"));
    AddField(tr("Speed"));
    AddField(tr("BPM"));
    AddField(tr("MTSTAT"));
    AddField(tr("FX"));

    _modules = MakeTable({tr("#"), tr("State"), tr("Name"), tr("Length")}, _body);
    _modules->setMaximumHeight(_modules->horizontalHeader()->sizeHint().height() + 2 * _modules->verticalHeader()->defaultSectionSize() + 4);
    body->addWidget(new QLabel(tr("Module"), _body));
    body->addWidget(_modules);

    _channels = MakeTable({tr("Ch"), tr("Sample"), tr("Volume"), tr("Period"), tr("Note"), tr("Rate Hz")}, _body);
    _channels->setRowCount(4);
    _channels->setMaximumHeight(_channels->horizontalHeader()->sizeHint().height() + 4 * _channels->verticalHeader()->defaultSectionSize() + 4);
    body->addWidget(new QLabel(tr("Channels"), _body));
    body->addWidget(_channels);

    // The Unreal Speccy GS dialog's sample columns
    _samples = MakeTable({tr("#"), tr("Name"), tr("Volume"), tr("Note"), tr("Priority"), tr("Rate Hz"), tr("Length"),
                          QStringLiteral("*"), tr("Loop")},
                         _body);
    _samples->horizontalHeaderItem(7)->setToolTip(tr("The sample the effect commands act on (CURSMP)"));
    body->addWidget(new QLabel(tr("Samples"), _body));
    body->addWidget(_samples, 1);
}

QLabel* GsLwPane::AddField(const QString& name)
{
    auto* value = new QLabel(_body);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _form->addRow(name, value);
    _fields.emplace_back(name, value);
    return value;
}

void GsLwPane::SetField(const QString& name, const QString& text)
{
    for (auto& [title, label] : _fields)
        if (title == name)
        {
            if (label->text() != text)
                label->setText(text);
            return;
        }
}

void GsLwPane::refresh(EmulatorContext* context, bool live)
{
    (void)live;  // a paused machine reads the same report: the view freezes by itself
    if (!CardPanes::LightweightFitted(context))
    {
        display(nullptr, tr("The lightweight player is not in the GS slot (plug one: slots plug zxbus.next gs-lw)"));
        return;
    }
    const StateNode gs = DeviceState::Gs(context);
    const StateNode* part = gs.find(_view == View::Interpreter ? "interpreter" : "player");
    display(part && part->isObject() ? part : nullptr, tr("The card's report has no %1").arg(_view == View::Interpreter ? "interpreter" : "player"));
}

void GsLwPane::display(const StateNode* part, const QString& why)
{
    if (!part)
    {
        _status->setText(why);
        _status->setVisible(true);
        _body->setVisible(false);
        _lit.clear();
        return;
    }
    _status->clear();
    _status->setVisible(false);
    _body->setVisible(true);
    if (_view == View::Interpreter)
        ShowInterpreter(*part);
    else
        ShowPlayer(*part);
}

void GsLwPane::ShowInterpreter(const StateNode& in)
{
    _lit = Str(in, "state");
    for (int i = 0; i < static_cast<int>(_chips.size()); i++)
        _chips[static_cast<size_t>(i)]->setStyleSheet(States()[i] == _lit ? kLitStyle : kChipStyle);
    _headline->setText(Str(in, "text"));

    const QString name = Str(in, "command_name");
    SetField(tr("Command"), Hex2(Int(in, "command")) + (name.isEmpty() ? QString() : QStringLiteral(" ") + name));
    SetField(tr("Bytes"), tr("expected %1, received %2").arg(Grouped(Int(in, "expected")), Grouped(Int(in, "received"))));
    SetField(tr("Reply"), Flag(in, "reply_pending") && Member(in, "reply_next")
                              ? tr("pending, next %1").arg(Hex2(Int(in, "reply_next")))
                              : tr("none pending"));
    SetField(tr("Selector"), Flag(in, "selector_expected") ? tr("awaited") : tr("no"));

    const int64_t capacity = std::max<int64_t>(Int(in, "queue_capacity"), 1);
    SetBar(_params, Int(in, "params_queued"), capacity, tr("%1 of %2").arg(Int(in, "params_queued")).arg(capacity));
    SetBar(_replies, Int(in, "replies_queued"), capacity, tr("%1 of %2").arg(Int(in, "replies_queued")).arg(capacity));

    const StateNode* upload = Member(in, "upload");
    const StateNode none = StateNode::Object();
    const StateNode& u = upload ? *upload : none;
    const int64_t received = Int(u, "received");
    const int64_t total = Int(u, "total");
    const QString kind = Str(u, "kind") == "fx" ? tr("FX") : tr("module");
    const bool active = Flag(u, "active");
    SetField(tr("Upload"), active ? tr("%1 in progress").arg(kind) : received ? tr("last %1: done").arg(kind) : tr("none"));
    if (total > 0)
        SetBar(_upload, received, total, tr("%1 of %2 bytes (%p%)").arg(Grouped(received), Grouped(total)));
    else
        SetBar(_upload, 0, 1, received ? tr("%1 bytes, the total once the header is in").arg(Grouped(received)) : tr("no upload"));
    _upload->setEnabled(active);
}

void GsLwPane::ShowPlayer(const StateNode& player)
{
    const QString state = Str(player, "state");
    _headline->setText(state == "playing" ? tr("Playing") : state == "stopped" ? tr("Stopped") : tr("No module loaded"));
    SetField(tr("Position"), tr("%1 of %2").arg(Int(player, "position")).arg(Int(player, "song_length")));
    SetField(tr("Pattern"), tr("%1 of %2").arg(Int(player, "pattern")).arg(Int(player, "pattern_count")));
    SetField(tr("Row"), QString::number(Int(player, "row")));
    SetField(tr("Tick"), QString::number(Int(player, "tick")));
    SetField(tr("Speed"), QString::number(Int(player, "speed")));
    SetField(tr("BPM"), QString::number(Int(player, "bpm")));
    SetField(tr("MTSTAT"), Hex2(Int(player, "mtstat")));
    const StateNode* fx = Member(player, "fx");
    SetField(tr("FX"), fx ? tr("%1 counted, current %2%3")
                                .arg(Int(*fx, "count"))
                                .arg(Int(*fx, "current"))
                                .arg(Flag(*fx, "stored") ? QString() : tr(" (not stored on this card)"))
                          : QString());

    const std::vector<StateNode>& modules = Items(player, "modules");
    _modules->setRowCount(static_cast<int>(modules.size()));
    for (size_t i = 0; i < modules.size(); i++)
    {
        const StateNode& m = modules[i];
        FillRow(_modules, static_cast<int>(i),
                {QString::number(Int(m, "number")), Str(m, "marker"), Str(m, "name"), Grouped(Int(m, "length"))});
    }

    const std::vector<StateNode>& channels = Items(player, "channels");
    _channels->setRowCount(static_cast<int>(channels.size()));
    for (size_t i = 0; i < channels.size(); i++)
    {
        const StateNode& c = channels[i];
        const int64_t sample = Int(c, "sample");
        const int64_t period = Int(c, "period");
        const QString note = Str(c, "note");
        FillRow(_channels, static_cast<int>(i),
                {QString::number(Int(c, "channel")), sample ? QString::number(sample) : QStringLiteral("–"),
                 QString::number(Int(c, "volume")), period ? QString::number(period) : QStringLiteral("–"),
                 note.isEmpty() ? QStringLiteral("–") : note, Rate(Real(c, "rate_hz"))});
    }

    const std::vector<StateNode>& samples = Items(player, "samples");
    _samples->setRowCount(static_cast<int>(samples.size()));
    for (size_t i = 0; i < samples.size(); i++)
    {
        const StateNode& s = samples[i];
        const QString note = Str(s, "note");
        FillRow(_samples, static_cast<int>(i),
                {QString::number(Int(s, "number")), Str(s, "name"), QString::number(Int(s, "volume")),
                 note.isEmpty() ? QStringLiteral("–") : note, QString::number(Int(s, "priority")), Rate(Real(s, "rate_hz")),
                 Grouped(Int(s, "length")), Flag(s, "effect") ? QStringLiteral("*") : QString(),
                 Flag(s, "loop") ? QStringLiteral("(L)") : QString()});
    }
}

QString GsLwPane::status() const
{
    return _status->isHidden() ? QString() : _status->text();
}

QString GsLwPane::litState() const
{
    return _lit;
}

QString GsLwPane::headline() const
{
    return _headline ? _headline->text() : QString();
}

QString GsLwPane::field(const QString& name) const
{
    for (const auto& [title, label] : _fields)
        if (title == name)
            return label->text();
    return QString();
}

#include "moc_gslwpane.cpp"
