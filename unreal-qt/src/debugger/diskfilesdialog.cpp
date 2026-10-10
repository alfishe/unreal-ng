#include "diskfilesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "debugger/asm/asmcontrol.h"
#include "debugger/labels/symbolcontrol.h"
#include "emulator/emulator.h"

namespace
{
enum Column
{
    ColumnName,
    ColumnType,
    ColumnStart,
    ColumnLength,
    ColumnFormat,
    ColumnCount
};

QString Diagnostics(const StateNode& body)
{
    QStringList lines;
    if (const StateNode* diagnostics = body.find("diagnostics"))
        for (const StateNode& d : diagnostics->items)
            lines << QString("%1, line %2: %3")
                         .arg(QString::fromStdString(d.find("severity")->s))
                         .arg(d.find("line")->i)
                         .arg(QString::fromStdString(d.find("message")->s));
    return lines.join("\n");
}

/// A read-only text window (the decoded or converted source)
void ShowText(QWidget* parent, const QString& title, const QString& text, const QString& diagnostics)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(760, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(text);
    layout->addWidget(view);
    if (!diagnostics.isEmpty())
    {
        auto* notes = new QPlainTextEdit(&dialog);
        notes->setReadOnly(true);
        notes->setMaximumHeight(90);
        notes->setPlainText(diagnostics);
        layout->addWidget(notes);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
}  // namespace

DiskFilesDialog::DiskFilesDialog(Emulator* emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle(tr("Disk Files"));
    resize(640, 420);
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Drive:"), this));
    _drive = new QComboBox(this);
    _drive->addItems({"A", "B", "C", "D"});
    top->addWidget(_drive);
    auto* refreshButton = new QPushButton(tr("&Refresh"), this);
    top->addWidget(refreshButton);
    top->addStretch();
    layout->addLayout(top);

    _table = new QTableWidget(this);
    _table->setColumnCount(ColumnCount);
    _table->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Start"), tr("Length"), tr("Source format")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->verticalHeader()->setVisible(false);
    _table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(_table);

    auto* buttons = new QHBoxLayout();
    _open = new QPushButton(tr("&Open as Source"), this);
    _export = new QPushButton(tr("&Export as Text..."), this);
    _convert = new QPushButton(tr("&Convert to..."), this);
    _labels = new QPushButton(tr("Import &Labels"), this);
    _labels->setToolTip(tr("Assemble the source on the host (the disk as its project) and give the debugger its labels"));
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(_open);
    buttons->addWidget(_export);
    buttons->addWidget(_convert);
    buttons->addWidget(_labels);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    _status = new QLabel(this);
    layout->addWidget(_status);

    connect(_drive, &QComboBox::currentIndexChanged, this, &DiskFilesDialog::refresh);
    connect(refreshButton, &QPushButton::clicked, this, &DiskFilesDialog::refresh);
    connect(_table, &QTableWidget::itemSelectionChanged, this, &DiskFilesDialog::updateButtons);
    connect(_table, &QTableWidget::cellDoubleClicked, this, [this]() { openSource(); });
    connect(_open, &QPushButton::clicked, this, &DiskFilesDialog::openSource);
    connect(_export, &QPushButton::clicked, this, &DiskFilesDialog::exportText);
    connect(_convert, &QPushButton::clicked, this, &DiskFilesDialog::convertTo);
    connect(_labels, &QPushButton::clicked, this, &DiskFilesDialog::importLabels);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void DiskFilesDialog::refresh()
{
    _table->setRowCount(0);
    const AsmReply reply = AsmControl(_emulator ? _emulator->GetContext() : nullptr)
                               .Execute({"files", {{"drive", _drive->currentText().toStdString()}}});
    if (!reply.Ok())
    {
        _status->setText(QString::fromStdString(reply.message));
        updateButtons();
        return;
    }
    const auto& files = reply.body.find("files")->items;
    _table->setRowCount(static_cast<int>(files.size()));
    int sources = 0;
    for (int row = 0; row < static_cast<int>(files.size()); ++row)
    {
        const StateNode& f = files[static_cast<size_t>(row)];
        const bool source = f.find("format")->kind == StateNode::Kind::String;
        sources += source;
        auto* name = new QTableWidgetItem(QString::fromStdString(f.find("name")->s));
        name->setData(Qt::UserRole, QString::fromStdString(f.find("path")->s));
        name->setData(Qt::UserRole + 1, source);
        _table->setItem(row, ColumnName, name);
        _table->setItem(row, ColumnType, new QTableWidgetItem(QString::fromStdString(f.find("type")->s)));
        _table->setItem(row, ColumnStart, new QTableWidgetItem(QString::number(f.find("start")->i)));
        _table->setItem(row, ColumnLength, new QTableWidgetItem(QString::number(f.find("length")->i)));
        _table->setItem(row, ColumnFormat, new QTableWidgetItem(source ? QString::fromStdString(f.find("format")->s) : QString("-")));
    }
    _status->setText(tr("%1 files, %2 sources").arg(files.size()).arg(sources));
    updateButtons();
}

QString DiskFilesDialog::selectedPath() const
{
    const QList<QTableWidgetItem*> selected = _table->selectedItems();
    if (selected.isEmpty())
        return {};
    return _table->item(selected.first()->row(), ColumnName)->data(Qt::UserRole).toString();
}

void DiskFilesDialog::updateButtons()
{
    const QList<QTableWidgetItem*> selected = _table->selectedItems();
    const bool source = !selected.isEmpty() && _table->item(selected.first()->row(), ColumnName)->data(Qt::UserRole + 1).toBool();
    _open->setEnabled(source);
    _export->setEnabled(source);
    _convert->setEnabled(source);
    _labels->setEnabled(source);
}

void DiskFilesDialog::openSource()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    const AsmReply reply = AsmControl(_emulator->GetContext()).Execute({"decode", {{"path", path.toStdString()}}});
    if (!reply.Ok())
    {
        QMessageBox::warning(this, tr("Open as Source"), QString::fromStdString(reply.message));
        return;
    }
    const QString title = tr("%1 (%2 %3)")
                              .arg(path, QString::fromStdString(reply.body.find("format")->s), QString::fromStdString(reply.body.find("version")->s));
    ShowText(this, title, QString::fromStdString(reply.body.find("text")->s), Diagnostics(reply.body));
}

void DiskFilesDialog::exportText()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    const QString name = _table->item(_table->selectedItems().first()->row(), ColumnName)->text().trimmed();
    const QString output = QFileDialog::getSaveFileName(this, tr("Export as Text"), name + ".txt", tr("Text (*.txt *.asm);;All Files (*)"));
    if (output.isEmpty())
        return;
    const AsmReply reply =
        AsmControl(_emulator->GetContext()).Execute({"decode", {{"path", path.toStdString()}, {"output", output.toStdString()}}});
    if (!reply.Ok())
        QMessageBox::warning(this, tr("Export as Text"), QString::fromStdString(reply.message));
    else
        _status->setText(tr("%1 lines written to %2").arg(reply.body.find("lines")->i).arg(output));
}

void DiskFilesDialog::convertTo()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    QStringList dialects;
    const AsmReply list = AsmControl(nullptr).Execute({"dialects", {}});
    if (list.Ok())
        for (const StateNode& d : list.body.find("write")->items)
            dialects << QString::fromStdString(d.s);
    bool ok = false;
    const QString to = QInputDialog::getItem(this, tr("Convert to"), tr("Dialect:"), dialects, std::max<int>(0, static_cast<int>(dialects.indexOf("sjasmplus"))), false, &ok);
    if (!ok || to.isEmpty())
        return;
    const AsmReply reply = AsmControl(_emulator->GetContext()).Execute({"convert", {{"path", path.toStdString()}, {"to", to.toStdString()}}});
    if (!reply.Ok())
    {
        QMessageBox::warning(this, tr("Convert to"), QString::fromStdString(reply.message) + "\n" + Diagnostics(reply.body));
        return;
    }
    const QString text = QString::fromStdString(reply.body.find("text")->s);
    ShowText(this, tr("%1 in %2").arg(path, to), text, Diagnostics(reply.body));
    const QString name = _table->item(_table->selectedItems().first()->row(), ColumnName)->text().trimmed();
    const QString output = QFileDialog::getSaveFileName(this, tr("Save the Conversion"), name + ".asm", tr("Assembler source (*.asm *.s *.z80);;All Files (*)"));
    if (output.isEmpty())
        return;
    const AsmReply saved = AsmControl(_emulator->GetContext())
                               .Execute({"convert", {{"path", path.toStdString()}, {"to", to.toStdString()}, {"output", output.toStdString()}}});
    if (!saved.Ok())
        QMessageBox::warning(this, tr("Convert to"), QString::fromStdString(saved.message));
    else
        _status->setText(tr("%1 converted to %2: %3").arg(path, to, output));
}

void DiskFilesDialog::importLabels()
{
    const QString path = selectedPath();
    if (path.isEmpty())
        return;
    const SymbolReply reply = SymbolControl(_emulator->GetContext()).Execute({"import-source", {{"path", path.toStdString()}}});
    if (!reply.Ok())
    {
        QMessageBox::warning(this, tr("Import Labels"), QString::fromStdString(reply.message) + "\n" + Diagnostics(reply.body));
        return;
    }
    const bool complete = reply.body.find("complete")->b;
    _status->setText(tr("%1 labels of %2 imported into %3%4")
                         .arg(reply.body.find("records")->i)
                         .arg(QString::fromStdString(reply.body.find("main")->s), QString::fromStdString(reply.body.find("set")->s),
                              complete ? QString() : tr(" (not every label got a value)")));
}
