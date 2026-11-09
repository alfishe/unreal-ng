#pragma once

/// @file host.h
/// @brief The GUI control plane's host (design §4): one per GUI process. It keeps the tree of nodes, serves clients on
/// a local socket (always) and on TCP (optional, all interfaces, no token for now), writes the discovery file,
/// resolves targets (§4.5), pushes the topics coalesced, and dispatches in process for the program's own controller
/// bindings (§4.7).
///
/// Everything runs on the thread that owns the host (the GUI thread): the servers are non-blocking, a command runs
/// there and answers now or later through its Done; no thread is added for the host or its clients.

#include "guicontrol/node.h"
#include "guicontrol/protocol.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QLocalServer;
class QTcpServer;

namespace guicontrol
{

struct HostOptions
{
    QString program;  ///< "unreal-qt"
    QString version;
    /// The discovery directory; "" = discovery::Directory()
    QString directory;
    /// -1: no TCP; 0: any free port; else the port. FromEnvironment reads UNREAL_GUI_CONTROL_PORT
    int tcpPort = -1;
    /// "" = all interfaces. FromEnvironment reads UNREAL_GUI_CONTROL_BIND
    QString tcpBind;
    /// A subscriber whose unsent bytes are over this when a push comes is dropped
    qint64 backlogLimit = 4 * 1024 * 1024;
    /// The emulator endpoints the program is bound to, asked on every `list` and on RefreshDiscovery
    std::function<QJsonArray()> endpoints;

    /// The TCP settings from the environment over these options (an invalid port is ignored)
    HostOptions& FromEnvironment();
};

class Host : public QObject
{
    Q_OBJECT

public:
    explicit Host(HostOptions options, QObject* parent = nullptr);
    ~Host() override;

    /// Listens and writes the discovery file; false with `error` when the local socket cannot listen (a TCP failure
    /// is logged and the host serves locally)
    bool Start(QString* error = nullptr);
    /// Closes every client, stops listening, removes the discovery file
    void Stop();
    bool IsRunning() const;

    /// The host's id: the pid, or "<pid>-<n>" for another host in the same process
    const QString& Id() const { return _id; }
    QString SocketName() const;
    /// 0 when TCP is off or failed
    quint16 TcpPort() const;
    QString DiscoveryFile() const { return _discoveryFile; }
    /// Writes the discovery file again (the endpoints changed: the WebAPI came up)
    void RefreshDiscovery();

    /// The root: the host's own interfaces and commands; windows are its children
    Node& Root() { return *_root; }

    /// The user gave keyboard focus to `node` for `interfaceName` (design §4.5): recorded with the wall-clock time,
    /// pushed on the `focus` topic. Null clears it
    void SetFocus(const QString& interfaceName, const Node* node);
    /// {interface: {target, at}}
    QJsonObject FocusRecords() const;

    /// Runs `method` at `target` as a client's call would (target resolution, `activity`), in process: the
    /// program's own bindings (design §4.7)
    void Invoke(const QString& method, const QString& target, const QJsonObject& params, const QString& client,
                Done done);

    /// A declared command's parameters: unknown or missing ones refused (bad_request naming them and what the command
    /// takes; body `unknown`, `missing`). Commands without a declaration pass
    static Result CheckParams(const QString& method, const QJsonObject& params);

    /// What a target names (design §4.5): the node, or a failure (not_found, ambiguous with the candidates)
    struct Resolution
    {
        Node* node = nullptr;
        Result failure;
        qint64 focusAt = 0;  ///< when the node took focus for the method's interface; 0 when it is not the focused one
    };
    Resolution Resolve(const QString& method, const QString& target);

    /// The `list` body: the host, its tree, the focus records, the endpoints
    QJsonObject List() const;

    int ClientCount() const;

signals:
    /// A client went: "closed", "slow_consumer", "bad_request"
    void clientDropped(const QString& reason);

private:
    friend class Node;
    class Connection;

    void NodeAdded(const Node& node);
    void NodeRemoved(const QString& path);
    void NodeChanged(const Node& node);

    void Accept(QIODevice* device, const QString& peer);
    void Handle(Connection& connection, const QJsonObject& message);
    void Publish(const QString& topic, const QJsonObject& event);
    void PublishState(const QString& path);
    void Drop(Connection* connection, const QString& reason);
    QJsonObject Welcome() const;
    void WriteDiscovery();

    HostOptions _options;
    QString _id;
    qint64 _started = 0;
    std::unique_ptr<Node> _root;
    QLocalServer* _local = nullptr;
    QTcpServer* _tcp = nullptr;
    QString _discoveryFile;
    std::vector<Connection*> _connections;
    struct Focus
    {
        QString path;
        qint64 at = 0;
    };
    QHash<QString, Focus> _focus;
};

}  // namespace guicontrol
