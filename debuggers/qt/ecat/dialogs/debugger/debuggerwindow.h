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
class QToolBar;
class QTabWidget;
class QTimer;
class BankStrip;
class DisasmView;
class RegistersView;
class FlagsView;
class MemoryTabs;
class MemoryView;
class StackView;
class BreakpointsView;
class HistoryView;
class DevicesView;
class AddressRouter;
class TtdControls;

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
    //The scale of the whole window (not in ecat): its fonts, and the
    //rows, columns and icons measured in them; the window keeps its size
    void set_zoom(double zoom);
    double zoom() const;
    //The processor whose tab is in front
    CPU * current_cpu();

    //The GUI control plane's way in (guiplane/, not in ecat): the listing and the dump of the processor in front;
    //shown: the dump's tab in front
    DisasmView * current_code();
    //The machine's own processor (the first tab), whose history the time travel records
    CPU * main_cpu() { return m_panes.empty() ? nullptr : m_panes.front().cpu; }
    MemoryView * current_memory(bool shown = false);
    //The pane the keyboard is in: "disassembly", "registers", "memory", "stack", or "" (none of them)
    QString focused_pane();
    bool focus_pane(const QString &name);
    void move_pane_focus(int delta);
    //A key into the pane the keyboard is in (the listing when it is in none)
    void send_pane_key(int key);
    //Where the dump of the processor in front goes when the machine stops: a register ("pc", "sp", "bc", "de",
    //"hl", "ix", "iy") or "off"
    bool set_memory_follow(const QString &name);
    QString memory_follow() const { return m_memory_follow.isEmpty() ? QString("off") : m_memory_follow; }
    //The machine is held (paused, a step made): the steps and the run to the cursor can be made
    bool held();
    //F4 and F2 at the cursor of the listing in front; false when the machine is not held, or only seen
    bool run_to_cursor();
    bool toggle_breakpoint_at_cursor();
    //The time travel group of the toolbar (not in ecat): the self-check drives it
    TtdControls * ttd() { return m_ttd; }

signals:
    //The views were read again: after a stop, and while the machine runs
    void views_refreshed();

public slots:
    void update_view() override;

private slots:
    void on_debug_event();
    void on_timer();

protected:
    void closeEvent(QCloseEvent *event) override;
    //Ctrl + the wheel anywhere in the window: the scale
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct CpuPane {
        CPU * cpu;
        BankStrip * banks;
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
    QToolBar * m_toolbar;
    QLabel * m_zoom_label;
    QFont m_ui_font;        //The window's font at 100 %
    int m_wheel_zoom;       //Wheel turns not yet a step of the scale
    double m_zoom;          //The scale the window has
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
    TtdControls * m_ttd = nullptr;
    unsigned int m_serial;
    bool m_paused;
    bool m_online = true;
    bool follow_online();
    AddressRouter * m_router;
    //The machine only seen or changed too: every view follows (editors closed, actions off)
    void set_read_only(bool on);
    int m_device_tick;
    QString m_memory_follow;    //The register the dump follows, "" none
    //The dump of the processor in front at the register it follows
    void follow_register();

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
    void zoom_step(int steps);
    void restore_layout();
};

GenericDbgWnd * CreateDebuggerWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
