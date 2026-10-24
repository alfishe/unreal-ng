#include "learnwizard.h"

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#ifdef ENABLE_CONTROLLERS
using namespace controllers;

namespace
{
QString Text(const JsonValue& body, const char* key)
{
    const JsonValue* value = body.Find(key);
    return value != nullptr && value->IsString() ? QString::fromStdString(value->AsString()) : QString();
}

int Number(const JsonValue& body, const char* key)
{
    const JsonValue* value = body.Find(key);
    return value != nullptr && value->IsNumber() ? static_cast<int>(value->AsNumber()) : 0;
}

bool Flag(const JsonValue& body, const char* key)
{
    const JsonValue* value = body.Find(key);
    return value != nullptr && value->IsBool() && value->AsBool();
}
}  // namespace

LearnWizard::LearnWizard(const std::string& device, const QString& name, Call call, QWidget* parent)
    : QDialog(parent), _device(device), _call(std::move(call))
{
    setWindowTitle(tr("Learn %1").arg(name));
    setModal(false);
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Press each control on the pad when it is asked for. Until you finish or cancel, the "
                                "pad's controls answer here and drive nothing else."),
                             this);
    intro->setWordWrap(true);
    root->addWidget(intro);
    _progress = new QLabel(this);
    root->addWidget(_progress);
    _prompt = new QLabel(this);
    QFont big = _prompt->font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    big.setBold(true);
    _prompt->setFont(big);
    _prompt->setMinimumHeight(40);
    root->addWidget(_prompt);
    _release = new QLabel(tr("Release it first"), this);
    _release->setStyleSheet(QStringLiteral("color: #c08000;"));
    root->addWidget(_release);
    _map = new QListWidget(this);
    _map->setToolTip(tr("What each control was learned as (the pad's raw control)"));
    root->addWidget(_map, 1);

    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:"), this));
    _name = new QLineEdit(name, this);
    nameRow->addWidget(_name, 1);
    _save = new QCheckBox(tr("Save as my device"), this);
    _save->setChecked(true);
    _save->setToolTip(
        tr("Saved into devices.user.json by the pad's USB ids: it gets this map whenever it connects. Off: for this "
           "connection only"));
    nameRow->addWidget(_save);
    root->addLayout(nameRow);
    _export = new QLineEdit(this);
    _export->setReadOnly(true);
    _export->setPlaceholderText(tr("The gamecontrollerdb.txt line appears here after Finish"));
    _export->setToolTip(tr("The map as a gamecontrollerdb.txt line: copy it to share the pad's mapping"));
    root->addWidget(_export);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    root->addWidget(_status);

    auto* buttons = new QHBoxLayout();
    _back = new QPushButton(tr("Back"), this);
    _back->setToolTip(tr("Ask for the previous control again"));
    _skip = new QPushButton(tr("Skip"), this);
    _skip->setToolTip(tr("The pad has no such control"));
    _finish = new QPushButton(tr("Finish"), this);
    _finish->setToolTip(tr("Use the controls learned so far"));
    _cancel = new QPushButton(tr("Cancel"), this);
    _cancel->setToolTip(tr("Stop without saving anything"));
    buttons->addWidget(_back);
    buttons->addWidget(_skip);
    buttons->addStretch(1);
    buttons->addWidget(_finish);
    buttons->addWidget(_cancel);
    root->addLayout(buttons);
    connect(_back, &QPushButton::clicked, this, &LearnWizard::back);
    connect(_skip, &QPushButton::clicked, this, &LearnWizard::skip);
    connect(_finish, &QPushButton::clicked, this, &LearnWizard::finish);
    connect(_cancel, &QPushButton::clicked, this, &LearnWizard::reject);

    _poll = new QTimer(this);
    _poll->setInterval(33);  // the pad's presses show at once; one status read under the service's map lock
    connect(_poll, &QTimer::timeout, this, &LearnWizard::refresh);
    resize(460, 420);
}

LearnWizard::~LearnWizard()
{
    if (_learning)
        step("cancel");
}

HostReply LearnWizard::step(const std::string& name, std::map<std::string, std::string> options)
{
    options["device"] = _device;
    options["step"] = name;
    return _call({"learn", std::move(options)});
}

bool LearnWizard::start()
{
    // A session a script started is followed as it stands
    HostReply reply = step("status");
    if (!reply.Ok() || !Flag(reply.body, "learning"))
        reply = step("start");
    if (!reply.Ok())
    {
        setStatus(QString::fromStdString(reply.message), true);
        return false;
    }
    _learning = true;
    _export->clear();
    setStatus(QString(), false);
    display(reply);
    _poll->start();
    return true;
}

void LearnWizard::refresh()
{
    if (!_learning)
        return;
    const HostReply reply = step("status");
    if (!reply.Ok() || !Flag(reply.body, "learning"))
    {
        ended(tr("The session ended elsewhere: nothing more is learned here"));
        return;
    }
    display(reply);
}

void LearnWizard::skip()
{
    if (_learning)
        display(step("skip"));
}

void LearnWizard::back()
{
    if (_learning)
        display(step("back"));
}

bool LearnWizard::finish()
{
    if (!_learning)
        return false;
    std::map<std::string, std::string> options;
    if (!_name->text().trimmed().isEmpty())
        options["name"] = _name->text().trimmed().toStdString();
    options["save"] = _save->isChecked() ? "true" : "false";
    const HostReply reply = step("finish", std::move(options));
    const QString message = QString::fromStdString(reply.message);
    if (!reply.Ok())
    {
        setStatus(message, true);  // nothing learned yet: the session goes on
        return false;
    }
    _export->setText(Text(reply.body, "gamecontrollerdb"));
    ended(message);
    setStatus(message, false);
    emit learned(message);
    return true;
}

void LearnWizard::display(const HostReply& reply)
{
    if (!reply.Ok())
    {
        setStatus(QString::fromStdString(reply.message), true);
        return;
    }
    const std::string dump = reply.body.Dump();
    if (dump == _shown)
        return;
    _shown = dump;
    const int count = Number(reply.body, "count");
    const bool done = Flag(reply.body, "done");
    _progress->setText(done ? tr("All %1 controls asked").arg(count)
                            : tr("Control %1 of %2").arg(Number(reply.body, "index") + 1).arg(count));
    _prompt->setText(Text(reply.body, "prompt"));
    _release->setVisible(Flag(reply.body, "waiting_for_release"));
    _back->setEnabled(Number(reply.body, "index") > 0);
    _skip->setEnabled(!done);
    // The map in the order asked
    _map->clear();
    const JsonValue* map = reply.body.Find("map");
    if (const JsonValue* controls = reply.body.Find("controls"); controls != nullptr && map != nullptr)
        for (const JsonValue& control : controls->Items())
            if (const JsonValue* raw = map->Find(control.AsString()); raw != nullptr)
                _map->addItem(QString::fromStdString(control.AsString() + " = " + raw->AsString()));
    _finish->setEnabled(_map->count() > 0);
}
#else
LearnWizard::~LearnWizard() = default;
bool LearnWizard::start()
{
    return false;
}
void LearnWizard::refresh() {}
bool LearnWizard::finish()
{
    return false;
}
void LearnWizard::skip() {}
void LearnWizard::back() {}
#endif

void LearnWizard::reject()
{
#ifdef ENABLE_CONTROLLERS
    if (_learning)
    {
        step("cancel");
        ended(tr("Cancelled: nothing saved"));
    }
#endif
    QDialog::reject();
}

void LearnWizard::ended(const QString& text)
{
    _learning = false;
    if (_poll != nullptr)
        _poll->stop();
    for (QPushButton* button : {_back, _skip, _finish})
        if (button != nullptr)
            button->setEnabled(false);
    if (_cancel != nullptr)
        _cancel->setText(tr("Close"));
    setStatus(text, false);
}

void LearnWizard::setStatus(const QString& text, bool error)
{
    if (_status == nullptr)
        return;
    _status->setStyleSheet(error ? QStringLiteral("color: #c0504d;") : QString());
    _status->setText(text);
}

QString LearnWizard::promptText() const
{
    return _prompt != nullptr ? _prompt->text() : QString();
}

QString LearnWizard::progressText() const
{
    return _progress != nullptr ? _progress->text() : QString();
}

QString LearnWizard::statusText() const
{
    return _status != nullptr ? _status->text() : QString();
}

QStringList LearnWizard::mapLines() const
{
    QStringList lines;
    if (_map != nullptr)
        for (int row = 0; row < _map->count(); row++)
            lines << _map->item(row)->text();
    return lines;
}

QString LearnWizard::exportLine() const
{
    return _export != nullptr ? _export->text() : QString();
}
