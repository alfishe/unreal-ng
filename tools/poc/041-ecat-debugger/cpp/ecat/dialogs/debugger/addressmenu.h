// POC 041: one menu for an address wherever the debugger shows one

#pragma once

#include <QObject>
#include <QString>

#include "emulator/emulator.h"

class QMenu;
class QWidget;

//An address in a processor's space and what it was shown as: "HL", "on the
//stack at #5BF3", empty for the address itself
struct AddressTarget {
    CPU * cpu = nullptr;
    unsigned int address = 0;
    QString what;
};

//What the menu asks of the window. One per debugger window, its child: a view
//finds it through its own window
class AddressRouter : public QObject
{
    Q_OBJECT
public:
    explicit AddressRouter(QObject * parent) : QObject(parent) {}
    static AddressRouter * of(const QWidget * w);

signals:
    void show_code(CPU * cpu, unsigned int address);
    void show_memory(CPU * cpu, unsigned int address);
    void show_page(CPU * cpu, unsigned int address);
    void toggle_breakpoint(CPU * cpu, unsigned int address);
    void run_to(CPU * cpu, unsigned int address);
    void set_pc(CPU * cpu, unsigned int address);
};

//The actions of an address, as the debuggers have them (IDA's jump and
//xrefs, x64dbg's follow in disassembler / dump, set new origin, run to
//cursor): show it in the listing, in the dump, its page; run to it, set PC
//to it, a breakpoint on it; copy it, its byte, its word. The section title
//says the address, its window and what it was shown as
void add_address_actions(QMenu * menu, const QWidget * from, const AddressTarget &t);

//The menu of one address at a point of the screen, with the view's own
//actions (if any) above the common ones
void exec_address_menu(QWidget * from, const QPoint &global, const AddressTarget &t, QMenu * own = nullptr);
