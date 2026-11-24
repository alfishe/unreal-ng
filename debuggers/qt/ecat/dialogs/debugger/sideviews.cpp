// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Stack, breakpoints, history and devices of the debugger

#include "sideviews.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <set>
#include <stdexcept>

#include "addressmenu.h"
#include "debugstyle.h"
#include "dialogs/numfmt.h"

//------------------------------- StackView --------------------------------//

StackView::StackView(CPU * cpu, QWidget *parent):
    QWidget(parent),
    cpu(cpu)
{
    setFont(dbg::font());
    QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    setFocusPolicy(Qt::ClickFocus);
    //Paints every pixel of itself: nothing beneath it needs painting first
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void StackView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    const QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    m_painted.clear();
    update();
}

void StackView::refresh()
{
    unsigned int a;
    if (!isVisible() || cpu == nullptr || !cpu->stack_address(0, a)) return;
    const int n = height() / m_rh + 1;
    for (int r = 0; r < n; r++)
        if (!m_painted.same(r, row_key(r))) update(0, r * m_rh, width(), m_rh);
}

QString StackView::row_key(int row) const
{
    unsigned int sp, a;
    if (!cpu->stack_address(0, sp) || !cpu->stack_address(row - center(), a)) return QString();
    const unsigned int v = entry(a);
    auto it = m_prev.find(a);
    const bool changed = it != m_prev.end() && it->second != v;
    return QString::number(view_base(cpu)) + ' ' + QString::number(addr_digits(sp, view_base(cpu))) + ' '
           + QString::number(row - center()) + ' ' + QString::number(a) + ' ' + QString::number(v) + (changed ? " c" : "");
}

void StackView::set_cpu(CPU * c)
{
    cpu = c;
    m_prev.clear();
    mark_stop();
    update();
}

unsigned int StackView::entry(unsigned int address) const
{
    unsigned int v = 0;
    const unsigned int n = cpu->stack_entry_bytes();
    for (unsigned int b = 0; b < n; b++) v |= (cpu->peek_mem(address + b) & 0xFF) << (8 * b);
    return v;
}

void StackView::mark_stop()
{
    m_prev.clear();
    if (cpu == nullptr) return;
    const int n = height() / m_rh + 1;
    for (int r = 0; r < n; r++) {
        unsigned int a;
        if (cpu->stack_address(r - center(), a)) m_prev[a] = entry(a);
    }
}

void StackView::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(rect(), c.back);
    if (cpu == nullptr) return;
    p.setFont(font());
    const QFontMetrics fm(font());
    const int base_y = (m_rh - fm.height()) / 2 + fm.ascent();
    const int base = view_base(cpu);
    const unsigned int bytes = cpu->stack_entry_bytes();
    const int value_digits = cell_digits(base, bytes > 1);

    unsigned int a;
    if (!cpu->stack_address(0, a)) {
        p.setPen(c.dim);
        p.drawText(m_cw, base_y, tr("The processor has no stack"));
        return;
    }
    const int addr_chars = addr_digits(a, base);
    const int n = height() / m_rh + 1;
    for (int r = 0; r < n; r++) {
        const int i = r - center();
        const int y = r * m_rh;
        const QRect row(0, y, width(), m_rh);
        if (!event->region().intersects(row)) continue;
        m_painted.painted(event, r, row, row_key(r));
        if (!cpu->stack_address(i, a)) continue;
        if (i == 0) {
            p.fillRect(0, y, width(), m_rh, c.pc_back);
            p.setPen(c.text);
            p.drawText(m_cw, y + base_y, "SP>");
        }
        p.setPen(c.address);
        p.drawText(m_cw * 5, y + base_y, fmt_num(a, base, addr_chars));
        const unsigned int v = entry(a);
        auto it = m_prev.find(a);
        const bool changed = it != m_prev.end() && it->second != v;
        p.setPen(changed ? c.changed : (i < 0 ? c.dim : c.text));
        p.drawText(m_cw * (7 + addr_chars), y + base_y, fmt_num(v, base, value_digits));
    }
}

//The value as an address: a return address shows its call in the listing
bool StackView::value_at(const QPoint &pos, unsigned int &value) const
{
    unsigned int a;
    if (cpu == nullptr || !cpu->stack_address(pos.y() / m_rh - center(), a)) return false;
    value = entry(a);
    return true;
}

void StackView::mouseDoubleClickEvent(QMouseEvent *event)
{
    unsigned int v;
    if (value_at(event->pos(), v)) emit activated(v);
}

void StackView::contextMenuEvent(QContextMenuEvent *event)
{
    unsigned int v;
    if (!value_at(event->pos(), v)) return;
    unsigned int slot = 0;
    cpu->stack_address(event->pos().y() / m_rh - center(), slot);
    const QString at = QString::number(slot, 16).toUpper().rightJustified(4, '0');
    exec_address_menu(this, event->globalPos(), AddressTarget{cpu, v, tr("on the stack at #%1").arg(at)});
}

//------------------------------ CodeDelegate ------------------------------//

//A line of the code lists drawn as the listing draws it: rows as tight as its
//own, the address in its colour, the instruction after it
class CodeDelegate : public QStyledItemDelegate
{
public:
    explicit CodeDelegate(QObject * parent) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QFontMetrics fm(dbg::font());
        return QSize(dbg::text_width(fm, index.data().toString()) + 8, dbg::row_height(fm));
    }

    void paint(QPainter * p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const dbg::Colors c = dbg::colors(option.palette);
        const bool sel = (option.state & QStyle::State_Selected) != 0;
        p->fillRect(option.rect, sel ? c.selection : c.back);
        p->setFont(dbg::font());
        const QFontMetrics fm(dbg::font());
        const int y = option.rect.top() + (option.rect.height() - fm.height()) / 2 + fm.ascent();
        const QString text = index.data().toString();
        //The address is up to the first double space; a note without one is all text
        const int split = text.indexOf("  ");
        int x = option.rect.left() + 4;
        if (split > 0) {
            const QString address = text.left(split);
            p->setPen(sel ? c.selection_text : c.address);
            p->drawText(x, y, address);
            x += dbg::text_width(fm, address + "  ");
        }
        p->setPen(sel ? c.selection_text : c.text);
        p->drawText(x, y, split > 0 ? text.mid(split).trimmed() : text);
    }
};

static void tight_list(QListWidget * w)
{
    w->setFont(dbg::font());
    w->setItemDelegate(new CodeDelegate(w));
    w->setUniformItemSizes(true);
    w->setSpacing(0);
}

//---------------------------- BreakpointsView -----------------------------//

namespace {

//The event kinds the editor offers, in its groups (gui-card-debugger A4 `W.bp kinds`, A6 S5), by the core's names
//(gsdebughooks.h); a processor offers those of them it takes (CPU::event_kinds)
struct EventKindInfo {
    const char * name;
    const char * group;
    const char * label;
};

const char * const kGroupCard = QT_TRANSLATE_NOOP("BreakpointsView", "Card");
const char * const kGroupCardSide = QT_TRANSLATE_NOOP("BreakpointsView", "Protocol, card side");
const char * const kGroupHostSide = QT_TRANSLATE_NOOP("BreakpointsView", "Protocol, host side");
const char * const kGroupMachine = QT_TRANSLATE_NOOP("BreakpointsView", "Machine");

const EventKindInfo kEventKinds[] = {
    {"int", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card takes an interrupt")},
    {"nmi", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card takes an NMI")},
    {"reset", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card is reset")},
    {"page", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card switches a page")},
    {"dac_fetch", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card fetches a DAC sample")},
    {"clock_change", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card's clock changes")},
    {"dma_start", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "a card DMA starts")},
    {"dma_done", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "a card DMA ends")},
    {"gscfg0", kGroupCard, QT_TRANSLATE_NOOP("BreakpointsView", "the card writes GSCFG0")},
    {"card_command", kGroupCardSide, QT_TRANSLATE_NOOP("BreakpointsView", "the card takes a command")},
    {"card_data", kGroupCardSide, QT_TRANSLATE_NOOP("BreakpointsView", "the card takes a data byte")},
    {"card_reply", kGroupCardSide, QT_TRANSLATE_NOOP("BreakpointsView", "the card writes its reply")},
    {"host_command", kGroupHostSide, QT_TRANSLATE_NOOP("BreakpointsView", "the host sends a command")},
    {"host_data", kGroupHostSide, QT_TRANSLATE_NOOP("BreakpointsView", "the host sends a data byte")},
    {"host_reply", kGroupHostSide, QT_TRANSLATE_NOOP("BreakpointsView", "the host reads the card's reply")},
    {"int_accept", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the CPU accepts an interrupt")},
    {"int_return", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "an interrupt handler returns")},
    {"cpu_int", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "an interrupt source is accepted")},
    {"cpu_reti", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "RETI ends an interrupt source")},
    {"cpu_nmi", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "an NMI is requested")},
    {"cpu_retn", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "RETN ends an NMI")},
    {"machine_reset", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the machine is reset")},
    {"divmmc_map", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the DivMMC maps in")},
    {"divmmc_unmap", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the DivMMC maps out")},
    {"esxdos_call", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "a program calls esxDOS")},
    {"zxndma_start", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the zxnDMA starts")},
    {"zxndma_done", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the zxnDMA stops")},
    {"tsdma_start", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the TS-Conf DMA starts")},
    {"tsdma_done", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the TS-Conf DMA stops")},
    {"pld_read", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the PLD decodes a port read")},
    {"pld_write", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the PLD decodes a port write")},
    {"pld_event", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the PLD logs an event")},
    {"fdc_command", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the disk controller takes a command")},
    {"fdc_done", kGroupMachine, QT_TRANSLATE_NOOP("BreakpointsView", "the disk controller ends a command")},
};

const EventKindInfo * event_kind_info(const std::string &kind)
{
    for (const EventKindInfo &k : kEventKinds)
        if (kind == k.name) return &k;
    return nullptr;
}

//The event's byte as the list writes it: nothing for any value
QString event_value_text(const EventBreakpoint &b)
{
    if (b.mask == 0) return QString();
    if (b.mask == 0xFF) return " = #" + fmt_num(b.value, 16, 2);
    return " = #" + fmt_num(b.value, 16, 2) + " / #" + fmt_num(b.mask, 16, 2);
}

//The rows' roles: an execution row keeps its address, an event row its id
enum { RoleAddress = Qt::UserRole, RoleEventId, RoleIsEvent, RoleCpu };

} // namespace

QString breakpoint_cpu_name(CPU * cpu)
{
    if (cpu == nullptr) return QString();
    //The main processor is "cpu" among the devices
    return cpu->name == "cpu" ? QStringLiteral("main") : QString::fromStdString(cpu->name);
}

QString event_kind_label(const std::string &kind)
{
    const EventKindInfo * k = event_kind_info(kind);
    return k ? QCoreApplication::translate("BreakpointsView", k->label) : QString::fromStdString(kind);
}

void add_event_breakpoint_actions(QMenu * menu, Emulator * e, QWidget * from, const std::function<void()> &added)
{
    const std::vector<CPU *> cpus = e->dm->get_cpus();
    //Two card CPUs (a General Sound and a MultiSound) name theirs in the section titles
    int card_cpus = 0;
    for (CPU * c : cpus)
        for (const std::string &k : c->event_kinds())
            if (const EventKindInfo * info = event_kind_info(k); info && info->group == kGroupCard) {
                card_cpus++;
                break;
            }
    const bool read_only = AddressRouter::read_only_at(from);
    for (const char * group : {kGroupCard, kGroupCardSide, kGroupHostSide, kGroupMachine})
        for (CPU * c : cpus) {
            bool titled = false;
            for (const std::string &kind : c->event_kinds()) {
                const EventKindInfo * info = event_kind_info(kind);
                if (info == nullptr || info->group != group) continue;
                if (!titled) {
                    QString title = QCoreApplication::translate("BreakpointsView", group);
                    if (card_cpus > 1 && (group == kGroupCard || group == kGroupCardSide))
                        title += " (" + breakpoint_cpu_name(c) + ")";
                    menu->addSection(title);
                    titled = true;
                }
                const QString label = QCoreApplication::translate("BreakpointsView", info->label);
                QAction * a = menu->addAction(label);
                a->setEnabled(!read_only && e->loaded);
                QObject::connect(a, &QAction::triggered, from, [e, c, kind, label, from, added]() {
                    bool ok = false;
                    const QString text = QInputDialog::getText(from, QCoreApplication::translate("BreakpointsView", "Event breakpoint"),
                        label + "\n" + QCoreApplication::translate("BreakpointsView",
                            "The event's byte: empty for any, a value (#0F), or a value and a mask (#80/#E0)"),
                        QLineEdit::Normal, QString(), &ok);
                    if (!ok) return;
                    unsigned int value = 0, mask = 0;
                    try {
                        const QStringList parts = text.split('/');
                        if (!text.trimmed().isEmpty()) {
                            value = parse_input(parts[0], 16);
                            mask = parts.size() > 1 ? parse_input(parts[1], 16) : 0xFF;
                        }
                        if (parts.size() > 2 || value > 0xFF || mask > 0xFF) throw std::runtime_error("range");
                    } catch (const std::exception &) {
                        QMessageBox::critical(from, QCoreApplication::translate("BreakpointsView", "Error"),
                            QCoreApplication::translate("BreakpointsView", "The value and the mask are bytes: #0F, #80/#E0"));
                        return;
                    }
                    std::string error;
                    bool done = false;
                    e->invoke([&]() { done = c->add_event_breakpoint(kind, value & (mask ? mask : 0xFF), mask, error); });
                    if (!done) {
                        QMessageBox::critical(from, QCoreApplication::translate("BreakpointsView", "Event breakpoint"),
                                              QString::fromStdString(error));
                        return;
                    }
                    if (added) added();
                });
            }
        }
}

BreakpointsView::BreakpointsView(Emulator * e, QWidget *parent):
    QTreeWidget(parent),
    e(e)
{
    setColumnCount(2);
    setHeaderLabels({tr("CPU"), tr("Breakpoint")});
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setFont(dbg::font());
    //The breakpoint as the code lists draw it; the CPU column keeps the event rows' on / off box
    setItemDelegateForColumn(1, new CodeDelegate(this));
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setStretchLastSection(true);
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem * item, int) {
        if (!item->data(0, RoleIsEvent).toBool()) emit activated(row_cpu(item), item->data(0, RoleAddress).toUInt());
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem * item, int column) {
        if (m_filling || column != 0 || !item->data(0, RoleIsEvent).toBool()) return;
        CPU * c = row_cpu(item);
        if (c == nullptr) return;
        const unsigned int id = item->data(0, RoleEventId).toUInt();
        const bool on = item->checkState(0) == Qt::Checked;
        //The list is filled again after this signal, never inside it: the item is still being changed
        if (!AddressRouter::read_only_at(this)) this->e->invoke([c, id, on]() { c->enable_event_breakpoint(id, on); });
        QMetaObject::invokeMethod(this, [this]() {
            if (AddressRouter::read_only_at(this)) relist();
            else emit changed();
        }, Qt::QueuedConnection);
    });
}

CPU * BreakpointsView::row_cpu(QTreeWidgetItem * item) const
{
    return item ? static_cast<CPU *>(item->data(0, RoleCpu).value<void *>()) : nullptr;
}

void BreakpointsView::refresh(const CpuCodeFormatter &fmt)
{
    m_fmt = fmt;
    relist();
}

void BreakpointsView::relist()
{
    QTreeWidgetItem * current = currentItem();
    const int row = current ? indexOfTopLevelItem(current) : -1;
    m_filling = true;
    clear();
    if (e->loaded)
        for (CPU * c : e->dm->get_cpus()) {
            const QString cpu = breakpoint_cpu_name(c);
            for (unsigned int a : c->get_breakpoints()) {
                QTreeWidgetItem * item = new QTreeWidgetItem(this, {cpu, m_fmt ? m_fmt(c, a) : QString::number(a)});
                item->setData(0, RoleCpu, QVariant::fromValue<void *>(c));
                item->setData(0, RoleAddress, a);
            }
            for (const EventBreakpoint &b : c->get_event_breakpoints()) {
                QTreeWidgetItem * item = new QTreeWidgetItem(this,
                    {cpu, QString::fromStdString(b.kind) + "  " + event_kind_label(b.kind) + event_value_text(b)});
                item->setData(0, RoleCpu, QVariant::fromValue<void *>(c));
                item->setData(0, RoleIsEvent, true);
                item->setData(0, RoleEventId, b.id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, b.enabled ? Qt::Checked : Qt::Unchecked);
            }
        }
    m_filling = false;
    if (row >= 0 && row < topLevelItemCount()) setCurrentItem(topLevelItem(row));
}

void BreakpointsView::remove_row(QTreeWidgetItem * item)
{
    CPU * c = row_cpu(item);
    if (c == nullptr || AddressRouter::read_only_at(this)) return;
    if (!item->data(0, RoleIsEvent).toBool()) {
        emit remove(c, item->data(0, RoleAddress).toUInt());
        return;
    }
    const unsigned int id = item->data(0, RoleEventId).toUInt();
    e->invoke([c, id]() { c->remove_event_breakpoint(id); });
    emit changed();
}

void BreakpointsView::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && currentItem() != nullptr) {
        remove_row(currentItem());
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

//The row is taken before the menu: removing the breakpoint fills the list
//again. Every row's menu (and the empty list's) can add an event breakpoint
void BreakpointsView::contextMenuEvent(QContextMenuEvent *event)
{
    QTreeWidgetItem * item = itemAt(viewport()->mapFromGlobal(event->globalPos()));
    auto add_events = [this](QMenu * menu) {
        QMenu * sub = menu->addMenu(tr("Add an event breakpoint"));
        add_event_breakpoint_actions(sub, e, this, [this]() { emit changed(); });
        sub->setEnabled(!sub->isEmpty());
    };
    if (item == nullptr || item->data(0, RoleIsEvent).toBool()) {
        QMenu menu(this);
        if (item != nullptr) {
            setCurrentItem(item);
            const bool live = !AddressRouter::read_only_at(this);
            QAction * remove = menu.addAction(tr("Remove the breakpoint"));
            remove->setEnabled(live);
            const bool on = item->checkState(0) == Qt::Checked;
            QAction * toggle = menu.addAction(on ? tr("Disable") : tr("Enable"));
            toggle->setEnabled(live);
            connect(remove, &QAction::triggered, this, [this, item]() { remove_row(item); });
            connect(toggle, &QAction::triggered, this, [item, on]() { item->setCheckState(0, on ? Qt::Unchecked : Qt::Checked); });
            menu.addSeparator();
        }
        add_events(&menu);
        menu.exec(event->globalPos());
        return;
    }
    setCurrentItem(item);
    QMenu own(this);
    add_events(&own);
    exec_address_menu(this, event->globalPos(), AddressTarget{row_cpu(item), item->data(0, RoleAddress).toUInt(), tr("breakpoint")}, &own);
}

//------------------------------ HistoryView -------------------------------//

HistoryView::HistoryView(Emulator * e, QWidget *parent):
    QWidget(parent),
    e(e),
    m_cpu(nullptr)
{
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    //Whose history over the whole width (a device's name read whole), below it recording and the counts
    m_source = new QComboBox(this);
    m_source->setToolTip(tr("Whose history: the processor's instructions or a device's records"));
    m_source->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layout->addWidget(m_source);
    QHBoxLayout * row = new QHBoxLayout();
    row->setContentsMargins(2, 0, 2, 0);
    row->setSpacing(12);
    m_record = new QCheckBox(tr("Record"), this);
    m_record->setToolTip(tr("Keep this device's records (a ring of the newest); off again when the debugger closes"));
    row->addWidget(m_record);
    m_count = new QLabel(this);
    m_count->setToolTip(tr("Records kept of written"));
    row->addWidget(m_count, 1);
    layout->addLayout(row);
    m_list = new QListWidget(this);
    tight_list(m_list);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    connect(m_source, &QComboBox::currentIndexChanged, this, [this](int) {
        m_list->clear();
        m_shown.clear();
        m_lines.clear();
        refresh(m_cpu, m_fmt);
    });
    connect(m_record, &QCheckBox::toggled, this, [this](bool on) {
        const std::string device = m_source->currentData().toString().toStdString();
        if (device.empty() || !this->e->loaded || this->e->dm == nullptr) return;
        DeviceManager * dm = this->e->dm;
        this->e->invoke([dm, &device, on]() {
            for (unsigned int i = 0; i < dm->device_count; i++)
                if (auto * h = dynamic_cast<History *>(dm->get_device(i)->device.get()); h != nullptr && h->device() == device)
                    h->set_recording(on);
        });
        refresh(m_cpu, m_fmt);
    });
    //The line saying there is nothing has no address
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem * item) {
        if (item->data(Qt::UserRole).isValid()) emit activated(item->data(Qt::UserRole).toUInt());
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, &HistoryView::menu);
}

void HistoryView::set_read_only(bool on)
{
    m_read_only = on;
    m_record->setEnabled(!on && !m_source->currentData().toString().isEmpty());
}

//The processor's instructions first, then every device that keeps a history, as the machine lists them
void HistoryView::list_sources()
{
    std::vector<std::pair<QString, QString>> sources;
    if (e->loaded && e->dm != nullptr) {
        DeviceManager * dm = e->dm;
        e->invoke([dm, &sources]() {
            for (unsigned int i = 0; i < dm->device_count; i++)
                if (auto * h = dynamic_cast<History *>(dm->get_device(i)->device.get())) {
                    const std::string what = h->summary().what;
                    sources.emplace_back(QString::fromStdString(what + " (" + h->device() + ")"),
                                         QString::fromStdString(h->device()));
                }
        });
    }
    if (m_source->count() == (int)sources.size() + 1) return;
    const QSignalBlocker block(m_source);
    m_source->clear();
    m_source->addItem(tr("Executed instructions"), QString());
    for (const auto &s : sources) m_source->addItem(s.first, s.second);
}

void HistoryView::refresh(CPU * cpu, const CodeFormatter &fmt)
{
    m_fmt = fmt;
    list_sources();
    const QString device = m_source->currentData().toString();
    m_record->setVisible(!device.isEmpty());
    m_record->setEnabled(!m_read_only && !device.isEmpty());
    if (device.isEmpty()) show_cpu(cpu);
    else {
        m_cpu = cpu;
        show_device(device.toStdString());
    }
}

template <typename Fill> void HistoryView::relist(Fill fill)
{
    QScrollBar * bar = m_list->verticalScrollBar();
    const bool atNewest = bar->value() == bar->maximum();
    const int place = bar->value();
    m_list->clear();
    fill();
    if (atNewest) m_list->scrollToBottom();
    else bar->setValue(place);
}

void HistoryView::show_cpu(CPU * cpu)
{
    //Read again while the machine runs: the same list stays as it is
    const std::vector<unsigned int> h = cpu != nullptr ? cpu->get_history() : std::vector<unsigned int>();
    m_count->setText(h.empty() ? QString() : QString::number(h.size()));
    if (cpu == m_cpu && h == m_shown && m_list->count() > 0) return;
    m_cpu = cpu;
    m_shown = h;
    relist([this, cpu, &h]() {
        if (cpu == nullptr) return;
        if (h.empty()) {
            m_list->addItem(tr("Nothing recorded yet: the history starts with the debugger"));
            return;
        }
        for (unsigned int a : h) {
            QListWidgetItem * item = new QListWidgetItem(m_fmt(a), m_list);
            item->setData(Qt::UserRole, a);
        }
    });
}

void HistoryView::show_device(const std::string &device)
{
    if (!e->loaded || e->dm == nullptr) return;
    History::Summary summary;
    std::vector<std::string> lines;
    DeviceManager * dm = e->dm;
    e->invoke([dm, &device, &summary, &lines]() {
        for (unsigned int i = 0; i < dm->device_count; i++)
            if (auto * h = dynamic_cast<History *>(dm->get_device(i)->device.get()); h != nullptr && h->device() == device) {
                summary = h->summary();
                if (summary.recording) lines = h->records(256);
            }
    });
    {
        const QSignalBlocker block(m_record);
        m_record->setChecked(summary.recording);
    }
    m_count->setText(summary.recording ? tr("%1 kept of %2").arg(summary.kept).arg(summary.written) : QString());
    if (lines == m_lines && m_list->count() > 0) return;
    m_lines = lines;
    relist([this, &summary, &lines]() {
        if (!summary.recording) m_list->addItem(tr("Not recording: Record keeps its newest records"));
        else if (lines.empty()) m_list->addItem(tr("Nothing recorded yet"));
        for (const std::string &l : lines) m_list->addItem(QString::fromStdString(l));
    });
}

void HistoryView::menu(const QPoint &at)
{
    QListWidgetItem * item = m_list->itemAt(at);
    if (item == nullptr || m_cpu == nullptr || !item->data(Qt::UserRole).isValid()) return;
    m_list->setCurrentItem(item);
    const unsigned int address = item->data(Qt::UserRole).toUInt();
    exec_address_menu(this, m_list->viewport()->mapToGlobal(at), AddressTarget{m_cpu, address, tr("in the history")});
}

//------------------------------ DevicesView -------------------------------//

DevicesView::DevicesView(Emulator * e, QWidget *parent):
    QTreeWidget(parent),
    e(e)
{
    setFont(dbg::font());
    setColumnCount(2);
    setHeaderLabels(QStringList() << tr("Device") << tr("Value"));
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    //A name or a value is read whole: the columns take their text's width, the view scrolls sideways
    setTextElideMode(Qt::ElideNone);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    //An entry opened is read whole at once, not at the next tick
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem * item) {
        if (item->parent() == nullptr) refresh();
    });
}

void DevicesView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction * update = menu.addAction(QIcon(":/icons/reload"), tr("Refresh"));
    if (menu.exec(event->globalPos()) == update) refresh();
}

namespace {
struct DeviceEntryText {
    std::string device;
    bool deep = false;
    DeviceNode node;
};

//The texts and the parts of an item, the items kept where they are so what is open stays open
void sync_item(QTreeWidgetItem * item, const DeviceNode &n, bool parts)
{
    const QString name = QString::fromStdString(n.name);
    const QString value = QString::fromStdString(n.value);
    if (item->text(0) != name) item->setText(0, name);
    if (item->text(1) != value) item->setText(1, value);
    if (!parts) return;
    for (size_t k = 0; k < n.children.size(); k++) {
        QTreeWidgetItem * child = (k < (size_t)item->childCount()) ? item->child((int)k) : new QTreeWidgetItem(item);
        sync_item(child, n.children[k], true);
    }
    while (item->childCount() > (int)n.children.size()) delete item->takeChild(item->childCount() - 1);
}
}

void DevicesView::refresh()
{
    if (!e->loaded || e->dm == nullptr) return;
    //The open entries are read whole, the closed ones only for their summary
    std::set<std::string> open;
    for (int i = 0; i < topLevelItemCount(); i++)
        if (topLevelItem(i)->isExpanded()) open.insert(topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString());

    //The reports are read on the emulation thread, where the devices live
    std::vector<DeviceEntryText> snapshot;
    DeviceManager * dm = e->dm;
    e->invoke([dm, &open, &snapshot]() {
        for (unsigned int i = 0; i < dm->device_count; i++) {
            ComputerDevice * d = dm->get_device(i)->device.get();
            DeviceEntryText t;
            t.device = d->name;
            t.deep = open.count(d->name) != 0;
            if (d->get_tree(t.deep, t.node)) snapshot.push_back(std::move(t));
        }
    });

    for (size_t i = 0; i < snapshot.size(); i++) {
        QTreeWidgetItem * top = (i < (size_t)topLevelItemCount()) ? topLevelItem((int)i) : nullptr;
        if (top == nullptr) {
            top = new QTreeWidgetItem(this);
            top->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        const QString device = QString::fromStdString(snapshot[i].device);
        if (top->data(0, Qt::UserRole).toString() != device) {
            top->setData(0, Qt::UserRole, device);
            qDeleteAll(top->takeChildren());
        }
        //A closed entry keeps the parts it had: they are out of sight and come up to date when it opens
        sync_item(top, snapshot[i].node, snapshot[i].deep);
    }
    while (topLevelItemCount() > (int)snapshot.size()) delete takeTopLevelItem(topLevelItemCount() - 1);
}
