// unreal-debugger: eCat3's debugger API (ecatshim.h) over a running emulator's WebAPI: unreal-qt, or the headless one.
//
// The views read a lot and often: every row of the listing, every byte of a dump, every register, five times a
// second while the machine runs. One request per read would be thousands per tick, so the machine is read as one
// coherent snapshot per tick (GET /debug/snapshot: registers, the CPU's 64 KB, its page map, the stop) and the views
// read that copy. Dumps of RAM, ROM and device memories read whole 16 KB pages / 4 KB chunks, once per snapshot. A
// sound card's Z80 (?cpu=gs / ms) reads its registers, windows and 64 KB once per snapshot too, when looked at.
// A snapshot is taken again when the copy is older than a tick while the machine runs, after any change this
// debugger makes, and when the emulator says something happened (a stop, a run start, an edit by another client).
//
// The GUI thread never waits for the network (the owner's rule: the window never freezes, whatever the network or
// the emulator does). Every request goes through Net: one worker thread, one keep-alive connection, the requests in
// the order they were asked, the commands before the reads. A read answers at once from what the worker fetched last
// (a GET's answer by its path) and asks the worker again when that is stale: after a new snapshot, after a command of
// this debugger, a second old; a read never answered yet says so (status 0) and the views show a wait. An answer that
// changes what was kept tells the window (the data listener), which reads its views again. A command (a step, a run,
// an edit, a breakpoint) is queued and answers at once; its result reaches the window through the snapshot it leads to
// or, for the commands whose answer matters (a card's run to, a time travel move, a GS send), through a callback on
// the window's thread. While the worker has waited on one request for more than a moment, the state label says so
// (Waiting for the emulator) and the window's commands are off; a lost emulator is the waiter's to find (below).
// Synchronous mode (the self-check: it reads back what it just wrote) waits for each request instead.
//
// That "something happened" is a long poll (GET /debug/wait) on a thread of its own with its own connection; it
// calls the window's listener, as the core's events do in the in-process adapter. Every request says it is a
// debugger's (X-Unreal-Source: debugger): the user's own hands like the GUI, no run-control session - other debuggers
// and the GUI go on, every client sees the changes; while an automation session holds the machine its changes are
// refused, as the GUI is locked.

#include "remotemachine.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include "unrealmachine.h"
#include "zxwindows.h"

namespace {

using ecatshim::CpuRegister;
using ecatshim::DeviceFieldInfo;
using ecatshim::DeviceFieldValue;

const char * kRunToGroup = "ecat-run-to";
const char * kSnapshotRoute = "/debug/snapshot?stack=0&memory=cpu:0:65536";
const unsigned int kPage = 0x4000;
const unsigned int kChunk = 0x1000;

//------------------------------------------------------------------- HTTP

// HTTP/1.1 over one keep-alive socket, blocking: Net's worker threads and the waiter each have their own. Not
// thread-safe: one per thread
class Http
{
public:
    Http(const QString &host, uint16_t port) : m_host(host), m_port(port) {}

    // While the gate is down every request answers 0 at once (the machine is offline: nothing to ask)
    void set_gate(const std::atomic<bool> * open) { m_gate = open; }
    // Once set, a request waiting for an answer gives up within a tenth of a second (the debugger closes)
    void set_abort(const std::atomic<bool> * abort) { m_abort = abort; }
    uint64_t requests() const { return m_requests; }
    uint64_t bytes() const { return m_bytes; }

    // The status (0: no connection or no answer in time) and the body
    int request(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms = 5000)
    {
        if (m_gate && !m_gate->load()) return 0;
        // A kept connection the server has closed fails at once: then once more on a new one
        for (int attempt = 0; attempt < 2 && !aborted(); attempt++) {
            const int status = once(method, path, body, out, timeout_ms);
            if (status != 0) return status;
            m_socket.reset();
        }
        return 0;
    }

    QJsonObject json(const char * method, const QString &path, const QJsonObject &body = QJsonObject(), int *status = nullptr,
                     int timeout_ms = 5000)
    {
        QByteArray out;
        const QByteArray in = (body.isEmpty() && (strcmp(method, "GET") == 0 || strcmp(method, "DELETE") == 0))
                                  ? QByteArray()
                                  : QJsonDocument(body).toJson(QJsonDocument::Compact);
        const int s = request(method, path, in, out, timeout_ms);
        if (status) *status = s;
        return QJsonDocument::fromJson(out).object();
    }

private:
    QString m_host;
    uint16_t m_port;
    const std::atomic<bool> * m_gate = nullptr;
    const std::atomic<bool> * m_abort = nullptr;
    std::unique_ptr<QTcpSocket> m_socket;
    QByteArray m_buffer;
    uint64_t m_requests = 0;
    uint64_t m_bytes = 0;

    bool aborted() const { return m_abort != nullptr && m_abort->load(); }
    // Waits for the socket in slices of a tenth of a second, the abort looked at between them
    bool wait_slices(bool connect, int ms)
    {
        QElapsedTimer t;
        t.start();
        for (;;) {
            if (aborted()) return false;
            const int left = ms - (int)t.elapsed();
            if (left <= 0) return false;
            const int slice = std::min(left, 100);
            if (connect ? m_socket->waitForConnected(slice) : m_socket->waitForReadyRead(slice)) return true;
            if (m_socket->state() == QAbstractSocket::UnconnectedState) return false;
        }
    }

    int once(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms)
    {
        QElapsedTimer clock;
        clock.start();
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
            m_socket = std::make_unique<QTcpSocket>();
            m_buffer.clear();
            m_socket->connectToHost(m_host, m_port);
            if (!wait_slices(true, std::min(timeout_ms, 2000))) return 0;
        }
        QByteArray head = QByteArray(method) + ' ' + path.toUtf8() + " HTTP/1.1\r\nHost: " + m_host.toUtf8() + ':' +
                          QByteArray::number(m_port) + "\r\nConnection: keep-alive\r\n";
        if (!body.isEmpty()) head += "Content-Type: application/json\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += "X-Unreal-Source: debugger\r\n";
        head += "\r\n";
        m_socket->write(head + body);
        m_requests++;

        auto more = [&]() {
            const int left = timeout_ms - (int)clock.elapsed();
            if (left <= 0 || !wait_slices(false, left)) return false;
            m_buffer += m_socket->readAll();
            return true;
        };
        int end;
        while ((end = m_buffer.indexOf("\r\n\r\n")) < 0)
            if (!more()) return 0;
        const QList<QByteArray> lines = m_buffer.left(end).split('\n');
        m_buffer.remove(0, end + 4);
        const QList<QByteArray> status_line = lines.value(0).trimmed().split(' ');
        const int status = status_line.value(1).toInt();
        qint64 length = -1;
        bool chunked = false, close = false;
        for (int i = 1; i < lines.size(); i++) {
            const QByteArray l = lines[i].trimmed();
            const int colon = l.indexOf(':');
            if (colon < 0) continue;
            const QByteArray name = l.left(colon).trimmed().toLower(), value = l.mid(colon + 1).trimmed().toLower();
            if (name == "content-length") length = value.toLongLong();
            else if (name == "transfer-encoding" && value.contains("chunked")) chunked = true;
            else if (name == "connection" && value == "close") close = true;
        }
        out.clear();
        if (chunked) {
            for (;;) {
                int eol;
                while ((eol = m_buffer.indexOf("\r\n")) < 0)
                    if (!more()) return 0;
                bool ok = false;
                const qint64 size = m_buffer.left(eol).trimmed().toLongLong(&ok, 16);
                if (!ok) return 0;
                while (m_buffer.size() < eol + 2 + size + 2)
                    if (!more()) return 0;
                out += m_buffer.mid(eol + 2, size);
                m_buffer.remove(0, eol + 2 + size + 2);
                if (size == 0) break;
            }
        } else if (length >= 0) {
            while (m_buffer.size() < length)
                if (!more()) return 0;
            out = m_buffer.left(length);
            m_buffer.remove(0, length);
        } else {
            while (more()) {}
            out = m_buffer;
            m_buffer.clear();
            close = true;
        }
        m_bytes += out.size();
        if (close) m_socket.reset();
        return status;
    }
};

//------------------------------------------------------------------- the worker

// A read never answered yet: the views take it as a failure and show its message
const int kPending = -1;
// A command queued, its answer to come (asynchronous mode)
const int kQueued = 202;
const char * kPendingBody = R"({"error":"pending","message":"Waiting for the emulator"})";

// Every request of the views' thread, off it: a few worker threads (lanes), each with its own keep-alive connection.
// Lane 0 takes the commands (writes and tasks, in the order they came), then the snapshot, then reads; the other
// lanes take reads only, so that a slow command (a step over runs the machine) holds no read back.
//
// A read (a GET, or a POST that only reads) answers at once from what was fetched last for the same request, and is
// fetched again when that is stale: fetched for an older read generation (a new snapshot), sent before a command the
// views' thread queued since had ended, older than its age limit, or invalidated. Two asks of the same stale request
// make one fetch. An answer that changes what was kept, and a command's end, call the arrival handler on the views'
// thread (one call for all that came since the last). Synchronous mode waits for every request instead (the
// self-check reads back what it has just written)
class Net
{
public:
    using Done = std::function<void(int status, const QByteArray &body)>;
    using Work = std::function<void(Http &)>;

    Net(const QString &host, uint16_t port, int lanes = 3) : m_host(host), m_port(port)
    {
        m_context = new QObject();
        if (QCoreApplication::instance() != nullptr) m_context->moveToThread(QCoreApplication::instance()->thread());
        m_clock.start();
        m_lanes.resize((size_t)std::max(1, lanes));
        for (size_t i = 0; i < m_lanes.size(); i++) {
            m_lanes[i].thread = QThread::create([this, i]() { lane(i); });
            m_lanes[i].thread->start();
        }
    }
    ~Net() { close(0); }

    void set_gate(const std::atomic<bool> * open) { m_gate = open; }
    void set_sync(bool on) { m_sync = on; }
    bool sync() const { return m_sync; }
    // The views' thread: what came (answers that changed something, commands' ends) / a command that failed
    void set_on_arrival(const std::function<void()> &fn) { m_on_arrival = fn; }
    void set_on_failure(const std::function<void(const QString &)> &fn) { m_on_failure = fn; }

    // The read generation: an answer fetched for an older one is stale
    void set_generation(uint64_t g)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_generation = g;
    }
    // The next ask of this read fetches it again, whatever it was
    void invalidate(const QString &path, const char * method = "GET", const QByteArray &body = QByteArray())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries[key(method, path, body)].invalid_epoch = ++m_epoch;
    }

    // A read: the status (kPending: none yet) and the body of the last answer. max_age_ms < 0: no age limit
    int get(const QString &path, QByteArray &out, int max_age_ms = 1000, const char * method = "GET",
            const QByteArray &body = QByteArray(), uint64_t * version = nullptr, int timeout_ms = 5000)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const QString k = key(method, path, body);
        Entry &e = m_entries[k];
        if (e.method.isEmpty()) {
            e.method = method;
            e.path = path;
            e.body = body;
            e.snapshot = path.endsWith(kSnapshotRoute);
        }
        e.used = m_clock.elapsed();
        const bool fetch = !current(e, max_age_ms);
        if (fetch && !e.queued && !flight_current(e)) {
            e.timeout = timeout_ms;
            e.queued = true;
            m_jobs.push_back(Job{Job::Read, k, e.snapshot ? 1 : 2, nullptr});
            prune();
            m_cv.notify_all();
        }
        if (fetch && m_sync) {
            // Wait for the fetch (one queued, or the one in flight)
            m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms * 2 + 1000), [&]() { return !e.queued && !e.in_flight; });
        }
        if (version) *version = e.version;
        if (!e.has) {
            out = kPendingBody;
            return kPending;
        }
        out = e.data;
        return e.status;
    }
    // Whether the last get of this read was answered for the state as it is now (no refetch to come)
    bool fresh(const QString &path, int max_age_ms = 1000, const char * method = "GET", const QByteArray &body = QByteArray())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key(method, path, body));
        return it != m_entries.end() && current(it->second, max_age_ms);
    }

    // A command: queued (kQueued at once; done on the views' thread when it ends), or in synchronous mode its answer
    // (done called before the return)
    int send(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms, Done done)
    {
        auto job = std::make_shared<Command>();
        job->method = method;
        job->path = path;
        job->body = body;
        job->timeout = timeout_ms;
        job->done = std::move(done);
        queue(job);
        if (!m_sync) return kQueued;
        wait(job);
        out = job->out;
        if (job->done) job->done(job->status, job->out);
        return job->status;
    }
    // Requests of their own on lane 0 in the commands' order (a remove and an add under one lock, a read of a seq
    // just before a write): done on the views' thread after it
    void task(Work work, std::function<void()> done = {})
    {
        auto job = std::make_shared<Command>();
        job->work = std::move(work);
        job->task_done = std::move(done);
        queue(job);
        if (!m_sync) return;
        wait(job);
        if (job->task_done) job->task_done();
    }

    // The blocking API of Http over the lanes: a GET a read (age limit 1 s), anything else a command (its failure
    // to the failure handler)
    int request(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms = 5000)
    {
        if (strcmp(method, "GET") == 0) return get(path, out, 1000, "GET", QByteArray(), nullptr, timeout_ms);
        return send(method, path, body, out, timeout_ms, Done());
    }
    QJsonObject json(const char * method, const QString &path, const QJsonObject &body = QJsonObject(), int *status = nullptr,
                     int timeout_ms = 5000)
    {
        QByteArray out;
        const QByteArray in = (body.isEmpty() && (strcmp(method, "GET") == 0 || strcmp(method, "DELETE") == 0))
                                  ? QByteArray()
                                  : QJsonDocument(body).toJson(QJsonDocument::Compact);
        const int s = request(method, path, in, out, timeout_ms);
        if (status) *status = s;
        return QJsonDocument::fromJson(out).object();
    }

    // Commands queued or running (counted from the views' thread's side: one queued is one until its done ran)
    uint64_t commands_queued() const { return m_commands_queued.load(); }
    uint64_t commands_done() const { return m_commands_done.load(); }
    // How long the oldest request in flight has waited for its answer: reads, commands (0: none)
    void waiting(int &read_ms, int &command_ms, QString &command) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        read_ms = command_ms = 0;
        const qint64 now = m_clock.elapsed();
        for (const Lane &l : m_lanes) {
            if (l.since < 0) continue;
            const int ms = (int)(now - l.since);
            if (l.command) {
                if (ms > command_ms) command = l.what;
                command_ms = std::max(command_ms, ms);
            } else {
                read_ms = std::max(read_ms, ms);
            }
        }
    }
    uint64_t requests() const { return m_requests.load(); }
    uint64_t bytes() const { return m_bytes.load(); }

    // The commands queued get up to drain_ms to go; then the requests in flight are given up and the lanes end
    void close(int drain_ms)
    {
        if (m_closed.exchange(true)) return;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // The reads are for views that are going
            std::deque<Job> commands;
            for (const Job &j : m_jobs)
                if (j.kind == Job::Cmd) commands.push_back(j);
            m_jobs.swap(commands);
            if (drain_ms > 0)
                m_cv.wait_for(lock, std::chrono::milliseconds(drain_ms), [&]() {
                    if (!m_jobs.empty()) return false;
                    for (const Lane &l : m_lanes)
                        if (l.since >= 0 && l.command) return false;
                    return true;
                });
            m_stop = true;
            m_abort = true;
            m_cv.notify_all();
        }
        for (Lane &l : m_lanes) {
            l.thread->wait();
            delete l.thread;
        }
        m_lanes.clear();
        delete m_context;
        m_context = nullptr;
    }

private:
    struct Entry {
        QByteArray method;
        QString path;
        QByteArray body;
        int timeout = 5000;
        bool has = false;
        int status = 0;
        QByteArray data;
        uint64_t version = 0;   // one more at every answer that changed it
        uint64_t generation = 0, writes = 0, epoch = 0;   // as it was when the kept answer was sent
        qint64 at = 0;          // when it came (m_clock)
        uint64_t invalid_epoch = 0;
        bool queued = false, in_flight = false, snapshot = false;
        uint64_t flight_generation = 0, flight_writes = 0, flight_epoch = 0;
        qint64 used = 0;
    };
    struct Command {
        QByteArray method;
        QString path;
        QByteArray body;
        int timeout = 5000;
        Done done;
        Work work;
        std::function<void()> task_done;
        int status = 0;
        QByteArray out;
        bool finished = false;
    };
    struct Job {
        enum Kind { Read, Cmd } kind;
        QString key;
        int priority = 2;   // 0 a command, 1 the snapshot, 2 a read
        std::shared_ptr<Command> command;
    };
    struct Lane {
        QThread * thread = nullptr;
        qint64 since = -1;  // the request in flight's start (m_clock), -1: none
        bool command = false;
        QString what;
    };

    QString m_host;
    uint16_t m_port;
    const std::atomic<bool> * m_gate = nullptr;
    bool m_sync = false;
    // UNREAL_DEBUGGER_NET_TRACE=1: every request's end on stderr (lane, ms since the start, what, status, time taken)
    const bool m_trace = qEnvironmentVariableIntValue("UNREAL_DEBUGGER_NET_TRACE") != 0;
    QObject * m_context = nullptr;
    std::function<void()> m_on_arrival;
    std::function<void(const QString &)> m_on_failure;
    QElapsedTimer m_clock;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<QString, Entry> m_entries;
    std::deque<Job> m_jobs;
    std::vector<Lane> m_lanes;
    uint64_t m_generation = 0;
    uint64_t m_epoch = 0;
    uint64_t m_writes_queued = 0;   // commands queued from the views' thread
    uint64_t m_writes_ended = 0;    // and ended
    unsigned int m_prune = 0;
    bool m_stop = false;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_closed{false};
    std::atomic<uint64_t> m_commands_queued{0}, m_commands_done{0};
    std::atomic<uint64_t> m_requests{0}, m_bytes{0};

    // What came for the views' thread: commands ended, an answer that changed something
    std::vector<std::shared_ptr<Command>> m_ended;
    bool m_changed = false;
    std::atomic<bool> m_posted{false};

    static QString key(const char * method, const QString &path, const QByteArray &body)
    {
        return QString::fromLatin1(method) + ' ' + path + (body.isEmpty() ? QString() : '\n' + QString::fromUtf8(body));
    }
    // The snapshot is what makes a generation: never stale by one
    bool current(const Entry &e, int max_age_ms) const
    {
        return e.has && (e.snapshot || e.generation == m_generation) && e.writes >= m_writes_queued &&
               e.epoch >= e.invalid_epoch && (max_age_ms < 0 || m_clock.elapsed() - e.at <= max_age_ms);
    }
    bool flight_current(const Entry &e) const
    {
        return e.in_flight && (e.snapshot || e.flight_generation == m_generation) && e.flight_writes >= m_writes_queued &&
               e.flight_epoch >= e.invalid_epoch;
    }
    // The reads nobody asked for in a minute go (a cursor's old positions)
    void prune()
    {
        if (++m_prune % 256 != 0) return;
        const qint64 now = m_clock.elapsed();
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (!it->second.queued && !it->second.in_flight && now - it->second.used > 60000) it = m_entries.erase(it);
            else ++it;
        }
    }
    void queue(const std::shared_ptr<Command> &job)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writes_queued++;
        m_commands_queued++;
        m_jobs.push_back(Job{Job::Cmd, QString(), 0, job});
        m_cv.notify_all();
    }
    void wait(const std::shared_ptr<Command> &job)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&]() { return job->finished || m_stop; });
    }

    // The next job for a lane: lane 0 the commands first, then the snapshot, then reads; the others reads only, the
    // snapshot not while a command is queued or runs (it would show the machine before the command)
    bool take(size_t lane, Job &out)
    {
        bool command_busy = false;
        for (const Lane &l : m_lanes) command_busy = command_busy || (l.since >= 0 && l.command);
        int best = -1;
        for (size_t i = 0; i < m_jobs.size(); i++) {
            const Job &j = m_jobs[i];
            if (j.kind == Job::Cmd) command_busy = true;
            if (lane != 0 && j.kind == Job::Cmd) continue;
            if (j.priority == 1 && (command_busy || (lane != 0 && m_jobs[0].kind == Job::Cmd))) continue;
            if (best < 0 || j.priority < m_jobs[(size_t)best].priority) best = (int)i;
            if (j.priority == 0) break;
        }
        if (best < 0) return false;
        out = m_jobs[(size_t)best];
        m_jobs.erase(m_jobs.begin() + best);
        return true;
    }

    void lane(size_t index)
    {
        Http http(m_host, m_port);
        http.set_gate(m_gate);
        http.set_abort(&m_abort);
        uint64_t requests = 0, bytes = 0;
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_stop) {
            Job job;
            if (!take(index, job)) {
                m_cv.wait(lock);
                continue;
            }
            Lane &me = m_lanes[index];
            me.since = m_clock.elapsed();
            me.command = job.kind == Job::Cmd;
            if (job.kind == Job::Cmd) {
                std::shared_ptr<Command> c = job.command;
                me.what = c->work ? QString() : c->path.mid(c->path.lastIndexOf('/') + 1).section('?', 0, 0);
                lock.unlock();
                if (c->work) c->work(http);
                else c->status = http.request(c->method.constData(), c->path, c->body, c->out, c->timeout);
                if (m_trace)
                    fprintf(stderr, "net %zu %6lld cmd  %s %s -> %d (%lld ms)\n", index, (long long)m_clock.elapsed(),
                            c->work ? "task" : c->method.constData(), qPrintable(c->path), c->status,
                            (long long)(m_clock.elapsed() - me.since));
                lock.lock();
                c->finished = true;
                m_writes_ended++;
                m_ended.push_back(c);
            } else {
                auto it = m_entries.find(job.key);
                if (it == m_entries.end()) {
                    me.since = -1;
                    continue;
                }
                Entry &e = it->second;
                e.queued = false;
                e.in_flight = true;
                e.flight_generation = m_generation;
                e.flight_writes = m_writes_ended;
                e.flight_epoch = m_epoch;
                const QByteArray method = e.method, body = e.body;
                const QString path = e.path;
                const int timeout = e.timeout;
                lock.unlock();
                QByteArray out;
                const int status = http.request(method.constData(), path, body, out, timeout);
                if (m_trace)
                    fprintf(stderr, "net %zu %6lld read %s %s -> %d (%lld ms)\n", index, (long long)m_clock.elapsed(),
                            method.constData(), qPrintable(path), status, (long long)(m_clock.elapsed() - me.since));
                lock.lock();
                // Gone meanwhile (pruned): the answer has nobody
                it = m_entries.find(job.key);
                if (it != m_entries.end()) {
                    Entry &f = it->second;
                    f.in_flight = false;
                    // No answer keeps the last one there was (the views keep what they show)
                    if (status != 0 || !f.has) {
                        if (!f.has || f.status != status || f.data != out) {
                            f.version++;
                            m_changed = true;
                        }
                        f.has = true;
                        f.status = status;
                        f.data = out;
                    }
                    // The same bytes still tell something: a snapshot now taken after a command (the window stops
                    // showing it under way), a read that was shown waiting (the wait is over)
                    if ((f.snapshot && (f.flight_writes != f.writes || f.flight_epoch != f.epoch)) ||
                        m_clock.elapsed() - me.since > 1000)
                        m_changed = true;
                    f.generation = f.flight_generation;
                    f.writes = f.flight_writes;
                    f.epoch = f.flight_epoch;
                    f.at = m_clock.elapsed();
                }
            }
            me.since = -1;
            m_requests += http.requests() - requests;
            m_bytes += http.bytes() - bytes;
            requests = http.requests();
            bytes = http.bytes();
            m_cv.notify_all();
            if ((m_changed || !m_ended.empty()) && !m_posted.exchange(true) && m_context != nullptr)
                QMetaObject::invokeMethod(m_context, [this]() { deliver(); }, Qt::QueuedConnection);
        }
    }

    // The views' thread: the commands' dones, then the arrival handler
    void deliver()
    {
        std::vector<std::shared_ptr<Command>> ended;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_posted = false;
            ended.swap(m_ended);
            changed = m_changed;
            m_changed = false;
        }
        for (const auto &c : ended) {
            m_commands_done++;
            if (m_sync) continue;  // the waiting call ran it
            if (c->work) {
                if (c->task_done) c->task_done();
                continue;
            }
            if (c->done) c->done(c->status, c->out);
            else if ((c->status < 200 || c->status >= 300) && m_on_failure) {
                const QString message = QJsonDocument::fromJson(c->out).object().value("message").toString();
                m_on_failure(c->status == 0 ? QString("no answer to %1 %2").arg(QString::fromLatin1(c->method), c->path)
                                            : QString("%1 %2: %3").arg(QString::fromLatin1(c->method), c->path,
                                                                        message.isEmpty() ? QString("HTTP %1").arg(c->status) : message));
            }
        }
        if ((changed || !ended.empty()) && m_on_arrival) m_on_arrival();
    }
};

QString hex_body(unsigned int v)
{
    return QString::asprintf("%02X", v & 0xFF);
}

unsigned int num(const QJsonObject &o, const char * a, const char * b)
{
    return (unsigned int)o.value(a).toObject().value(b).toInt();
}

//------------------------------------------------------------------- the snapshot

// One coherent picture of the machine and what was read for it since (pages, chunks, breakpoints)
struct Snapshot {
    bool valid = false;
    uint64_t generation = 0;
    uint64_t seq = 0;
    uint64_t frame = 0;
    QString state;          // running | paused | stopped
    QString reason;         // none | pause | breakpoint | step
    QString stop_cpu;       // the CPU of the stop: main, gs, ms
    uint64_t stop_serial = 0; // the stop's number (pause.serial): one more at every stop
    QString stop_main;      // a card's stop: where the main CPU stood (pause.main.text)
    QJsonObject regs;
    QByteArray memory;      // the CPU's 64 KB
    struct Bank { QString kind; unsigned int page = 0; bool writable = false; } banks[4];
};

struct Breakpoint {
    int id = 0;
    unsigned int address = 0;
    bool run_to = false;
    std::string condition;  // canonical (Unreal) form; empty: none
};

class RemoteMachine;

//------------------------------------------------------------------- memories

// RAM or ROM pages (GET /memory/page/{kind}/{n}), a 16 KB page per read
class RemotePages : public ecatshim::Memory
{
public:
    RemotePages(RemoteMachine * m, const char * kind, unsigned int pages) : m_m(m), m_kind(kind), m_pages(pages)
    {
        name = kind;
        type = kind;
    }
    unsigned int get_size() override { return m_pages * kPage; }
    unsigned int page_size() override { return kPage; }
    std::string page_title(unsigned int page) override;
    std::string page_role(unsigned int page) override;
    void set_paging(const ZxPaging &p) { m_paging = p; }
    unsigned int get_direct(unsigned int a) override;
    void set_value(unsigned int a, unsigned int v, bool) override;

private:
    RemoteMachine * m_m;
    ZxPaging m_paging;
    QString m_kind;
    unsigned int m_pages;
    std::map<unsigned int, std::pair<uint64_t, QByteArray>> m_cache;
    // The ROM pages' titles and roles (GET /state/memory/rom), asked again after an event of the machine (a stop, a
    // run, an edit): a flash write or an edit gives a page another name
    std::vector<std::pair<std::string, std::string>> m_titles;
    uint64_t m_titles_seq = ~0ull;
    void rom_titles();
};

// A device memory (GET /memory/region/{name}), 4 KB per read; as a Memory a dump tab, else in the "+" menu
template <class Base>
class RemoteRegion : public Base
{
public:
    RemoteRegion(RemoteMachine * m, const QString &region, unsigned int size, bool writable, const std::string &kind)
        : m_m(m), m_region(region), m_size(size), m_writable(writable)
    {
        this->name = region.toStdString();
        this->type = kind;
    }
    unsigned int get_size() override { return m_size; }
    unsigned int get_direct(unsigned int a) override;
    void set_value(unsigned int a, unsigned int v, bool) override;
    bool debug_writable(unsigned int) override { return m_writable; }

private:
    RemoteMachine * m_m;
    QString m_region;
    unsigned int m_size;
    bool m_writable;
    std::map<unsigned int, std::pair<uint64_t, QByteArray>> m_cache;
};

// A sound card's RAM or ROM (flash on the NeoGS): its Z80's pages ("gs.ram", "neogs.flash", "multisound.gs.rom")
class RemoteCardPages : public RemoteRegion<ecatshim::Memory>
{
public:
    using RemoteRegion<ecatshim::Memory>::RemoteRegion;
    unsigned int page_size() override { return kPage; }
};

// The CPU's 64 KB as its page map, from the snapshot
class RemoteMapper : public ecatshim::MemoryMapper
{
public:
    RemoteMapper(RemoteMachine * m, ecatshim::AddressableDevice * ram, ecatshim::AddressableDevice * rom)
        : m_m(m), m_ram(ram), m_rom(rom)
    {
        name = "mapper";
        type = "z80-pages";
    }
    unsigned int get_size() override { return 0x10000; }
    unsigned int get_direct(unsigned int a) override;
    void set_value(unsigned int a, unsigned int v, bool) override;
    ecatshim::AddressableDevice * peek_owner(unsigned int address, unsigned int &offset) override;

private:
    RemoteMachine * m_m;
    ecatshim::AddressableDevice * m_ram;
    ecatshim::AddressableDevice * m_rom;
};

// A report's part as the Devices tab shows it (unrealmachine.cpp's to_tree does the same with a StateNode): an
// object's members under it, named by its first naming field; a short list of scalars on one line; a longer list item
// by item
std::string scalar_text(const QJsonValue &v)
{
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) {
        const double d = v.toDouble();
        return d == (double)(long long)d ? std::to_string((long long)d) : QString::number(d, 'g', 6).toStdString();
    }
    if (v.isString()) return v.toString().toStdString();
    return "null";
}

void to_tree(const QJsonValue &v, ecatshim::DeviceNode &out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (const char * key : {"name", "id", "slot", "card", "device", "kind", "model"})
            if (o.value(key).isString() && !o.value(key).toString().isEmpty()) {
                out.value = o.value(key).toString().toStdString();
                break;
            }
        for (auto it = o.begin(); it != o.end(); ++it) {
            ecatshim::DeviceNode c;
            c.name = it.key().toStdString();
            to_tree(it.value(), c);
            out.children.push_back(std::move(c));
        }
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        bool scalars = a.size() <= 32;
        for (const QJsonValue &i : a) scalars = scalars && !i.isObject() && !i.isArray();
        if (scalars) {
            for (int i = 0; i < a.size(); i++) out.value += (i ? " " : "") + scalar_text(a[i]);
            if (a.isEmpty()) out.value = "none";
            return;
        }
        out.value = std::to_string(a.size()) + (a.size() == 1 ? " item" : " items");
        const int shown = std::min<int>((int)a.size(), 256);
        for (int i = 0; i < shown; i++) {
            ecatshim::DeviceNode c;
            c.name = "[" + std::to_string(i) + "]";
            to_tree(a[i], c);
            out.children.push_back(std::move(c));
        }
        if (shown < a.size())
            out.children.push_back(ecatshim::DeviceNode{"", "+ " + std::to_string(a.size() - shown) + " more items", {}});
    } else {
        out.value = scalar_text(v);
    }
}

// A history record as one line: "frame 12  pc 32785  kind ram", a nested part in braces, a list in brackets
std::string line_text(const QJsonValue &v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        std::string line;
        for (auto it = o.begin(); it != o.end(); ++it) {
            const std::string part = it.value().isObject() ? "{" + line_text(it.value()) + "}" : line_text(it.value());
            line += (line.empty() ? "" : "  ") + it.key().toStdString() + " " + part;
        }
        return line;
    }
    if (v.isArray()) {
        std::string line;
        for (const QJsonValue &i : v.toArray()) line += (line.empty() ? "" : " ") + line_text(i);
        return "[" + line + "]";
    }
    return scalar_text(v);
}

// One device's history (GET /history/{device}) for the History tab: its counts, the newest records, recording on
// or off (POST /history/{device}/config)
class RemoteHistory : public ecatshim::History
{
public:
    RemoteHistory(RemoteMachine * m, const std::string &device, const std::string &what)
        : m_m(m), m_device(device), m_what(what)
    {
        name = device + " history";
        type = "history";
    }
    std::string device() const override { return m_device; }
    Summary summary() override;
    std::vector<std::string> records(unsigned int count) override;
    void set_recording(bool on) override;

private:
    RemoteMachine * m_m;
    std::string m_device;
    std::string m_what;
    std::vector<std::string> m_last;
};

// A WebAPI state report of the device catalog (GET /state/devices): the Devices tab's entry (title, the catalog's
// summary, the report as a tree) and, for scripts, device fields (the leaves of its JSON, dotted paths as names)
class RemoteReport : public ecatshim::ComputerDevice
{
public:
    RemoteReport(RemoteMachine * m, const std::string &id, const std::string &title, const QString &route)
        : m_m(m), m_route(route), m_title(title)
    {
        name = id;
        type = "report";
    }
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_tree(bool deep, ecatshim::DeviceNode &out) override;
    bool get_field(const std::string &field, unsigned int, unsigned int, DeviceFieldValue &out) override
    {
        for (const auto &v : m_values)
            if (v.first == field) {
                out.numeric = false;
                out.text = v.second;
                return true;
            }
        return false;
    }

private:
    RemoteMachine * m_m;
    QString m_route;  // under the machine's base: the emulator found again after a loss has another id
    std::string m_title;
    std::vector<std::pair<std::string, std::string>> m_values;
    QJsonObject m_last;

    // The report, or the last one read when the emulator does not give it (gone, an error): an error's answer is
    // never shown as the device's state
    QJsonObject read();

    void flatten(const QJsonValue &v, const std::string &path)
    {
        if (m_values.size() > 400) return;
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
                flatten(it.value(), path.empty() ? it.key().toStdString() : path + "." + it.key().toStdString());
        } else if (v.isArray()) {
            const QJsonArray a = v.toArray();
            for (int i = 0; i < a.size() && i < 32; i++) flatten(a[i], path + "[" + std::to_string(i) + "]");
        } else if (v.isBool()) {
            m_values.emplace_back(path, v.toBool() ? "true" : "false");
        } else if (v.isDouble()) {
            const double d = v.toDouble();
            m_values.emplace_back(path, d == (double)(long long)d ? std::to_string((long long)d) : std::to_string(d));
        } else if (v.isString()) {
            m_values.emplace_back(path, v.toString().toStdString());
        } else {
            m_values.emplace_back(path, "null");
        }
    }
};

//------------------------------------------------------------------- the processor

const char * const kFlagNames[8] = {"S", "Z", "5", "H", "3", "P", "N", "C"};

class RemoteZ80 : public ecatshim::CPU
{
public:
    explicit RemoteZ80(RemoteMachine * m) : m_m(m)
    {
        name = "cpu";
        type = "Z80";
    }
    unsigned int get_pc() override;
    unsigned int peek_mem(unsigned int address) override;
    bool poke_mem(unsigned int address, unsigned int value) override;
    ecatshim::DisAsm * create_disasm(const std::string &, emulator::Result &) override { return CreatePlainZ80DisAsm(); }
    std::vector<CpuRegister> get_register_values() override;
    std::vector<std::pair<std::string, std::string>> get_flags() override;
    void set_context_value(const std::string &name, unsigned int value) override;
    bool set_flag(const std::string &name, bool value) override;
    bool stack_address(int i, unsigned int &address) override;
    std::vector<unsigned int> get_history() override;
    bool has_breakpoint(unsigned int address) override;
    void add_breakpoint(unsigned int address) override;
    void remove_breakpoint(unsigned int address) override;
    void clear_breakpoints() override
    {
        for (unsigned int a : get_breakpoints()) remove_breakpoint(a);
    }
    std::vector<unsigned int> get_breakpoints() override;
    std::string breakpoint_condition(unsigned int address) override;
    std::vector<std::string> event_kinds() override { return m_event_kinds; }
    std::vector<ecatshim::EventBreakpoint> get_event_breakpoints() override;
    bool add_event_breakpoint(const std::string &kind, unsigned int value, unsigned int mask, const std::string &hits,
                              std::string &error) override;
    bool reset_event_hits(unsigned int id) override;
    bool remove_event_breakpoint(unsigned int id) override;
    bool enable_event_breakpoint(unsigned int id, bool on) override;
    void set_event_kinds(std::vector<std::string> kinds) { m_event_kinds = std::move(kinds); }
    std::vector<ecatshim::AddressWindow> get_windows() override;
    void set_pages(ecatshim::AddressableDevice * ram, ecatshim::AddressableDevice * rom, const ZxPaging &paging)
    {
        m_ram = ram;
        m_rom = rom;
        m_paging = paging;
    }

private:
    RemoteMachine * m_m;
    ecatshim::AddressableDevice * m_ram = nullptr;
    ecatshim::AddressableDevice * m_rom = nullptr;
    ZxPaging m_paging;
    std::vector<std::string> m_event_kinds;
};

// A sound card's Z80: the General Sound's ("gs") or the GS of a MultiSound card ("ms"). Its registers, windows and
// 64 KB come with ?cpu= (once per snapshot of the machine, each when first looked at), its breakpoints are its own,
// a step of its tab is the card's step (POST /step?cpu=: the machine runs until the card's step ends)
class RemoteCardZ80 : public ecatshim::CPU
{
public:
    RemoteCardZ80(RemoteMachine * m, const QString &cpu) : m_m(m), m_cpu(cpu)
    {
        name = cpu.toStdString();
        type = (cpu == "gs") ? "Z80 (General Sound)" : "Z80 (MultiSound GS)";
    }
    const QString &cpu() const { return m_cpu; }
    unsigned int get_pc() override;
    unsigned int peek_mem(unsigned int address) override;
    bool poke_mem(unsigned int address, unsigned int value) override;
    ecatshim::DisAsm * create_disasm(const std::string &, emulator::Result &) override { return CreatePlainZ80DisAsm(); }
    std::vector<CpuRegister> get_register_values() override;
    std::vector<std::pair<std::string, std::string>> get_flags() override;
    void set_context_value(const std::string &name, unsigned int value) override;
    bool set_flag(const std::string &name, bool value) override;
    bool stack_address(int i, unsigned int &address) override;
    std::vector<unsigned int> get_history() override;
    bool has_breakpoint(unsigned int address) override;
    void add_breakpoint(unsigned int address) override;
    void remove_breakpoint(unsigned int address) override;
    void clear_breakpoints() override
    {
        for (unsigned int a : get_breakpoints()) remove_breakpoint(a);
    }
    std::vector<unsigned int> get_breakpoints() override;
    std::vector<std::string> event_kinds() override { return m_event_kinds; }
    std::vector<ecatshim::EventBreakpoint> get_event_breakpoints() override;
    bool add_event_breakpoint(const std::string &kind, unsigned int value, unsigned int mask, const std::string &hits,
                              std::string &error) override;
    bool reset_event_hits(unsigned int id) override;
    bool remove_event_breakpoint(unsigned int id) override;
    bool enable_event_breakpoint(unsigned int id, bool on) override;
    void set_event_kinds(std::vector<std::string> kinds) { m_event_kinds = std::move(kinds); }
    std::vector<ecatshim::AddressWindow> get_windows() override;
    bool card_cycle(CardCycle &out) override;

    // The card's RAM and ROM (flash on the NeoGS) as paged memories, when the machine lists them
    void set_ram(ecatshim::AddressableDevice * ram) { m_ram = ram; }
    void set_rom(ecatshim::AddressableDevice * rom, bool flash)
    {
        m_rom = rom;
        m_flash = flash;
    }
    // The card's execution history (`<slot>.cpu`), when it has one
    void set_history_device(const QString &device) { m_history = device; }

private:
    RemoteMachine * m_m;
    QString m_cpu;
    QString m_history;
    std::vector<std::string> m_event_kinds;
    ecatshim::AddressableDevice * m_ram = nullptr;
    ecatshim::AddressableDevice * m_rom = nullptr;
    bool m_flash = false;
    // What was read for a snapshot of the machine: each part once per generation
    uint64_t m_regs_gen = ~0ull;
    QJsonObject m_regs;
    uint64_t m_memory_gen = ~0ull;
    QByteArray m_memory;
    uint64_t m_windows_gen = ~0ull;
    QJsonArray m_windows;

    QString route(const QString &path) const;
    const QJsonObject &regs();
};

//------------------------------------------------------------------- the machine

class RemoteMachine : public ecatshim::Emulator
{
public:
    RemoteMachine(const RemoteOptions &o, const QString &id)
        : m_http(QString::fromStdString(o.host), o.port), m_host(QString::fromStdString(o.host)), m_port(o.port),
          m_want(QString::fromStdString(o.emulator)), m_base("/api/v1/emulator/" + id),
          m_settings("unreal-ng", "unreal-debugger"), m_sync(o.synchronous)
    {
        m_http.set_gate(&m_online);
        // The build reads everything before the window is there: waiting, whatever the mode
        m_http.set_sync(true);
        m_http.set_on_arrival([this]() { arrived(); });
        m_http.set_on_failure([this](const QString &why) { notice(why); });
        m_wait_base = m_base;
        m_notice_age.start();
    }

    ~RemoteMachine() override
    {
        m_stop = true;
        m_listener_off = true;
        if (m_waiter) {
            m_waiter->wait();
            delete m_waiter;
        }
        // The last commands: the run-to gone, debug mode as it was before the debugger came, the histories the
        // debugger turned on off again; up to 2 s for them, then the requests in flight are given up
        const QString base = m_base;
        const bool attached = m_attached;
        const std::set<QString> ours = m_histories_ours;
        m_http.task([this, base, attached, ours](Http &http) {
            remove_run_to(http, base);
            if (attached) http.json("PUT", base + "/debugmode", QJsonObject{{"enabled", m_debug_before.load()}});
            for (const QString &device : ours)
                http.json("POST", base + "/history/" + device + "/config", QJsonObject{{"mode", "off"}});
        });
        m_http.close(2000);
    }

    // The devices, as the in-process adapter makes them: the CPU, RAM, ROM, the device memories, state reports
    bool build(std::string &error)
    {
        m_id = m_base.mid(QString("/api/v1/emulator/").size());
        for (const QJsonValue &e : m_http.json("GET", "/api/v1/emulator").value("emulators").toArray())
            if (e.toObject().value("id").toString() == m_id) m_model = e.toObject().value("model").toString();
        if (!fresh()) {
            error = "no debugger snapshot from the emulator";
            return false;
        }
        m_cpu = std::make_shared<RemoteZ80>(this);
        m_dm.add(m_cpu);
        // A processor tab for each sound card's Z80 the machine has (404 without that card)
        for (const char * cpu : {"gs", "ms"}) {
            int status = 0;
            m_http.json("GET", m_base + "/registers?cpu=" + cpu, QJsonObject(), &status);
            if (status != 200) continue;
            m_cards.push_back(std::make_shared<RemoteCardZ80>(this, cpu));
            m_dm.add(m_cards.back());
        }

        const QJsonObject info = m_http.json("GET", m_base + "/memory/info");
        const QJsonObject pages = info.value("pages").toObject();
        // The RAM the model has (ram_kb), not the core's capacity; ROM as the core counts it
        const QJsonArray list = m_http.json("GET", "/api/v1/emulator").value("emulators").toArray();
        unsigned int ram_pages = (unsigned int)pages.value("ram").toObject().value("count").toInt();
        for (const QJsonValue &e : list)
            if (e.toObject().value("id").toString() == m_id && e.toObject().value("ram_kb").toInt() > 0)
                ram_pages = (unsigned int)e.toObject().value("ram_kb").toInt() / 16;
        // The ROM pages the machine loaded (16 KB each), not the core's capacity
        unsigned int rom_pages = (unsigned int)m_http.json("GET", m_base + "/state/memory/rom").value("total_rom_pages").toInt();
        if (rom_pages == 0) rom_pages = (unsigned int)pages.value("rom").toObject().value("count").toInt();
        auto ram = std::make_shared<RemotePages>(this, "ram", std::max(1u, ram_pages));
        auto rom = std::make_shared<RemotePages>(this, "rom", std::max(1u, rom_pages));
        m_mapper = std::make_shared<RemoteMapper>(this, ram.get(), rom.get());
        m_cpu->mm = m_mapper.get();
        const std::string model = m_http.json("GET", m_base + "/state/paging").value("model").toString().toStdString();
        const ZxPaging paging = ZxModelPaging(model);
        m_cpu->set_pages(ram.get(), rom.get(), paging);
        m_cpu->set_event_kinds(ZxMainEventKinds(model, !m_cards.empty()));
        ram->set_paging(paging);
        m_dm.add(ram);
        m_dm.add(rom);

        const QJsonArray regions = m_http.json("GET", m_base + "/memory/regions").value("regions").toArray();
        bool neogs = false;
        for (const QJsonValue &v : regions) {
            const QJsonObject r = v.toObject();
            const QString n = r.value("name").toString();
            neogs = neogs || n.startsWith("neogs.");
            const QString kind = n.mid(n.lastIndexOf('.') + 1);
            const unsigned int size = (unsigned int)r.value("size").toDouble();
            const bool writable = r.value("writable").toBool();
            std::string desc = r.value("description").toString().toStdString();
            desc = desc.rfind("Device memory recorded by time travel", 0) == 0 ? "device memory"
                                                                                : desc.substr(0, desc.find_first_of(":,;("));
            // A sound card's RAM and ROM: its Z80's pages
            const QString owner = n.left(n.lastIndexOf('.'));
            RemoteCardZ80 * card = this->card(owner == "multisound.gs" ? "ms" : (owner == "gs" || owner == "neogs") ? "gs" : "");
            if (card != nullptr && (kind == "ram" || kind == "rom" || kind == "flash")) {
                auto pages = std::make_shared<RemoteCardPages>(this, n, size, writable, desc);
                if (kind == "ram") card->set_ram(pages.get());
                else card->set_rom(pages.get(), kind == "flash");
                m_dm.add(pages);
                continue;
            }
            if (kind == "ram" || kind == "vram" || kind == "fastram")
                m_dm.add(std::make_shared<RemoteRegion<ecatshim::Memory>>(this, n, size, writable, desc));
            else
                m_dm.add(std::make_shared<RemoteRegion<ecatshim::AddressableDevice>>(this, n, size, writable, desc));
        }

        for (const auto &c : m_cards) c->set_event_kinds(ZxCardEventKinds(c->cpu() == "gs" && neogs));

        // The reports this machine has, as the device catalog lists them (its cards are fixed: a slot change makes a
        // new machine, another id)
        for (const QJsonValue &v : catalog().value("reports").toArray()) {
            const QJsonObject r = v.toObject();
            m_dm.add(std::make_shared<RemoteReport>(this, r.value("id").toString().toStdString(),
                                                    r.value("title").toString().toStdString(),
                                                    r.value("route").toString()));
        }
        // Every device's history for the History tab; a processor's ("cpu", "zxbus.1.cpu") comes through its CPU
        for (const QJsonValue &v : histories().value("histories").toArray()) {
            const QJsonObject h = v.toObject();
            const QString device = h.value("device").toString();
            // The General Sound's (`<slot>.cpu`) through the card's CPU; the MultiSound's GS keeps none
            if (device != "cpu" && device.endsWith(".cpu") && card("gs") != nullptr) card("gs")->set_history_device(device);
            if (device == "cpu" || device.endsWith(".cpu")) continue;
            m_dm.add(std::make_shared<RemoteHistory>(this, h.value("device").toString().toStdString(),
                                                     h.value("what").toString().toStdString()));
        }

        dm = &m_dm;
        loaded = true;
        data_path = QDir::currentPath().toStdString();

        m_seq = m_snap.seq;
        m_waiter = QThread::create([this]() { wait_loop(); });
        m_waiter->start();
        // From now on the views' thread never waits (unless asked to: the self-check)
        m_http.set_sync(m_sync);
        return true;
    }

    Net &http() { return m_http; }

    // The device catalog, read again when the last read is half a second old: one request a refresh for every
    // entry's summary. Not given (the emulator gone, an error), the last one read stays
    QJsonObject catalog()
    {
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        if (!m_catalog_age.isValid() || m_catalog_age.elapsed() > 500) {
            QByteArray out;
            if (m_http.get(m_base + "/state/devices", out, 500) == 200) m_catalog = QJsonDocument::fromJson(out).object();
            m_catalog_age.start();
        }
        return m_catalog;
    }
    // The device histories with their modes and counts, read again when half a second old (the History tab's
    // sources); not given, the last one read stays
    QJsonObject histories()
    {
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        if (!m_histories_age.isValid() || m_histories_age.elapsed() > 500) {
            QByteArray out;
            if (m_http.get(m_base + "/history", out, 500) == 200) m_histories = QJsonDocument::fromJson(out).object();
            m_histories_age.start();
        }
        return m_histories;
    }
    const QString &base() const { return m_base; }
    // The emulator taken over (empty while offline) and the WebAPI's address, for the GUI control plane
    QString id() const { return m_online.load() ? m_id : QString(); }
    QString endpoint() const { return QString("http://%1:%2").arg(m_host).arg(m_port); }

    bool debug_online() const override { return m_online.load(); }
    // A device board: GET /debug/boards/{id}, its layout as the shim's board
    Board read_board(const std::string &id) override
    {
        Board out;
        out.id = id;
        if (!m_online.load()) {
            out.message = "The emulator is not connected";
            return out;
        }
        int status = 0;
        const QJsonObject body = m_http.json("GET", m_base + "/debug/boards/" + QString::fromStdString(id), QJsonObject(), &status);
        out.ok = status == 200;
        if (!out.ok) {
            out.message = status == 0 ? std::string("No answer from the emulator") : body.value("message").toString().toStdString();
            return out;
        }
        auto str = [](const QJsonObject &o, const char * key) { return o.value(key).toString().toStdString(); };
        out.title = str(body, "title");
        out.available = body.value("available").toBool();
        for (const QJsonValue &column : body.value("columns").toArray())
            for (const QJsonValue &gv : column.toObject().value("groups").toArray()) {
                const QJsonObject g = gv.toObject();
                BoardGroup group{str(g, "title"), str(g, "raw"), {}};
                for (const QJsonValue &cv : g.value("controls").toArray()) {
                    const QJsonObject c = cv.toObject();
                    BoardControl control;
                    control.id = str(c, "id");
                    control.label = str(c, "label");
                    control.type = str(c, "type");
                    control.note = str(c, "note");
                    control.value = scalar_text(c.value("value"));
                    for (const QJsonValue &name : c.value("names").toArray()) control.names.push_back(name.toString().toStdString());
                    if (c.value("max").isDouble()) control.max = (long long)c.value("max").toDouble();
                    group.controls.push_back(std::move(control));
                }
                out.groups.push_back(std::move(group));
            }
        for (const QJsonValue &a : body.value("actions").toArray())
            out.actions.emplace_back(str(a.toObject(), "id"), str(a.toObject(), "title"));
        return out;
    }
    // A board's action: POST /debug/boards/{board}/actions/{action} {arg}
    BoardAnswer board_action(const std::string &board, const std::string &action, const std::string &arg) override
    {
        if (!m_online.load()) return {false, "The emulator is not connected"};
        QJsonObject body;
        if (!arg.empty()) body.insert("arg", QString::fromStdString(arg));
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/debug/boards/" + QString::fromStdString(board) + "/actions/" +
                                                      QString::fromStdString(action), body, &status);
        if (status == 0) return {false, "No answer from the emulator"};
        if (status == kQueued) return {true, board + " " + action + ": sent"};
        if (status != 200) return {false, r.value("message").toString().toStdString()};
        return {true, board + " " + action + ": " + r.value("status").toString().toStdString()};
    }

    // A card's `gs` report (gs: GET state/audio/gs; ms: the gs part of GET state/audio/multisound), once per snapshot
    // of the machine; not given, the last one read stays
    QJsonObject card_report(const QString &cpu)
    {
        CardReport &c = m_card_reports[cpu];
        const uint64_t gen = snap().generation;
        if (c.generation != gen && m_online.load()) {
            c.generation = gen;
            int status = 0;
            const bool ms = cpu == "ms";
            const QJsonObject o = m_http.json("GET", m_base + (ms ? "/state/audio/multisound" : "/state/audio/gs"), QJsonObject(), &status);
            if (status == 200) c.report = ms ? o.value("gs").toObject() : o;
        }
        return c.report;
    }
    // The ZX-DMA banner: the `gs` report's neogs.dma.zx (no NeoGS: no such part, no banner)
    ZxDmaBanner zxdma_banner() override
    {
        ZxDmaBanner out;
        if (card("gs") == nullptr) return out;
        const QJsonObject zx = card_report("gs").value("neogs").toObject().value("dma").toObject().value("zx").toObject();
        out.text = zx.value("banner").toString().toStdString();
        out.card_bytes = !out.text.empty() && zx.value("mode").toString() == "divert" && zx.value("rom_paged").toBool(true);
        out.window_start = (unsigned int)zx.value("window_start").toInt(0x0000);
        out.window_end = (unsigned int)zx.value("window_end").toInt(0x3FFF);
        return out;
    }
    // Plain GETs, a second apart: the card's line and the banner of a running machine
    unsigned int live_card_ms() const override { return 1000; }

    // The ZX-DMA trace: GET state/audio/gs/zxdma?last=&since=
    ZxDmaTrace read_zxdma_trace(size_t last, uint64_t since) override
    {
        ZxDmaTrace out;
        if (!m_online.load()) {
            out.message = "The emulator is not connected";
            return out;
        }
        int status = 0;
        const QJsonObject body = m_http.json("GET", m_base + QString("/state/audio/gs/zxdma?last=%1&since=%2")
                                                                  .arg((qulonglong)last).arg((qulonglong)since),
                                             QJsonObject(), &status);
        if (status != 200) {
            out.message = status == 0 ? std::string("No answer from the emulator") : body.value("message").toString().toStdString();
            return out;
        }
        auto str = [](const QJsonObject &o, const char * key) { return o.value(key).toString().toStdString(); };
        auto num = [](const QJsonObject &o, const char * key) { return (int64_t)o.value(key).toDouble(); };
        out.ok = true;
        out.available = body.value("available").toBool();
        if (!out.available) {
            out.message = str(body, "description");
            return out;
        }
        out.card = str(body, "card");
        out.device = str(body, "device");
        out.capturing = body.value("capturing").toBool();
        out.armed = body.value("armed").toBool();
        out.total_bytes_kept = (uint64_t)num(body, "total_bytes_kept");
        out.evicted = (uint64_t)num(body, "evicted");
        out.last_seq = (uint64_t)num(body, "last_seq");
        out.restarted = body.value("restarted").toBool();
        const QJsonObject c = body.value("counters").toObject();
        out.bytes_read = (uint64_t)num(c, "bytes_read");
        out.bytes_written = (uint64_t)num(c, "bytes_written");
        out.bytes_dropped = (uint64_t)num(c, "bytes_dropped");
        out.wait_t_states = (uint64_t)num(c, "wait_t_states");
        for (const QJsonValue &v : body.value("rows").toArray()) {
            const QJsonObject r = v.toObject();
            ZxDmaRow row;
            row.seq = (uint64_t)num(r, "seq");
            row.frame = num(r, "frame");
            row.host_t = num(r, "host_t");
            row.card_time = num(r, "card_time");
            row.write = r.value("direction").toString() == "write";
            row.card_address = (unsigned int)num(r, "card_address");
            row.page = (unsigned int)num(r, "page");
            row.value = (unsigned int)num(r, "value");
            row.wait = (unsigned int)num(r, "wait");
            row.host_pc = (unsigned int)num(r, "host_pc");
            row.dropped = r.value("dropped").toBool();
            out.rows.push_back(row);
        }
        out.text = str(body, "text");
        out.how_to_start = str(body, "how_to_start");
        return out;
    }
    // The port trace's capture: POST control/audio/gs/porttrace {action}
    BoardAnswer zxdma_trace_capture(const std::string &action) override
    {
        if (!m_online.load()) return {false, "The emulator is not connected"};
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/control/audio/gs/porttrace",
                                          QJsonObject{{"action", QString::fromStdString(action)}}, &status);
        if (status == 0) return {false, "No answer from the emulator"};
        if (status == kQueued) return {true, "port trace " + action + ": sent"};
        if (status != 200) return {false, r.value("message").toString().toStdString()};
        return {true, "port trace " + action + ": " + r.value("status").toString().toStdString()};
    }

    // The command log history of a card (`zxbus.1.commands`, the MultiSound's `zxbus.1.gs.commands`): the device the
    // catalog's command log entry routes to (history/<device>?last=N), "" without one
    QString command_log_device(const std::string &card)
    {
        const QString id = card == "ms" ? "ms-gs-commands" : "gs-commands";
        for (const QJsonValue &v : catalog().value("reports").toArray()) {
            const QJsonObject r = v.toObject();
            if (r.value("id").toString() != id) continue;
            QString route = r.value("route").toString();
            if (!route.startsWith("history/")) return QString();
            route = route.mid(QString("history/").size());
            return route.left(route.indexOf('?') < 0 ? route.size() : route.indexOf('?'));
        }
        return QString();
    }
    // The GS monitor over the WebAPI: plain GETs, nothing pauses (the card's report, the command log history, the
    // firmware's unknown commands)
    GsMonitor read_gs_monitor(const std::string &card, size_t last, std::optional<uint64_t> since) override
    {
        GsMonitor out;
        if (!m_online.load()) {
            out.message = "The emulator is not connected";
            return out;
        }
        const bool ms = card == "ms";
        int status = 0;
        QJsonObject report = m_http.json("GET", m_base + (ms ? "/state/audio/multisound" : "/state/audio/gs"), QJsonObject(), &status);
        if (status != 200) {
            out.message = status == 0 ? std::string("No answer from the emulator") : report.value("message").toString().toStdString();
            return out;
        }
        if (ms) report = report.value("gs").toObject();
        if (report.isEmpty() || (report.contains("available") && !report.value("available").toBool())) {
            out.message = ms ? "No ZX-MultiSound with a General Sound" : "General Sound card not fitted";
            return out;
        }
        out.ok = true;
        auto str = [](const QJsonObject &o, const char * key) { return o.value(key).toString().toStdString(); };
        auto num = [](const QJsonObject &o, const char * key, int64_t def = 0) {
            return o.value(key).isDouble() ? (int64_t)o.value(key).toDouble() : def;
        };
        const QJsonObject protocol = report.value("protocol").toObject();
        out.protocol_on = protocol.value("on").toBool();
        out.protocol_text = str(protocol, "text");
        out.status = (int)num(report, "status", -1);
        out.command_pending = report.value("command_pending").toBool();
        out.data_pending = report.value("data_pending").toBool();
        out.command_from_host = (int)num(report, "command_from_host", -1);
        out.data_from_host = (int)num(report, "data_from_host", -1);
        out.data_to_host = (int)num(report, "data_to_host", -1);
        if (report.value("last_send").isObject()) {
            const QJsonObject send = report.value("last_send").toObject();
            out.has_last_send = true;
            out.last_send_command = (int)num(send, "command", -1);
            out.last_send_name = str(send, "name");
            out.last_send_state = str(send, "state");
            for (const QJsonValue &r : send.value("replies").toArray()) out.last_send_replies.push_back(r.toInt());
        }
        const QString device = command_log_device(card);
        out.log_device = device.toStdString();
        if (device.isEmpty()) return out;
        QString query = QString("?last=%1").arg((qulonglong)last);
        if (since) query += QString("&since=%1").arg((qulonglong)*since);
        const QJsonObject log = m_http.json("GET", m_base + "/history/" + device + query, QJsonObject(), &status);
        if (status != 200) return out;
        out.log_mode = str(log, "mode");
        out.written = (uint64_t)num(log, "written");
        out.kept = (uint64_t)num(log, "kept");
        out.dropped = (uint64_t)num(log, "dropped");
        if (log.contains("next_since")) {
            out.cursor = true;
            out.next_since = (uint64_t)num(log, "next_since");
            out.missed = (uint64_t)num(log, "missed");
            out.restarted = log.value("restarted").toBool();
        }
        for (const QJsonValue &v : log.value("records").toArray()) {
            const QJsonObject r = v.toObject();
            GsMonitorRow row;
            row.seq = (uint64_t)num(r, "seq");
            row.frame = num(r, "frame");
            row.t = num(r, "t");
            row.card_cycle = num(r, "card_cycle");
            row.command = (int)num(r, "command", -1);
            row.kind = str(r, "kind");
            row.name = str(r, "name");
            row.text = str(r, "text");
            row.anomaly = str(r, "anomaly");
            row.bytes = num(r, "bytes", -1);
            row.direction = str(r, "direction");
            row.host_data = r.value("host_data").toBool();
            out.rows.push_back(std::move(row));
        }
        if (out.log_mode == "off") return out;
        const QJsonObject firmware = m_http.json("GET", m_base + "/state/audio/gs/firmware?object=unknown_commands" +
                                                            QString(ms ? "&cpu=ms" : ""), QJsonObject(), &status);
        if (status == 200 && firmware.value("unknown_commands").isArray()) {
            out.unknown_present = true;
            for (const QJsonValue &v : firmware.value("unknown_commands").toArray()) {
                const uint64_t count = (uint64_t)num(v.toObject(), "count");
                out.unknown.emplace_back((int)num(v.toObject(), "command"), count);
                out.unknown_total += count;
            }
        }
        return out;
    }
    // The command log on or off: POST history/<device>/config {mode}
    BoardAnswer gs_command_log(const std::string &card, bool on) override
    {
        if (!m_online.load()) return {false, "The emulator is not connected"};
        const QString device = command_log_device(card);
        if (device.isEmpty()) return {false, "This card has no command log"};
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/history/" + device + "/config",
                                          QJsonObject{{"mode", on ? "ring" : "off"}}, &status);
        if (status == 0) return {false, "No answer from the emulator"};
        if (status != 200 && status != kQueued)
            return {false, (r.value("error").toString() + r.value("message").toString()).toStdString()};
        {
            // The History tab's sources read again at their next look
            std::lock_guard<std::mutex> lock(m_catalog_mutex);
            m_histories_age.invalidate();
        }
        return {true, "command log " + device.toStdString() + (on ? ": ring" : ": off")};
    }

    // The card's report for the analysis tabs: state/audio/gs, or state/audio/multisound's gs part (empty: not read)
    QJsonObject gs_report(const std::string &card, std::string &message)
    {
        int status = 0;
        const bool ms = card == "ms";
        QJsonObject report = m_http.json("GET", m_base + (ms ? "/state/audio/multisound" : "/state/audio/gs"), QJsonObject(), &status);
        if (status != 200) {
            message = status == 0 ? std::string("No answer from the emulator") : report.value("message").toString().toStdString();
            return QJsonObject();
        }
        if (ms) report = report.value("gs").toObject();
        if (report.isEmpty() || (report.contains("available") && !report.value("available").toBool())) {
            message = ms ? "No ZX-MultiSound with a General Sound" : "General Sound card not fitted";
            return QJsonObject();
        }
        return report;
    }
    // A card CPU's symbols (GET labels?cpu=&active=true), by address, kept 30 s or until the profile read changes:
    // the dispatch's 256 handlers are looked up in it
    const std::map<unsigned int, std::string> &card_labels(const std::string &card, const std::string &profile)
    {
        CardLabels &c = m_card_labels[card];
        if (!c.age.isValid() || c.age.elapsed() > 30000 || c.profile != profile) {
            c.by_address.clear();
            c.profile = profile;
            c.age.start();
            int status = 0;
            const QJsonObject r = m_http.json("GET", m_base + "/labels?active=true&cpu=" + QString::fromStdString(card), QJsonObject(), &status);
            if (status == kPending) c.age.invalidate();  // asked again at the next look
            if (status == 200)
                for (const QJsonValue &v : r.value("labels").toArray()) {
                    const QJsonObject l = v.toObject();
                    c.by_address.emplace((unsigned int)l.value("address").toInt(), l.value("name").toString().toStdString());
                }
        }
        return c.by_address;
    }
    // The label at @p pc of a CPU ("main", "gs", "ms"): GET labels/resolve?address= (exact, else the nearest below
    // within 256 bytes: "NAME+4"), kept 30 s
    std::string resolve_label(const std::string &cpu, unsigned int pc)
    {
        if (!m_resolved_age.isValid() || m_resolved_age.elapsed() > 30000) {
            m_resolved.clear();
            m_resolved_age.start();
        }
        const auto key = std::make_pair(cpu, pc);
        auto it = m_resolved.find(key);
        if (it != m_resolved.end()) return it->second;
        int status = 0;
        const QJsonObject r = m_http.json("GET", m_base + QString("/labels/resolve?address=%1").arg(pc) +
                                                     (cpu == "main" ? QString() : "&cpu=" + QString::fromStdString(cpu)),
                                          QJsonObject(), &status);
        std::string label;
        if (status == kPending) return label;  // not kept: asked again at the next look
        if (status == 200) {
            if (r.value("found").toBool()) {
                label = r.value("label").toObject().value("name").toString().toStdString();
            } else if (r.value("nearest_below").isObject()) {
                const QJsonObject below = r.value("nearest_below").toObject();
                const int distance = below.value("distance").toInt();
                if (distance > 0 && distance < 0x100) label = below.value("name").toString().toStdString() + "+" + std::to_string(distance);
            }
        }
        m_resolved.emplace(key, label);
        return label;
    }
    static unsigned int hex_value(const QString &s)
    {
        QString t = s;
        if (t.startsWith('#')) t = t.mid(1);
        bool ok = false;
        const unsigned int v = t.toUInt(&ok, 16);
        return ok ? v : 0;
    }
    // A struct field's value as a cell (as in process): under 256 decimal, a bigger number #hex, lists in brackets
    static std::string field_text(const QJsonValue &v)
    {
        if (v.isDouble()) {
            const qint64 i = (qint64)v.toDouble();
            if (i < 256 || i > 0xFFFF) return std::to_string(i);
            char b[16];
            std::snprintf(b, sizeof(b), "#%04X", (unsigned int)i);
            return b;
        }
        if (v.isString()) return v.toString().toStdString();
        if (v.isArray()) {
            std::string t = "[";
            const QJsonArray a = v.toArray();
            for (int i = 0; i < a.size(); i++) t += (i ? " " : "") + field_text(a.at(i));
            return t + "]";
        }
        if (v.isObject()) {
            std::string t = "{";
            const QJsonObject o = v.toObject();
            bool first = true;
            for (auto it = o.begin(); it != o.end(); ++it) {
                t += (first ? "" : ", ") + it.key().toStdString() + " " + field_text(it.value());
                first = false;
            }
            return t + "}";
        }
        return std::string();
    }

    // The firmware objects over the WebAPI: GET state/audio/gs/firmware[?profile=][&cpu=ms] and the card's report
    GsFirmware read_gs_firmware(const std::string &card, const std::string &profile) override
    {
        GsFirmware out;
        if (!m_online.load()) {
            out.message = "The emulator is not connected";
            return out;
        }
        const QJsonObject report = gs_report(card, out.message);
        if (report.isEmpty()) return out;
        QStringList query;
        if (!profile.empty()) query << "profile=" + QString::fromStdString(profile);
        if (card == "ms") query << "cpu=ms";
        int status = 0;
        const QJsonObject body = m_http.json("GET", m_base + "/state/audio/gs/firmware" + (query.isEmpty() ? QString() : "?" + query.join('&')),
                                             QJsonObject(), &status);
        if (status != 200) {
            out.message = status == 0 ? std::string("No answer from the emulator")
                                      : (body.value("error").toString() + body.value("message").toString()).toStdString();
            return out;
        }
        out.ok = true;
        auto str = [](const QJsonObject &o, const char * key) { return o.value(key).toString().toStdString(); };
        auto num = [](const QJsonObject &o, const char * key, int64_t def = 0) {
            return o.value(key).isDouble() ? (int64_t)o.value(key).toDouble() : def;
        };
        out.profile = str(body, "profile");
        out.title = str(body, "title");
        out.source = str(body, "source");
        const QJsonObject fp = report.value("firmware_profile").toObject();
        out.rom_profile = str(fp, "id");
        out.rom_title = str(fp, "title");
        const QJsonObject protocol = report.value("protocol").toObject();
        out.log_on = protocol.value("on").toBool();
        out.top_profile = str(protocol, "profile");
        for (const QJsonValue &v : protocol.value("uploads").toArray()) {
            const QJsonObject u = v.toObject();
            GsUpload up;
            up.address = (unsigned int)num(u, "address");
            up.length = (uint64_t)num(u, "length");
            up.matched_length = (uint64_t)num(u, "matched_length", (int64_t)up.length);
            up.sha256 = str(u, "sha256");
            up.profile = str(u, "profile");
            up.frame = num(u, "frame");
            up.started = u.value("started").toBool();
            out.uploads.push_back(std::move(up));
        }
        if (body.value("dispatch").isObject()) {
            const QJsonObject d = body.value("dispatch").toObject();
            const std::map<unsigned int, std::string> &labels = card_labels(card == "ms" ? "ms" : "gs", out.profile);
            out.has_dispatch = true;
            out.table = str(d, "table");
            out.table_high = str(d, "table_high");
            out.patched = num(d, "patched");
            out.unknown = num(d, "unknown");
            for (const QJsonValue &v : d.value("entries").toArray()) {
                const QJsonObject e = v.toObject();
                GsDispatchEntry de;
                de.command = (int)num(e, "command");
                de.name = str(e, "name");
                de.readable = !e.contains("readable") || e.value("readable").toBool();
                de.handler = hex_value(e.value("handler").toString());
                if (de.readable) {
                    auto it = labels.find(de.handler);
                    if (it != labels.end()) de.label = it->second;
                }
                de.has_baseline = e.contains("baseline");
                de.baseline = hex_value(e.value("baseline").toString());
                de.patched = e.value("patched").toBool();
                de.patched_by = str(e, "patched_by");
                de.unknown = e.value("unknown").toBool();
                out.entries.push_back(std::move(de));
            }
        }
        if (body.value("unknown_commands").isArray()) {
            out.unknown_present = true;
            for (const QJsonValue &v : body.value("unknown_commands").toArray())
                out.unknown_commands.emplace_back((int)num(v.toObject(), "command"), (uint64_t)num(v.toObject(), "count"));
        }
        for (const QJsonValue &iv : body.value("instances").toArray()) {
            const QJsonObject inst = iv.toObject();
            GsStructTable t;
            t.name = str(inst, "name");
            t.struct_name = str(inst, "struct");
            t.at = str(inst, "at");
            t.count = num(inst, "count");
            t.size = num(inst, "size");
            for (const QJsonValue &rv : inst.value("rows").toArray()) {
                const QJsonObject r = rv.toObject();
                const QJsonObject fields = r.value("fields").toObject();
                // QJsonObject sorts its keys: the columns are the schema's order as the first row's members came
                if (t.columns.empty()) for (const QString &k : fields.keys()) t.columns.push_back(k.toStdString());
                std::vector<std::string> cells;
                for (const std::string &k : t.columns) cells.push_back(field_text(fields.value(QString::fromStdString(k))));
                t.row_at.push_back(str(r, "at"));
                t.rows.push_back(std::move(cells));
            }
            out.structs.push_back(std::move(t));
        }
        for (const QJsonValue &vv : body.value("variables").toArray()) {
            const QJsonObject v = vv.toObject();
            GsVariable gv;
            gv.name = str(v, "name");
            gv.kind = str(v, "kind");
            gv.at = str(v, "at");
            if (gv.at.empty()) gv.at = str(v, "register") + " " + str(v, "pc_range");
            gv.meaning = str(v, "meaning");
            gv.reason = str(v, "reason");
            gv.valid = v.value("valid").toBool() && v.contains("value");
            gv.value = num(v, "value");
            out.variables.push_back(std::move(gv));
        }
        return out;
    }
    // The profiles: GET state/audio/gs/firmware/profiles, read once (they are the program's, they do not change)
    std::vector<GsProfileInfo> gs_firmware_profiles() override
    {
        if (!m_profiles.empty() || !m_online.load()) return m_profiles;
        int status = 0;
        const QJsonObject r = m_http.json("GET", m_base + "/state/audio/gs/firmware/profiles", QJsonObject(), &status);
        if (status != 200) return m_profiles;
        for (const QJsonValue &v : r.value("profiles").toArray()) {
            const QJsonObject p = v.toObject();
            GsProfileInfo info;
            info.id = p.value("id").toString().toStdString();
            info.title = p.value("title").toString().toStdString();
            info.source = p.value("source").toString().toStdString();
            info.commands = (int64_t)p.value("commands").toDouble();
            info.structs = (int64_t)p.value("structs").toDouble();
            info.instances = (int64_t)p.value("instances").toDouble();
            info.variables = (int64_t)p.value("variables").toDouble();
            m_profiles.push_back(std::move(info));
        }
        return m_profiles;
    }

    // The port writes over the WebAPI: the command log's newest records (history/<device>?last=400) and the port
    // trace's newest events (state/audio/gs/porttrace?events=N: both sides and the DAC fetches in one ring, no side
    // filter on the route, the GS slot's card only)
    GsWrites read_gs_writes(const std::string &card) override
    {
        GsWrites out;
        if (!m_online.load()) {
            out.message = "The emulator is not connected";
            return out;
        }
        const QJsonObject report = gs_report(card, out.message);
        if (report.isEmpty()) return out;
        out.ok = true;
        out.card_cpu = report.value("implementation").toString() != "lightweight" && !report.value("interpreter").isObject();
        auto num = [](const QJsonObject &o, const char * key, int64_t def = 0) {
            return o.value(key).isDouble() ? (int64_t)o.value(key).toDouble() : def;
        };
        int status = 0;
        const QString device = command_log_device(card);
        out.log_device = device.toStdString();
        if (!device.isEmpty()) {
            const QJsonObject log = m_http.json("GET", m_base + "/history/" + device + "?last=400", QJsonObject(), &status);
            if (status == 200) {
                out.log_mode = log.value("mode").toString().toStdString();
                for (const QJsonValue &v : log.value("records").toArray()) {
                    const QJsonObject r = v.toObject();
                    const QString kind = r.value("kind").toString();
                    GsPortWrite w;
                    w.host = true;
                    w.kind = kind.toStdString();
                    if (kind == "command") {
                        w.port = 0xBB;
                        w.value = (unsigned int)num(r, "command");
                    } else if (kind == "param" || kind == "data") {
                        w.port = 0xB3;
                        w.value = (unsigned int)num(r, "value");
                    } else if (kind == "marker") {
                        w.port = 0x33;
                        w.value = 0x80;
                    } else {
                        continue;
                    }
                    w.frame = num(r, "frame");
                    w.t = num(r, "t");
                    w.card_cycle = num(r, "card_cycle", -1);
                    w.has_pc = true;
                    w.pc = (unsigned int)num(r, "pc");
                    w.label = resolve_label("main", w.pc);
                    w.source = "command log";
                    out.writes.push_back(std::move(w));
                }
            }
        }
        if (card == "ms") {
            out.trace_note = "the WebAPI's port trace route reads the GS slot's card only (no cpu=ms)";
            return out;
        }
        const QJsonObject trace = m_http.json("GET", m_base + QString("/state/audio/gs/porttrace?events=%1").arg(kTraceEvents),
                                              QJsonObject(), &status);
        if (status != 200) {
            out.trace_note = "the port trace was not read";
            return out;
        }
        out.trace_present = true;
        const QJsonObject t = trace.value("trace").toObject();
        out.trace_capturing = t.value("capturing").toBool();
        out.trace_kept = (uint64_t)num(t, "event_count");
        out.trace_evicted = (uint64_t)num(t, "total_evicted");
        out.trace_note = QString("the trace's newest %1 events (the route has no side filter: the DAC fetches crowd the "
                                 "ports out)").arg(kTraceEvents).toStdString();
        for (const QJsonValue &v : trace.value("events").toArray()) {
            const QJsonObject e = v.toObject();
            const QString side = e.value("side").toString();
            if (e.value("direction").toString() != "out") continue;
            const bool host = side == "host";
            if (!host && side != "gs") continue;
            GsPortWrite w;
            w.host = host;
            w.port = (unsigned int)num(e, "port") & 0xFF;
            w.value = (unsigned int)num(e, "value");
            w.frame = num(e, "frame");
            w.card_cycle = num(e, "timestamp", -1);
            if (!host) {
                w.has_pc = true;
                w.pc = (unsigned int)num(e, "pc");
                w.label = resolve_label("gs", w.pc);
            }
            w.source = "port trace";
            out.writes.push_back(std::move(w));
        }
        return out;
    }
    // The port trace started / stopped: POST control/audio/gs/porttrace {action} (the GS slot's card only)
    BoardAnswer gs_port_trace(const std::string &card, const std::string &action) override
    {
        if (card == "ms") return {false, "The WebAPI's port trace route has no cpu=ms: the MultiSound's GS trace is not reachable"};
        return zxdma_trace_capture(action);
    }

    // The profiler and analysis reports over the WebAPI: GET state/audio/gs/<report>?<query>, the body as it came; a
    // 404 with a message (no such card, the lightweight player has no card CPU) as {available: false, description}
    GsJson read_gs_report(const std::string &report, const std::map<std::string, std::string> &query) override
    {
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        QStringList parts;
        for (const auto &[key, value] : query)
            if (!value.empty())
                parts << QString::fromStdString(key) + "=" + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(value)));
        QString path = m_base + "/state/audio/gs/" + QString::fromStdString(report);
        if (!parts.isEmpty()) path += "?" + parts.join('&');
        QByteArray out;
        const int status = m_http.request("GET", path, QByteArray(), out);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        const QJsonObject body = QJsonDocument::fromJson(out).object();
        if (status == 404 && body.contains("message")) {
            const QJsonObject na{{"available", false}, {"description", body.value("message").toString()}};
            return {true, QJsonDocument(na).toJson(QJsonDocument::Compact).toStdString(), std::string()};
        }
        if (status != 200) return {false, std::string(), body.value("message").toString(QString("HTTP %1").arg(status)).toStdString()};
        return {true, out.toStdString(), std::string()};
    }
    GsJson gs_profiler(const std::string &card, const std::string &action) override
    {
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/control/audio/gs",
                                          QJsonObject{{"action", QString::fromStdString("profile_" + action)},
                                                      {"cpu", QString::fromStdString(card)}}, &status);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        if (status == kQueued) return {true, "{}", "sent"};
        const std::string json = QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString();
        if (status != 200) return {false, json, r.value("message").toString().toStdString()};
        return {true, json, r.value("text").toString().toStdString()};
    }
    GsJson gs_analysis(const std::string &card, const std::string &action, int decay) override
    {
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        QJsonObject body{{"action", QString::fromStdString(action)}, {"cpu", QString::fromStdString(card)}};
        if (decay >= 0) body.insert("decay", decay);
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/control/audio/gs/analysis", body, &status);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        if (status == kQueued) return {true, "{}", "sent"};
        const std::string json = QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString();
        if (status != 200) return {false, json, r.value("message").toString().toStdString()};
        return {true, json, r.value("text").toString().toStdString()};
    }

    // The send tab over the WebAPI (the GS slot's card only: the routes have no cpu=ms): the command set a GET of the
    // firmware objects, the send and its state POSTs of control/audio/gs that never wait (wait_ms 0)
    static constexpr const char * kNoMsSend = "The ZX-MultiSound's card has no send: the send addresses the GS slot's card (gs send)";
    GsJson gs_commands(const std::string &card) override
    {
        if (card == "ms") return {false, std::string(), kNoMsSend};
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        QByteArray out;
        const int status = m_http.request("GET", m_base + "/state/audio/gs/firmware?object=commands", QByteArray(), out);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        if (status != 200)
            return {false, std::string(),
                    QJsonDocument::fromJson(out).object().value("message").toString(QString("HTTP %1").arg(status)).toStdString()};
        return {true, out.toStdString(), std::string()};
    }
    static GsJson send_answer(int status, const QByteArray &out)
    {
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        const QJsonObject r = QJsonDocument::fromJson(out).object();
        const std::string json = QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString();
        if (status != 200) return {false, json, r.value("message").toString(QString("HTTP %1").arg(status)).toStdString()};
        return {true, json, r.value("text").toString().toStdString()};
    }
    GsJson gs_send(const std::string &card, int command, std::optional<int> pre, const std::vector<int> &params,
                   std::optional<int> replies) override
    {
        auto a = std::make_shared<GsJson>(GsJson{false, std::string(), "The send is under way"});
        gs_send_async(card, command, pre, params, replies, [a](const GsJson &r) { *a = r; });
        return *a;
    }
    void gs_send_async(const std::string &card, int command, std::optional<int> pre, const std::vector<int> &params,
                       std::optional<int> replies, const std::function<void(const GsJson &)> &done) override
    {
        if (card == "ms") return done({false, std::string(), kNoMsSend});
        if (!m_online.load()) return done({false, std::string(), "The emulator is not connected"});
        QJsonObject request{{"action", "send"}, {"value", command}, {"wait_ms", 0}};
        if (pre) request.insert("pre", *pre);
        if (replies) request.insert("replies", *replies);
        QJsonArray bytes;
        for (int p : params) bytes.append(p);
        request.insert("params", bytes);
        QByteArray out;
        m_http.send("POST", m_base + "/control/audio/gs", QJsonDocument(request).toJson(QJsonDocument::Compact), out, 5000,
                    [done](int status, const QByteArray &body) { done(send_answer(status, body)); });
    }
    // A read: the same request's answer comes from Net while the next is fetched (half a second old at most)
    GsJson gs_conversation(const std::string &card, uint32_t id) override
    {
        if (card == "ms") return {false, std::string(), kNoMsSend};
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        QByteArray out;
        const QByteArray body =
            QJsonDocument(QJsonObject{{"action", "conversation"}, {"send_id", (qint64)id}}).toJson(QJsonDocument::Compact);
        const int status = m_http.get(m_base + "/control/audio/gs", out, 500, "POST", body);
        if (status == kPending) {
            GsJson wait{false, std::string(), "Waiting for the emulator"};
            wait.busy = true;
            return wait;
        }
        return send_answer(status, out);
    }

    // The twin timeline over the WebAPI: GET debug/timeline (no session: a read never blocks another client), the
    // lease a timed one that runs out when this process stops renewing it (it ends with a crash too); POST
    // debug/timeline/arm renews or releases it. A 404 (no card CPU there) as {available: false, message}
    static constexpr unsigned int kTimelineLeaseMs = 15000;
    std::string timeline_client() const override
    {
        return "ecat-" + std::to_string(QCoreApplication::applicationPid());
    }
    GsJson read_timeline(const std::string &card, double span_t, unsigned int recent, unsigned int density, bool arm) override
    {
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        QStringList parts;
        if (!card.empty()) parts << "cpu=" + QString::fromStdString(card);
        parts << "span=" + QString::number(span_t, 'f', 1) << QString("recent=%1").arg(recent) << QString("density=%1").arg(density)
              << "client=" + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(timeline_client())))
              << QString("lease_ms=%1").arg(kTimelineLeaseMs) << QString("arm=%1").arg(arm ? "true" : "false")
              << "wait_ms=0";  // a 503 at once rather than the server's wait under this blocking request
        QByteArray out;
        const int status = m_http.get(m_base + "/debug/timeline?" + parts.join('&'), out, 500);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        if (status == kPending) {
            // Not read yet: busy, the strip asks again
            GsJson wait{false, std::string(), "Waiting for the emulator"};
            wait.busy = true;
            return wait;
        }
        const QJsonObject body = QJsonDocument::fromJson(out).object();
        if (status == 404) {
            const QJsonObject na{{"available", false}, {"message", body.value("message").toString(body.value("error").toString())}};
            return {true, QJsonDocument(na).toJson(QJsonDocument::Compact).toStdString(), std::string()};
        }
        if (status != 200) {
            GsJson failed{false, std::string(),
                          body.value("message").toString(body.value("error").toString(QString("HTTP %1").arg(status))).toStdString()};
            failed.busy = status == 503;
            return failed;
        }
        return {true, out.toStdString(), std::string()};
    }
    GsJson timeline_arm(bool on) override
    {
        if (!m_online.load()) return {false, std::string(), "The emulator is not connected"};
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/debug/timeline/arm",
                                          QJsonObject{{"on", on}, {"client", QString::fromStdString(timeline_client())},
                                                      {"lease_ms", (int)kTimelineLeaseMs}}, &status);
        if (status == 0) return {false, std::string(), "No answer from the emulator"};
        if (status == kQueued) return {true, "{}", std::string()};
        const std::string json = QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString();
        if (status != 200) return {false, json, r.value("message").toString(r.value("error").toString()).toStdString()};
        return {true, json, std::string()};
    }

    // Time travel over the WebAPI: the reads are GETs, the branch commands the branch routes, every other verb
    // POST /ttd/<verb> with its options as the JSON body. ok: a 2xx answer
    bool ttd_supported() const override { return true; }
    // Asynchronous mode: a read answers from Net at once, a command says it is under way (ttd_call_async has its
    // answer)
    TtdAnswer ttd_call(const std::string &verb, const std::map<std::string, std::string> &options) override
    {
        // Shared: an answer that comes later has its own place to go
        auto a = std::make_shared<TtdAnswer>(TtdAnswer{true, R"({"queued":true,"message":"Under way"})"});
        ttd_call_async(verb, options, [a](const TtdAnswer &r) { *a = r; });
        return *a;
    }
    void ttd_call_async(const std::string &verb, const std::map<std::string, std::string> &options,
                        const std::function<void(const TtdAnswer &)> &done) override
    {
        if (!m_online.load()) return done({false, R"({"error":"Service Unavailable","message":"The emulator is not connected"})"});
        std::map<std::string, std::string> rest = options;
        auto take = [&rest](const char * key) {
            const auto it = rest.find(key);
            if (it == rest.end()) return QString();
            const QString value = QString::fromStdString(it->second);
            rest.erase(it);
            return value;
        };
        // A branch name may hold spaces or '+' ("resume 1", "+00:00:25.150"): one path segment, percent-encoded
        auto segment = [](const QString &value) { return QString::fromUtf8(QUrl::toPercentEncoding(value)); };
        const char * method = "POST";
        QString path = m_base + "/ttd/";
        if (verb == "status" || verb == "position" || verb == "branches") {
            method = "GET";
            path += QString::fromStdString(verb);
        } else if (verb == "branch-info") {
            method = "GET";
            path += "branches/" + segment(take("branch"));
        } else if (verb == "branch-create") {
            path += "branches";
        } else if (verb == "branch-prune") {
            path += "branches/prune";
        } else if (verb == "branch-switch") {
            path += "branches/" + segment(take("branch")) + "/switch";
        } else if (verb == "branch-update") {
            method = "PATCH";
            path += "branches/" + segment(take("branch"));
        } else if (verb == "branch-delete") {
            method = "DELETE";
            path += "branches/" + segment(take("branch"));
        } else {
            path += QString::fromStdString(verb);
        }
        QJsonObject body;
        QStringList query;
        const bool in_query = strcmp(method, "GET") == 0 || strcmp(method, "DELETE") == 0;
        for (const auto &[key, value] : rest) {
            if (in_query)
                query << QString::fromStdString(key) + "=" +
                             QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(value)));
            else
                body.insert(QString::fromStdString(key), QString::fromStdString(value));
        }
        if (!query.isEmpty()) path += "?" + query.join('&');
        auto answer = [](int status, const QByteArray &out) -> TtdAnswer {
            if (status == 0) return {false, R"({"error":"Service Unavailable","message":"The emulator did not answer"})"};
            if (status == kPending) return {false, kPendingBody};
            const QJsonObject o = QJsonDocument::fromJson(out).object();
            return {status >= 200 && status < 300, QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString()};
        };
        QByteArray out;
        if (strcmp(method, "GET") == 0) {
            const int status = m_http.get(path, out);
            return done(answer(status, out));
        }
        // A move in time replays up to a frame and a reverse-continue searches the history: as long as a step over
        const QByteArray in = in_query && body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
        m_http.send(method, path, in, out, 15000, [done, answer](int status, const QByteArray &data) { done(answer(status, data)); });
        run_command();
    }
    std::string debug_offline_text() const override
    {
        return QString("Waiting for %1 at %2:%3")
            .arg(m_want.isEmpty() ? (m_model.isEmpty() ? QString("a machine") : m_model) : m_want)
            .arg(m_host)
            .arg(m_port)
            .toStdString();
    }

    // The snapshot, asked for again when stale (the views' thread never waits for it: the arrival takes it, in
    // synchronous mode it is here at the return): false while there is none
    bool fresh()
    {
        adopt();
        const bool running = m_snap.state == "running";
        // Once per cause: an event of the machine (the waiter's seq), a tick of a running machine; our own commands
        // make the snapshot taken before them stale by themselves (Net)
        const uint64_t seq = m_seq.load();
        bool again = false;
        // None yet (the emulator did not answer): asked again a second later
        if (!m_snap.valid && (!m_snap_retry.isValid() || m_snap_retry.elapsed() > 1000)) {
            m_snap_retry.start();
            again = m_snap_version != 0 || m_http.fresh(snapshot_path(), -1);
        }
        if (m_dirty.exchange(false)) again = true;
        if (m_snap.valid && m_snap.seq != seq && m_snap_asked_seq != seq) {
            m_snap_asked_seq = seq;
            again = true;
        }
        if (running && m_age.elapsed() >= 150 && m_snap_asked != m_snap_count) {
            m_snap_asked = m_snap_count;
            again = true;
        }
        if (again) m_http.invalidate(snapshot_path());
        QByteArray out;
        uint64_t version = 0;
        const int status = m_http.get(snapshot_path(), out, -1, "GET", QByteArray(), &version);
        if (status == 200 && version != m_snap_version) {
            m_snap_version = version;
            install(out);
        }
        return m_snap.valid;
    }
    Snapshot &snap()
    {
        fresh();
        return m_snap;
    }
    // A change of ours: the next read takes a new snapshot (a command makes it stale in Net already)
    void changed() { m_dirty = true; }
    QString snapshot_path() const { return m_base + kSnapshotRoute; }
    void install(const QByteArray &out)
    {
        const QJsonObject s = QJsonDocument::fromJson(out).object();
        m_snap.valid = true;
        m_snap.generation++;
        m_snap_count++;
        m_snap.seq = (uint64_t)s.value("seq").toDouble();
        m_snap.frame = (uint64_t)s.value("time").toObject().value("frame").toDouble();
        m_snap.state = s.value("state").toString();
        m_snap.reason = s.value("pause").toObject().value("reason").toString();
        m_snap.stop_cpu = s.value("pause").toObject().value("cpu").toString();
        m_snap.stop_serial = (uint64_t)s.value("pause").toObject().value("serial").toDouble();
        m_snap.stop_main = s.value("pause").toObject().value("main").toObject().value("text").toString();
        m_snap.regs = s.value("regs").toObject();
        const QJsonArray mem = s.value("memory").toArray();
        m_snap.memory = mem.isEmpty() ? QByteArray(0x10000, '\xFF')
                                      : QByteArray::fromBase64(mem[0].toObject().value("base64").toString().toLatin1());
        if (m_snap.memory.size() < 0x10000) m_snap.memory.resize(0x10000);
        const QJsonArray banks = s.value("pages").toArray();
        for (int i = 0; i < 4; i++) {
            const QJsonObject b = banks.at(i).toObject();
            m_snap.banks[i].kind = b.value("kind").toString();
            m_snap.banks[i].page = (unsigned int)b.value("page").toInt();
            m_snap.banks[i].writable = b.value("writable").toBool();
        }
        m_age.restart();
        // Every read made for the last snapshot is stale now
        m_http.set_generation(m_snap_count);
    }
    // What came from Net (the views' thread): the snapshot taken, the reads' generation moved (the views' caches of
    // a generation read Net again), the window told
    void arrived()
    {
        fresh();
        m_snap.generation++;
        m_data_serial++;
        if (m_run_pending && m_http.commands_done() >= m_run_mark && m_http.fresh(snapshot_path(), -1)) m_run_pending = false;
        update_serial();
        if (!m_listener_off) notify();
    }
    // The window's serial: one more when the stop, the state or the stepping it shows has changed
    void update_serial()
    {
        const QString tuple = QString("%1 %2 %3 %4").arg(m_snap.seq).arg(m_snap.state).arg(m_snap.stop_serial).arg(stepping());
        if (tuple != m_serial_tuple) {
            m_serial_tuple = tuple;
            m_serial_out++;
        }
    }
    bool stepping() const
    {
        if (m_sync) return false;
        int read_ms = 0, command_ms = 0;
        QString what;
        m_http.waiting(read_ms, command_ms, what);
        return m_run_pending || read_ms > 1000;
    }
    // A command the emulator refused or did not answer: said after the state for a few seconds
    void notice(const QString &why)
    {
        m_notice = why;
        m_notice_age.restart();
        m_data_serial++;
    }
    // A run control command queued: the window shows it stepping until a snapshot taken after it has come
    void run_command()
    {
        m_run_pending = !m_sync;
        m_run_mark = m_http.commands_queued();
        changed();
        update_serial();
    }

    // A suitable emulator the waiter found: this machine is its from now (the views' thread; the waiter never
    // touches m_base / m_http). Its debug mode and history go on as commands
    void adopt()
    {
        QString id;
        {
            std::lock_guard<std::mutex> lock(m_wait_mutex);
            if (m_pending_id.isEmpty()) return;
            id = m_pending_id;
            m_pending_id.clear();
            m_base = m_wait_base = "/api/v1/emulator/" + id;
        }
        m_id = id;
        m_run_to_armed = false;
        breakpoints_changed();
        m_catalog_age.invalidate();
        m_histories_age.invalidate();
        m_dirty = true;
        m_online = true;
        if (m_attached) {
            const QString base = m_base;
            m_histories_ours.clear();
            m_http.task([this, base](Http &http) {
                m_debug_before = http.json("GET", base + "/debugmode").value("enabled").toBool();
                http.json("PUT", base + "/debugmode", QJsonObject{{"enabled", true}});
            });
            start_cpu_history();
        }
    }

    // The execution history `cpu` on while the debugger is (a ring), unless the user has it some way already: read
    // as a snapshot, the machine runs on
    void start_cpu_history()
    {
        // The read and the switch in one task: the answer of the first decides the second
        const QString base = m_base;
        auto turned = std::make_shared<bool>(false);
        m_http.task(
            [base, turned](Http &http) {
                for (const QJsonValue &v : http.json("GET", base + "/history").value("histories").toArray()) {
                    const QJsonObject h = v.toObject();
                    if (h.value("device").toString() != "cpu" || h.value("mode").toString() != "off") continue;
                    int status = 0;
                    http.json("POST", base + "/history/cpu/config", QJsonObject{{"mode", "ring"}}, &status);
                    *turned = status == 200;
                }
            },
            [this, turned]() {
                if (*turned) m_histories_ours.insert("cpu");
                m_histories_age.invalidate();
            });
    }
    // A history on (a ring) or off; one the debugger turned on it turns off when it goes, one it found on it leaves
    void set_history(const QString &device, bool on)
    {
        QByteArray out;
        m_http.send("POST", m_base + "/history/" + device + "/config",
                    QJsonDocument(QJsonObject{{"mode", on ? "ring" : "off"}}).toJson(QJsonDocument::Compact), out, 5000,
                    [this, device, on](int status, const QByteArray &body) {
                        if (status != 200) {
                            notice(failure_text("history " + device, status, body));
                            return;
                        }
                        if (on) m_histories_ours.insert(device);
                        else m_histories_ours.erase(device);
                        m_histories_age.invalidate();
                    });
    }
    static QString failure_text(const QString &what, int status, const QByteArray &body)
    {
        if (status == 0) return what + ": no answer from the emulator";
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        return what + ": " + o.value("message").toString(o.value("error").toString(QString("HTTP %1").arg(status)));
    }

    // The execution breakpoints of a CPU: main, gs, ms (each card CPU has breakpoints of its own, ?cpu=)
    std::vector<Breakpoint> breakpoints(const QString &cpu = "main")
    {
        fresh();
        BreakpointList &l = m_bps[cpu];
        if (l.generation != m_snap.generation || l.dirty) {
            l.generation = m_snap.generation;
            l.dirty = false;
            l.list.clear();
            l.events.clear();
            const QJsonArray list = m_http.json("GET", m_base + "/breakpoints?cpu=" + cpu).value("breakpoints").toArray();
            for (const QJsonValue &v : list) {
                const QJsonObject b = v.toObject();
                if (b.value("type").toString() == "event") {
                    ecatshim::EventBreakpoint x;
                    x.id = (unsigned int)b.value("id").toInt();
                    x.kind = b.value("event").toString().toStdString();
                    x.value = (unsigned int)b.value("value").toInt();
                    x.mask = (unsigned int)b.value("value_mask").toInt();
                    x.enabled = b.value("active").toBool(true);
                    const QString mode = b.value("hit_mode").toString();
                    const QString target = QString::number(b.value("hit_target").toInteger());
                    x.hits = (mode == "equal"      ? target
                              : mode == "at_least" ? ">=" + target
                              : mode == "multiple" ? "%" + target
                                                   : QString()).toStdString();
                    x.stops = b.value("stop").toBool(true);
                    l.events.push_back(x);
                    continue;
                }
                if (b.value("type").toString() != "memory" || !b.value("execute").toBool() || b.contains("address_end"))
                    continue;
                l.list.push_back(Breakpoint{b.value("id").toInt(), (unsigned int)b.value("address").toInt(),
                                            b.value("group").toString() == kRunToGroup,
                                            b.value("condition").toString().toStdString()});
            }
        }
        return l.list;
    }
    void breakpoints_changed()
    {
        for (auto &l : m_bps) l.second.dirty = true;
    }
    // The event breakpoints of a CPU (main, gs, ms), read with its execution ones
    std::vector<ecatshim::EventBreakpoint> event_breakpoints(const QString &cpu)
    {
        breakpoints(cpu);
        return m_bps[cpu].events;
    }
    bool add_event_breakpoint(const QString &cpu, const std::string &kind, unsigned int value, unsigned int mask,
                              const std::string &hits, std::string &error)
    {
        if (value > 0xFF || mask > 0xFF) {
            error = "the value and the mask are bytes (0..255)";
            return false;
        }
        QJsonObject body{{"type", "event"}, {"event", QString::fromStdString(kind)}, {"cpu", cpu}};
        if (mask != 0) {
            body["value"] = (int)(value & mask);
            body["value_mask"] = (int)mask;
        }
        if (!hits.empty()) body["hits"] = QString::fromStdString(hits);
        int status = 0;
        const QJsonObject r = m_http.json("POST", m_base + "/breakpoints", body, &status);
        breakpoints_changed();
        if (status == 200 || status == 201 || status == kQueued) return true;
        error = r.value("message").toString().toStdString();
        if (error.empty()) error = "the emulator did not add it (HTTP " + std::to_string(status) + ")";
        return false;
    }
    bool reset_event_hits(const QString &cpu, unsigned int id)
    {
        int status = 0;
        m_http.json("POST", m_base + "/breakpoints/hits/reset?cpu=" + cpu, QJsonObject{{"id", (int)id}}, &status);
        breakpoints_changed();
        return status == 200 || status == kQueued;
    }
    bool remove_event_breakpoint(const QString &cpu, unsigned int id)
    {
        int status = 0;
        m_http.json("DELETE", m_base + "/breakpoints/" + QString::number(id) + "?cpu=" + cpu, QJsonObject(), &status);
        breakpoints_changed();
        return status == 200 || status == kQueued;
    }
    bool enable_event_breakpoint(const QString &cpu, unsigned int id, bool on)
    {
        int status = 0;
        m_http.json("PUT", m_base + "/breakpoints/" + QString::number(id) + (on ? "/enable" : "/disable") + "?cpu=" + cpu,
                    QJsonObject(), &status);
        breakpoints_changed();
        return status == 200 || status == kQueued;
    }
    RemoteCardZ80 * card(const QString &cpu) const
    {
        for (const auto &c : m_cards)
            if (c->cpu() == cpu) return c.get();
        return nullptr;
    }

    void debug_attach(bool on) override
    {
        if (!on) return;
        const bool first = !m_attached;
        m_attached = true;
        const QString base = m_base;
        m_http.task([this, base, first](Http &http) {
            if (first) m_debug_before = http.json("GET", base + "/debugmode").value("enabled").toBool();
            http.json("PUT", base + "/debugmode", QJsonObject{{"enabled", true}});
        });
        start_cpu_history();
        m_cpu->m_debug = ecatshim::DEBUG_BRAKES;
        changed();
    }
    void debug_detach() override
    {
        clear_run_to();
        m_http.json("PUT", m_base + "/debug/selected_cpu", QJsonObject{{"cpu", "main"}});
        if (debug_paused()) m_http.json("POST", m_base + "/resume");
        run_command();
    }
    bool debug_paused() const override
    {
        auto * self = const_cast<RemoteMachine *>(this);
        return self->snap().state == "paused";
    }
    bool debug_stepping() const override { return stepping(); }
    std::string debug_link_text() const override
    {
        if (m_sync || !m_online.load()) return std::string();
        int read_ms = 0, command_ms = 0;
        QString what;
        m_http.waiting(read_ms, command_ms, what);
        if (read_ms > 1000) return QString("Waiting for the emulator: %1 s").arg(read_ms / 1000).toStdString();
        if (command_ms > 2000)
            return QString("Waiting for the emulator: %1%2 s").arg(what.isEmpty() ? QString() : what + ", ").arg(command_ms / 1000).toStdString();
        if (!m_notice.isEmpty() && m_notice_age.elapsed() < 5000) return m_notice.toStdString();
        return std::string();
    }
    uint64_t debug_data_serial() const override { return m_data_serial; }
    void debug_pause() override
    {
        m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", true}});
        m_http.json("POST", m_base + "/pause");
        run_command();
    }
    void debug_run(bool breakpoints) override
    {
        if (!m_arming_run_to) drop_run_to();
        // Breakpoints fire only in debug mode: a run without them is a run with it off. Paused as the last snapshot
        // says (the window runs only a machine it shows held: no command of its own is under way)
        m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", breakpoints}});
        m_cpu->m_debug = breakpoints ? ecatshim::DEBUG_BRAKES : ecatshim::DEBUG_OFF;
        if (debug_paused()) m_http.json("POST", m_base + "/resume");
        run_command();
    }
    // A step of a card's tab is the card's step (?cpu=): the machine runs until it ends (up to 2 s, over / out 5 s)
    void debug_step(ecatshim::CPU * c, unsigned int count) override
    {
        drop_run_to();
        const QString q = step_query(c);
        if (count <= 1) m_http.json("POST", m_base + "/step" + q, QJsonObject(), nullptr, 15000);
        else m_http.json("POST", m_base + "/steps" + q, QJsonObject{{"count", (int)count}}, nullptr, 15000);
        run_command();
    }
    // Step over / out answer when the machine has stopped (a pool of the WebAPI runs them)
    void debug_step_over(ecatshim::CPU * c) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepover" + step_query(c), QJsonObject(), nullptr, 15000);
        run_command();
    }
    void debug_step_out(ecatshim::CPU * c) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepout" + step_query(c), QJsonObject(), nullptr, 15000);
        run_command();
    }
    // A card's run to a moment (POST /card_run_to {cpu, until, value}): it answers when the run has ended (a breakpoint
    // or 5 s at most). Asynchronous mode: the answer says it is under way, the snapshot shows the stop
    CardRunAnswer debug_card_run_to(ecatshim::CPU * c, const std::string &until, uint64_t value) override
    {
        auto a = std::make_shared<CardRunAnswer>();
        a->ok = true;
        a->message = "Card run to " + until + ": under way";
        debug_card_run_to_async(c, until, value, [a](const CardRunAnswer &r) { *a = r; });
        return *a;
    }
    void debug_card_run_to_async(ecatshim::CPU * c, const std::string &until, uint64_t value,
                                 const std::function<void(const CardRunAnswer &)> &done) override
    {
        auto * card = dynamic_cast<RemoteCardZ80 *>(c);
        if (card == nullptr) {
            CardRunAnswer a;
            a.message = "A card run takes a card's CPU (gs, ms)";
            done(a);
            return;
        }
        drop_run_to();
        QByteArray out;
        const QByteArray body = QJsonDocument(QJsonObject{{"cpu", card->cpu()}, {"until", QString::fromStdString(until)},
                                                          {"value", (qint64)value}}).toJson(QJsonDocument::Compact);
        m_http.send("POST", m_base + "/card_run_to", body, out, 15000, [done](int status, const QByteArray &data) {
            const QJsonObject r = QJsonDocument::fromJson(data).object();
            CardRunAnswer a;
            a.ok = status >= 200 && status < 300;
            a.message = r.value("message").toString(status == 0 ? QString("No answer from the emulator") : QString()).toStdString();
            a.hit = r.value("hit").toBool();
            a.stop_reason = r.value("stop").toObject().value("reason").toString().toStdString();
            a.card_cycle = r.contains("card_cycle") ? r.value("card_cycle").toInteger() : -1;
            a.main_t = r.contains("main_t") ? r.value("main_t").toInteger() : -1;
            a.frame = r.contains("frame") ? r.value("frame").toInteger() : -1;
            done(a);
        });
        run_command();
    }
    // The tab's CPU shown (GET / PUT /debug/selected_cpu): a card's follows the main CPU's steps of the paused machine
    void debug_select(ecatshim::CPU * c) override
    {
        auto * card = dynamic_cast<RemoteCardZ80 *>(c);
        m_http.json("PUT", m_base + "/debug/selected_cpu", QJsonObject{{"cpu", card ? card->cpu() : QString("main")}});
    }
    void debug_run_to(ecatshim::CPU * c, unsigned int address) override
    {
        // A breakpoint of its own group (of the CPU of the tab) that goes away at the next stop. The WebAPI keeps one
        // per address: on a user's breakpoint a run-to would move it into its group, and the user's stops there all
        // the same
        auto * card = dynamic_cast<RemoteCardZ80 *>(c);
        const QString cpu = card ? card->cpu() : QString("main");
        bool user = false;
        for (const Breakpoint &b : breakpoints(cpu)) user = user || (!b.run_to && b.address == (address & 0xFFFF));
        // Gone at a stop after this run starts (the run start and the stop each move the seq), not at the stop it
        // was set at, whose answer may come only now. A task on the commands' lane: the lock is the worker's to wait
        // for, never the views' thread's
        const QString base = m_base;
        m_http.task([this, base, cpu, user, address](Http &http) {
            // The waiter's removal of an earlier run-to (by group) never lands after this one is set
            std::lock_guard<std::mutex> lock(m_run_to_mutex);
            if (m_run_to_armed.exchange(false)) remove_run_to(http, base);
            if (user) return;
            http.json("POST", base + "/breakpoints",
                      QJsonObject{{"type", "execution"}, {"address", (int)(address & 0xFFFF)}, {"group", kRunToGroup},
                                  {"note", "run to the cursor"}, {"cpu", cpu}});
            // The emulator's seq now, not the waiter's: the waiter lags behind the edits just made (each poke and
            // register write moves the seq), and its answer for them, still paused, would count as the stop of
            // this run and take the breakpoint away before the run starts. A running machine has no run start
            // to come: its stop alone moves the seq past the mark
            int status = 0;
            const QJsonObject now = http.json("GET", base + "/debug/wait?timeout_ms=0", QJsonObject(), &status);
            const uint64_t seq = status == 200 ? (uint64_t)now.value("seq").toDouble() : m_seq.load();
            m_run_to_seq = (status == 200 && now.value("state").toString() != "paused" && seq > 0) ? seq - 1 : seq;
            m_run_to_armed = true;
        });
        breakpoints_changed();
        m_arming_run_to = true;
        debug_run(true);
        m_arming_run_to = false;
    }
    unsigned int debug_serial() const override
    {
        auto * self = const_cast<RemoteMachine *>(this);
        self->fresh();
        self->update_serial();
        return (unsigned int)m_serial_out;
    }
    ecatshim::CPU * debug_stop_cpu() const override
    {
        // A card's breakpoint or step: its tab
        RemoteCardZ80 * c = card(const_cast<RemoteMachine *>(this)->snap().stop_cpu);
        return c ? static_cast<ecatshim::CPU *>(c) : m_cpu.get();
    }
    std::string debug_stop_main_text() const override
    {
        return const_cast<RemoteMachine *>(this)->snap().stop_main.toStdString();
    }
    uint64_t debug_stop_serial() const override { return const_cast<RemoteMachine *>(this)->snap().stop_serial; }
    int debug_stop_reason() const override
    {
        const QString r = const_cast<RemoteMachine *>(this)->snap().reason;
        if (r == "pause") return DSTOP_PAUSE;
        if (r == "breakpoint") return DSTOP_BREAKPOINT;
        if (r == "step") return DSTOP_STEP;
        return DSTOP_NONE;
    }
    void set_debug_listener(const std::function<void()> &fn) override
    {
        std::lock_guard<std::mutex> lock(m_listener_mutex);
        m_listener = fn;
    }

    std::string read_setup(const std::string &section, const std::string &key, const std::string &def) override
    {
        return m_settings.value(QString::fromStdString(section + "/" + key), QString::fromStdString(def)).toString().toStdString();
    }
    void write_setup(const std::string &section, const std::string &key, const std::string &value) override
    {
        m_settings.setValue(QString::fromStdString(section + "/" + key), QString::fromStdString(value));
    }
    std::string get_last_path() override { return read_setup("Paths", "last", QDir::homePath().toStdString()); }
    void set_last_path(const std::string &path) override { write_setup("Paths", "last", path); }
    std::string translate_char(unsigned int code) override
    {
        if (code == 0x7F) return "\xC2\xA9";
        if (code >= 0x20 && code < 0x7F) return std::string(1, (char)code);
        return ".";
    }
    int char_code(const std::string &text) override
    {
        if (text == "\xC2\xA9") return 0x7F;
        return (text.size() == 1 && (unsigned char)text[0] < 0x80) ? (unsigned char)text[0] : -1;
    }

    uint64_t frames() { return snap().frame; }
    void traffic(uint64_t &requests, uint64_t &bytes)
    {
        requests = m_http.requests() + m_wait_requests.load();
        bytes = m_http.bytes() + m_wait_bytes.load();
    }

private:
    Net m_http;
    QString m_host;
    uint16_t m_port;
    QString m_want;    // the emulator asked for (an id or symbolic id); empty: one of the same model
    QString m_model;   // the model it runs
    QString m_base;
    QJsonObject m_catalog;
    QElapsedTimer m_catalog_age;
    QJsonObject m_histories;
    QElapsedTimer m_histories_age;
    std::mutex m_catalog_mutex;
    // The analysis tabs' symbols (GUI thread only): a card CPU's by address, and the labels resolved at PCs
    struct CardLabels {
        std::map<unsigned int, std::string> by_address;
        std::string profile;
        QElapsedTimer age;
    };
    std::map<std::string, CardLabels> m_card_labels;
    std::map<std::pair<std::string, unsigned int>, std::string> m_resolved;
    QElapsedTimer m_resolved_age;
    std::vector<GsProfileInfo> m_profiles;
    static constexpr int kTraceEvents = 3000;
    QString m_id;
    QSettings m_settings;
    ecatshim::DeviceManager m_dm;
    std::shared_ptr<RemoteZ80> m_cpu;
    std::shared_ptr<RemoteMapper> m_mapper;
    struct CardReport {
        uint64_t generation = ~0ull;
        QJsonObject report;
    };
    std::map<QString, CardReport> m_card_reports;

    Snapshot m_snap;
    QElapsedTimer m_age;
    bool m_sync = false;            // the self-check: every request waited for
    uint64_t m_snap_count = 0;      // snapshots taken (Net's read generation)
    uint64_t m_snap_asked = ~0ull;  // the snapshot after which a running machine's next was asked for
    uint64_t m_snap_asked_seq = ~0ull;
    uint64_t m_snap_version = 0;    // Net's version of the snapshot installed
    QElapsedTimer m_snap_retry;
    bool m_run_pending = false;     // a run control command whose snapshot has not come yet
    uint64_t m_run_mark = 0;
    uint64_t m_serial_out = 0;
    QString m_serial_tuple;
    uint64_t m_data_serial = 0;
    QString m_notice;
    QElapsedTimer m_notice_age;
    std::atomic<bool> m_listener_off{false};
    std::atomic<bool> m_dirty{true};
    std::atomic<uint64_t> m_seq{0};
    std::atomic<uint64_t> m_run_to_seq{0};
    std::atomic<bool> m_run_to_armed{false};
    std::mutex m_run_to_mutex;  // the run-to's arming and removal (views' thread and waiter)
    bool m_arming_run_to = false;
    std::vector<std::shared_ptr<RemoteCardZ80>> m_cards;
    struct BreakpointList {
        std::vector<Breakpoint> list;
        std::vector<ecatshim::EventBreakpoint> events;
        uint64_t generation = 0;
        bool dirty = true;
    };
    std::map<QString, BreakpointList> m_bps;
    bool m_attached = false;
    std::set<QString> m_histories_ours;  // the histories the debugger turned on: off again when it goes
    std::atomic<bool> m_debug_before{false};

    QThread * m_waiter = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_online{true};  // the gate of m_http too
    std::mutex m_wait_mutex;           // the waiter's view of the instance and a found one
    QString m_wait_base;
    QString m_pending_id;
    std::atomic<uint64_t> m_wait_requests{0};
    std::atomic<uint64_t> m_wait_bytes{0};
    std::mutex m_listener_mutex;
    std::function<void()> m_listener;

    // A run-to of an earlier command is over whatever happened since: a new command never meets it (on a CPU in
    // HALT at its address it would stop the run at once)
    void drop_run_to()
    {
        // Looked at on the worker: a run-to queued before this is armed by then
        const QString base = m_base;
        m_http.task([this, base](Http &http) {
            std::lock_guard<std::mutex> lock(m_run_to_mutex);
            if (m_run_to_armed.exchange(false)) remove_run_to(http, base);
        });
        breakpoints_changed();
    }
    void clear_run_to()
    {
        const QString base = m_base;
        m_http.task([this, base](Http &http) { remove_run_to(http, base); });
        breakpoints_changed();
    }
    // The run-to breakpoints of every CPU (the cards are fixed after build: the waiter reads them too)
    void remove_run_to(Http &http, const QString &base)
    {
        http.json("POST", base + "/breakpoints/remove", QJsonObject{{"select", QString("group:") + kRunToGroup}});
        for (const auto &c : m_cards)
            http.json("POST", base + "/breakpoints/remove",
                      QJsonObject{{"select", QString("group:") + kRunToGroup}, {"cpu", c->cpu()}});
    }
    QString step_query(ecatshim::CPU * c) const
    {
        auto * card = dynamic_cast<RemoteCardZ80 *>(c);
        return card ? "?cpu=" + card->cpu() : QString();
    }

    // The long poll: each answer with a new seq (a stop, a run start, an edit) is an event for the window. At a stop
    // the run-to breakpoint goes, whatever stopped the machine
    void wait_loop()
    {
        Http wait(m_host, m_port);
        wait.set_abort(&m_stop);  // the debugger closing: no wait for a stalled emulator's answer
        while (!m_stop) {
            QString base;
            {
                std::lock_guard<std::mutex> lock(m_wait_mutex);
                if (!m_pending_id.isEmpty()) base.clear();  // found: the views' thread takes it over (adopt)
                else base = m_wait_base;
            }
            if (base.isEmpty() || !m_online) {
                if (base.isEmpty() || !look(wait)) pause_a_second();
                continue;
            }
            QByteArray out;
            const QString path = base + "/debug/wait?since=" + QString::number(m_seq.load()) + "&timeout_ms=500";
            const int status = wait.request("GET", path, QByteArray(), out, 3000);
            m_wait_requests = wait.requests();
            m_wait_bytes = wait.bytes();
            if ((status == 0 || status == 404) && !listed(wait, base)) {
                // The emulator closed or the server stopped: a normal course, the window shows it and waits
                m_online = false;
                notify();
                continue;
            }
            if (status != 200) {
                QThread::msleep(200);
                continue;
            }
            const QJsonObject a = QJsonDocument::fromJson(out).object();
            if (!a.value("changed").toBool()) continue;
            m_seq = (uint64_t)a.value("seq").toDouble();
            m_dirty = true;
            if (a.value("state").toString() == "paused" && m_seq.load() >= m_run_to_seq.load() + 2) {
                std::lock_guard<std::mutex> lock(m_run_to_mutex);
                if (m_seq.load() >= m_run_to_seq.load() + 2 && m_run_to_armed.exchange(false)) remove_run_to(wait, base);
            }
            notify();
        }
    }

    void notify()
    {
        std::lock_guard<std::mutex> lock(m_listener_mutex);
        if (m_listener) m_listener();
    }
    void pause_a_second()
    {
        for (int i = 0; i < 10 && !m_stop; i++) QThread::msleep(100);
    }
    // The instance at `base` is still listed (false also when the server does not answer)
    bool listed(Http &wait, const QString &base)
    {
        int status = 0;
        const QJsonArray list = wait.json("GET", "/api/v1/emulator", QJsonObject(), &status).value("emulators").toArray();
        if (status != 200) return false;
        for (const QJsonValue &v : list)
            if ("/api/v1/emulator/" + v.toObject().value("id").toString() == base) return true;
        return false;
    }
    // Offline: a suitable emulator (the one asked for by id, else one of the same model: the views' devices fit it);
    // the views' thread takes it over (adopt)
    bool look(Http &wait)
    {
        int status = 0;
        const QJsonArray list = wait.json("GET", "/api/v1/emulator", QJsonObject(), &status).value("emulators").toArray();
        if (status != 200) return false;
        for (const QJsonValue &v : list) {
            const QJsonObject e = v.toObject();
            const bool suits = m_want.isEmpty() ? e.value("model").toString() == m_model
                                                : e.value("id").toString() == m_want || e.value("symbolic_id").toString() == m_want;
            if (!suits) continue;
            {
                std::lock_guard<std::mutex> lock(m_wait_mutex);
                m_pending_id = e.value("id").toString();
            }
            m_seq = 0;
            notify();
            return true;
        }
        return false;
    }
};

//------------------------------------------------------------------- the parts, out of line

unsigned int RemotePages::get_direct(unsigned int a)
{
    const unsigned int page = a / kPage;
    const uint64_t gen = m_m->snap().generation;
    auto &c = m_cache[page];
    if (c.first != gen || c.second.size() != (int)kPage) {
        QByteArray out;
        // Not read yet (or not given): what was read before stays, else #FF
        if (m_m->http().request("GET", m_m->base() + "/memory/page/" + m_kind + "/" + QString::number(page) +
                                    "?format=binary&length=" + QString::number(kPage),
                                QByteArray(), out) != 200)
            out = c.second.size() == (int)kPage ? c.second : QByteArray();
        out.resize(kPage, '\xFF');
        c = {gen, out};
    }
    return (uint8_t)c.second[a % kPage];
}

void RemotePages::rom_titles()
{
    const uint64_t seq = m_m->snap().seq;
    if (seq == m_titles_seq) return;
    int status = 0;
    const QJsonArray list = m_m->http().json("GET", m_m->base() + "/state/memory/rom", QJsonObject(), &status).value("pages").toArray();
    // Not read yet: the titles there were stay, asked again at the next look
    if (status != 200) return;
    m_titles_seq = seq;
    m_titles.assign(m_pages, {});
    for (const QJsonValue &v : list) {
        const QJsonObject p = v.toObject();
        const unsigned int page = (unsigned int)p.value("page").toInt();
        if (page >= m_pages) continue;
        std::string title = p.value("title").toString().toStdString();
        std::string role = p.value("description").toString().toStdString();
        title = ZxRomPageNote(title);
        if (role.rfind("ROM Page", 0) == 0) role.clear();
        m_titles[page] = {title, role};
    }
}

std::string RemotePages::page_title(unsigned int page)
{
    if (m_kind == "ram") return ZxRamNote(m_paging, page);
    rom_titles();
    return page < m_titles.size() ? m_titles[page].first : std::string();
}

std::string RemotePages::page_role(unsigned int page)
{
    if (m_kind == "ram") return std::string();
    rom_titles();
    return page < m_titles.size() ? m_titles[page].second : std::string();
}

void RemotePages::set_value(unsigned int a, unsigned int v, bool)
{
    m_m->http().json("PUT", m_m->base() + "/memory/" + m_kind + "/" + QString::number(a / kPage) + "/" + QString::number(a % kPage),
                     QJsonObject{{"data", QJsonArray{(int)(v & 0xFF)}}});
    m_m->changed();
}

template <class Base>
unsigned int RemoteRegion<Base>::get_direct(unsigned int a)
{
    if (a >= m_size) return 0xFF;
    const unsigned int chunk = a / kChunk;
    const uint64_t gen = m_m->snap().generation;
    auto &c = m_cache[chunk];
    if (c.first != gen || c.second.size() != (int)kChunk) {
        QByteArray out;
        const QString path = m_m->base() + "/memory/region/" + QUrl::toPercentEncoding(m_region) + "?offset=" +
                             QString::number(chunk * kChunk) + "&length=" + QString::number(kChunk) + "&format=binary";
        if (m_m->http().request("GET", path, QByteArray(), out) != 200)
            out = c.second.size() == (int)kChunk ? c.second : QByteArray();
        out.resize(kChunk, '\xFF');
        c = {gen, out};
    }
    return (uint8_t)c.second[a % kChunk];
}

template <class Base>
void RemoteRegion<Base>::set_value(unsigned int a, unsigned int v, bool)
{
    m_m->http().json("POST", m_m->base() + "/memory/region/" + QUrl::toPercentEncoding(m_region),
                     QJsonObject{{"offset", (int)a}, {"hex", hex_body(v)}});
    m_m->changed();
}

unsigned int RemoteMapper::get_direct(unsigned int a)
{
    return (uint8_t)m_m->snap().memory[a & 0xFFFF];
}

void RemoteMapper::set_value(unsigned int a, unsigned int v, bool)
{
    m_m->http().json("PUT", m_m->base() + "/memory/" + QString::number(a & 0xFFFF), QJsonObject{{"data", QJsonArray{(int)(v & 0xFF)}}});
    m_m->changed();
}

ecatshim::AddressableDevice * RemoteMapper::peek_owner(unsigned int address, unsigned int &offset)
{
    const Snapshot::Bank &b = m_m->snap().banks[(address >> 14) & 3];
    offset = b.page * kPage + (address & (kPage - 1));
    if (b.kind == "ram") return m_ram;
    if (b.kind == "rom") return m_rom;
    return nullptr;
}

QJsonObject RemoteReport::read()
{
    int status = 0;
    const QJsonObject o = m_m->http().json("GET", m_m->base() + "/" + m_route, QJsonObject(), &status);
    if (status == 200) m_last = o;
    return m_last;
}

std::vector<DeviceFieldInfo> RemoteReport::get_device_fields()
{
    m_values.clear();
    flatten(read(), "");
    std::vector<DeviceFieldInfo> r;
    for (const auto &v : m_values) r.push_back(DeviceFieldInfo{v.first, false});
    return r;
}

bool RemoteReport::get_tree(bool deep, ecatshim::DeviceNode &out)
{
    out = ecatshim::DeviceNode{m_title, std::string(), {}};
    for (const QJsonValue &v : m_m->catalog().value("reports").toArray())
        if (v.toObject().value("id").toString().toStdString() == name)
            out.value = v.toObject().value("summary").toString().toStdString();
    if (!deep) return true;
    ecatshim::DeviceNode parts;
    to_tree(read(), parts);
    for (ecatshim::DeviceNode &c : parts.children)
        if (c.name != "available") out.children.push_back(std::move(c));
    return true;
}

ecatshim::History::Summary RemoteHistory::summary()
{
    Summary out;
    out.what = m_what;
    for (const QJsonValue &v : m_m->histories().value("histories").toArray()) {
        const QJsonObject h = v.toObject();
        if (h.value("device").toString().toStdString() != m_device) continue;
        out.recording = h.value("mode").toString() != "off";
        out.kept = (uint64_t)h.value("kept").toDouble();
        out.written = (uint64_t)h.value("written").toDouble();
    }
    return out;
}

std::vector<std::string> RemoteHistory::records(unsigned int count)
{
    // Not given (the emulator gone, an error): the last records read stay
    int status = 0;
    const QJsonObject read = m_m->http().json(
        "GET", m_m->base() + "/history/" + QString::fromStdString(m_device) + "?last=" + QString::number(count), QJsonObject(), &status);
    if (status != 200) return m_last;
    m_last.clear();
    for (const QJsonValue &r : read.value("records").toArray()) m_last.push_back(line_text(r));
    return m_last;
}

void RemoteHistory::set_recording(bool on)
{
    m_m->set_history(QString::fromStdString(m_device), on);
}

unsigned int RemoteZ80::get_pc()
{
    return num(m_m->snap().regs, "special", "pc");
}

unsigned int RemoteZ80::peek_mem(unsigned int a)
{
    return (uint8_t)m_m->snap().memory[a & 0xFFFF];
}

bool RemoteZ80::poke_mem(unsigned int a, unsigned int v)
{
    int status = 0;
    m_m->http().json("PUT", m_m->base() + "/memory/" + QString::number(a & 0xFFFF),
                     QJsonObject{{"data", QJsonArray{(int)(v & 0xFF)}}}, &status);
    m_m->changed();
    return status == 200 || status == kQueued;
}

std::vector<ecatshim::AddressWindow> RemoteZ80::get_windows()
{
    std::vector<ecatshim::AddressWindow> r;
    const Snapshot &s = m_m->snap();
    for (unsigned int w = 0; w < 4; w++) {
        const Snapshot::Bank &b = s.banks[w];
        ecatshim::AddressWindow x;
        x.start = w * kPage;
        x.size = kPage;
        x.page = b.page;
        x.offset = b.page * kPage;
        x.writable = b.writable;
        x.switchable = m_paging.switchable[w];
        if (b.kind == "rom") {
            x.kind = "ROM";
            x.device = m_rom;
            if (auto * pages = dynamic_cast<ecatshim::Memory *>(m_rom)) {
                x.name = pages->page_title(b.page);
                x.role = pages->page_role(b.page);
            }
        } else if (b.kind == "ram") {
            x.kind = "RAM";
            x.device = m_ram;
            x.name = ZxRamNote(m_paging, b.page);
        } else if (!b.kind.isEmpty()) {
            x.kind = b.kind.toStdString();
        }
        r.push_back(x);
    }
    return r;
}

// The register list of a Z80 for the window, from a /registers answer (the main CPU's or a card's)
std::vector<CpuRegister> z80_register_list(const QJsonObject &r)
{
    auto reg = [&r](const char * n, const char * group, const char * key, unsigned int bits) {
        return CpuRegister{n, num(r, group, key), bits};
    };
    const CpuRegister gap;
    return {
        reg("AF", "main", "af", 16), reg("BC", "main", "bc", 16), reg("DE", "main", "de", 16), reg("HL", "main", "hl", 16), gap,
        reg("AF'", "alternate", "af_", 16), reg("BC'", "alternate", "bc_", 16), reg("DE'", "alternate", "de_", 16),
        reg("HL'", "alternate", "hl_", 16), gap,
        reg("IX", "index", "ix", 16), reg("IY", "index", "iy", 16), gap,
        reg("SP", "special", "sp", 16), reg("PC", "special", "pc", 16), gap,
        reg("I", "special", "i", 8), reg("R", "special", "r", 8), reg("IM", "interrupt", "im", 8),
        reg("IFF1", "interrupt", "iff1", 8), reg("IFF2", "interrupt", "iff2", 8),
    };
}

std::vector<std::pair<std::string, std::string>> z80_flag_list(unsigned int f)
{
    std::vector<std::pair<std::string, std::string>> r;
    for (int i = 0; i < 8; i++) r.emplace_back(kFlagNames[i], ((f >> (7 - i)) & 1) ? "1" : "0");
    return r;
}

// The F bit of a flag name, -1 for another name
int z80_flag_bit(const std::string &n)
{
    for (int i = 0; i < 8; i++)
        if (n == kFlagNames[i]) return 7 - i;
    return -1;
}

std::vector<CpuRegister> RemoteZ80::get_register_values()
{
    return z80_register_list(m_m->snap().regs);
}

std::vector<std::pair<std::string, std::string>> RemoteZ80::get_flags()
{
    return z80_flag_list(num(m_m->snap().regs, "main", "af") & 0xFF);
}

void RemoteZ80::set_context_value(const std::string &n, unsigned int v)
{
    m_m->http().json("PUT", m_m->base() + "/registers/" + QUrl::toPercentEncoding(QString::fromStdString(n)),
                     QJsonObject{{"value", (int)v}});
    m_m->changed();
}

bool RemoteZ80::set_flag(const std::string &n, bool value)
{
    const int bit = z80_flag_bit(n);
    if (bit < 0) return false;
    unsigned int f = num(m_m->snap().regs, "main", "af") & 0xFF;
    f = value ? (f | (1u << bit)) : (f & ~(1u << bit));
    int status = 0;
    m_m->http().json("PUT", m_m->base() + "/registers/F", QJsonObject{{"value", (int)f}}, &status);
    m_m->changed();
    return status == 200 || status == kQueued;
}

bool RemoteZ80::stack_address(int i, unsigned int &address)
{
    address = (num(m_m->snap().regs, "special", "sp") + 2 * i) & 0xFFFF;
    return true;
}

std::vector<unsigned int> RemoteZ80::get_history()
{
    // Oldest first, as ecat keeps it: the execution history `cpu`, a snapshot (the machine runs on)
    std::vector<unsigned int> r;
    const QJsonArray records = m_m->http().json("GET", m_m->base() + "/history/cpu?last=256").value("records").toArray();
    for (const QJsonValue &e : records) r.push_back((unsigned int)e.toObject().value("pc").toInt());
    return r;
}

bool RemoteZ80::has_breakpoint(unsigned int address)
{
    for (const Breakpoint &b : m_m->breakpoints())
        if (!b.run_to && b.address == (address & 0xFFFF)) return true;
    return false;
}

void RemoteZ80::add_breakpoint(unsigned int address)
{
    if (has_breakpoint(address)) return;
    // One breakpoint per address: a pending run-to there gives its place to the user's
    for (const Breakpoint &b : m_m->breakpoints())
        if (b.run_to && b.address == (address & 0xFFFF)) m_m->http().json("DELETE", m_m->base() + "/breakpoints/" + QString::number(b.id));
    m_m->http().json("POST", m_m->base() + "/breakpoints", QJsonObject{{"type", "execution"}, {"address", (int)(address & 0xFFFF)}});
    m_m->breakpoints_changed();
}

void RemoteZ80::remove_breakpoint(unsigned int address)
{
    for (const Breakpoint &b : m_m->breakpoints())
        if (!b.run_to && b.address == (address & 0xFFFF)) m_m->http().json("DELETE", m_m->base() + "/breakpoints/" + QString::number(b.id));
    m_m->breakpoints_changed();
}

std::string RemoteZ80::breakpoint_condition(unsigned int address)
{
    // As the embedded machine: any of the address's breakpoints holds; one without a condition always
    std::string condition;
    for (const Breakpoint &b : m_m->breakpoints()) {
        if (b.run_to || b.address != (address & 0xFFFF)) continue;
        if (b.condition.empty()) return {};
        condition += (condition.empty() ? "(" : " || (") + b.condition + ")";
    }
    return condition;
}

std::vector<unsigned int> RemoteZ80::get_breakpoints()
{
    std::vector<unsigned int> r;
    for (const Breakpoint &b : m_m->breakpoints())
        if (!b.run_to && std::find(r.begin(), r.end(), b.address) == r.end()) r.push_back(b.address);
    std::sort(r.begin(), r.end());
    return r;
}

std::vector<ecatshim::EventBreakpoint> RemoteZ80::get_event_breakpoints()
{
    return m_m->event_breakpoints("main");
}

bool RemoteZ80::add_event_breakpoint(const std::string &kind, unsigned int value, unsigned int mask, const std::string &hits,
                                    std::string &error)
{
    return m_m->add_event_breakpoint("main", kind, value, mask, hits, error);
}

bool RemoteZ80::reset_event_hits(unsigned int id)
{
    return m_m->reset_event_hits("main", id);
}

bool RemoteZ80::remove_event_breakpoint(unsigned int id)
{
    return m_m->remove_event_breakpoint("main", id);
}

bool RemoteZ80::enable_event_breakpoint(unsigned int id, bool on)
{
    return m_m->enable_event_breakpoint("main", id, on);
}

std::vector<ecatshim::EventBreakpoint> RemoteCardZ80::get_event_breakpoints()
{
    return m_m->event_breakpoints(m_cpu);
}

bool RemoteCardZ80::add_event_breakpoint(const std::string &kind, unsigned int value, unsigned int mask, const std::string &hits,
                                    std::string &error)
{
    return m_m->add_event_breakpoint(m_cpu, kind, value, mask, hits, error);
}

bool RemoteCardZ80::reset_event_hits(unsigned int id)
{
    return m_m->reset_event_hits(m_cpu, id);
}

bool RemoteCardZ80::remove_event_breakpoint(unsigned int id)
{
    return m_m->remove_event_breakpoint(m_cpu, id);
}

bool RemoteCardZ80::enable_event_breakpoint(unsigned int id, bool on)
{
    return m_m->enable_event_breakpoint(m_cpu, id, on);
}

QString RemoteCardZ80::route(const QString &path) const
{
    return m_m->base() + path + (path.contains('?') ? "&cpu=" : "?cpu=") + m_cpu;
}

const QJsonObject &RemoteCardZ80::regs()
{
    // Not given (the card gone, an error): the last ones read stay
    const uint64_t gen = m_m->snap().generation;
    if (m_regs_gen != gen) {
        m_regs_gen = gen;
        int status = 0;
        const QJsonObject r = m_m->http().json("GET", route("/registers"), QJsonObject(), &status);
        if (status == 200) m_regs = r;
    }
    return m_regs;
}

unsigned int RemoteCardZ80::get_pc()
{
    return num(regs(), "special", "pc");
}

unsigned int RemoteCardZ80::peek_mem(unsigned int a)
{
    const uint64_t gen = m_m->snap().generation;
    if (m_memory_gen != gen || m_memory.size() != 0x10000) {
        m_memory_gen = gen;
        QByteArray out;
        if (m_m->http().request("GET", route("/memory/read/0?length=65536&format=binary"), QByteArray(), out) == 200)
            m_memory = out;
        if (m_memory.size() != 0x10000) m_memory = QByteArray(0x10000, '\xFF');
    }
    return (uint8_t)m_memory[a & 0xFFFF];
}

bool RemoteCardZ80::poke_mem(unsigned int a, unsigned int v)
{
    int status = 0;
    m_m->http().json("PUT", route("/memory/" + QString::number(a & 0xFFFF)), QJsonObject{{"data", QJsonArray{(int)(v & 0xFF)}}},
                     &status);
    m_m->changed();
    return status == 200 || status == kQueued;
}

// The card's clock: the `gs` report's cpu.frame_cycle (its cycles since it started are not there: the change is
// taken within one frame)
bool RemoteCardZ80::card_cycle(CardCycle &out)
{
    const QJsonObject cpu = m_m->card_report(m_cpu).value("cpu").toObject();
    if (!cpu.contains("frame_cycle")) return false;
    out.frame_cycle = (int64_t)cpu.value("frame_cycle").toDouble();
    out.total = -1;
    out.frame = m_m->snap().frame;
    out.clock_hz = (unsigned int)cpu.value("clock_hz").toDouble();
    return true;
}

std::vector<CpuRegister> RemoteCardZ80::get_register_values()
{
    return z80_register_list(regs());
}

std::vector<std::pair<std::string, std::string>> RemoteCardZ80::get_flags()
{
    return z80_flag_list(num(regs(), "main", "af") & 0xFF);
}

void RemoteCardZ80::set_context_value(const std::string &n, unsigned int v)
{
    m_m->http().json("PUT", route("/registers/" + QUrl::toPercentEncoding(QString::fromStdString(n))), QJsonObject{{"value", (int)v}});
    m_m->changed();
}

bool RemoteCardZ80::set_flag(const std::string &n, bool value)
{
    const int bit = z80_flag_bit(n);
    if (bit < 0) return false;
    unsigned int f = num(regs(), "main", "af") & 0xFF;
    f = value ? (f | (1u << bit)) : (f & ~(1u << bit));
    int status = 0;
    m_m->http().json("PUT", route("/registers/F"), QJsonObject{{"value", (int)f}}, &status);
    m_m->changed();
    return status == 200 || status == kQueued;
}

bool RemoteCardZ80::stack_address(int i, unsigned int &address)
{
    address = (num(regs(), "special", "sp") + 2 * i) & 0xFFFF;
    return true;
}

std::vector<unsigned int> RemoteCardZ80::get_history()
{
    // Oldest first: the card's execution history, a snapshot (the card runs on). Off it comes on the first time it
    // is looked at (it costs the card's idle sleep its records), off again when the debugger goes
    std::vector<unsigned int> r;
    if (m_history.isEmpty()) return r;
    for (const QJsonValue &v : m_m->histories().value("histories").toArray()) {
        const QJsonObject h = v.toObject();
        if (h.value("device").toString() == m_history && h.value("mode").toString() == "off") m_m->set_history(m_history, true);
    }
    const QJsonArray records = m_m->http().json("GET", m_m->base() + "/history/" + m_history + "?last=256").value("records").toArray();
    for (const QJsonValue &e : records) r.push_back((unsigned int)e.toObject().value("pc").toInt());
    return r;
}

bool RemoteCardZ80::has_breakpoint(unsigned int address)
{
    for (const Breakpoint &b : m_m->breakpoints(m_cpu))
        if (!b.run_to && b.address == (address & 0xFFFF)) return true;
    return false;
}

void RemoteCardZ80::add_breakpoint(unsigned int address)
{
    if (has_breakpoint(address)) return;
    for (const Breakpoint &b : m_m->breakpoints(m_cpu))
        if (b.run_to && b.address == (address & 0xFFFF)) m_m->http().json("DELETE", route("/breakpoints/" + QString::number(b.id)));
    m_m->http().json("POST", m_m->base() + "/breakpoints",
                     QJsonObject{{"type", "execution"}, {"address", (int)(address & 0xFFFF)}, {"cpu", m_cpu}});
    m_m->breakpoints_changed();
}

void RemoteCardZ80::remove_breakpoint(unsigned int address)
{
    for (const Breakpoint &b : m_m->breakpoints(m_cpu))
        if (!b.run_to && b.address == (address & 0xFFFF)) m_m->http().json("DELETE", route("/breakpoints/" + QString::number(b.id)));
    m_m->breakpoints_changed();
}

std::vector<unsigned int> RemoteCardZ80::get_breakpoints()
{
    std::vector<unsigned int> r;
    for (const Breakpoint &b : m_m->breakpoints(m_cpu))
        if (!b.run_to && std::find(r.begin(), r.end(), b.address) == r.end()) r.push_back(b.address);
    std::sort(r.begin(), r.end());
    return r;
}

// The classic card maps ROM at #0000 and RAM page 0 at #4000 for good, its #00 port pages #8000-#FFFF; the NeoGS also
// puts RAM at #0000 (GSCFG0.NOROM) and pages #C000 on its own (MPAGEX)
std::vector<ecatshim::AddressWindow> RemoteCardZ80::get_windows()
{
    const uint64_t gen = m_m->snap().generation;
    if (m_windows_gen != gen) {
        m_windows_gen = gen;
        int status = 0;
        const QJsonObject o = m_m->http().json("GET", route("/memory/map"), QJsonObject(), &status);
        if (status == 200) m_windows = o.value("windows").toArray();
    }
    std::vector<ecatshim::AddressWindow> r;
    for (unsigned int w = 0; w < 4 && w < (unsigned int)m_windows.size(); w++) {
        const QJsonObject o = m_windows[(int)w].toObject();
        ecatshim::AddressWindow x;
        x.start = w * kPage;
        x.size = kPage;
        x.switchable = (w >= 2) || (w == 0 && m_flash);
        x.page = (unsigned int)o.value("page").toInt();
        x.offset = (unsigned int)o.value("offset").toInt();
        const QString kind = o.value("kind").toString();
        if (kind == "rom") {
            x.kind = m_flash ? "flash" : "ROM";
            x.device = m_rom;
            x.writable = false;
        } else if (kind == "ram") {
            x.kind = "RAM";
            x.device = m_ram;
        }
        r.push_back(x);
    }
    return r;
}

} // namespace

std::unique_ptr<ecatshim::Emulator> CreateRemoteMachine(const RemoteOptions &options, std::string &error)
{
    Http http(QString::fromStdString(options.host), options.port);
    int status = 0;
    const QJsonArray list = http.json("GET", "/api/v1/emulator", QJsonObject(), &status).value("emulators").toArray();
    if (status != 200) {
        error = "no WebAPI at " + options.host + ":" + std::to_string(options.port);
        return nullptr;
    }
    QString id;
    for (const QJsonValue &v : list) {
        const QJsonObject e = v.toObject();
        const QString want = QString::fromStdString(options.emulator);
        if (want.isEmpty() || e.value("id").toString() == want || e.value("symbolic_id").toString() == want) {
            id = e.value("id").toString();
            break;
        }
    }
    if (id.isEmpty()) {
        error = options.emulator.empty() ? "the emulator runs no machine" : "no emulator " + options.emulator;
        return nullptr;
    }
    auto m = std::make_unique<RemoteMachine>(options, id);
    if (!m->build(error)) return nullptr;
    return m;
}

uint64_t RemoteMachineFrames(ecatshim::Emulator * e)
{
    auto * m = dynamic_cast<RemoteMachine *>(e);
    return m ? m->frames() : 0;
}

void RemoteMachineTraffic(ecatshim::Emulator * e, uint64_t &requests, uint64_t &bytes)
{
    requests = bytes = 0;
    if (auto * m = dynamic_cast<RemoteMachine *>(e)) m->traffic(requests, bytes);
}

std::string RemoteMachineId(ecatshim::Emulator * e)
{
    auto * m = dynamic_cast<RemoteMachine *>(e);
    return m ? m->id().toStdString() : std::string();
}

std::string RemoteMachineEndpoint(ecatshim::Emulator * e)
{
    auto * m = dynamic_cast<RemoteMachine *>(e);
    return m ? m->endpoint().toStdString() : std::string();
}

void RemoteMachineAction(ecatshim::Emulator * e, const std::string &action, const std::string &params, QObject * context,
                         MachineActionDone done)
{
    auto * m = dynamic_cast<RemoteMachine *>(e);
    const QString id = m ? m->id() : QString();
    if (id.isEmpty()) {
        done(MachineActionResult{false, "not_now", "the debugger is not connected to an emulator", "{}"});
        return;
    }
    // The views' Http blocks: a search through the history may take a while, so this one goes on its own, the GUI
    // thread free meanwhile
    static QNetworkAccessManager * network = new QNetworkAccessManager(QCoreApplication::instance());
    QNetworkRequest request(QUrl(m->endpoint() + "/api/v1/controllers/actions/" + QString::fromStdString(action)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);
    const QJsonObject body{{"instance", id}, {"value", true},
                           {"params", QJsonDocument::fromJson(QByteArray::fromStdString(params)).object()}};
    QNetworkReply * reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const QPointer<QObject> alive(context);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, alive, done]() {
        reply->deleteLater();
        if (!alive) return;
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        MachineActionResult r;
        if (!answer.contains("ok")) {
            r.error = "failed";
            r.message = answer.value("message").toString(reply->errorString()).toStdString();
            r.body = "{}";
        } else {
            r.ok = answer.value("ok").toBool();
            r.error = answer.value("error").toString().toStdString();
            r.message = answer.value("message").toString().toStdString();
            r.body = QJsonDocument(answer.value("result").toObject()).toJson(QJsonDocument::Compact).toStdString();
        }
        done(r);
    });
}
