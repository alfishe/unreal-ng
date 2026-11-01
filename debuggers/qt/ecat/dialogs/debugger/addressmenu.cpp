// unreal-debugger: one menu for an address wherever the debugger shows one

#include "addressmenu.h"

#include <QApplication>
#include <QClipboard>
#include <QIcon>
#include <QMenu>
#include <QWidget>

#include "bankstrip.h"
#include "debugutil.h"
#include "dialogs/numfmt.h"

AddressRouter * AddressRouter::of(const QWidget * w)
{
    const QWidget * top = w ? w->window() : nullptr;
    return top ? top->findChild<AddressRouter*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

static QString hex(unsigned int value, int digits)
{
    return '#' + fmt_num(value, 16, digits);
}

void add_address_actions(QMenu * menu, const QWidget * from, const AddressTarget &t)
{
    AddressRouter * r = AddressRouter::of(from);
    CPU * cpu = t.cpu;
    const unsigned int space = cpu != nullptr ? code_space(cpu) : 0x10000;
    const unsigned int a = t.address % space;
    const int digits = addr_digits(space - 1, 16);

    //The title: the address, its window and page, what it was shown as
    QString title = hex(a, digits);
    if (cpu != nullptr) {
        const std::vector<AddressWindow> windows = cpu->get_windows();
        const int w = window_at(windows, a);
        if (w >= 0 && !windows[w].kind.empty())
            title += "  " + window_page(windows[w]) + " + " + hex(a - windows[w].start, 4);
    }
    if (!t.what.isEmpty()) title = t.what + " = " + title;
    menu->addSection(title);

    const bool can = r != nullptr && cpu != nullptr;
    const auto act = [&](const QString &icon, const QString &text, void (AddressRouter::*signal)(CPU*, unsigned int)) {
        QAction * x = menu->addAction(icon.isEmpty() ? QIcon() : QIcon(icon), text);
        x->setEnabled(can);
        if (can) QObject::connect(x, &QAction::triggered, r, [r, signal, cpu, a]() { emit (r->*signal)(cpu, a); });
        return x;
    };
    act(":/icons/next", QObject::tr("Show in disassembly"), &AddressRouter::show_code);
    act(QString(), QObject::tr("Show in memory"), &AddressRouter::show_memory);
    act(QString(), QObject::tr("Show its page"), &AddressRouter::show_page);
    menu->addSeparator();
    act(QString(), QObject::tr("Run to here"), &AddressRouter::run_to);
    act(QString(), QObject::tr("Set PC here"), &AddressRouter::set_pc);
    const bool bp = cpu != nullptr && cpu->has_breakpoint(a);
    act(bp ? ":/icons/remove" : ":/icons/add", bp ? QObject::tr("Remove the breakpoint") : QObject::tr("Set a breakpoint"),
        &AddressRouter::toggle_breakpoint);
    menu->addSeparator();

    //Copy: the address, and what is there as the processor reads it
    QMenu * copy = menu->addMenu(QObject::tr("Copy"));
    const auto put = [copy](const QString &text, const QString &value) {
        QAction * x = copy->addAction(text + "   " + value);
        QObject::connect(x, &QAction::triggered, x, [value]() { QApplication::clipboard()->setText(value); });
    };
    put(QObject::tr("Address"), hex(a, digits));
    if (cpu != nullptr) {
        const unsigned int b = cpu->peek_mem(a) & 0xFF;
        const unsigned int word = b | ((cpu->peek_mem((a + 1) % space) & 0xFF) << 8);
        put(QObject::tr("Byte"), hex(b, 2));
        put(QObject::tr("Word"), hex(word, 4));
    }
}

void exec_address_menu(QWidget * from, const QPoint &global, const AddressTarget &t, QMenu * own)
{
    QMenu menu(from);
    if (own != nullptr && !own->actions().isEmpty()) {
        for (QAction * x : own->actions()) menu.addAction(x);
    }
    add_address_actions(&menu, from, t);
    menu.exec(global);
}
