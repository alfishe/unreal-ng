// unreal-debugger: eCat3's debugger API (ecatshim.h) over a running emulator's WebAPI: unreal-qt, or the headless one.
//
// The views read a lot and often: every row of the listing, every byte of a dump, every register, five times a
// second while the machine runs. One request per read would be thousands per tick, so the machine is read as one
// coherent snapshot per tick (GET /debug/snapshot: registers, the CPU's 64 KB, its page map, the stop) and the views
// read that copy. Dumps of RAM, ROM and device memories read whole 16 KB pages / 4 KB chunks, once per snapshot.
// A snapshot is taken again when the copy is older than a tick while the machine runs, after any change this
// debugger makes, and when the emulator says something happened (a stop, a run start, an edit by another client).
//
// That "something happened" is a long poll (GET /debug/wait) on a thread of its own with its own connection; it
// calls the window's listener, as the core's events do in the in-process adapter. Everything else runs on the GUI
// thread over one keep-alive connection. Changes go through a run-control session (X-Unreal-Session).

#include "remotemachine.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

#include "unrealmachine.h"
#include "zxwindows.h"

namespace {

using ecatshim::CpuRegister;
using ecatshim::DeviceFieldInfo;
using ecatshim::DeviceFieldValue;

const char * kRunToGroup = "ecat-run-to";
const unsigned int kPage = 0x4000;
const unsigned int kChunk = 0x1000;

//------------------------------------------------------------------- HTTP

// HTTP/1.1 over one keep-alive socket, blocking: the views ask synchronously, and a request to a local emulator
// answers in well under a millisecond. Not thread-safe: one per thread
class Http
{
public:
    Http(const QString &host, uint16_t port) : m_host(host), m_port(port) {}

    void set_session(const QByteArray &s) { m_session = s; }
    // While the gate is down every request answers 0 at once (the machine is offline: nothing to ask)
    void set_gate(const std::atomic<bool> * open) { m_gate = open; }
    uint64_t requests() const { return m_requests; }
    uint64_t bytes() const { return m_bytes; }

    // The status (0: no connection or no answer in time) and the body
    int request(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms = 5000)
    {
        if (m_gate && !m_gate->load()) return 0;
        // A kept connection the server has closed fails at once: then once more on a new one
        for (int attempt = 0; attempt < 2; attempt++) {
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
    QByteArray m_session;
    const std::atomic<bool> * m_gate = nullptr;
    std::unique_ptr<QTcpSocket> m_socket;
    QByteArray m_buffer;
    uint64_t m_requests = 0;
    uint64_t m_bytes = 0;

    int once(const char * method, const QString &path, const QByteArray &body, QByteArray &out, int timeout_ms)
    {
        QElapsedTimer clock;
        clock.start();
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
            m_socket = std::make_unique<QTcpSocket>();
            m_buffer.clear();
            m_socket->connectToHost(m_host, m_port);
            if (!m_socket->waitForConnected(std::min(timeout_ms, 2000))) return 0;
        }
        QByteArray head = QByteArray(method) + ' ' + path.toUtf8() + " HTTP/1.1\r\nHost: " + m_host.toUtf8() + ':' +
                          QByteArray::number(m_port) + "\r\nConnection: keep-alive\r\n";
        if (!body.isEmpty()) head += "Content-Type: application/json\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        if (!m_session.isEmpty()) head += "X-Unreal-Session: " + m_session + "\r\n";
        head += "\r\n";
        m_socket->write(head + body);
        m_requests++;

        auto more = [&]() {
            const int left = timeout_ms - (int)clock.elapsed();
            if (left <= 0 || !m_socket->waitForReadyRead(left)) return false;
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
    QJsonObject regs;
    QByteArray memory;      // the CPU's 64 KB
    struct Bank { QString kind; unsigned int page = 0; bool writable = false; } banks[4];
};

struct Breakpoint {
    int id = 0;
    unsigned int address = 0;
    bool run_to = false;
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

// One device's history (GET /history/{device}): an entry of its own in the Devices tab, every device's apart. The
// summary is its mode and counts, opened the newest records
class RemoteHistory : public ecatshim::ComputerDevice
{
public:
    RemoteHistory(RemoteMachine * m, const std::string &device, const std::string &what)
        : m_m(m), m_device(device), m_what(what)
    {
        name = device + " history";
        type = "history";
    }
    std::vector<DeviceFieldInfo> get_device_fields() override { return {}; }
    bool get_field(const std::string &, unsigned int, unsigned int, DeviceFieldValue &) override { return false; }
    bool get_tree(bool deep, ecatshim::DeviceNode &out) override;

private:
    static constexpr int kShown = 64;
    RemoteMachine * m_m;
    std::string m_device;
    std::string m_what;
};

// A WebAPI state report of the device catalog (GET /state/devices): the Devices tab's entry (title, the catalog's
// summary, the report as a tree) and, for scripts, device fields (the leaves of its JSON, dotted paths as names)
class RemoteReport : public ecatshim::ComputerDevice
{
public:
    RemoteReport(RemoteMachine * m, const std::string &id, const std::string &title, const QString &path)
        : m_m(m), m_path(path), m_title(title)
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
    QString m_path;
    std::string m_title;
    std::vector<std::pair<std::string, std::string>> m_values;

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
};

//------------------------------------------------------------------- the machine

class RemoteMachine : public ecatshim::Emulator
{
public:
    RemoteMachine(const RemoteOptions &o, const QString &id, const QByteArray &session)
        : m_http(QString::fromStdString(o.host), o.port), m_host(QString::fromStdString(o.host)), m_port(o.port),
          m_want(QString::fromStdString(o.emulator)), m_base("/api/v1/emulator/" + id), m_session(session),
          m_settings("unreal-ng", "unreal-debugger")
    {
        m_http.set_session(session);
        m_http.set_gate(&m_online);
        m_wait_base = m_base;
        m_wait_session = session;
    }

    ~RemoteMachine() override
    {
        m_stop = true;
        if (m_waiter) {
            m_waiter->wait();
            delete m_waiter;
        }
        clear_run_to();
        // Debug mode as it was before the debugger came
        if (m_attached) m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", m_debug_before}});
        if (m_cpu_history_ours) m_http.json("POST", m_base + "/history/cpu/config", QJsonObject{{"mode", "off"}});
        m_http.json("DELETE", m_base + "/control/session");
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
        const ZxPaging paging = ZxModelPaging(m_http.json("GET", m_base + "/state/paging").value("model").toString().toStdString());
        m_cpu->set_pages(ram.get(), rom.get(), paging);
        ram->set_paging(paging);
        m_dm.add(ram);
        m_dm.add(rom);

        const QJsonArray regions = m_http.json("GET", m_base + "/memory/regions").value("regions").toArray();
        for (const QJsonValue &v : regions) {
            const QJsonObject r = v.toObject();
            const QString n = r.value("name").toString();
            const QString kind = n.mid(n.lastIndexOf('.') + 1);
            const unsigned int size = (unsigned int)r.value("size").toDouble();
            const bool writable = r.value("writable").toBool();
            std::string desc = r.value("description").toString().toStdString();
            desc = desc.rfind("Device memory recorded by time travel", 0) == 0 ? "device memory"
                                                                                : desc.substr(0, desc.find_first_of(":,;("));
            if (kind == "ram" || kind == "vram" || kind == "fastram")
                m_dm.add(std::make_shared<RemoteRegion<ecatshim::Memory>>(this, n, size, writable, desc));
            else
                m_dm.add(std::make_shared<RemoteRegion<ecatshim::AddressableDevice>>(this, n, size, writable, desc));
        }

        // The reports this machine has, as the device catalog lists them (its cards are fixed: a slot change makes a
        // new machine, another id)
        for (const QJsonValue &v : catalog().value("reports").toArray()) {
            const QJsonObject r = v.toObject();
            m_dm.add(std::make_shared<RemoteReport>(this, r.value("id").toString().toStdString(),
                                                    r.value("title").toString().toStdString(),
                                                    m_base + "/" + r.value("route").toString()));
        }
        // Every device history, each an entry of its own
        for (const QJsonValue &v : histories().value("histories").toArray()) {
            const QJsonObject h = v.toObject();
            m_dm.add(std::make_shared<RemoteHistory>(this, h.value("device").toString().toStdString(),
                                                     h.value("what").toString().toStdString()));
        }

        dm = &m_dm;
        loaded = true;
        data_path = QDir::currentPath().toStdString();

        m_seq = m_snap.seq;
        m_waiter = QThread::create([this]() { wait_loop(); });
        m_waiter->start();
        return true;
    }

    Http &http() { return m_http; }

    // The device catalog, read again when the last read is half a second old: one request a refresh for every
    // entry's summary
    QJsonObject catalog()
    {
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        if (!m_catalog_age.isValid() || m_catalog_age.elapsed() > 500) {
            m_catalog = m_http.json("GET", m_base + "/state/devices");
            m_catalog_age.start();
        }
        return m_catalog;
    }
    // The device histories with their modes and counts, read again when half a second old (the Devices tab's
    // summaries of every one)
    QJsonObject histories()
    {
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        if (!m_histories_age.isValid() || m_histories_age.elapsed() > 500) {
            m_histories = m_http.json("GET", m_base + "/history");
            m_histories_age.start();
        }
        return m_histories;
    }
    const QString &base() const { return m_base; }

    bool debug_online() const override { return m_online.load(); }
    std::string debug_offline_text() const override
    {
        return QString("No emulator: waiting for %1 at %2:%3")
            .arg(m_want.isEmpty() ? QString("a %1").arg(m_model.isEmpty() ? QString("machine") : m_model) : m_want)
            .arg(m_host)
            .arg(m_port)
            .toStdString();
    }

    // The snapshot, taken again when stale: false when the emulator does not answer
    bool fresh()
    {
        adopt();
        const bool stale = !m_snap.valid || m_dirty.load() || m_snap.seq != m_seq.load() ||
                           (m_snap.state == "running" && m_age.elapsed() >= 150);
        if (!stale) return true;
        m_dirty = false;
        QByteArray out;
        const int status = m_http.request("GET", m_base + "/debug/snapshot?stack=0&memory=cpu:0:65536", QByteArray(), out);
        if (status != 200) return m_snap.valid;
        const QJsonObject s = QJsonDocument::fromJson(out).object();
        m_snap.valid = true;
        m_snap.generation++;
        m_snap.seq = (uint64_t)s.value("seq").toDouble();
        m_snap.frame = (uint64_t)s.value("time").toObject().value("frame").toDouble();
        m_snap.state = s.value("state").toString();
        m_snap.reason = s.value("pause").toObject().value("reason").toString();
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
        return true;
    }
    Snapshot &snap()
    {
        fresh();
        return m_snap;
    }
    // A change of ours: the next read takes a new snapshot
    void changed() { m_dirty = true; }

    // A suitable emulator the waiter found and took a session on: this machine is its from now (the views' thread;
    // the waiter never touches m_base / m_http)
    void adopt()
    {
        QString id;
        QByteArray session;
        {
            std::lock_guard<std::mutex> lock(m_wait_mutex);
            if (m_pending_id.isEmpty()) return;
            id = m_pending_id;
            session = m_pending_session;
            m_pending_id.clear();
            m_base = m_wait_base = "/api/v1/emulator/" + id;
            m_session = session;
        }
        m_id = id;
        m_http.set_session(session);
        m_run_to_armed = false;
        m_bp_dirty = true;
        {
            std::lock_guard<std::mutex> lock(m_catalog_mutex);
            m_catalog_age.invalidate();
            m_histories_age.invalidate();
        }
        m_dirty = true;
        m_online = true;
        if (m_attached) {
            m_debug_before = m_http.json("GET", m_base + "/debugmode").value("enabled").toBool();
            m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", true}});
            m_cpu_history_ours = false;
            start_cpu_history();
        }
    }

    // The execution history `cpu` on while the debugger is (a ring), unless the user has it some way already: read
    // as a snapshot, the machine runs on
    void start_cpu_history()
    {
        for (const QJsonValue &v : m_http.json("GET", m_base + "/history").value("histories").toArray()) {
            const QJsonObject h = v.toObject();
            if (h.value("device").toString() != "cpu" || h.value("mode").toString() != "off") continue;
            int status = 0;
            m_http.json("POST", m_base + "/history/cpu/config", QJsonObject{{"mode", "ring"}}, &status);
            m_cpu_history_ours = status == 200;
        }
    }

    std::vector<Breakpoint> breakpoints()
    {
        fresh();
        if (m_bp_generation != m_snap.generation || m_bp_dirty) {
            m_bp_generation = m_snap.generation;
            m_bp_dirty = false;
            m_bps.clear();
            const QJsonArray list = m_http.json("GET", m_base + "/breakpoints").value("breakpoints").toArray();
            for (const QJsonValue &v : list) {
                const QJsonObject b = v.toObject();
                if (b.value("type").toString() != "memory" || !b.value("execute").toBool() || b.contains("address_end"))
                    continue;
                m_bps.push_back(Breakpoint{b.value("id").toInt(), (unsigned int)b.value("address").toInt(),
                                           b.value("group").toString() == kRunToGroup});
            }
        }
        return m_bps;
    }
    void breakpoints_changed() { m_bp_dirty = true; }

    void debug_attach(bool on) override
    {
        if (!on) return;
        if (!m_attached) m_debug_before = m_http.json("GET", m_base + "/debugmode").value("enabled").toBool();
        m_attached = true;
        m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", true}});
        start_cpu_history();
        m_cpu->m_debug = ecatshim::DEBUG_BRAKES;
        changed();
    }
    void debug_detach() override
    {
        clear_run_to();
        if (debug_paused()) m_http.json("POST", m_base + "/resume");
        changed();
    }
    bool debug_paused() const override
    {
        auto * self = const_cast<RemoteMachine *>(this);
        return self->snap().state == "paused";
    }
    bool debug_stepping() const override { return false; }
    void debug_pause() override
    {
        m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", true}});
        m_http.json("POST", m_base + "/pause");
        changed();
    }
    void debug_run(bool breakpoints) override
    {
        if (!m_arming_run_to) drop_run_to();
        // Breakpoints fire only in debug mode: a run without them is a run with it off
        m_http.json("PUT", m_base + "/debugmode", QJsonObject{{"enabled", breakpoints}});
        m_cpu->m_debug = breakpoints ? ecatshim::DEBUG_BRAKES : ecatshim::DEBUG_OFF;
        if (debug_paused()) m_http.json("POST", m_base + "/resume");
        changed();
    }
    void debug_step(ecatshim::CPU *, unsigned int count) override
    {
        drop_run_to();
        if (count <= 1) m_http.json("POST", m_base + "/step");
        else m_http.json("POST", m_base + "/steps", QJsonObject{{"count", (int)count}});
        changed();
    }
    // Step over / out answer when the machine has stopped (a pool of the WebAPI runs them)
    void debug_step_over(ecatshim::CPU *) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepover", QJsonObject(), nullptr, 15000);
        changed();
    }
    void debug_step_out(ecatshim::CPU *) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepout", QJsonObject(), nullptr, 15000);
        changed();
    }
    void debug_run_to(ecatshim::CPU *, unsigned int address) override
    {
        // A breakpoint of its own group that goes away at the next stop. The WebAPI keeps one per address: on a
        // user's breakpoint a run-to would move it into its group, and the user's stops there all the same
        bool user = false;
        for (const Breakpoint &b : breakpoints()) user = user || (!b.run_to && b.address == (address & 0xFFFF));
        // Gone at a stop after this run starts (the run start and the stop each move the seq), not at the stop it
        // was set at, whose answer may come only now
        drop_run_to();
        m_run_to_seq = m_seq.load();
        if (!user) {
            m_http.json("POST", m_base + "/breakpoints",
                        QJsonObject{{"type", "execution"}, {"address", (int)(address & 0xFFFF)}, {"group", kRunToGroup},
                                    {"note", "run to the cursor"}});
            m_run_to_armed = true;
        }
        breakpoints_changed();
        m_arming_run_to = true;
        debug_run(true);
        m_arming_run_to = false;
    }
    unsigned int debug_serial() const override { return (unsigned int)m_seq.load(); }
    ecatshim::CPU * debug_stop_cpu() const override { return m_cpu.get(); }
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
    Http m_http;
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
    QString m_id;
    QByteArray m_session;
    QSettings m_settings;
    ecatshim::DeviceManager m_dm;
    std::shared_ptr<RemoteZ80> m_cpu;
    std::shared_ptr<RemoteMapper> m_mapper;

    Snapshot m_snap;
    QElapsedTimer m_age;
    std::atomic<bool> m_dirty{true};
    std::atomic<uint64_t> m_seq{0};
    std::atomic<uint64_t> m_run_to_seq{0};
    std::atomic<bool> m_run_to_armed{false};
    bool m_arming_run_to = false;
    std::vector<Breakpoint> m_bps;
    uint64_t m_bp_generation = 0;
    bool m_bp_dirty = true;
    bool m_attached = false;
    bool m_cpu_history_ours = false;  // the debugger turned `cpu` on: off again when it goes
    bool m_debug_before = false;

    QThread * m_waiter = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_online{true};  // the gate of m_http too
    std::mutex m_wait_mutex;           // the waiter's view of the instance and a found one
    QString m_wait_base;
    QByteArray m_wait_session;
    QString m_pending_id;
    QByteArray m_pending_session;
    std::atomic<uint64_t> m_wait_requests{0};
    std::atomic<uint64_t> m_wait_bytes{0};
    std::mutex m_listener_mutex;
    std::function<void()> m_listener;

    // A run-to of an earlier command is over whatever happened since: a new command never meets it (on a CPU in
    // HALT at its address it would stop the run at once)
    void drop_run_to()
    {
        if (m_run_to_armed.exchange(false)) clear_run_to();
    }
    void clear_run_to()
    {
        m_http.json("POST", m_base + "/breakpoints/remove", QJsonObject{{"select", QString("group:") + kRunToGroup}});
        breakpoints_changed();
    }

    // The long poll: each answer with a new seq (a stop, a run start, an edit) is an event for the window. At a stop
    // the run-to breakpoint goes, whatever stopped the machine
    void wait_loop()
    {
        Http wait(m_host, m_port);
        while (!m_stop) {
            QString base;
            {
                std::lock_guard<std::mutex> lock(m_wait_mutex);
                if (!m_pending_id.isEmpty()) base.clear();  // found: the views' thread takes it over (adopt)
                else base = m_wait_base;
                wait.set_session(m_wait_session);
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
            if (a.value("state").toString() == "paused" && m_seq.load() >= m_run_to_seq.load() + 2 &&
                m_run_to_armed.exchange(false))
                wait.json("POST", base + "/breakpoints/remove", QJsonObject{{"select", QString("group:") + kRunToGroup}});
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
    // Offline: a suitable emulator (the one asked for by id, else one of the same model: the views' devices fit it)
    // with a run-control session of ours; the views' thread takes it over (adopt)
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
            const QString id = e.value("id").toString();
            const QJsonObject s = wait.json("POST", "/api/v1/emulator/" + id + "/control/session",
                                            QJsonObject{{"client", "ecat-debugger"}, {"surface", "webapi"}, {"lease_seconds", 120}},
                                            &status);
            if (status != 200) continue;
            {
                std::lock_guard<std::mutex> lock(m_wait_mutex);
                m_pending_id = id;
                m_pending_session = s.value("session").toString().toUtf8();
                m_wait_session = m_pending_session;
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
        if (m_m->http().request("GET", m_m->base() + "/memory/page/" + m_kind + "/" + QString::number(page) +
                                    "?format=binary&length=" + QString::number(kPage),
                                QByteArray(), out) != 200)
            out.clear();
        out.resize(kPage, '\xFF');
        c = {gen, out};
    }
    return (uint8_t)c.second[a % kPage];
}

void RemotePages::rom_titles()
{
    const uint64_t seq = m_m->snap().seq;
    if (seq == m_titles_seq) return;
    m_titles_seq = seq;
    m_titles.assign(m_pages, {});
    const QJsonArray list = m_m->http().json("GET", m_m->base() + "/state/memory/rom").value("pages").toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject p = v.toObject();
        const unsigned int page = (unsigned int)p.value("page").toInt();
        if (page >= m_pages) continue;
        std::string title = p.value("title").toString().toStdString();
        std::string role = p.value("description").toString().toStdString();
        if (title.rfind("Unknown ROM", 0) == 0) title = "unknown ROM";
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
        if (m_m->http().request("GET", path, QByteArray(), out) != 200) out.clear();
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

std::vector<DeviceFieldInfo> RemoteReport::get_device_fields()
{
    m_values.clear();
    flatten(m_m->http().json("GET", m_path), "");
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
    to_tree(m_m->http().json("GET", m_path), parts);
    for (ecatshim::DeviceNode &c : parts.children)
        if (c.name != "available") out.children.push_back(std::move(c));
    return true;
}

bool RemoteHistory::get_tree(bool deep, ecatshim::DeviceNode &out)
{
    out = ecatshim::DeviceNode{name, std::string(), {}};
    for (const QJsonValue &v : m_m->histories().value("histories").toArray()) {
        const QJsonObject h = v.toObject();
        if (h.value("device").toString().toStdString() != m_device) continue;
        const std::string mode = h.value("mode").toString().toStdString();
        out.value = mode == "off" ? m_what + ": off"
                                  : m_what + ": " + mode + ", " + std::to_string((long long)h.value("kept").toDouble()) +
                                        " kept of " + std::to_string((long long)h.value("written").toDouble());
    }
    if (!deep) return true;
    const QJsonObject read =
        m_m->http().json("GET", m_m->base() + "/history/" + QString::fromStdString(m_device) + "?last=" + QString::number(kShown));
    const QJsonArray records = read.value("records").toArray();
    if (records.isEmpty()) {
        out.children.push_back(ecatshim::DeviceNode{"", "no records", {}});
        return true;
    }
    // Newest first, as a reader looks for what just happened
    for (int i = records.size() - 1; i >= 0; i--) {
        ecatshim::DeviceNode c;
        c.name = "[" + std::to_string(records.size() - 1 - i) + "]";
        // A flat record reads as one line ("frame 12  pc 32785  kind ram"), a nested one as a tree
        const QJsonObject r = records[i].toObject();
        bool flat = true;
        for (auto it = r.begin(); it != r.end(); ++it) flat = flat && !it.value().isObject() && !it.value().isArray();
        if (flat) {
            for (auto it = r.begin(); it != r.end(); ++it)
                c.value += (c.value.empty() ? "" : "  ") + it.key().toStdString() + " " + scalar_text(it.value());
        } else {
            to_tree(records[i], c);
        }
        out.children.push_back(std::move(c));
    }
    return true;
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
    return status == 200;
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

std::vector<CpuRegister> RemoteZ80::get_register_values()
{
    const QJsonObject &r = m_m->snap().regs;
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

std::vector<std::pair<std::string, std::string>> RemoteZ80::get_flags()
{
    const unsigned int f = num(m_m->snap().regs, "main", "af") & 0xFF;
    std::vector<std::pair<std::string, std::string>> r;
    for (int i = 0; i < 8; i++) r.emplace_back(kFlagNames[i], ((f >> (7 - i)) & 1) ? "1" : "0");
    return r;
}

void RemoteZ80::set_context_value(const std::string &n, unsigned int v)
{
    m_m->http().json("PUT", m_m->base() + "/registers/" + QUrl::toPercentEncoding(QString::fromStdString(n)),
                     QJsonObject{{"value", (int)v}});
    m_m->changed();
}

bool RemoteZ80::set_flag(const std::string &n, bool value)
{
    int bit = -1;
    for (int i = 0; i < 8; i++)
        if (n == kFlagNames[i]) bit = 7 - i;
    if (bit < 0) return false;
    unsigned int f = num(m_m->snap().regs, "main", "af") & 0xFF;
    f = value ? (f | (1u << bit)) : (f & ~(1u << bit));
    int status = 0;
    m_m->http().json("PUT", m_m->base() + "/registers/F", QJsonObject{{"value", (int)f}}, &status);
    m_m->changed();
    return status == 200;
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

std::vector<unsigned int> RemoteZ80::get_breakpoints()
{
    std::vector<unsigned int> r;
    for (const Breakpoint &b : m_m->breakpoints())
        if (!b.run_to && std::find(r.begin(), r.end(), b.address) == r.end()) r.push_back(b.address);
    std::sort(r.begin(), r.end());
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
    // Run control for this debugger: its changes would otherwise take an implicit session another client may hold
    const QJsonObject s = http.json("POST", "/api/v1/emulator/" + id + "/control/session",
                                    QJsonObject{{"client", "ecat-debugger"}, {"surface", "webapi"}, {"lease_seconds", 120}},
                                    &status);
    if (status != 200) {
        error = "no run-control session: " + QJsonDocument(s).toJson(QJsonDocument::Compact).toStdString();
        return nullptr;
    }
    auto m = std::make_unique<RemoteMachine>(options, id, s.value("session").toString().toUtf8());
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
