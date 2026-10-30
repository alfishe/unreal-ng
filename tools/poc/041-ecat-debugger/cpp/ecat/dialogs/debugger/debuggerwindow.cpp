// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The debugger window

#include "debuggerwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "debugstyle.h"
#include "debugutil.h"
#include "disasmview.h"
#include "memoryview.h"
#include "registersview.h"
#include "sideviews.h"
#include "dialogs/numfmt.h"

DebuggerWindow::DebuggerWindow(QWidget *parent, Emulator * e):
    GenericDbgWnd(parent),
    e(e),
    m_serial(0),
    m_paused(false),
    m_device_tick(0)
{
    setWindowTitle(tr("Debugger"));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(1100, 760);

    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    build_toolbar(this);

    m_split_main = new QSplitter(Qt::Vertical, this);
    layout->addWidget(m_split_main, 1);

    //Upper part: a tab per processor, its listing beside its registers
    m_cpu_tabs = new QTabWidget(m_split_main);
    for (CPU * cpu : e->dm->get_cpus()) {
        CpuPane p;
        p.cpu = cpu;
        p.split = new QSplitter(Qt::Horizontal, m_cpu_tabs);
        p.code = new DisasmView(e, cpu, p.split);
        QWidget * right = new QWidget(p.split);
        QVBoxLayout * rl = new QVBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);
        p.regs = new RegistersView(e, cpu, right);
        p.flags = new FlagsView(e, cpu, right);
        rl->addWidget(p.regs, 1);
        rl->addWidget(p.flags);
        p.split->setStretchFactor(0, 2);
        p.split->setStretchFactor(1, 1);
        m_cpu_tabs->addTab(p.split, QString::fromStdString(cpu->name + " : " + cpu->type));
        m_panes.push_back(p);

        RegistersView * regs = p.regs;
        FlagsView * flags = p.flags;
        DisasmView * code = p.code;
        connect(p.regs, &RegistersView::changed, this, [this, code, flags]() {
            flags->refresh();
            code->show_pc();
            m_stack->refresh();
        });
        connect(p.flags, &FlagsView::changed, this, [regs]() { regs->refresh(); });
        connect(p.code, &DisasmView::breakpoints_changed, this, [this]() {
            refresh_side();
            update_cursor();
        });
        connect(p.code, &DisasmView::cursor_moved, this, [this, code]() {
            if (CpuPane * cp = pane())
                if (cp->code == code) update_cursor();
        });
        connect(p.code, &DisasmView::memory_changed, this, [this]() {
            m_memory_tabs->refresh();
            emit data_changed(this);
        });
    }

    //Lower part: the dumps beside the stack and the lists
    m_split_bottom = new QSplitter(Qt::Horizontal, m_split_main);
    m_memory_tabs = new MemoryTabs(e, m_split_bottom);
    connect(m_memory_tabs, &MemoryTabs::memory_changed, this, [this]() {
        for (CpuPane &p : m_panes) p.code->refresh_memory();
        emit data_changed(this);
    });

    m_side_tabs = new QTabWidget(m_split_bottom);
    m_stack = new StackView(current_cpu(), m_side_tabs);
    m_breakpoints = new BreakpointsView(m_side_tabs);
    m_history = new HistoryView(m_side_tabs);
    m_devices = new DevicesView(e, m_side_tabs);
    m_side_tabs->addTab(m_stack, tr("Stack"));
    m_side_tabs->addTab(m_breakpoints, tr("Breakpoints"));
    m_side_tabs->addTab(m_history, tr("History"));
    m_side_tabs->addTab(m_devices, tr("Devices"));
    m_split_bottom->setStretchFactor(0, 2);
    m_split_bottom->setStretchFactor(1, 1);
    m_split_main->setStretchFactor(0, 2);
    m_split_main->setStretchFactor(1, 1);

    auto show_code = [this](unsigned int address) {
        if (CpuPane * p = pane()) {
            p->code->go_to(address);
            p->code->setFocus();
        }
    };
    connect(m_breakpoints, &BreakpointsView::activated, this, show_code);
    connect(m_history, &HistoryView::activated, this, show_code);
    connect(m_stack, &StackView::activated, this, show_code);
    connect(m_breakpoints, &BreakpointsView::remove, this, [this](unsigned int address) {
        if (CpuPane * p = pane()) p->code->toggle_breakpoint(address);
    });
    connect(m_history, &HistoryView::toggle_breakpoint, this, [this](unsigned int address) {
        if (CpuPane * p = pane()) p->code->toggle_breakpoint(address);
    });
    connect(m_cpu_tabs, &QTabWidget::currentChanged, this, [this](int) {
        m_stack->set_cpu(current_cpu());
        refresh_side();
        update_cursor();
    });
    connect(m_side_tabs, &QTabWidget::currentChanged, this, [this](int) { refresh_side(); });

    restore_layout();

    //The machine's events come on the emulation thread and are handed over
    e->set_debug_listener([this]() {
        QMetaObject::invokeMethod(this, "on_debug_event", Qt::QueuedConnection);
    });
    Emulator * em = e;
    e->invoke([em]() { em->debug_attach(true); });

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DebuggerWindow::on_timer);
    m_timer->start(200);

    m_paused = e->debug_paused();
    m_serial = e->debug_serial();
    refresh_all(true);
    mark_all();
    show_state();
    update_cursor();
}

DebuggerWindow::~DebuggerWindow()
{
    e->set_debug_listener(nullptr);
}

GenericDbgWnd * CreateDebuggerWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    DebuggerWindow * w = new DebuggerWindow(parent, e);
    w->select_cpu(dynamic_cast<CPU*>(d));
    return w;
}

void DebuggerWindow::closeEvent(QCloseEvent *event)
{
    save_layout();
    m_timer->stop();
    e->set_debug_listener(nullptr);
    Emulator * em = e;
    if (em->loaded) em->invoke([em]() { em->debug_detach(); });
    GenericDbgWnd::closeEvent(event);
}

DebuggerWindow::CpuPane * DebuggerWindow::pane()
{
    const int i = m_cpu_tabs->currentIndex();
    if (i < 0 || i >= (int)m_panes.size()) return nullptr;
    return &m_panes[i];
}

CPU * DebuggerWindow::current_cpu()
{
    CpuPane * p = pane();
    return p ? p->cpu : nullptr;
}

void DebuggerWindow::select_cpu(CPU * cpu)
{
    for (size_t i = 0; i < m_panes.size(); i++)
        if (m_panes[i].cpu == cpu) m_cpu_tabs->setCurrentIndex((int)i);
}

QString DebuggerWindow::code_text(unsigned int address)
{
    CpuPane * p = pane();
    if (p == nullptr) return QString();
    return p->code->describe(address);
}

void DebuggerWindow::build_toolbar(QWidget * host)
{
    QToolBar * tb = new QToolBar(host);
    tb->setIconSize(QSize(20, 20));
    host->layout()->addWidget(tb);

    auto add = [this, tb](const char * icon, const QString &text, const QKeySequence &key,
                          void (*command)(Emulator *, CPU *)) -> QAction * {
        QAction * a = tb->addAction(QIcon(icon), text);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WindowShortcut);
            a->setToolTip(text + " (" + key.toString(QKeySequence::NativeText) + ")");
        }
        addAction(a);
        connect(a, &QAction::triggered, this, [this, command]() { run_command(command); });
        return a;
    };

    //The machine: run, pause, run on its own
    m_run = add(":/icons/play", tr("Run, stopping at the breakpoints"), QKeySequence(Qt::Key_F9),
        [](Emulator * e, CPU *) { e->debug_run(true); });
    m_pause = add(":/icons/pause", tr("Pause"), QKeySequence(Qt::Key_F12),
        [](Emulator * e, CPU *) { e->debug_pause(); });
    m_run_free = add(":/icons/play_fwd", tr("Run without debugging"), QKeySequence(),
        [](Emulator * e, CPU *) { e->debug_run(false); });
    tb->addSeparator();


    //The processor in front: steps
    m_to_pc = tb->addAction(QIcon(":/icons/undo"), tr("Show PC"));
    m_to_pc->setShortcut(QKeySequence(Qt::Key_Asterisk));
    connect(m_to_pc, &QAction::triggered, this, [this]() { if (CpuPane * p = pane()) p->code->show_pc(); });
    addAction(m_to_pc);

    m_step_action = add(":/icons/forward", tr("Step"), QKeySequence(Qt::Key_F7),
        [](Emulator * e, CPU * c) { e->debug_step(c); });
    m_step_over = add(":/icons/step_over", tr("Step over"), QKeySequence(Qt::Key_F8),
        [](Emulator * e, CPU * c) { e->debug_step_over(c); });
    m_step_out = add(":/icons/top", tr("Step out of the routine"), QKeySequence(Qt::CTRL | Qt::Key_F8),
        [](Emulator * e, CPU * c) { e->debug_step_out(c); });

    m_to_cursor = tb->addAction(QIcon(":/icons/finish"), tr("Run to the cursor"));
    m_to_cursor->setShortcut(QKeySequence(Qt::Key_F4));
    m_to_cursor->setToolTip(tr("Run to the cursor") + " (F4)");
    addAction(m_to_cursor);
    connect(m_to_cursor, &QAction::triggered, this, [this]() {
        CpuPane * p = pane();
        if (p == nullptr || !e->loaded) return;
        mark_all();
        const unsigned int address = p->code->cursor_address();
        CPU * c = p->cpu;
        Emulator * em = e;
        e->invoke([em, c, address]() { em->debug_run_to(c, address); });
        show_state();
    });
    tb->addSeparator();

    //An address of the listing: it follows the cursor, and Enter goes there
    m_goto = new QLineEdit(tb);
    m_goto->setFont(dbg::font());
    m_goto->setMaximumWidth(dbg::text_width(QFontMetrics(dbg::font()), "0000000000000"));
    m_goto->setToolTip(tr("An address to show in the listing"));
    tb->addWidget(m_goto);
    m_go = tb->addAction(QIcon(":/icons/next"), tr("Go to the address"));
    auto go_to = [this]() {
        CpuPane * p = pane();
        if (p == nullptr) return;
        try {
            p->code->go_to(parse_input(m_goto->text(), p->code->base()));
            p->code->setFocus();
        } catch (const std::exception &) {
            QMessageBox::critical(this, tr("Error"), tr("Incorrect address value"));
        }
    };
    connect(m_go, &QAction::triggered, this, go_to);
    connect(m_goto, &QLineEdit::returnPressed, this, go_to);
    tb->addSeparator();

    //Breakpoints. F2 turns the one at the cursor on and off
    m_bp_add = tb->addAction(QIcon(":/icons/add"), tr("Breakpoint at the cursor"));
    m_bp_add->setToolTip(tr("Breakpoint at the cursor") + " (F2)");
    connect(m_bp_add, &QAction::triggered, this, [this]() {
        CpuPane * p = pane();
        if (p != nullptr && !p->cpu->has_breakpoint(p->code->cursor_address()))
            p->code->toggle_breakpoint(p->code->cursor_address());
    });
    m_bp_remove = tb->addAction(QIcon(":/icons/remove"), tr("Remove the breakpoint at the cursor"));
    connect(m_bp_remove, &QAction::triggered, this, [this]() {
        CpuPane * p = pane();
        if (p != nullptr && p->cpu->has_breakpoint(p->code->cursor_address()))
            p->code->toggle_breakpoint(p->code->cursor_address());
    });
    m_bp_toggle = new QAction(this);
    m_bp_toggle->setShortcut(QKeySequence(Qt::Key_F2));
    m_bp_toggle->setShortcutContext(Qt::WindowShortcut);
    addAction(m_bp_toggle);
    connect(m_bp_toggle, &QAction::triggered, this, [this]() {
        if (CpuPane * p = pane()) p->code->toggle_breakpoint(p->code->cursor_address());
    });

    m_bp_clear = tb->addAction(QIcon(":/icons/cancel"), tr("Remove all breakpoints"));
    connect(m_bp_clear, &QAction::triggered, this, [this]() {
        CPU * c = current_cpu();
        if (c == nullptr || c->get_breakpoints().empty()) return;
        if (QMessageBox::question(this, tr("Breakpoints"),
                tr("Remove all breakpoints of %1?").arg(QString::fromStdString(c->name)),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        e->invoke([c]() {
            c->clear_breakpoints();
            if (c->m_debug == DEBUG_BRAKES && !c->has_temp_break()) c->m_debug = DEBUG_OFF;
        });
        refresh_side();
        if (CpuPane * p = pane()) p->code->viewport()->update();
        update_cursor();
    });
    tb->addSeparator();

    m_state = new QLabel(tb);
    m_state->setMinimumWidth(200);
    m_state->setContentsMargins(6, 0, 6, 0);
    tb->addWidget(m_state);

    QWidget * spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    QAction * close_action = tb->addAction(QIcon(":/icons/exit"), tr("Close"));
    connect(close_action, &QAction::triggered, this, &QDialog::close);
    if (QToolButton * b = qobject_cast<QToolButton*>(tb->widgetForAction(close_action)))
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

//The address field and the breakpoint buttons follow the listing's cursor
void DebuggerWindow::update_cursor()
{
    CpuPane * p = pane();
    if (p != nullptr && !m_goto->hasFocus()) {
        const unsigned int a = p->code->cursor_address();
        m_goto->setText(fmt_num(a, p->code->base(), addr_digits(p->code->space() - 1, p->code->base())));
    }
    update_actions();
}

//The machine is in one of four states: held (paused, a step made), stepping
//(a step that takes long), running with the breakpoints looked at, running
//without them. A stop by itself (every processor stopped by a script) counts
//as held
void DebuggerWindow::update_actions()
{
    CpuPane * p = pane();
    const bool loaded = e->loaded && p != nullptr;
    const bool stepping = loaded && e->debug_stepping();
    const bool held = loaded && !stepping && e->debug_paused();
    const bool running = loaded && !held;

    //A free run stops at nothing: F9 then makes the breakpoints count again
    bool watching = stepping, any_breakpoints = false;
    if (running)
        for (CPU * c : e->dm->get_cpus()) {
            if (c->m_debug == DEBUG_BRAKES) watching = true;
            if (!c->get_breakpoints().empty()) any_breakpoints = true;
        }

    m_run->setEnabled(held || (running && !watching && any_breakpoints));
    m_pause->setEnabled(running);
    m_run_free->setEnabled(held || (running && watching));

    m_step_action->setEnabled(held);
    m_step_over->setEnabled(held);
    m_step_out->setEnabled(held);
    m_to_cursor->setEnabled(held);
    m_to_pc->setEnabled(loaded);
    m_go->setEnabled(loaded);
    m_goto->setEnabled(loaded);

    const bool on = loaded && p->cpu->has_breakpoint(p->code->cursor_address());
    m_bp_add->setEnabled(loaded && !on);
    m_bp_remove->setEnabled(on);
    m_bp_toggle->setEnabled(loaded);
    m_bp_clear->setEnabled(loaded && !p->cpu->get_breakpoints().empty());
}

void DebuggerWindow::run_command(void (*command)(Emulator *, CPU *))
{
    CPU * c = current_cpu();
    if (c == nullptr || !e->loaded) return;
    mark_all();
    Emulator * em = e;
    e->invoke([em, c, command]() { command(em, c); });
    show_state();
}

void DebuggerWindow::mark_all()
{
    for (CpuPane &p : m_panes) {
        p.regs->mark_stop();
        p.flags->mark_stop();
    }
    m_memory_tabs->mark_stop();
    m_stack->mark_stop();
}

void DebuggerWindow::refresh_side()
{
    CPU * c = current_cpu();
    QWidget * side = m_side_tabs->currentWidget();
    auto fmt = [this](unsigned int a) { return code_text(a); };
    if (side == m_stack) m_stack->refresh();
    m_breakpoints->refresh(c, fmt);
    if (side == m_history && m_paused) m_history->refresh(c, fmt);
    if (side == m_devices) m_devices->refresh();
}

void DebuggerWindow::refresh_all(bool stopped)
{
    if (!e->loaded) return;
    for (CpuPane &p : m_panes) {
        p.regs->refresh();
        p.flags->refresh();
        if (stopped) p.code->show_pc();
        else p.code->follow_memory();
    }
    m_memory_tabs->refresh();
    if (stopped) refresh_side();
    else m_stack->refresh();
}

void DebuggerWindow::update_view()
{
    m_memory_tabs->refresh();
    for (CpuPane &p : m_panes) p.code->refresh_memory();
}

void DebuggerWindow::show_state()
{
    QString text;
    if (e->debug_stepping()) {
        text = tr("Stepping...");
    } else if (e->debug_paused()) {
        CPU * c = e->debug_stop_cpu();
        const QString who = c ? QString::fromStdString(c->name) : QString();
        switch (e->debug_stop_reason()) {
        case Emulator::DSTOP_STEP:        text = tr("Step: %1").arg(who); break;
        case Emulator::DSTOP_BREAKPOINT:  text = tr("Breakpoint: %1").arg(who); break;
        case Emulator::DSTOP_TEMP:        text = tr("Stopped: %1").arg(who); break;
        case Emulator::DSTOP_ERROR:       text = tr("Stopped by a device error"); break;
        case Emulator::DSTOP_NO_PROGRESS: text = tr("%1 executes nothing").arg(who); break;
        default:                          text = tr("Paused"); break;
        }
    } else {
        text = tr("Running");
    }
    m_state->setText(text);
    update_actions();
}

void DebuggerWindow::on_debug_event()
{
    if (!e->loaded) return;
    const unsigned int serial = e->debug_serial();
    if (serial == m_serial) return;
    m_serial = serial;
    const bool paused = e->debug_paused() && !e->debug_stepping();
    //Set before the views are read: the history is filled only while paused
    m_paused = paused;
    if (paused) {
        CPU * c = e->debug_stop_cpu();
        if (c != nullptr) select_cpu(c);
        refresh_all(true);
        if (CpuPane * p = pane()) p->code->setFocus();
    }
    show_state();
}

//While the machine runs the views follow it; a stop that came without an
//event (the main window's button, a script) is taken as one
void DebuggerWindow::on_timer()
{
    if (!e->loaded) {
        update_actions();
        return;
    }
    const bool paused = e->debug_paused() && !e->debug_stepping();
    if (paused != m_paused) {
        m_paused = paused;
        if (paused) refresh_all(true);
    } else if (!paused && isVisible() && !isMinimized()) {
        //Following a running machine is for the eyes: not while minimized
        refresh_all(false);
        //The devices cost a call into the emulation thread: once a second
        if (m_side_tabs->currentWidget() == m_devices && ++m_device_tick >= 5) {
            m_device_tick = 0;
            m_devices->refresh();
        }
    }
    show_state();
}

void DebuggerWindow::save_layout()
{
    e->write_setup("Debugger", "split_main", m_split_main->saveState().toHex().toStdString());
    e->write_setup("Debugger", "split_bottom", m_split_bottom->saveState().toHex().toStdString());
    if (!m_panes.empty())
        e->write_setup("Debugger", "split_cpu", m_panes[0].split->saveState().toHex().toStdString());
}

void DebuggerWindow::restore_layout()
{
    const std::string main = e->read_setup("Debugger", "split_main", "");
    const std::string bottom = e->read_setup("Debugger", "split_bottom", "");
    const std::string cpu = e->read_setup("Debugger", "split_cpu", "");
    if (!main.empty()) m_split_main->restoreState(QByteArray::fromHex(QByteArray::fromStdString(main)));
    else m_split_main->setSizes(QList<int>() << 500 << 250);
    if (!bottom.empty()) m_split_bottom->restoreState(QByteArray::fromHex(QByteArray::fromStdString(bottom)));
    else m_split_bottom->setSizes(QList<int>() << 730 << 370);
    for (CpuPane &p : m_panes) {
        if (!cpu.empty()) p.split->restoreState(QByteArray::fromHex(QByteArray::fromStdString(cpu)));
        else p.split->setSizes(QList<int>() << 730 << 370);
    }
}
