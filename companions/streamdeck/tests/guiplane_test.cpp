// The daemon's GUI control plane client (sdb/guiplane.h) against a fake host: the call and its answer between events,
// a refusal with its message, a reconnect after the program restarted, the host chosen by its WebAPI endpoint from
// the discovery files, the root state's readouts, a real Unix socket (POSIX), and the `gui` kind of a scenario step.
#include "sdb/guiplane.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#endif

#include "check.h"
#include "sdb/actions.h"
#include "sdb/scenario.h"
#include "sdb/surfacereadouts.h"

using namespace sdb;
using json = nlohmann::json;

namespace
{

// A host in memory: answers each call from `answer`, sends an event before every result
struct FakeHost
{
    std::vector<json> calls;
    int connects = 0;
    bool closeNext = false;  // the program restarted: the next send fails
    std::function<json(const json& call)> answer = [](const json&) { return json{{"ok", true}, {"body", json::object()}}; };

    struct FakeLine final : gui::Line
    {
        FakeHost* host;
        std::deque<std::string> out;
        explicit FakeLine(FakeHost* h) : host(h)
        {
            out.push_back(json{{"op", "welcome"}, {"protocol", 1}, {"host", "4242"}}.dump());
        }
        bool Send(const std::string& line) override
        {
            if (host->closeNext)
            {
                host->closeNext = false;
                return false;
            }
            const json call = json::parse(line);
            host->calls.push_back(call);
            out.push_back(json{{"op", "event"}, {"topic", "state"}, {"target", "debugger/memory"}}.dump());
            json result = host->answer(call);
            result["op"] = "result";
            result["rid"] = call["rid"];
            out.push_back(result.dump());
            return true;
        }
        bool Receive(std::string& line, int) override
        {
            if (out.empty())
                return false;
            line = out.front();
            out.pop_front();
            return true;
        }
    };

    gui::Client::Connector Connector()
    {
        return [this](std::string&) -> std::unique_ptr<gui::Line> {
            ++connects;
            return std::make_unique<FakeLine>(this);
        };
    }
};

json ListBody()
{
    return json{{"id", "4242"},
                {"state", {{"metadata", {{"labels", true}, {"regions", false}}}, {"osd_keyboard", false}}},
                {"windows",
                 json::array({{{"id", "debugger"},
                               {"interfaces", json::array({"window"})},
                               {"views", json::array({{{"id", "memory"}, {"interfaces", json::array({"memory"})}}})}}})}};
}

void TestCalls()
{
    FakeHost host;
    host.answer = [](const json& call) {
        if (call["method"] == "list")
            return json{{"ok", true}, {"body", ListBody()}};
        if (call["method"] == "memory.goto" && !call.contains("params"))
            return json{{"ok", false}, {"error", "bad_request"}, {"message", "memory.goto: missing addr"}};
        return json{{"ok", true}, {"body", {{"cursor", "0x6A00"}}}};
    };
    gui::Client client(host.Connector());

    const gui::Result moved = client.Call("memory.goto", "memory@abc", {{"addr", "0x6A00"}});
    CHECK(moved.ok);
    CHECK(moved.body.value("cursor", "") == "0x6A00");  // the event before the result is skipped
    CHECK(host.calls.back()["target"] == "memory@abc");
    CHECK(host.calls.back()["params"]["addr"] == "0x6A00");

    const gui::Result refused = client.Call("memory.goto", "", json::object());
    CHECK(!refused.ok);
    CHECK(refused.error == "bad_request");
    CHECK(refused.message == "memory.goto: missing addr");
    CHECK(!host.calls.back().contains("target"));  // no target: the plane resolves it
    CHECK_EQ(host.connects, 1);

    host.closeNext = true;  // the program restarted
    CHECK(client.Call("debugger.open", "", json::object()).ok);
    CHECK_EQ(host.connects, 2);

    CHECK(client.IsViewInterface("memory"));
    CHECK(!client.IsViewInterface("debugger"));
    const json list = client.List();
    CHECK(gui::ReadoutOf(list, "gui.metadata.labels") == true);
    CHECK(gui::ReadoutOf(list, "gui.osd_keyboard") == false);
    CHECK(gui::ReadoutOf(list, "gui.metadata.nothing").is_null());
    CHECK(gui::ReadoutOf(list, "toggle.labels").is_null());
}

void TestDiscovery()
{
    CHECK(gui::SameEndpoint("http://127.0.0.1:8090", "http://localhost:8090/"));
    CHECK(gui::SameEndpoint("http://127.0.0.1:8090/api", "HTTP://127.0.0.1:8090"));
    CHECK(!gui::SameEndpoint("http://127.0.0.1:8090", "http://127.0.0.1:8091"));

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sdb-guiplane-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto write = [&](const std::string& id, long long pid, const std::string& endpoint) {
        std::ofstream(dir / (id + ".json"))
            << json{{"id", id}, {"pid", pid}, {"program", "unreal-qt"}, {"socket", (dir / ("s" + id)).string()},
                    {"endpoints", json::array({endpoint})}}
                   .dump();
    };
#ifdef _WIN32
    const long long self = static_cast<long long>(GetCurrentProcessId());
#else
    const long long self = static_cast<long long>(::getpid());
#endif
    write("1", self, "http://127.0.0.1:8090");
    write("2", self, "http://127.0.0.1:8091");
    write("3", 0, "http://127.0.0.1:8090");  // a process that is gone
    const std::vector<gui::HostEntry> hosts = gui::Hosts(dir.string());
    CHECK_EQ(hosts.size(), 2u);

    // Nobody listens on the sockets: the error names the WebAPI and what is there
    gui::Client none("http://127.0.0.1:9999", dir.string());
    const gui::Result r = none.Call("debugger.open", "", json::object());
    CHECK(!r.ok);
    CHECK(r.error == "not_found");
    CHECK(r.message.find("http://127.0.0.1:9999") != std::string::npos);
    CHECK(r.message.find("unreal-qt 1") != std::string::npos);
    std::filesystem::remove_all(dir);
}

#ifndef _WIN32
// A real Unix socket and a host thread that answers one call
void TestUnixSocket()
{
    const std::string path = (std::filesystem::temp_directory_path() / "sdb-guiplane.sock").string();
    ::unlink(path.c_str());
    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    CHECK(::bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    CHECK(::listen(server, 1) == 0);
    std::thread host([server] {
        const int fd = ::accept(server, nullptr, nullptr);
        const std::string welcome = json{{"op", "welcome"}}.dump() + "\n";
        (void)::send(fd, welcome.data(), welcome.size(), 0);
        std::string in;
        char c = 0;
        while (::recv(fd, &c, 1, 0) == 1 && c != '\n')
            in += c;
        const json call = json::parse(in);
        const std::string out =
            json{{"op", "result"}, {"rid", call["rid"]}, {"ok", true}, {"body", {{"method", call["method"]}}}}.dump() + "\n";
        (void)::send(fd, out.data(), out.size(), 0);
        ::close(fd);
    });
    std::string error;
    auto line = gui::ConnectLocal(path, error);
    CHECK(line != nullptr);
    gui::Client client([&](std::string&) { return std::move(line); });
    const gui::Result r = client.Call("debugger.open", "", json::object(), 2000);
    CHECK(r.ok);
    CHECK(r.body.value("method", "") == "debugger.open");
    host.join();
    ::close(server);
    ::unlink(path.c_str());
}
#endif

// The `gui` kind: the step goes to the host as written, with the phase of a held key
struct GuiHost final : scn::ActionHost
{
    std::vector<json> steps;
    HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override
    {
        return {};
    }
    HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
    {
        return {};
    }
    std::vector<int> phases;
    scn::ActionResult RunGui(const json& step, int phase) override
    {
        phases.push_back(phase);
        if (phase < 0)
            return {true, "released"};
        steps.push_back(step);
        return {true, "gui " + step.value("method", "")};
    }
    int Shell(const std::string&, int, std::string&) override
    {
        return 0;
    }
    bool Open(const std::string&) override
    {
        return true;
    }
    bool Keys(const std::string&, const std::string&, std::string&) override
    {
        return true;
    }
    void Navigate(const json&) override {}
    void Brightness(const json&) override {}
    void SleepMs(int) override {}
    void Note(const std::string&) override {}
};

void TestGuiKind()
{
    GuiHost host;
    const scn::Action page{"gui", json{{"kind", "gui"}, {"method", "memory.page"}, {"params", {{"n", "$ticks"}}}}};
    CHECK(scn::Execute(page, host, json{{"ticks", -2}}).ok);
    CHECK_EQ(host.steps.size(), 1u);
    CHECK(host.steps[0]["params"]["n"] == -2);
    CHECK(scn::Execute(page, host, json{{"phase", -1}, {"ticks", 1}}).ok);
    CHECK_EQ(host.steps.size(), 1u);
    CHECK(host.phases.back() == -1);  // the release goes to the host: it ends a held key's repeat

    CHECK(scn::IsKnownReadoutPath("gui.metadata.labels"));
}

}  // namespace

void TestGuiPlane()
{
    TestCalls();
    TestDiscovery();
#ifndef _WIN32
    TestUnixSocket();
#endif
    TestGuiKind();
}
