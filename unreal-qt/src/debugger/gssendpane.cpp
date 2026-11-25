#include "gssendpane.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

#include <algorithm>

#include "debugger/cardpanes.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/gshostdriver.h"

namespace
{
const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key, int64_t none = 0)
{
    const StateNode* v = Member(node, key);
    if (v && v->kind == StateNode::Kind::Int)
        return v->i;
    if (v && v->kind == StateNode::Kind::Bool)
        return v->b ? 1 : 0;
    return none;
}

QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

QString Hex2(int64_t value)
{
    return QLatin1Char('#') + QString::number(static_cast<qulonglong>(value & 0xFF), 16).toUpper().rightJustified(2, QLatin1Char('0'));
}

/// The range a parameter of `kind` takes, for its field's hint
QString KindHint(const QString& kind)
{
    if (kind == QLatin1String("volume"))
        return QObject::tr("volume #00-#40");
    if (kind == QLatin1String("note"))
        return QObject::tr("note 0-95");
    if (kind == QLatin1String("channel"))
        return QObject::tr("channel number");
    if (kind == QLatin1String("channel_mask"))
        return QObject::tr("a bit per channel");
    if (kind == QLatin1String("handle"))
        return QObject::tr("handle (0: the current one)");
    if (kind == QLatin1String("port"))
        return QObject::tr("card port");
    if (kind == QLatin1String("low") || kind == QLatin1String("middle") || kind == QLatin1String("high"))
        return QObject::tr("%1 byte").arg(kind);
    if (kind == QLatin1String("mode"))
        return QObject::tr("mode / format code");
    return QObject::tr("byte");
}

QString StateName(const StateNode& conversation)
{
    return Str(conversation, "state");
}
}  // namespace

GsSendPane::GsSendPane(std::shared_ptr<DebugCpuId> follow, QWidget* parent) : QWidget(parent), _follow(std::move(follow))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setVisible(false);
    layout->addWidget(_status);

    _body = new QWidget(this);
    auto* body = new QVBoxLayout(_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);
    layout->addWidget(_body, 1);

    // 1. The command: the profile's set, or a byte typed
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Command"), _body));
    _picker = new QComboBox(_body);
    _picker->setObjectName("gsSendCommand");
    _picker->setEditable(true);
    _picker->setInsertPolicy(QComboBox::NoInsert);
    _picker->setToolTip(tr("A command of the card's firmware profile, or any byte: #NN, $NN, 0xNN or decimal"));
    _picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _picker->setMinimumContentsLength(24);
    top->addWidget(_picker, 1);
    body->addLayout(top);
    connect(_picker, &QComboBox::editTextChanged, this, [this]() {
        // A byte typed: its parameters when the profile names it; the form is rebuilt only when the command changes
        const StateNode* c = current();
        const int command = c ? static_cast<int>(Int(*c, "command")) : -1;
        if (command != _formCommand)
            rebuildForm();
    });

    // 2. The parameters, the replies, the note
    _form = new QWidget(_body);
    _formLayout = new QFormLayout(_form);
    _formLayout->setContentsMargins(0, 0, 0, 0);
    _formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    body->addWidget(_form);
    _note = new QLabel(_body);
    _note->setWordWrap(true);
    _note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _note->setStyleSheet("QLabel { color: palette(placeholder-text); }");
    body->addWidget(_note);

    // 3. Send
    auto* actions = new QHBoxLayout();
    _send = new QPushButton(tr("Send"), _body);
    _send->setObjectName("gsSendButton");
    _send->setToolTip(tr("Send through the card's mailbox as the host would (gs send): the command log marks the bytes "
                         "automation, TTD records them"));
    connect(_send, &QPushButton::clicked, this, &GsSendPane::sendRequested);
    actions->addWidget(_send);
    _hint = new QLabel(_body);
    _hint->setWordWrap(true);
    actions->addWidget(_hint, 1);
    body->addLayout(actions);

    // 4. The sends, newest first
    _table = new QTableWidget(0, 3, _body);
    _table->setObjectName("gsSendTable");
    _table->setHorizontalHeaderLabels({tr("#"), tr("State"), tr("Command → replies")});
    _table->verticalHeader()->setVisible(false);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setWordWrap(false);
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setStretchLastSection(true);
    body->addWidget(_table, 1);

    rebuildForm();
}

std::optional<int> GsSendPane::ParseByte(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    int base = 10;
    if (t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char('$')))
    {
        t = t.mid(1);
        base = 16;
    }
    else if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    {
        t = t.mid(2);
        base = 16;
    }
    bool ok = false;
    const int value = t.toInt(&ok, base);
    if (!ok || value < 0 || value > 0xFF)
        return std::nullopt;
    return value;
}

void GsSendPane::showUnavailable(const QString& why)
{
    _status->setText(why);
    _status->setVisible(true);
    _body->setVisible(false);
}

void GsSendPane::refresh(EmulatorContext* context, bool live)
{
    _live = live;
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const CardPanes::Target target = CardPanes::Front(context, front);
    if (target == CardPanes::Target::None)
    {
        showUnavailable(tr("No General Sound card fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs | multisound)"));
        return;
    }
    if (target == CardPanes::Target::MultiSound)
    {
        showUnavailable(tr("The ZX-MultiSound's card has no send: the send addresses the GS slot's card (gs send)"));
        return;
    }
    _status->setVisible(false);
    _body->setVisible(true);

    // The command set of the profile decoding the card now (an upload's, else the ROM's): rebuilt when it changes
    GSFirmwareObjectsQuery query;
    query.object = "commands";
    const GSFirmwareObjectsReply objects = GSFirmwareObjectsRead(context, query);
    const std::string profile = objects.status == 200 ? Str(objects.body, "profile").toStdString() : std::string();
    if (profile != _profile)
    {
        _profile = profile;
        const StateNode* commands = objects.status == 200 ? objects.body.find("commands") : nullptr;
        loadCommands(commands ? *commands : StateNode::Array());
    }

    // The sends not finished: their state now
    for (Sent& s : _sent)
        if (!s.finished)
        {
            const GSSendReply read = GSConversationRead(context, s.id);
            if (read.status != 200)
            {
                s.finished = true;
                s.text = QString::fromStdString(read.text);
                continue;
            }
            s.conversation = read.body;
            s.text = QString::fromStdString(read.text);
            const QString state = StateName(read.body);
            s.finished = state == QLatin1String("done") || state == QLatin1String("failed");
        }
    _hint->setText(live ? QString() : tr("The machine is paused: a send waits in the queue until it runs"));
    showRows();
}

void GsSendPane::loadCommands(const StateNode& commands)
{
    const QSignalBlocker block(_picker);
    const std::optional<int> picked = current() ? std::optional<int>(static_cast<int>(Int(*current(), "command"))) : std::nullopt;
    _commands.assign(commands.items.begin(), commands.items.end());
    _picker->clear();
    for (const StateNode& c : _commands)
        _picker->addItem(Hex2(Int(c, "command")) + QLatin1Char(' ') + Str(c, "name"), static_cast<int>(Int(c, "command")));
    if (picked)
        selectCommand(*picked);
    rebuildForm();
}

const StateNode* GsSendPane::current() const
{
    // The picker's text: an entry ("#2A Set module master volume") or a byte typed; its first word is the byte
    const std::optional<int> command = ParseByte(_picker->currentText().section(QLatin1Char(' '), 0, 0));
    if (!command)
        return nullptr;
    for (const StateNode& c : _commands)
        if (Int(c, "command") == *command)
            return &c;
    return nullptr;
}

void GsSendPane::selectCommand(int command)
{
    const int index = _picker->findData(command);
    if (index >= 0)
    {
        _picker->setCurrentIndex(index);
        _picker->setEditText(_picker->itemText(index));
    }
    else
        _picker->setEditText(Hex2(command));
    rebuildForm();
}

void GsSendPane::rebuildForm()
{
    while (_formLayout->rowCount() > 0)
        _formLayout->removeRow(0);
    _params.clear();
    _more = nullptr;
    _replies = nullptr;

    const StateNode* c = current();
    _formCommand = c ? static_cast<int>(Int(*c, "command")) : -1;
    const int pre = c ? static_cast<int>(Int(*c, "pre")) : 0;
    const StateNode* params = c ? Member(*c, "params") : nullptr;
    const int count = params ? static_cast<int>(params->items.size()) : 0;
    for (int i = 0; i < count; i++)
    {
        const StateNode& p = params->items[static_cast<size_t>(i)];
        const QString name = Str(p, "name").isEmpty() ? tr("param %1").arg(i + 1) : Str(p, "name");
        auto* field = new QLineEdit(_form);
        field->setPlaceholderText(KindHint(Str(p, "kind")));
        field->setObjectName(QStringLiteral("gsSendParam%1").arg(i));
        _formLayout->addRow(i < pre ? tr("%1 (before)").arg(name) : name, field);
        _params.push_back(field);
    }
    if (!c || Int(*c, "post") < 0)
    {
        // A byte the profile does not name, or a variable count: the bytes after the command in one field
        _more = new QLineEdit(_form);
        _more->setObjectName("gsSendMore");
        _more->setPlaceholderText(tr("bytes after the command, space separated"));
        _formLayout->addRow(c ? tr("More bytes") : tr("Bytes"), _more);
    }
    _replies = new QLineEdit(_form);
    _replies->setObjectName("gsSendReplies");
    const int reply = c ? static_cast<int>(Int(*c, "reply")) : 0;
    _replies->setPlaceholderText(reply ? tr("%1 (the profile's)").arg(reply) : tr("0 (the profile's)"));
    _formLayout->addRow(tr("Replies"), _replies);

    QString note;
    if (c)
    {
        if (Int(*c, "reply") > 0 && !Str(*c, "reply_meaning").isEmpty())
            note += tr("Replies: %1. ").arg(Str(*c, "reply_meaning"));
        if (!Str(*c, "transfer").isEmpty())
            note += tr("Starts a transfer (%1): the bytes after it are not sent here. ").arg(Str(*c, "transfer"));
        if (!Str(*c, "note").isEmpty())
            note += Str(*c, "note");
        if (!Int(*c, "documented"))
            note += (note.isEmpty() ? QString() : QStringLiteral(" ")) + tr("(not in the programming guide)");
    }
    else if (!_commands.empty() || !_profile.empty())
        note = tr("The profile does not name this command: its bytes and replies are yours to give");
    _note->setText(note.trimmed());
    _note->setVisible(!note.isEmpty());
}

bool GsSendPane::request(GSSendRequest& out, QString& error) const
{
    out = GSSendRequest();
    const StateNode* c = current();
    std::optional<int> command = c ? std::optional<int>(static_cast<int>(Int(*c, "command")))
                                   : ParseByte(_picker->currentText().section(QLatin1Char(' '), 0, 0));
    if (!command)
    {
        error = tr("Command: not a byte (#NN, $NN, 0xNN or decimal)");
        return false;
    }
    out.command = static_cast<uint8_t>(*command);
    const int pre = c ? static_cast<int>(Int(*c, "pre")) : 0;
    for (size_t i = 0; i < _params.size(); i++)
    {
        const std::optional<int> value = ParseByte(_params[i]->text());
        if (!value)
        {
            error = tr("%1: not a byte").arg(_formLayout->labelForField(_params[i]) ?
                                                 qobject_cast<QLabel*>(_formLayout->labelForField(_params[i]))->text() :
                                                 tr("parameter %1").arg(i + 1));
            return false;
        }
        if (static_cast<int>(i) < pre)
            out.pre = static_cast<uint8_t>(*value);
        else
            out.params.push_back(static_cast<uint8_t>(*value));
    }
    if (_more)
        for (const QString& part : _more->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        {
            const std::optional<int> value = ParseByte(part);
            if (!value)
            {
                error = tr("Bytes: '%1' is not a byte").arg(part);
                return false;
            }
            out.params.push_back(static_cast<uint8_t>(*value));
        }
    if (_replies && !_replies->text().trimmed().isEmpty())
    {
        bool ok = false;
        const int replies = _replies->text().trimmed().toInt(&ok);
        if (!ok || replies < 0 || replies > 255)
        {
            error = tr("Replies: a count 0-255");
            return false;
        }
        out.replies = static_cast<uint8_t>(replies);
    }
    return true;
}

bool GsSendPane::send(EmulatorContext* context, QString* error)
{
    GSSendRequest req;
    QString why;
    if (!request(req, why))
    {
        _error = why;
        _hint->setText(why);
        if (error)
            *error = why;
        return false;
    }
    // Never waits here: the emulation runs the steps; refresh reads how far it got
    const GSSendReply reply = GSSend(context, req, 0);
    if (reply.status != 200 || reply.id == 0)
    {
        _error = QString::fromStdString(reply.text);
        _hint->setText(_error);
        if (error)
            *error = _error;
        return false;
    }
    _error.clear();
    Sent s;
    s.id = reply.id;
    s.conversation = reply.body;
    s.text = QString::fromStdString(reply.text);
    const QString state = StateName(reply.body);
    s.finished = state == QLatin1String("done") || state == QLatin1String("failed");
    _sent.push_back(std::move(s));
    if (_sent.size() > static_cast<size_t>(kMaxRows))
        _sent.erase(_sent.begin());
    showRows();
    return true;
}

void GsSendPane::showRows()
{
    _table->setRowCount(static_cast<int>(_sent.size()));
    int row = 0;
    for (auto it = _sent.rbegin(); it != _sent.rend(); ++it, ++row)
    {
        const QString state = StateName(it->conversation);
        const QString cells[] = {QString::number(it->id), state.isEmpty() ? tr("gone") : state, it->text};
        for (int col = 0; col < 3; col++)
        {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setToolTip(cells[2]);
            if (state == QLatin1String("failed"))
                item->setBackground(QColor(220, 60, 60, 70));
            _table->setItem(row, col, item);
        }
    }
}

void GsSendPane::setParam(int index, const QString& text)
{
    if (index >= 0 && index < static_cast<int>(_params.size()))
        _params[static_cast<size_t>(index)]->setText(text);
    else if (_more && index >= static_cast<int>(_params.size()))
        _more->setText(text);
}

void GsSendPane::setReplies(const QString& text)
{
    if (_replies)
        _replies->setText(text);
}

QString GsSendPane::status() const
{
    return _status->isHidden() ? QString() : _status->text();
}

QString GsSendPane::paramLabel(int index) const
{
    if (index < 0 || index >= static_cast<int>(_params.size()))
        return {};
    const auto* label = qobject_cast<QLabel*>(_formLayout->labelForField(_params[static_cast<size_t>(index)]));
    return label ? label->text() : QString();
}

QString GsSendPane::noteText() const
{
    return _note->text();
}

QString GsSendPane::rowsText() const
{
    QStringList lines;
    for (int r = 0; r < _table->rowCount(); r++)
        lines << _table->item(r, 1)->text() + QLatin1Char(' ') + _table->item(r, 2)->text();
    return lines.join(QLatin1Char('\n'));
}
