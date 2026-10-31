#include "streamdeckpage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <thread>

#include "emulator/companioncontroller.h"
#include "emulator/companions/streamdeckapi.h"
#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/execution/companionregistry.h"

#include "platform/windowlesssession.h"
#ifdef ENABLE_CONTROLLERS
#include "usbpresence.h"
#endif

using companion::streamdeck::Supervisor;

namespace
{
QSettings Store()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
}

QString Text(const StateNode* node)
{
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return {};
    switch (node->kind)
    {
        case StateNode::Kind::String: return QString::fromStdString(node->s);
        case StateNode::Kind::Int: return QString::number(node->i);
        case StateNode::Kind::Double: return QString::number(node->d);
        case StateNode::Kind::Bool: return node->b ? QStringLiteral("yes") : QStringLiteral("no");
        default: return {};
    }
}

bool Flag(const StateNode* node)
{
    return node != nullptr && ((node->kind == StateNode::Kind::Bool && node->b) ||
                               (node->kind == StateNode::Kind::Int && node->i != 0));
}

StateNode ArgsOf(const QVariantMap& map)
{
    StateNode args = StateNode::Object();
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        if (it.value().typeId() == QMetaType::Int || it.value().typeId() == QMetaType::LongLong)
            args[it.key().toStdString()] = static_cast<int64_t>(it.value().toLongLong());
        else
            args[it.key().toStdString()] = it.value().toString().toStdString();
    }
    return args;
}

/// A state for people: "running (ours)", "adopted (started by Stream Deck Studio or by hand)", ...
QString StateText(const StateNode& status)
{
    const QString state = Text(status.find("state"));
    if (state == QLatin1String("running"))
        return QObject::tr("Running");
    if (state == QLatin1String("adopted"))
        return QObject::tr("Running (started by Stream Deck Studio or by hand; unreal-qt holds a lease)");
    if (state == QLatin1String("starting"))
        return QObject::tr("Starting...");
    if (state == QLatin1String("waiting"))
        return QObject::tr("Waiting for a Stream Deck");
    if (state == QLatin1String("off"))
        return QObject::tr("Off (not started by unreal-qt)");
    if (state == QLatin1String("stopped"))
        return QObject::tr("Stopped");
    if (state == QLatin1String("backoff"))
        return QObject::tr("Ended: starting it again in %1 s").arg(Text(status.find("restart_in_ms")).toInt() / 1000.0,
                                                                      0, 'f', 1);
    if (state == QLatin1String("failed"))
        return QObject::tr("Failed: trying again in %1 s").arg(Text(status.find("retry_in_ms")).toInt() / 1000);
    if (state == QLatin1String("missing"))
        return QObject::tr("Not in this build: no unreal-streamdeck beside unreal-qt");
    if (state == QLatin1String("foreign"))
        return QObject::tr("Another emulator's daemon uses the port");
    if (state == QLatin1String("detached"))
        return QObject::tr("No supervisor");
    return state;
}
}  // namespace

// ---------------------------------------------------------------- StreamDeckSupervision

QString StreamDeckSupervision::DefaultBinary()
{
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("unreal-streamdeck.exe");
#else
    const QString exe = QStringLiteral("unreal-streamdeck");
#endif
    const QDir dir(QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath());
    // beside us (UnrealNG.app/Contents/MacOS, the Windows ZIP, the AppImage's usr/bin, the DEB's lib/unreal-ng);
    // beside our bundle (a build tree: bin/unreal-qt.app/Contents/MacOS -> bin/)
    for (const QString& rel : {QStringLiteral("."), QStringLiteral("../../..")})
        if (const QString path = QDir::cleanPath(dir.filePath(rel + QLatin1Char('/') + exe)); QFileInfo(path).isExecutable())
            return path;
    return {};
}

QString StreamDeckSupervision::LogFile()
{
    const QString folder = QFileInfo(Store().fileName()).absolutePath() + QStringLiteral("/streamdeck");
    QDir().mkpath(folder);
    return folder + QStringLiteral("/daemon.log");
}

StreamDeckSupervision::StreamDeckSupervision(QObject* parent, bool attach) : QObject(parent)
{
    if (!attach)
        return;
    using namespace companion::streamdeck;
    Settings s;
    {
        QSettings store = Store();
        store.beginGroup(QStringLiteral("StreamDeck"));
        ModeFromName(store.value(QStringLiteral("mode"), QStringLiteral("deck")).toString().toStdString(), s.mode);
        const QString activity = store.value(QStringLiteral("activity"), QStringLiteral("flash")).toString();
        if (activity == QLatin1String("off") || activity == QLatin1String("flash") || activity == QLatin1String("line"))
            s.activity = activity.toStdString();
        s.apiPort = store.value(QStringLiteral("apiPort"), s.apiPort).toInt();
        s.viewersPort = store.value(QStringLiteral("viewersPort"), s.viewersPort).toInt();
        s.binary = store.value(QStringLiteral("binary")).toString().toStdString();
        store.beginGroup(QStringLiteral("brightness"));
        for (const QString& serial : store.childKeys())
            s.brightness[serial.toStdString()] = std::clamp(store.value(serial).toInt(), 0, 100);
        store.endGroup();
        store.beginGroup(QStringLiteral("pack"));  // the icon pack per deck (its scenario's when absent)
        for (const QString& serial : store.childKeys())
            if (const QString pack = store.value(serial).toString(); !pack.isEmpty())
                s.pack[serial.toStdString()] = pack.toStdString();
    }
    // UNREAL_STREAMDECK_SUPERVISOR=off, or a windowless session (agents, tests; UNREAL_WINDOWLESS_DEVICES=streamdeck
    // lets one): this run starts no daemon whatever the setting; the setting itself is kept
    if (qEnvironmentVariable("UNREAL_STREAMDECK_SUPERVISOR") == QLatin1String("off") ||
        !WindowlessSessionUses("streamdeck"))
        s.mode = Mode::Off;
    Host host;
    host.webapiPort = 8090;
    if (const int port = qEnvironmentVariableIntValue("UNREAL_WEBAPI_PORT"); port >= 1 && port <= 65535)
        host.webapiPort = port;
    host.ownPid = static_cast<int>(QCoreApplication::applicationPid());
    host.defaultBinary = DefaultBinary().toStdString();
    host.logFile = LogFile().toStdString();
#ifdef ENABLE_CONTROLLERS
    host.decksAttached = [] { return controllers::CountUsbDevices(controllers::kElgatoVendorId); };
#endif
    Supervisor::Instance().SetSettingsListener([](const Settings& next) {
        // from any thread (a plane changed it): QSettings instances are per call
        QSettings store = Store();
        store.beginGroup(QStringLiteral("StreamDeck"));
        store.setValue(QStringLiteral("mode"), QString::fromLatin1(ModeName(next.mode)));
        store.setValue(QStringLiteral("activity"), QString::fromStdString(next.activity));
        store.setValue(QStringLiteral("apiPort"), next.apiPort);
        store.setValue(QStringLiteral("viewersPort"), next.viewersPort);
        store.setValue(QStringLiteral("binary"), QString::fromStdString(next.binary));
        store.remove(QStringLiteral("brightness"));
        store.beginGroup(QStringLiteral("brightness"));
        for (const auto& [serial, value] : next.brightness)
            store.setValue(QString::fromStdString(serial), value);
        store.endGroup();
        store.remove(QStringLiteral("pack"));
        store.beginGroup(QStringLiteral("pack"));
        for (const auto& [serial, value] : next.pack)
            store.setValue(QString::fromStdString(serial), QString::fromStdString(value));
    });
    Supervisor::Instance().Attach(host, s);
    _attached = true;
}

StreamDeckSupervision::~StreamDeckSupervision()
{
    if (!_attached)
        return;
    Supervisor::Instance().SetSettingsListener(nullptr);
    Supervisor::Instance().Detach();  // the lease goes back: the daemon ends unless the studio still holds it
}

// ---------------------------------------------------------------- StreamDeckDialog

StreamDeckDialog::StreamDeckDialog(CompanionController* companions, QWidget* parent)
    : QWidget(parent, Qt::Window), _companions(companions)
{
    setWindowTitle(tr("Stream Deck"));
    resize(640, 640);
    auto* root = new QVBoxLayout(this);

    // The daemon
    auto* daemonBox = new QGroupBox(tr("Stream Deck daemon (unreal-streamdeck)"), this);
    auto* daemonLayout = new QVBoxLayout(daemonBox);
    _state = new QLabel(daemonBox);
    _state->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _details = new QLabel(daemonBox);
    _details->setWordWrap(true);
    _details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    daemonLayout->addWidget(_state);
    daemonLayout->addWidget(_details);
    auto* buttons = new QHBoxLayout();
    _start = new QPushButton(tr("Start"), daemonBox);
    _stop = new QPushButton(tr("Stop"), daemonBox);
    _restart = new QPushButton(tr("Restart"), daemonBox);
    _start->setToolTip(tr("Start it now (or take over the one running), whatever the setting below"));
    _stop->setToolTip(tr("Stop it; it is not started again until Start"));
    buttons->addWidget(_start);
    buttons->addWidget(_stop);
    buttons->addWidget(_restart);
    buttons->addStretch(1);
    daemonLayout->addLayout(buttons);
    auto* form = new QFormLayout();
    _mode = new QComboBox(daemonBox);
    _mode->addItem(tr("When a Stream Deck is attached"), QStringLiteral("deck"));
    _mode->addItem(tr("Always (software decks without one)"), QStringLiteral("always"));
    _mode->addItem(tr("Never (a daemon started by hand still works)"), QStringLiteral("off"));
    form->addRow(tr("Start it:"), _mode);
    _activity = new QComboBox(daemonBox);
    _activity->addItem(tr("Flash the key"), QStringLiteral("flash"));
    _activity->addItem(tr("Flash the key and a line on the strip"), QStringLiteral("line"));
    _activity->addItem(tr("Off"), QStringLiteral("off"));
    _activity->setToolTip(tr("How a deck shows an action another surface ran (a pad, the Surface Dial, a menu, a "
                             "script); a time-travel scrub never flashes"));
    form->addRow(tr("Activity of other surfaces:"), _activity);
    daemonLayout->addLayout(form);
    root->addWidget(daemonBox);

    // Pairing
    auto* pairBox = new QGroupBox(tr("Companion pairing"), this);
    auto* pairLayout = new QVBoxLayout(pairBox);
    _pairing = new QLabel(pairBox);
    _pairing->setWordWrap(true);
    pairLayout->addWidget(_pairing);
    auto* pairButtons = new QHBoxLayout();
    _pair = new QPushButton(tr("Pair"), pairBox);
    _pair->setToolTip(tr("The daemon asks to pair; its deck shows the code, approve it here"));
    _approve = new QPushButton(tr("Approve"), pairBox);
    _approve->setToolTip(tr("Allow the daemon's pairing (compare the code with the deck's strip)"));
    _revoke = new QPushButton(tr("Revoke"), pairBox);
    _revoke->setToolTip(tr("The daemon's token stops working; the deck offers to pair again"));
    pairButtons->addWidget(_pair);
    pairButtons->addWidget(_approve);
    pairButtons->addWidget(_revoke);
    pairButtons->addStretch(1);
    pairLayout->addLayout(pairButtons);
    root->addWidget(pairBox);

    // Decks
    auto* decksBox = new QGroupBox(tr("Decks"), this);
    auto* decksLayout = new QVBoxLayout(decksBox);
    _decks = new QTableWidget(0, 5, decksBox);
    _decks->setHorizontalHeaderLabels({tr("Deck"), tr("Model"), tr("Kind"), tr("Icon pack"), tr("Brightness")});
    _decks->horizontalHeader()->setStretchLastSection(true);
    _decks->verticalHeader()->hide();
    _decks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _decks->setMinimumHeight(110);
    decksLayout->addWidget(_decks);
    auto* surfacesRow = new QHBoxLayout();
    auto* surfacesNote = new QLabel(tr("Which window or machine each deck drives: the surfaces table"), decksBox);
    auto* surfaces = new QPushButton(tr("Surfaces..."), decksBox);
    surfacesRow->addWidget(surfacesNote, 1);
    surfacesRow->addWidget(surfaces);
    decksLayout->addLayout(surfacesRow);
    root->addWidget(decksBox, 1);

#ifdef Q_OS_LINUX
    // An AppImage cannot install the udev rule (packaging.md §5): the text to install by hand
    auto* udevBox = new QGroupBox(qEnvironmentVariableIsSet("APPIMAGE")
                                      ? tr("Linux: the udev rule (an AppImage cannot install it)")
                                      : tr("Linux: the udev rule (the DEB installs it)"),
                                  this);
    auto* udevLayout = new QVBoxLayout(udevBox);
    _udev = new QPlainTextEdit(udevBox);
    _udev->setReadOnly(true);
    _udev->setPlainText(QString::fromUtf8(companion::streamdeck::UdevRuleText()));
    _udev->setMaximumHeight(150);
    udevLayout->addWidget(_udev);
    auto* copy = new QPushButton(tr("Copy"), udevBox);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(_udev->toPlainText()); });
    udevLayout->addWidget(copy, 0, Qt::AlignRight);
    root->addWidget(udevBox);
#endif

    _message = new QLabel(this);
    _message->setWordWrap(true);
    root->addWidget(_message);

    auto run = [this](const char* verb) {
        const companion::streamdeck::Reply reply = companion::streamdeck::Call(verb, StateNode::Object());
        _message->setText(reply.Ok() ? QString() : Text(reply.body.find("message")));
        refresh();
    };
    connect(_start, &QPushButton::clicked, this, [run] { run("start"); });
    connect(_stop, &QPushButton::clicked, this, [run] { run("stop"); });
    connect(_restart, &QPushButton::clicked, this, [run] { run("restart"); });
    auto setting = [this](const char* name, const QString& value) {
        StateNode args = StateNode::Object();
        args[name] = value.toStdString();
        const companion::streamdeck::Reply reply = companion::streamdeck::Call("settings", args);
        _message->setText(reply.Ok() ? QString() : Text(reply.body.find("message")));
    };
    connect(_mode, &QComboBox::activated, this, [this, setting] { setting("mode", _mode->currentData().toString()); });
    connect(_activity, &QComboBox::activated, this,
            [this, setting] { setting("activity", _activity->currentData().toString()); });
    connect(_pair, &QPushButton::clicked, this, [this] { callAsync(QStringLiteral("pair"), {}); });
    connect(_approve, &QPushButton::clicked, this, &StreamDeckDialog::approve);
    connect(_revoke, &QPushButton::clicked, this, [run] { run("revoke"); });
    connect(surfaces, &QPushButton::clicked, this, &StreamDeckDialog::surfacesRequested);

    _timer = new QTimer(this);
    _timer->setInterval(1000);
    connect(_timer, &QTimer::timeout, this, &StreamDeckDialog::refresh);
    refresh();
}

void StreamDeckDialog::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    _timer->start();
}

void StreamDeckDialog::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
}

void StreamDeckDialog::callAsync(const QString& verb, const QVariantMap& args)
{
    _message->setText(tr("%1...").arg(verb));
    QPointer<StreamDeckDialog> self(this);
    std::thread([self, verb = verb.toStdString(), args = ArgsOf(args)] {
        const companion::streamdeck::Reply reply = companion::streamdeck::Call(verb, args);
        const QString text = reply.Ok() ? QString() : Text(reply.body.find("message"));
        QMetaObject::invokeMethod(qApp, [self, text] {
            if (!self)
                return;
            self->_message->setText(text);
            self->refresh();
        });
    }).detach();
}

QComboBox* StreamDeckDialog::packBox(int row) const
{
    return qobject_cast<QComboBox*>(_decks->cellWidget(row, 3));
}

void StreamDeckDialog::loadPacks(const QString& pid)
{
    _packsOf = pid;
    QPointer<StreamDeckDialog> self(this);
    std::thread([self, pid] {
        const companion::streamdeck::Reply reply = companion::streamdeck::Call("packs", StateNode::Object());
        QList<QPair<QString, QString>> packs;
        if (const StateNode* list = reply.Ok() ? reply.body.find("packs") : nullptr)
            for (const StateNode& pack : list->items)
                if (const QString name = Text(pack.find("name")); !name.isEmpty())
                    packs.append({name, Text(pack.find("title"))});
        QMetaObject::invokeMethod(qApp, [self, pid, packs] {
            if (!self || self->_packsOf != pid)
                return;
            self->_packs = packs;
            self->_decksShown.clear();  // the rows again, with the packs
            self->refresh();
        });
    }).detach();
}

void StreamDeckDialog::fillPackBox(QComboBox* box, const QString& setting) const
{
    box->addItem(tr("Its scenario's"), QStringLiteral("scenario"));
    bool listed = setting.isEmpty() || setting == QLatin1String("scenario");
    for (const auto& [name, title] : _packs)
    {
        box->addItem(title.isEmpty() || title == name ? name : QStringLiteral("%1 (%2)").arg(title, name), name);
        listed = listed || name == setting;
    }
    if (!listed)
        box->addItem(setting, setting);  // set elsewhere, not among the packs the daemon listed
}

void StreamDeckDialog::approve()
{
    if (_pendingPairing.isEmpty())
        return;
    // At the GUI: the user compared the code on this page with the deck's strip. The prompt, if open, goes by itself
    // (decided elsewhere)
    const auto info = companion::CompanionRegistry::Instance().ApprovePairing(_pendingPairing.toStdString());
    _message->setText(info ? tr("Approved: the deck connects within two seconds")
                           : tr("The pairing is gone (expired, cancelled or decided)"));
    refresh();
}

void StreamDeckDialog::refresh()
{
    const StateNode status = Supervisor::Instance().Status();
    const QString state = Text(status.find("state"));
    _state->setText(QStringLiteral("<b>%1</b>").arg(StateText(status).toHtmlEscaped()));
    const StateNode* daemon = status.find("daemon");
    const bool up = daemon != nullptr && daemon->isObject();
    QStringList lines;
    if (up)
        lines << tr("pid %1%2, version %3, %4 deck(s), %5 viewer(s), link to unreal-qt: %6")
                     .arg(Text(status.find("pid")),
                          Flag(status.find("own")) ? tr(" (started by unreal-qt)") : QString(),
                          Text(daemon->find("version")), Text(daemon->find("decks")), Text(daemon->find("viewers")),
                          Text(daemon->find("surface")));
    lines << tr("Stream Decks on USB: %1; automation 127.0.0.1:%2, viewers %3; restarts %4")
                 .arg(Text(status.find("deck_attached")), Text(status.find("api_port")),
                      Text(status.find("viewers_port")), Text(status.find("restarts")));
    const QString binary = Text(status.find("binary"));
    lines << (binary.isEmpty() ? tr("unreal-streamdeck: not found beside unreal-qt (UNREAL_STREAMDECK_BIN names one)")
                               : tr("unreal-streamdeck: %1").arg(binary));
    if (const QString log = Text(status.find("log")); !log.isEmpty())
        lines << tr("log: %1").arg(log);
    if (const QString error = Text(status.find("error")); !error.isEmpty())
        lines << tr("<span style='color:#c33'>%1</span>").arg(error.toHtmlEscaped());
    _details->setText(lines.join(QStringLiteral("<br>")));
    const bool attached = state != QLatin1String("detached");
    _start->setEnabled(attached && !up);
    _stop->setEnabled(attached && (up || state == QLatin1String("starting") || state == QLatin1String("backoff")));
    _restart->setEnabled(attached);

    {
        const QSignalBlocker b1(_mode), b2(_activity);
        _mode->setCurrentIndex(std::max(0, _mode->findData(Text(status.find("mode")))));
        _activity->setCurrentIndex(std::max(0, _activity->findData(Text(status.find("activity")))));
    }

    // pairing: the daemon's state, the companion at the registry
    const StateNode* pairing = up ? daemon->find("pairing") : nullptr;
    const QString pairState = pairing ? Text(pairing->find("state")) : QString();
    const QString companionId = pairing ? Text(pairing->find("companion_id")) : QString();
    const auto known = companionId.isEmpty() ? std::nullopt
                                             : companion::CompanionRegistry::Instance().Find(companionId.toStdString());
    _pendingPairing = pairState == QLatin1String("pending") && pairing ? Text(pairing->find("pairing_id")) : QString();
    QString pairText;
    if (!up)
        pairText = tr("The daemon is not running: start it to pair");
    else if (!_pendingPairing.isEmpty())
    {
        const QString code = Text(pairing->find("code"));
        pairText = tr("Waiting for your approval: code <b>%1 %2</b> (the deck's strip shows the same)")
                       .arg(code.left(3), code.mid(3));
    }
    else if (known)
        pairText = tr("Paired as %1 (%2)").arg(QString::fromStdString(known->name), companionId);
    else if (Flag(pairing ? pairing->find("paired") : nullptr))
        pairText = tr("The daemon has a token unreal-qt does not know (revoked, or from another emulator): pair again");
    else
        pairText = tr("Not paired: the deck uses the REST fallback; pair to drive the emulator over one socket");
    if (pairState == QLatin1String("denied") || pairState == QLatin1String("expired") ||
        pairState == QLatin1String("refused") || pairState == QLatin1String("unreachable"))
        pairText += tr(" (last pairing: %1%2)")
                        .arg(pairState, pairing->find("error") ? QStringLiteral(", ") + Text(pairing->find("error"))
                                                               : QString());
    _pairing->setText(pairText);
    _pair->setEnabled(up && _pendingPairing.isEmpty() && pairState != QLatin1String("requesting"));
    _approve->setEnabled(!_pendingPairing.isEmpty());
    _revoke->setEnabled(known.has_value());

    // decks: one row each, an icon pack box, a brightness slider
    if (up && Text(status.find("pid")) != _packsOf)
        loadPacks(Text(status.find("pid")));
    else if (!up && !_packsOf.isEmpty())
    {
        _packsOf.clear();
        _packs.clear();
    }
    const StateNode* decks = status.find("decks");
    QStringList serials;
    if (decks != nullptr)
        for (const StateNode& deck : decks->items)
            serials << Text(deck.find("serial"));
    const QString shown = serials.join(QLatin1Char(','));
    if (shown != _decksShown)
    {
        _decksShown = shown;
        _decks->setRowCount(0);
        if (decks != nullptr)
            for (const StateNode& deck : decks->items)
            {
                const int row = _decks->rowCount();
                _decks->insertRow(row);
                const QString serial = Text(deck.find("serial"));
                _decks->setItem(row, 0, new QTableWidgetItem(serial));
                _decks->setItem(row, 1, new QTableWidgetItem(Text(deck.find("model"))));
                _decks->setItem(row, 2, new QTableWidgetItem(Flag(deck.find("real")) ? tr("USB") : tr("own (studio)")));
                auto* pack = new QComboBox(_decks);
                pack->setObjectName(serial);
                pack->setToolTip(tr("The icon pack the deck draws its glyph keys from; kept for this deck and set "
                                    "again when the daemon starts"));
                fillPackBox(pack, Text(deck.find("pack_setting")));
                pack->setEnabled(deck.find("pack_setting") != nullptr);  // an older daemon has no packs
                connect(pack, &QComboBox::activated, this, [this, pack] {
                    callAsync(QStringLiteral("pack"), {{QStringLiteral("deck"), pack->objectName()},
                                                       {QStringLiteral("pack"), pack->currentData().toString()}});
                });
                _decks->setCellWidget(row, 3, pack);
                auto* slider = new QSlider(Qt::Horizontal, _decks);
                slider->setRange(0, 100);
                slider->setObjectName(serial);
                connect(slider, &QSlider::sliderReleased, this, [this, slider] {
                    callAsync(QStringLiteral("brightness"), {{QStringLiteral("deck"), slider->objectName()},
                                                             {QStringLiteral("value"), slider->value()}});
                });
                _decks->setCellWidget(row, 4, slider);
            }
    }
    if (decks != nullptr)
        for (int row = 0; row < _decks->rowCount() && row < static_cast<int>(decks->items.size()); ++row)
        {
            const StateNode& deck = decks->items[static_cast<size_t>(row)];
            if (auto* slider = qobject_cast<QSlider*>(_decks->cellWidget(row, 4)); slider && !slider->isSliderDown())
            {
                const QSignalBlocker block(slider);
                slider->setValue(Text(deck.find("brightness")).toInt());
            }
            // the deck's setting as the daemon reports it (changed here, in the studio, by an agent)
            if (QComboBox* pack = packBox(row); pack && !pack->view()->isVisible())
            {
                const QString setting = Text(deck.find("pack_setting"));
                const QSignalBlocker block(pack);
                int at = pack->findData(setting.isEmpty() ? QStringLiteral("scenario") : setting);
                if (at < 0)
                {
                    pack->addItem(setting, setting);
                    at = pack->count() - 1;
                }
                pack->setCurrentIndex(at);
            }
        }
}
