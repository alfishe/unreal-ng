// The GUI control plane of unreal-ng's programs (docs/emulator/design/gui-control): the daemon's `gui` keys
// (`{kind: gui, method, params?, target?}`) and its `gui.<path>` readouts. A program writes a discovery file per host
// (UNREAL_GUI_CONTROL_DIR, else <runtime dir>/unreal-ng/gui); the client takes the host that serves the daemon's
// WebAPI (its `endpoints`), talks newline-delimited JSON over the host's local socket (a Unix socket; a named pipe on
// Windows), and connects again after the program restarts.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sdb::gui
{

using json = nlohmann::json;

// One discovery file
struct HostEntry
{
    std::string id;
    long long pid = 0;
    std::string program;
    std::string socket;  // the local socket's path (a pipe name on Windows)
    std::vector<std::string> endpoints;
};

// UNREAL_GUI_CONTROL_DIR, else Qt's runtime location + /unreal-ng/gui (macOS ~/Library/Application Support, Linux
// $XDG_RUNTIME_DIR, Windows the home folder)
[[nodiscard]] std::string DiscoveryDirectory();
// The hosts of live processes in `dir`, by id
[[nodiscard]] std::vector<HostEntry> Hosts(const std::string& dir);
// Whether `endpoint` and `webapi` name the same WebAPI: scheme, host (localhost = 127.0.0.1) and port, no path
[[nodiscard]] bool SameEndpoint(const std::string& endpoint, const std::string& webapi);

// A call's answer: ok, else the plane's error code (not_found, bad_request, not_now, ...) and its message
struct Result
{
    bool ok = false;
    std::string error;
    std::string message;
    json body = json::object();
};

// One line of the protocol, both ways (tests: a fake host)
class Line
{
public:
    virtual ~Line() = default;
    virtual bool Send(const std::string& line) = 0;  // with its newline
    // the next line without its newline; false on a closed line or no line within `timeoutMs`
    virtual bool Receive(std::string& line, int timeoutMs) = 0;
};
// The local socket at `path`; null with `error` when it does not connect
[[nodiscard]] std::unique_ptr<Line> ConnectLocal(const std::string& path, std::string& error);

// Blocking calls on the GUI host that serves `webapi`; thread-safe (one call at a time)
class Client
{
public:
    using Connector = std::function<std::unique_ptr<Line>(std::string& error)>;
    // `dir` "" = DiscoveryDirectory()
    explicit Client(std::string webapi, std::string dir = {});
    // A fixed connection (tests): every reconnect calls `connect`
    explicit Client(Connector connect);

    // `target` "" = the plane resolves it (the one node with the command)
    Result Call(const std::string& method, const std::string& target, const json& params, int timeoutMs = 5000);
    // The host's tree and root state (`list`); null when no host answers
    json List(int timeoutMs = 2000);
    // Whether `iface` sits on a view of the host (its commands then take `iface@<instance>`); from the last List
    bool IsViewInterface(const std::string& iface);

private:
    Result CallLocked(const std::string& method, const std::string& target, const json& params, int timeoutMs);
    bool ConnectLocked(std::string& error);

    std::string _webapi, _dir;
    Connector _connect;
    std::mutex _m;
    std::unique_ptr<Line> _line;
    uint64_t _rid = 0;
    json _tree;  // the last List body
};

// `gui.metadata.labels` -> the root state's metadata.labels; null when it has none
[[nodiscard]] json ReadoutOf(const json& list, const std::string& path);

}  // namespace sdb::gui
