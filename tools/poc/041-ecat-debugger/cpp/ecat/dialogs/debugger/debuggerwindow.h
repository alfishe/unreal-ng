// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The debugger window, header

#pragma once

#include <vector>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

class QAction;
class QLabel;
class QLineEdit;
class QSplitter;
class QTabWidget;
class QTimer;
class DisasmView;
class RegistersView;
class FlagsView;
class MemoryTabs;
class StackView;
class BreakpointsView;
class HistoryView;
class DevicesView;

//One window for the whole machine. Each processor has a tab with its listing,
//registers and flags; below are the memory dumps (each processor's address
//space, the mappers, the memories, any other device on demand) and the stack,
//the breakpoints, the history and the devices of the processor in front.
//The window pauses and steps the machine through the emulator's debugger
//calls (Emulator::debug_pause() and the rest): a step is a step in time, and
//a breakpoint of any processor pauses all of them
class DebuggerWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    DebuggerWindow(QWidget *parent, Emulator * e);
    ~DebuggerWindow() override;

    void select_cpu(CPU * cpu);
    //The processor whose tab is in front
    CPU * current_cpu();

public slots:
    void update_view() override;

private slots:
    void on_debug_event();
    void on_timer();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct CpuPane {
        CPU * cpu;
        DisasmView * code;
        RegistersView * regs;
        FlagsView * flags;
        QSplitter * split;
    };

    Emulator * e;
    std::vector<CpuPane> m_panes;
    QTabWidget * m_cpu_tabs;
    MemoryTabs * m_memory_tabs;
    QTabWidget * m_side_tabs;
    QSplitter * m_split_main;
    QSplitter * m_split_bottom;
    StackView * m_stack;
    BreakpointsView * m_breakpoints;
    HistoryView * m_history;
    DevicesView * m_devices;
    QLabel * m_state;
    QLineEdit * m_goto;
    QTimer * m_timer;
    QAction * m_run;
    QAction * m_pause;
    QAction * m_run_free;
    QAction * m_to_pc;
    QAction * m_step_action;
    QAction * m_step_over;
    QAction * m_step_out;
    QAction * m_to_cursor;
    QAction * m_go;
    QAction * m_bp_add;
    QAction * m_bp_remove;
    QAction * m_bp_toggle;
    QAction * m_bp_clear;
    unsigned int m_serial;
    bool m_paused;
    int m_device_tick;

    CpuPane * pane();
    QString code_text(unsigned int address);
    void build_toolbar(QWidget * host);
    //Every view read again; a stop also brings PC into view
    void refresh_all(bool stopped);
    void refresh_side();
    //What the next stop shows changes against
    void mark_all();
    void show_state();
    void update_cursor();
    //What can be done now: the steps only while the machine is held, the
    //run while it is not running already
    void update_actions();
    void run_command(void (*command)(Emulator *, CPU *));
    void save_layout();
    void restore_layout();
};

GenericDbgWnd * CreateDebuggerWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
