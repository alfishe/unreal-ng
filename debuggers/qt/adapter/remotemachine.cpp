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
// That "something happened" is a long poll (GET /debug/wait) on a thread of its own with its own connection; it
// calls the window's listener, as the core's events do in the in-process adapter. Everything else runs on the GUI
// thread over one keep-alive connection. Every request says it is a debugger's (X-Unreal-Source: debugger): the
// user's own hands like the GUI, no run-control session - other debuggers and the GUI go on, every client sees the
// changes; while an automation session holds the machine its changes are refused, as the GUI is locked.

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
const unsigned int kPage = 0x4000;
const unsigned int kChunk = 0x1000;

//------------------------------------------------------------------- HTTP

// HTTP/1.1 over one keep-alive socket, blocking: the views ask synchronously, and a request to a local emulator
// answers in well under a millisecond. Not thread-safe: one per thread
class Http
{
public:
    Http(const QString &host, uint16_t port) : m_host(host), m_port(port) {}

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
        head += "X-Unreal-Source: debugger\r\n";
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
    QString stop_cpu;       // the CPU of the stop: main, gs, ms
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
    std::vector<ecatshim::AddressWindow> get_windows() override;

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
          m_settings("unreal-ng", "unreal-debugger")
    {
        m_http.set_gate(&m_online);
        m_wait_base = m_base;
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
        // The histories the debugger turned on, off again
        for (const QString &device : m_histories_ours)
            m_http.json("POST", m_base + "/history/" + device + "/config", QJsonObject{{"mode", "off"}});
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
        return true;
    }

    Http &http() { return m_http; }

    // The device catalog, read again when the last read is half a second old: one request a refresh for every
    // entry's summary. Not given (the emulator gone, an error), the last one read stays
    QJsonObject catalog()
    {
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        if (!m_catalog_age.isValid() || m_catalog_age.elapsed() > 500) {
            int status = 0;
            const QJsonObject o = m_http.json("GET", m_base + "/state/devices", QJsonObject(), &status);
            if (status == 200) m_catalog = o;
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
            int status = 0;
            const QJsonObject o = m_http.json("GET", m_base + "/history", QJsonObject(), &status);
            if (status == 200) m_histories = o;
            m_histories_age.start();
        }
        return m_histories;
    }
    const QString &base() const { return m_base; }
    // The emulator taken over (empty while offline) and the WebAPI's address, for the GUI control plane
    QString id() const { return m_online.load() ? m_id : QString(); }
    QString endpoint() const { return QString("http://%1:%2").arg(m_host).arg(m_port); }

    bool debug_online() const override { return m_online.load(); }
    // Time travel over the WebAPI: the reads are GETs, the branch commands the branch routes, every other verb
    // POST /ttd/<verb> with its options as the JSON body. ok: a 2xx answer
    bool ttd_supported() const override { return true; }
    TtdAnswer ttd_call(const std::string &verb, const std::map<std::string, std::string> &options) override
    {
        if (!m_online.load()) return {false, R"({"error":"Service Unavailable","message":"The emulator is not connected"})"};
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
        int status = 0;
        // A move in time replays up to a frame and a reverse-continue searches the history: as long as a step over
        const QJsonObject answer = m_http.json(method, path, body, &status, in_query ? 5000 : 15000);
        if (!in_query) changed();
        const bool ok = status >= 200 && status < 300;
        if (status == 0)
            return {false, R"({"error":"Service Unavailable","message":"The emulator did not answer"})"};
        return {ok, QJsonDocument(answer).toJson(QJsonDocument::Compact).toStdString()};
    }
    std::string debug_offline_text() const override
    {
        return QString("Waiting for %1 at %2:%3")
            .arg(m_want.isEmpty() ? (m_model.isEmpty() ? QString("a machine") : m_model) : m_want)
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
        m_snap.stop_cpu = s.value("pause").toObject().value("cpu").toString();
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

    // A suitable emulator the waiter found: this machine is its from now (the views' thread;
    // the waiter never touches m_base / m_http)
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
            m_histories_ours.clear();
            start_cpu_history();
        }
    }

    // The execution history `cpu` on while the debugger is (a ring), unless the user has it some way already: read
    // as a snapshot, the machine runs on
    void start_cpu_history()
    {
        for (const QJsonValue &v : m_http.json("GET", m_base + "/history").value("histories").toArray()) {
            const QJsonObject h = v.toObject();
            if (h.value("device").toString() == "cpu" && h.value("mode").toString() == "off") set_history("cpu", true);
        }
    }
    // A history on (a ring) or off; one the debugger turned on it turns off when it goes, one it found on it leaves
    void set_history(const QString &device, bool on)
    {
        int status = 0;
        m_http.json("POST", m_base + "/history/" + device + "/config", QJsonObject{{"mode", on ? "ring" : "off"}}, &status);
        if (status != 200) return;
        if (on) m_histories_ours.insert(device);
        else m_histories_ours.erase(device);
        std::lock_guard<std::mutex> lock(m_catalog_mutex);
        m_histories_age.invalidate();
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
            const QJsonArray list = m_http.json("GET", m_base + "/breakpoints?cpu=" + cpu).value("breakpoints").toArray();
            for (const QJsonValue &v : list) {
                const QJsonObject b = v.toObject();
                if (b.value("type").toString() != "memory" || !b.value("execute").toBool() || b.contains("address_end"))
                    continue;
                l.list.push_back(Breakpoint{b.value("id").toInt(), (unsigned int)b.value("address").toInt(),
                                            b.value("group").toString() == kRunToGroup});
            }
        }
        return l.list;
    }
    void breakpoints_changed()
    {
        for (auto &l : m_bps) l.second.dirty = true;
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
    // A step of a card's tab is the card's step (?cpu=): the machine runs until it ends (up to 2 s, over / out 5 s)
    void debug_step(ecatshim::CPU * c, unsigned int count) override
    {
        drop_run_to();
        const QString q = step_query(c);
        if (count <= 1) m_http.json("POST", m_base + "/step" + q, QJsonObject(), nullptr, 15000);
        else m_http.json("POST", m_base + "/steps" + q, QJsonObject{{"count", (int)count}}, nullptr, 15000);
        changed();
    }
    // Step over / out answer when the machine has stopped (a pool of the WebAPI runs them)
    void debug_step_over(ecatshim::CPU * c) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepover" + step_query(c), QJsonObject(), nullptr, 15000);
        changed();
    }
    void debug_step_out(ecatshim::CPU * c) override
    {
        drop_run_to();
        m_http.json("POST", m_base + "/stepout" + step_query(c), QJsonObject(), nullptr, 15000);
        changed();
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
        // was set at, whose answer may come only now
        drop_run_to();
        m_run_to_seq = m_seq.load();
        if (!user) {
            m_http.json("POST", m_base + "/breakpoints",
                        QJsonObject{{"type", "execution"}, {"address", (int)(address & 0xFFFF)}, {"group", kRunToGroup},
                                    {"note", "run to the cursor"}, {"cpu", cpu}});
            m_run_to_armed = true;
        }
        breakpoints_changed();
        m_arming_run_to = true;
        debug_run(true);
        m_arming_run_to = false;
    }
    unsigned int debug_serial() const override { return (unsigned int)m_seq.load(); }
    ecatshim::CPU * debug_stop_cpu() const override
    {
        // A card's breakpoint or step: its tab
        RemoteCardZ80 * c = card(const_cast<RemoteMachine *>(this)->snap().stop_cpu);
        return c ? static_cast<ecatshim::CPU *>(c) : m_cpu.get();
    }
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
    std::vector<std::shared_ptr<RemoteCardZ80>> m_cards;
    struct BreakpointList {
        std::vector<Breakpoint> list;
        uint64_t generation = 0;
        bool dirty = true;
    };
    std::map<QString, BreakpointList> m_bps;
    bool m_attached = false;
    std::set<QString> m_histories_ours;  // the histories the debugger turned on: off again when it goes
    bool m_debug_before = false;

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
        if (m_run_to_armed.exchange(false)) clear_run_to();
    }
    void clear_run_to()
    {
        remove_run_to(m_http, m_base);
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
            if (a.value("state").toString() == "paused" && m_seq.load() >= m_run_to_seq.load() + 2 &&
                m_run_to_armed.exchange(false))
                remove_run_to(wait, base);
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
    return status == 200;
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
    return status == 200;
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
