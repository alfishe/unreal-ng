// unreal-debugger: the GUI thread under a slow or stalled emulator (--gui-gap-check); see latencycheck.h

#include "latencycheck.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

#include "remotemachine.h"
#include "dialogs/debugger/debuggerwindow.h"

namespace {

double thread_cpu_ms()
{
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.0;
    auto ms = [](const FILETIME &t) { return (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime) / 1e4; };
    return ms(kernel) + ms(user);
#else
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
#endif
}

} // namespace

//------------------------------------------------------------------- the proxy

// Lives on the proxy's thread: the listener, the connection pairs and the pump that forwards what is due
class ProxyWorker : public QObject
{
public:
    ProxyWorker(const QString &host, uint16_t port) : m_host(host), m_port(port) {}

    std::atomic<int> delay_ms{0};
    std::atomic<bool> stalled{false};

    uint16_t start()
    {
        m_server = new QTcpServer(this);
        m_server->listen(QHostAddress::LocalHost, 0);
        connect(m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket * client = m_server->nextPendingConnection()) open(client);
        });
        m_pump = new QTimer(this);
        m_pump->setTimerType(Qt::PreciseTimer);
        connect(m_pump, &QTimer::timeout, this, [this]() { pump(); });
        m_pump->start(2);
        m_clock.start();
        return m_server->serverPort();
    }

    void stop()
    {
        m_pump->stop();
        for (Pair * p : m_pairs) {
            p->client->disconnect(this);
            p->upstream->disconnect(this);
            p->client->abort();
            p->upstream->abort();
            delete p;
        }
        m_pairs.clear();
        m_server->close();
    }

private:
    struct Chunk {
        qint64 due;
        QByteArray data;
    };
    struct Pair {
        QTcpSocket * client;
        QTcpSocket * upstream;
        std::deque<Chunk> up, down;   // to the emulator, to the debugger
        bool client_gone = false, upstream_gone = false;
    };
    QString m_host;
    uint16_t m_port;
    QTcpServer * m_server = nullptr;
    QTimer * m_pump = nullptr;
    QElapsedTimer m_clock;
    std::list<Pair *> m_pairs;

    void open(QTcpSocket * client)
    {
        auto * p = new Pair{client, new QTcpSocket(this), {}, {}};
        client->setParent(this);
        connect(client, &QTcpSocket::readyRead, this, [this, p]() { p->up.push_back({m_clock.elapsed(), p->client->readAll()}); });
        connect(p->upstream, &QTcpSocket::readyRead, this,
                [this, p]() { p->down.push_back({m_clock.elapsed() + delay_ms.load(), p->upstream->readAll()}); });
        connect(client, &QTcpSocket::disconnected, this, [p]() { p->client_gone = true; });
        connect(p->upstream, &QTcpSocket::disconnected, this, [p]() { p->upstream_gone = true; });
        connect(p->upstream, &QTcpSocket::errorOccurred, this, [p]() { p->upstream_gone = true; });
        p->upstream->connectToHost(m_host, m_port);
        m_pairs.push_back(p);
    }

    void pump()
    {
        if (stalled.load()) return;
        const qint64 now = m_clock.elapsed();
        for (auto it = m_pairs.begin(); it != m_pairs.end();) {
            Pair * p = *it;
            if (p->upstream->state() == QAbstractSocket::ConnectedState)
                while (!p->up.empty() && p->up.front().due <= now) {
                    p->upstream->write(p->up.front().data);
                    p->up.pop_front();
                }
            while (!p->down.empty() && p->down.front().due <= now) {
                p->client->write(p->down.front().data);
                p->down.pop_front();
            }
            // A side gone: the other closes once what it was owed is through
            const bool done = (p->client_gone && p->up.empty()) || (p->upstream_gone && p->down.empty());
            if (done) {
                p->client->disconnect(this);
                p->upstream->disconnect(this);
                p->client->disconnectFromHost();
                p->upstream->disconnectFromHost();
                p->client->deleteLater();
                p->upstream->deleteLater();
                delete p;
                it = m_pairs.erase(it);
            } else {
                ++it;
            }
        }
    }
};

struct LatencyProxy::Impl {
    QThread thread;
    ProxyWorker * worker = nullptr;
    uint16_t port = 0;
};

LatencyProxy::LatencyProxy(const QString &host, uint16_t port) : m(std::make_unique<Impl>())
{
    m->worker = new ProxyWorker(host, port);
    m->worker->moveToThread(&m->thread);
    m->thread.start();
    QMetaObject::invokeMethod(m->worker, [this]() { m->port = m->worker->start(); }, Qt::BlockingQueuedConnection);
}

LatencyProxy::~LatencyProxy()
{
    QMetaObject::invokeMethod(m->worker, [this]() {
        m->worker->stop();
        delete m->worker;
    }, Qt::BlockingQueuedConnection);
    m->thread.quit();
    m->thread.wait();
}

uint16_t LatencyProxy::port() const { return m->port; }
void LatencyProxy::set_delay_ms(int ms) { m->worker->delay_ms = ms; }
void LatencyProxy::set_stalled(bool on) { m->worker->stalled = on; }

//------------------------------------------------------------------- the probe

namespace {

// A 1 ms timer on the GUI thread: the wall time between two of its ticks is how long the event loop did not turn,
// the GUI thread's CPU time over the same span what it spent itself (the rest: waiting, or other processes)
class GapProbe : public QObject
{
public:
    struct Phase {
        std::string name;
        double max_gap_ms = 0, cpu_in_max_ms = 0, max_cpu_ms = 0;
        int over_50 = 0, over_16 = 0;
        uint64_t ticks = 0;
        qint64 start_ms = 0, end_ms = 0;
        std::vector<std::string> actions;
    };
    std::vector<Phase> phases;

    GapProbe()
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_clock.start();
    }
    void begin(const std::string &name)
    {
        end();
        phases.push_back(Phase{name});
        phases.back().start_ms = m_clock.elapsed();
        m_last_ns = m_clock.nsecsElapsed();
        m_last_cpu = thread_cpu_ms();
        m_timer.start(1);
    }
    void end()
    {
        if (phases.empty() || !m_timer.isActive()) return;
        tick();
        m_timer.stop();
        phases.back().end_ms = m_clock.elapsed();
    }
    void note(const std::string &what)
    {
        if (!phases.empty()) phases.back().actions.push_back(what);
    }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last_ns = 0;
    double m_last_cpu = 0;

    void tick()
    {
        const qint64 now = m_clock.nsecsElapsed();
        const double cpu = thread_cpu_ms();
        const double gap = (now - m_last_ns) / 1e6, spent = cpu - m_last_cpu;
        Phase &p = phases.back();
        p.ticks++;
        if (gap > p.max_gap_ms) {
            p.max_gap_ms = gap;
            p.cpu_in_max_ms = spent;
        }
        p.max_cpu_ms = std::max(p.max_cpu_ms, spent);
        if (gap > 50) p.over_50++;
        if (gap > 16) p.over_16++;
        m_last_ns = now;
        m_last_cpu = cpu;
    }
};

// Processes events until the condition holds or the time is out
bool pump_until(const std::function<bool()> &done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

QAction * action_for(QWidget * window, const QKeySequence &key)
{
    for (QAction * a : window->findChildren<QAction *>())
        if (a->shortcut() == key) return a;
    return nullptr;
}

} // namespace

int GuiGapCheck(ecatshim::Emulator *, DebuggerWindow * window, LatencyProxy * proxy, const GapCheckOptions &options)
{
    int failed = 0;
    GapProbe probe;
    // Only what the window shows is looked at here: the state label and the stop it took (a read of the machine from
    // here would be a request of its own)
    auto state = [window]() { return window->state_text(); };
    auto held = [&]() {
        const QString s = state();
        return !s.startsWith("Running") && !s.startsWith("Stepping") && !s.startsWith("Waiting");
    };
    auto press = [&](const QKeySequence &key, const char * name) {
        QAction * a = action_for(window, key);
        if (a == nullptr || !a->isEnabled()) {
            probe.note(std::string(name) + " (disabled)");
            return false;
        }
        a->trigger();
        return true;
    };
    // An action whose end shows in the window: a step's stop taken, the label running or held
    auto act = [&](const QKeySequence &key, const char * name, int timeout_ms, bool must) {
        QElapsedTimer t;
        t.start();
        // A step's end: the views read at a stop after the press (the stop's own number may stay: a step of the tab
        // not in front)
        int refreshed = 0;
        QMetaObject::Connection counting =
            QObject::connect(window, &DebuggerWindow::views_refreshed, [&refreshed]() { refreshed++; });
        if (!press(key, name)) {
            QObject::disconnect(counting);
            if (must) failed++;
            return false;
        }
        bool ended = false;
        if (key == QKeySequence(Qt::Key_F9)) ended = pump_until([&]() { return state().startsWith("Running"); }, timeout_ms);
        else if (key == QKeySequence(Qt::Key_F12)) ended = pump_until([&]() { return held(); }, timeout_ms);
        else ended = pump_until([&]() { return refreshed > 0 && held(); }, timeout_ms);
        QObject::disconnect(counting);
        probe.note(std::string(name) + (ended ? "" : " (did not end)") + " " + std::to_string(t.elapsed()) + " ms");
        if (!ended && must) failed++;
        return ended;
    };
    auto commands = [&](bool must) {
        if (!held()) act(QKeySequence(Qt::Key_F12), "pause", 20000, must);
        act(QKeySequence(Qt::Key_F7), "step", 20000, must);
        act(QKeySequence(Qt::Key_F7), "step", 20000, must);
        act(QKeySequence(Qt::Key_F8), "step over", 20000, must);
        act(QKeySequence(Qt::CTRL | Qt::Key_F8), "step out", 20000, false);  // a long routine may not return in time
        if (!held()) act(QKeySequence(Qt::Key_F12), "pause", 20000, must);
        act(QKeySequence(Qt::Key_F9), "run", 20000, must);
        pump_until([]() { return false; }, 1000);
        act(QKeySequence(Qt::Key_F12), "pause", 20000, must);
    };

    proxy->set_delay_ms(0);
    probe.begin("no delay");
    commands(true);

    proxy->set_delay_ms(options.latency_ms);
    probe.begin("answers +" + std::to_string(options.latency_ms) + " ms");
    commands(true);

    // The emulator stops answering: the window goes on (a wait shown), its commands wait or are refused
    probe.begin("stalled " + std::to_string(options.stall_ms) + " ms");
    proxy->set_stalled(true);
    QElapsedTimer stall;
    stall.start();
    press(QKeySequence(Qt::Key_F7), "step");
    pump_until([&]() { return stall.elapsed() > options.stall_ms / 3; }, options.stall_ms);
    press(QKeySequence(Qt::Key_F9), "run");
    pump_until([&]() { return stall.elapsed() > options.stall_ms * 2 / 3; }, options.stall_ms);
    press(QKeySequence(Qt::Key_F12), "pause");
    pump_until([&]() { return stall.elapsed() > options.stall_ms; }, options.stall_ms);
    probe.note("state at the stall's end: " + state().toStdString());

    // Back: the window takes the machine again and its commands work
    proxy->set_stalled(false);
    proxy->set_delay_ms(0);
    probe.begin("after the stall");
    QElapsedTimer back;
    back.start();
    const bool settled = pump_until([&]() { return !state().startsWith("Waiting") && !state().startsWith("Stepping"); }, 30000);
    probe.note(std::string(settled ? "settled" : "not settled") + " in " + std::to_string(back.elapsed()) + " ms: " +
               state().toStdString());
    if (!settled) failed++;
    commands(true);
    probe.end();

    for (const GapProbe::Phase &p : probe.phases) {
        const bool over = options.limit_ms > 0 && p.max_gap_ms > options.limit_ms;
        if (over) failed++;
        printf("gui-gap %-22s max gap %8.1f ms (GUI thread CPU in it %6.1f ms), max GUI CPU between ticks %6.1f ms, "
               "gaps > 16 ms %d, > 50 ms %d, %.1f s%s\n",
               p.name.c_str(), p.max_gap_ms, p.cpu_in_max_ms, p.max_cpu_ms, p.over_16, p.over_50,
               (p.end_ms - p.start_ms) / 1000.0, over ? "  FAILED" : "");
        std::string list;
        for (const std::string &a : p.actions) list += (list.empty() ? "" : "; ") + a;
        printf("        %s\n", list.c_str());
    }
    fflush(stdout);
    return failed;
}

int AsyncRemoteCheck(const std::string &host, uint16_t port, const std::string &emulator)
{
    int failed = 0;
    auto check = [&failed](bool ok, const std::string &what) {
        printf("  %-52s %s\n", what.c_str(), ok ? "ok" : "FAILED");
        if (!ok) failed++;
    };
    LatencyProxy proxy(QString::fromStdString(host), port);
    RemoteOptions ro;
    ro.host = "127.0.0.1";
    ro.port = proxy.port();
    ro.emulator = emulator;
    ro.synchronous = false;
    std::string error;
    std::unique_ptr<ecatshim::Emulator> m = CreateRemoteMachine(ro, error);
    check(m != nullptr, "async: connects through the proxy");
    if (!m) return failed;
    ecatshim::CPU * cpu = m->dm->get_cpus().front();
    auto held = [&]() { return m->debug_paused() && !m->debug_stepping(); };
    // A call of the views' thread (no timing: a loaded host makes any wall bound flaky; the stall below shows a call
    // that would wait)
    auto call = [](const char *, const std::function<void()> &fn) { fn(); };
    // What the views read at a stop: registers, the listing's bytes, the dump's pages, breakpoints, the devices, the
    // time travel's position
    auto reads = [&]() {
        call("registers", [&]() { (void)cpu->get_register_values(); });
        call("listing bytes", [&]() {
            for (unsigned int a = 0; a < 256; a++) (void)cpu->peek_mem(a);
        });
        call("memory pages", [&]() {
            for (unsigned int i = 0; i < m->dm->device_count; i++)
                if (auto * mem = dynamic_cast<ecatshim::Memory *>(m->dm->get_device(i)->device.get()))
                    if (mem->get_size() > 0) (void)mem->get_direct(0x1234 % mem->get_size());
        });
        call("breakpoints", [&]() { (void)cpu->get_breakpoints(); });
        call("ttd position", [&]() { (void)m->ttd_call("position", {}); });
        call("link text", [&]() { (void)m->debug_link_text(); });
        call("state", [&]() { (void)held(); });
    };
    // On a failure what the machine showed (a busy host answers late, another client's lease refuses)
    auto state = [&]() {
        return std::string(" (paused ") + (m->debug_paused() ? "1" : "0") + ", stepping " +
               (m->debug_stepping() ? "1" : "0") + ", link '" + m->debug_link_text() + "')";
    };
    m->debug_attach(true);
    m->debug_pause();
    const bool paused = pump_until(held, 10000);
    check(paused, "async: pause, the machine held" + (paused ? std::string() : state()));

    proxy.set_delay_ms(300);
    const uint64_t stop = m->debug_stop_serial();
    call("step", [&]() { m->debug_step(cpu); });
    reads();
    check(m->debug_stepping(), "async: +300 ms: a step shows under way at once");
    const bool stepped = pump_until([&]() { reads(); return held() && m->debug_stop_serial() != stop; }, 5000);
    check(stepped, "async: +300 ms: the step's stop is taken");

    proxy.set_delay_ms(0);
    proxy.set_stalled(true);
    call("step (stalled)", [&]() { m->debug_step(cpu); });
    pump_until([&]() { reads(); return false; }, 1500);
    const std::string link = m->debug_link_text();
    check(m->debug_stepping() && link.find("Waiting for the emulator") != std::string::npos,
          "async: stalled: waiting shown (" + link + ")");
    proxy.set_stalled(false);
    const bool again = pump_until([&]() { reads(); return held() && m->debug_link_text().empty(); }, 15000);
    check(again, "async: after the stall the machine is held again" + (again ? std::string() : state()));
    char line[160];

    // Closing gives the last commands their time, never more than a few seconds, even stalled
    proxy.set_stalled(true);
    m->debug_step(cpu);
    QElapsedTimer closing;
    closing.start();
    m.reset();
    proxy.set_stalled(false);
    snprintf(line, sizeof(line), "async: closes stalled in bounded time (%lld ms)", (long long)closing.elapsed());
    check(closing.elapsed() < 4000, line);
    return failed;
}
