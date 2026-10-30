// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Stack, breakpoints, history and devices of the debugger, header

#pragma once

#include <functional>
#include <map>

#include <QListWidget>
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

//The breakpoints of a processor. A double click shows one in the listing,
//Delete removes it
class BreakpointsView : public QListWidget
{
    Q_OBJECT
public:
    explicit BreakpointsView(QWidget *parent);
    void refresh(CPU * cpu, const CodeFormatter &fmt);

signals:
    void activated(unsigned int address);
    void remove(unsigned int address);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
};

//Where PC has been, the newest last and in view, for a processor that keeps it
class HistoryView : public QListWidget
{
    Q_OBJECT
public:
    explicit HistoryView(QWidget *parent);
    void refresh(CPU * cpu, const CodeFormatter &fmt);

signals:
    void activated(unsigned int address);
    void toggle_breakpoint(unsigned int address);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    CPU * m_cpu;
};

//The fields of every device as a script reads them (LOG dev.field): the
//state of the timers, the controllers and the ports in one place
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
