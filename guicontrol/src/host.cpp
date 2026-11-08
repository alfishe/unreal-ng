#include "guicontrol/host.h"

#include "guicontrol/discovery.h"
#include "guicontrol/interfaces.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <utility>

namespace guicontrol
{

namespace
{
/// A line longer than this is not a message: the client is dropped
constexpr qsizetype kMaxLine = 1024 * 1024;
constexpr int kDefaultMaxHz = 20;
constexpr int kMaxHz = 50;

/// The n-th host of this process: the first is the pid, the next ones "<pid>-<n>"
QString NextHostId()
{
    static std::atomic<int> hosts{0};
    const int n = hosts.fetch_add(1);
    const QString pid = QString::number(QCoreApplication::applicationPid());
    return n == 0 ? pid : QStringLiteral("%1-%2").arg(pid).arg(n);
}

void Walk(Node& node, const std::function<void(Node&)>& visit)
{
    visit(node);
    for (const std::unique_ptr<Node>& child : node.Children())
        Walk(*child, visit);
}
}  // namespace

HostOptions& HostOptions::FromEnvironment()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("UNREAL_GUI_CONTROL_PORT", &ok);
    if (ok && port >= 0 && port <= 65535)
        tcpPort = port;
    const QString bind = qEnvironmentVariable("UNREAL_GUI_CONTROL_BIND");
    if (!bind.isEmpty())
        tcpBind = bind;
    return *this;
}

/// One client: its socket, its subscriptions and the state pushes waiting for its rate
class Host::Connection : public QObject
{
public:
    Connection(QIODevice* device, QString peer, QObject* parent)
        : QObject(parent), device(device), peer(std::move(peer))
    {
        device->setParent(this);
        flush.setSingleShot(true);
    }

    void Send(const QJsonObject& message) { device->write(Encode(message)); }

    QIODevice* device;
    QString peer;
    QByteArray buffer;
    QSet<QString> topics;
    int maxHz = kDefaultMaxHz;
    QSet<QString> pending;  ///< paths whose state waits for the rate
    QElapsedTimer lastFlush;
    QTimer flush;
};

Host::Host(HostOptions options, QObject* parent)
    : QObject(parent), _options(std::move(options)), _id(NextHostId()),
      _started(QDateTime::currentMSecsSinceEpoch()), _root(new Node(this, nullptr, QString()))
{
}

Host::~Host()
{
    Stop();
    // The tree goes without notifications: no client is left to tell
    _root->_host = nullptr;
    Walk(*_root, [](Node& node) { node._host = nullptr; });
}

bool Host::Start(QString* error)
{
    if (IsRunning())
        return true;

    const QString directory = _options.directory.isEmpty() ? discovery::Directory() : _options.directory;
    if (!QDir().mkpath(directory))
    {
        if (error != nullptr)
            *error = QStringLiteral("cannot create %1").arg(directory);
        return false;
    }
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // A host killed by a signal leaves its file behind: clients skip it, the next host removes it
    discovery::RemoveStale(directory);

    // A random suffix: a stale socket of a crashed host never blocks the name, another user's never collides
    const QString name = QStringLiteral("unreal-gui-%1-%2")
                             .arg(_id)
                             .arg(QRandomGenerator::global()->generate() & 0xFFFFFF, 6, 16, QChar('0'));
    _local = new QLocalServer(this);
    _local->setSocketOptions(QLocalServer::UserAccessOption);
    if (!_local->listen(name))
    {
        if (error != nullptr)
            *error = _local->errorString();
        delete _local;
        _local = nullptr;
        return false;
    }
    connect(_local, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* socket = _local->nextPendingConnection())
            Accept(socket, QStringLiteral("local"));
    });

    if (_options.tcpPort >= 0)
    {
        _tcp = new QTcpServer(this);
        const QHostAddress address =
            _options.tcpBind.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(_options.tcpBind);
        if (_tcp->listen(address, static_cast<quint16>(_options.tcpPort)))
        {
            connect(_tcp, &QTcpServer::newConnection, this, [this]() {
                while (QTcpSocket* socket = _tcp->nextPendingConnection())
                    Accept(socket, QStringLiteral("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort()));
            });
            qInfo().noquote() << QStringLiteral("GUI control: TCP on %1:%2 without authentication")
                                     .arg(address.toString())
                                     .arg(_tcp->serverPort());
        }
        else
        {
            qWarning().noquote() << QStringLiteral("GUI control: TCP port %1 not opened: %2")
                                        .arg(_options.tcpPort)
                                        .arg(_tcp->errorString());
            delete _tcp;
            _tcp = nullptr;
        }
    }

    _discoveryFile = directory + '/' + _id + QStringLiteral(".json");
    WriteDiscovery();
    return true;
}

void Host::Stop()
{
    for (Connection* connection : std::vector<Connection*>(_connections))
        Drop(connection, QStringLiteral("closed"));
    if (_local != nullptr)
    {
        _local->close();
        delete _local;
        _local = nullptr;
    }
    if (_tcp != nullptr)
    {
        _tcp->close();
        delete _tcp;
        _tcp = nullptr;
    }
    if (!_discoveryFile.isEmpty())
    {
        QFile::remove(_discoveryFile);
        _discoveryFile.clear();
    }
}

bool Host::IsRunning() const
{
    return _local != nullptr;
}

QString Host::SocketName() const
{
    return _local != nullptr ? _local->fullServerName() : QString();
}

quint16 Host::TcpPort() const
{
    return _tcp != nullptr ? _tcp->serverPort() : 0;
}

void Host::RefreshDiscovery()
{
    if (IsRunning())
        WriteDiscovery();
}

void Host::WriteDiscovery()
{
    discovery::Entry entry;
    entry.id = _id;
    entry.pid = QCoreApplication::applicationPid();
    entry.program = _options.program;
    entry.version = _options.version;
    entry.protocol = kProtocolVersion;
    entry.socket = SocketName();
    entry.tcpPort = TcpPort();
    entry.tcpBind = _options.tcpBind;
    entry.started = _started;
    if (_options.endpoints)
        entry.endpoints = _options.endpoints();
    QString error;
    if (!discovery::Write(_discoveryFile, entry, &error))
        qWarning().noquote() << QStringLiteral("GUI control: discovery file %1 not written: %2").arg(_discoveryFile, error);
}

void Host::SetFocus(const QString& interfaceName, const Node* node)
{
    QJsonObject event{{"op", "event"}, {"topic", topics::kFocus}, {"interface", interfaceName}};
    if (node == nullptr)
    {
        if (!_focus.remove(interfaceName))
            return;
        event["target"] = QJsonValue::Null;
    }
    else
    {
        Focus focus{node->Path(), QDateTime::currentMSecsSinceEpoch()};
        _focus[interfaceName] = focus;
        event["target"] = focus.path;
        event["at"] = focus.at;
    }
    Publish(topics::kFocus, event);
}

QJsonObject Host::FocusRecords() const
{
    QJsonObject records;
    for (auto it = _focus.begin(); it != _focus.end(); ++it)
        records[it.key()] = QJsonObject{{"target", it->path}, {"at", it->at}};
    return records;
}

Host::Resolution Host::Resolve(const QString& method, const QString& target)
{
    Resolution resolution;
    QString interfaceName = InterfaceOf(method);
    std::vector<Node*> candidates;
    const auto withMethod = [&](Node& node) {
        if (node.HasCommand(method))
            candidates.push_back(&node);
    };

    QString rest = target;
    if (rest == _id)
        rest.clear();
    else if (rest.startsWith(_id + '/'))
        rest = rest.mid(_id.size() + 1);

    if (target.isEmpty())
        Walk(*_root, withMethod);
    else if (rest.isEmpty() || rest.contains('/') ||
             (_root->Child(rest) != nullptr && _root->Child(rest)->HasCommand(method)))
    {
        // A path: the node itself. A bare word is a window when that window has the command, else an interface
        // ("debugger" is both the debugger window and the host's debugger interface)
        resolution.node = _root->Find(rest);
        if (resolution.node == nullptr)
            resolution.failure = Result::Failure(errors::kNotFound, QStringLiteral("no node %1").arg(target));
    }
    else
    {
        // An interface, maybe with the instance its view shows
        const qsizetype at = rest.indexOf('@');
        const QString name = at < 0 ? rest : rest.left(at);
        const QString instance = at < 0 ? QString() : rest.mid(at + 1);
        interfaceName = name;
        Walk(*_root, [&](Node& node) {
            if (node.Implements(name) && node.HasCommand(method))
                candidates.push_back(&node);
        });
        if (!instance.isEmpty())
        {
            std::vector<Node*> showing;
            std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(showing),
                         [&instance](const Node* node) { return node->InstanceId() == instance; });
            // No view shows that machine: the focused one of the interface (design §4.5)
            if (!showing.empty())
                candidates = std::move(showing);
        }
    }

    if (resolution.node == nullptr && resolution.failure.ok)
    {
        const auto focus = _focus.constFind(interfaceName);
        if (candidates.empty())
            resolution.failure = Result::Failure(
                errors::kNotFound, target.isEmpty() ? QStringLiteral("nothing here has %1").arg(method)
                                                    : QStringLiteral("%1 names nothing with %2").arg(target, method));
        else if (candidates.size() == 1)
            resolution.node = candidates.front();
        else if (focus != _focus.constEnd())
        {
            const auto it = std::find_if(candidates.begin(), candidates.end(),
                                         [&focus](const Node* node) { return node->Path() == focus->path; });
            if (it != candidates.end())
                resolution.node = *it;
        }
        if (resolution.node == nullptr && resolution.failure.ok)
        {
            QJsonArray paths;
            for (const Node* node : candidates)
                paths.append(node->Path());
            resolution.failure = Result::Failure(errors::kAmbiguous,
                                                 QStringLiteral("%1 matches %2 nodes and none has focus")
                                                     .arg(target.isEmpty() ? method : target)
                                                     .arg(candidates.size()),
                                                 QJsonObject{{"candidates", paths}});
        }
    }

    if (resolution.node != nullptr)
    {
        const auto focus = _focus.constFind(interfaceName);
        if (focus != _focus.constEnd() && focus->path == resolution.node->Path())
            resolution.focusAt = focus->at;
    }
    return resolution;
}

void Host::Invoke(const QString& method, const QString& target, const QJsonObject& params, const QString& client,
                  Done done)
{
    const Resolution resolution = Resolve(method, target);
    if (resolution.node == nullptr)
    {
        done(resolution.failure);
        return;
    }
    const QString path = resolution.node->Path();
    QPointer<Host> self(this);
    resolution.node->Invoke(Call{method, params, client},
                            [self, method, path, client, done = std::move(done)](Result result) {
                                if (self)
                                    self->Publish(topics::kActivity,
                                                  QJsonObject{{"op", "event"},
                                                              {"topic", topics::kActivity},
                                                              {"method", method},
                                                              {"target", path},
                                                              {"client", client},
                                                              {"ok", result.ok}});
                                done(std::move(result));
                            });
}

QJsonObject Host::Welcome() const
{
    return QJsonObject{{"op", "welcome"},          {"protocol", kProtocolVersion}, {"host", _id},
                       {"program", _options.program}, {"version", _options.version},  {"auth", false}};
}

QJsonObject Host::List() const
{
    QJsonObject body = _root->Describe();
    body["id"] = _id;
    body["host"] = _id;
    body["pid"] = QCoreApplication::applicationPid();
    body["program"] = _options.program;
    body["version"] = _options.version;
    body["protocol"] = kProtocolVersion;
    body["focus"] = FocusRecords();
    body["endpoints"] = _options.endpoints ? _options.endpoints() : QJsonArray();
    return body;
}

int Host::ClientCount() const
{
    return static_cast<int>(_connections.size());
}

void Host::Accept(QIODevice* device, const QString& peer)
{
    auto* connection = new Connection(device, peer, this);
    _connections.push_back(connection);
    connect(&connection->flush, &QTimer::timeout, connection, [this, connection]() {
        const QSet<QString> paths = std::exchange(connection->pending, {});
        connection->lastFlush.start();
        for (const QString& path : paths)
        {
            const Node* node = _root->Find(path);
            if (node == nullptr)
                continue;
            if (connection->device->bytesToWrite() > _options.backlogLimit)
            {
                Drop(connection, QStringLiteral("slow_consumer"));
                return;
            }
            connection->Send(QJsonObject{
                {"op", "event"}, {"topic", topics::kState}, {"target", path}, {"state", node->State()}});
        }
    });
    connect(device, &QIODevice::readyRead, connection, [this, connection]() {
        connection->buffer.append(connection->device->readAll());
        qsizetype end;
        while ((end = connection->buffer.indexOf('\n')) >= 0)
        {
            const QByteArray line = connection->buffer.left(end).trimmed();
            connection->buffer.remove(0, end + 1);
            if (line.isEmpty())
                continue;
            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (!document.isObject())
            {
                connection->Send(QJsonObject{{"op", "error"},
                                             {"error", errors::kBadRequest},
                                             {"message", QStringLiteral("not a JSON object: %1")
                                                             .arg(parseError.errorString())}});
                continue;
            }
            Handle(*connection, document.object());
            // Handle may have dropped it
            if (std::find(_connections.begin(), _connections.end(), connection) == _connections.end())
                return;
        }
        if (connection->buffer.size() > kMaxLine)
            Drop(connection, QStringLiteral("bad_request"));
    });
    if (auto* local = qobject_cast<QLocalSocket*>(device))
        connect(local, &QLocalSocket::disconnected, connection,
                [this, connection]() { Drop(connection, QStringLiteral("closed")); });
    else if (auto* tcp = qobject_cast<QTcpSocket*>(device))
        connect(tcp, &QTcpSocket::disconnected, connection,
                [this, connection]() { Drop(connection, QStringLiteral("closed")); });
    connection->Send(Welcome());
}

void Host::Handle(Connection& connection, const QJsonObject& message)
{
    const QString op = message.value("op").toString();
    const QJsonValue rid = message.value("rid");
    if (op == QLatin1String("call"))
    {
        const QString method = message.value("method").toString();
        const QString target = message.value("target").toString();
        const QJsonObject params = message.value("params").toObject();
        QPointer<Connection> alive(&connection);
        const auto reply = [alive, rid](const Result& result) {
            if (!alive)
                return;
            QJsonObject answer = result.ToJson();
            answer["op"] = "result";
            answer["rid"] = rid;
            alive->Send(answer);
        };
        if (method.isEmpty())
            reply(Result::Failure(errors::kBadRequest, QStringLiteral("a call needs a method")));
        else if (method == QLatin1String("list"))
            reply(Result::Success(List()));
        else if (method == QLatin1String("resolve"))
        {
            // Which node a target names here, and since when it has focus: a client choosing between hosts
            const Resolution resolution =
                Resolve(params.value("method").toString(), params.value("target").toString());
            if (resolution.node == nullptr)
                reply(resolution.failure);
            else
                reply(Result::Success(QJsonObject{{"host", _id},
                                                  {"target", resolution.node->Path()},
                                                  {"focus_at", resolution.focusAt}}));
        }
        else
            Invoke(method, target, params, connection.peer, reply);
        return;
    }
    if (op == QLatin1String("subscribe") || op == QLatin1String("unsubscribe"))
    {
        const bool subscribe = op == QLatin1String("subscribe");
        QJsonArray names;
        for (const QJsonValue& topic : message.value("topics").toArray())
        {
            const QString name = topic.toString();
            if (name != QLatin1String(topics::kState) && name != QLatin1String(topics::kNodes) &&
                name != QLatin1String(topics::kFocus) && name != QLatin1String(topics::kActivity))
            {
                connection.Send(QJsonObject{{"op", "error"},
                                            {"rid", rid},
                                            {"error", errors::kBadRequest},
                                            {"message", QStringLiteral("no topic %1").arg(name)}});
                return;
            }
            if (subscribe)
                connection.topics.insert(name);
            else
                connection.topics.remove(name);
            names.append(name);
        }
        if (subscribe && message.contains("max_hz"))
            connection.maxHz = std::clamp(message.value("max_hz").toInt(kDefaultMaxHz), 1, kMaxHz);
        if (!connection.topics.contains(topics::kState))
            connection.pending.clear();
        connection.Send(QJsonObject{{"op", subscribe ? "subscribed" : "unsubscribed"},
                                    {"rid", rid},
                                    {"topics", names},
                                    {"max_hz", connection.maxHz}});
        return;
    }
    if (op == QLatin1String("auth"))
    {
        // No token for now (owner decision 2026-11-07): every client is let in
        connection.Send(QJsonObject{{"op", "auth"}, {"rid", rid}, {"ok", true}});
        return;
    }
    connection.Send(QJsonObject{{"op", "error"},
                                {"rid", rid},
                                {"error", errors::kBadRequest},
                                {"message", QStringLiteral("unknown op %1").arg(op)}});
}

void Host::Publish(const QString& topic, const QJsonObject& event)
{
    std::vector<Connection*> slow;
    for (Connection* connection : _connections)
    {
        if (!connection->topics.contains(topic))
            continue;
        if (connection->device->bytesToWrite() > _options.backlogLimit)
        {
            slow.push_back(connection);
            continue;
        }
        connection->Send(event);
    }
    for (Connection* connection : slow)
        Drop(connection, QStringLiteral("slow_consumer"));
}

void Host::PublishState(const QString& path)
{
    for (Connection* connection : _connections)
    {
        if (!connection->topics.contains(topics::kState))
            continue;
        connection->pending.insert(path);
        if (connection->flush.isActive())
            continue;
        const qint64 interval = 1000 / connection->maxHz;
        const qint64 since = connection->lastFlush.isValid() ? connection->lastFlush.elapsed() : interval;
        connection->flush.start(static_cast<int>(std::max<qint64>(0, interval - since)));
    }
}

void Host::Drop(Connection* connection, const QString& reason)
{
    const auto it = std::find(_connections.begin(), _connections.end(), connection);
    if (it == _connections.end())
        return;
    _connections.erase(it);
    connection->flush.stop();
    connection->device->disconnect(connection);
    if (auto* local = qobject_cast<QLocalSocket*>(connection->device))
        local->abort();
    else if (auto* tcp = qobject_cast<QTcpSocket*>(connection->device))
        tcp->abort();
    connection->deleteLater();
    emit clientDropped(reason);
}

void Host::NodeAdded(const Node& node)
{
    Publish(topics::kNodes, QJsonObject{{"op", "event"}, {"topic", topics::kNodes}, {"added", node.Path()}});
}

void Host::NodeRemoved(const QString& path)
{
    for (auto it = _focus.begin(); it != _focus.end();)
    {
        if (it->path == path || it->path.startsWith(path + '/'))
        {
            const QString interfaceName = it.key();
            it = _focus.erase(it);
            Publish(topics::kFocus, QJsonObject{{"op", "event"},
                                               {"topic", topics::kFocus},
                                               {"interface", interfaceName},
                                               {"target", QJsonValue::Null}});
        }
        else
            ++it;
    }
    for (Connection* connection : _connections)
        connection->pending.remove(path);
    Publish(topics::kNodes, QJsonObject{{"op", "event"}, {"topic", topics::kNodes}, {"removed", path}});
}

void Host::NodeChanged(const Node& node)
{
    PublishState(node.Path());
}

}  // namespace guicontrol
