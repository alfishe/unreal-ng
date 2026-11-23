// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The debugger window

#include "debuggerwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QGraphicsColorizeEffect>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "addressmenu.h"
#include "celleditor.h"
#include "bankstrip.h"
#include "debugstyle.h"
#include "debugutil.h"
#include "disasmview.h"
#include "memoryview.h"
#include "registersview.h"
#include "sideviews.h"
#include "ttdcontrols.h"
#include "dialogs/numfmt.h"

DebuggerWindow::DebuggerWindow(QWidget *parent, Emulator * e):
    GenericDbgWnd(parent),
    e(e),
    m_serial(0),
    m_paused(false),
    m_device_tick(0),
    m_wheel_zoom(0),
    m_zoom(1.0)
{
    //Everything is built at 100 %, the scale of the setup comes after: the
    //font of the views is process-wide and may still be a closed window's
    dbg::set_zoom(1.0);
    m_router = new AddressRouter(this);
    m_ui_font = font();
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
        //The listing under the windows of its space
        QWidget * left = new QWidget(p.split);
        QVBoxLayout * ll = new QVBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(1);
        p.banks = new BankStrip([cpu]() { return cpu->get_windows(); }, view_base(cpu), left);
        p.banks->set_pc([cpu]() { return cpu->get_pc(); });
        p.banks->set_cpu(cpu);
        p.code = new DisasmView(e, cpu, left);
        p.code->set_windows(p.banks->windows());
        ll->addWidget(p.banks);
        ll->addWidget(p.code, 1);
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
        connect(p.banks, &BankStrip::activated, this, [code](unsigned int address) {
            code->go_to(address);
            code->setFocus();
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
    m_breakpoints = new BreakpointsView(e, m_side_tabs);
    m_history = new HistoryView(e, m_side_tabs);
    m_devices = new DevicesView(e, m_side_tabs);
    m_side_tabs->addTab(m_stack, tr("Stack"));
    m_side_tabs->addTab(m_breakpoints, tr("Breakpoints"));
    m_side_tabs->addTab(m_history, tr("History"));
    m_side_tabs->addTab(m_devices, tr("Devices"));
    //A Next's own tabs: shown while the machine has their reports
    m_next_tabs = CreateNextTabs(e, m_side_tabs);
    for (const NextTab &t : m_next_tabs) m_side_tabs->setTabVisible(m_side_tabs->addTab(t.view, t.title), false);
    m_card_tabs = CreateCardTabs(e, m_side_tabs);
    for (const NextTab &t : m_card_tabs) m_side_tabs->setTabVisible(m_side_tabs->addTab(t.view, t.title), false);
    update_next_tabs();
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
    //The address menu of every view: its requests come here
    AddressRouter * router = m_router;
    connect(router, &AddressRouter::show_code, this, [this](CPU * cpu, unsigned int address) {
        select_cpu(cpu);
        if (CpuPane * p = pane()) {
            p->code->go_to(address);
            p->code->setFocus();
        }
    });
    connect(router, &AddressRouter::show_memory, this, [this](CPU * cpu, unsigned int address) {
        m_memory_tabs->show_address(cpu, address);
    });
    connect(router, &AddressRouter::show_page, this, [this](CPU * cpu, unsigned int address) {
        m_memory_tabs->show_page(cpu, address);
    });
    connect(router, &AddressRouter::show_memory_offset, this, [this](Memory * memory, unsigned int offset) {
        m_memory_tabs->show_memory_offset(memory, offset);
    });
    connect(router, &AddressRouter::toggle_breakpoint, this, [this](CPU * cpu, unsigned int address) {
        for (CpuPane &p : m_panes)
            if (p.cpu == cpu) p.code->toggle_breakpoint(address);
    });
    connect(router, &AddressRouter::run_to, this, [this](CPU * cpu, unsigned int address) {
        if (!this->e->loaded) return;
        mark_all();
        Emulator * em = this->e;
        this->e->invoke([em, cpu, address]() { em->debug_run_to(cpu, address); });
        show_state();
    });
    connect(router, &AddressRouter::set_pc, this, [this](CPU * cpu, unsigned int address) {
        this->e->invoke([cpu, address]() { cpu->set_context_value("PC", address); });
        this->e->record_command(cpu->name, "setreg", "PC, $" + hex_str(address, 1));
        for (CpuPane &p : m_panes)
            if (p.cpu == cpu) {
                p.regs->refresh();
                p.flags->refresh();
                p.code->show_pc();
            }
        m_stack->refresh();
    });

    //A breakpoint of any processor: its tab, its listing
    connect(m_breakpoints, &BreakpointsView::activated, router, &AddressRouter::show_code);
    connect(m_history, &HistoryView::activated, this, show_code);
    connect(m_stack, &StackView::activated, this, show_code);
    connect(m_breakpoints, &BreakpointsView::remove, router, &AddressRouter::toggle_breakpoint);
    connect(m_breakpoints, &BreakpointsView::changed, this, [this]() {
        refresh_side();
        update_cursor();
    });
    connect(m_history, &HistoryView::toggle_breakpoint, this, [this](unsigned int address) {
        if (CpuPane * p = pane()) p->code->toggle_breakpoint(address);
    });
    connect(m_cpu_tabs, &QTabWidget::currentChanged, this, [this](int) {
        //The tab's CPU is the one shown: a card's follows the main CPU's steps of the paused machine
        //(this->e: the constructor's parameter of the same name is not captured)
        if (CPU * c = current_cpu(); c != nullptr && this->e->loaded) {
            Emulator * em = this->e;
            em->invoke([em, c]() { em->debug_select(c); });
        }
        m_stack->set_cpu(current_cpu());
        refresh_side();
        update_cursor();
        show_state();  //the card tabs' card, its line, may have followed the tab
        //The time group follows the tab: a card's back steps are the card's, the moment names where it stands
        if (m_ttd != nullptr && this->e->loaded && m_ttd->shown()) m_ttd->poll(true);
    });
    connect(m_side_tabs, &QTabWidget::currentChanged, this, [this](int) { refresh_side(); });

    restore_layout();
    //The macOS style draws tabs at one fixed height whatever the font: Fusion sizes them to their text, so they grow
    //with the scale; names are not cut, the bar scrolls when they do not fit
    static QStyle * tab_style = QStyleFactory::create("Fusion");
    if (tab_style != nullptr)
        for (QTabWidget * t : findChildren<QTabWidget*>()) {
            t->setStyle(tab_style);
            t->tabBar()->setStyle(tab_style);
            t->tabBar()->setElideMode(Qt::ElideNone);
            t->tabBar()->setUsesScrollButtons(true);
        }
    set_zoom(QString::fromStdString(e->read_setup("Debugger", "zoom", "1")).toDouble());
    qApp->installEventFilter(this);

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
    //The stop before the window: taken (the tab its caller asked for stays)
    if (e->loaded) m_stop_serial = e->debug_stop_serial();
    refresh_all(true);
    mark_all();
    if (e->loaded) m_ttd->poll(true);
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

QString DebuggerWindow::code_text(CPU * cpu, unsigned int address)
{
    for (CpuPane &p : m_panes)
        if (p.cpu == cpu) return p.code->describe(address);
    return QString();
}

//A toolbar icon whose off state reads as off: gray, half seen (the platform's own barely pales a coloured one)
static QIcon tool_icon(const char * path)
{
    const QIcon icon(path);
    QIcon out;
    for (const int size : {20, 40}) {
        const QPixmap on = icon.pixmap(QSize(size, size), 1.0);
        out.addPixmap(on, QIcon::Normal);
        QImage off = on.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < off.height(); y++) {
            QRgb * line = reinterpret_cast<QRgb *>(off.scanLine(y));
            for (int x = 0; x < off.width(); x++) {
                const int g = 96 + qGray(line[x]) / 3;
                line[x] = qRgba(g, g, g, qAlpha(line[x]) / 2);
            }
        }
        out.addPixmap(QPixmap::fromImage(off), QIcon::Disabled);
    }
    return out;
}

void DebuggerWindow::build_toolbar(QWidget * host)
{
    QToolBar * tb = new QToolBar(host);
    m_toolbar = tb;
    tb->setIconSize(QSize(20, 20));
    host->layout()->addWidget(tb);

    auto add = [this, tb](const char * icon, const QString &text, const QKeySequence &key,
                          void (*command)(Emulator *, CPU *)) -> QAction * {
        QAction * a = tb->addAction(tool_icon(icon), text);
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
    m_to_pc = tb->addAction(tool_icon(":/icons/undo"), tr("Show PC"));
    m_to_pc->setShortcut(QKeySequence(Qt::Key_Asterisk));
    connect(m_to_pc, &QAction::triggered, this, [this]() { if (CpuPane * p = pane()) p->code->show_pc(); });
    addAction(m_to_pc);

    m_step_action = add(":/icons/forward", tr("Step"), QKeySequence(Qt::Key_F7),
        [](Emulator * e, CPU * c) { e->debug_step(c); });
    //Not in ecat: the Step of a card's tab in the past is a step through the history by card instructions (the time
    //group's verb, gs-debugger design §8); everywhere else the step above. Ahead of it: it runs first
    disconnect(m_step_action, &QAction::triggered, this, nullptr);
    connect(m_step_action, &QAction::triggered, this, [this]() {
        if (m_ttd != nullptr && !m_router->read_only() && m_ttd->step_forward()) return;
        run_command([](Emulator * e, CPU * c) { e->debug_step(c); });
    });
    m_step_over = add(":/icons/step_over", tr("Step over"), QKeySequence(Qt::Key_F8),
        [](Emulator * e, CPU * c) { e->debug_step_over(c); });
    m_step_out = add(":/icons/top", tr("Step out of the routine"), QKeySequence(Qt::CTRL | Qt::Key_F8),
        [](Emulator * e, CPU * c) { e->debug_step_out(c); });
    //Not in ecat either: on a card's tab in the past Step over and Step out go through the history too (at its end
    //the card steps on live, as the main CPU does)
    disconnect(m_step_over, &QAction::triggered, this, nullptr);
    connect(m_step_over, &QAction::triggered, this, [this]() {
        if (m_ttd != nullptr && !m_router->read_only() && m_ttd->step_forward("over")) return;
        run_command([](Emulator * e, CPU * c) { e->debug_step_over(c); });
    });
    disconnect(m_step_out, &QAction::triggered, this, nullptr);
    connect(m_step_out, &QAction::triggered, this, [this]() {
        if (m_ttd != nullptr && !m_router->read_only() && m_ttd->step_forward("out")) return;
        run_command([](Emulator * e, CPU * c) { e->debug_step_out(c); });
    });

    m_to_cursor = tb->addAction(tool_icon(":/icons/finish"), tr("Run to the cursor"));
    m_to_cursor->setShortcut(QKeySequence(Qt::Key_F4));
    m_to_cursor->setToolTip(tr("Run to the cursor") + " (F4)");
    addAction(m_to_cursor);
    connect(m_to_cursor, &QAction::triggered, this, [this]() { run_to_cursor(); });

    //Not in ecat (gs-debugger P5.4, gui-card-debugger A4 Toolbar): Step other, the CPU not in front one instruction,
    //and Run to, a sound card's moments. Both only with a card
    m_step_other = tb->addAction(tr("Step other"));
    m_step_other->setObjectName("step_other");
    m_step_other->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F11));
    m_step_other->setShortcutContext(Qt::WindowShortcut);
    addAction(m_step_other);
    connect(m_step_other, &QAction::triggered, this, [this]() { step_other(); });

    m_run_to_menu = new QToolButton(tb);
    m_run_to_menu->setText(tr("Run to"));
    m_run_to_menu->setToolTip(tr("Run the card to a moment: its cycle, a main CPU T-state, its next interrupt, the "
                                 "host's next command"));
    m_run_to_menu->setPopupMode(QToolButton::InstantPopup);
    QMenu * run_to = new QMenu(m_run_to_menu);
    m_run_to_menu->setMenu(run_to);
    auto run_to_entry = [this, run_to](const QString &text, const char * name, const char * until, const QString &ask) {
        QAction * a = run_to->addAction(text);
        a->setObjectName(name);
        const QString what = QString::fromLatin1(until);
        connect(a, &QAction::triggered, this, [this, what, ask, text]() {
            uint64_t value = 0;
            if (!ask.isEmpty()) {
                bool ok = false;
                const QString input = QInputDialog::getText(this, text, ask, QLineEdit::Normal, QString(), &ok).trimmed();
                if (!ok || input.isEmpty()) return;
                //Plain digits decimal; #, $ or 0x hex
                QString digits = input;
                int base = 10;
                if (digits.startsWith('#') || digits.startsWith('$')) {
                    digits = digits.mid(1);
                    base = 16;
                } else if (digits.startsWith("0x", Qt::CaseInsensitive)) {
                    digits = digits.mid(2);
                    base = 16;
                }
                value = digits.toULongLong(&ok, base);
                if (!ok) {
                    QMessageBox::critical(this, tr("Error"), tr("Incorrect number: %1").arg(input));
                    return;
                }
            }
            card_run_to(what, value);
        });
    };
    run_to->addAction(m_to_cursor);
    run_to->addSeparator();
    run_to_entry(tr("Card cycle..."), "run_to_card_cycle", "cycle",
                 tr("The card cycle of the frame to run the card to (decimal; #, $ or 0x for hex):"));
    run_to_entry(tr("Main T..."), "run_to_main_t", "main_t",
                 tr("The main CPU's T-state of the frame to run to (decimal; #, $ or 0x for hex):"));
    run_to_entry(tr("Next card interrupt"), "run_to_card_int", "int", QString());
    run_to_entry(tr("Next host command"), "run_to_host_command", "command", QString());
    m_run_to_menu_action = tb->addWidget(m_run_to_menu);

    //The processor in front: the machine's own (Ctrl+1), the card's (Ctrl+2)
    m_select_main = new QAction(tr("Main CPU"), this);
    m_select_main->setObjectName("select_main_cpu");
    m_select_main->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_select_main->setShortcutContext(Qt::WindowShortcut);
    addAction(m_select_main);
    connect(m_select_main, &QAction::triggered, this, [this]() {
        if (CPU * c = main_cpu()) select_cpu(c);
    });
    m_select_card = new QAction(tr("Card CPU"), this);
    m_select_card->setObjectName("select_card_cpu");
    m_select_card->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    m_select_card->setShortcutContext(Qt::WindowShortcut);
    addAction(m_select_card);
    connect(m_select_card, &QAction::triggered, this, [this]() {
        if (m_panes.size() > 1) select_cpu(m_panes[1].cpu);
    });

    //Back in time (not in ecat): shown only while the machine has a recorded history
    m_ttd = new TtdControls(this, tb, e, [this]() { return main_cpu(); }, [this]() { return current_cpu(); });
    connect(m_ttd, &TtdControls::moved, this, [this]() {
        //A move in time is a stop of its own: every view read again, PC in sight
        m_serial = e->debug_serial();
        m_paused = e->debug_paused() && !e->debug_stepping();
        mark_all();
        refresh_all(true);
        show_state();
    });
    connect(m_ttd, &TtdControls::moment_changed, this, [this]() { show_state(); });
    tb->addSeparator();

    //An address of the listing: it follows the cursor, and Enter goes there
    m_goto = new QLineEdit(tb);
    m_goto->setFont(dbg::font());
    m_goto->setMaximumWidth(dbg::text_width(QFontMetrics(dbg::font()), "0000000000000"));
    m_goto->setToolTip(tr("An address to show in the listing"));
    tb->addWidget(m_goto);
    m_go = tb->addAction(tool_icon(":/icons/next"), tr("Go to the address"));
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
    m_bp_add = tb->addAction(tool_icon(":/icons/add"), tr("Breakpoint at the cursor"));
    m_bp_add->setToolTip(tr("Breakpoint at the cursor") + " (F2)");
    connect(m_bp_add, &QAction::triggered, this, [this]() {
        CpuPane * p = pane();
        if (p != nullptr && !p->cpu->has_breakpoint(p->code->cursor_address()))
            p->code->toggle_breakpoint(p->code->cursor_address());
    });
    m_bp_remove = tb->addAction(tool_icon(":/icons/remove"), tr("Remove the breakpoint at the cursor"));
    connect(m_bp_remove, &QAction::triggered, this, [this]() {
        CpuPane * p = pane();
        if (p != nullptr && p->cpu->has_breakpoint(p->code->cursor_address()))
            p->code->toggle_breakpoint(p->code->cursor_address());
    });
    m_bp_toggle = new QAction(this);
    m_bp_toggle->setShortcut(QKeySequence(Qt::Key_F2));
    m_bp_toggle->setShortcutContext(Qt::WindowShortcut);
    addAction(m_bp_toggle);
    connect(m_bp_toggle, &QAction::triggered, this, [this]() { toggle_breakpoint_at_cursor(); });

    m_bp_clear = tb->addAction(tool_icon(":/icons/cancel"), tr("Remove all breakpoints"));
    connect(m_bp_clear, &QAction::triggered, this, [this]() {
        CPU * c = current_cpu();
        if (c == nullptr || (c->get_breakpoints().empty() && c->get_event_breakpoints().empty())) return;
        if (QMessageBox::question(this, tr("Breakpoints"),
                tr("Remove all breakpoints of %1?").arg(QString::fromStdString(c->name)),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        e->invoke([c]() {
            c->clear_breakpoints();
            for (const EventBreakpoint &b : c->get_event_breakpoints()) c->remove_event_breakpoint(b.id);
            if (c->m_debug == DEBUG_BRAKES && !c->has_temp_break()) c->m_debug = DEBUG_OFF;
        });
        refresh_side();
        if (CpuPane * p = pane()) p->code->viewport()->update();
        update_cursor();
    });
    //Not in ecat: event breakpoints (a card's event or protocol step, a machine's event; gui-card-debugger A4, A6
    //S5), the kinds of every processor in one menu
    m_bp_events = new QToolButton(tb);
    m_bp_events->setText(tr("Events"));
    m_bp_events->setToolTip(tr("Add an event breakpoint: the card takes a command, the host sends one, ..."));
    m_bp_events->setPopupMode(QToolButton::InstantPopup);
    QMenu * events = new QMenu(m_bp_events);
    m_bp_events->setMenu(events);
    connect(events, &QMenu::aboutToShow, this, [this, events]() {
        events->clear();
        add_event_breakpoint_actions(events, e, this, [this]() {
            refresh_side();
            update_cursor();
        });
    });
    tb->addWidget(m_bp_events);
    tb->addSeparator();

    m_state = new QLabel(tb);
    m_state->setMinimumWidth(200);
    m_state->setContentsMargins(6, 0, 6, 0);
    tb->addWidget(m_state);

    QWidget * spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    //The scale: larger, smaller, as it was; the wheel with Ctrl too
    auto zoom_action = [this](const QString &text, const QList<QKeySequence> &keys, int steps) {
        QAction * a = new QAction(text, this);
        a->setShortcuts(keys);
        a->setShortcutContext(Qt::WindowShortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, [this, steps]() { zoom_step(steps); });
    };
    zoom_action(tr("Larger"), {QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)}, 1);
    zoom_action(tr("Smaller"), {QKeySequence::ZoomOut}, -1);
    zoom_action(tr("Actual size"), {QKeySequence(Qt::CTRL | Qt::Key_0)}, 0);
    m_zoom_label = new QLabel(tb);
    m_zoom_label->setContentsMargins(6, 0, 6, 0);
    m_zoom_label->setToolTip(tr("The scale of the window: %1, %2, %3 or Ctrl + the wheel")
                                 .arg(QKeySequence(QKeySequence::ZoomIn).toString(QKeySequence::NativeText),
                                      QKeySequence(QKeySequence::ZoomOut).toString(QKeySequence::NativeText),
                                      QKeySequence(Qt::CTRL | Qt::Key_0).toString(QKeySequence::NativeText)));
    tb->addWidget(m_zoom_label);
    QAction * close_action = tb->addAction(tool_icon(":/icons/exit"), tr("Close"));
    connect(close_action, &QAction::triggered, this, &QDialog::close);
    if (QToolButton * b = qobject_cast<QToolButton*>(tb->widgetForAction(close_action)))
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

DisasmView * DebuggerWindow::current_code()
{
    CpuPane * p = pane();
    return p ? p->code : nullptr;
}

MemoryView * DebuggerWindow::current_memory(bool shown)
{
    CPU * c = current_cpu();
    return c ? m_memory_tabs->view_of(c, shown) : nullptr;
}

//The panes in the order the focus goes through them
static const char * const PANES[] = {"disassembly", "registers", "memory", "stack"};

QString DebuggerWindow::focused_pane()
{
    QWidget * f = focusWidget();
    CpuPane * p = pane();
    if (f == nullptr || p == nullptr) return QString();
    const auto in = [f](QWidget * w) { return w != nullptr && (w == f || w->isAncestorOf(f)); };
    if (in(p->code)) return PANES[0];
    if (in(p->regs) || in(p->flags)) return PANES[1];
    if (in(m_memory_tabs)) return PANES[2];
    if (in(m_stack)) return PANES[3];
    return QString();
}

bool DebuggerWindow::focus_pane(const QString &name)
{
    CpuPane * p = pane();
    if (p == nullptr) return false;
    QWidget * w = nullptr;
    if (name == PANES[0]) {
        w = p->code;
    } else if (name == PANES[1]) {
        w = p->regs;
    } else if (name == PANES[2]) {
        w = current_memory(true);
        if (w == nullptr) w = m_memory_tabs->currentWidget();
    } else if (name == PANES[3]) {
        m_side_tabs->setCurrentWidget(m_stack);
        w = m_stack;
    }
    if (w == nullptr) return false;
    w->setFocus(Qt::OtherFocusReason);
    return true;
}

void DebuggerWindow::move_pane_focus(int delta)
{
    const int n = (int)(sizeof(PANES) / sizeof(PANES[0]));
    const QString now = focused_pane();
    int i = -1;
    for (int k = 0; k < n; k++)
        if (now == PANES[k]) i = k;
    //From none: the first pane forward, the last back
    i = (i < 0) ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    focus_pane(PANES[i]);
}

void DebuggerWindow::send_pane_key(int key)
{
    if (focused_pane().isEmpty()) focus_pane(PANES[0]);
    QWidget * target = focusWidget();
    if (target == nullptr) return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    QCoreApplication::sendEvent(target, &release);
}

bool DebuggerWindow::set_memory_follow(const QString &name)
{
    static const char * const REGISTERS[] = {"pc", "sp", "bc", "de", "hl", "ix", "iy"};
    const QString n = name.toLower();
    if (n == "off") {
        m_memory_follow.clear();
        return true;
    }
    for (const char * r : REGISTERS)
        if (n == r) {
            m_memory_follow = n;
            //Held: the dump goes there now, as it would at the stop
            if (held()) follow_register();
            return true;
        }
    return false;
}

void DebuggerWindow::follow_register()
{
    CPU * c = current_cpu();
    MemoryView * v = current_memory();
    if (m_memory_follow.isEmpty() || c == nullptr || v == nullptr) return;
    v->go_to(register_value(c, m_memory_follow.toUpper().toStdString()) - v->source().origin);
}

bool DebuggerWindow::held()
{
    return e->loaded && pane() != nullptr && !m_router->read_only() && !e->debug_stepping() && e->debug_paused();
}

bool DebuggerWindow::run_to_cursor()
{
    CpuPane * p = pane();
    if (!held()) return false;
    mark_all();
    const unsigned int address = p->code->cursor_address();
    //Not in ecat: on a card's tab in the past the card runs to the cursor through the history (at its end it runs
    //on live, as the main CPU does)
    if (m_ttd != nullptr && !m_router->read_only() && m_ttd->step_forward("to", address)) {
        show_state();
        return true;
    }
    CPU * c = p->cpu;
    Emulator * em = e;
    e->invoke([em, c, address]() { em->debug_run_to(c, address); });
    show_state();
    return true;
}

CPU * DebuggerWindow::card_cpu()
{
    if (m_panes.size() < 2) return nullptr;
    CPU * c = current_cpu();
    return (c != nullptr && c != main_cpu()) ? c : m_panes[1].cpu;
}

bool DebuggerWindow::card_run_to(const QString &until, uint64_t value, Emulator::CardRunAnswer * answer)
{
    CPU * card = card_cpu();
    if (card == nullptr || !held()) return false;
    mark_all();
    Emulator * em = e;
    Emulator::CardRunAnswer a;
    const std::string what = until.toStdString();
    e->invoke([em, card, what, value, &a]() { a = em->debug_card_run_to(card, what, value); });
    //Said where the machine's state is: the core's sentence (where it stopped, or why it did not run)
    m_run_message = QString::fromStdString(a.message);
    //Taken as a stop of its own: every view read again (a run to a main T is a direct run, with no stop event)
    m_serial = e->debug_serial();
    m_paused = e->debug_paused() && !e->debug_stepping();
    if (m_paused) {
        refresh_all(true);
        m_card_strip = read_card_strip();
    }
    show_state();
    if (answer != nullptr) *answer = a;
    return true;
}

bool DebuggerWindow::step_other()
{
    CPU * front = current_cpu();
    if (front == nullptr || m_panes.size() < 2 || !held()) return false;
    CPU * other = front == main_cpu() ? card_cpu() : main_cpu();
    if (other == nullptr) return false;
    mark_all();
    //Its stop leaves the tab in front: the other CPU steps, the one looked at stays
    m_keep_front = true;
    Emulator * em = e;
    e->invoke([em, other]() { em->debug_step(other); });
    show_state();
    return true;
}

QString DebuggerWindow::state_text() const
{
    return m_state->text();
}

bool DebuggerWindow::toggle_breakpoint_at_cursor()
{
    CpuPane * p = pane();
    if (p == nullptr || !e->loaded || m_router->read_only()) return false;
    p->code->toggle_breakpoint(p->code->cursor_address());
    return true;
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
    //Offline (a remote machine whose emulator is gone) the last state is read only: the views move, nothing else
    const bool loaded = e->loaded && p != nullptr;
    const bool live = loaded && !m_router->read_only();
    const bool stepping = live && e->debug_stepping();
    const bool held = live && !stepping && e->debug_paused();
    const bool running = live && !held;

    //A free run stops at nothing: F9 then makes the breakpoints count again
    bool watching = stepping, any_breakpoints = false;
    if (running)
        for (CPU * c : e->dm->get_cpus()) {
            if (c->m_debug == DEBUG_BRAKES) watching = true;
            if (!c->get_breakpoints().empty() || !c->get_event_breakpoints().empty()) any_breakpoints = true;
        }

    m_run->setEnabled(held || (running && !watching && any_breakpoints));
    m_pause->setEnabled(running);
    m_run_free->setEnabled(held || (running && watching));

    m_step_action->setEnabled(held);
    m_step_over->setEnabled(held);
    m_step_out->setEnabled(held);
    m_to_cursor->setEnabled(held);
    //A card's actions: there with a card, made while held
    const bool card = m_panes.size() > 1;
    m_run_to_menu_action->setVisible(card);
    m_run_to_menu->setEnabled(held && card);
    m_step_other->setVisible(card);
    m_step_other->setEnabled(held && card);
    if (card && p != nullptr) {
        const QString other = QString::fromStdString(p->cpu == main_cpu() ? m_panes[1].cpu->name : main_cpu()->name);
        m_step_other->setText(tr("Step %1").arg(other));
        m_step_other->setToolTip(tr("Step %1: one instruction of the CPU not in front (Alt+F11)").arg(other));
    }
    m_select_card->setEnabled(card);
    m_to_pc->setEnabled(loaded);
    m_go->setEnabled(loaded);
    m_goto->setEnabled(loaded);

    const bool on = live && p->cpu->has_breakpoint(p->code->cursor_address());
    m_bp_add->setEnabled(live && !on);
    m_bp_remove->setEnabled(on);
    m_bp_toggle->setEnabled(live);
    m_bp_clear->setEnabled(live && (!p->cpu->get_breakpoints().empty() || !p->cpu->get_event_breakpoints().empty()));
    bool kinds = false;
    if (live)
        for (CPU * c : e->dm->get_cpus()) kinds = kinds || !c->event_kinds().empty();
    m_bp_events->setEnabled(kinds);
    if (m_ttd != nullptr) m_ttd->set_live(live);
}

void DebuggerWindow::run_command(void (*command)(Emulator *, CPU *))
{
    CPU * c = current_cpu();
    if (c == nullptr || !e->loaded || m_router->read_only()) return;
    mark_all();
    Emulator * em = e;
    e->invoke([em, c, command]() { command(em, c); });
    show_state();
}

void DebuggerWindow::mark_all()
{
    //A new command: the last card run's message and the kept tab are over
    m_run_message.clear();
    m_keep_front = false;
    for (CpuPane &p : m_panes) {
        p.banks->mark_stop();
        p.regs->mark_stop();
        p.flags->mark_stop();
    }
    m_memory_tabs->mark_stop();
    m_stack->mark_stop();
    for (const NextTab &t : m_next_tabs) t.view->mark_stop();
    for (const NextTab &t : m_card_tabs) t.view->mark_stop();
}

//The Next tabs while the machine is one, the card tabs while it has a General Sound, a NeoGS's own while the card is
//one, the lightweight player's while it is (the report's part they need, read once); a tab in sight that goes leaves
//the side on the stack. The card tabs follow a card (gs-debugger P5.5): with a General Sound and a MultiSound's GS
//both, the card CPU's tab in front, else the first card CPU (as the card runs); else the one there
void DebuggerWindow::update_next_tabs()
{
    CPU * card = card_cpu();
    const std::string source = CardTabsCard(e, card != nullptr ? card->name : std::string());
    if (source != m_card_source) {
        m_card_source = source;
        CardTabsFollow(m_card_tabs, source);
        m_card_strip = read_card_strip();
    }
    for (const std::vector<NextTab> *group : {&m_next_tabs, &m_card_tabs}) {
        const bool present = !group->empty() && group->front().view->present();
        std::map<std::string, bool> needed;
        for (const NextTab &t : *group) {
            bool shown = present;
            if (shown && !t.view->needs().empty()) {
                auto it = needed.find(t.view->needs());
                if (it == needed.end()) it = needed.emplace(t.view->needs(), t.view->present()).first;
                shown = it->second;
            }
            m_side_tabs->setTabVisible(m_side_tabs->indexOf(t.view), shown);
        }
    }
}

NextReportView * DebuggerWindow::next_view_in_sight() const
{
    QWidget * side = m_side_tabs->currentWidget();
    for (const std::vector<NextTab> *group : {&m_next_tabs, &m_card_tabs})
        for (const NextTab &t : *group)
            if (t.view == side) return t.view;
    return nullptr;
}

void DebuggerWindow::refresh_side()
{
    CPU * c = current_cpu();
    QWidget * side = m_side_tabs->currentWidget();
    auto fmt = [this](unsigned int a) { return code_text(a); };
    if (side == m_stack) m_stack->refresh();
    m_breakpoints->refresh([this](CPU * cpu, unsigned int a) { return code_text(cpu, a); });
    if (side == m_history) m_history->refresh(c, fmt);
    if (side == m_devices) m_devices->refresh();
    update_next_tabs();
    if (NextReportView * v = next_view_in_sight()) v->refresh();
}

void DebuggerWindow::refresh_all(bool stopped)
{
    if (!e->loaded) return;
    for (CpuPane &p : m_panes) {
        p.banks->refresh();
        p.code->set_windows(p.banks->windows());
        p.regs->refresh();
        p.flags->refresh();
        if (stopped) p.code->show_pc();
        else p.code->follow_memory();
    }
    m_memory_tabs->refresh();
    if (stopped) {
        refresh_side();
        follow_register();
    } else {
        m_stack->refresh();
    }
    emit views_refreshed();
}

void DebuggerWindow::update_view()
{
    m_memory_tabs->refresh();
    for (CpuPane &p : m_panes) p.code->refresh_memory();
}

void DebuggerWindow::show_state()
{
    QString text;
    if (!e->debug_online()) {
        text = QString::fromStdString(e->debug_offline_text());
    } else if (e->debug_stepping()) {
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
        //A card's stop: where the main CPU stood at the card's moment
        const std::string main = e->debug_stop_main_text();
        if (!main.empty()) text += (QString(" ") + QChar(0x00B7) + " ") + QString::fromStdString(main);
        //The card: its CPU, PC, cycle in the frame, page, symbols
        if (!m_card_strip.isEmpty()) text += (QString(" ") + QChar(0x00B7) + " ") + m_card_strip;
        //A card's run to a moment: the core's sentence
        if (!m_run_message.isEmpty()) text += (QString(" ") + QChar(0x00B7) + " ") + m_run_message;
    } else {
        text = tr("Running");
    }
    //Where the machine is in time: "History: frame 12 of 40, replaying" (forward actions replay)
    if (m_ttd != nullptr && !m_ttd->moment().isEmpty()) text += (QString(" ") + QChar(0x00B7) + " ") + m_ttd->moment();
    m_state->setText(text);
    //Read whole: the label as wide as its text
    m_state->setMinimumWidth(std::max((int)std::lround(200 * dbg::zoom(dbg::Zoom::Frame, m_zoom)), m_state->sizeHint().width()));
    update_actions();
}

void DebuggerWindow::on_debug_event()
{
    if (!e->loaded) return;
    if (follow_online()) return;
    const unsigned int serial = e->debug_serial();
    if (serial == m_serial) return;
    m_serial = serial;
    const bool paused = e->debug_paused() && !e->debug_stepping();
    m_paused = paused;
    if (paused) {
        //A stop of a processor (a breakpoint, a step) brings its tab to the front, once. Not in ecat: the events
        //after it (a move in time, a card's Step Back in the past, an edit, the stop's own event delivered late) find
        //the same stop and leave the tab in front; a pause names no processor
        const uint64_t stop = e->debug_stop_serial();
        if (stop != m_stop_serial) {
            m_stop_serial = stop;
            const int reason = e->debug_stop_reason();
            CPU * c = e->debug_stop_cpu();
            if (c != nullptr && reason != Emulator::DSTOP_PAUSE && reason != Emulator::DSTOP_NONE && !m_keep_front)
                select_cpu(c);
        }
        refresh_all(true);
        m_card_strip = read_card_strip();
        if (CpuPane * p = pane()) p->code->setFocus();
    }
    show_state();
}

QString DebuggerWindow::read_card_strip() const
{
    if (!e->loaded || e->dm == nullptr || m_card_source.empty()) return QString();
    const bool ms = m_card_source == "ms";
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * d = e->dm->get_device(i)->device.get();
        if (d->name != (ms ? "multisound" : "gs") || d->type != "report") continue;  //"gs" is the card's CPU as well
        if (ms) {
            //The MultiSound's GS: its part of the multisound report (read whole: past the fields' bound)
            DeviceNode report;
            bool read = false;
            e->invoke([d, &report, &read]() { read = d->get_tree(true, report); });
            for (const DeviceNode &part : report.children)
                if (read && part.name == "gs")
                    for (const DeviceNode &field : part.children)
                        if (field.name == "strip") return tr("MultiSound %1").arg(QString::fromStdString(field.value));
            return QString();
        }
        DeviceFieldValue v;
        bool read = false;
        e->invoke([d, &v, &read]() {
            d->get_device_fields();
            read = d->get_field("strip", 0, 0, v);
        });
        return read ? QString::fromStdString(v.text) : QString();
    }
    return QString();
}

//While the machine runs the views follow it; a stop that came without an
//event (the main window's button, a script) is taken as one
void DebuggerWindow::on_timer()
{
    if (!e->loaded) {
        update_actions();
        return;
    }
    if (follow_online()) return;
    //The time travel session: once a second
    m_ttd->poll();
    const bool paused = e->debug_paused() && !e->debug_stepping();
    if (paused != m_paused) {
        m_paused = paused;
        if (paused) refresh_all(true);
    } else if (!paused && isVisible() && !isMinimized()) {
        //Following a running machine is for the eyes: not while minimized
        refresh_all(false);
        //The devices and the history cost a read of the machine each: once a second, the one in sight
        QWidget * side = m_side_tabs->currentWidget();
        NextReportView * next = next_view_in_sight();
        if ((side == m_devices || side == m_history || next != nullptr) && ++m_device_tick >= 5) {
            m_device_tick = 0;
            if (next != nullptr) next->refresh();
            else if (side == m_devices) m_devices->refresh();
            else m_history->refresh(current_cpu(), [this](unsigned int a) { return code_text(a); });
        }
    }
    show_state();
}

//The connection of a remote machine: lost, the state shows it and the views stay as they were (no reads); back, every
//view is read again. True while offline or on the change
bool DebuggerWindow::follow_online()
{
    const bool online = e->debug_online();
    if (online == m_online) {
        if (online) return false;
        e->debug_paused();  //a found emulator is taken over at the next read (no request while none is)
        if (!e->debug_online()) return true;
    }
    m_online = e->debug_online();
    set_read_only(!m_online);
    if (m_online) {
        m_serial = e->debug_serial();
        m_paused = e->debug_paused() && !e->debug_stepping();
        refresh_all(true);
    }
    show_state();
    return true;
}

void DebuggerWindow::set_read_only(bool on)
{
    //The last state read, every view of it gray: seen, scrolled, copied, not current
    if (on) {
        QGraphicsColorizeEffect * gray = new QGraphicsColorizeEffect(m_split_main);
        gray->setColor(QColor(128, 128, 128));
        gray->setStrength(1.0);
        m_split_main->setGraphicsEffect(gray);
    } else {
        m_split_main->setGraphicsEffect(nullptr);
    }
    m_router->set_read_only(on);
    if (on)
        for (CellEditor * editor : findChildren<CellEditor *>())
            if (editor->isVisible()) emit editor->cancelled();
    m_memory_tabs->set_read_only(on);
    m_history->set_read_only(on);
    update_actions();
}

//The steps a browser takes: round numbers, the same way back
static const double ZOOMS[] = {0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};

double DebuggerWindow::zoom() const
{
    return m_zoom;
}

void DebuggerWindow::zoom_step(int steps)
{
    if (steps == 0) {
        set_zoom(1.0);
        return;
    }
    const double z = m_zoom;
    const int n = (int)(sizeof(ZOOMS) / sizeof(ZOOMS[0]));
    int i = 0;
    //The step nearest to the scale now, then the next one
    for (int k = 1; k < n; k++)
        if (std::fabs(ZOOMS[k] - z) < std::fabs(ZOOMS[i] - z)) i = k;
    set_zoom(ZOOMS[std::clamp(i + steps, 0, n - 1)]);
}

void DebuggerWindow::set_zoom(double zoom)
{
    if (!(zoom > 0)) zoom = 1.0;
    zoom = std::clamp(zoom, ZOOMS[0], ZOOMS[sizeof(ZOOMS) / sizeof(ZOOMS[0]) - 1]);
    m_zoom_label->setText(QString::number(std::lround(zoom * 100)) + " %");
    if (std::fabs(zoom - m_zoom) < 0.001) return;
    const double before = m_zoom;
    m_zoom = zoom;
    dbg::set_zoom(zoom);

    //Tabs, buttons, labels, column titles: one font, the window's own at 100 % scaled slower than the views. Set on
    //each of them, as the platform gives some classes (tab bars, buttons) fonts of their own
    const auto ui_font = [this](double scale) {
        QFont f = m_ui_font;
        if (m_ui_font.pointSizeF() > 0) f.setPointSizeF(m_ui_font.pointSizeF() * scale);
        else f.setPixelSize(std::max(1, (int)std::lround(m_ui_font.pixelSize() * scale)));
        return f;
    };
    const QFont frame = ui_font(dbg::zoom(dbg::Zoom::Frame, zoom));
    setFont(frame);
    //The views and fields set the debugger's font themselves: it again, at the new size; each one measures its rows
    //and columns again on the change, nothing is drawn scaled
    const QFont mono = dbg::font();
    //Which is which is told once, before the frame's font is set on anything (the frame may be of that family too)
    for (QWidget * w : findChildren<QWidget*>()) {
        if (!w->property("dbg_mono").isValid())
            w->setProperty("dbg_mono", w->testAttribute(Qt::WA_SetFont) && w->font().family() == mono.family());
        w->setFont(w->property("dbg_mono").toBool() ? mono : frame);
    }

    const double icons = dbg::zoom(dbg::Zoom::Icons, zoom);
    m_toolbar->setIconSize(QSize(20, 20) * icons);
    scale_icons(this, icons);
    m_goto->setMaximumWidth(dbg::text_width(QFontMetrics(mono), "0000000000000"));
    m_state->setMinimumWidth((int)std::lround(200 * dbg::zoom(dbg::Zoom::Frame, zoom)));
    //Columns sized to the views' text: the same text at its new size. Each one's
    //width at 100 % is kept, so steps there and back do not drift; a column
    //resized by hand gives a new one
    const double was_scale = dbg::zoom(dbg::Zoom::Content, before);
    const double scale = dbg::zoom(dbg::Zoom::Content, zoom);
    for (QHeaderView * h : findChildren<QHeaderView*>()) {
        //A header that follows its view's columns itself (the listing's)
        if (h->property("dbg_fixed_sections").toBool()) continue;
        QList<QVariant> base = h->property("dbg_section_base").toList();
        while (base.size() < h->count()) base.append(QVariant());
        const int stretched = h->stretchLastSection() ? h->logicalIndex(h->count() - 1) : -1;
        for (int i = 0; i < h->count(); i++) {
            if (i == stretched) continue;
            const double was = base[i].isValid() ? base[i].toDouble() : h->sectionSize(i) / was_scale;
            const double b = (std::lround(was * was_scale) == h->sectionSize(i)) ? was : h->sectionSize(i) / was_scale;
            base[i] = b;
            h->resizeSection(i, (int)std::lround(b * scale));
        }
        h->setProperty("dbg_section_base", base);
    }
    //Lists drawn by a delegate in the debugger's font: their rows measured again
    for (QAbstractItemView * v : findChildren<QAbstractItemView*>())
        if (qobject_cast<QHeaderView*>(v) == nullptr) v->doItemsLayout();

    e->write_setup("Debugger", "zoom", QString::number(zoom).toStdString());
}

bool DebuggerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        QWheelEvent * w = static_cast<QWheelEvent*>(event);
        QWidget * target = qobject_cast<QWidget*>(watched);
        if ((w->modifiers() & Qt::ControlModifier) && target != nullptr && target->window() == this) {
            //A notch is 120; a touchpad gives it in small parts
            m_wheel_zoom += w->angleDelta().y();
            const int steps = m_wheel_zoom / 120;
            m_wheel_zoom -= steps * 120;
            if (steps != 0) zoom_step(steps);
            return true;
        }
    }
    return GenericDbgWnd::eventFilter(watched, event);
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
