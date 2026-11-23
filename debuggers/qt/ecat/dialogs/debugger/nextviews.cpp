// unreal-debugger (not in ecat): the ZX Spectrum Next's tabs of the debugger window

#include "nextviews.h"

#include <QAbstractButton>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    m_absent(tr("Not a ZX Spectrum Next"))
{
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_caption = new QLabel(this);
    m_caption->setObjectName("caption");
    m_caption->hide();
    layout->addWidget(m_caption);
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
    layout->addWidget(m_fields, 1);
    if (!table.empty()) add_table(table, columns);
}

void NextReportView::add_table(const std::string &key, const std::vector<std::string> &columns)
{
    QTableWidget * table = new QTableWidget(this);
    table->setObjectName(QString::fromStdString("table:" + key));
    table->setFont(dbg::font());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(dbg::row_height(table->fontMetrics()));
    table->horizontalHeader()->setStretchLastSection(true);
    //The corner button is the table's: the views' font (the window's scale tells views from frames by it)
    if (QAbstractButton * corner = table->findChild<QAbstractButton *>()) corner->setProperty("dbg_mono", true);
    if (m_split == nullptr) {
        auto * layout = static_cast<QVBoxLayout *>(this->layout());
        const int at = layout->indexOf(m_fields);
        m_split = new QSplitter(Qt::Vertical, this);
        layout->insertWidget(at, m_split, 1);
        m_split->addWidget(m_fields);
    }
    m_split->addWidget(table);
    m_tables.push_back(Table{key, columns, table});
    QList<int> sizes;
    sizes << 110;
    for (size_t i = 0; i < m_tables.size(); i++) sizes << 400 / (int)m_tables.size();
    m_split->setSizes(sizes);
    for (int i = 1; i < m_split->count(); i++) m_split->setStretchFactor(i, 1);
}

void NextReportView::add_source(const std::string &card, const std::string &device, const std::string &part)
{
    m_sources[card] = std::make_pair(device, part);
}

bool NextReportView::use_source(const std::string &card, const QString &caption)
{
    if (m_sources.empty()) return false;
    const auto it = m_sources.find(card);
    const std::string device = it == m_sources.end() ? std::string() : it->second.first;
    const std::string part = it == m_sources.end() ? std::string() : it->second.second;
    m_caption->setText(caption);
    m_caption->setVisible(!caption.isEmpty());
    if (device == m_device && part == m_part) return false;
    m_device = device;
    m_part = part;
    m_now.clear();
    m_stop.clear();
    return true;
}

bool NextReportView::read(ComputerDevice * d, DeviceNode &report) const
{
    bool read = false;
    e->invoke([d, &report, &read]() { read = d->get_tree(true, report); });
    if (!read || m_part.empty()) return read;
    for (DeviceNode &part : report.children)
        if (part.name == m_part) {
            DeviceNode taken = std::move(part);
            report = std::move(taken);
            return true;
        }
    return false;
}

ComputerDevice * NextReportView::device() const
{
    if (m_device.empty() || !e->loaded || e->dm == nullptr) return nullptr;
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * d = e->dm->get_device(i)->device.get();
        if (d->name == m_device && d->type == "report") return d;  //"gs" is the card's CPU as well
    }
    return nullptr;
}

bool NextReportView::present() const
{
    ComputerDevice * d = device();
    if (d == nullptr) return false;
    if (m_needs.empty() && m_part.empty()) return true;
    DeviceNode report;
    if (!read(d, report)) return false;
    if (m_needs.empty()) return true;
    for (const DeviceNode &part : report.children)
        if (part.name == m_needs) return true;
    return false;
}

void NextReportView::set_shape(const Shape &shape, const std::string &needs, const QString &absent)
{
    m_shape = shape;
    m_needs = needs;
    if (!absent.isEmpty()) m_absent = absent;
}

void NextReportView::add_action(const QString &title, const std::function<QString()> &run)
{
    if (m_actions == nullptr) {
        m_actions = new QWidget(this);
        QHBoxLayout * row = new QHBoxLayout(m_actions);
        row->setContentsMargins(0, 0, 0, 0);
        m_answer = new QLabel(m_actions);
        m_answer->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(m_answer, 1);
        static_cast<QVBoxLayout *>(layout())->addWidget(m_actions);
    }
    QPushButton * button = new QPushButton(title, m_actions);
    button->setObjectName("action:" + title);
    auto * row = static_cast<QHBoxLayout *>(m_actions->layout());
    row->insertWidget(row->count() - 1, button);
    connect(button, &QPushButton::clicked, this, [this, run]() {
        const QString answer = run();
        if (answer.isNull()) return;  //cancelled
        m_answer->setText(answer);
        refresh();
    });
}

void NextReportView::mark_stop()
{
    m_stop = m_now;
}

void NextReportView::refresh()
{
    ComputerDevice * d = device();
    if (d == nullptr) {
        m_status->setText(m_absent);
        m_status->show();
        return;
    }
    DeviceNode report;
    if (!read(d, report)) return;
    bool shaped = false;
    if (m_shape) {
        DeviceNode shown;
        if (m_shape(report, shown)) {
            report = std::move(shown);
            shaped = true;
        } else if (!m_needs.empty()) {
            m_status->setText(m_absent);
            m_status->show();
            m_fields->clear();
            for (const Table &t : m_tables) t.widget->setRowCount(0);
            return;
        }
    }
    m_status->hide();

    const dbg::Colors c = dbg::colors(palette());
    m_now.clear();

    //The fields: every part but the tables', nested ones under their name
    m_fields->setUpdatesEnabled(false);
    m_fields->clear();
    std::function<void(const DeviceNode &, QTreeWidgetItem *, const std::string &)> add =
        [&](const DeviceNode &n, QTreeWidgetItem * parent, const std::string &prefix) {
            QTreeWidgetItem * item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_fields);
            const std::string path = prefix.empty() ? n.name : prefix + "." + n.name;
            item->setText(0, QString::fromStdString(n.name));
            if (n.children.empty()) {
                item->setText(1, QString::fromStdString(n.value));
                if (changed(path, n.value)) item->setForeground(1, c.changed);
                return;
            }
            if (shaped) item->setText(1, QString::fromStdString(n.value));  //a board group's register
            for (const DeviceNode &child : n.children) add(child, item, path);
        };
    std::vector<const DeviceNode *> rows(m_tables.size(), nullptr);
    for (const DeviceNode &part : report.children) {
        if (!m_keys.empty() && std::find(m_keys.begin(), m_keys.end(), part.name) == m_keys.end()) continue;
        bool listed = false;
        for (size_t t = 0; t < m_tables.size() && !listed; t++)
            if (part.name == m_tables[t].key && (is_row_list(part) || shaped)) {
                rows[t] = &part;
                listed = true;
            }
        if (!listed) add(part, nullptr, std::string());
    }
    m_fields->expandAll();
    m_fields->setUpdatesEnabled(true);
    for (size_t t = 0; t < m_tables.size(); t++) fill(m_tables[t], rows[t]);
}

bool NextReportView::changed(const std::string &path, const std::string &value)
{
    m_now[path] = value;
    const auto last = m_stop.find(path);
    return !m_stop.empty() && last != m_stop.end() && last->second != value;
}

void NextReportView::fill(const Table &t, const DeviceNode * rows)
{
    //A row an item, a column a part of the items (in the order they first come)
    QTableWidget * table = t.widget;
    const dbg::Colors c = dbg::colors(palette());
    std::vector<std::string> columns = t.columns;
    if (rows != nullptr && columns.empty())
        for (const DeviceNode &row : rows->children)
            for (const DeviceNode &cell : row.children)
                if (std::find(columns.begin(), columns.end(), cell.name) == columns.end()) columns.push_back(cell.name);
    QStringList labels;
    for (const std::string &name : columns) labels << QString::fromStdString(name);
    const int scroll = table->verticalScrollBar()->value();
    table->setUpdatesEnabled(false);
    table->verticalHeader()->setDefaultSectionSize(dbg::row_height(table->fontMetrics()));  //the window's scale
    if (table->property("columns").toStringList() != labels) {
        table->clear();
        table->setColumnCount((int)columns.size());
        table->setHorizontalHeaderLabels(labels);
        table->setProperty("columns", labels);
    }
    const int count = rows ? (int)rows->children.size() : 0;
    table->setRowCount(count);
    for (int r = 0; r < count; r++) {
        const DeviceNode &row = rows->children[(size_t)r];
        for (int k = 0; k < (int)columns.size(); k++) {
            std::string value;
            for (const DeviceNode &cell : row.children)
                if (cell.name == columns[(size_t)k]) value = cell.value;
            QTableWidgetItem * item = table->item(r, k);
            if (item == nullptr) {
                item = new QTableWidgetItem();
                table->setItem(r, k, item);
            }
            item->setText(QString::fromStdString(value));
            const bool moved = changed(t.key + "[" + std::to_string(r) + "]." + columns[(size_t)k], value);
            item->setForeground(moved ? QBrush(c.changed) : QBrush(c.text));
        }
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalScrollBar()->setValue(scroll);
    table->setUpdatesEnabled(true);
}

namespace {
std::vector<NextTab> named(std::vector<NextTab> tabs, const char * group)
{
    for (NextTab &t : tabs) t.view->setObjectName(QString(group) + ":" + QString::fromStdString(t.view->report()));
    return tabs;
}

//The NeoGS tabs (gui-card-debugger B3) fall back on this decoding when the core's boards cannot be read: the gs report's parts as the in-process tree and the WebAPI's JSON give them
//(numbers decimal, true / false, a short list of scalars on one line), decoded into groups. Only what the report has
const DeviceNode * part(const DeviceNode &n, const std::string &path)
{
    const DeviceNode * at = &n;
    size_t from = 0;
    while (at != nullptr && from <= path.size()) {
        const size_t dot = path.find('.', from);
        const std::string name = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        const DeviceNode * next = nullptr;
        for (const DeviceNode &c : at->children)
            if (c.name == name) next = &c;
        at = next;
        if (dot == std::string::npos) break;
        from = dot + 1;
    }
    return at;
}

std::string text(const DeviceNode &n, const std::string &path)
{
    const DeviceNode * p = part(n, path);
    return p ? p->value : std::string();
}

long long number(const DeviceNode &n, const std::string &path, long long otherwise = -1)
{
    const std::string v = text(n, path);
    if (v.empty()) return otherwise;
    char * end = nullptr;
    const long long r = std::strtoll(v.c_str(), &end, 10);
    return (end != nullptr && *end == 0) ? r : otherwise;
}

bool truth(const DeviceNode &n, const std::string &path)
{
    return text(n, path) == "true";
}

std::string hex(long long v, int digits)
{
    if (v < 0) return "-";
    char b[24];
    std::snprintf(b, sizeof(b), "#%0*llX", digits, v);
    return b;
}

std::string grouped(long long v)
{
    if (v < 0) return "-";
    std::string digits = std::to_string(v);
    for (int i = (int)digits.size() - 3; i > 0; i -= 3) digits.insert((size_t)i, ",");
    return digits;
}

std::string mhz(long long hz)
{
    if (hz <= 0) return "-";
    char b[24];
    if (hz % 1000000 == 0) std::snprintf(b, sizeof(b), "%lld MHz", hz / 1000000);
    else std::snprintf(b, sizeof(b), "%.3g MHz", (double)hz / 1e6);
    return b;
}

//"#0B: SD_nCS MC_nCS MCSPD0": the value and its set bits by name (the core's DebugBoards names)
std::string bits(long long v, const std::vector<std::string> &names, int digits = 2)
{
    if (v < 0) return "-";
    std::string set;
    for (size_t i = 0; i < names.size(); i++)
        if ((v & (1LL << i)) && !names[i].empty()) set += (set.empty() ? "" : " ") + std::string(names[i]);
    return hex(v, digits) + ": " + (set.empty() ? std::string("none") : set);
}

std::string yes(bool b, const char * on = "yes", const char * off = "no")
{
    return b ? on : off;
}

DeviceNode leaf(const std::string &name, const std::string &value)
{
    return DeviceNode{name, value, {}};
}

DeviceNode group(const std::string &name, std::vector<DeviceNode> children)
{
    return DeviceNode{name, std::string(), std::move(children)};
}

//"8ch", "pan4ch" or "4ch" from GSCFG0's flags (as the core's board: 8CHANS before PAN4CH)
std::string audio_mode(const std::string &flags)
{
    auto has = [&flags](const char * f) { return (" " + flags + " ").find(std::string(" ") + f + " ") != std::string::npos; };
    return has("8_channels") ? "8ch" : has("pan4ch") ? "pan4ch" : "4ch";
}

//B3.1: the Hardware tab on a NeoGS (the GS board stays for a GS: the report as it is)
bool neogs_board_report(const DeviceNode &r, DeviceNode &out)
{
    const DeviceNode * ngs = part(r, "neogs");
    if (ngs == nullptr) return false;
    const DeviceNode &n = *ngs;
    if (const DeviceNode * strip = part(r, "strip")) out.children.push_back(*strip);

    const std::string flags = text(n, "gscfg0_flags");
    std::string chips;
    auto chip = [&chips](const std::string &c) { chips += (chips.empty() ? "" : " · ") + c; };
    static const std::vector<std::pair<const char *, const char *>> kChips = {
        {"ram_mode", "RAM mode"}, {"rom_mode", "ROM mode"}};
    static const std::vector<std::pair<const char *, const char *>> kExtras = {
        {"ramro", "RAMRO"}, {"expag", "EXPAG"}, {"pan4ch", "PAN4CH"}, {"inv7b", "INV7B"}};
    const std::string spaced = " " + flags + " ";
    for (const auto &c : kChips)
        if (spaced.find(std::string(" ") + c.first + " ") != std::string::npos) chip(c.second);
    chip(spaced.find(" 8_channels ") != std::string::npos ? "8 ch" : "4 ch");
    chip(mhz(number(n, "clock_hz")));
    for (const auto &c : kExtras)
        if (spaced.find(std::string(" ") + c.first + " ") != std::string::npos) chip(c.second);
    out.children.push_back(group("Config", {leaf("GSCFG0", hex(number(n, "gscfg0"), 2)), leaf("chips", chips)}));

    std::vector<DeviceNode> paging;
    std::string pages;
    for (int w = 0; w < 4; w++) {
        const std::string at = "windows.[" + std::to_string(w) + "]";
        const long long page = number(n, at + ".page");
        const bool flash = truth(n, at + ".flash");
        pages += (w ? " " : "") + hex(page, 2).substr(1);
        paging.push_back(leaf("PG" + std::to_string(w) + " " + hex(w * 0x4000, 4), std::string(flash ? "FL " : "RAM ") + hex(page, 2)));
    }
    paging.insert(paging.begin(), leaf("PG0-PG3", pages));
    paging.push_back(leaf("MPAG", hex(number(r, "page"), 2)));
    out.children.push_back(group("Paging", std::move(paging)));

    out.children.push_back(group("Clock", {leaf("clock", mhz(number(n, "clock_hz")))}));
    const DeviceNode * channels = part(r, "channels");
    out.children.push_back(group("DAC", {leaf("mode", audio_mode(flags)),
                                         leaf("channels", std::to_string(channels ? channels->children.size() : 0) + ", in the table")}));
    out.children.push_back(group("Output", {leaf("listening", text(n, "stereo_mode"))}));

    //TIM_FREQ picks a falling edge of the 4.8 MHz counter: bits 6, 7, 8, 9, 10, 12, 14, 16 (the core's board)
    static const int kTimerBits[8] = {6, 7, 8, 9, 10, 12, 14, 16};
    const long long tim = number(n, "tim_freq");
    std::string rate = "-";
    if (tim >= 0) {
        char b[48];
        std::snprintf(b, sizeof(b), "%lld (%g Hz)", tim, 4800000.0 / double(2 << kTimerBits[tim & 7]));
        rate = b;
    }
    static const std::vector<std::string> kInts = {"timer", "sd_dma", "mp3_dma"};
    out.children.push_back(group("Interrupts", {leaf("INTENA", bits(number(n, "int_enable"), kInts)),
                                                leaf("INTREQ", bits(number(n, "int_request"), kInts)), leaf("TIM_FREQ", rate)}));
    out.children.push_back(group("SPI", {leaf("SCTRL", bits(number(n, "sctrl"), {"SD_nCS", "MC_nCS", "XRESET", "MCSPD0", "MDHLF", "MCSPD1"}))}));
    out.children.push_back(group("LED, firmware", {leaf("LED", yes(truth(n, "led_on"), "on", "off")), leaf("ready", yes(truth(n, "ready"))),
                                                   leaf("flash", text(n, "flash")), leaf("modified", yes(truth(n, "flash_modified")))}));
    out.children.push_back(group("Mailbox", {leaf("status", hex(number(r, "status"), 2)),
                                             leaf("command pending", text(r, "command_pending")),
                                             leaf("data pending", text(r, "data_pending")),
                                             leaf("command from host", hex(number(r, "command_from_host"), 2)),
                                             leaf("data from host", hex(number(r, "data_from_host"), 2)),
                                             leaf("data to host", hex(number(r, "data_to_host"), 2)),
                                             leaf("command queue", text(r, "command_queue_count")),
                                             leaf("data queue", text(r, "data_queue_count"))}));
    out.children.push_back(group("Card", {leaf("device", text(r, "device")), leaf("firmware", text(r, "firmware")),
                                          leaf("RAM", text(r, "ram_kb") + " KB")}));
    if (channels != nullptr) out.children.push_back(*channels);
    return true;
}

//B3.2: the three DMA modules (a row each) and the ZX-DMA block
bool neogs_dma_report(const DeviceNode &r, DeviceNode &out)
{
    const DeviceNode * dma = part(r, "neogs.dma");
    if (dma == nullptr) return false;
    const DeviceNode &d = *dma;
    static const char * const kModules[3] = {"zx", "sd", "mp3"};
    const long long select = number(d, "select");
    DeviceNode rows{"modules", std::string(), {}};
    for (int m = 0; m < 3; m++) {
        const long long address = number(d, std::string(kModules[m]) + ".address");
        DeviceNode row{"[" + std::to_string(m) + "]", std::string(), {}};
        row.children = {leaf("module", kModules[m]), leaf("sel", select == m ? "●" : ""),
                        leaf("run", truth(d, std::string(kModules[m]) + ".running") ? "●" : "○"),
                        leaf("address", hex(address, 6)), leaf("page", address < 0 ? "-" : std::to_string(address >> 14))};
        rows.children.push_back(std::move(row));
    }
    std::string watch = text(d, "zx.watch_setting") + " · " + text(d, "zx.watch_frames") + " frames · ";
    const long long left = number(d, "zx.watch_frames_left");
    watch += left < 0 ? "closed" : std::to_string(left) + " left";
    const std::string pending = text(d, "zx.pending");
    std::vector<DeviceNode> zx = {
        leaf("mode", text(d, "zx.mode")),
        leaf("watch", watch),
        leaf("overlay", yes(truth(d, "zx.overlay_installed"), "installed", "not installed")),
        leaf("host memory bus", yes(truth(d, "zx.host_memory_bus"))),
        leaf("latch", hex(number(d, "zx.read_latch"), 2)),
        leaf("pending", pending.empty() || pending == "none" ? std::string("none") : pending + " " + hex(number(d, "zx.pending_address"), 6)),
        leaf("waits", grouped(number(d, "zx.wait_tstates")) + " T"),
        leaf("late starts", grouped(number(d, "zx.late_starts")) + " (" + grouped(number(d, "zx.late_start_ticks")) + " card ticks)"),
        leaf("read", grouped(number(d, "zx.bytes_read")) + " B"),
        leaf("written", grouped(number(d, "zx.bytes_written")) + " B"),
        leaf("dropped", grouped(number(d, "zx.bytes_dropped")) + " B"),
    };
    if (const DeviceNode * note = part(d, "zx.note")) zx.push_back(leaf("note", note->value));
    out.children.push_back(leaf("DMA_MOD", select >= 0 && select < 3 ? std::string(kModules[select]) : std::to_string(select)));
    out.children.push_back(group("ZX-DMA", std::move(zx)));
    out.children.push_back(std::move(rows));
    return true;
}

std::string size_text(long long bytes)
{
    if (bytes < 0) return "-";
    char b[48];
    if (bytes >= (1LL << 30)) std::snprintf(b, sizeof(b), "%.1f GB", (double)bytes / double(1LL << 30));
    else if (bytes >= (1LL << 20)) std::snprintf(b, sizeof(b), "%.1f MB", (double)bytes / double(1LL << 20));
    else std::snprintf(b, sizeof(b), "%lld KB", bytes >> 10);
    return std::string(b) + " (" + grouped(bytes) + " B)";
}

//B3.3: the SD card, the MP3 decoder, the flash chip
bool neogs_media_report(const DeviceNode &r, DeviceNode &out)
{
    const DeviceNode * ngs = part(r, "neogs");
    if (ngs == nullptr) return false;
    const DeviceNode &n = *ngs;
    std::vector<DeviceNode> sd;
    if (truth(n, "sd.present")) {
        sd = {leaf("medium", text(n, "sd.path")), leaf("type", truth(n, "sd.sdhc") ? "SDHC" : "SDSC"),
              leaf("size", size_text(number(n, "sd.size_bytes"))), leaf("blocks read", grouped(number(n, "sd.blocks_read"))),
              leaf("blocks written", grouped(number(n, "sd.blocks_written")))};
    } else {
        sd = {leaf("medium", "no card")};
    }
    out.children.push_back(group("SD (slot sd.ngs)", std::move(sd)));

    std::vector<DeviceNode> mp3;
    if (truth(n, "mp3.fitted")) {
        const long long channels = number(n, "mp3.channels");
        const long long seconds = number(n, "mp3.decode_time_s");
        char time[32] = "-";
        if (seconds >= 0) std::snprintf(time, sizeof(time), "%02lld:%02lld", seconds / 60, seconds % 60);
        mp3 = {leaf("chip", text(n, "mp3.chip")), leaf("DREQ", yes(truth(n, "mp3.dreq"), "on", "off")),
               leaf("rate", grouped(number(n, "mp3.rate")) + " Hz"),
               leaf("channels", channels == 2 ? std::string("stereo") : channels == 1 ? std::string("mono") : std::to_string(channels)),
               leaf("decode time", time), leaf("input", grouped(number(n, "mp3.input_fill")) + " / 2,048"),
               leaf("frames", grouped(number(n, "mp3.frames")))};
    } else {
        mp3 = {leaf("chip", "not fitted")};
    }
    out.children.push_back(group("MP3", std::move(mp3)));
    out.children.push_back(group("Flash", {leaf("title", text(n, "flash")), leaf("modified", yes(truth(n, "flash_modified")))}));
    return true;
}

//The core's device boards (DebugBoards: neogs, neogs_dma, neogs_sd, neogs_mp3, neogs_flash, gs_stats), the same
//data the embedded debugger shows: a board group a tree group (its register next to the title), a control a field
typedef Emulator::Board Board;
typedef Emulator::BoardControl Control;

std::string control_text(const Control &c)
{
    char * end = nullptr;
    const long long v = std::strtoll(c.value.c_str(), &end, 10);
    const bool integer = !c.value.empty() && end != nullptr && *end == 0;
    if (c.type == "hex8" || c.type == "hex16" || c.type == "hex21")
        return integer ? hex(v, c.type == "hex8" ? 2 : c.type == "hex16" ? 4 : 6) : c.value;
    if (c.type == "bits") return integer ? bits(v, c.names) : c.value;
    if (c.type == "bool") return yes(c.value == "true");
    if (c.type == "led") return yes(c.value == "true", "on", "off");
    if (c.type == "bar") return c.value + " / " + std::to_string(c.max);
    if (c.type == "counter") return integer ? grouped(v) : c.value;
    if (c.type == "rate") {
        char b[32];
        std::snprintf(b, sizeof(b), "%.2f", std::strtod(c.value.c_str(), nullptr));
        return b;
    }
    if (integer && c.id.size() >= 8 && c.id.compare(c.id.size() - 8, 8, "clock_hz") == 0) return mhz(v);
    return c.value;
}

DeviceNode board_group(const Emulator::BoardGroup &g, const std::string &title, const std::function<bool(const Control &)> &keep)
{
    DeviceNode n{title.empty() ? g.title : title, g.raw, {}};
    for (const Control &c : g.controls)
        if (!keep || keep(c)) n.children.push_back(leaf(c.label, control_text(c)));
    return n;
}

const Emulator::BoardGroup * board_group_titled(const Board &b, const std::string &title)
{
    for (const Emulator::BoardGroup &g : b.groups)
        if (g.title == title) return &g;
    return nullptr;
}

//The statistics board's counters of one section ("mailbox", "sd"): a group of their own; while the statistics wait for
//a frame end (a running machine's first read), that said
void stats_counters(DeviceNode &out, const Board &stats, const char * section, const std::string &title)
{
    if (!stats.ok || !stats.available) {
        const Control * status = stats.control("status");
        out.children.push_back(group(title, {leaf("statistics", status ? status->value : stats.message)}));
        return;
    }
    if (const Emulator::BoardGroup * g = board_group_titled(stats, section)) {
        DeviceNode n = board_group(*g, title, [](const Control &c) { return c.type == "counter"; });
        if (!n.children.empty()) out.children.push_back(std::move(n));
    }
}

std::string stat(const Board &stats, const std::string &id)
{
    const Control * c = stats.ok && stats.available ? stats.control(id) : nullptr;
    return c ? control_text(*c) : std::string("-");
}

//B3.1 from the neogs board: every group (the DAC's channels in the table, from the report), the clock's switch count
//and the mailbox, interrupt and SPI counters from the statistics
bool neogs_board(Emulator * e, const DeviceNode &r, DeviceNode &out)
{
    if (part(r, "neogs") == nullptr) return false;
    const Board b = e->read_board("neogs");
    if (!b.ok) return neogs_board_report(r, out);
    const Board stats = e->read_board("gs_stats");
    if (const DeviceNode * strip = part(r, "strip")) out.children.push_back(*strip);
    const DeviceNode * channels = part(r, "channels");
    for (const Emulator::BoardGroup &g : b.groups) {
        DeviceNode n = board_group(g, std::string(), [](const Control &c) { return c.id.rfind("channels[", 0) != 0; });
        if (g.title == "DAC") n.children.push_back(leaf("channels", std::to_string(channels ? channels->children.size() : 0) + ", in the table"));
        if (g.title == "Clock") n.children.push_back(leaf("switches", stat(stats, "card.clock_switches")));
        out.children.push_back(std::move(n));
    }
    stats_counters(out, stats, "mailbox", "Mailbox counters");
    stats_counters(out, stats, "interrupts", "Interrupt counters");
    stats_counters(out, stats, "spi", "SPI counters");
    if (channels != nullptr) out.children.push_back(*channels);
    return true;
}

//B3.2 from the neogs_dma board and the statistics' dma section: a row a module, the ZX-DMA block, the DMA rates and
//errors
bool neogs_dma(Emulator * e, const DeviceNode &r, DeviceNode &out)
{
    if (part(r, "neogs") == nullptr) return false;
    const Board b = e->read_board("neogs_dma");
    if (!b.ok) return neogs_dma_report(r, out);
    const Board stats = e->read_board("gs_stats");
    static const char * const kModules[3] = {"zx", "sd", "mp3"};
    DeviceNode rows{"modules", std::string(), {}};
    for (int m = 0; m < 3; m++) {
        const std::string board = std::string("modules[") + kModules[m] + "].";
        const std::string dma = std::string("dma.") + kModules[m] + ".";
        auto value = [&b](const std::string &id) {
            const Control * c = b.control(id);
            return c ? control_text(*c) : std::string("-");
        };
        const Control * selected = b.control(board + "selected");
        const Control * running = b.control(board + "running");
        std::string bytes = stat(stats, dma + "bytes"), last = "-";
        if (m == 0 && stats.ok && stats.available)
            bytes = "r " + stat(stats, dma + "bytes_read") + " · w " + stat(stats, dma + "bytes_written");
        if (const Control * at = stats.available ? stats.control(dma + "last.address") : nullptr) {
            char text[16];
            std::snprintf(text, sizeof(text), "#%06llX", std::strtoll(at->value.c_str(), nullptr, 10));
            last = std::string(text) + " · " + stat(stats, dma + "last.bytes") + " B";
        }
        DeviceNode row{"[" + std::to_string(m) + "]", std::string(), {}};
        row.children = {leaf("module", kModules[m]), leaf("sel", selected && selected->value == "true" ? "●" : ""),
                        leaf("run", running && running->value == "true" ? "●" : "○"), leaf("address", value(board + "address")),
                        leaf("page", value(board + "page")), leaf("starts", stat(stats, dma + "starts")),
                        leaf("done", stat(stats, dma + "completions")), leaf("bytes", bytes),
                        leaf("errors", stat(stats, dma + "errors")), leaf("aborts", stat(stats, dma + "aborts")),
                        leaf("stall clk", stat(stats, dma + "stall_clocks")), leaf("last", last)};
        rows.children.push_back(std::move(row));
    }
    for (const Emulator::BoardGroup &g : b.groups)
        if (g.title.size() < 7 || g.title.compare(g.title.size() - 7, 7, " module") != 0) out.children.push_back(board_group(g, std::string(), nullptr));
    if (stats.ok && stats.available) {
        out.children.push_back(group("DMA rates", {leaf("ZX-DMA", stat(stats, "rates.zx_dma_bytes") + " B/s"),
                                                   leaf("SD DMA", stat(stats, "rates.sd_dma_bytes") + " B/s"),
                                                   leaf("MP3 DMA", stat(stats, "rates.mp3_dma_bytes") + " B/s")}));
        out.children.push_back(group("DMA errors", {leaf("aborts", stat(stats, "errors.dma_aborts")),
                                                    leaf("SD DMA errors", stat(stats, "errors.sd_dma_errors")),
                                                    leaf("ZX bytes dropped", stat(stats, "errors.zx_bytes_dropped"))}));
    } else {
        stats_counters(out, stats, "dma", "DMA counters");
    }
    out.children.push_back(std::move(rows));
    return true;
}

//B3.3 from the neogs_sd, neogs_mp3 and neogs_flash boards, with their counters from the statistics
bool neogs_media(Emulator * e, const DeviceNode &r, DeviceNode &out)
{
    if (part(r, "neogs") == nullptr) return false;
    const Board sd = e->read_board("neogs_sd"), mp3 = e->read_board("neogs_mp3"), flash = e->read_board("neogs_flash");
    if (!sd.ok || !mp3.ok || !flash.ok) return neogs_media_report(r, out);
    const Board stats = e->read_board("gs_stats");
    for (const Emulator::BoardGroup &g : sd.groups) out.children.push_back(board_group(g, "SD: " + g.title, nullptr));
    stats_counters(out, stats, "sd", "SD counters");
    for (const Emulator::BoardGroup &g : mp3.groups) out.children.push_back(board_group(g, "MP3: " + g.title, nullptr));
    if (!mp3.available) out.children.push_back(group("MP3: Decoder", {leaf("fitted", "no")}));
    stats_counters(out, stats, "mp3", "MP3 counters");
    for (const Emulator::BoardGroup &g : flash.groups) out.children.push_back(board_group(g, std::string(), nullptr));
    stats_counters(out, stats, "flash", "Flash counters");
    return true;
}

//A10, W2: the lightweight player's interpreter, read-only: the state diagram with the current state marked, the ready
//line, the command, the byte counts, the two queues of 16 and the upload's progress
std::string capital(std::string s)
{
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

std::string hz(const std::string &value)
{
    if (value.empty()) return "-";
    const double v = std::strtod(value.c_str(), nullptr);
    return v <= 0 ? std::string("-") : grouped((long long)std::lround(v));
}

bool lw_interpreter_report(const DeviceNode &r, DeviceNode &out)
{
    const DeviceNode * at = part(r, "interpreter");
    if (at == nullptr) return false;
    const DeviceNode &n = *at;
    if (const DeviceNode * strip = part(r, "strip")) out.children.push_back(*strip);
    const std::string state = text(n, "state");
    std::string diagram;
    static const char * const kStates[5] = {"idle", "command", "params", "upload", "reply"};
    for (const char * s : kStates)
        diagram += (diagram.empty() ? "" : "   ") + std::string(state == s ? "● " : "○ ") + capital(s);
    if (state == "covox") diagram += "   ● Covox";
    out.children.push_back(group("Interpreter", {leaf("state", capital(state)), leaf("diagram", diagram), leaf("text", text(n, "text"))}));

    const long long command = number(n, "command");
    const std::string name = text(n, "command_name");
    out.children.push_back(group("Command", {leaf("command", hex(command, 2) + (name.empty() ? std::string() : " " + name)),
                                             leaf("expected", grouped(number(n, "expected"))),
                                             leaf("received", grouped(number(n, "received"))),
                                             leaf("selector expected", yes(truth(n, "selector_expected")))}));
    const std::string capacity = std::to_string(number(n, "queue_capacity", 16));
    out.children.push_back(group("Queues", {leaf("params", std::to_string(number(n, "params_queued", 0)) + " / " + capacity),
                                            leaf("replies", std::to_string(number(n, "replies_queued", 0)) + " / " + capacity),
                                            leaf("reply pending", yes(truth(n, "reply_pending"))),
                                            leaf("next reply", part(n, "reply_next") ? hex(number(n, "reply_next"), 2) : std::string("-"))}));
    const long long received = number(n, "upload.received", 0), total = number(n, "upload.total", 0);
    std::string progress = grouped(received) + (total > 0 ? " of " + grouped(total) : std::string()) + " bytes";
    if (total > 0) progress += " (" + std::to_string(received * 100 / total) + " %)";
    out.children.push_back(group("Upload", {leaf("active", yes(truth(n, "upload.active"))), leaf("kind", text(n, "upload.kind")),
                                            leaf("progress", progress)}));
    return true;
}

//A10, W3: the player: the song, the module row of the Unreal GS dialog ((P) / (S) and its name), the four channels and
//the samples (number, name, volume, note, priority, rate, length, the current effect `*`, the loop `(L)`), the FX
bool lw_player_report(const DeviceNode &r, DeviceNode &out)
{
    const DeviceNode * at = part(r, "player");
    if (at == nullptr) return false;
    const DeviceNode &p = *at;
    const long long length = number(p, "song_length", 0);
    out.children.push_back(group("Song", {leaf("state", capital(text(p, "state"))), leaf("playing", yes(truth(p, "playing"))),
                                          leaf("position", std::to_string(number(p, "position", 0)) + " / " + std::to_string(length)),
                                          leaf("pattern", std::to_string(number(p, "pattern", 0)) + " of " + std::to_string(number(p, "pattern_count", 0))),
                                          leaf("row", std::to_string(number(p, "row", 0))), leaf("tick", std::to_string(number(p, "tick", 0))),
                                          leaf("speed", std::to_string(number(p, "speed", 0))), leaf("BPM", std::to_string(number(p, "bpm", 0))),
                                          leaf("MTSTAT", hex(number(p, "mtstat"), 2))}));
    std::vector<DeviceNode> module;
    if (const DeviceNode * modules = part(p, "modules"))
        for (const DeviceNode &m : modules->children)
            module.push_back(leaf(std::to_string(number(m, "number", 0)) + " " + text(m, "marker"),
                                  text(m, "name") + ", " + grouped(number(m, "length")) + " bytes" + (truth(m, "current") ? ", current" : "")));
    if (module.empty()) module.push_back(leaf("module", "none loaded"));
    out.children.push_back(group("Module", std::move(module)));
    out.children.push_back(group("FX", {leaf("count", std::to_string(number(p, "fx.count", 0))),
                                        leaf("current", std::to_string(number(p, "fx.current", 0))),
                                        leaf("stored", yes(truth(p, "fx.stored")))}));

    DeviceNode channels{"channels", std::string(), {}};
    if (const DeviceNode * list = part(p, "channels"))
        for (const DeviceNode &c : list->children) {
            DeviceNode row{"[" + std::to_string(channels.children.size()) + "]", std::string(), {}};
            row.children = {leaf("channel", text(c, "channel")), leaf("sample", text(c, "sample")), leaf("volume", text(c, "volume")),
                            leaf("period", text(c, "period")), leaf("note", text(c, "note")), leaf("rate Hz", hz(text(c, "rate_hz")))};
            channels.children.push_back(std::move(row));
        }
    out.children.push_back(std::move(channels));
    DeviceNode samples{"samples", std::string(), {}};
    if (const DeviceNode * list = part(p, "samples"))
        for (const DeviceNode &s : list->children) {
            DeviceNode row{"[" + std::to_string(samples.children.size()) + "]", std::string(), {}};
            const long long channel = number(s, "channel");
            row.children = {leaf("number", text(s, "number")), leaf("name", text(s, "name")), leaf("volume", text(s, "volume")),
                            leaf("note", text(s, "note")), leaf("priority", text(s, "priority")), leaf("rate Hz", hz(text(s, "rate_hz"))),
                            leaf("channel", channel < 0 ? std::string("-") : std::to_string(channel)),
                            leaf("length", grouped(number(s, "length"))), leaf("effect", truth(s, "effect") ? "*" : ""),
                            leaf("loop", truth(s, "loop") ? "(L)" : "")};
            samples.children.push_back(std::move(row));
        }
    out.children.push_back(std::move(samples));
    return true;
}

QString board_answer(Emulator * e, const char * board, const char * action, const std::string &arg = std::string())
{
    const Emulator::BoardAnswer a = e->board_action(board, action, arg);
    return QString::fromStdString(a.ok ? a.message : "refused: " + a.message);
}
}

std::vector<NextTab> CreateCardTabs(Emulator * e, QWidget *parent)
{
    using namespace std::placeholders;
    NextReportView * hardware = new NextReportView(e, "gs", {}, "channels", {}, parent);
    hardware->set_shape(std::bind(neogs_board, e, _1, _2));
    NextReportView * dma = new NextReportView(e, "gs", {}, "modules",
                                              {"module", "sel", "run", "address", "page", "starts", "done", "bytes", "errors", "aborts",
                                               "stall clk", "last"},
                                              parent);
    dma->set_shape(std::bind(neogs_dma, e, _1, _2), "neogs", QObject::tr("No NeoGS card"));
    NextReportView * media = new NextReportView(e, "gs", {}, std::string(), {}, parent);
    media->set_shape(std::bind(neogs_media, e, _1, _2), "neogs", QObject::tr("No NeoGS card"));
    media->add_action(QObject::tr("Insert image..."), [e, media]() {
        const QString file = QFileDialog::getOpenFileName(media, QObject::tr("SD card image"));
        return file.isEmpty() ? QString() : board_answer(e, "neogs_sd", "insert", file.toStdString());
    });
    media->add_action(QObject::tr("Insert folder..."), [e, media]() {
        const QString folder = QFileDialog::getExistingDirectory(media, QObject::tr("A host folder as the SD card"));
        return folder.isEmpty() ? QString() : board_answer(e, "neogs_sd", "insert", folder.toStdString());
    });
    media->add_action(QObject::tr("Eject"), [e]() { return board_answer(e, "neogs_sd", "eject"); });
    media->add_action(QObject::tr("Save flash"), [e]() { return board_answer(e, "neogs_flash", "save"); });
    NextReportView * interpreter = new NextReportView(e, "gs", {}, std::string(), {}, parent);
    interpreter->set_shape(&lw_interpreter_report, "interpreter", QObject::tr("Not the lightweight player"));
    NextReportView * player = new NextReportView(e, "gs", {}, "channels", {"channel", "sample", "volume", "period", "note", "rate Hz"}, parent);
    player->add_table("samples", {"number", "name", "volume", "note", "priority", "rate Hz", "channel", "length", "effect", "loop"});
    player->set_shape(&lw_player_report, "player", QObject::tr("Not the lightweight player"));
    NextReportView * log = new NextReportView(e, "gs-commands", {}, "records", {"frame", "t", "kind", "command", "name", "text"}, parent);
    NextReportView * protocol = new NextReportView(e, "gs", {"protocol", "last_send", "firmware_profile", "status", "command_pending",
                                                           "data_pending", "command_from_host", "data_from_host", "data_to_host"},
                                                   std::string(), {}, parent);
    NextReportView * firmware = new NextReportView(e, "gs-firmware", {}, "window_variables", {"name", "at", "size", "value", "meaning"}, parent);
    NextReportView * stats = new NextReportView(e, "gs-stats", {}, std::string(), {}, parent);
    std::vector<NextTab> tabs = named({
        {QObject::tr("GS log"), log},
        {QObject::tr("GS protocol"), protocol},
        {QObject::tr("GS firmware"), firmware},
        {QObject::tr("GS hardware"), hardware},
        {QObject::tr("GS stats"), stats},
        {QObject::tr("NeoGS DMA"), dma},
        {QObject::tr("SD · MP3 · Flash"), media},
        {QObject::tr("GS interpreter"), interpreter},
        {QObject::tr("GS player"), player},
    }, "card");
    dma->setObjectName("card:neogs-dma");
    media->setObjectName("card:neogs-media");
    interpreter->setObjectName("card:lw-interpreter");
    player->setObjectName("card:lw-player");
    //The card each view reads: the General Sound's reports, or the MultiSound's GS (the multisound report's gs part,
    //its own command log, firmware and statistics read with cpu ms); NeoGS and LW views only on the General Sound
    for (NextTab &t : tabs) t.view->add_source("gs", t.view->report());
    log->add_source("ms", "ms-gs-commands");
    protocol->add_source("ms", "multisound", "gs");
    firmware->add_source("ms", "ms-gs-firmware");
    hardware->add_source("ms", "multisound", "gs");
    stats->add_source("ms", "ms-gs-stats");
    for (NextTab &t : tabs)
        if (t.view->needs().empty()) t.view->set_absent(QObject::tr("No General Sound card"));
    return tabs;
}

std::string CardTabsCard(Emulator * e, const std::string &front)
{
    bool gs = false, ms = false;
    if (e->loaded && e->dm != nullptr)
        for (unsigned int i = 0; i < e->dm->device_count; i++) {
            ComputerDevice * d = e->dm->get_device(i)->device.get();
            if (d->type != "report") continue;
            gs = gs || d->name == "gs";
            ms = ms || d->name == "ms-gs-stats";
        }
    if (gs && ms) return front == "ms" ? "ms" : "gs";
    return ms ? "ms" : gs ? "gs" : std::string();
}

bool CardTabsFollow(const std::vector<NextTab> &tabs, const std::string &card)
{
    const QString caption = card == "ms" ? QObject::tr("The ZX-MultiSound's General Sound (ms)")
                          : card == "gs" ? QObject::tr("The General Sound card (gs)") : QString();
    bool changed = false;
    for (const NextTab &t : tabs) changed = t.view->use_source(card.empty() ? std::string("gs") : card, caption) || changed;
    return changed;
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
