#include "profilesdialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "controllers/hostcontrollers.h"

#ifdef ENABLE_CONTROLLERS
using namespace controllers;
#endif

namespace
{
const char* const kTemplate = R"(# A play profile of your own (mapping-and-profiles.md §7): bindings per layer, control: target
schema: 1
kind: play
id: user/my-profile
label: My profile
# applies_to: { families: [dualsense] }
# extends: [scheme/qaop-space]
layers:
  play:
    south: { retro: "auto:fire", label: Fire }
    north: { key: SPACE }
)";
}  // namespace

ProfilesDialog::ProfilesDialog(HostControllers* host, QWidget* parent) : QDialog(parent), _host(host)
{
    setWindowTitle(tr("Controller profiles"));
    resize(1000, 640);
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Show"), this));
    _set = new QComboBox(this);
    _set->addItem(tr("All"), "all");
    _set->addItem(tr("Games"), "game");
    _set->addItem(tr("Play"), "play");
    _set->addItem(tr("Debug"), "debug");
    _set->addItem(tr("Schemes"), "scheme");
    top->addWidget(_set);
    top->addStretch(1);
    auto* folder = new QPushButton(tr("Open folder"), this);
    folder->setToolTip(HostControllers::UserProfileDir());
    top->addWidget(folder);
    root->addLayout(top);

    auto* splitter = new QSplitter(this);
    _list = new QTreeWidget(splitter);
    _list->setHeaderLabels({tr("Profile"), tr("Kind"), tr("Scope"), tr("Applies")});
    _list->setRootIsDecorated(false);
    _list->setSortingEnabled(true);
    _list->sortByColumn(0, Qt::AscendingOrder);
    _list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _text = new QPlainTextEdit(splitter);
    _text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _text->setLineWrapMode(QPlainTextEdit::NoWrap);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    root->addWidget(splitter, 1);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(_status);

    auto* buttons = new QHBoxLayout();
    auto* create = new QPushButton(tr("New"), this);
    auto* saveButton = new QPushButton(tr("Save"), this);
    saveButton->setToolTip(tr("Save the text as a user profile and apply it (a shipped id: your copy overrides it)"));
    _delete = new QPushButton(tr("Delete"), this);
    _delete->setToolTip(tr("Delete a user profile (a shipped one of the same id comes back)"));
    auto* importButton = new QPushButton(tr("Import…"), this);
    auto* exportButton = new QPushButton(tr("Export…"), this);
    _apply = new QPushButton(tr("Use as game profile"), this);
    _apply->setToolTip(tr("Choose this profile for the program running now, as the Game combo does"));
    for (QPushButton* button : {create, saveButton, _delete, importButton, exportButton, _apply})
        buttons->addWidget(button);
    buttons->addStretch(1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(_set, &QComboBox::currentIndexChanged, this, [this] { reload(_shown); });
    connect(_list, &QTreeWidget::currentItemChanged, this, [this] {
        const QString id = selectedId();
        if (!id.isEmpty() && id != _shown)
            showProfile(id);
    });
    connect(create, &QPushButton::clicked, this, &ProfilesDialog::newProfile);
    connect(saveButton, &QPushButton::clicked, this, &ProfilesDialog::save);
    connect(_delete, &QPushButton::clicked, this, &ProfilesDialog::remove);
    connect(importButton, &QPushButton::clicked, this, &ProfilesDialog::importFile);
    connect(exportButton, &QPushButton::clicked, this, &ProfilesDialog::exportFile);
    connect(_apply, &QPushButton::clicked, this, [this] {
#ifdef ENABLE_CONTROLLERS
        const HostReply reply = _host->Host({"apply", {{"id", selectedId().toStdString()}}});
        report(reply.Ok(), QString::fromStdString(reply.message));
        reload(_shown);
#endif
    });
    connect(folder, &QPushButton::clicked, this, [] {
        const QString dir = HostControllers::UserProfileDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    reload();
}

QString ProfilesDialog::selectedId() const
{
    QTreeWidgetItem* item = _list->currentItem();
    return item != nullptr ? item->data(0, Qt::UserRole).toString() : QString();
}

void ProfilesDialog::report(bool ok, const QString& text)
{
    _status->setStyleSheet(ok ? QString() : QString("color: #c33"));
    _status->setText(text);
}

void ProfilesDialog::reload(const QString& select)
{
#ifdef ENABLE_CONTROLLERS
    const HostReply reply = _host->Host({"profiles", {{"set", _set->currentData().toString().toStdString()}}});
    _list->blockSignals(true);
    _list->clear();
    QTreeWidgetItem* current = nullptr;
    const std::string manual = reply.body.Find("manual") != nullptr ? reply.body.Find("manual")->AsString() : "";
    if (const JsonValue* profiles = reply.body.Find("profiles"))
        for (const JsonValue& profile : profiles->Items())
        {
            const auto field = [&profile](const char* name) {
                const JsonValue* value = profile.Find(name);
                return value != nullptr && value->IsString() ? QString::fromStdString(value->AsString()) : QString();
            };
            const QString id = field("id");
            const QString label = field("label");
            const JsonValue* game = profile.Find("game");
            const JsonValue* applied = profile.Find("applied");
            QString kind = field("kind");
            if (game != nullptr && game->AsBool())
                kind += tr(" (game)");
            QString applies = applied != nullptr && applied->AsBool() ? tr("now") : QString();
            if (id.toStdString() == manual)
                applies += applies.isEmpty() ? tr("chosen") : tr(", chosen");
            auto* item = new QTreeWidgetItem(_list, {label.isEmpty() ? id : QString("%1 — %2").arg(label, id), kind,
                                                     field("scope"), applies});
            item->setData(0, Qt::UserRole, id);
            item->setData(1, Qt::UserRole, field("scope"));
            item->setToolTip(0, field("path"));
            if (id == select)
                current = item;
        }
    _list->blockSignals(false);
    if (current != nullptr)
        _list->setCurrentItem(current);
    else if (_list->topLevelItemCount() > 0 && select.isEmpty())
        _list->setCurrentItem(_list->topLevelItem(0));
#else
    (void)select;
#endif
}

void ProfilesDialog::showProfile(const QString& id)
{
#ifdef ENABLE_CONTROLLERS
    const HostReply reply = _host->Host({"profile", {{"id", id.toStdString()}}});
    _shown = id;
    if (!reply.Ok())
    {
        report(false, QString::fromStdString(reply.message));
        _text->clear();
        return;
    }
    const JsonValue* yaml = reply.body.Find("yaml");
    _text->setPlainText(yaml != nullptr ? QString::fromStdString(yaml->AsString()) : QString());
    QTreeWidgetItem* item = _list->currentItem();
    const bool user = item != nullptr && item->data(1, Qt::UserRole).toString() == "user";
    _delete->setEnabled(user);
    _apply->setEnabled(item != nullptr && item->text(1).startsWith("play"));
    report(true, user ? tr("Your profile: Save writes it back") :
                        tr("Shipped: Save keeps your copy of it as a user profile, which overrides this one"));
#else
    (void)id;
#endif
}

void ProfilesDialog::save()
{
#ifdef ENABLE_CONTROLLERS
    const HostReply reply = _host->Host({"save", {{"text", _text->toPlainText().toStdString()}}});
    report(reply.Ok(), QString::fromStdString(reply.message));
    if (reply.Ok())
    {
        _shown.clear();
        reload(QString::fromStdString(reply.body.Find("id")->AsString()));
    }
#endif
}

void ProfilesDialog::remove()
{
#ifdef ENABLE_CONTROLLERS
    const QString id = selectedId();
    const HostReply reply = _host->Host({"delete", {{"id", id.toStdString()}}});
    report(reply.Ok(), QString::fromStdString(reply.message));
    if (reply.Ok())
    {
        _shown.clear();
        reload(id);
    }
#endif
}

void ProfilesDialog::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import a controller profile"), QString(),
                                                      tr("Profiles (*.yaml *.yml *.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        report(false, tr("%1: %2").arg(path, file.errorString()));
        return;
    }
    // Into the editor and saved: an error shows with the text still there to fix
    _text->setPlainText(QString::fromUtf8(file.readAll()));
    save();
}

void ProfilesDialog::exportFile()
{
    QString name = _shown.section('/', -1);
    if (name.isEmpty())
        name = "profile";
    const QString path = QFileDialog::getSaveFileName(this, tr("Export the controller profile"), name + ".yaml",
                                                      tr("Profiles (*.yaml)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(_text->toPlainText().toUtf8()) < 0)
    {
        report(false, tr("%1: %2").arg(path, file.errorString()));
        return;
    }
    report(true, tr("Exported to %1").arg(path));
}

void ProfilesDialog::newProfile()
{
    _list->setCurrentItem(nullptr);
    _shown.clear();
    _text->setPlainText(kTemplate);
    _delete->setEnabled(false);
    _apply->setEnabled(false);
    report(true, tr("A new profile: change its id and bindings, then Save"));
}
