#include "boardpane.h"

#include <QBoxLayout>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>

#include "debugger/boards/debugboards.h"

namespace
{
// The port watch's change colour (portwatchdialog.cpp, reportpane.cpp): a value that differs from the last stop
const char* const kChangedStyle = "background-color: rgb(255, 220, 90); color: black;";
// The narrowest a block reads well at: blocks stack below this width each
constexpr int kBlockWidth = 250;

const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
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

QString Hex(int64_t value, int digits)
{
    return QLatin1Char('#') + QString::number(static_cast<qulonglong>(value), 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}

/// The names of a `bits` control's set bits, bit 0 first
QStringList SetBits(const StateNode& control)
{
    QStringList set;
    const StateNode* value = Member(control, "value");
    const std::vector<StateNode>& names = Items(control, "names");
    for (size_t bit = 0; value && bit < names.size() && bit < 8; bit++)
        if (((value->i >> bit) & 1) != 0 && !names[bit].s.empty())
            set << QString::fromStdString(names[bit].s);
    return set;
}

/// The value as rich text: a `bits` register's set bits as chips, an LED as a dot
QString RichText(const StateNode& control, const QString& plain)
{
    const QString type = Str(control, "type");
    const StateNode* value = Member(control, "value");
    if (type == QLatin1String("bits") && value)
    {
        QString out = Hex(value->i, 2).toHtmlEscaped();
        for (const QString& name : SetBits(control))
            out += QStringLiteral(" <span style=\"background-color:#5a8cdc; color:white;\">&nbsp;%1&nbsp;</span>").arg(name.toHtmlEscaped());
        return out;
    }
    if (type == QLatin1String("led") && value)
        return value->b ? QStringLiteral("<span style=\"color:#2fbf4a;\">&#9679;</span> on")
                        : QStringLiteral("<span style=\"color:#808080;\">&#9675;</span> off");
    return plain.toHtmlEscaped();
}

void ClearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0))
    {
        if (QWidget* w = item->widget())
            delete w;
        else if (QLayout* l = item->layout())
        {
            ClearLayout(l);
            delete l;
        }
        delete item;
    }
}
}  // namespace

BoardPane::BoardPane(std::vector<Part> parts, Blocks blocks, QWidget* parent)
    : QWidget(parent), _parts(std::move(parts)), _blocks(blocks)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->hide();
    layout->addWidget(_status);
    _answer = new QLabel(this);
    _answer->setWordWrap(true);
    _answer->hide();
    layout->addWidget(_answer);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    _content = new QWidget(scroll);
    QVBoxLayout* contentLayout = new QVBoxLayout(_content);
    contentLayout->setContentsMargins(2, 2, 2, 2);
    _table = new QTableWidget(_content);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->verticalHeader()->hide();
    _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    _table->hide();
    contentLayout->addWidget(_table);
    _blockLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    _blockLayout->setSpacing(4);
    contentLayout->addLayout(_blockLayout);
    contentLayout->addStretch(1);
    scroll->setWidget(_content);
    layout->addWidget(scroll, 1);
}

void BoardPane::setTable(Table table)
{
    _tableSpec = std::move(table);
    _table->setColumnCount(static_cast<int>(_tableSpec.columns.size()) + 1);
    QStringList headers{tr("Module")};
    for (const auto& column : _tableSpec.columns)
        headers << column.second;
    _table->setHorizontalHeaderLabels(headers);
    _table->setRowCount(static_cast<int>(_tableSpec.rows.size()));
    for (int r = 0; r < static_cast<int>(_tableSpec.rows.size()); r++)
        for (int c = 0; c < _table->columnCount(); c++)
            _table->setItem(r, c, new QTableWidgetItem(c == 0 ? _tableSpec.rows[static_cast<size_t>(r)].second : QString()));
    _table->setVisible(!_tableSpec.section.empty());
}

QString BoardPane::ValueText(const StateNode& control)
{
    const QString type = Str(control, "type");
    const StateNode* value = Member(control, "value");
    if (!value)
        return QString();
    if (type == QLatin1String("hex8"))
        return Hex(value->i, 2);
    if (type == QLatin1String("hex16"))
        return Hex(value->i, 4);
    if (type == QLatin1String("hex21"))
        return Hex(value->i, 6);
    if (type == QLatin1String("bits"))
    {
        const QStringList set = SetBits(control);
        return set.isEmpty() ? Hex(value->i, 2) : Hex(value->i, 2) + QLatin1Char(' ') + set.join(QLatin1Char(' '));
    }
    if (type == QLatin1String("bool"))
        return value->b ? QStringLiteral("yes") : QStringLiteral("no");
    if (type == QLatin1String("led"))
        return value->b ? QStringLiteral("on") : QStringLiteral("off");
    if (type == QLatin1String("bar"))
    {
        const StateNode* max = Member(control, "max");
        return Grouped(value->i) + QStringLiteral(" / ") + Grouped(max ? max->i : 0);
    }
    switch (value->kind)
    {
        case StateNode::Kind::Int: return Grouped(value->i);
        case StateNode::Kind::Double: return QString::number(value->d, 'f', 2);
        case StateNode::Kind::String: return QString::fromStdString(value->s);
        case StateNode::Kind::Bool: return value->b ? QStringLiteral("yes") : QStringLiteral("no");
        default: return QString();
    }
}

std::vector<StateNode> BoardPane::Collect(EmulatorContext* context)
{
    bool needStats = !_tableSpec.section.empty();
    for (const Part& part : _parts)
        needStats = needStats || !part.statsSection.empty();
    const std::vector<DebugBoardBuilder> built = DebugBoards::Build(context, needStats ? "gs_stats" : "");
    if (built.empty())
        _stats = StateNode();  // no card: nothing to keep
    for (const DebugBoardBuilder& b : built)
    {
        if (!needStats || b.Id() != "gs_stats")
            continue;
        StateNode stats = b.Build(false);
        if (const StateNode* available = stats.find("available"); available && available->b)
            _stats = std::move(stats);  // pending (a running machine before a frame end): the last block stays
    }

    std::vector<StateNode> boards;
    for (const Part& part : _parts)
    {
        const DebugBoardBuilder* builder = nullptr;
        for (const DebugBoardBuilder& b : built)
            if (b.Id() == part.board)
                builder = &b;
        if (!builder)
            continue;
        StateNode board = builder->Build(false);
        StateNode& columns = board["columns"];
        for (StateNode& column : columns.items)
        {
            std::vector<StateNode>& groups = column["groups"].items;
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                                        [&part](const StateNode& g) { return part.skipGroups.contains(Str(g, "title")); }),
                         groups.end());
        }
        columns.items.erase(std::remove_if(columns.items.begin(), columns.items.end(),
                                           [](const StateNode& c) { return Items(c, "groups").empty(); }),
                            columns.items.end());
        // The section's counters of the statistics block join the board as a group (the same numbers as gs stats)
        if (!part.statsSection.empty() && _stats.isObject())
        {
            StateNode counters = StateNode::Array();
            for (const StateNode& column : Items(_stats, "columns"))
                for (const StateNode& group : Items(column, "groups"))
                    if (Str(group, "title").toStdString() == part.statsSection)
                        for (const StateNode& control : Items(group, "controls"))
                            if (Str(control, "type") == QLatin1String("counter"))
                                counters.push(control);
            if (!counters.items.empty())
            {
                StateNode group = StateNode::Object();
                group["title"] = std::string("Counters");
                group["controls"] = std::move(counters);
                if (columns.items.empty())
                {
                    StateNode column = StateNode::Object();
                    column["groups"] = StateNode::Array();
                    columns.push(std::move(column));
                }
                columns.items.back()["groups"].push(std::move(group));
            }
        }
        boards.push_back(std::move(board));
    }
    return boards;
}

QString BoardPane::Signature(const std::vector<StateNode>& boards)
{
    QString signature;
    for (const StateNode& board : boards)
    {
        signature += Str(board, "id") + (Member(board, "available") && board.find("available")->b ? "+" : "-") + '{';
        for (const StateNode& column : Items(board, "columns"))
        {
            signature += '[';
            for (const StateNode& group : Items(column, "groups"))
            {
                signature += Str(group, "title") + '(';
                for (const StateNode& control : Items(group, "controls"))
                    signature += Str(control, "id") + ':' + Str(control, "type") + ',';
                signature += ')';
            }
            signature += ']';
        }
        for (const StateNode& action : Items(board, "actions"))
            signature += Str(action, "id") + ';';
        signature += '}';
    }
    return signature;
}

void BoardPane::Rebuild(const std::vector<StateNode>& boards)
{
    ClearLayout(_blockLayout);
    _blockWidgets.clear();
    _groupBoxes.clear();
    _cards.clear();
    _shown.clear();
    _groups.clear();
    _actions.clear();

    auto addGroup = [this](QWidget* parent, QVBoxLayout* into, const QString& board, const StateNode& group) {
        QGroupBox* box = new QGroupBox(Str(group, "title"), parent);
        _groupBoxes.push_back(box);
        _groups[board] << Str(group, "title");
        QFormLayout* form = new QFormLayout(box);
        form->setContentsMargins(4, 2, 4, 2);
        form->setVerticalSpacing(1);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const StateNode& control : Items(group, "controls"))
        {
            const QString note = Str(control, "note");
            QLabel* label = new QLabel(Str(control, "label"), box);
            label->setToolTip(note);
            Shown shown;
            if (Str(control, "type") == QLatin1String("bar"))
            {
                shown.bar = new QProgressBar(box);
                const StateNode* min = Member(control, "min");
                const StateNode* max = Member(control, "max");
                shown.bar->setRange(min ? static_cast<int>(min->i) : 0, max ? static_cast<int>(max->i) : 100);
                shown.bar->setMaximumHeight(label->sizeHint().height());
                shown.bar->setToolTip(note);
                form->addRow(label, shown.bar);
            }
            else
            {
                shown.value = new QLabel(box);
                shown.value->setTextFormat(Qt::RichText);
                shown.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
                shown.value->setWordWrap(true);
                shown.value->setToolTip(note);
                form->addRow(label, shown.value);
            }
            _shown[board + '/' + Str(control, "id")] = shown;
        }
        into->addWidget(box);
    };
    auto addActions = [this](QWidget* parent, QVBoxLayout* into, const StateNode& board) {
        const std::vector<StateNode>& actions = Items(board, "actions");
        const QString id = Str(board, "id");
        const bool owned = std::any_of(_uiActions.begin(), _uiActions.end(), [&](const UiAction& a) { return a.board == id; });
        if (actions.empty() && !owned)
            return;
        QHBoxLayout* row = new QHBoxLayout();
        for (const StateNode& action : actions)
        {
            const QString name = Str(action, "id");
            QToolButton* button = new QToolButton(parent);
            button->setText(Str(action, "title"));
            if (!Str(action, "param").isEmpty())
            {
                // The one parameter the boards take is a path: an image file or a host folder (neogs_sd insert)
                button->setText(Str(action, "title") + QStringLiteral("…"));
                button->setPopupMode(QToolButton::InstantPopup);
                QMenu* menu = new QMenu(button);
                menu->addAction(tr("Image file…"), this, [this, id, name]() {
                    const QString path = QFileDialog::getOpenFileName(this, tr("Insert an SD card image"));
                    if (!path.isEmpty())
                        emit actionRequested(id, name, path);
                });
                menu->addAction(tr("Host folder…"), this, [this, id, name]() {
                    const QString path = QFileDialog::getExistingDirectory(this, tr("Insert a host folder as the SD card"));
                    if (!path.isEmpty())
                        emit actionRequested(id, name, path);
                });
                button->setMenu(menu);
            }
            else
                connect(button, &QToolButton::clicked, this, [this, id, name]() { emit actionRequested(id, name, QString()); });
            _actions[id + '/' + name] = button;
            row->addWidget(button);
        }
        for (const UiAction& action : _uiActions)
        {
            if (action.board != id)
                continue;
            QToolButton* button = new QToolButton(parent);
            button->setText(action.title);
            button->setToolTip(action.tooltip);
            const QString name = action.id;
            connect(button, &QToolButton::clicked, this, [this, id, name]() { emit uiActionRequested(id, name); });
            _actions[id + '/' + name] = button;
            row->addWidget(button);
        }
        row->addStretch(1);
        into->addLayout(row);
    };

    for (const StateNode& board : boards)
    {
        const QString id = Str(board, "id");
        _groups.emplace(id, QStringList());
        if (_blocks == Blocks::Cards)
        {
            QGroupBox* card = new QGroupBox(Str(board, "title"), _content);
            card->setObjectName(id);
            _cards.push_back(card);
            QVBoxLayout* layout = new QVBoxLayout(card);
            layout->setContentsMargins(3, 3, 3, 3);
            for (const StateNode& column : Items(board, "columns"))
                for (const StateNode& group : Items(column, "groups"))
                    addGroup(card, layout, id, group);
            addActions(card, layout, board);
            layout->addStretch(1);
            _blockLayout->addWidget(card, 1, Qt::AlignTop);
            _blockWidgets.push_back(card);
            continue;
        }
        for (const StateNode& column : Items(board, "columns"))
        {
            QWidget* block = new QWidget(_content);
            QVBoxLayout* layout = new QVBoxLayout(block);
            layout->setContentsMargins(0, 0, 0, 0);
            for (const StateNode& group : Items(column, "groups"))
                addGroup(block, layout, id, group);
            if (&column == &Items(board, "columns").back())
                addActions(block, layout, board);
            layout->addStretch(1);
            _blockLayout->addWidget(block, 1, Qt::AlignTop);
            _blockWidgets.push_back(block);
        }
    }
    UpdateDirection();
}

bool BoardPane::Changed(const QString& path, const QString& text, bool live)
{
    if (live)
        return false;  // live values are never compared (rules.md section 4)
    const auto last = _lastStop.find(path);
    const bool differs = last != _lastStop.end() && last->second != text;
    if (differs)
        _marked[path] = true;
    return differs;
}

void BoardPane::Fill(const std::vector<StateNode>& boards, bool live, std::map<QString, QString>& now)
{
    size_t box = 0;
    size_t card = 0;
    for (const StateNode& board : boards)
    {
        const QString id = Str(board, "id");
        const StateNode* available = Member(board, "available");
        if (_blocks == Blocks::Cards && card < _cards.size())
        {
            _cards[card]->setTitle(Str(board, "title") + (available && !available->b ? tr(" (not fitted)") : QString()));
            _cards[card++]->setEnabled(!available || available->b);
        }
        for (const StateNode& column : Items(board, "columns"))
            for (const StateNode& group : Items(column, "groups"))
            {
                const QString raw = Str(group, "raw");
                if (box < _groupBoxes.size())
                    _groupBoxes[box++]->setTitle(Str(group, "title") + (raw.isEmpty() ? QString() : QStringLiteral("  ") + raw));
                for (const StateNode& control : Items(group, "controls"))
                {
                    const QString path = id + '/' + Str(control, "id");
                    auto shown = _shown.find(path);
                    if (shown == _shown.end())
                        continue;
                    const QString text = ValueText(control);
                    shown->second.text = text;
                    now[path] = text;
                    const bool changed = Changed(path, text, live);
                    QWidget* target = shown->second.bar ? static_cast<QWidget*>(shown->second.bar) : shown->second.value;
                    if (shown->second.bar)
                    {
                        const StateNode* value = Member(control, "value");
                        shown->second.bar->setValue(value ? static_cast<int>(value->i) : 0);
                        shown->second.bar->setFormat(text);
                    }
                    else
                        shown->second.value->setText(RichText(control, text));
                    target->setStyleSheet(changed ? QString::fromLatin1(kChangedStyle) : QString());
                }
            }
    }
}

void BoardPane::FillTable(bool live, std::map<QString, QString>& now)
{
    if (_tableSpec.section.empty())
        return;
    std::map<std::string, const StateNode*> controls;
    for (const StateNode& column : Items(_stats, "columns"))
        for (const StateNode& group : Items(column, "groups"))
            if (Str(group, "title").toStdString() == _tableSpec.section)
                for (const StateNode& control : Items(group, "controls"))
                    controls[Str(control, "id").toStdString()] = &control;
    for (size_t r = 0; r < _tableSpec.rows.size(); r++)
        for (size_t c = 0; c < _tableSpec.columns.size(); c++)
        {
            const std::string id = _tableSpec.section + "." + _tableSpec.rows[r].first + "." + _tableSpec.columns[c].first;
            const auto found = controls.find(id);
            const std::string& column = _tableSpec.columns[c].first;
            const StateNode* value = found == controls.end() ? nullptr : found->second->find("value");
            QString text = !value ? QStringLiteral("–") : ValueText(*found->second);
            if (value && value->kind == StateNode::Kind::Bool)
                text = value->b ? QStringLiteral("●") : QStringLiteral("○");
            else if (value && value->kind == StateNode::Kind::Int && column.size() >= 7 &&
                     column.compare(column.size() - 7, 7, "address") == 0)
                text = Hex(value->i, 6);
            QTableWidgetItem* item = _table->item(static_cast<int>(r), static_cast<int>(c) + 1);
            if (!item)
                continue;
            item->setText(text);
            const QString path = QStringLiteral("table/") + QString::fromStdString(id);
            now[path] = text;
            if (Changed(path, text, live))
            {
                item->setBackground(QColor(255, 220, 90));
                item->setForeground(QColor(Qt::black));
            }
            else
            {
                item->setBackground(QBrush());
                item->setData(Qt::ForegroundRole, QVariant());
            }
        }
    _table->resizeColumnsToContents();
    _table->horizontalHeader()->setStretchLastSection(true);
}

void BoardPane::refresh(EmulatorContext* context, bool live)
{
    _marked.clear();
    if (!context)
    {
        SetStatus(tr("No machine."));
        return;
    }
    const std::vector<StateNode> boards = Collect(context);
    if (boards.empty())
    {
        QStringList ids;
        for (const Part& part : _parts)
            ids << QString::fromStdString(part.board);
        SetStatus(tr("No board %1 on this machine: a NeoGS card publishes it (slots plug zxbus.next neogs)").arg(ids.join(", ")));
        return;
    }
    SetStatus(live ? tr("live: the machine runs (changes are marked at the next stop)") : QString());
    _content->show();
    const QString signature = Signature(boards);
    if (signature != _signature)
    {
        _signature = signature;
        Rebuild(boards);
    }
    UpdateDirection();
    std::map<QString, QString> now;
    Fill(boards, live, now);
    FillTable(live, now);
    if (!live)
        _lastStop = std::move(now);
    emit refreshed(context, live);
}

void BoardPane::addExtra(QWidget* widget)
{
    QVBoxLayout* contentLayout = static_cast<QVBoxLayout*>(_content->layout());
    widget->setParent(_content);
    contentLayout->insertWidget(contentLayout->count() - 1, widget);  // above the closing stretch
}

void BoardPane::addUiAction(const QString& board, const QString& id, const QString& title, const QString& tooltip)
{
    _uiActions.push_back({board, id, title, tooltip});
    _signature.clear();  // the next refresh builds the action rows again
}

void BoardPane::SetStatus(const QString& text)
{
    _status->setText(text);
    _status->setVisible(!text.isEmpty());
    if (!text.isEmpty() && !text.startsWith(QStringLiteral("live:")))
    {
        _content->hide();  // nothing to show: the status says why
        _signature.clear();
        ClearLayout(_blockLayout);
        _blockWidgets.clear();
        _groupBoxes.clear();
        _cards.clear();
        _shown.clear();
        _groups.clear();
        _actions.clear();
    }
}

DebugBoardReply BoardPane::runAction(EmulatorContext* context, const QString& board, const QString& action, const QString& arg)
{
    DebugBoardReply reply = DebugBoards::Action(context, board.toStdString(), action.toStdString(), arg.toStdString());
    _answer->setText(QString::fromStdString(reply.text).trimmed());
    _answer->setVisible(!_answer->text().isEmpty());
    return reply;
}

void BoardPane::UpdateDirection()
{
    const int count = static_cast<int>(_blockWidgets.size());
    const bool stack = count > 1 && width() < count * kBlockWidth;
    _blockLayout->setDirection(stack ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

void BoardPane::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    UpdateDirection();
}

QString BoardPane::status() const
{
    return _status->isHidden() ? QString() : _status->text();
}

QString BoardPane::answer() const
{
    return _answer->isHidden() ? QString() : _answer->text();
}

QString BoardPane::valueText(const QString& board, const QString& id) const
{
    const auto shown = _shown.find(board + '/' + id);
    return shown == _shown.end() ? QString() : shown->second.text;
}

QStringList BoardPane::groupTitles(const QString& board) const
{
    const auto found = _groups.find(board);
    return found == _groups.end() ? QStringList() : found->second;
}

QStringList BoardPane::blockTitles() const
{
    QStringList titles;
    for (const QGroupBox* card : _cards)
        titles << card->title();
    return titles;
}

QToolButton* BoardPane::actionButton(const QString& board, const QString& action) const
{
    const auto found = _actions.find(board + '/' + action);
    return found == _actions.end() ? nullptr : found->second;
}

bool BoardPane::stacked() const
{
    return _blockLayout->direction() == QBoxLayout::TopToBottom;
}

#include "moc_boardpane.cpp"
