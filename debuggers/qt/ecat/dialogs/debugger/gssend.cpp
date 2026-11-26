// unreal-debugger (not in ecat): the GS send tab (gui-card-debugger A5.3 "Send console", C11; gs-debugger P5.6)

#include "gssend.h"

#include <QBrush>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdio>

#include "addressmenu.h"
#include "debugstyle.h"

namespace
{
QString hex2(int64_t v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X", (unsigned int)(v & 0xFF));
    return QString(b);
}

int64_t num(const QJsonObject &o, const char * key, int64_t def = 0)
{
    const QJsonValue v = o.value(key);
    if (v.isBool()) return v.toBool() ? 1 : 0;
    return v.isDouble() ? (int64_t)v.toDouble() : def;
}

//The range a parameter of `kind` takes, for its field's hint
QString kind_hint(const QString &kind)
{
    if (kind == "volume") return QObject::tr("volume #00-#40");
    if (kind == "note") return QObject::tr("note 0-95");
    if (kind == "channel") return QObject::tr("channel number");
    if (kind == "channel_mask") return QObject::tr("a bit per channel");
    if (kind == "handle") return QObject::tr("handle (0: the current one)");
    if (kind == "port") return QObject::tr("card port");
    if (kind == "low" || kind == "middle" || kind == "high") return QObject::tr("%1 byte").arg(kind);
    if (kind == "mode") return QObject::tr("mode / format code");
    return QObject::tr("byte");
}
}

GsSendView::GsSendView(Emulator * e, QWidget *parent):
    GsCardTab(e, "card:gs-send", parent)
{
    m_layout->addWidget(m_line);
    m_line->hide();

    m_body = new QWidget(this);
    m_body->setObjectName("gssend:body");
    QVBoxLayout * body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    m_layout->addWidget(m_body, 1);

    //1. The command: the profile's set, or a byte typed
    QHBoxLayout * top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(new QLabel(tr("Command"), m_body));
    m_picker = new QComboBox(m_body);
    m_picker->setObjectName("gssend:command");
    m_picker->setEditable(true);
    m_picker->setInsertPolicy(QComboBox::NoInsert);
    m_picker->setToolTip(tr("A command of the card's firmware profile, or any byte: #NN, $NN, 0xNN or decimal"));
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_picker->setMinimumContentsLength(24);
    top->addWidget(m_picker, 1);
    body->addLayout(top);
    connect(m_picker, &QComboBox::editTextChanged, this, [this]() {
        //A byte typed: its parameters when the profile names it; the form is rebuilt only when the command changes
        const QJsonObject c = current();
        const int command = c.isEmpty() ? -1 : (int)num(c, "command");
        if (command != m_form_command) rebuild_form();
    });

    //2. The parameters, the replies, the note
    m_form = new QWidget(m_body);
    m_form_layout = new QFormLayout(m_form);
    m_form_layout->setContentsMargins(0, 0, 0, 0);
    m_form_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    body->addWidget(m_form);
    m_note = new QLabel(m_body);
    m_note->setObjectName("gssend:note");
    m_note->setWordWrap(true);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_note->setStyleSheet("QLabel { color: palette(placeholder-text); }");
    body->addWidget(m_note);

    //3. Send, the hint beside it (paused, a refusal)
    QHBoxLayout * actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    m_send = new QPushButton(tr("Send"), m_body);
    m_send->setObjectName("gssend:send");
    m_send->setToolTip(tr("Send through the card's mailbox as the host would (gs send): the command log marks the bytes "
                          "automation, TTD records them"));
    connect(m_send, &QPushButton::clicked, this, [this]() { send(); });
    actions->addWidget(m_send, 0, Qt::AlignTop);
    m_hint = new QLabel(m_body);
    m_hint->setObjectName("gssend:hint");
    m_hint->setWordWrap(true);
    actions->addWidget(m_hint, 1);
    body->addLayout(actions);

    //4. The sends, newest first
    m_table = make_table("gssend:table", QStringList() << tr("#") << tr("State") << tr("Command") + " " + QChar(0x2192) + " " + tr("replies"));
    m_table->setParent(m_body);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColId, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColState, QHeaderView::ResizeToContents);
    body->addWidget(m_table, 1);

    rebuild_form();
}

std::optional<int> GsSendView::parse_byte(const QString &text)
{
    QString t = text.trimmed();
    if (t.isEmpty()) return std::nullopt;
    int base = 10;
    if (t.startsWith('#') || t.startsWith('$')) {
        t = t.mid(1);
        base = 16;
    } else if (t.startsWith("0x", Qt::CaseInsensitive)) {
        t = t.mid(2);
        base = 16;
    }
    bool ok = false;
    const int value = t.toInt(&ok, base);
    if (!ok || value < 0 || value > 0xFF) return std::nullopt;
    return value;
}

void GsSendView::restart()
{
    //Another card: its own command set, none of the last card's sends
    m_profile.clear();
    m_loaded = false;
    m_sent.clear();
    m_error.clear();
    m_hint->clear();
    show_rows();
}

void GsSendView::show_unavailable(const QString &why)
{
    m_line->setText(why);
    m_line->show();
    m_body->hide();
}

void GsSendView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    if (m_card == "ms") {
        show_unavailable(tr("The ZX-MultiSound's card has no send: the send addresses the GS slot's card (gs send)"));
        return;
    }
    //The command set of the profile decoding the card now (an upload's, else the ROM's): reloaded when it changes
    const Emulator::GsJson commands = e->gs_commands(m_card);
    if (!commands.ok) {
        show_unavailable(tr("No send: %1").arg(QString::fromStdString(commands.message)));
        return;
    }
    m_line->hide();
    m_body->show();
    const QJsonObject body = QJsonDocument::fromJson(QByteArray::fromStdString(commands.json)).object();
    const std::string profile = body.value("profile").toString().toStdString();
    if (!m_loaded || profile != m_profile) {
        m_loaded = true;
        m_profile = profile;
        load_commands(body.value("commands").toArray());
    }

    //The sends not finished: their state now
    for (Sent &s : m_sent)
        if (!s.finished) take(s, e->gs_conversation(m_card, s.id));
    const bool paused = !e->debug_online() || (e->debug_paused() && !e->debug_stepping());
    if (m_error.isEmpty()) m_hint->setText(paused ? tr("The machine is paused: a send waits in the queue until it runs") : QString());
    show_rows();
}

void GsSendView::take(Sent &s, const Emulator::GsJson &answer)
{
    if (!answer.ok) {
        //The driver no longer has it (it keeps its last 16), or the card went
        s.finished = true;
        s.state = tr("gone");
        if (!answer.message.empty()) s.text = QString::fromStdString(answer.message);
        return;
    }
    const QJsonObject c = QJsonDocument::fromJson(QByteArray::fromStdString(answer.json)).object();
    s.state = c.value("state").toString();
    s.text = c.value("text").toString();
    s.finished = s.state == "done" || s.state == "failed";
}

void GsSendView::load_commands(const QJsonArray &commands)
{
    const QSignalBlocker block(m_picker);
    const QJsonObject was = current();
    m_commands.clear();
    m_picker->clear();
    for (const QJsonValue &v : commands) {
        const QJsonObject c = v.toObject();
        m_commands.push_back(c);
        m_picker->addItem(hex2(num(c, "command")) + " " + c.value("name").toString(), (int)num(c, "command"));
    }
    if (!was.isEmpty()) select_command((int)num(was, "command"));
    rebuild_form();
}

QJsonObject GsSendView::current() const
{
    //The picker's text: an entry ("#2A Set module master volume") or a byte typed; its first word is the byte
    const std::optional<int> command = parse_byte(m_picker->currentText().section(' ', 0, 0));
    if (!command) return QJsonObject();
    for (const QJsonObject &c : m_commands)
        if (num(c, "command") == *command) return c;
    return QJsonObject();
}

void GsSendView::select_command(int command)
{
    const int index = m_picker->findData(command);
    if (index >= 0) {
        m_picker->setCurrentIndex(index);
        m_picker->setEditText(m_picker->itemText(index));
    } else {
        m_picker->setEditText(hex2(command));
    }
    rebuild_form();
}

void GsSendView::rebuild_form()
{
    while (m_form_layout->rowCount() > 0) m_form_layout->removeRow(0);
    m_params.clear();
    m_more = nullptr;
    m_replies = nullptr;

    const QJsonObject c = current();
    const bool named = !c.isEmpty();
    m_form_command = named ? (int)num(c, "command") : -1;
    const int pre = named ? (int)num(c, "pre") : 0;
    const QJsonArray params = c.value("params").toArray();
    for (int i = 0; i < params.size(); i++) {
        const QJsonObject p = params[i].toObject();
        const QString name = p.value("name").toString().isEmpty() ? tr("param %1").arg(i + 1) : p.value("name").toString();
        QLineEdit * field = new QLineEdit(m_form);
        field->setObjectName(QString("gssend:param%1").arg(i));
        field->setPlaceholderText(kind_hint(p.value("kind").toString()));
        m_form_layout->addRow(i < pre ? tr("%1 (before)").arg(name) : name, field);
        m_params.push_back(field);
    }
    if (!named || num(c, "post") < 0) {
        //A byte the profile does not name, or a variable count: the bytes after the command in one field
        m_more = new QLineEdit(m_form);
        m_more->setObjectName("gssend:more");
        m_more->setPlaceholderText(tr("bytes after the command, space separated"));
        m_form_layout->addRow(named ? tr("More bytes") : tr("Bytes"), m_more);
    }
    m_replies = new QLineEdit(m_form);
    m_replies->setObjectName("gssend:replies");
    const int64_t reply = named ? num(c, "reply") : 0;
    m_replies->setPlaceholderText(tr("%1 (the profile's)").arg(reply));
    m_form_layout->addRow(tr("Replies"), m_replies);

    QString note;
    if (named) {
        if (reply > 0 && !c.value("reply_meaning").toString().isEmpty())
            note += tr("Replies: %1. ").arg(c.value("reply_meaning").toString());
        if (!c.value("transfer").toString().isEmpty())
            note += tr("Starts a transfer (%1): the bytes after it are not sent here. ").arg(c.value("transfer").toString());
        if (!c.value("note").toString().isEmpty()) note += c.value("note").toString();
        if (!num(c, "documented")) note += (note.isEmpty() ? QString() : QString(" ")) + tr("(not in the programming guide)");
    } else if (!m_commands.empty() || !m_profile.empty()) {
        note = tr("The profile does not name this command: its bytes and replies are yours to give");
    }
    note = note.trimmed();
    m_note->setText(note);
    m_note->setVisible(!note.isEmpty());
}

bool GsSendView::request(Request &out, QString &error) const
{
    out = Request();
    const QJsonObject c = current();
    const std::optional<int> command = !c.isEmpty() ? std::optional<int>((int)num(c, "command"))
                                                    : parse_byte(m_picker->currentText().section(' ', 0, 0));
    if (!command) {
        error = tr("Command: not a byte (#NN, $NN, 0xNN or decimal)");
        return false;
    }
    out.command = *command;
    const int pre = !c.isEmpty() ? (int)num(c, "pre") : 0;
    for (size_t i = 0; i < m_params.size(); i++) {
        const std::optional<int> value = parse_byte(m_params[i]->text());
        if (!value) {
            error = tr("%1: not a byte").arg(param_label((int)i));
            return false;
        }
        if ((int)i < pre) out.pre = *value;
        else out.params.push_back(*value);
    }
    if (m_more != nullptr)
        for (const QString &part : m_more->text().split(' ', Qt::SkipEmptyParts)) {
            const std::optional<int> value = parse_byte(part);
            if (!value) {
                error = tr("Bytes: '%1' is not a byte").arg(part);
                return false;
            }
            out.params.push_back(*value);
        }
    if (m_replies != nullptr && !m_replies->text().trimmed().isEmpty()) {
        bool ok = false;
        const int replies = m_replies->text().trimmed().toInt(&ok);
        if (!ok || replies < 0 || replies > 255) {
            error = tr("Replies: a count 0-255");
            return false;
        }
        out.replies = replies;
    }
    return true;
}

bool GsSendView::send(QString * error)
{
    auto refuse = [this, error](const QString &why) {
        m_error = why;
        m_hint->setText(tr("refused: %1").arg(why));
        if (error != nullptr) *error = why;
        return false;
    };
    if (m_card.empty()) return refuse(tr("no General Sound card"));
    if (AddressRouter::read_only_at(this)) return refuse(tr("the window shows the past: go back to the present to send"));
    Request r;
    QString why;
    if (!request(r, why)) return refuse(why);
    //Never waits here: the emulation runs the steps; refresh reads how far it got
    const Emulator::GsJson answer = e->gs_send(m_card, r.command, r.pre, r.params, r.replies);
    if (!answer.ok) return refuse(QString::fromStdString(answer.message));
    m_error.clear();
    Sent s;
    s.id = (uint32_t)num(QJsonDocument::fromJson(QByteArray::fromStdString(answer.json)).object(), "id");
    take(s, answer);
    m_sent.push_back(std::move(s));
    if (m_sent.size() > (size_t)kMaxRows) m_sent.erase(m_sent.begin());
    refresh();
    return true;
}

void GsSendView::show_rows()
{
    const dbg::Colors c = dbg::colors(palette());
    QColor tint = c.breakpoint;
    tint.setAlpha(50);
    m_table->setRowCount((int)m_sent.size());
    int row = 0;
    for (auto it = m_sent.rbegin(); it != m_sent.rend(); ++it, ++row) {
        const QString cells[ColCount] = {QString::number(it->id), it->state, it->text};
        const bool failed = it->state == "failed";
        for (int k = 0; k < ColCount; k++) {
            QTableWidgetItem * item = new QTableWidgetItem(cells[k]);
            item->setToolTip(it->text);
            if (k == ColId) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (failed) {
                item->setForeground(QBrush(c.breakpoint));
                item->setBackground(QBrush(tint));
            }
            m_table->setItem(row, k, item);
        }
    }
}

void GsSendView::set_param(int index, const QString &text)
{
    if (index >= 0 && index < (int)m_params.size()) m_params[(size_t)index]->setText(text);
    else if (m_more != nullptr && index >= (int)m_params.size()) m_more->setText(text);
}

void GsSendView::set_replies(const QString &text)
{
    if (m_replies != nullptr) m_replies->setText(text);
}

QString GsSendView::param_label(int index) const
{
    if (index < 0 || index >= (int)m_params.size()) return QString();
    const QLabel * label = qobject_cast<QLabel *>(m_form_layout->labelForField(m_params[(size_t)index]));
    return label != nullptr ? label->text() : tr("parameter %1").arg(index + 1);
}

QString GsSendView::newest_state() const
{
    return m_sent.empty() ? QString() : m_sent.back().state;
}
