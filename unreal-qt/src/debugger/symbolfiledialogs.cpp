#include "symbolfiledialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QVBoxLayout>

#include "debugger/labels/symbolcontrol.h"

namespace
{
/// The codec ids and titles SymbolControl lists
void AddFormats(QComboBox* combo)
{
    const SymbolReply reply = SymbolControl(static_cast<LabelManager*>(nullptr)).Execute({"formats", {}});
    if (!reply.Ok())
        return;
    for (const StateNode& f : reply.body.find("formats")->items)
        combo->addItem(QString::fromStdString(f.find("id")->s + "  (" + f.find("title")->s + ")"), QString::fromStdString(f.find("id")->s));
}

QString Count(const StateNode& body, const char* key)
{
    const StateNode* n = body.find(key);
    return n ? QString::number(n->i) : QString("0");
}
}  // namespace

SymbolImportOptionsDialog::SymbolImportOptionsDialog(const QString& path, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Import Symbols"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QFileInfo(path).fileName(), this));

    auto* form = new QFormLayout();
    _format = new QComboBox(this);
    _format->addItem(tr("Auto (by the extension, else detected)"), QString());
    AddFormats(_format);
    _set = new QLineEdit(this);
    _set->setPlaceholderText(tr("the file's own set (replaced on a reload)"));
    _space = new QLineEdit(this);
    _space->setPlaceholderText(tr("cpu:main, rom0, ram3, cache0, const ..."));
    _base = new QLineEdit(this);
    _base->setPlaceholderText(tr("0 (decimal, 0x, #, $)"));
    _policy = new QComboBox(this);
    _policy->addItem(tr("both: a second name for a place becomes an alias"), "both");
    _policy->addItem(tr("keep: what the set has wins"), "keep");
    _policy->addItem(tr("replace: the new record wins"), "replace");
    _policy->addItem(tr("fail: stop at the first conflict"), "fail");
    form->addRow(tr("Format:"), _format);
    form->addRow(tr("Into set:"), _set);
    form->addRow(tr("Space:"), _space);
    form->addRow(tr("Base:"), _base);
    form->addRow(tr("Merge policy:"), _policy);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Without a set, space or base the file is a set of its own; the policy applies to a "
                                    "merge into a set."),
                                 this));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::map<std::string, std::string> SymbolImportOptionsDialog::Options() const
{
    std::map<std::string, std::string> options;
    if (!_format->currentData().toString().isEmpty())
        options["format"] = _format->currentData().toString().toStdString();
    const auto text = [](const QLineEdit* edit) { return edit->text().trimmed().toStdString(); };
    if (!text(_set).empty())
        options["set"] = text(_set);
    if (!text(_space).empty())
        options["space"] = text(_space);
    if (!text(_base).empty())
        options["base"] = text(_base);
    if (options.count("set") || options.count("space") || options.count("base"))
        options["policy"] = _policy->currentData().toString().toStdString();
    return options;
}

SymbolExportOptionsDialog::SymbolExportOptionsDialog(LabelManager* labelManager, const QString& path, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Export Symbols"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QFileInfo(path).fileName(), this));

    auto* form = new QFormLayout();
    _format = new QComboBox(this);
    _format->addItem(tr("By the extension"), QString());
    AddFormats(_format);
    _pages = new QComboBox(this);
    _pages->addItem(tr("fold: at its CPU address"), "fold");
    _pages->addItem(tr("comment: as a comment line"), "comment");
    _pages->addItem(tr("drop: left out"), "drop");
    form->addRow(tr("Format:"), _format);
    form->addRow(tr("Page symbols in a format without pages:"), _pages);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("Sets (none checked: the labels as they show):"), this));
    _sets = new QListWidget(this);
    const SymbolReply reply = SymbolControl(labelManager).Execute({"sets", {}});
    if (reply.Ok())
        for (const StateNode& set : reply.body.find("sets")->items)
        {
            auto* item = new QListWidgetItem(QString::fromStdString(set.find("id")->s), _sets);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    layout->addWidget(_sets);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::map<std::string, std::string> SymbolExportOptionsDialog::Options() const
{
    std::map<std::string, std::string> options;
    if (!_format->currentData().toString().isEmpty())
        options["format"] = _format->currentData().toString().toStdString();
    options["pages"] = _pages->currentData().toString().toStdString();
    std::string sets;
    for (int i = 0; i < _sets->count(); i++)
        if (_sets->item(i)->checkState() == Qt::Checked)
            sets += (sets.empty() ? "" : ",") + _sets->item(i)->text().toStdString();
    if (!sets.empty())
        options["sets"] = sets;
    return options;
}

void ShowSymbolReport(QWidget* parent, const QString& title, const SymbolReply& reply)
{
    const StateNode& body = reply.body;
    QString text;
    if (!reply.Ok())
        text = QObject::tr("Failed: %1").arg(QString::fromStdString(reply.message));
    else if (body.find("written"))
        text = QObject::tr("%1 symbols written as %2.").arg(Count(body, "written"), QString::fromStdString(body.find("format")->s));
    else
        text = QObject::tr("%1 records into %2: %3 added, %4 aliases, %5 updated, %6 skipped. Labels now: %7.")
                   .arg(Count(body, "records"), body.find("set") ? QString::fromStdString(body.find("set")->s) : QString("-"),
                        Count(body, "added"), Count(body, "aliased"), Count(body, "updated"), Count(body, "skipped"), Count(body, "labels"));
    QStringList details;
    if (const StateNode* conflicts = body.find("conflicts"))
        for (const StateNode& c : conflicts->items)
            details << QObject::tr("conflict, line %1: %2 %3 -> %4 (%5)")
                           .arg(c.find("line")->i)
                           .arg(QString::fromStdString(c.find("name")->s), QString::fromStdString(c.find("old")->s),
                                QString::fromStdString(c.find("new")->s), QString::fromStdString(c.find("resolution")->s));
    if (const StateNode* diagnostics = body.find("diagnostics"))
        for (const StateNode& d : diagnostics->items)
            details << QObject::tr("%1, line %2: %3")
                           .arg(QString::fromStdString(d.find("severity")->s))
                           .arg(d.find("line")->i)
                           .arg(QString::fromStdString(d.find("message")->s));
    QMessageBox box(reply.Ok() ? QMessageBox::Information : QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    if (!details.isEmpty())
        box.setDetailedText(details.join("\n"));
    box.exec();
}
