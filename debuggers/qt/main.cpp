// unreal-debugger: eCat3's debugger window over an unreal-ng machine.
//
//   unreal-debugger [--model PENTAGON] [--card neogs|gs|gs-lw|multisound | --no-gs] [file.sna|.z80|.tap|.trd|...]
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
#include <QAction>
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
#include <QJsonArray>
#include <QJsonDocument>
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
#include <QTableWidget>
#include <QPushButton>
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

#include "debuggercontrol.h"
#include "guicontrol/host.h"
#include "guicontrol/windowbinding.h"
#include "remotemachine.h"
#include "unrealmachine.h"
#include "dialogs/debugger/addressmenu.h"
#include "dialogs/debugger/bankstrip.h"
#include "dialogs/debugger/debuggerwindow.h"
#include "dialogs/debugger/debugstyle.h"
#include "dialogs/debugger/disasmview.h"
#include "dialogs/debugger/ttdcontrols.h"
#include "dialogs/debugger/memoryview.h"
#include "dialogs/debugger/nextviews.h"
#include "dialogs/debugger/pagedmemory.h"
#include "dialogs/debugger/registersview.h"
#include "dialogs/debugger/sideviews.h"

namespace {

// SIGINT / SIGTERM end the event loop, so the machine is destroyed: over the WebAPI that restores the debug mode and
// turns off the histories the debugger started (killed outright, they stay as they are)
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
                // A dialog that stays open after 20 answers (a file that is not there, a folder that cannot be written)
                // is closed: the check fails on its result instead of waiting for ever
                if (ticks >= 400) {
                    QMetaObject::invokeMethod(f, "reject", Qt::QueuedConnection);
                    return;
                }
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
    // No scratch/ above the binary (a CI checkout has none): the system temp folder, not a path from the root of the
    // file system, which macOS does not let a user create
    QDir d(QCoreApplication::applicationDirPath());
    while (!d.exists("scratch") && d.cdUp()) {}
    const QString leaf = "debugger-self-check/" + QString::number(QCoreApplication::applicationPid());
    QString path = d.exists("scratch") ? d.filePath("scratch/" + leaf) : QDir::temp().filePath(leaf);
    if (!QDir().mkpath(path)) {
        path = QDir::temp().filePath(leaf);
        QDir().mkpath(path);
    }
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
        // Over the WebAPI the edits' events come through the long poll: let them land (a stop's refresh brings the
        // listing back to PC) before the row is looked for
        wait_for([]() { return false; }, 600);
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
            printf("  (row y %d, editor %s, #C000 = %02X %02X, listing \"%s\", tab %d, listing %s %dx%d)\n", y,
                   editor ? "opened" : "none", cpu->peek_mem(0xC000), cpu->peek_mem(0xC001),
                   code->describe(0xC000).toUtf8().constData(), cpu_tabs->currentIndex(),
                   code->isVisible() ? "shown" : "hidden", vp->width(), vp->height());
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
// The GUI control plane (guicontrol/, as unreal-qt's): the window `debugger` with the views disassembly and memory,
// for unreal-gui and the agents. UNREAL_GUI_CONTROL_DIR and UNREAL_GUI_CONTROL_PORT as for every GUI program. Null
// when the host cannot listen (the debugger runs on without it)
std::unique_ptr<guicontrol::Host> start_gui_control(DebuggerWindow * window, ecatshim::Emulator * m, bool remote)
{
    guicontrol::HostOptions options;
    options.program = "unreal-debugger";
    options.version = QCoreApplication::applicationVersion();
    options.FromEnvironment();
    // The WebAPI the machine is reached at: --connect's; in process none
    options.endpoints = [m, remote]() {
        QJsonArray endpoints;
        if (remote) endpoints.append(QString::fromStdString(RemoteMachineEndpoint(m)));
        return endpoints;
    };
    auto host = std::make_unique<guicontrol::Host>(std::move(options));
    QString error;
    if (!host->Start(&error)) {
        fprintf(stderr, "GUI control: not started: %s\n", qPrintable(error));
        return nullptr;
    }
    new guicontrol::WindowBinding(*host, window, "debugger");
    DebuggerControl::Hooks hooks;
    hooks.instance = [m, remote]() {
        return QString::fromStdString(remote ? RemoteMachineId(m) : UnrealMachineId(m));
    };
    hooks.endpoint = [m, remote]() { return remote ? QString::fromStdString(RemoteMachineEndpoint(m)) : QString(); };
    hooks.coreAction = [m, remote, window](const QString &action, const QJsonObject &params,
                                           std::function<void(const guicontrol::Result &)> done) {
        const std::string text = QJsonDocument(params).toJson(QJsonDocument::Compact).toStdString();
        MachineActionDone answer = [done](const MachineActionResult &r) {
            const QJsonObject body = QJsonDocument::fromJson(QByteArray::fromStdString(r.body)).object();
            done(r.ok ? guicontrol::Result::Success(body)
                      : guicontrol::Result::Failure(r.error.empty() ? QString(guicontrol::errors::kFailed)
                                                                    : QString::fromStdString(r.error),
                                                    QString::fromStdString(r.message), body));
        };
        if (remote) RemoteMachineAction(m, action.toStdString(), text, window, std::move(answer));
        else UnrealMachineAction(m, action.toStdString(), text, window, std::move(answer));
    };
    new DebuggerControl(*host, window, std::move(hooks));
    return host;
}


// The time group on a sound card's CPU (gs-debugger design §8): the card's tab in front, its back steps, the Step of
// the past, Run Back to the card's breakpoints, the moment naming both CPUs, the refusals said; the main CPU's tab
// acts on the main CPU again. The machine stands in the recorded history (detached) when called
void card_ttd_check(ecatshim::Emulator * m, DebuggerWindow * dw, TtdControls * ttd, ecatshim::CPU * cpu,
                    ecatshim::CPU * gs, const std::function<void(bool, const char *)> &check)
{
    struct Said {
        QString title, text;
        bool warning = false;
        int count = 0;
    } said;
    ttd->inform = [&said](const QString &title, const QString &text, bool warning) {
        said.title = title;
        said.text = text;
        said.warning = warning;
        said.count++;
    };
    // The card in front: "gs" (General Sound, NeoGS) or "ms" (the MultiSound's GS); the other one the machine lacks
    const QString name = QString::fromStdString(gs->name);
    const std::string missing = name == "gs" ? "ms" : "gs";
    auto card = [ttd, name]() {
        QJsonObject p;
        ttd->call("position", {}, &p);
        for (const QJsonValue &v : p.value("cards").toArray())
            if (v.toObject().value("cpu").toString() == name) return v.toObject();
        return QJsonObject();
    };
    auto count = [](const QJsonObject &c) { return (uint64_t)c.value("instruction").toInteger(); };
    QAction * step = nullptr;
    QAction * step_over = nullptr;
    QAction * step_out = nullptr;
    for (QAction * a : dw->actions()) {
        if (a->text() == "Step") step = a;
        if (a->text() == "Step over") step_over = a;
        if (a->text() == "Step out of the routine") step_out = a;
    }

    // The readout's words, inside an instruction of the main CPU and at its boundary
    const QJsonObject inside{{"cpu", "gs"}, {"known", true}, {"instruction", 1200}, {"pc", 0x0A3C}, {"card_position", true},
                             {"main", QJsonObject{{"pc", 0x8009}, {"t_offset", 7}, {"inside", true}}}};
    QJsonObject boundary = inside;
    boundary["main"] = QJsonObject{{"pc", 0x800B}, {"t_offset", 0}, {"inside", false}};
    QJsonObject away = inside;
    away["card_position"] = false;
    const QJsonObject unknown{{"cpu", "ms"}, {"known", false}, {"instruction", QJsonValue()}, {"pc", 0}};
    check(TtdControls::describe_card(inside) ==
              "gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the next instruction",
          "ttd card: readout inside a main instruction");
    check(TtdControls::describe_card(boundary) == "gs at instruction 1200, PC #0A3C; main CPU PC #800B, at its instruction boundary",
          "ttd card: readout at a main boundary");
    check(TtdControls::describe_card(away) == "gs at instruction 1200, PC #0A3C" &&
              TtdControls::describe_card(unknown) == "ms: no instruction counts in this recording",
          "ttd card: readout without a card position, without counts");

    // The card's tab in front: the back steps are the card's. The last stop first taken by the window (its event may
    // come late, under load seconds late): a stop brings its processor's tab to the front once, when taken
    check(wait_for([m, dw]() { return dw->taken_stop() == m->debug_stop_serial(); }, 15000),
          "ttd card: the window has taken the last stop");
    dw->select_cpu(gs);
    QApplication::processEvents();
    ttd->poll(true);
    const QJsonObject c0 = card();
    check(ttd->card_cpu() == name && c0.value("known").toBool(), "ttd card: the card's tab, its count known");
    printf("    (card %s at instruction %llu)\n", name.toUtf8().constData(), (unsigned long long)c0.value("instruction").toInteger());
    check(ttd->step_back_action()->isEnabled() && ttd->run_back_action()->isEnabled() &&
              ttd->step_over_back_action()->isEnabled() && ttd->step_back_action()->toolTip().contains(name + " instruction") &&
              ttd->step_over_back_action()->toolTip().contains(name + " instruction"),
          "ttd card: Step Back, Step Over Back and Run Back the card's");
    check(ttd->moment().contains(name + " at instruction " + QString::number(count(c0))), "ttd card: the moment names the card");

    // Step Back: one card instruction; the card's view reads the card there, the moment names both CPUs
    const uint64_t n0 = count(c0);
    ttd->step_back();
    QJsonObject c1 = card();
    check(c1.value("card_position").toBool() && count(c1) + 1 == n0 && said.count == 0, "ttd card: Step Back, one card instruction");
    check(gs->get_pc() == (unsigned int)c1.value("pc").toInteger(), "ttd card: the card's tab shows the card at the position");
    // A move in time is no stop: its events, and any late one of the stop before, find the stop taken, and the
    // card's tab stays in front
    QApplication::processEvents();
    check(dw->current_cpu() == gs && ttd->card_cpu() == name, "ttd card: the card's tab stays in front after the move");
    check(cpu->get_pc() == (unsigned int)c1.value("main").toObject().value("pc").toInteger(),
          "ttd card: the main CPU's tab at its boundary (PC of the next instruction)");
    check(ttd->moment().contains(TtdControls::describe_card(c1)) && ttd->moment().contains("main CPU PC"),
          "ttd card: the moment names both CPUs");
    if (!ttd->moment().contains(TtdControls::describe_card(c1)))
        printf("  (moment '%s', readout '%s')\n", ttd->moment().toUtf8().constData(),
               TtdControls::describe_card(c1).toUtf8().constData());

    // Further back until the card's moment is inside a main instruction (an LDIR, a long OUT): "+N T"
    bool met_inside = c1.value("main").toObject().value("inside").toBool();
    for (int i = 0; i < 40 && !met_inside; i++) {
        ttd->step_back();
        c1 = card();
        met_inside = c1.value("main").toObject().value("inside").toBool();
    }
    const QJsonObject main_part = c1.value("main").toObject();
    check(met_inside && ttd->moment().contains(QString("+%1 T into the next instruction").arg(main_part.value("t_offset").toInteger())),
          "ttd card: inside a main instruction, the moment says +N T");

    // The Step of the past (F7 on the card's tab): one card instruction forward through the history
    const uint64_t n1 = count(c1);
    if (step != nullptr) step->trigger();
    QJsonObject c2 = card();
    check(step != nullptr && c2.value("card_position").toBool() && count(c2) == n1 + 1 && m->debug_paused(),
          "ttd card: Step (F7) in the past, one card instruction forward");

    // Step Over Back (Shift+F8): the card back, one instruction or over a whole card routine
    said = Said();
    ttd->step_over_back();
    const QJsonObject cb = card();
    check(said.count == 0 && cb.value("card_position").toBool() && count(cb) < count(c2) &&
              ttd->last_result().startsWith("Step Over Back: " + name),
          "ttd card: Step Over Back, the card back");
    // Step over (F8) and Step out (Ctrl+F8) in the past: the card's steps through the history, no live run. From well
    // inside the history: the checks so far stand at its end, where F7 / F8 / Ctrl+F8 step on live (D-3) and leave the
    // machine inside the card's run (a move in time from there first leaves it)
    const uint64_t past = count(cb) > 5000 ? count(cb) - 5000 : count(cb);
    auto seek_card = [&](uint64_t n) {
        ttd->call("seek", {{"cpu", name.toStdString()}, {"instruction", std::to_string(n)}}, nullptr);
        ttd->poll(true);
    };
    seek_card(past);
    if (step_over != nullptr) step_over->trigger();
    const QJsonObject co = card();
    check(step_over != nullptr && said.count == 0 && co.value("card_position").toBool() && count(co) > past &&
              m->debug_paused() && ttd->last_result().startsWith("Step over: " + name),
          "ttd card: Step over (F8) in the past, through the history");
    if (!ttd->last_result().isEmpty()) printf("    (%s)\n", ttd->last_result().toUtf8().constData());
    seek_card(past);
    if (step_out != nullptr) step_out->trigger();
    const QJsonObject cu = card();
    check(step_out != nullptr && said.count == 0 && cu.value("card_position").toBool() && count(cu) > past &&
              m->debug_paused() && ttd->last_result().startsWith("Step out: " + name),
          "ttd card: Step out (Ctrl+F8) in the past, through the history");
    if (!ttd->last_result().isEmpty()) printf("    (%s)\n", ttd->last_result().toUtf8().constData());
    // Run to cursor (F4) in the past: the card's next instruction at the cursor, through the history; one never
    // reached is said and nothing moves (no live run)
    seek_card(past + 1);
    const unsigned int cursor = (unsigned int)card().value("pc").toInteger();
    seek_card(past);
    said = Said();
    const bool ran = ttd->step_forward("to", cursor);
    const QJsonObject ct = card();
    check(ran && said.count == 0 && ct.value("card_position").toBool() && count(ct) == past + 1 && m->debug_paused() &&
              ttd->last_result().startsWith("Run to the cursor: " + name),
          "ttd card: Run to cursor (F4) in the past, through the history");
    if (!ttd->last_result().isEmpty()) printf("    (%s)\n", ttd->last_result().toUtf8().constData());
    said = Said();
    const bool missed = ttd->step_forward("to", 0xFFFF);
    check(missed && said.count == 1 && count(card()) == past + 1,
          "ttd card: Run to cursor (F4) in the past never reached: said, nothing moved");
    if (said.count == 1) printf("    (said: %s)\n", said.text.toUtf8().constData());
    // The checks below start in the past too (their F7s replay the history, not live steps)
    const uint64_t base = count(c2) > 2000 ? count(c2) - 2000 : count(c2);
    seek_card(base);

    // Run Back without a card breakpoint: the core's refusal is said, nothing moved
    said = Said();
    ttd->run_back();
    check(said.count == 1 && said.warning && !said.text.isEmpty() && count(card()) == base,
          "ttd card: Run Back without a card breakpoint is refused, said");
    if (said.count == 1) printf("    (said: %s)\n", said.text.toUtf8().constData());

    // Run Back with one: at an earlier instruction of the card's PC, the breakpoint named
    const unsigned int at = (unsigned int)c1.value("pc").toInteger();
    for (int i = 0; i < 3; i++)
        if (step != nullptr) step->trigger();
    const uint64_t n3 = count(card());
    gs->add_breakpoint(at);
    said = Said();
    ttd->run_back();
    const QJsonObject c3 = card();
    gs->remove_breakpoint(at);
    char what[96];
    snprintf(what, sizeof(what), "ttd card: Run Back to the card's breakpoint #%04X", at);
    check(said.count == 0 && count(c3) < n3 && (unsigned int)c3.value("pc").toInteger() == at && gs->get_pc() == at &&
              ttd->last_result().contains("at the breakpoint"),
          what);
    if (said.count != 0) printf("    (said: %s)\n", said.text.toUtf8().constData());

    // The start of the history: Step Back answers at_start, said, nothing moved
    QJsonObject status;
    ttd->call("status", {}, &status);
    ttd->call("seek", {{"frame", std::to_string(status.value("session_start_frame").toInteger())}}, nullptr);
    ttd->poll(true);
    // The history's first card position: the core names it when asked for one before it
    QJsonObject before;
    ttd->call("seek", {{"cpu", name.toStdString()}, {"instruction", "0"}}, &before);
    const QString words = before.value("message").toString();
    const qsizetype from = words.lastIndexOf(' ');
    const std::string first = from > 0 ? words.mid(from + 1).toStdString() : std::string();
    check(before.value("status").toString() == "at_start" && !first.empty(), "ttd card: the history's first card position named");
    QJsonObject there;
    ttd->call("seek", {{"cpu", name.toStdString()}, {"instruction", first}}, &there);
    ttd->poll(true);
    const uint64_t s0 = count(card());
    said = Said();
    ttd->step_back();
    check(there.value("reached").toBool() && said.count == 1 && !said.warning && !said.text.isEmpty() && count(card()) == s0,
          "ttd card: Step Back at the start of the history is said");
    printf("    (first card position %s; reached %d)\n", first.c_str(), there.value("reached").toBool());
    if (said.count == 1) printf("    (said: %s)\n", said.text.toUtf8().constData());

    // A card the machine does not have: refused with the core's words (in process and over REST alike)
    QJsonObject none;
    check(!ttd->call("step-instruction", {{"cpu", missing}, {"dir", "back"}}, &none) && none.value("status").toString() == "no_card" &&
              !none.value("message").toString().isEmpty(),
          "ttd card: a missing card's move is refused (no_card)");

    // The main CPU's tab again: its own back steps (a main move ends the card position)
    ttd->call("seek", {{"frame", std::to_string(status.value("session_start_frame").toInteger() + 3)}}, nullptr);
    dw->select_cpu(cpu);
    QApplication::processEvents();
    ttd->poll(true);
    check(ttd->card_cpu().isEmpty() && ttd->step_over_back_action()->isEnabled() &&
              !ttd->step_back_action()->toolTip().contains(name),
          "ttd card: the main CPU's tab, its own back steps");
    const unsigned int r1 = reg(cpu, "R");
    ttd->step_back();
    check(reg(cpu, "R") != r1 && !card().value("card_position").toBool(), "ttd card: Step Back on the main CPU, no card position");
    ttd->inform = nullptr;
}

// The host reads 17 bytes through the ZX-DMA window (gszxdmatrace_test's way, through the shim: in process and over
// the WebAPI alike): the card's Z80 runs a few instructions put in a RAM window of its own (DI, select the ZX module
// at kCardBase, start it, JR $), the host an LDIR from #0000 (DI first: the ROM is diverted while the module runs);
// then the card stops and deselects the module. Every byte and register changed is put back. @p before runs just
// before the LDIR, @p after as soon as it ends (the host parked by a run to cursor: the DAC fetches sharing the port
// trace's ring evict ZX-DMA bytes within a few frames). False: not done (said)
bool zxdma_make_bytes(ecatshim::Emulator * m, ecatshim::CPU * cpu, ecatshim::CPU * gs, unsigned int &card_base,
                      const std::function<void()> &before, const std::function<void()> &after)
{
    card_base = 0x013FF8;  // the block crosses the page boundary #014000
    if (gs == nullptr) return false;
    unsigned int at = 0;
    for (const ecatshim::AddressWindow &w : gs->get_windows())
        if (at == 0 && w.kind == "RAM" && w.writable && w.start >= 0x4000 && w.size >= 0x100) at = w.start + w.size - 0x40;
    if (at == 0) {
        printf("  (ZX-DMA: the card has no RAM window above #4000)\n");
        return false;
    }
    auto run_frames = [m](uint64_t frames) {
        const uint64_t from = machine_frames(m);
        m->debug_run(false);
        wait_for([m, from, frames]() { return machine_frames(m) >= from + frames; }, 5000);
        m->debug_pause();
        return wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
    };
    const uint8_t start[] = {0xF3,                                         // DI
                             0x3E, 0x01, 0xD3, 0x1B,                       // DMA_MOD = 1 (ZX)
                             0x3E, uint8_t(card_base >> 16), 0xD3, 0x1C,   // HAD
                             0x3E, uint8_t(card_base >> 8), 0xD3, 0x1D,    // MAD
                             0x3E, uint8_t(card_base), 0xD3, 0x1E,         // LAD
                             0x3E, 0x80, 0xD3, 0x1F,                       // CST = #80: start
                             0x18, 0xFE};                                  // JR $
    const uint8_t stop[] = {0xF3, 0xAF, 0xD3, 0x1F, 0xD3, 0x1B, 0x18, 0xFE};  // DI : XOR A : CST = 0 : DMA_MOD = 0 : JR $
    const uint8_t idle[] = {0xF3, 0x18, 0xFE};                                // DI : JR $
    const uint8_t copy[] = {0xF3, 0x21, 0x00, 0x00, 0x11, 0x00, 0x90, 0x01, 17, 0x00, 0xED, 0xB0, 0x18, 0xFE};
    // What is put back: the card's bytes and registers, the host's
    std::vector<std::pair<unsigned int, unsigned int>> card_bytes, host_bytes;
    for (unsigned int i = 0; i < sizeof(start); i++) card_bytes.emplace_back(at + i, gs->peek_mem(at + i));
    for (unsigned int i = 0; i < 0x20; i++) host_bytes.emplace_back(0x8000 + i, cpu->peek_mem(0x8000 + i));
    for (unsigned int i = 0; i < 17; i++) host_bytes.emplace_back(0x9000 + i, cpu->peek_mem(0x9000 + i));
    const std::vector<std::string> names = {"PC", "AF", "BC", "DE", "HL", "IFF1", "IFF2"};
    std::map<std::string, unsigned int> card_regs, host_regs;
    for (const std::string &n : names) {
        card_regs[n] = reg(gs, n);
        host_regs[n] = reg(cpu, n);
    }
    auto load = [](ecatshim::CPU * c, unsigned int to, const uint8_t * bytes, size_t n) {
        for (size_t i = 0; i < n; i++) c->poke_mem(to + (unsigned int)i, bytes[i]);
        c->set_context_value("PC", to);
    };
    load(gs, at, start, sizeof(start));
    load(cpu, 0x8010, idle, sizeof(idle));
    bool ok = run_frames(3);
    auto mode = [m]() {
        const ecatshim::Emulator::Board b = m->read_board("neogs_dma");
        const ecatshim::Emulator::BoardControl * c = b.control("zx.mode");
        return c ? c->value : std::string("?");
    };
    const std::string started = mode();
    load(cpu, 0x8000, copy, sizeof(copy));
    before();
    m->debug_run_to(cpu, 0x8000 + sizeof(copy) - 2);
    ok = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000) && ok;
    const bool copied = reg(cpu, "PC") == 0x8000 + sizeof(copy) - 2;
    after();
    load(gs, at, stop, sizeof(stop));
    load(cpu, 0x8010, idle, sizeof(idle));
    ok = run_frames(2) && ok;
    // Debug mode back on, as the rest of the check runs (both CPUs still in their JR $), then everything put back
    m->debug_run(true);
    m->debug_pause();
    ok = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000) && ok;
    for (const auto &b : card_bytes) gs->poke_mem(b.first, b.second);
    for (const auto &b : host_bytes) cpu->poke_mem(b.first, b.second);
    for (const std::string &n : names) {
        if (card_regs[n] != ~0u) gs->set_context_value(n, card_regs[n]);
        if (host_regs[n] != ~0u) cpu->set_context_value(n, host_regs[n]);
    }
    // Over the WebAPI the registers read come from a snapshot taken again after a change: give it a moment
    wait_for([&]() {
        for (const std::string &n : names)
            if (reg(gs, n) != card_regs[n] || reg(cpu, n) != host_regs[n]) return false;
        return true;
    }, 2000);
    bool restored = true;
    for (const std::string &n : names) {
        const unsigned int card_now = reg(gs, n), host_now = reg(cpu, n);
        if (card_now != card_regs[n]) printf("  (card %s #%X, was #%X)\n", n.c_str(), card_now, card_regs[n]);
        if (host_now != host_regs[n]) printf("  (host %s #%X, was #%X)\n", n.c_str(), host_now, host_regs[n]);
        restored = restored && card_now == card_regs[n] && host_now == host_regs[n];
    }
    printf("  (ZX-DMA: card program at #%04X, module %s after the start, host LDIR %s; registers %s; card PC #%04X)\n", at,
           started.c_str(), copied ? "done" : "not done", restored ? "put back" : "NOT all put back", card_regs["PC"]);
    return ok && copied && restored;
}

// The ZX-DMA banners in sight (gui-card-debugger S4): over the main CPU's listing and over its dump
int banners_shown(QWidget * window)
{
    int n = 0;
    for (QLabel * l : window->findChildren<QLabel *>("zxdma_banner")) n += l->isVisibleTo(window) && !l->text().isEmpty() ? 1 : 0;
    return n;
}

// The main CPU's listing
DisasmView * main_listing(QWidget * window, ecatshim::CPU * cpu)
{
    QTabWidget * cpu_tabs = tabs_with(window, QString::fromStdString(cpu->name + " :"));
    return cpu_tabs ? cpu_tabs->widget(0)->findChild<DisasmView *>() : nullptr;
}

// The NeoGS DMA tab's ZX-DMA trace (gui-card-debugger B3.2): its toggle only with a NeoGS; expanded, the header and
// the empty table; Start, bytes moved, the rows (read, consecutive card addresses, the LDIR's PC), a second read adds
// nothing (since), the double-click hooks (Host PC to the main listing, the card address to the pages tab); Stop,
// Clear
void zxdma_trace_check(ecatshim::Emulator * m, QWidget * window, ecatshim::CPU * cpu, ecatshim::CPU * gs,
                       NextReportView * dma, bool neogs, const std::function<void(bool, const char *)> &check)
{
    ZxDmaTraceView * trace = dma->zxdma_trace();
    check(trace != nullptr && trace->toggle() != nullptr && trace->table() != nullptr &&
              trace->table()->columnCount() == ZxDmaTraceView::ColCount,
          "zx-dma trace: the DMA tab has the Trace toggle and table");
    if (trace == nullptr) return;
    if (!neogs) {
        dma->refresh();
        check(trace->isHidden(), "zx-dma trace: no toggle without a NeoGS");
        return;
    }
    dma->refresh();
    check(!trace->isHidden() && !trace->expanded() && trace->table()->isHidden() == false &&
              !trace->table()->isVisibleTo(trace),
          "zx-dma trace: the toggle shown, collapsed");
    trace->capture("stop");
    trace->capture("clear");
    trace->set_expanded(true);
    const QString idle = trace->header()->text();
    check(trace->expanded() && trace->table()->isVisibleTo(trace) && trace->toggle()->text().contains(QString::fromUtf8("▾")) &&
              idle.startsWith("not capturing") && idle.contains("Start records the bytes") && idle.contains("porttrace") &&
              trace->table()->rowCount() == 0,
          "zx-dma trace: expanded, not capturing, the how-to line, no rows");
    check(trace->button("start") && trace->button("stop") && trace->button("clear"), "zx-dma trace: Start, Stop, Clear");
    QString capturing;
    int rows = 0;
    uint64_t seq = 0;
    QTableWidget * t = trace->table();
    unsigned int base = 0;
    QString divert_banner;
    int divert_shown = 0;
    bool divert_marked = false;
    const bool made = zxdma_make_bytes(
        m, cpu, gs, base,
        [&]() {
            trace->button("clear")->click();
            trace->button("start")->click();
            capturing = trace->header()->text();
        },
        [&]() {
            dma->refresh();
            rows = t->rowCount();
            seq = trace->last_seq();
            trace->button("stop")->click();
            // S4: the host parked with the module diverting: the banner over the main listing and dump (at the stop)
            auto * dw = dynamic_cast<DebuggerWindow *>(window);
            DisasmView * code = main_listing(window, cpu);
            wait_for([&]() { return dw != nullptr && !dw->zxdma_banner_text().isEmpty() && banners_shown(window) == 2; }, 3000);
            divert_banner = dw ? dw->zxdma_banner_text() : QString();
            divert_shown = banners_shown(window);
            divert_marked = code != nullptr && code->runtime_bytes(0x0000) && code->runtime_bytes(0x3FFF) &&
                            !code->runtime_bytes(0x4000) && code->runtime_note().contains("card at run time");
        });
    check(capturing.startsWith("capturing") && capturing.contains("0 bytes kept"), "zx-dma trace: Start: capturing, empty");
    printf("  (header: %s)\n", capturing.section('\n', 0, 0).toUtf8().constData());
    check(made, "zx-dma trace: the host moved bytes through the window");
    {
        char card_ram[64];
        std::snprintf(card_ram, sizeof(card_ram), "return card RAM @#%06X", base + 17);
        check(divert_banner.startsWith("ZX-DMA active: reads at #0000-#3FFF return card RAM @#") && divert_banner.contains("; next byte #") &&
                  divert_shown == 2,
              "zx-dma banner: in Divert over the main listing and dump");
        printf("  (banner: %s; the module started at #%06X, %s)\n", divert_banner.toUtf8().constData(), base,
               divert_banner.contains(card_ram) ? "the banner names the cell after the 17 bytes" : "17 bytes read");
        check(divert_marked, "zx-dma banner: #0000-#3FFF marked as card bytes at run time");
        auto * dw = dynamic_cast<DebuggerWindow *>(window);
        DisasmView * code = main_listing(window, cpu);
        // The module stopped: gone at the next stop
        wait_for([&]() { return dw != nullptr && dw->zxdma_banner_text().isEmpty() && banners_shown(window) == 0; }, 3000);
        check(dw != nullptr && dw->zxdma_banner_text().isEmpty() && banners_shown(window) == 0 && code != nullptr &&
                  !code->runtime_bytes(0x0000),
              "zx-dma banner: gone with the module stopped");
    }
    printf("  (%d rows; %s)\n", rows, QString(trace->header()->text()).replace('\n', " / ").toUtf8().constData());
    bool consecutive = rows == 17;
    for (int r = 0; consecutive && r < rows; r++) {
        char address[16];
        std::snprintf(address, sizeof(address), "#%06X", base + (unsigned int)r);
        consecutive = t->item(r, ZxDmaTraceView::ColDir)->text() == "read" &&
                      t->item(r, ZxDmaTraceView::ColAddress)->text().startsWith(address) &&
                      t->item(r, ZxDmaTraceView::ColPc)->text() == "#800A";
    }
    check(consecutive, "zx-dma trace: 17 read rows, consecutive card addresses, the LDIR's PC");
    if (rows > 9)
        check(t->item(9, ZxDmaTraceView::ColAddress)->text().endsWith("page 5") &&
                  t->item(7, ZxDmaTraceView::ColAddress)->text().endsWith("page 4"),
              "zx-dma trace: the page of the card address");
    for (int r = 0; r < rows && r < 3; r++) {
        QString line;
        for (int k = 0; k < ZxDmaTraceView::ColCount; k++) line += (k ? " | " : "") + t->item(r, k)->text();
        printf("  (row %d: %s)\n", r, line.toUtf8().constData());
    }
    dma->refresh();
    check(t->rowCount() == rows && trace->last_seq() >= seq, "zx-dma trace: a second read appends nothing (since)");
    check(trace->header()->text().startsWith("not capturing") && trace->header()->text().contains(QString("%1 bytes kept").arg(rows)),
          "zx-dma trace: Stop: the header counts the bytes kept");

    if (rows > 0) {
        // The hooks: Host PC to the main CPU's listing, the card address to the card RAM's page (after the stop
        // events of the runs above, which bring PC into sight)
        wait_for([]() { return false; }, 700);
        emit t->cellDoubleClicked(0, ZxDmaTraceView::ColPc);
        QApplication::processEvents();
        QTabWidget * cpu_tabs = tabs_with(window, QString::fromStdString(cpu->name + " :"));
        DisasmView * code = cpu_tabs ? cpu_tabs->widget(0)->findChild<DisasmView *>() : nullptr;
        check(code != nullptr && code->cursor_address() == 0x800A, "zx-dma trace: double-click Host PC: the main listing there");
        if (QTabWidget * mt = tabs_with(window, "pages")) mt->setCurrentIndex(0);
        emit t->cellDoubleClicked(9, ZxDmaTraceView::ColAddress);
        QApplication::processEvents();
        QTabWidget * memory_tabs = tabs_with(window, "pages");
        check(memory_tabs != nullptr && qobject_cast<PagedMemory *>(memory_tabs->currentWidget()) != nullptr,
              "zx-dma trace: double-click Card address: the pages tab");
    }
    check(t->rowCount() == rows, "zx-dma trace: Stop keeps the rows");
    trace->button("clear")->click();
    check(t->rowCount() == 0 && trace->header()->text().contains("0 bytes kept"), "zx-dma trace: Clear empties it");
    trace->set_expanded(false);
}

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
        // The run resumes asynchronously: a bare "paused" may still be the earlier pause. Wait for the stop the card's
        // breakpoint makes (its reason and its CPU), then read the state once
        const bool ghit = wait_for(
            [m, gs]() {
                return m->debug_paused() && !m->debug_stepping() && m->debug_stop_reason() == ecatshim::Emulator::DSTOP_BREAKPOINT &&
                       m->debug_stop_cpu() == gs;
            },
            5000);
        const unsigned int gstopPc = gs->get_pc();
        const int greason = m->debug_stop_reason();
        const bool gpaused = m->debug_paused(), gstepping = m->debug_stepping(), gcardStop = m->debug_stop_cpu() == gs;
        check(ghit && gstopPc == gbp, "gs breakpoint stops the machine, gs in front");
        if (!ghit || gstopPc != gbp)
            printf("  (gs breakpoint #%04X, %u times in %zu of history: %s, gs PC #%04X, stop cpu %s, paused %d stepping %d,"
                   " reason %d, frames %llu)\n",
                   gbp, (unsigned)gseen[gbp], ghistory.size(), ghit ? "stopped" : "not stopped in 5 s", gstopPc,
                   gcardStop ? "gs" : "other", gpaused, gstepping, greason, (unsigned long long)machine_frames(m));
        gs->remove_breakpoint(gbp);
        if (!ghit) {
            m->debug_pause();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
        }

        // A step of the card's tab is the card's step: the machine stops after one card instruction, gs in front
        const unsigned int gr = reg(gs, "R");
        m->debug_step(gs);
        const bool gstep = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
        check(gstep && m->debug_stop_cpu() == gs && m->debug_stop_reason() == ecatshim::Emulator::DSTOP_STEP &&
                  reg(gs, "R") != gr,
              "gs step (the card's own, gs in front)");
        m->debug_step_over(gs);
        const bool gover = wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 6000);
        check(gover && m->debug_stop_cpu() == gs, "gs step over");

        // A step of the main CPU's tab brings it to the front again (the window checks below type into its listing;
        // over the WebAPI the card's stop event may come late, so wait for the main CPU's stop to be the last)
        m->debug_step(cpu);
        const bool back = wait_for([m, cpu]() { return m->debug_paused() && !m->debug_stepping() && m->debug_stop_cpu() == cpu; }, 3000);
        wait_for([]() { return false; }, 600);  // the event of that stop, past one long poll
        check(back, "main step after the card's: main in front");
    }

    size_t fields = 0;
    for (unsigned int i = 0; i < m->dm->device_count; i++)
        fields += m->dm->get_device(i)->device->get_device_fields().size();
    check(fields > 20, "devices tab has fields");

    // A Next's own tabs (NextREG, MMU, Copper, Sprites, DMA, CTC / IM2): there on a Next only, each read whole
    {
        QTabWidget * side = tabs_with(window, "Devices");
        QList<NextReportView *> views;  // in the tabs' order
        for (int i = 0; side != nullptr && i < side->count(); i++)
            if (auto * v = qobject_cast<NextReportView *>(side->widget(i)); v && v->objectName().startsWith("next:")) views << v;
        check(views.size() == 6, "next tabs: six views");
        const bool next = !views.isEmpty() && views.front()->present();
        bool shown = side != nullptr;
        for (NextReportView * v : views) shown = shown && side->isTabVisible(side->indexOf(v)) == next;
        check(shown, next ? "next tabs: shown on a Next" : "next tabs: hidden on another machine");
        if (next) {
            for (NextReportView * v : views) v->refresh();
            check(views[0]->table() && views[0]->table()->rowCount() > 100, "next tabs: NextREG lists the registers");
            check(views[1]->table() && views[1]->table()->rowCount() == 8, "next tabs: MMU lists the eight slots");
            check(views[4]->fields()->topLevelItemCount() > 10, "next tabs: DMA fields");
            check(views[5]->table() && views[5]->table()->rowCount() == 4, "next tabs: four CTC channels");
        }
    }

    // A General Sound card's tabs (Log, Protocol, Firmware, Hardware, Stats): there while the machine has the card;
    // a NeoGS's DMA and SD · MP3 · Flash only on a NeoGS, whose Hardware tab is the NeoGS board; the lightweight
    // player's Interpreter and Player only on it; on a MultiSound's GS the tabs read its reports (gs-debugger P5.5)
    {
        QTabWidget * side = tabs_with(window, "Devices");
        QList<NextReportView *> views, neogs_views, lw_views;  // in the tabs' order
        for (int i = 0; side != nullptr && i < side->count(); i++)
            if (auto * v = qobject_cast<NextReportView *>(side->widget(i)); v && v->objectName().startsWith("card:"))
                (v->needs().empty() ? views : v->objectName().startsWith("card:lw-") ? lw_views : neogs_views) << v;
        check(views.size() == 5, "card tabs: five views");
        check(neogs_views.size() == 2 && neogs_views[0]->objectName() == "card:neogs-dma" &&
                  neogs_views[1]->objectName() == "card:neogs-media",
              "card tabs: a NeoGS's DMA and SD · MP3 · Flash views");
        check(lw_views.size() == 2 && lw_views[0]->objectName() == "card:lw-interpreter" &&
                  lw_views[1]->objectName() == "card:lw-player",
              "card tabs: the LW player's Interpreter and Player views");
        const bool card = !views.isEmpty() && views.front()->present();
        const bool neogs = card && !neogs_views.isEmpty() && neogs_views.front()->present();
        const bool lw = card && !lw_views.isEmpty() && lw_views.front()->present();
        bool shown = side != nullptr;
        for (NextReportView * v : views) shown = shown && side->isTabVisible(side->indexOf(v)) == card;
        check(shown, card ? "card tabs: shown with a General Sound" : "card tabs: hidden without one");
        shown = side != nullptr;
        for (NextReportView * v : neogs_views) shown = shown && side->isTabVisible(side->indexOf(v)) == neogs;
        check(shown, neogs ? "card tabs: DMA and SD · MP3 · Flash shown with a NeoGS" : "card tabs: DMA and SD · MP3 · Flash hidden without a NeoGS");
        shown = side != nullptr;
        for (NextReportView * v : lw_views) shown = shown && side->isTabVisible(side->indexOf(v)) == lw;
        check(shown, lw ? "card tabs: Interpreter and Player shown with the LW player" : "card tabs: Interpreter and Player hidden without the LW player");
        // The card CPUs: the LW player has none; the card the tabs read (gs, the MultiSound's ms)
        bool cpu_gs = false, cpu_ms = false;
        for (ecatshim::CPU * c : cpus) {
            cpu_gs = cpu_gs || c->name == "gs";
            cpu_ms = cpu_ms || c->name == "ms";
        }
        if (lw) check(!cpu_gs && !cpu_ms && cpus.size() == 1, "card tabs: the LW player has no card CPU tab");
        const bool ms = card && views.size() == 5 && views[4]->report() == "ms-gs-stats";
        if (cpu_ms && !cpu_gs) {
            check(ms && views[0]->report() == "ms-gs-commands" && views[1]->report() == "multisound" && views[1]->part() == "gs" &&
                      views[2]->report() == "ms-gs-firmware" && views[3]->report() == "multisound" && views[3]->part() == "gs",
                  "card tabs: the MultiSound's GS (ms-gs-commands, multisound.gs, ms-gs-firmware, ms-gs-stats)");
            bool captioned = true;
            for (NextReportView * v : views) captioned = captioned && v->caption()->text().contains("MultiSound");
            check(captioned, "card tabs: each says the MultiSound's GS");
            auto * dw = dynamic_cast<DebuggerWindow *>(window);
            check(dw != nullptr && dw->state_text().contains("MultiSound GS"), "card tabs: the state's card line is the MultiSound's GS");
            if (dw != nullptr) printf("  (state: %s)\n", dw->state_text().toUtf8().constData());
        } else if (card && views.size() == 5) {
            check(!ms && views[4]->report() == "gs-stats" && views[0]->caption()->text().contains("General Sound card"),
                  "card tabs: the General Sound's reports (gs-commands, gs, gs-firmware, gs-stats)");
        }
        // A field anywhere in a view's tree, by its name; one of a group's, by both
        auto field = [](NextReportView * v, const char * name) {
            const QList<QTreeWidgetItem *> found = v->fields()->findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
            return found.isEmpty() ? QString() : found.front()->text(1);
        };
        auto in_group = [](NextReportView * v, const char * group, const char * name) {
            for (QTreeWidgetItem * g : v->fields()->findItems(group, Qt::MatchExactly))
                for (int i = 0; i < g->childCount(); i++)
                    if (g->child(i)->text(0) == name) return g->child(i)->text(1);
            return QString();
        };
        if (card && views.size() == 5) {
            for (NextReportView * v : views) v->refresh();
            check(views[1]->fields()->topLevelItemCount() > 3, "card tabs: protocol and mailbox fields");
            check(views[3]->table() && views[3]->table()->rowCount() >= 4, "card tabs: hardware lists the DAC channels");
            check(views[4]->fields()->topLevelItemCount() > 0, "card tabs: stats");
            if (ms) {
                // Read with cpu ms: the MultiSound's own statistics and the firmware objects of its GS
                check(views[2]->table() && views[2]->table()->rowCount() > 0, "card tabs: the MultiSound GS's firmware variables");
                printf("  (ms: %d stats fields, %d firmware variables, %d log records)\n", views[4]->fields()->topLevelItemCount(),
                       views[2]->table() ? views[2]->table()->rowCount() : -1, views[0]->table() ? views[0]->table()->rowCount() : -1);
            }
            QString strip;
            for (QTreeWidgetItem * item : views[3]->fields()->findItems("strip", Qt::MatchExactly))
                strip = item->text(1);
            if (lw)
                check(strip.startsWith("GS lightweight player"), "card tabs: the card's line (the lightweight player)");
            else
                check(strip.contains(" Z80 ") && strip.contains(" · cycle ") && strip.contains(" · MPAG #"),
                      "card tabs: the card's line (CPU, cycle, page)");
            if (!strip.isEmpty()) printf("  (card line: %s)\n", strip.toUtf8().constData());
            const QString gscfg0 = field(views[3], "GSCFG0"), clock = field(views[3], "clock");
            if (neogs) {
                // The core's neogs board (DebugBoards), read through the shim, with the statistics' counters
                const ecatshim::Emulator::Board board = m->read_board("neogs");
                check(board.ok && board.control("sstat") != nullptr, "card tabs: the neogs board read through the shim");
                const QString sstat = field(views[3], "SSTAT"), switches = in_group(views[3], "Clock", "switches");
                check(gscfg0.startsWith("#") && clock.endsWith("MHz") && sstat.startsWith("#") &&
                          !field(views[3], "window 0 page").isEmpty() && !field(views[3], "left").isEmpty() &&
                          !switches.isEmpty() && switches != "-" && views[3]->table()->rowCount() == 8,
                      "card tabs: hardware is the NeoGS board (GSCFG0, clock and its switches, PG0-PG3, SSTAT, DAC levels, 8 channels)");
                check(views[3]->fields()->findItems("Mailbox counters", Qt::MatchExactly).size() == 1,
                      "card tabs: hardware has the mailbox counters");
                const QString mpagex = in_group(views[3], "Paging", "MPAGEX"), expag = in_group(views[3], "Paging", "extended paging");
                check(mpagex.startsWith("#") && !expag.isEmpty(), "card tabs: Paging has MPAGEX and extended paging");
                printf("  (Paging: MPAG %s, MPAGEX %s, extended paging %s)\n", in_group(views[3], "Paging", "MPAG").toUtf8().constData(),
                       mpagex.toUtf8().constData(), expag.toUtf8().constData());
                printf("  (NeoGS board: GSCFG0 %s; clock %s, %s switches; SSTAT %s; SCTRL %s)\n", gscfg0.toUtf8().constData(),
                       clock.toUtf8().constData(), switches.toUtf8().constData(), sstat.toUtf8().constData(),
                       field(views[3], "SCTRL").toUtf8().constData());
            } else {
                check(gscfg0.isEmpty(), "card tabs: hardware is the GS board (no GSCFG0)");
            }
        }
        if (lw && lw_views.size() == 2) {
            // A10: the interpreter's state (Idle on a quiet card, or its line) and the player's
            for (NextReportView * v : lw_views) v->refresh();
            NextReportView * interpreter = lw_views[0];
            NextReportView * player = lw_views[1];
            const QString state = in_group(interpreter, "Interpreter", "state"), line = in_group(interpreter, "Interpreter", "text");
            const QString diagram = in_group(interpreter, "Interpreter", "diagram");
            check(!state.isEmpty() && (state == "Idle" || !line.isEmpty()) && diagram.contains(QString::fromUtf8("● ") + state) &&
                      in_group(interpreter, "Queues", "params").endsWith("/ 16") && !in_group(interpreter, "Upload", "progress").isEmpty(),
                  "card tabs: the LW interpreter's state, diagram, queues of 16, upload");
            const QString song = in_group(player, "Song", "state");
            check(!song.isEmpty() && !in_group(player, "Song", "position").isEmpty() && !in_group(player, "Song", "BPM").isEmpty() &&
                      player->table(0) && player->table(0)->rowCount() == 4 && player->table(1) &&
                      player->table(1)->columnCount() == 10 && player->fields()->findItems("Module", Qt::MatchExactly).size() == 1,
                  "card tabs: the LW player's song, four channels, module row and samples table");
            printf("  (LW interpreter: %s, \"%s\"; %s; player %s, position %s, BPM %s, %d samples)\n", state.toUtf8().constData(),
                   line.toUtf8().constData(), diagram.toUtf8().constData(), song.toUtf8().constData(),
                   in_group(player, "Song", "position").toUtf8().constData(), in_group(player, "Song", "BPM").toUtf8().constData(),
                   player->table(1) ? player->table(1)->rowCount() : -1);
        }
        if (neogs && neogs_views.size() == 2) {
            for (NextReportView * v : neogs_views) v->refresh();
            NextReportView * dma = neogs_views[0];
            NextReportView * media = neogs_views[1];
            const QString mode = in_group(dma, "ZX-DMA", "mode");
            QTableWidget * rows = dma->table();
            int starts = -1;
            for (int k = 0; rows && k < rows->columnCount(); k++)
                if (rows->horizontalHeaderItem(k) && rows->horizontalHeaderItem(k)->text() == "starts") starts = k;
            const QString zx_starts = starts >= 0 && rows->item(0, starts) ? rows->item(0, starts)->text() : QString();
            check(!mode.isEmpty() && rows && rows->rowCount() == 3 && rows->item(0, 0) && rows->item(0, 0)->text() == "zx",
                  "card tabs: DMA shows the ZX-DMA mode and the three modules");
            check(!zx_starts.isEmpty() && zx_starts != "-", "card tabs: DMA rows carry the statistics' starts");
            const QString present = in_group(media, "SD: Medium", "present"), state = in_group(media, "SD: Protocol", "state");
            const QString flash = in_group(media, "Flash", "title"), flash_mode = in_group(media, "Flash", "mode");
            check(!present.isEmpty() && !state.isEmpty() && !in_group(media, "MP3: Decoder", "chip").isEmpty() && !flash.isEmpty() &&
                      !flash_mode.isEmpty() && !in_group(media, "MP3 counters", "underruns").isEmpty(),
                  "card tabs: SD · MP3 · Flash shows the SD (present, state), the MP3 chip and underruns, the flash title and mode");
            printf("  (ZX-DMA mode %s, ZX starts %s; SD present %s, state %s; MP3 %s; flash %s, mode %s, vendor %s)\n",
                   mode.toUtf8().constData(), zx_starts.toUtf8().constData(), present.toUtf8().constData(),
                   state.toUtf8().constData(), in_group(media, "MP3: Decoder", "chip").toUtf8().constData(),
                   flash.toUtf8().constData(), flash_mode.toUtf8().constData(),
                   in_group(media, "Flash", "vendor").toUtf8().constData());
            int buttons = 0;
            for (const char * b : {"Insert image...", "Insert folder...", "Eject", "Save flash"})
                buttons += media->findChild<QPushButton *>(QString("action:") + b) != nullptr;
            check(buttons == 4, "card tabs: SD · MP3 · Flash has Insert image / folder, Eject, Save flash");
            // The verb's road, nothing changed: an unknown action comes back refused, with the core's words
            const ecatshim::Emulator::BoardAnswer refused = m->board_action("neogs_sd", "no_such_action", std::string());
            check(!refused.ok && !refused.message.empty(), "card tabs: a board action reaches the core and its refusal comes back");
        }
        if (card && neogs_views.size() == 2) zxdma_trace_check(m, window, cpu, gs, neogs_views[0], neogs, check);
        if (card && !neogs) {
            // S4: a card without ZX-DMA has no banner, nothing marked
            auto * dw = dynamic_cast<DebuggerWindow *>(window);
            DisasmView * code = main_listing(window, cpu);
            check(m->zxdma_banner().text.empty() && dw != nullptr && dw->zxdma_banner_text().isEmpty() &&
                      banners_shown(window) == 0 && code != nullptr && !code->runtime_bytes(0x0000),
                  "zx-dma banner: none without a NeoGS");
        }
    }

    // Event breakpoints (gs-debugger P5.3): the card's side on the card CPU, the host's side on the main CPU, added
    // through the shim, listed with their CPU (in the breakpoint list too), switched off, removed
    {
        auto has_kind = [](ecatshim::CPU * c, const char * kind) {
            const std::vector<std::string> kinds = c->event_kinds();
            return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        };
        auto find_event = [](ecatshim::CPU * c, const char * kind, ecatshim::EventBreakpoint &out) {
            for (const ecatshim::EventBreakpoint &b : c->get_event_breakpoints())
                if (b.kind == kind) {
                    out = b;
                    return true;
                }
            return false;
        };
        BreakpointsView * list = window->findChild<BreakpointsView *>();
        auto listed = [list](const char * cpu_name, const char * kind) {
            if (list == nullptr) return false;
            list->relist();
            for (int i = 0; i < list->topLevelItemCount(); i++) {
                const QTreeWidgetItem * item = list->topLevelItem(i);
                if (item->text(0) == cpu_name && item->text(1).startsWith(QString(kind) + "  ")) return true;
            }
            return false;
        };
        check(has_kind(cpu, "int_accept"), "event kinds: the machine's on main");
        if (gs == nullptr) {
            check(!has_kind(cpu, "host_command"), "event kinds: no host side without a card");
        } else {
            check(has_kind(gs, "card_command") && !has_kind(gs, "host_command") && has_kind(cpu, "host_command") &&
                      !has_kind(cpu, "card_command"),
                  "event kinds: card side on gs, host side on main");
            std::string card_error, host_error, wrong_error;
            const bool card_added = gs->add_event_breakpoint("card_command", 0x0F, 0xFF, card_error);
            const bool host_added = cpu->add_event_breakpoint("host_command", 0, 0, host_error);
            check(card_added && host_added, "event bp: card_command on gs, host_command on main");
            if (!card_added || !host_added)
                printf("  (card: %s; host: %s)\n", card_error.c_str(), host_error.c_str());
            check(!cpu->add_event_breakpoint("card_command", 0, 0, wrong_error) && !wrong_error.empty(),
                  "event bp: a card kind on main refused, said");
            ecatshim::EventBreakpoint cb, hb;
            const bool card_found = find_event(gs, "card_command", cb), host_found = find_event(cpu, "host_command", hb);
            check(card_found && cb.value == 0x0F && cb.mask == 0xFF && cb.enabled && host_found && hb.mask == 0 &&
                      hb.enabled && !find_event(cpu, "card_command", cb) && !find_event(gs, "host_command", hb),
                  "event bp: each listed by its own CPU, value kept");
            find_event(gs, "card_command", cb);
            find_event(cpu, "host_command", hb);
            check(listed(gs->name.c_str(), "card_command") && listed("main", "host_command"),
                  "event bp: the breakpoint list's rows say the card CPU / main");
            ecatshim::EventBreakpoint off;
            check(gs->enable_event_breakpoint(cb.id, false) && find_event(gs, "card_command", off) && !off.enabled,
                  "event bp: switched off");
            const bool removed = gs->remove_event_breakpoint(cb.id) && cpu->remove_event_breakpoint(hb.id);
            check(removed && !find_event(gs, "card_command", off) && !find_event(cpu, "host_command", off) &&
                      !listed("gs", "card_command") && !listed("main", "host_command"),
                  "event bp: removed, gone from the list");
        }
    }

    // Run control of a card (gs-debugger P5.4): the card's run to a card cycle and to a main T through the shim and
    // the window, Step other (Alt+F11) both ways, the CPU in front by Ctrl+1 / Ctrl+2; without a card all hidden
    if (auto * cw = dynamic_cast<DebuggerWindow *>(window)) {
        auto action = [window](const char * name) { return window->findChild<QAction *>(name); };
        QAction * other = action("step_other");
        QAction * by1 = action("select_main_cpu");
        QAction * by2 = action("select_card_cpu");
        const char * const entries[] = {"run_to_card_cycle", "run_to_main_t", "run_to_card_int", "run_to_host_command"};
        int found = 0;
        for (const char * name : entries) found += action(name) != nullptr ? 1 : 0;
        check(other && by1 && by2 && other->shortcut() == QKeySequence(Qt::ALT | Qt::Key_F11) &&
                  by1->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_1) && by2->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_2),
              "run control: Step other (Alt+F11), Ctrl+1, Ctrl+2");
        QApplication::processEvents();
        if (gs == nullptr) {
            check(other && !other->isVisible() && by2 && !by2->isEnabled(), "run control: no card, no card actions");
        } else {
            check(found == 4 && other && other->isVisible(), "run control: the four card Run to entries, Step other shown");
            // Through the shim: a card cycle, a main T; each gets there, parked at the next instruction boundary
            const uint64_t cycle = 1000, t = 30000;
            const ecatshim::Emulator::CardRunAnswer c1 = m->debug_card_run_to(gs, "cycle", cycle);
            check(c1.ok && c1.hit && c1.card_cycle >= (int64_t)cycle && c1.card_cycle < (int64_t)cycle + 64,
                  "card run to: card cycle 1000 reached");
            printf("    (%s; card cycle %lld, main T %lld)\n", c1.message.c_str(), (long long)c1.card_cycle, (long long)c1.main_t);
            const ecatshim::Emulator::CardRunAnswer t1 = m->debug_card_run_to(gs, "main_t", t);
            check(t1.ok && t1.hit && t1.main_t >= (int64_t)t && t1.main_t < (int64_t)t + 64, "card run to: main T 30000 reached");
            printf("    (%s; card cycle %lld, main T %lld)\n", t1.message.c_str(), (long long)t1.card_cycle, (long long)t1.main_t);
            // Through the window: the core's message in the state label; a refusal said there too
            ecatshim::Emulator::CardRunAnswer w1;
            const bool ran = cw->card_run_to("cycle", 2000, &w1);
            check(ran && w1.ok && w1.hit && w1.card_cycle >= 2000 && !w1.message.empty() &&
                      cw->state_text().contains(QString::fromStdString(w1.message)),
                  "card run to (window): cycle 2000, the message shown");
            ecatshim::Emulator::CardRunAnswer w2;
            cw->card_run_to("cycle", 0xFFFFFFF0u, &w2);
            check(!w2.ok && !w2.message.empty() && cw->state_text().contains(QString::fromStdString(w2.message)),
                  "card run to (window): a cycle out of the frame refused, said");
            if (!w2.message.empty()) printf("    (%s)\n", w2.message.c_str());

            // Step other with the main CPU in front: the card steps one instruction, main stays in front
            by1->trigger();
            QApplication::processEvents();
            const bool main_front = cw->current_cpu() == cpu;
            const unsigned int gr = reg(gs, "R");
            other->trigger();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
            wait_for([]() { return false; }, 600);  // the step's stop event, past one long poll
            check(main_front && reg(gs, "R") != gr && cw->current_cpu() == cpu, "step other: main in front, the card stepped");
            {
                // P5.2: the card CPU's register pane has its cycle in the frame and the change since the value shown
                // before (the card run to cycle 2000, then this step); main's has none
                QTabWidget * cpu_tabs = tabs_with(window, QString::fromStdString(cpu->name + " :"));
                RegistersView * main_regs = cpu_tabs ? cpu_tabs->widget(0)->findChild<RegistersView *>() : nullptr;
                RegistersView * card_regs = cpu_tabs && cpu_tabs->count() > 1 ? cpu_tabs->widget(1)->findChild<RegistersView *>() : nullptr;
                ecatshim::CPU::CardCycle now;
                const bool clock = gs->card_cycle(now);
                const QString text = card_regs ? card_regs->cycle_text() : QString();
                check(clock && text.startsWith(QString("c%1 ").arg(now.frame_cycle)) && text.contains(QChar(0x0394)) &&
                          main_regs != nullptr && main_regs->cycle_text().isEmpty(),
                      "card registers: T is the card cycle and its delta");
                printf("    (card T: %s; frame cycle %lld, since the start %lld)\n", text.toUtf8().constData(),
                       (long long)now.frame_cycle, (long long)now.total);
                // S3: the card's stop says where the main CPU stands inside its instruction (+ N T wait under ZX-DMA)
                const QString main_at = QString::fromStdString(m->debug_stop_main_text());
                check(main_at.startsWith("main ") && cw->state_text().contains(main_at), "card stop: the main CPU's position in the state");
                printf("    (state: %s%s)\n", cw->state_text().toUtf8().constData(),
                       main_at.contains(" T wait") ? "" : "; no ZX-DMA wait in this instruction");
            }
            // Ctrl+2: the card in front; Step other steps main, the card stays in front
            by2->trigger();
            QApplication::processEvents();
            const bool card_front = cw->current_cpu() == gs;
            const unsigned int r1 = reg(cpu, "R");
            other->trigger();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 3000);
            wait_for([]() { return false; }, 600);
            check(card_front && reg(cpu, "R") != r1 && cw->current_cpu() == gs && m->debug_stop_cpu() == cpu,
                  "step other: the card in front (Ctrl+2), main stepped");
            by1->trigger();
            QApplication::processEvents();
            check(cw->current_cpu() == cpu, "Ctrl+1: the main CPU in front again");

            // The card line follows the running machine (throttled: Emulator::live_card_ms), with no pause
            m->debug_run(true);
            const QString running = QString("Running ") + QChar(0x00B7) + " ";
            QString first, second;
            wait_for([&]() {
                const QString s = cw->state_text();
                if (!s.startsWith(running) || !s.contains(" cycle ")) return false;
                if (first.isEmpty()) first = s;
                else if (s != first) second = s;
                return !second.isEmpty();
            }, 4000);
            check(!first.isEmpty() && !second.isEmpty() && m->debug_paused() == false, "card line: followed while the machine runs");
            printf("    (running: %s; then %s)\n", first.toUtf8().constData(), second.toUtf8().constData());
            m->debug_pause();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
        }
    }

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
        // The listing in sight (each processor's tab has one)
        DisasmView * code = nullptr;
        for (DisasmView * v : window->findChildren<DisasmView *>())
            if (code == nullptr && v->isVisible()) code = v;
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

    // Time travel (not in ecat): a short recording asked for here, then the toolbar's back steps and a branch
    auto * dw = dynamic_cast<DebuggerWindow *>(window);
    TtdControls * ttd = dw ? dw->ttd() : nullptr;
    // A card's breakpoint above may have left the machine inside the card's run at a frame end: the recording then
    // begins at the next frame boundary (docs/inprogress/2026-11-08-ttd-branches/debuggers.md §7)
    if (ttd != nullptr && ttd->call("start", {}, nullptr)) {
        auto record = [m](uint64_t frames) {
            const uint64_t from = machine_frames(m);
            m->debug_run(true);
            wait_for([m, from, frames]() { return machine_frames(m) >= from + frames; }, 5000);
            m->debug_pause();
            wait_for([m]() { return m->debug_paused() && !m->debug_stepping(); }, 2000);
        };
        auto frame = [ttd]() {
            QJsonObject p;
            ttd->call("position", {}, &p);
            return (uint64_t)p.value("current").toObject().value("frame").toInteger();
        };
        record(20);
        ttd->call("stop", {}, nullptr);
        ttd->poll(true);
        check(ttd->shown() && ttd->moment() == "Live, not recorded", "ttd: the group shows, live past the end");
        ttd->frame_back();
        const uint64_t past = frame();
        check(ttd->moment().startsWith("History: frame"), "ttd: frame back replays the history");
        const unsigned int r1 = reg(cpu, "R");
        ttd->step_back();
        check(reg(cpu, "R") != r1 && ttd->moment().startsWith("History: frame"), "ttd: step back (R moved)");
        ttd->frame_back();
        ttd->frame_back();
        check(frame() < past, "ttd: frame back twice goes earlier");
        check(ttd->branch_here_action()->text() == "Branch Here", "ttd: Branch Here in the past");
        ttd->branch_here();
        record(5);
        ttd->call("stop", {}, nullptr);
        ttd->poll(true);
        QJsonObject list;
        ttd->call("branches", {}, &list);
        const QJsonArray branches = list.value("branches").toArray();
        check(branches.size() == 2 && ttd->moment().contains("(2 branches)"), "ttd: Branch Here made a second branch");
        ttd->build_branch_menu();
        check(!ttd->branch_menu()->actions().isEmpty(), "ttd: the branch drop-down lists them");
        unsigned int made = 0;
        for (const QJsonValue &b : branches)
            if (b.toObject().value("id").toInteger() != 0) made = (unsigned int)b.toObject().value("id").toInteger();
        ttd->confirm = [](const QString &, const QString &) { return true; };
        check(made != 0 && ttd->delete_branch(made), "ttd: delete the branch (switches to the trunk first)");
        ttd->call("branches", {}, &list);
        check(list.value("branches").toArray().size() < 2, "ttd: one line of history again");
        // Run Back honours breakpoint conditions (set as another client sets them: eCat has no condition editor)
        check(TtdControls::run_back_condition({{0x8000, ""}}).empty() &&
                  TtdControls::run_back_condition({{0x8000, "A == 5"}, {0x9000, ""}}) ==
                      "(PC == #8000 && (A == 5)) || (PC == #9000)",
              "ttd: Run Back's condition from the breakpoints'");
        if (past >= 6) {
            ttd->call("seek", {{"frame", std::to_string(past - 6)}}, nullptr);
            ttd->poll(true);
            const unsigned int pc = cpu->get_pc();
            const std::vector<unsigned int> kept = cpu->get_breakpoints();
            cpu->clear_breakpoints();
            cpu->add_breakpoint(pc);
            std::string error;
            const auto inform = ttd->inform;
            ttd->inform = [](const QString &, const QString &, bool) {};   // the answer is read from last_result
            if (UnrealMachineSetBreakpointCondition(m, pc, "FRAME > 100000000", error)) {
                ttd->call("seek", {{"frame", std::to_string(past)}}, nullptr);
                ttd->poll(true);
                const uint64_t at = frame();
                ttd->run_back();
                check(frame() == at && ttd->last_result().contains("no breakpoint"),
                      "ttd: Run Back with a condition that never holds: nothing moved");
                UnrealMachineSetBreakpointCondition(m, pc, "FRAME >= 0", error);
                ttd->run_back();
                check(frame() < at && cpu->get_pc() == pc && ttd->last_result().contains("at the breakpoint"),
                      "ttd: Run Back with a condition that holds: at the breakpoint");
                if (!ttd->last_result().isEmpty()) printf("    (%s)\n", ttd->last_result().toUtf8().constData());
            } else {
                printf("  %-52s %s (%s)\n", "ttd: Run Back with a condition", "skipped", error.c_str());
            }
            ttd->inform = inform;
            cpu->clear_breakpoints();
            for (unsigned int a : kept) cpu->add_breakpoint(a);
        }
        if (gs != nullptr) {
            // In the history, a few frames in: the card's own positions
            ttd->call("seek", {{"frame", std::to_string(past)}}, nullptr);
            card_ttd_check(m, dw, ttd, cpu, gs, check);
        }
        ttd->call("invalidate", {}, nullptr);
        ttd->poll(true);
        check(!ttd->shown(), "ttd: without a history nothing shows");
    } else {
        printf("  %-52s %s\n", "ttd", "skipped (no time travel on this machine)");
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
    QCommandLineOption cardOpt("card", "The card in zxbus.1: neogs (default), gs, gs-lw, multisound, ...", "card", "neogs");
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
    const std::unique_ptr<guicontrol::Host> gui = start_gui_control(static_cast<DebuggerWindow *>(dbg), machine.get(), remote);

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
