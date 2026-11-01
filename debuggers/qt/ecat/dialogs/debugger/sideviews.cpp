// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Stack, breakpoints, history and devices of the debugger

#include "sideviews.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>

#include <set>

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

BreakpointsView::BreakpointsView(QWidget *parent):
    QListWidget(parent)
{
    tight_list(this);
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem * item) {
        emit activated(item->data(Qt::UserRole).toUInt());
    });
}

void BreakpointsView::refresh(CPU * cpu, const CodeFormatter &fmt)
{
    const int row = currentRow();
    clear();
    m_cpu = cpu;
    if (cpu == nullptr) return;
    for (unsigned int a : cpu->get_breakpoints()) {
        QListWidgetItem * item = new QListWidgetItem(fmt(a), this);
        item->setData(Qt::UserRole, a);
    }
    if (row >= 0 && row < count()) setCurrentRow(row);
}

void BreakpointsView::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && currentItem() != nullptr) {
        emit remove(currentItem()->data(Qt::UserRole).toUInt());
        return;
    }
    QListWidget::keyPressEvent(event);
}

//The address is taken before the menu: removing the breakpoint fills the
//list again
void BreakpointsView::contextMenuEvent(QContextMenuEvent *event)
{
    QListWidgetItem * item = itemAt(event->pos());
    if (item == nullptr) return;
    setCurrentItem(item);
    const unsigned int address = item->data(Qt::UserRole).toUInt();
    exec_address_menu(this, event->globalPos(), AddressTarget{m_cpu, address, tr("breakpoint")});
}

//------------------------------ HistoryView -------------------------------//

HistoryView::HistoryView(QWidget *parent):
    QListWidget(parent),
    m_cpu(nullptr)
{
    tight_list(this);
    //The line saying there is no history has no address
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem * item) {
        if (item->data(Qt::UserRole).isValid()) emit activated(item->data(Qt::UserRole).toUInt());
    });
}

void HistoryView::refresh(CPU * cpu, const CodeFormatter &fmt)
{
    clear();
    m_cpu = cpu;
    if (cpu == nullptr) return;
    const std::vector<unsigned int> h = cpu->get_history();
    if (h.empty()) {
        addItem(tr("The processor does not keep its history"));
        return;
    }
    for (size_t i = 0; i < h.size(); i++) {
        QListWidgetItem * item = new QListWidgetItem(fmt(h[i]), this);
        item->setData(Qt::UserRole, h[i]);
    }
    scrollToBottom();
}

void HistoryView::contextMenuEvent(QContextMenuEvent *event)
{
    QListWidgetItem * item = itemAt(event->pos());
    if (item == nullptr || m_cpu == nullptr || !item->data(Qt::UserRole).isValid()) return;
    setCurrentItem(item);
    const unsigned int address = item->data(Qt::UserRole).toUInt();
    exec_address_menu(this, event->globalPos(), AddressTarget{m_cpu, address, tr("in the history")});
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
