// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Registers and flags of a processor in the debugger, header

#pragma once

#include <map>
#include <string>
#include <vector>

#include <QAbstractScrollArea>
#include <QWidget>

#include "emulator/emulator.h"
#include "debugutil.h"

class CellEditor;

//The registers: the name, the value in the processor's notation and in
//binary. A double click on either value edits it in its notation; values
//that changed since the last stop are coloured
class RegistersView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    RegistersView(Emulator * e, CPU * cpu, QWidget *parent);

    void refresh();
    //The values now are what the next stop compares against
    void mark_stop();
    QSize sizeHint() const override;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    Emulator * e;
    CPU * cpu;
    int m_base;
    std::vector<CpuRegister> m_regs;
    std::map<std::string, unsigned int> m_prev;
    int m_rh;
    int m_cw;
    int m_name_chars;
    CellEditor * m_editor;
    int m_edit_row;
    int m_edit_base;

    int value_chars(const CpuRegister &r) const;
    int binary_chars(const CpuRegister &r) const;
    QString value_text(const CpuRegister &r) const;
    QString binary_text(const CpuRegister &r) const;
    int x_value() const;
    int x_binary() const;
    int first_row() const;
    void commit_edit();
    void close_edit();

    PaintedRows m_painted;
    //What a row of the view shows, for PaintedRows
    QString row_key(int row) const;
};

//The flags as a row of letters: the letter when set, a dash when clear. A
//double click turns one over
class FlagsView : public QWidget
{
    Q_OBJECT
public:
    FlagsView(Emulator * e, CPU * cpu, QWidget *parent);

    void refresh();
    void mark_stop();
    QSize sizeHint() const override;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    Emulator * e;
    CPU * cpu;
    std::vector<std::pair<std::string, std::string> > m_flags;
    std::map<std::string, std::string> m_prev;
    int m_rh;
    int m_cw;

    int cell_width(const std::string &name) const;
    int x_first() const;

    PaintedRows m_painted;  //The whole line as one row
    QString key() const;
};
