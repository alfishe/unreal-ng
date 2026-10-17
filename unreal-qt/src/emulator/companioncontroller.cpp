#include "companioncontroller.h"

#include <algorithm>

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace
{
companion::CompanionRegistry& Registry()
{
    return companion::CompanionRegistry::Instance();
}

/// The user's settings file (QSettings: Unreal / Unreal-NG); "Accept pairing requests" lives there
constexpr const char* kAcceptPairingKey = "companions/acceptPairingRequests";

/// What a scope lets the device do, for the prompt
QString ScopeLabel(const std::string& scope)
{
    if (scope == "run")
        return QObject::tr("Run: resume, reset, NMI, turbo, speed");
    if (scope == "input")
        return QObject::tr("Input: keyboard, joystick, mouse");
    if (scope == "media")
        return QObject::tr("Media: tape, disk, snapshot load");
    if (scope == "machine")
        return QObject::tr("Machine: the model and its configuration (settings, slots, network)");
    if (scope == "rewind")
        return QObject::tr("Rewind: time-travel history");
    if (scope == "takeback")
        return QObject::tr("Take control back from automation");
    if (scope == "debug")
        return QObject::tr("Debug: memory, registers, breakpoints, stepping");
    return QString::fromStdString(scope);
}

QString ScopesText(uint32_t scopes)
{
    QStringList names;
    for (const std::string& name : automation_gate::CompanionScopeNames(scopes))
        names << QString::fromStdString(name);
    return names.isEmpty() ? QObject::tr("none") : names.join(QStringLiteral(", "));
}
}  // namespace

/// region <CompanionController>

CompanionController::CompanionController(QWidget* dialogParent, bool attachStore)
    : QObject(dialogParent), _dialogParent(dialogParent), _attached(attachStore)
{
    if (_attached)
    {
        // Next to the application's own settings file
        const QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
        const QString path = QFileInfo(settings.fileName()).absolutePath() + QStringLiteral("/companions.json");
        Registry().SetStorePath(path.toStdString());
        Registry().SetPairingAllowed(settings.value(kAcceptPairingKey, true).toBool());
        Registry().SetApprovalAvailable(true);
    }
    _observer = MessageCenter::DefaultMessageCenter().AddObserver(NC_COMPANION_CHANGED, [this](int, Message* message) {
        auto* payload = message ? dynamic_cast<CompanionChangedPayload*>(message->obj) : nullptr;
        if (!payload)
            return;
        const QString event = QString::fromStdString(payload->event);
        const QString pairing = QString::fromStdString(payload->pairingId);
        const QString companionId = QString::fromStdString(payload->companionId);
        const QString name = QString::fromStdString(payload->name);
        QMetaObject::invokeMethod(
            this, [this, event, pairing, companionId, name]() { handleEvent(event, pairing, companionId, name); },
            Qt::QueuedConnection);
    });
}

CompanionController::~CompanionController()
{
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_COMPANION_CHANGED, _observer);
    if (_attached)
    {
        Registry().SetApprovalAvailable(false);
        Registry().Flush();
    }
}

void CompanionController::handleEvent(const QString& event, const QString& pairingId, const QString& companionId,
                                      const QString& name)
{
    Q_UNUSED(companionId);
    if (event == QLatin1String("pairing_requested"))
    {
        if (_prompts.value(pairingId) || Registry().CodeOf(pairingId.toStdString()).empty())
            return;  // shown already, or no longer pending
        auto* prompt = new CompanionPairingPrompt(pairingId, _dialogParent);
        _prompts.insert(pairingId, prompt);
        connect(prompt, &QObject::destroyed, this, [this, pairingId]() { _prompts.remove(pairingId); });
        prompt->show();
        prompt->raise();
        prompt->activateWindow();
        return;
    }
    if (event == QLatin1String("pairing_cancelled") || event == QLatin1String("pairing_expired") ||
        event == QLatin1String("paired") || event == QLatin1String("denied"))
    {
        if (CompanionPairingPrompt* prompt = _prompts.value(pairingId))
            prompt->dismiss();
    }
    if (event == QLatin1String("paired"))
        emit toastRequested(tr("Companion paired"),
                            tr("'%1' acts for you now: its actions open no remote control session").arg(name));
    else if (event == QLatin1String("revoked"))
        emit toastRequested(tr("Companion revoked"), tr("'%1' can no longer control the emulator").arg(name));
    if (_dialog)
        _dialog->refresh();
}

void CompanionController::showCompanionsDialog()
{
    if (!_dialog)
    {
        _dialog = new CompanionsDialog(_dialogParent);
        _dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(_dialog, &CompanionsDialog::pairingAllowedChanged, this, [this](bool allowed) {
            if (!_attached)
                return;  // tests: the user's settings stay untouched
            QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
            settings.setValue(kAcceptPairingKey, allowed);
        });
    }
    _dialog->refresh();
    _dialog->show();
    _dialog->raise();
    _dialog->activateWindow();
}

CompanionPairingPrompt* CompanionController::prompt(const QString& pairingId) const
{
    return _prompts.value(pairingId);
}

/// endregion </CompanionController>

/// region <CompanionPairingPrompt>

CompanionPairingPrompt::CompanionPairingPrompt(const QString& pairingId, QWidget* parent)
    : QDialog(parent), _pairingId(pairingId)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setWindowTitle(tr("Pair a companion"));

    companion::PairingInfo info;
    for (const companion::PairingInfo& p : Registry().Pending())
        if (p.id == pairingId.toStdString())
            info = p;
    _code = QString::fromStdString(Registry().CodeOf(info.id));

    auto* layout = new QVBoxLayout(this);
    auto* question = new QLabel(
        tr("<b>Pair companion '%1'?</b>").arg(QString::fromStdString(info.name).toHtmlEscaped()), this);
    layout->addWidget(question);
    QString details = info.device.empty() ? QString() : QString::fromStdString(info.device).toHtmlEscaped() + "<br>";
    details += tr("from %1").arg(QString::fromStdString(info.peer).toHtmlEscaped());
    layout->addWidget(new QLabel(details, this));
    auto* code = new QLabel(tr("Code: <b><span style='font-size:18pt'>%1 %2</span></b> - it must match the device")
                                .arg(_code.left(3), _code.mid(3)),
                            this);
    code->setObjectName(QStringLiteral("companionCode"));
    layout->addWidget(code);
    layout->addWidget(new QLabel(tr("A companion acts as your own hands: it controls the emulator without the remote "
                                    "control lock. Allow it to:"),
                                 this));
    for (const std::string& scope : automation_gate::CompanionScopeNames(automation_gate::kCompanionAllScopes))
    {
        auto* box = new QCheckBox(ScopeLabel(scope), this);
        box->setObjectName(QString::fromStdString(scope));
        box->setChecked((info.scopes & automation_gate::CompanionScopeFromName(scope)) != 0);
        _boxes << box;
        layout->addWidget(box);
    }
    auto* buttons = new QDialogButtonBox(this);
    _allow = buttons->addButton(tr("Allow"), QDialogButtonBox::AcceptRole);
    _deny = buttons->addButton(tr("Deny"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(_allow, &QPushButton::clicked, this, [this]() {
        _decided = true;
        Registry().ApprovePairing(_pairingId.toStdString(), scopes());
        accept();
    });
    connect(_deny, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this]() {
        if (!_decided)
        {
            _decided = true;
            Registry().DenyPairing(_pairingId.toStdString());
        }
    });
    // The request ends on its own: the prompt goes with it
    QTimer::singleShot(std::max(1, info.expiresIn) * 1000, this, &CompanionPairingPrompt::dismiss);
}

uint32_t CompanionPairingPrompt::scopes() const
{
    uint32_t bits = 0;
    for (const QCheckBox* box : _boxes)
        if (box->isChecked())
            bits |= automation_gate::CompanionScopeFromName(box->objectName().toStdString());
    return bits;
}

QCheckBox* CompanionPairingPrompt::scopeBox(const QString& name) const
{
    for (QCheckBox* box : _boxes)
        if (box->objectName() == name)
            return box;
    return nullptr;
}

void CompanionPairingPrompt::dismiss()
{
    _decided = true;
    close();
}

/// endregion </CompanionPairingPrompt>

/// region <CompanionsDialog>

CompanionsDialog::CompanionsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Companions"));
    resize(640, 300);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Devices you paired to act as your own hands: their actions open no remote "
                                    "control session. Revoke one you no longer use or have lost."),
                                 this));
    _table = new QTableWidget(0, 5, this);
    _table->setHorizontalHeaderLabels({tr("Name"), tr("Device"), tr("Scopes"), tr("Source"), tr("Last seen")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->verticalHeader()->hide();
    layout->addWidget(_table);
    _note = new QLabel(this);
    layout->addWidget(_note);
    _acceptPairing = new QCheckBox(tr("Accept pairing requests from devices"), this);
    _acceptPairing->setToolTip(tr("Off: a device asking to pair is refused at once, the requests waiting are denied. "
                                  "Paired companions keep working."));
    layout->addWidget(_acceptPairing);
    auto* row = new QHBoxLayout();
    _revoke = new QPushButton(tr("Revoke"), this);
    _revokeAll = new QPushButton(tr("Revoke all"), this);
    auto* close = new QPushButton(tr("Close"), this);
    row->addWidget(_revoke);
    row->addWidget(_revokeAll);
    row->addStretch();
    row->addWidget(close);
    layout->addLayout(row);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(_revoke, &QPushButton::clicked, this, [this]() {
        const int r = _table->currentRow();
        if (r < 0 || !_table->item(r, 0))
            return;
        Registry().Revoke(_table->item(r, 0)->data(Qt::UserRole).toString().toStdString());
        refresh();
    });
    connect(_revokeAll, &QPushButton::clicked, this, [this]() {
        Registry().RevokeAll();
        refresh();
    });
    connect(_acceptPairing, &QCheckBox::toggled, this, [this](bool allowed) {
        if (allowed == Registry().IsPairingAllowed())
            return;  // refresh() setting the box
        Registry().SetPairingAllowed(allowed);
        emit pairingAllowedChanged(allowed);
        refresh();
    });
    connect(_table, &QTableWidget::itemSelectionChanged, this,
            [this]() { _revoke->setEnabled(_table->currentRow() >= 0); });
    refresh();
}

void CompanionsDialog::refresh()
{
    const std::vector<companion::CompanionInfo> companions = Registry().List();
    _table->setRowCount(static_cast<int>(companions.size()));
    for (int r = 0; r < static_cast<int>(companions.size()); ++r)
    {
        const companion::CompanionInfo& c = companions[static_cast<size_t>(r)];
        auto* name = new QTableWidgetItem(QString::fromStdString(c.name));
        name->setData(Qt::UserRole, QString::fromStdString(c.id));
        _table->setItem(r, 0, name);
        _table->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(c.device)));
        _table->setItem(r, 2, new QTableWidgetItem(ScopesText(c.scopes)));
        _table->setItem(r, 3,
                        new QTableWidgetItem(c.source == "environment" ? tr("environment (this run)") : tr("paired")));
        _table->setItem(r, 4,
                        new QTableWidgetItem(c.lastSeenMs ? QDateTime::fromMSecsSinceEpoch(c.lastSeenMs)
                                                                .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                                                          : tr("never")));
    }
    const size_t pending = Registry().Pending().size();
    const bool allowed = Registry().IsPairingAllowed();
    _acceptPairing->setChecked(allowed);
    _note->setText(!allowed && companions.empty() ? tr("No companions, and pairing requests are turned off.")
                   : companions.empty()           ? tr("No companions. A device asks to pair through the WebAPI "
                                                       "(POST /api/v1/companions/pairings); you allow it here.")
                   : pending ? tr("%n pairing request(s) waiting.", nullptr, static_cast<int>(pending))
                             : QString());
    _revoke->setEnabled(_table->currentRow() >= 0);
    _revokeAll->setEnabled(!companions.empty());
}

/// endregion </CompanionsDialog>
