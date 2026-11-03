// unreal-debugger: eCat3's debugger window over an unreal-ng machine.
//
//   unreal-debugger [--model PENTAGON] [--card neogs|multisound | --no-gs] [file.sna|.z80|.tap|.trd|...]
//   unreal-debugger --measure 10 [...]   frames/s, process and GUI-thread CPU: screen only, debug mode, window
//   unreal-debugger --screenshot out.png  stop, step, breakpoint; saves the window (cpu and card tabs) and exits
//   unreal-debugger --self-check          the debugger API and the window's editors and buttons; exit code 2 on a failure
//   unreal-debugger --list-memory         the dump tabs and the "+" menu the window builds
//   unreal-debugger --connect 127.0.0.1:8090 [--emulator id]   a running emulator (unreal-qt) through its WebAPI;
//                                           --self-check, --measure and --screenshot work on it too
//   unreal-debugger --zoom 1.5 [...]      the window's scale (Ctrl+= / Ctrl+- / Ctrl+0, Ctrl + the wheel)
//
// Run from the build's bin/ directory (the core finds data/ there). A small screen window shows the machine; the
// debugger is ecat's DebuggerWindow as it is, over the adapter of adapter/unrealmachine.cpp (the core in this
// process) or adapter/remotemachine.cpp (--connect; the emulator shows its own screen).

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // windows.h below must not turn std::min / std::max into macros
#endif
#endif

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <ctime>

#include "remotemachine.h"
#include "unrealmachine.h"
#include "dialogs/debugger/addressmenu.h"
#include "dialogs/debugger/bankstrip.h"
#include "dialogs/debugger/debuggerwindow.h"
#include "dialogs/debugger/debugstyle.h"
#include "dialogs/debugger/disasmview.h"
#include "dialogs/debugger/memoryview.h"
#include "dialogs/debugger/pagedmemory.h"
#include "dialogs/debugger/sideviews.h"

namespace {

// SIGINT / SIGTERM end the event loop, so the machine is destroyed: over the WebAPI that releases the run-control
// session and debug mode (killed outright, the session stays held until its lease runs out)
std::atomic<bool> g_quit{false};
// The first signal asks the event loop to end; a second one (a GUI thread stuck in a request) ends the process
void on_quit_signal(int sig)
{
    g_quit = true;
    std::signal(sig, SIG_DFL);
}

// Frames the machine has run, in this process or behind the WebAPI
uint64_t machine_frames(ecatshim::Emulator * m)
{
    const uint64_t remote = RemoteMachineFrames(m);
    return remote ? remote : UnrealMachineFrames(m);
}

class ScreenView : public QWidget
{
public:
    explicit ScreenView(ecatshim::Emulator * e) : m_e(e)
    {
        setWindowTitle("unreal-ng debugger");
        resize(704, 576);
        QTimer * t = new QTimer(this);
        // Only a new frame is read, and only the box of pixels it changed is repainted
        connect(t, &QTimer::timeout, this, [this]() {
            const uint64_t frame = UnrealMachineFrames(m_e);
            if (frame == m_frame) return;
            m_frame = frame;
            int w = 0, h = 0;
            if (!UnrealMachineScreen(m_e, m_pixels, w, h)) return;
            QRect box;
            if (w != m_w || h != m_h || m_shown.size() != m_pixels.size()) {
                box = QRect(0, 0, w, h);
            } else {
                int x0 = w, y0 = h, x1 = -1, y1 = -1;
                for (int y = 0; y < h; y++) {
                    const uint32_t * a = m_pixels.data() + (size_t)y * w;
                    const uint32_t * b = m_shown.data() + (size_t)y * w;
                    if (std::equal(a, a + w, b)) continue;
                    y0 = std::min(y0, y);
                    y1 = y;
                    for (int x = 0; x < w; x++)
                        if (a[x] != b[x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
                }
                if (y1 < 0) return;
                box = QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
            }
            m_w = w;
            m_h = h;
            m_shown = m_pixels;
            m_image = QImage(reinterpret_cast<const uchar *>(m_shown.data()), w, h, QImage::Format_RGB32);
            // The box in widget pixels, a pixel wider for the scaling's rounding
            const double sx = (double)width() / w, sy = (double)height() / h;
            update(QRect((int)(box.x() * sx) - 1, (int)(box.y() * sy) - 1,
                         (int)(box.width() * sx) + 3, (int)(box.height() * sy) + 3));
        });
        t->start(40);
    }

protected:
    void paintEvent(QPaintEvent * event) override
    {
        QPainter p(this);
        p.setClipRect(event->rect());
        if (m_image.isNull()) p.fillRect(event->rect(), Qt::black);
        else p.drawImage(rect(), m_image);
    }

private:
    ecatshim::Emulator * m_e;
    std::vector<uint32_t> m_pixels;
    std::vector<uint32_t> m_shown;      // what m_image shows
    QImage m_image;
    uint64_t m_frame = ~0ull;
    int m_w = 0, m_h = 0;
};

// Paint events and their area, the screen window apart from the debugger's widgets
class PaintCounter : public QObject
{
public:
    explicit PaintCounter(QWidget * screen) : m_screen(screen) {}
    struct Count { uint64_t events = 0; uint64_t pixels = 0; };
    Count screen, debugger;
    std::map<std::string, Count> widgets;     // the debugger's, by parent / class

protected:
    bool eventFilter(QObject * o, QEvent * e) override
    {
        if (e->type() == QEvent::Paint && o->isWidgetType()) {
            // The area of the region itself: its bounding rect would count the gaps between the rows
            uint64_t area = 0;
            for (const QRect & r : static_cast<QPaintEvent *>(e)->region()) area += (uint64_t)r.width() * r.height();
            Count & c = (static_cast<QWidget *>(o)->window() == m_screen) ? screen : debugger;
            c.events++;
            c.pixels += area;
            if (&c == &debugger) {
                QObject * owner = o->parent() ? o->parent() : o;
                Count & w = widgets[std::string(owner->metaObject()->className()) + "/" + o->metaObject()->className()];
                w.events++;
                w.pixels += area;
            }
        }
        return false;
    }

private:
    QWidget * m_screen;
};

#ifdef _WIN32
static double fileTimeSeconds(const FILETIME &f)
{
    ULARGE_INTEGER v;
    v.LowPart = f.dwLowDateTime;
    v.HighPart = f.dwHighDateTime;
    return static_cast<double>(v.QuadPart) / 1e7;  // 100 ns units
}

double process_cpu_seconds()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    return fileTimeSeconds(kernel) + fileTimeSeconds(user);
}

// CPU time of the calling thread
double thread_cpu_seconds()
{
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.0;
    return fileTimeSeconds(kernel) + fileTimeSeconds(user);
}
#else
double process_cpu_seconds()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
}

// CPU time of the calling thread
double thread_cpu_seconds()
{
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
#endif

// Processes events until the condition holds or the time is out
bool wait_for(const std::function<bool()> &done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms || g_quit) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

unsigned int reg(ecatshim::CPU * c, const std::string &name)
{
    for (const ecatshim::CpuRegister &r : c->get_register_values())
        if (r.name == name) return r.value;
    return ~0u;
}

// The tab widget with a tab of this title (or starting with it)
QTabWidget * tabs_with(QWidget * w, const QString &title)
{
    for (QTabWidget * t : w->findChildren<QTabWidget *>())
        for (int i = 0; i < t->count(); i++)
            if (t->tabText(i).startsWith(title)) return t;
    return nullptr;
}

// Answers the window's modal dialogs while a check runs: a file dialog gets the path, a message box is noted and
// closed (a search that found nothing, a file that could not be read or written)
class DialogAnswer : public QObject
{
public:
    QString path;
    QStringList messages;
    DialogAnswer()
    {
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            // Answered again each second while it stays open: on a loaded machine the first answer can come before
            // the dialog is ready for it (no name field yet, the folder still being read) and is lost
            QWidget * w = QApplication::activeModalWidget();
            if (w == nullptr) return;
            const int ticks = w->property("debugger-answered").toInt();
            w->setProperty("debugger-answered", ticks + 1);
            if (ticks % 20 != 0) return;
            // Answered from the event loop, not from here: an answer may open a dialog of its own (a file that
            // exists), which this timer has to reach
            if (auto * f = qobject_cast<QFileDialog *>(w)) {
                // The name typed in, as a user does (selectFile does not replace the suggested name here)
                if (auto * name = f->findChild<QLineEdit *>("fileNameEdit")) name->setText(path);
                QMetaObject::invokeMethod(f, "accept", Qt::QueuedConnection);
            } else if (auto * b = qobject_cast<QMessageBox *>(w)) {
                messages << b->text();
                QMetaObject::invokeMethod(b, "accept", Qt::QueuedConnection);
            }
        });
        m_timer.start(50);
    }

private:
    QTimer m_timer;
};

void send_mouse(QWidget * w, QEvent::Type type, const QPoint &at)
{
    QMouseEvent e(type, QPointF(at), QPointF(w->mapToGlobal(at)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

// The scratch folder of the checkout the binary was built in (build/bin/ -> the checkout), one per process: checks
// running at once must not meet each other's files
QString scratch_dir()
{
    QDir d(QCoreApplication::applicationDirPath());
    while (!d.exists("scratch") && d.cdUp()) {}
    const QString path = d.filePath("scratch/debugger-self-check/" + QString::number(QCoreApplication::applicationPid()));
    QDir().mkpath(path);
    return path;
}

// The window's own editors and buttons, driven as a user would: an instruction's bytes typed over in the listing,
// the dump's search, save and load (through Qt's file dialogs, answered by DialogAnswer)
void window_check(QWidget * window, ecatshim::CPU * cpu, const std::function<void(bool, const char *)> &check)
{
    QApplication::processEvents();
    QTabWidget * cpu_tabs = tabs_with(window, QString::fromStdString(cpu->name + " :"));
    QTabWidget * memory_tabs = tabs_with(window, "pages");
    DisasmView * code = cpu_tabs ? cpu_tabs->widget(0)->findChild<DisasmView *>() : nullptr;
    auto * panel = memory_tabs ? qobject_cast<MemoryPanel *>(memory_tabs->widget(0)) : nullptr;
    check(code != nullptr && panel != nullptr, "window: listing and dump found");
    if (code == nullptr || panel == nullptr) return;
    cpu_tabs->setCurrentIndex(0);
    memory_tabs->setCurrentIndex(0);
    DialogAnswer answer;

    // An instruction's bytes typed over: NOPs at #C000, double click on the bytes of that row, "3E 42", Enter
    {
        unsigned int saved[4];
        for (unsigned int i = 0; i < 4; i++) {
            saved[i] = cpu->peek_mem(0xC000 + i);
            cpu->poke_mem(0xC000 + i, 0x00);
        }
        code->refresh_memory();
        code->go_to(0xC000);
        QApplication::processEvents();
        QWidget * vp = code->viewport();
        // The row: a click at the right edge (the instruction column) moves the cursor only
        int y = -1;
        for (int at = 1; at < vp->height() && y < 0; at += 2) {
            send_mouse(vp, QEvent::MouseButtonPress, QPoint(vp->width() - 4, at));
            if (code->cursor_address() == 0xC000) y = at;
        }
        // The bytes column: from the instruction column leftwards until the editor opens
        QLineEdit * editor = nullptr;
        for (int x = vp->width() - 4; y >= 0 && x > 0 && editor == nullptr; x -= 3) {
            send_mouse(vp, QEvent::MouseButtonDblClick, QPoint(x, y));
            for (QLineEdit * l : code->findChildren<QLineEdit *>())
                if (l->isVisible()) editor = l;
        }
        if (editor != nullptr) {
            editor->setText("3E 42");
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(editor, &enter);
        }
        check(editor != nullptr && cpu->peek_mem(0xC000) == 0x3E && cpu->peek_mem(0xC001) == 0x42 &&
                  code->describe(0xC000).contains("42"),
              "listing: instruction bytes typed over (LD A,#42)");
        if (editor == nullptr || cpu->peek_mem(0xC000) != 0x3E || cpu->peek_mem(0xC001) != 0x42)
            printf("  (row y %d, editor %s, #C000 = %02X %02X, listing \"%s\")\n", y, editor ? "opened" : "none",
                   cpu->peek_mem(0xC000), cpu->peek_mem(0xC001), code->describe(0xC000).toUtf8().constData());
        for (unsigned int i = 0; i < 4; i++) cpu->poke_mem(0xC000 + i, saved[i]);
        code->refresh_memory();
    }

    MemoryView * view = panel->view();
    QLineEdit * find = nullptr;
    for (QLineEdit * l : panel->findChildren<QLineEdit *>())
        if (l->placeholderText() == "Find") find = l;
    auto button = [panel](const QString &tip) {
        for (QToolButton * b : panel->findChildren<QToolButton *>())
            if (b->toolTip() == tip) return b;
        return (QToolButton *)nullptr;
    };

    // Search: four bytes put at #C123, searched from #C000
    {
        const unsigned int pattern[4] = {0xA5, 0x5A, 0xC3, 0x3C};
        unsigned int saved[4];
        for (unsigned int i = 0; i < 4; i++) {
            saved[i] = cpu->peek_mem(0xC123 + i);
            cpu->poke_mem(0xC123 + i, pattern[i]);
        }
        view->go_to(0xC000);
        if (find != nullptr) {
            find->setText("A5 5A C3 3C");
            emit find->returnPressed();
        }
        check(find != nullptr && view->cursor() == 0xC123 && answer.messages.isEmpty(), "dump: find (#C123)");
        for (unsigned int i = 0; i < 4; i++) cpu->poke_mem(0xC123 + i, saved[i]);
    }

    // Save: the CPU's 64 KB to a file, compared with what the CPU reads
    {
        answer.path = scratch_dir() + "/save.bin";
        QFile::remove(answer.path);
        QToolButton * save = button("Save to a file");
        if (save != nullptr) save->click();
        QFile f(answer.path);
        QByteArray data;
        if (f.open(QIODevice::ReadOnly)) data = f.readAll();
        bool same = data.size() == 0x10000;
        for (int a = 0; same && a < data.size(); a++) same = (uint8_t)data[a] == cpu->peek_mem((unsigned int)a);
        check(save != nullptr && same && answer.messages.isEmpty(), "dump: save (64 KB, as the CPU reads it)");
    }

    // Load: 16 bytes from a file at the cursor (#C200)
    {
        answer.path = scratch_dir() + "/load.bin";
        QByteArray data;
        for (int i = 0; i < 16; i++) data.append((char)(0x11 * i + 7));
        QFile f(answer.path);
        if (f.open(QIODevice::WriteOnly)) f.write(data);
        f.close();
        unsigned int saved[16];
        for (unsigned int i = 0; i < 16; i++) saved[i] = cpu->peek_mem(0xC200 + i);
        view->go_to(0xC200);
        QToolButton * load = button("Load a file at the cursor");
        if (load != nullptr) load->click();
        bool same = true;
        for (unsigned int i = 0; i < 16; i++) same = same && cpu->peek_mem(0xC200 + i) == (uint8_t)data[(int)i];
        check(load != nullptr && same && answer.messages.isEmpty(), "dump: load 16 bytes at the cursor (#C200)");
        for (unsigned int i = 0; i < 16; i++) cpu->poke_mem(0xC200 + i, saved[i]);
    }
    for (const QString &t : answer.messages) printf("  (dialog: %s)\n", t.toUtf8().constData());

    // The windows of the space: each one's page holds the bytes the CPU reads there; a strip above the listing and
    // one above the dump; the pages tab marks the page at #C000 and shows it
    {
        // Four 16 KB windows, or the Next's eight 8 KB slots: together the 64 KB
        const std::vector<ecatshim::AddressWindow> windows = cpu->get_windows();
        unsigned int covered = 0;
        for (const ecatshim::AddressWindow &w : windows) covered += w.size;
        bool same = !windows.empty() && covered == 0x10000;
        for (const ecatshim::AddressWindow &w : windows)
            for (unsigned int k : {0u, 0x123u, w.size - 1})
                same = same && w.device != nullptr && w.device->get_direct(w.offset + k) == cpu->peek_mem(w.start + k);
        check(same, "windows: each window's page holds its bytes");
        if (!same)
            for (const ecatshim::AddressWindow &w : windows)
                printf("  (window #%04X %s %u, %s + #%X: %02X / cpu %02X)\n", w.start, w.kind.c_str(), w.page,
                       w.device ? w.device->name.c_str() : "-", w.offset,
                       w.device ? w.device->get_direct(w.offset) : 0, cpu->peek_mem(w.start));
        int strips = 0;
        for (BankStrip * b : window->findChildren<BankStrip *>())
            if (!b->windows().empty()) strips++;
        check(strips >= 2, "windows: strips above the listing and the dump");

        PagedMemory * paged = window->findChild<PagedMemory *>();
        QTreeWidget * tree = paged ? paged->findChild<QTreeWidget *>() : nullptr;
        if (memory_tabs != nullptr && paged != nullptr) memory_tabs->setCurrentWidget(paged);
        QApplication::processEvents();
        if (paged != nullptr) paged->refresh();
        ecatshim::AddressWindow top;
        for (const ecatshim::AddressWindow &w : windows)
            if (w.start == 0xC000) top = w;
        // The tab's pages are the memory's own (16 KB), a Next slot is half of one
        auto * top_memory = dynamic_cast<ecatshim::Memory *>(top.device);
        const unsigned int page_size = top_memory ? top_memory->page_size() : 0;
        // The tree: device ("system" for the machine's own, "neogs" for "neogs.ram"), memory, page
        const QString memory = top.device ? QString::fromStdString(top.device->name) : QString();
        const int dot = memory.lastIndexOf('.');
        const QString device = dot < 0 ? QString("system") : memory.left(dot);
        const QString name = page_size ? memory.mid(dot + 1) + ' ' + QString::number(top.offset / page_size) : QString();
        QTreeWidgetItem * item = nullptr;
        for (QTreeWidgetItemIterator it(tree); tree != nullptr && *it; ++it)
            if ((*it)->text(0) == name && (*it)->parent() && (*it)->parent()->parent() &&
                (*it)->parent()->parent()->text(0) == device)
                item = *it;
        if (item != nullptr) tree->setCurrentItem(item);
        QApplication::processEvents();
        MemoryPanel * shown = paged ? paged->findChild<MemoryPanel *>() : nullptr;
        const bool marked = item != nullptr && item->text(1).contains("#C000");
        const bool holds = shown != nullptr && page_size && shown->view()->source().size == page_size &&
                           shown->view()->source().read(top.offset % page_size + 0x123) == cpu->peek_mem(0xC123);
        check(marked && holds, "pages tab: the page at #C000 marked and shown");
        if (memory_tabs != nullptr) memory_tabs->setCurrentIndex(0);
    }
    // The address menu: the same actions wherever an address is shown, done by the window
    {
        const auto run = [&](const AddressTarget &t, const QString &text) {
            QMenu menu;
            add_address_actions(&menu, code, t);
            for (QAction * a : menu.actions())
                if (a->text() == text && a->isEnabled()) {
                    a->trigger();
                    QApplication::processEvents();
                    return true;
                }
            return false;
        };
        const bool listing = code && run(AddressTarget{cpu, 0xC123, "HL"}, "Show in disassembly") &&
                             code->cursor_address() == 0xC123;
        const bool dump = panel && run(AddressTarget{cpu, 0xC124, QString()}, "Show in memory") &&
                          memory_tabs->currentWidget() == panel && panel->view()->cursor() == 0xC124;
        const std::vector<AddressWindow> windows = cpu->get_windows();
        const int w = window_at(windows, 0xC125);
        auto * m = w >= 0 ? dynamic_cast<ecatshim::Memory *>(windows[w].device) : nullptr;
        PagedMemory * paged = memory_tabs ? memory_tabs->findChild<PagedMemory *>() : nullptr;
        MemoryPanel * page_panel = nullptr;
        const bool page = m && m->page_size() && paged && run(AddressTarget{cpu, 0xC125, QString()}, "Show its page") &&
                          memory_tabs->currentWidget() == paged && (page_panel = paged->findChild<MemoryPanel *>()) &&
                          page_panel->view()->cursor() == (windows[w].offset + 0xC125 - windows[w].start) % m->page_size();
        const unsigned int pc = cpu->get_pc();
        const bool set_pc = run(AddressTarget{cpu, 0x8000, QString()}, "Set PC here") && cpu->get_pc() == 0x8000;
        cpu->set_context_value("PC", pc);
        check(listing && dump && page && set_pc, "address menu: listing, memory, page, set PC");
        if (!(listing && dump && page && set_pc))
            printf("  (listing %d, memory %d, page %d, set PC %d)\n", listing, dump, page, set_pc);
        if (memory_tabs != nullptr) memory_tabs->setCurrentIndex(0);
    }
}

// What the window's buttons and editors do, through the same calls, on the machine as it runs; the number failed
int self_check(ecatshim::Emulator * m, QWidget * window)
{
    int failed = 0;
    auto check = [&failed](bool ok, const char * what) {
        printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
        if (!ok) failed++;
    };
    const std::vector<ecatshim::CPU *> cpus = m->dm->get_cpus();
    ecatshim::CPU * cpu = cpus.front();
    ecatshim::CPU * gs = cpus.size() > 1 ? cpus[1] : nullptr;

    m->debug_run(true);
    m->debug_pause();
    check(wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000), "pause");

    const unsigned int r0 = reg(cpu, "R");
    m->debug_step(cpu);
    check(reg(cpu, "R") != r0, "step (R moved)");

    const unsigned int hl = reg(cpu, "HL");
    cpu->set_context_value("HL", hl ^ 0x1234);
    check(reg(cpu, "HL") == (hl ^ 0x1234), "register edit (HL)");
    cpu->set_context_value("HL", hl);

    auto flag = [cpu](const char * n) {
        for (const auto &f : cpu->get_flags())
            if (f.first == n) return f.second == "1";
        return false;
    };
    const bool c0 = flag("C");
    check(cpu->set_flag("C", !c0) && flag("C") == !c0, "flag edit (C)");
    cpu->set_flag("C", c0);

    const unsigned int a = 0xC000, v0 = cpu->peek_mem(a);
    check(cpu->poke_mem(a, v0 ^ 0xFF) && cpu->peek_mem(a) == (v0 ^ 0xFF), "memory edit (cpu space, #C000)");
    cpu->poke_mem(a, v0);

    ecatshim::MemoryMapper * mm = cpu->mm;
    unsigned int off = 0;
    ecatshim::AddressableDevice * owner = mm ? mm->peek_owner(a, off) : nullptr;
    check(owner != nullptr && owner->get_direct(off) == cpu->peek_mem(a), "page map: owner of #C000 holds its byte");

    const std::vector<unsigned int> history = cpu->get_history();
    // Addresses the CPU has been at; all of them PC itself on a CPU in HALT (EI; HALT waiting for the frame)
    check(!history.empty(), "history (PC history armed)");

    // Run to an address the machine has just executed, the one it came by most often (a loop's): it comes back there
    if (!history.empty()) {
        std::map<unsigned int, int> seen;
        unsigned int to = history.front();
        for (unsigned int h : history)
            if (++seen[h] > seen[to]) to = h;
        m->debug_run_to(cpu, to);
        const bool stopped = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
        check(stopped && cpu->get_pc() == to, "run to cursor");
        if (!stopped || cpu->get_pc() != to) {
            const unsigned int pc1 = cpu->get_pc();
            wait_for([]() { return false; }, 100);
            printf("  (run to #%04X, %u times in %zu of history: %s, PC #%04X then #%04X, paused %d stepping %d reason %d,"
                   " frames %llu, run-to bps %zu, IFF1 %u IM %u)\n",
                   to, (unsigned)seen[to], history.size(), stopped ? "stopped" : "not stopped in 3 s", pc1,
                   cpu->get_pc(), m->debug_paused(), m->debug_stepping(), m->debug_stop_reason(),
                   (unsigned long long)machine_frames(m), cpu->get_breakpoints().size(), reg(cpu, "IFF1"), reg(cpu, "IM"));
        }
        check(cpu->get_breakpoints().empty(), "run-to breakpoint gone after the stop");
    }

    // A breakpoint the machine passes: the IM 1 handler where interrupts come there, else an address it has just
    // executed (the TS-Conf and the Sprinter run with IM 2 or interrupts off)
    const bool im1 = reg(cpu, "IM") == 1 && reg(cpu, "IFF1") != 0;
    const unsigned int bp = im1 ? 0x0038 : (history.empty() ? cpu->get_pc() : history[history.size() / 3]);
    char what[64];
    cpu->add_breakpoint(bp);
    snprintf(what, sizeof(what), "breakpoint add (#%04X%s)", bp, im1 ? ", IM 1 handler" : ", from the history");
    check(cpu->has_breakpoint(bp), what);
    m->debug_run(true);
    const bool hit = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
    snprintf(what, sizeof(what), "breakpoint hit at #%04X", bp);
    check(hit && cpu->get_pc() == bp && m->debug_stop_reason() == ecatshim::Emulator::DSTOP_BREAKPOINT, what);
    if (!hit || cpu->get_pc() != bp)
        printf("  (%s, PC #%04X, paused %d stepping %d reason %d, frames %llu, IFF1 %u IM %u)\n",
               hit ? "stopped" : "not stopped in 3 s", cpu->get_pc(), m->debug_paused(), m->debug_stepping(),
               m->debug_stop_reason(), (unsigned long long)machine_frames(m), reg(cpu, "IFF1"), reg(cpu, "IM"));
    cpu->remove_breakpoint(bp);
    check(!cpu->has_breakpoint(bp), "breakpoint remove");
    if (!hit) {
        // The rest needs a stopped machine
        m->debug_pause();
        wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
    }

    if (im1) {
        m->debug_step_out(cpu);
        const bool out = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
        check(out && cpu->get_pc() >= 0x0040, "step out of the interrupt handler");
    }

    // Step over a CALL of a routine of our own: CALL #C010 at #C000, RET at #C010; PC lands after the CALL. Not on
    // a CPU in HALT: a new PC does not take it out of HALT (as in MAME), it would wait there for an interrupt
    if (cpu->peek_mem(cpu->get_pc()) == 0x76) {
        printf("  %-52s skipped (CPU in HALT)\n", "step over a CALL");
    } else {
        const unsigned int pc0 = cpu->get_pc();
        const unsigned int code[4] = {0xCD, 0x10, 0xC0, 0xC9};
        const unsigned int at[4] = {0xC000, 0xC001, 0xC002, 0xC010};
        unsigned int saved[4];
        for (int i = 0; i < 4; i++) {
            saved[i] = cpu->peek_mem(at[i]);
            cpu->poke_mem(at[i], code[i]);
        }
        cpu->set_context_value("PC", 0xC000);
        m->debug_step_over(cpu);
        const bool over = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
        check(over && cpu->get_pc() == 0xC003, "step over a CALL");
        if (!over || cpu->get_pc() != 0xC003)
            printf("    (stopped %d, PC #%04X, bytes at #C000 %02X %02X %02X; before: PC #%04X, %02X %02X)\n",
                   over ? 1 : 0, cpu->get_pc(), cpu->peek_mem(0xC000), cpu->peek_mem(0xC001), cpu->peek_mem(0xC002),
                   pc0, cpu->peek_mem((pc0 - 1) & 0xFFFF), cpu->peek_mem(pc0));
        for (int i = 0; i < 4; i++) cpu->poke_mem(at[i], saved[i]);
        cpu->set_context_value("PC", pc0);
    }

    if (gs != nullptr) {
        const unsigned int gpc = gs->get_pc();
        check(gs->get_register_values().size() > 10 && gs->peek_mem(gpc) == gs->peek_mem(gpc), "gs registers and memory");
        const unsigned int ga = 0x4100, g0 = gs->peek_mem(ga);
        check(gs->poke_mem(ga, g0 ^ 0x5A) && gs->peek_mem(ga) == (g0 ^ 0x5A), "gs memory edit (#4100)");
        gs->poke_mem(ga, g0);
        const unsigned int gde = reg(gs, "DE");
        gs->set_context_value("DE", gde ^ 0x0101);
        check(reg(gs, "DE") == (gde ^ 0x0101), "gs register edit (DE)");
        gs->set_context_value("DE", gde);

        // The card's windows: RAM pages hold what its Z80 reads there; a ROM / flash page too where the machine
        // lists the card's ROM (the NeoGS flash; the classic card's ROM is no device memory)
        const std::vector<ecatshim::AddressWindow> gw = gs->get_windows();
        bool gsame = gw.size() == 4;
        for (const ecatshim::AddressWindow &w : gw) {
            if (w.kind == "RAM") gsame = gsame && w.device != nullptr;
            if (w.device == nullptr) continue;
            for (unsigned int k : {0u, 0x123u, w.size - 1})
                gsame = gsame && w.device->get_direct(w.offset + k) == gs->peek_mem(w.start + k);
        }
        check(gsame, "gs windows: each listed page holds its bytes");

        // An address the card's Z80 comes back to: the one it has been at most often (a loop's), else where it stands.
        // Its own PC alone may be an address it visits once in several seconds (a timer routine), which a slow host misses
        const std::vector<unsigned int> ghistory = gs->get_history();
        std::map<unsigned int, int> gseen;
        unsigned int gbp = gpc;
        if (!ghistory.empty()) {
            gbp = ghistory.front();
            for (unsigned int h : ghistory)
                if (++gseen[h] > gseen[gbp]) gbp = h;
        }
        gs->add_breakpoint(gbp);
        m->debug_run(true);
        const bool ghit = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 5000);
        check(ghit && m->debug_stop_cpu() == gs && gs->get_pc() == gbp, "gs breakpoint stops the machine, gs in front");
        if (!ghit || m->debug_stop_cpu() != gs || gs->get_pc() != gbp)
            printf("  (gs breakpoint #%04X, %u times in %zu of history: %s, gs PC #%04X, stop cpu %s, paused %d stepping %d,"
                   " reason %d, frames %llu)\n",
                   gbp, (unsigned)gseen[gbp], ghistory.size(), ghit ? "stopped" : "not stopped in 5 s", gs->get_pc(),
                   m->debug_stop_cpu() == gs ? "gs" : "other", m->debug_paused(), m->debug_stepping(),
                   m->debug_stop_reason(), (unsigned long long)machine_frames(m));
        gs->remove_breakpoint(gbp);
        if (!ghit) {
            m->debug_pause();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
        }
    }

    size_t fields = 0;
    for (unsigned int i = 0; i < m->dm->device_count; i++)
        fields += m->dm->get_device(i)->device->get_device_fields().size();
    check(fields > 20, "devices tab has fields");

    window_check(window, cpu, check);

    // The window's scale: the views' font and their rows follow it, the keys step it, 100 % gives every size back
    {
        auto * w = static_cast<DebuggerWindow *>(window);
        // Every tab bar of the window: one font, one height
        const auto tabs_alike = [&](const char * what) {
            QString sizes;
            bool same = true;
            const QList<QTabBar *> bars = window->findChildren<QTabBar *>();
            for (QTabBar * b : bars) {
                sizes += QString(" %1pt/%2px").arg(b->font().pointSizeF()).arg(b->count() ? b->tabRect(0).height() : -1);
                same = same && b->font() == bars.front()->font() &&
                       (b->count() == 0 || b->tabRect(0).height() == bars.front()->tabRect(0).height());
            }
            check(same, what);
            if (!same) printf("  (tab bars:%s)\n", sizes.toUtf8().constData());
        };
        tabs_alike("zoom: every tab bar of one font and height as opened");
        const double z0 = w->zoom();
        w->set_zoom(1.0);
        QApplication::processEvents();
        DisasmView * code = window->findChild<DisasmView *>();
        QToolBar * tb = window->findChild<QToolBar *>();
        const int rows1 = code ? code->viewport()->height() / QFontMetrics(code->font()).height() : 0;
        const int h1 = code ? QFontMetrics(code->font()).height() : 0;
        const QSize icons1 = tb ? tb->iconSize() : QSize();
        const int ui1 = QFontMetrics(window->font()).height();
        QTabBar * bar = window->findChild<QTabBar *>();
        const qreal tab1 = bar ? bar->font().pointSizeF() : 0;
        QElapsedTimer switch_time;
        switch_time.start();
        w->set_zoom(2.0);
        window->repaint();
        QApplication::processEvents();
        const qint64 switch_ms = switch_time.elapsed();
        const int h2 = code ? QFontMetrics(code->font()).height() : 0;
        const int rows2 = code ? code->viewport()->height() / h2 : 0;
        tabs_alike("zoom 200 %: every tab bar of one font and height");
        // Tabs, buttons and labels: the window's one font, grown slower than the views
        bool frame_one = bar && bar->font().pointSizeF() > tab1 && bar->font() == window->font();
        for (QWidget * f : window->findChildren<QWidget *>())
            if ((qobject_cast<QAbstractButton *>(f) || qobject_cast<QLabel *>(f)) && !f->property("dbg_mono").toBool())
                frame_one = frame_one && f->font() == window->font();
        const bool tabs_slower = frame_one && bar->font().pointSizeF() / tab1 < 1.9;
        bool views = true;
        for (QWidget * v : window->findChildren<QWidget *>())
            if (v->property("dbg_mono").toBool())
                if (std::fabs(v->font().pointSizeF() - dbg::font().pointSizeF()) >= 0.01) {
                    views = false;
                    printf("  (%s %s at %.1f pt, the views at %.1f)\n", v->metaObject()->className(),
                           v->objectName().toUtf8().constData(), v->font().pointSizeF(), dbg::font().pointSizeF());
                }
        check(code && h2 >= h1 * 18 / 10 && rows2 < rows1 && views && tb && tb->iconSize() == icons1 * dbg::zoom(dbg::Zoom::Icons, 2.0) &&
                  QFontMetrics(window->font()).height() > ui1 * 13 / 10 && tabs_slower,
              "zoom 200 %: views twice the size, tabs, buttons, labels one font, slower");
        if (!(tabs_slower && h2 >= h1 * 18 / 10 && rows2 < rows1 && views))
            printf("  (rows %d -> %d, row %d -> %d px, views %d, tab %.1f -> %.1f pt, window %.1f pt, frame one %d)\n", rows1,
                   rows2, h1, h2, views, tab1, bar ? bar->font().pointSizeF() : 0.0, window->font().pointSizeF(),
                   frame_one);
        printf("  (zoom 100 -> 200 %%: %lld ms, layout and one full paint)\n", (long long)switch_ms);
        QAction * smaller = nullptr;
        for (QAction * a : window->actions())
            if (a->text() == "Smaller") smaller = a;
        if (smaller) smaller->trigger();
        const bool stepped = std::fabs(w->zoom() - 1.75) < 0.001;
        w->set_zoom(1.0);
        QApplication::processEvents();
        check(stepped && QFontMetrics(code->font()).height() == h1 && tb->iconSize() == icons1 &&
                  QFontMetrics(window->font()).height() == ui1,
              "zoom: a step down, 100 % gives the sizes back");
        w->set_zoom(z0);
    }

    m->debug_run(true);
    check(wait_for([m]() { return !m->debug_paused(); }, 2000), "run again");
    return failed;
}

} // namespace

int main(int argc, char * argv[])
{
    QApplication app(argc, argv);
    std::signal(SIGINT, on_quit_signal);
    std::signal(SIGTERM, on_quit_signal);
    QTimer quit_poll;
    QObject::connect(&quit_poll, &QTimer::timeout, [&app, &quit_poll]() {
        if (g_quit) {
            quit_poll.stop();
            for (QWidget * w : QApplication::topLevelWidgets()) w->close();
            app.quit();
        }
    });
    quit_poll.start(200);
    app.setApplicationName("unreal-debugger");
    // ecat's main window registers the views' font from its resources; without it Qt searches every family for an
    // alias of the missing "Consolas" when the window opens
    QFontDatabase::addApplicationFont(":/fonts/consolas");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption modelOpt("model", "Machine model (PENTAGON, 48K, 128K, ...)", "model", "PENTAGON");
    QCommandLineOption noGsOpt("no-gs", "No General Sound card");
    QCommandLineOption cardOpt("card", "The card in zxbus.1: neogs (default), multisound, ...", "card", "neogs");
    QCommandLineOption measureOpt("measure", "Measure N seconds without and with the window, then exit", "seconds");
    parser.addOption(modelOpt);
    parser.addOption(noGsOpt);
    parser.addOption(cardOpt);
    QCommandLineOption shotOpt("screenshot", "Stop, step, set a breakpoint, save the window to a PNG and exit", "png");
    parser.addOption(measureOpt);
    parser.addOption(shotOpt);
    QCommandLineOption checkOpt("self-check", "Drive the debugger API through the window's operations, report, exit");
    parser.addOption(checkOpt);
    QCommandLineOption listOpt("list-memory", "Print the dump tabs and the \"+\" menu the window builds, exit");
    parser.addOption(listOpt);
    QCommandLineOption connectOpt("connect", "A running emulator's WebAPI instead of a machine of its own", "host:port");
    QCommandLineOption zoomOpt("zoom", "The debugger window's scale (1.5 = 150 %)", "scale");
    parser.addOption(zoomOpt);
    QCommandLineOption emulatorOpt("emulator", "With --connect: the emulator's id or symbolic id (default: the first)", "id");
    parser.addOption(connectOpt);
    parser.addOption(emulatorOpt);
    parser.addPositionalArgument("file", "Snapshot, tape or disk to load");
    parser.process(app);

    MachineOptions options;
    options.model = parser.value(modelOpt).toStdString();
    options.card = parser.isSet(noGsOpt) ? std::string() : parser.value(cardOpt).toStdString();
    if (!parser.positionalArguments().isEmpty()) options.file = parser.positionalArguments().first().toStdString();

    std::string error;
    const bool remote = parser.isSet(connectOpt);
    std::unique_ptr<ecatshim::Emulator> machine;
    if (remote) {
        RemoteOptions ro;
        const QString where = parser.value(connectOpt);
        const int colon = where.lastIndexOf(':');
        ro.host = (colon > 0 ? where.left(colon) : where).toStdString();
        if (colon > 0) ro.port = (uint16_t)where.mid(colon + 1).toUInt();
        ro.emulator = parser.value(emulatorOpt).toStdString();
        machine = CreateRemoteMachine(ro, error);
        if (!machine && !parser.isSet(listOpt)) {
            //No suitable emulator yet: a normal course, the debugger waits for one (a look a second), nothing logged
            QProgressDialog waiting(QString("No emulator: waiting for %1 at %2")
                                        .arg(ro.emulator.empty() ? QString("one") : QString::fromStdString(ro.emulator))
                                        .arg(where),
                                    "Quit", 0, 0);
            waiting.setWindowTitle("unreal-debugger");
            waiting.setMinimumDuration(0);
            QTimer retry;
            QObject::connect(&retry, &QTimer::timeout, [&]() {
                machine = CreateRemoteMachine(ro, error);
                if (machine) waiting.accept();
            });
            retry.start(1000);
            if (waiting.exec() != QDialog::Accepted || !machine) return 0;
        }
        options.model = "remote";
        options.card = where.toStdString();
    } else {
        machine = CreateUnrealMachine(options, error);
    }
    if (!machine) {
        fprintf(stderr, "cannot create the machine: %s\n", error.c_str());
        return 1;
    }

    if (parser.isSet(listOpt)) {
        // As MemoryTabs builds them: each processor's space, the paged memories in the pages tab, then mappers and
        // memories as tabs, the rest in "+"
        ecatshim::Emulator * m = machine.get();
        for (ecatshim::CPU * c : m->dm->get_cpus()) printf("tab  %-20s cpu space\n", c->name.c_str());
        for (unsigned int i = 0; i < m->dm->device_count; i++) {
            ecatshim::ComputerDevice * d = m->dm->get_device(i)->device.get();
            auto * a = dynamic_cast<ecatshim::AddressableDevice *>(d);
            if (a == nullptr || a->get_size() == 0 || dynamic_cast<ecatshim::CPU *>(d) != nullptr) continue;
            auto * mem = dynamic_cast<ecatshim::Memory *>(d);
            const bool tab = dynamic_cast<ecatshim::MemoryMapper *>(d) != nullptr || mem != nullptr;
            const bool paged = mem != nullptr && mem->page_size() > 0;
            printf("%s %-20s %8u  %s\n", paged ? "page" : (tab ? "tab " : "more"), d->name.c_str(), a->get_size(), d->type.c_str());
        }
        return 0;
    }

    ScreenView screen(machine.get());
    if (!remote) screen.show();

    if (parser.isSet(measureOpt)) {
        // Three windows of the same length: the screen alone; debug mode on as the window sets it (breakpoint
        // checks, PC history), no window; the screen and the debugger following the run. The process CPU holds
        // the emulation thread, the GUI thread's own CPU the window's timer and painting
        const int seconds = parser.value(measureOpt).toInt();
        // A fourth window: the debugger over the paused machine, where nothing changes and nothing may be painted.
        // The window's own windows start a second after it opens and after the pause
        PaintCounter paints(&screen);
        app.installEventFilter(&paints);
        struct Sample { uint64_t frames; double cpu; double gui; qint64 ms; PaintCounter::Count sp, dp; uint64_t requests, bytes; };
        QElapsedTimer clock;
        auto sample = [&]() {
            Sample x{machine_frames(machine.get()), process_cpu_seconds(), thread_cpu_seconds(),
                     clock.elapsed(), paints.screen, paints.debugger, 0, 0};
            RemoteMachineTraffic(machine.get(), x.requests, x.bytes);
            return x;
        };
        std::vector<Sample> s;
        GenericDbgWnd * dbg = nullptr;
        ecatshim::Emulator * m = machine.get();
        QTimer::singleShot(2000, [&]() { clock.start(); s.push_back(sample()); });
        QTimer::singleShot(2000 + seconds * 1000, [&]() {
            s.push_back(sample());
            m->debug_attach(true);
            m->debug_run(true);
        });
        QTimer::singleShot(2000 + seconds * 2000, [&]() {
            s.push_back(sample());
            paints.widgets.clear();
            dbg = CreateDebuggerWindow(nullptr, m, nullptr);
            if (parser.isSet(zoomOpt)) static_cast<DebuggerWindow *>(dbg)->set_zoom(parser.value(zoomOpt).toDouble());
            dbg->show();
            m->debug_run(true);
        });
        // The window's first second (creation, fonts, layout, the first paint) is in no window
        QTimer::singleShot(3000 + seconds * 2000, [&]() { s.push_back(sample()); paints.widgets.clear(); });
        QTimer::singleShot(3000 + seconds * 3000, [&]() {
            s.push_back(sample());
            printf("debugger paints by widget, running window:\n");
            for (const auto & w : paints.widgets)
                printf("  %-48s %8llu events %12llu px\n", w.first.c_str(), (unsigned long long)w.second.events,
                       (unsigned long long)w.second.pixels);
            m->debug_pause();
        });
        // A second for the pause to land and its one repaint, then the quiet window
        QTimer::singleShot(4000 + seconds * 3000, [&]() { s.push_back(sample()); paints.widgets.clear(); });
        QTimer::singleShot(4000 + seconds * 4000, [&]() {
            s.push_back(sample());
            printf("model=%s card=%s, %d s each\n", options.model.c_str(), options.card.c_str(), seconds);
            const char * names[4] = {"screen only", "debug mode, no window", "debugger window", "window, paused"};
            const size_t from[4] = {0, 1, 3, 5}, to[4] = {1, 2, 4, 6};
            for (int i = 0; i < 4; i++) {
                const Sample & a = s[from[i]];
                const Sample & b = s[to[i]];
                const double t = (b.ms - a.ms) / 1000.0;
                printf("%-22s %6.2f frames/s, process CPU %5.1f %%, GUI thread %5.1f %%, paints/s screen %6.1f "
                       "(%7.0f px) debugger %6.1f (%8.0f px)\n",
                       names[i], (b.frames - a.frames) / t, 100.0 * (b.cpu - a.cpu) / t, 100.0 * (b.gui - a.gui) / t,
                       (b.sp.events - a.sp.events) / t, (b.sp.pixels - a.sp.pixels) / t,
                       (b.dp.events - a.dp.events) / t, (b.dp.pixels - a.dp.pixels) / t);
                if (remote)
                    printf("%-22s %6.1f WebAPI requests/s, %8.1f KB/s received\n", "", (b.requests - a.requests) / t,
                           (b.bytes - a.bytes) / t / 1024.0);
            }
            printf("debugger paints by widget, paused window:\n");
            for (const auto & w : paints.widgets)
                printf("  %-48s %8llu events %12llu px\n", w.first.c_str(), (unsigned long long)w.second.events,
                       (unsigned long long)w.second.pixels);
            fflush(stdout);
            dbg->close();
            app.quit();
        });
        const int rc = app.exec();
        delete dbg;
        return rc;
    }

    GenericDbgWnd * dbg = CreateDebuggerWindow(nullptr, machine.get(), nullptr);
    if (parser.isSet(zoomOpt)) static_cast<DebuggerWindow *>(dbg)->set_zoom(parser.value(zoomOpt).toDouble());
    dbg->show();

    if (parser.isSet(checkOpt)) {
        // Qt's own file dialogs: the check answers them, a native one it cannot reach
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        setvbuf(stdout, nullptr, _IONBF, 0);  // every line out at once, also when a check hangs
        QTimer::singleShot(0, [&]() {
            // After the boot (200 frames, 4 s of the machine's time): the ROM's menu, interrupts on
            ecatshim::Emulator * m = machine.get();
            wait_for([m]() { return machine_frames(m) >= 200; }, 30000);
            const int failed = self_check(m, dbg);
            QDir(scratch_dir()).removeRecursively();  // this process's files only
            printf("self-check: %s\n", failed ? "FAILED" : "all passed");
            fflush(stdout);
            dbg->close();
            QApplication::exit(failed ? 2 : 0);
        });
    }

    if (parser.isSet(shotOpt)) {
        // The window over a stopped machine: run, pause, three steps, a breakpoint two lines below PC, a picture
        machine->debug_run(true);
        ecatshim::Emulator * m = machine.get();
        const QString path = parser.value(shotOpt);
        // The history tab while the machine runs (read once a second, the machine not held)
        QTimer::singleShot(300, [dbg]() {
            if (QTabWidget * side = tabs_with(static_cast<DebuggerWindow *>(dbg), "History"))
                for (int i = 0; i < side->count(); i++)
                    if (side->tabText(i) == "History") side->setCurrentIndex(i);
        });
        QTimer::singleShot(2700, [dbg, path]() { dbg->grab().save(QString(path).replace(".png", "-history-running.png")); });
        // A device's history recorded while it runs on: the board's port accesses
        QTimer::singleShot(2800, [dbg]() {
            if (QComboBox * source = static_cast<DebuggerWindow *>(dbg)->findChild<HistoryView *>()->findChild<QComboBox *>()) {
                source->setCurrentIndex(source->findData("board.ports"));
                if (QCheckBox * record = static_cast<DebuggerWindow *>(dbg)->findChild<HistoryView *>()->findChild<QCheckBox *>())
                    record->setChecked(true);
            }
        });
        QTimer::singleShot(3000, [m]() { m->debug_pause(); });
        QTimer::singleShot(3500, [m]() {
            ecatshim::CPU * c = m->dm->get_cpus().front();
            for (int i = 0; i < 3; i++) m->debug_step(c);
            c->add_breakpoint(c->get_pc() + 6);
        });
        QTimer::singleShot(4500, [dbg, m, &screen, path, remote]() {
            dbg->grab().save(path);
            if (!remote) screen.grab().save(QString(path).replace(".png", "-screen.png"));
            // The card's processor in front, with the devices tab
            auto * w = static_cast<DebuggerWindow *>(dbg);
            const std::vector<ecatshim::CPU *> cpus = m->dm->get_cpus();
            if (cpus.size() > 1) w->select_cpu(cpus[1]);
            if (QTabWidget * side = tabs_with(w, "Devices"))
                for (int i = 0; i < side->count(); i++)
                    if (side->tabText(i) == "Devices") side->setCurrentIndex(i);
            // One entry open: the sound chips' report as a tree
            if (DevicesView * devices = w->findChild<DevicesView *>()) {
                devices->refresh();
                for (int i = 0; i < devices->topLevelItemCount(); i++)
                    if (devices->topLevelItem(i)->data(0, Qt::UserRole).toString() == "ay") devices->topLevelItem(i)->setExpanded(true);
            }
            QTimer::singleShot(300, [dbg, m, path, remote]() {
                dbg->grab().save(QString(path).replace(".png", "-gs.png"));
                // The board's port history recorded before the pause, in the history tab
                if (QTabWidget * side = tabs_with(static_cast<DebuggerWindow *>(dbg), "History"))
                    for (int i = 0; i < side->count(); i++)
                        if (side->tabText(i) == "History") side->setCurrentIndex(i);
                dbg->grab().save(QString(path).replace(".png", "-histories.png"));
                // The main Z80 again: the listing across the #8000 window's start, the pages tab
                auto * w = static_cast<DebuggerWindow *>(dbg);
                w->select_cpu(m->dm->get_cpus().front());
                if (QTabWidget * t = tabs_with(w, QString::fromStdString(m->dm->get_cpus().front()->name + " :")))
                    if (DisasmView * code = t->currentWidget()->findChild<DisasmView *>()) code->go_to(0x7FF0);
                if (QTabWidget * t = tabs_with(w, "pages"))
                    for (int i = 0; i < t->count(); i++)
                        if (t->tabText(i) == "pages") t->setCurrentIndex(i);
                QTimer::singleShot(400, [dbg, m, path, remote]() {
                    dbg->grab().save(QString(path).replace(".png", "-banks.png"));
                    // Remote, the emulator closed by whoever runs this (up to 15 s): the last state gray, the devices'
                    if (remote && wait_for([m]() { return !m->debug_online(); }, 15000)) {
                        if (QTabWidget * side = tabs_with(static_cast<DebuggerWindow *>(dbg), "Devices"))
                            for (int i = 0; i < side->count(); i++)
                                if (side->tabText(i) == "Devices") side->setCurrentIndex(i);
                        wait_for([]() { return false; }, 1500);
                        dbg->grab().save(QString(path).replace(".png", "-offline.png"));
                        // Offline nothing changes the machine: every action but the views' moves is off
                        for (QToolBar * bar : dbg->findChildren<QToolBar *>())
                            for (QAction * a : bar->actions())
                                if (!a->text().isEmpty()) printf("offline action %s: %s\n", a->isEnabled() ? "on" : "off", qPrintable(a->text()));
                    }
                    QApplication::quit();
                });
            });
        });
    }
    const int rc = app.exec();
    delete dbg;
    return rc;
}
