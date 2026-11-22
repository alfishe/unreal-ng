// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Stack, breakpoints, history and devices of the debugger, header

#pragma once

#include <functional>
#include <map>

#include <QListWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
#include <QTreeWidget>
#include <QWidget>

#include "emulator/emulator.h"
#include "debugutil.h"

//An address of code with its instruction, as the listing writes them
typedef std::function<QString(unsigned int)> CodeFormatter;

//The stack around its top: the entry SP points at in the middle, marked SP>,
//the ones pushed before it below and the free space above
class StackView : public QWidget
{
    Q_OBJECT
public:
    StackView(CPU * cpu, QWidget *parent);
    void set_cpu(CPU * cpu);
    //Repaints the rows that changed
    void refresh();
    void mark_stop();

signals:
    void activated(unsigned int address);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    CPU * cpu;
    int m_rh;
    int m_cw;
    std::map<unsigned int, unsigned int> m_prev;

    unsigned int entry(unsigned int address) const;
    //The value of the row under the point, if the stack has one there
    bool value_at(const QPoint &pos, unsigned int &value) const;
    int center() const { return (height() / m_rh) / 2; }

    PaintedRows m_painted;
    //What a row of the view shows, for PaintedRows
    QString row_key(int row) const;
};

//An address of code of a processor with its instruction, as that processor's listing writes them
typedef std::function<QString(CPU *, unsigned int)> CpuCodeFormatter;

//The breakpoints of every processor (not in ecat: ecat lists the one in front), a row each with its CPU (main, gs,
//ms): the execution ones, then the event ones (a card's event or protocol step, a machine's event) with their
//on / off box. A double click shows an execution one in its listing, Delete removes the row's
class BreakpointsView : public QTreeWidget
{
    Q_OBJECT
public:
    BreakpointsView(Emulator * e, QWidget *parent);
    void refresh(const CpuCodeFormatter &fmt);
    //The rows again with the last formatter (after a change made elsewhere)
    void relist();

signals:
    void activated(CPU * cpu, unsigned int address);
    void remove(CPU * cpu, unsigned int address);
    //An event breakpoint added, removed or switched here
    void changed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    Emulator * e;
    CpuCodeFormatter m_fmt;
    bool m_filling = false;

    CPU * row_cpu(QTreeWidgetItem * item) const;
    void remove_row(QTreeWidgetItem * item);
};

//The CPU's name in the breakpoint list: main, gs, ms
QString breakpoint_cpu_name(CPU * cpu);
//An event breakpoint kind in plain words ("the card takes a command"), the core's name when unknown
QString event_kind_label(const std::string &kind);
//The kinds the machine's processors take, grouped as the card debugger's editor has them (Card; Protocol, card
//side; Protocol, host side; Machine), each in plain words. Picked, it asks for the event's byte (any, a value, a
//value and a mask) and adds the breakpoint to its processor; @p added after it was. Nothing when no processor
//takes any
void add_event_breakpoint_actions(QMenu * menu, Emulator * e, QWidget * from, const std::function<void()> &added);

//What happened, the newest last and in view: where the processor's PC has been, or one device's records (a
//controller's commands, a chip's register writes) chosen above the list, each device's apart. A device records
//only when switched on: the debugger switches off again what it switched on
class HistoryView : public QWidget
{
    Q_OBJECT
public:
    HistoryView(Emulator * e, QWidget *parent);
    void refresh(CPU * cpu, const CodeFormatter &fmt);
    //Only seen: recording is not switched
    void set_read_only(bool on);

signals:
    void activated(unsigned int address);
    void toggle_breakpoint(unsigned int address);

private:
    Emulator * e;
    QComboBox * m_source;
    QCheckBox * m_record;
    QLabel * m_count;
    QListWidget * m_list;
    CPU * m_cpu;
    CodeFormatter m_fmt;
    std::vector<unsigned int> m_shown;
    std::vector<std::string> m_lines;
    bool m_read_only = false;

    void list_sources();
    void show_cpu(CPU * cpu);
    void show_device(const std::string &device);
    //The list's lines again, the reader's place kept (at the newest it follows them)
    template <typename Fill> void relist(Fill fill);
    void menu(const QPoint &at);
};

//The machine's devices as the core's device catalog lists them: each entry's
//one-line summary, opened the whole report as a tree (the CPUs and the
//memories have tabs of their own)
class DevicesView : public QTreeWidget
{
    Q_OBJECT
public:
    DevicesView(Emulator * e, QWidget *parent);
    void refresh();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    Emulator * e;
};
