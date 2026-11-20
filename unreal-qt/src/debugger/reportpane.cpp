#include "reportpane.h"

#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <set>

namespace
{
// The port watch's change colour (portwatchdialog.cpp): a value that differs from the last stop
const QColor kChanged(255, 220, 90);
// A row its pane tints (a NextREG off its reset value, the copper's current word, a visible sprite)
const QColor kRowMark(90, 140, 220, 60);

void MarkItem(QTreeWidgetItem* item)
{
    item->setBackground(1, kChanged);
    item->setForeground(1, QColor(Qt::black));
}
}  // namespace

ReportPane::ReportPane(Builder build, std::string tableKey, QStringList columns, QWidget* parent)
    : QWidget(parent), _build(std::move(build)), _tableKey(std::move(tableKey)), _columns(std::move(columns))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->hide();
    layout->addWidget(_status);

    _fields = new QTreeWidget(this);
    _fields->setColumnCount(2);
    _fields->setHeaderLabels({tr("Field"), tr("Value")});
    _fields->setRootIsDecorated(true);
    _fields->setUniformRowHeights(true);
    _fields->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _fields->header()->setStretchLastSection(true);
    if (_tableKey.empty())
    {
        layout->addWidget(_fields, 1);
        return;
    }
    _table = new QTableWidget(this);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->verticalHeader()->hide();
    _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    QSplitter* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(_fields);
    split->addWidget(_table);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({110, 400});
    layout->addWidget(split, 1);
}

QString ReportPane::ValueText(const StateNode& node)
{
    switch (node.kind)
    {
        case StateNode::Kind::Null:
            return QString();
        case StateNode::Kind::Bool:
            return node.b ? QStringLiteral("on") : QStringLiteral("off");
        case StateNode::Kind::Int:
            return QString::number(node.i);
        case StateNode::Kind::Double:
            return QString::number(node.d, 'g', 6);
        case StateNode::Kind::String:
            return QString::fromStdString(node.s);
        case StateNode::Kind::Object:
        {
            QStringList parts;
            for (const auto& [key, value] : node.members)
                parts << QStringLiteral("%1=%2").arg(QString::fromStdString(key), ValueText(value));
            return parts.join(QStringLiteral(", "));
        }
        case StateNode::Kind::Array:
        {
            QStringList parts;
            for (const StateNode& item : node.items)
                parts << (item.isObject() ? QStringLiteral("{%1}").arg(ValueText(item)) : ValueText(item));
            return parts.join(QStringLiteral(", "));
        }
    }
    return QString();
}

QString ReportPane::status() const
{
    return _status->isHidden() ? QString() : _status->text();
}

bool ReportPane::changed(const QString& path, const QString& text, bool live)
{
    if (live)
        return false;  // live values are never compared (rules.md section 4)
    const auto last = _lastStop.find(path);
    const bool differs = last != _lastStop.end() && last->second != text;
    if (differs)
        _marked[path] = true;
    return differs;
}

void ReportPane::refresh(EmulatorContext* context, bool live)
{
    _marked.clear();
    if (!context)
    {
        _status->setText(tr("No machine."));
        _status->show();
        _fields->clear();
        if (_table)
            _table->setRowCount(0);
        return;
    }
    const StateNode report = _build(context);
    const StateNode* available = report.find("available");
    if (available && available->kind == StateNode::Kind::Bool && !available->b)
    {
        const StateNode* description = report.find("description");
        _status->setText(description ? ValueText(*description) : tr("Not available on this machine."));
        _status->show();
        _fields->clear();
        if (_table)
            _table->setRowCount(0);
        return;
    }
    _status->setVisible(live);
    if (live)
        _status->setText(tr("live: the machine runs (changes are marked at the next stop)"));

    std::map<QString, QString> now;
    fillFields(report, live, now);
    fillTable(_tableKey.empty() ? nullptr : report.find(_tableKey), live, now);
    if (!live)
        _lastStop = std::move(now);
}

void ReportPane::fillFields(const StateNode& report, bool live, std::map<QString, QString>& now)
{
    const int scroll = _fields->verticalScrollBar()->value();
    _fields->setUpdatesEnabled(false);
    _fields->clear();
    std::function<void(const StateNode&, QTreeWidgetItem*, const QString&)> add = [&](const StateNode& node, QTreeWidgetItem* parent,
                                                                                     const QString& prefix) {
        for (const auto& [key, value] : node.members)
        {
            if (!parent && (key == "available" || key == _tableKey))
                continue;
            const QString name = QString::fromStdString(key);
            const QString path = prefix.isEmpty() ? name : prefix + QLatin1Char('.') + name;
            QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_fields);
            item->setText(0, name);
            const bool nestedRows = value.isArray() && !value.items.empty() && value.items.front().isObject();
            if (value.isObject())
            {
                add(value, item, path);
                continue;
            }
            if (nestedRows)
            {
                for (size_t i = 0; i < value.items.size(); i++)
                {
                    QTreeWidgetItem* row = new QTreeWidgetItem(item);
                    row->setText(0, QStringLiteral("[%1]").arg(i));
                    add(value.items[i], row, QStringLiteral("%1[%2]").arg(path).arg(i));
                }
                continue;
            }
            const QString text = ValueText(value);
            item->setText(1, text);
            now[path] = text;
            if (changed(path, text, live))
                MarkItem(item);
        }
    };
    add(report, nullptr, QString());
    _fields->expandAll();
    _fields->verticalScrollBar()->setValue(scroll);
    _fields->setUpdatesEnabled(true);
}

void ReportPane::fillTable(const StateNode* rows, bool live, std::map<QString, QString>& now)
{
    if (!_table)
        return;
    QStringList columns = _columns;
    if (columns.isEmpty() && rows)
    {
        std::set<std::string> seen;
        for (const StateNode& row : rows->items)
            for (const auto& member : row.members)
                if (seen.insert(member.first).second)
                    columns << QString::fromStdString(member.first);
    }
    const int scroll = _table->verticalScrollBar()->value();
    _table->setUpdatesEnabled(false);
    if (_table->columnCount() != columns.size() || _table->property("columns").toStringList() != columns)
    {
        _table->setColumnCount(static_cast<int>(columns.size()));
        _table->setHorizontalHeaderLabels(columns);
        _table->setProperty("columns", columns);
    }
    const int count = rows ? static_cast<int>(rows->items.size()) : 0;
    _table->setRowCount(count);
    for (int r = 0; r < count; r++)
    {
        const StateNode& row = rows->items[static_cast<size_t>(r)];
        const bool tinted = _rowMark && _rowMark(row);
        for (int c = 0; c < columns.size(); c++)
        {
            const StateNode* value = row.find(columns[c].toStdString());
            const QString text = value ? ValueText(*value) : QString();
            QTableWidgetItem* item = _table->item(r, c);
            if (!item)
            {
                item = new QTableWidgetItem();
                _table->setItem(r, c, item);
            }
            item->setText(text);
            const QString path = QStringLiteral("%1.%2").arg(r).arg(columns[c]);
            now[path] = text;
            if (changed(path, text, live))
            {
                item->setBackground(kChanged);
                item->setForeground(QColor(Qt::black));
            }
            else
            {
                item->setBackground(tinted ? QBrush(kRowMark) : QBrush());
                item->setData(Qt::ForegroundRole, QVariant());
            }
        }
    }
    _table->resizeColumnsToContents();
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->verticalScrollBar()->setValue(scroll);
    _table->setUpdatesEnabled(true);
}
