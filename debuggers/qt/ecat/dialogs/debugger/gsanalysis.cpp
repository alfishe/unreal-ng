// unreal-debugger (not in ecat): the GS analysis tabs (gui-card-debugger A5.2a Dispatch, Unknown, Objects, Writers)

#include "gsanalysis.h"

#include <QAbstractButton>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

#include "addressmenu.h"
#include "debugstyle.h"

namespace
{
QString hex2(unsigned int v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X", v & 0xFF);
    return QString(b);
}

QString hex4(unsigned int v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%04X", v & 0xFFFF);
    return QString(b);
}

QString grouped(uint64_t v)
{
    QString digits = QString::number((qulonglong)v);
    for (int i = digits.size() - 3; i > 0; i -= 3) digits.insert(i, ',');
    return digits;
}

const QString kDot = QString(" ") + QChar(0x00B7) + " ";
const QString kStep = QString(" ") + QChar(0x25B8) + " ";

QTableWidgetItem * cell(const QString &text, bool right = false)
{
    QTableWidgetItem * item = new QTableWidgetItem(text);
    if (right) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

//"#4000" -> 0x4000; false for a page place ("ram 2:#0100")
bool window_address(const std::string &s, unsigned int &out)
{
    if (s.size() < 2 || s[0] != '#') return false;
    bool ok = false;
    out = QString::fromStdString(s.substr(1)).toUInt(&ok, 16);
    return ok;
}
}

//------------------------------------------------------------------- GsCardTab

GsCardTab::GsCardTab(Emulator * e, const char * name, QWidget *parent):
    QWidget(parent),
    e(e)
{
    setObjectName(name);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(3);
    m_caption = new QLabel(this);
    m_caption->setObjectName("caption");
    m_caption->hide();
    m_layout->addWidget(m_caption);
    m_line = new QLabel(this);
    m_line->setObjectName("gscard:line");
    m_line->setWordWrap(true);
    m_line->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GsCardTab::on_timer);
}

void GsCardTab::set_card(const std::string &card, const QString &caption)
{
    m_caption->setText(caption);
    m_caption->setVisible(!caption.isEmpty());
    if (card == m_card) return;
    m_card = card;
    restart();
}

void GsCardTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer->start((int)e->live_card_ms());
}

void GsCardTab::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

//While the machine runs; its stops are the window's (refreshed once there). Not while minimized
void GsCardTab::on_timer()
{
    if (!e->loaded || !e->debug_online() || window()->isMinimized()) return;
    if (e->debug_paused() && !e->debug_stepping()) return;
    refresh();
}

QString GsCardTab::profile_title(const std::string &id)
{
    if (id.empty()) return QString();
    if (m_titles.empty())
        for (const Emulator::GsProfileInfo &p : e->gs_firmware_profiles()) m_titles[p.id] = p.title;
    auto it = m_titles.find(id);
    return QString::fromStdString(it != m_titles.end() && !it->second.empty() ? it->second : id);
}

QTableWidget * GsCardTab::make_table(const char * name, const QStringList &columns)
{
    QTableWidget * t = new QTableWidget(this);
    t->setObjectName(name);
    t->setFont(dbg::font());
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(dbg::row_height(t->fontMetrics()));
    t->setColumnCount(columns.size());
    t->setHorizontalHeaderLabels(columns);
    t->horizontalHeader()->setStretchLastSection(true);
    if (QAbstractButton * corner = t->findChild<QAbstractButton *>()) corner->setProperty("dbg_mono", true);
    return t;
}

//------------------------------------------------------------------- Dispatch

GsDispatchView::GsDispatchView(Emulator * e, QWidget *parent):
    GsCardTab(e, "card:gs-dispatch", parent)
{
    m_stack = new QLabel(this);
    m_stack->setObjectName("gsdispatch:stack");
    m_stack->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_layout->addWidget(m_stack);
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_named = new QCheckBox(tr("Named and patched only"), this);
    m_named->setObjectName("gsdispatch:named");
    m_named->setToolTip(tr("Hide the entries that go to the unknown-command stub (or that the profile names no command for)"));
    connect(m_named, &QCheckBox::toggled, this, [this]() { refresh(); });
    row->addWidget(m_named, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_table = make_table("gsdispatch:table", QStringList() << tr("Command") << tr("Name") << tr("Handler") << tr("Label")
                                                              << tr("Baseline") << tr("Patched by"));
    m_layout->addWidget(m_table, 1);
}

void GsDispatchView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    const Emulator::GsFirmware f = e->read_gs_firmware(m_card, std::string());
    if (!f.ok) {
        m_stack->setText(QString());
        m_line->setText(QString::fromStdString(f.message));
        m_table->setRowCount(0);
        return;
    }
    //The profile stack (M5): the ROM's at the bottom, the uploaded code's the decoder took on top
    const QString rom = !f.rom_profile.empty() ? profile_title(f.rom_profile) : profile_title(f.profile);
    QString stack = tr("Profile stack: %1").arg(rom.isEmpty() ? tr("no profile") : rom);
    if (f.log_on && !f.top_profile.empty() && f.top_profile != f.rom_profile && f.top_profile != (f.rom_profile.empty() ? f.profile : f.rom_profile))
        stack += kStep + profile_title(f.top_profile);
    if (!f.log_on) stack += kDot + tr("the uploaded code's profile shows while the command log records");
    m_stack->setText(stack);

    if (!f.has_dispatch) {
        m_line->setText(tr("Profile %1 declares no dispatch table").arg(profile_title(f.profile)));
        m_table->setRowCount(0);
        return;
    }
    bool any_readable = false;
    for (const Emulator::GsDispatchEntry &d : f.entries) any_readable = any_readable || d.readable;
    if (!any_readable) {
        m_line->setText(tr("profile %1 declares the table %2, but the card has no memory to read it from (the lightweight "
                           "player keeps no firmware RAM or ROM)").arg(profile_title(f.profile), QString::fromStdString(f.table)));
        m_table->setRowCount(0);
        return;
    }
    //The core reads the tables where the firmware keeps them (a GS's ROM, a NeoGS's RAM copy), whatever window 3
    //maps now, against the firmware as it started: an entry differs from its baseline only when patched
    m_line->setText(tr("table %1%2").arg(QString::fromStdString(f.table),
                                         f.table_high.empty() ? QString() : " / " + QString::fromStdString(f.table_high)) +
                    kDot + tr("%1 entries").arg(f.entries.size()) + kDot + tr("%1 patched").arg(f.patched) + kDot +
                    tr("%1 to the unknown stub or unnamed").arg(f.unknown) + kDot + tr("profile %1").arg(profile_title(f.profile)));

    //The table's slot of an entry, for the upload that wrote it: COMTAB words for #00-#1F and #F0-#FF, COMTABH low
    //bytes for #20-#EF (the high bytes 256 later); a word table a word per entry in order
    unsigned int table = 0, table_high = 0;
    const bool window = window_address(f.table, table);
    const bool split = window_address(f.table_high, table_high);
    auto slot = [&](const Emulator::GsDispatchEntry &d, size_t index, unsigned int &out) {
        if (!window) return false;
        if (split) {
            const int c = d.command;
            out = c < 0x20 ? table + 2 * c : c >= 0xF0 ? table + 2 * (c - 0xD0) : table_high + (c - 0x20);
        } else {
            out = table + 2 * (unsigned int)index;
        }
        return true;
    };
    auto patched_by = [&](const Emulator::GsDispatchEntry &d, size_t index) -> QString {
        const Emulator::GsUpload * up = nullptr;
        unsigned int at = 0;
        const bool known = slot(d, index, at);
        for (auto it = f.uploads.rbegin(); it != f.uploads.rend() && up == nullptr; ++it) {
            if (!d.patched_by.empty() && it->profile == d.patched_by) up = &*it;
            else if (known && at >= it->address && at < it->address + it->length) up = &*it;
        }
        if (up == nullptr)
            return d.patched_by.empty() ? tr("no upload seen") : tr("%1 (no upload seen)").arg(profile_title(d.patched_by));
        const QString who = up->profile.empty() ? tr("user code") : profile_title(up->profile);
        return tr("%1 upload @%2, %3 B, sha %4..., frame %5")
            .arg(who, hex4(up->address), grouped(up->length), QString::fromStdString(up->sha256.substr(0, 8)))
            .arg((qlonglong)up->frame);
    };

    const dbg::Colors c = dbg::colors(palette());
    QColor tint = c.changed;
    tint.setAlpha(60);
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);
    for (size_t i = 0; i < f.entries.size(); i++) {
        const Emulator::GsDispatchEntry &d = f.entries[i];
        if (m_named->isChecked() && d.unknown && !d.patched) continue;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        const QString cells[ColCount] = {hex2((unsigned int)d.command),
                                         QString::fromStdString(d.name),
                                         d.readable ? hex4(d.handler) : tr("unreadable"),
                                         QString::fromStdString(d.label),
                                         d.has_baseline ? hex4(d.baseline) : QString(),
                                         d.patched ? patched_by(d, i) : QString()};
        for (int k = 0; k < ColCount; k++) {
            QTableWidgetItem * item = cell(cells[k]);
            if (d.patched) {
                item->setBackground(QBrush(tint));
                QFont bold = m_table->font();
                bold.setBold(true);
                item->setFont(bold);
            } else if (d.unknown) {
                item->setForeground(QBrush(c.dim));
            }
            if (k == ColHandler && d.unknown) item->setToolTip(tr("The unknown-command stub, or a code the profile names no command for"));
            m_table->setItem(r, k, item);
        }
        m_table->item(r, ColCommand)->setData(Qt::UserRole, d.patched);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setUpdatesEnabled(true);
}

//------------------------------------------------------------------- Unknown

GsUnknownView::GsUnknownView(Emulator * e, QWidget *parent):
    GsCardTab(e, "card:gs-unknown", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_turn_on = new QPushButton(tr("Turn on"), this);
    m_turn_on->setObjectName("gsunknown:turn-on");
    m_turn_on->setToolTip(tr("Record the host's commands (the command log, mode ring): the counts come with it"));
    m_turn_on->hide();
    connect(m_turn_on, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        this->e->gs_command_log(m_card, true);
        refresh();
    });
    row->addWidget(m_turn_on, 0, Qt::AlignTop);
    m_clear = new QPushButton(tr("Clear"), this);
    m_clear->setObjectName("gsunknown:clear");
    m_clear->setToolTip(tr("The counts back to zero in this tab (the core counts on: the command log keeps its own)"));
    connect(m_clear, &QPushButton::clicked, this, [this]() { clear_counts(); });
    row->addWidget(m_clear, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_table = make_table("gsunknown:table", QStringList() << tr("Command") << tr("Since Clear") << tr("Total") << tr("Dispatch"));
    m_layout->addWidget(m_table, 1);
}

void GsUnknownView::restart()
{
    m_totals.clear();
    m_base.clear();
    m_table->setRowCount(0);
}

void GsUnknownView::clear_counts()
{
    m_base = m_totals;
    refresh();
}

void GsUnknownView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    const Emulator::GsFirmware f = e->read_gs_firmware(m_card, std::string());
    if (!f.ok) {
        m_line->setText(QString::fromStdString(f.message));
        m_table->setRowCount(0);
        m_turn_on->hide();
        return;
    }
    m_turn_on->setVisible(!f.log_on);
    if (!f.unknown_present || !f.log_on) {
        m_line->setText(tr("The command log is off: the commands are counted while it records (Turn on)"));
        m_table->setRowCount(0);
        return;
    }
    std::map<int, uint64_t> totals(f.unknown_commands.begin(), f.unknown_commands.end());
    //Fewer than at Clear: the log started again, so did its counts
    for (const auto &b : m_base)
        if (totals[b.first] < b.second) {
            m_base.clear();
            break;
        }
    m_totals = totals;
    std::map<int, const Emulator::GsDispatchEntry *> dispatch;
    for (const Emulator::GsDispatchEntry &d : f.entries) dispatch[d.command] = &d;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);
    uint64_t since_all = 0;
    for (const auto &t : m_totals) {
        const uint64_t base = m_base.count(t.first) ? m_base[t.first] : 0;
        if (t.second <= base) continue;
        since_all += t.second - base;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        QString where;
        if (auto it = dispatch.find(t.first); it != dispatch.end() && it->second->readable) {
            const Emulator::GsDispatchEntry &d = *it->second;
            where = hex4(d.handler) + (d.label.empty() ? QString() : " " + QString::fromStdString(d.label));
            if (d.unknown) where += kDot + tr("the unknown-command stub");
        } else if (!f.has_dispatch) {
            where = tr("no dispatch table in the profile");
        }
        m_table->setItem(r, ColCommand, cell(hex2((unsigned int)t.first)));
        m_table->setItem(r, ColSince, cell(QString::fromUtf8("\xC3\x97") + grouped(t.second - base), true));
        m_table->setItem(r, ColTotal, cell(grouped(t.second), true));
        m_table->setItem(r, ColDispatch, cell(where));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setUpdatesEnabled(true);
    m_line->setText(tr("%1 commands profile %2 does not name, %3 sends since Clear")
                        .arg(m_table->rowCount())
                        .arg(profile_title(f.profile))
                        .arg(grouped(since_all)) +
                    kDot + tr("counted by the command log since it started"));
}

//------------------------------------------------------------------- Objects

GsObjectsView::GsObjectsView(Emulator * e, QWidget *parent):
    GsCardTab(e, "card:gs-objects", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel(tr("Read as"), this));
    m_picker = new QComboBox(this);
    m_picker->setObjectName("gsobjects:profile");
    m_picker->setToolTip(tr("The card's memory read through the active profile, or through another one that declares structs"));
    m_picker->addItem(tr("the active profile"), QString());
    connect(m_picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_last.clear();
        refresh();
    });
    row->addWidget(m_picker);
    row->addStretch(1);
    m_layout->addLayout(row);
    m_layout->addWidget(m_line);
    QScrollArea * scroll = new QScrollArea(this);
    scroll->setObjectName("gsobjects:scroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_body = new QWidget(scroll);
    m_body_layout = new QVBoxLayout(m_body);
    m_body_layout->setContentsMargins(0, 0, 0, 0);
    m_body_layout->setSpacing(3);
    m_variables_title = new QLabel(tr("Variables"), m_body);
    m_variables = make_table("gsobjects:variables", QStringList() << tr("Name") << tr("Kind") << tr("At") << tr("Value") << tr("Meaning"));
    m_variables->setParent(m_body);
    m_body_layout->addWidget(m_variables_title);
    m_body_layout->addWidget(m_variables);
    m_body_layout->addStretch(1);
    m_variables_title->hide();
    m_variables->hide();
    scroll->setWidget(m_body);
    m_layout->addWidget(scroll, 1);
}

void GsObjectsView::restart()
{
    m_last.clear();
}

void GsObjectsView::fill_picker()
{
    if (m_picker->count() > 1) return;
    for (const Emulator::GsProfileInfo &p : e->gs_firmware_profiles())
        if (p.instances > 0)
            m_picker->addItem(tr("%1 (%2)").arg(QString::fromStdString(p.title), QString::fromStdString(p.id)), QString::fromStdString(p.id));
}

bool GsObjectsView::read_as(const std::string &id)
{
    fill_picker();
    const int i = m_picker->findData(QString::fromStdString(id));
    if (i < 0) return false;
    if (i == m_picker->currentIndex()) refresh();
    else m_picker->setCurrentIndex(i);
    return true;
}

std::vector<QTableWidget *> GsObjectsView::struct_tables() const
{
    std::vector<QTableWidget *> out;
    for (const Shown &s : m_shown) out.push_back(s.table);
    return out;
}

void GsObjectsView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    fill_picker();
    const std::string as = m_picker->currentData().toString().toStdString();
    const Emulator::GsFirmware f = e->read_gs_firmware(m_card, as);
    if (!f.ok) {
        m_line->setText(QString::fromStdString(f.message));
        for (Shown &s : m_shown) {
            s.title->hide();
            s.table->hide();
        }
        return;
    }
    const dbg::Colors c = dbg::colors(palette());
    QString line = tr("profile %1 (%2, %3)").arg(profile_title(f.profile), QString::fromStdString(f.profile), QString::fromStdString(f.source));
    if (!as.empty()) line += kDot + tr("read as it, not as the active profile");
    line += kDot + (f.structs.empty() ? tr("no structs declared: pick a profile that has them (Read as); the window variables "
                                           "of the built-in tables are in GS firmware")
                                      : tr("%1 struct instances").arg(f.structs.size()));
    if (!f.variables.empty()) line += kDot + tr("%1 variables").arg(f.variables.size());
    m_line->setText(line);

    //A table per instance: rebuilt when its shape changes, else its cells written again (a change in the changed colour)
    while (m_shown.size() > f.structs.size()) {
        delete m_shown.back().title;
        delete m_shown.back().table;
        m_shown.pop_back();
    }
    for (size_t i = 0; i < f.structs.size(); i++) {
        const Emulator::GsStructTable &st = f.structs[i];
        std::string key = st.name + ":" + st.struct_name;
        for (const std::string &col : st.columns) key += "," + col;
        if (i >= m_shown.size()) {
            Shown s;
            s.title = new QLabel(m_body);
            s.table = new QTableWidget(m_body);
            m_body_layout->insertWidget(m_body_layout->indexOf(m_variables_title), s.title);
            m_body_layout->insertWidget(m_body_layout->indexOf(m_variables_title), s.table);
            m_shown.push_back(s);
        }
        Shown &s = m_shown[i];
        if (s.key != key) {
            s.key = key;
            s.table->setObjectName(QString::fromStdString("gsobjects:" + st.name));
            s.table->setFont(dbg::font());
            s.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            s.table->setSelectionBehavior(QAbstractItemView::SelectRows);
            s.table->verticalHeader()->hide();
            s.table->verticalHeader()->setDefaultSectionSize(dbg::row_height(s.table->fontMetrics()));
            QStringList cols;
            cols << tr("#") << tr("At");
            for (const std::string &col : st.columns) cols << QString::fromStdString(col);
            s.table->setColumnCount(cols.size());
            s.table->setHorizontalHeaderLabels(cols);
            s.table->horizontalHeader()->setStretchLastSection(true);
        }
        s.title->setText(tr("%1: struct %2, %3 x %4 bytes @%5")
                             .arg(QString::fromStdString(st.name), QString::fromStdString(st.struct_name))
                             .arg((qlonglong)st.count)
                             .arg((qlonglong)st.size)
                             .arg(QString::fromStdString(st.at)));
        s.title->show();
        s.table->show();
        s.table->setUpdatesEnabled(false);
        s.table->setRowCount((int)st.rows.size());
        for (size_t r = 0; r < st.rows.size(); r++) {
            s.table->setItem((int)r, 0, cell(QString::number(r), true));
            s.table->setItem((int)r, 1, cell(r < st.row_at.size() ? QString::fromStdString(st.row_at[r]) : QString()));
            for (size_t k = 0; k < st.rows[r].size(); k++) {
                const std::string path = st.name + "[" + std::to_string(r) + "]." + std::to_string(k);
                QTableWidgetItem * item = cell(QString::fromStdString(st.rows[r][k]), true);
                auto it = m_last.find(path);
                if (it != m_last.end() && it->second != st.rows[r][k]) item->setForeground(QBrush(c.changed));
                m_last[path] = st.rows[r][k];
                s.table->setItem((int)r, (int)k + 2, item);
            }
        }
        s.table->resizeColumnsToContents();
        s.table->horizontalHeader()->setStretchLastSection(true);
        const int visible = std::min<int>((int)st.rows.size(), 12);
        s.table->setMinimumHeight(s.table->horizontalHeader()->height() + visible * s.table->verticalHeader()->defaultSectionSize() + 4);
        s.table->setUpdatesEnabled(true);
    }

    m_variables_title->setVisible(!f.variables.empty());
    m_variables->setVisible(!f.variables.empty());
    m_variables->setRowCount((int)f.variables.size());
    for (size_t i = 0; i < f.variables.size(); i++) {
        const Emulator::GsVariable &v = f.variables[i];
        const QString value = v.valid ? QString::number((qlonglong)v.value) + " (" + (v.value > 0xFF ? hex4((unsigned int)v.value) : hex2((unsigned int)v.value)) + ")"
                                      : QString::fromStdString(v.reason.empty() ? std::string("unreadable") : v.reason);
        const std::string path = "var:" + v.name;
        QTableWidgetItem * item = cell(value, true);
        auto it = m_last.find(path);
        if (it != m_last.end() && it->second != value.toStdString()) item->setForeground(QBrush(c.changed));
        m_last[path] = value.toStdString();
        m_variables->setItem((int)i, 0, cell(QString::fromStdString(v.name)));
        m_variables->setItem((int)i, 1, cell(QString::fromStdString(v.kind)));
        m_variables->setItem((int)i, 2, cell(QString::fromStdString(v.at)));
        m_variables->setItem((int)i, 3, item);
        m_variables->setItem((int)i, 4, cell(QString::fromStdString(v.meaning)));
    }
    m_variables->resizeColumnsToContents();
    m_variables->horizontalHeader()->setStretchLastSection(true);
}

//------------------------------------------------------------------- Writers

namespace
{
//The card's ports (GS; the NeoGS's own from #0C) and the host's
QString port_name(bool host, unsigned int port)
{
    if (host) {
        switch (port) {
        case 0xB3: return QObject::tr("data to the card");
        case 0xBB: return QObject::tr("command");
        case 0x33: return QObject::tr("control (bit 7 reset, bit 6 NMI)");
        default: return QString();
        }
    }
    switch (port) {
    case 0x00: return QObject::tr("MPAG (page)");
    case 0x01: return QObject::tr("LED (NeoGS)");
    case 0x02: return QObject::tr("DATRG (data flag off)");
    case 0x03: return QObject::tr("OUTRG (reply)");
    case 0x05: return QObject::tr("RSCOM (command flag off)");
    case 0x06: case 0x07: case 0x08: case 0x09: return QObject::tr("VOL%1").arg(port - 0x05);
    case 0x0A: return QObject::tr("data flag = MPAG bit 7");
    case 0x0B: return QObject::tr("command flag = VOL1 bit 5");
    case 0x0C: return QObject::tr("INTENA");
    case 0x0D: return QObject::tr("INTREQ");
    case 0x0E: return QObject::tr("TIM_FREQ");
    case 0x0F: return QObject::tr("GSCFG0");
    case 0x10: return QObject::tr("MPAGEX");
    case 0x11: return QObject::tr("SCTRL");
    case 0x13: return QObject::tr("SD_SEND");
    case 0x14: return QObject::tr("MD_SEND");
    case 0x15: return QObject::tr("MC_SEND");
    case 0x16: case 0x17: case 0x18: case 0x19: return QObject::tr("VOL%1").arg(port - 0x11);
    case 0x1B: return QObject::tr("DMA_MOD");
    case 0x1C: return QObject::tr("DMA_HAD");
    case 0x1F: return QObject::tr("DMA_CST");
    case 0x20: case 0x21: case 0x22: case 0x23: return QObject::tr("PG%1").arg(port - 0x20);
    default: return QString();
    }
}
}

GsWritersView::GsWritersView(Emulator * e, QWidget *parent):
    GsCardTab(e, "card:gs-writers", parent)
{
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_line, 1);
    m_turn_on = new QPushButton(tr("Turn on the log"), this);
    m_turn_on->setObjectName("gswriters:turn-on");
    m_turn_on->setToolTip(tr("Record the host's commands (the command log, mode ring): its records carry the host's PC"));
    m_turn_on->hide();
    connect(m_turn_on, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        const Emulator::BoardAnswer a = this->e->gs_command_log(m_card, true);
        m_answer->setText(QString::fromStdString(a.ok ? a.message : "refused: " + a.message));
        m_answer->show();
        refresh();
    });
    row->addWidget(m_turn_on, 0, Qt::AlignTop);
    m_start = new QPushButton(tr("Start the trace"), this);
    m_start->setObjectName("gswriters:start");
    m_start->setToolTip(tr("Start the GS port trace: it records the card's own port writes (with the card's PC) and the host's"));
    m_start->hide();
    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (AddressRouter::read_only_at(this)) return;
        const Emulator::BoardAnswer a = this->e->gs_port_trace(m_card, "start");
        m_answer->setText(QString::fromStdString(a.ok ? a.message : "refused: " + a.message));
        m_answer->show();
        refresh();
    });
    row->addWidget(m_start, 0, Qt::AlignTop);
    m_layout->addLayout(row);
    m_answer = new QLabel(this);
    m_answer->setObjectName("gswriters:answer");
    m_answer->hide();
    m_layout->addWidget(m_answer);
    m_table = make_table("gswriters:table", QStringList() << tr("Port") << tr("Register") << tr("Side") << tr("Value") << tr("Writer PC")
                                                              << tr("Label") << tr("Frame") << tr("Source") << tr("Writes"));
    m_layout->addWidget(m_table, 1);
}

int GsWritersView::row_of(bool host, unsigned int port) const
{
    for (int r = 0; r < m_table->rowCount(); r++) {
        QTableWidgetItem * item = m_table->item(r, ColPort);
        if (item != nullptr && item->data(Qt::UserRole).toInt() == (int)port && item->data(Qt::UserRole + 1).toBool() == host) return r;
    }
    return -1;
}

void GsWritersView::refresh()
{
    if (m_card.empty() || !e->loaded) return;
    const Emulator::GsWrites w = e->read_gs_writes(m_card);
    if (!w.ok) {
        m_line->setText(QString::fromStdString(w.message));
        m_table->setRowCount(0);
        m_start->hide();
        m_turn_on->hide();
        return;
    }
    const bool log_on = !w.log_device.empty() && !w.log_mode.empty() && w.log_mode != "off";
    m_turn_on->setVisible(!w.log_device.empty() && !log_on);
    m_start->setVisible(w.trace_present && !w.trace_capturing && w.card_cpu);
    QStringList line;
    line << (w.log_device.empty() ? tr("no command log")
                                  : tr("command log %1 (host #B3 / #BB with the host's PC)").arg(QString::fromStdString(w.log_mode.empty() ? "?" : w.log_mode)));
    if (!w.trace_present)
        line << tr("port trace not read: %1").arg(QString::fromStdString(w.trace_note));
    else if (!w.trace_capturing)
        line << tr("port trace off: the card's own writes and #33 come with it (Start the trace)");
    else
        line << tr("port trace %1 kept, %2 evicted, %3").arg(grouped(w.trace_kept), grouped(w.trace_evicted), QString::fromStdString(w.trace_note));
    if (!w.card_cpu) line << tr("the lightweight player has no card CPU: no card port writes");
    m_line->setText(line.join(kDot));

    //The newest write of each port and how many writes were read
    struct Last {
        const Emulator::GsPortWrite * w = nullptr;
        uint64_t writes = 0;
    };
    std::map<std::pair<bool, unsigned int>, Last> last;
    std::vector<std::pair<bool, unsigned int>> fixed = {{true, 0xB3}, {true, 0xBB}, {true, 0x33}};
    if (w.card_cpu)
        for (unsigned int p : {0x00u, 0x03u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u}) fixed.emplace_back(false, p);
    for (const auto &k : fixed) last[k];
    //The log has the host's #B3 / #BB with their PC; the trace's host side then adds #33 (its NMI and other bits)
    std::map<unsigned int, uint64_t> logged;
    for (const Emulator::GsPortWrite &x : w.writes)
        if (x.host && x.source == "command log") logged[x.port]++;
    for (const Emulator::GsPortWrite &x : w.writes) {
        //The trace's #B3 / #BB are the log's records again (without the host's PC)
        if (x.host && x.port != 0x33 && x.source != "command log" && logged[x.port] > 0) continue;
        Last &l = last[{x.host, x.port}];
        l.writes++;
        //Oldest first within a source: the later of one frame is the newer; of one frame the one with the PC (a
        //reset is both the log's marker and the trace's #33)
        if (l.w == nullptr || x.frame > l.w->frame || (x.frame == l.w->frame && (x.has_pc || !l.w->has_pc))) l.w = &x;
    }
    const dbg::Colors c = dbg::colors(palette());
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);
    for (const auto &kv : last) {
        const bool host = kv.first.first;
        const unsigned int port = kv.first.second;
        const Last &l = kv.second;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        QTableWidgetItem * p = cell(hex2(port));
        p->setData(Qt::UserRole, (int)port);
        p->setData(Qt::UserRole + 1, host);
        m_table->setItem(r, ColPort, p);
        m_table->setItem(r, ColRegister, cell(port_name(host, port)));
        m_table->setItem(r, ColSide, cell(host ? tr("host") : tr("card")));
        if (l.w == nullptr) {
            m_table->setItem(r, ColValue, cell(QString::fromUtf8("\xE2\x80\x93")));
            for (int k : {(int)ColPc, (int)ColLabel, (int)ColFrame, (int)ColSource}) m_table->setItem(r, k, cell(QString()));
            m_table->setItem(r, ColWrites, cell("0", true));
            for (int k = 0; k < ColCount; k++) m_table->item(r, k)->setForeground(QBrush(c.dim));
            continue;
        }
        const Emulator::GsPortWrite &x = *l.w;
        m_table->setItem(r, ColValue, cell(hex2(x.value)));
        QTableWidgetItem * pc = cell(x.has_pc ? hex4(x.pc) : tr("not kept"));
        if (!x.has_pc) {
            pc->setForeground(QBrush(c.dim));
            pc->setToolTip(tr("The port trace keeps the card CPU's PC, not the host's: the host's writer of #33 is not recorded "
                              "(only a reset, bit 7, is in the command log)"));
        } else {
            pc->setToolTip(host ? tr("The main CPU's instruction that wrote it") : tr("The card CPU's instruction that wrote it"));
        }
        m_table->setItem(r, ColPc, pc);
        m_table->setItem(r, ColLabel, cell(QString::fromStdString(x.label)));
        QString frame = QString::number((qlonglong)x.frame);
        if (x.t >= 0) frame += " T " + QString::number((qlonglong)x.t);
        m_table->setItem(r, ColFrame, cell(frame, true));
        m_table->setItem(r, ColSource, cell(QString::fromStdString(x.source) + (x.kind.empty() ? QString() : " (" + QString::fromStdString(x.kind) + ")")));
        m_table->setItem(r, ColWrites, cell(grouped(l.writes), true));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setUpdatesEnabled(true);
}
