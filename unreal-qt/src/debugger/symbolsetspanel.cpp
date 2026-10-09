#include "symbolsetspanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "debugger/labels/symbolcontrol.h"
#include "debugger/symbolbundlepreferences.h"

namespace
{
enum Column
{
    ColumnOn,
    ColumnPriority,
    ColumnId,
    ColumnTitle,
    ColumnOrigin,
    ColumnSymbols,
    ColumnCount
};
}  // namespace

SymbolSetsPanel::SymbolSetsPanel(LabelManager* labelManager, QWidget* parent) : QWidget(parent), _labelManager(labelManager)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _table = new QTableWidget(this);
    _table->setColumnCount(ColumnCount);
    _table->setHorizontalHeaderLabels({tr("On"), tr("Priority"), tr("Set"), tr("Title"), tr("Origin"), tr("Symbols")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->setVisible(false);
    _table->setColumnWidth(ColumnOn, 36);
    _table->setColumnWidth(ColumnPriority, 70);
    _table->setColumnWidth(ColumnId, 260);
    _table->setColumnWidth(ColumnTitle, 200);
    _table->setColumnWidth(ColumnOrigin, 70);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setToolTip(tr("A higher priority wins where two sets name the same label; between equal priorities the later "
                          "set wins. Edit a priority by double-clicking it."));

    _dropButton = new QPushButton(tr("&Drop Set"), this);
    auto* refreshButton = new QPushButton(tr("&Refresh"), this);
    _summary = new QLabel(this);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(_dropButton);
    buttons->addWidget(refreshButton);
    buttons->addStretch();
    buttons->addWidget(_summary);

    layout->addWidget(_table);
    layout->addLayout(buttons);

    connect(_table, &QTableWidget::cellChanged, this, &SymbolSetsPanel::onItemChanged);
    connect(_table, &QTableWidget::itemSelectionChanged, this, [this]() { _dropButton->setEnabled(!_table->selectedItems().isEmpty()); });
    connect(_dropButton, &QPushButton::clicked, this, &SymbolSetsPanel::dropSelected);
    connect(refreshButton, &QPushButton::clicked, this, &SymbolSetsPanel::refresh);

    refresh();
}

void SymbolSetsPanel::refresh()
{
    _filling = true;
    _table->setRowCount(0);
    const SymbolReply reply = SymbolControl(_labelManager).Execute({"sets", {}});
    if (reply.Ok())
    {
        const StateNode& sets = *reply.body.find("sets");
        _table->setRowCount(static_cast<int>(sets.items.size()));
        for (int row = 0; row < static_cast<int>(sets.items.size()); row++)
        {
            const StateNode& set = sets.items[static_cast<size_t>(row)];
            auto* on = new QTableWidgetItem();
            on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            on->setCheckState(set.find("enabled")->b ? Qt::Checked : Qt::Unchecked);
            _table->setItem(row, ColumnOn, on);

            auto* priority = new QTableWidgetItem(QString::number(set.find("priority")->i));
            priority->setFlags(Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            _table->setItem(row, ColumnPriority, priority);

            const auto fixed = [&](int column, const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                _table->setItem(row, column, item);
            };
            fixed(ColumnId, QString::fromStdString(set.find("id")->s));
            fixed(ColumnTitle, QString::fromStdString(set.find("title")->s));
            fixed(ColumnOrigin, QString::fromStdString(set.find("origin")->find("kind")->s));
            fixed(ColumnSymbols, QString::number(set.find("symbols")->i));
            _table->item(row, ColumnId)->setToolTip(QString::fromStdString(set.find("origin")->find("where")->s));
        }
        _summary->setText(tr("%1 sets, %2 labels").arg(sets.items.size()).arg(reply.body.find("labels")->i));
    }
    else
        _summary->setText(QString::fromStdString(reply.message));
    _dropButton->setEnabled(false);
    _filling = false;
}

void SymbolSetsPanel::onItemChanged(int row, int column)
{
    if (_filling || (column != ColumnOn && column != ColumnPriority))
        return;
    const QString id = _table->item(row, ColumnId)->text();
    SymbolRequest request{"set", {{"id", id.toStdString()}}};
    if (column == ColumnOn)
        request.options["enabled"] = _table->item(row, ColumnOn)->checkState() == Qt::Checked ? "true" : "false";
    else
        request.options["priority"] = _table->item(row, ColumnPriority)->text().trimmed().toStdString();
    const SymbolReply reply = SymbolControl(_labelManager).Execute(request);
    if (!reply.Ok())
        QMessageBox::warning(this, tr("Symbol Sets"), QString::fromStdString(reply.message));
    else if (column == ColumnOn)
        SymbolBundlePreferences::Save(id.toStdString(), request.options["enabled"] == "true");  // a bundle stays off between sessions
    refresh();
    emit setsChanged();
}

void SymbolSetsPanel::dropSelected()
{
    const QList<QTableWidgetItem*> selected = _table->selectedItems();
    if (selected.isEmpty())
        return;
    const QString id = _table->item(selected.first()->row(), ColumnId)->text();
    if (QMessageBox::question(this, tr("Drop Symbol Set"), tr("Drop the set %1 and its labels?").arg(id)) != QMessageBox::Yes)
        return;
    const SymbolReply reply = SymbolControl(_labelManager).Execute({"drop", {{"id", id.toStdString()}}});
    if (!reply.Ok())
        QMessageBox::warning(this, tr("Symbol Sets"), QString::fromStdString(reply.message));
    refresh();
    emit setsChanged();
}
