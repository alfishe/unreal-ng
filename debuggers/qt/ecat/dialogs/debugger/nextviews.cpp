// unreal-debugger (not in ecat): the ZX Spectrum Next's tabs of the debugger window

#include "nextviews.h"

#include <QAbstractButton>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "debugstyle.h"

namespace
{
bool is_row_list(const DeviceNode &n)
{
    return !n.children.empty() && n.children.front().name.size() > 1 && n.children.front().name.front() == '[';
}
}

NextReportView::NextReportView(Emulator * e, const std::string &device, const std::vector<std::string> &keys,
                               const std::string &table, const std::vector<std::string> &columns, QWidget *parent):
    QWidget(parent),
    e(e),
    m_device(device),
    m_keys(keys),
    m_table_key(table),
    m_columns(columns)
{
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_status = new QLabel(this);
    m_status->hide();
    layout->addWidget(m_status);
    m_fields = new QTreeWidget(this);
    m_fields->setFont(dbg::font());
    m_fields->setColumnCount(2);
    m_fields->setHeaderLabels(QStringList() << tr("Field") << tr("Value"));
    m_fields->setUniformRowHeights(true);
    m_fields->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fields->header()->setStretchLastSection(true);
    if (m_table_key.empty()) {
        layout->addWidget(m_fields, 1);
        return;
    }
    m_table = new QTableWidget(this);
    m_table->setFont(dbg::font());
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(dbg::row_height(m_table->fontMetrics()));
    m_table->horizontalHeader()->setStretchLastSection(true);
    //The corner button is the table's: the views' font (the window's scale tells views from frames by it)
    if (QAbstractButton * corner = m_table->findChild<QAbstractButton *>()) corner->setProperty("dbg_mono", true);
    QSplitter * split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_fields);
    split->addWidget(m_table);
    split->setStretchFactor(1, 1);
    split->setSizes(QList<int>() << 110 << 400);
    layout->addWidget(split, 1);
}

ComputerDevice * NextReportView::device() const
{
    if (!e->loaded || e->dm == nullptr) return nullptr;
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * d = e->dm->get_device(i)->device.get();
        if (d->name == m_device && d->type == "report") return d;  //"gs" is the card's CPU as well
    }
    return nullptr;
}

bool NextReportView::present() const
{
    return device() != nullptr;
}

void NextReportView::mark_stop()
{
    m_stop = m_now;
}

void NextReportView::refresh()
{
    ComputerDevice * d = device();
    if (d == nullptr) {
        m_status->setText(tr("Not a ZX Spectrum Next"));
        m_status->show();
        return;
    }
    m_status->hide();
    DeviceNode report;
    bool read = false;
    e->invoke([d, &report, &read]() { read = d->get_tree(true, report); });
    if (!read) return;

    const dbg::Colors c = dbg::colors(palette());
    m_now.clear();
    auto paint = [this, &c](const std::string &path, const std::string &value) {
        m_now[path] = value;
        const auto last = m_stop.find(path);
        return !m_stop.empty() && last != m_stop.end() && last->second != value;
    };

    //The fields: every part but the table's, nested ones under their name
    m_fields->setUpdatesEnabled(false);
    m_fields->clear();
    std::function<void(const DeviceNode &, QTreeWidgetItem *, const std::string &)> add =
        [&](const DeviceNode &n, QTreeWidgetItem * parent, const std::string &prefix) {
            QTreeWidgetItem * item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_fields);
            const std::string path = prefix.empty() ? n.name : prefix + "." + n.name;
            item->setText(0, QString::fromStdString(n.name));
            if (n.children.empty()) {
                item->setText(1, QString::fromStdString(n.value));
                if (paint(path, n.value)) item->setForeground(1, c.changed);
                return;
            }
            for (const DeviceNode &child : n.children) add(child, item, path);
        };
    const DeviceNode * rows = nullptr;
    for (const DeviceNode &part : report.children) {
        if (!m_keys.empty() && std::find(m_keys.begin(), m_keys.end(), part.name) == m_keys.end()) continue;
        if (part.name == m_table_key && is_row_list(part)) {
            rows = &part;
            continue;
        }
        add(part, nullptr, std::string());
    }
    m_fields->expandAll();
    m_fields->setUpdatesEnabled(true);
    if (m_table == nullptr) return;

    //The table: a row an item, a column a part of the items (in the order they first come)
    std::vector<std::string> columns = m_columns;
    if (rows != nullptr && columns.empty())
        for (const DeviceNode &row : rows->children)
            for (const DeviceNode &cell : row.children)
                if (std::find(columns.begin(), columns.end(), cell.name) == columns.end()) columns.push_back(cell.name);
    QStringList labels;
    for (const std::string &name : columns) labels << QString::fromStdString(name);
    const int scroll = m_table->verticalScrollBar()->value();
    m_table->setUpdatesEnabled(false);
    m_table->verticalHeader()->setDefaultSectionSize(dbg::row_height(m_table->fontMetrics()));  //the window's scale
    if (m_table->property("columns").toStringList() != labels) {
        m_table->clear();
        m_table->setColumnCount((int)columns.size());
        m_table->setHorizontalHeaderLabels(labels);
        m_table->setProperty("columns", labels);
    }
    const int count = rows ? (int)rows->children.size() : 0;
    m_table->setRowCount(count);
    for (int r = 0; r < count; r++) {
        const DeviceNode &row = rows->children[(size_t)r];
        for (int k = 0; k < (int)columns.size(); k++) {
            std::string value;
            for (const DeviceNode &cell : row.children)
                if (cell.name == columns[(size_t)k]) value = cell.value;
            QTableWidgetItem * item = m_table->item(r, k);
            if (item == nullptr) {
                item = new QTableWidgetItem();
                m_table->setItem(r, k, item);
            }
            item->setText(QString::fromStdString(value));
            const bool changed = paint(m_table_key + "[" + std::to_string(r) + "]." + columns[(size_t)k], value);
            item->setForeground(changed ? QBrush(c.changed) : QBrush(c.text));
        }
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalScrollBar()->setValue(scroll);
    m_table->setUpdatesEnabled(true);
}

namespace {
std::vector<NextTab> named(std::vector<NextTab> tabs, const char * group)
{
    for (NextTab &t : tabs) t.view->setObjectName(QString(group) + ":" + QString::fromStdString(t.view->report()));
    return tabs;
}
}

std::vector<NextTab> CreateCardTabs(Emulator * e, QWidget *parent)
{
    return named({
        {QObject::tr("GS log"), new NextReportView(e, "gs-commands", {}, "records", {"frame", "t", "kind", "command", "name", "text"}, parent)},
        {QObject::tr("GS protocol"), new NextReportView(e, "gs", {"protocol", "last_send", "firmware_profile", "status", "command_pending",
                                                                   "data_pending", "command_from_host", "data_from_host", "data_to_host"},
                                                        std::string(), {}, parent)},
        {QObject::tr("GS firmware"), new NextReportView(e, "gs-firmware", {}, "window_variables", {"name", "at", "size", "value", "meaning"}, parent)},
        {QObject::tr("GS hardware"), new NextReportView(e, "gs", {}, "channels", {}, parent)},
        {QObject::tr("GS stats"), new NextReportView(e, "gs-stats", {}, std::string(), {}, parent)},
    }, "card");
}

std::vector<NextTab> CreateNextTabs(Emulator * e, QWidget *parent)
{
    return named({
        {QObject::tr("NextREG"), new NextReportView(e, "next-regs", {}, "registers", {"nr", "value", "reset", "access", "name"}, parent)},
        {QObject::tr("MMU"), new NextReportView(e, "next-mmu", {}, "slots", {}, parent)},
        {QObject::tr("Copper"), new NextReportView(e, "next-copper", {}, "instructions", {"index", "word", "current", "text"}, parent)},
        {QObject::tr("Sprites"), new NextReportView(e, "next-sprites", {}, "sprites", {}, parent)},
        {QObject::tr("DMA"), new NextReportView(e, "next-dma", {}, std::string(), {}, parent)},
        {QObject::tr("CTC / IM2"), new NextReportView(e, "next", {"interrupts", "ctc"}, "ctc", {}, parent)},
    }, "next");
}
