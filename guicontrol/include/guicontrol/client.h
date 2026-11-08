#pragma once

/// @file client.h
/// @brief A client of one host (design §4.9): the CLI, the tests, a GUI program driving another. Calls wait for their
/// answer by running a local event loop, so a host on the same thread (a test) keeps serving meanwhile.

#include "guicontrol/discovery.h"
#include "guicontrol/protocol.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QIODevice;

namespace guicontrol
{

class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject* parent = nullptr);
    ~Client() override;

    /// A host's local socket (discovery::Entry::socket); waits for its `welcome`
    bool Connect(const QString& socketName, int timeoutMs = 3000, QString* error = nullptr);
    /// A host's TCP listener
    bool ConnectTcp(const QString& address, quint16 port, int timeoutMs = 3000, QString* error = nullptr);
    bool IsConnected() const;
    void Close();

    /// The host's `welcome`: protocol, host, program, version, auth
    const QJsonObject& Welcome() const { return _welcome; }

    /// Runs `method` at `target` and waits; `timeout` when nothing came, `failed` when the connection went
    Result Call(const QString& method, const QString& target = {}, const QJsonObject& params = {},
                int timeoutMs = 5000);
    bool Subscribe(const QStringList& topics, int maxHz = 20, int timeoutMs = 3000);
    /// The next `event`, waiting up to `timeoutMs`
    std::optional<QJsonObject> NextEvent(int timeoutMs);
    /// Events received and not taken yet
    size_t PendingEvents() const { return _events.size(); }

signals:
    void eventReceived(const QJsonObject& event);
    void disconnected();

private:
    bool Attach(QIODevice* device, int timeoutMs, QString* error);
    void Read();
    /// Runs the event loop until `done` or the time is up
    bool WaitFor(const std::function<bool()>& done, int timeoutMs);
    QJsonObject Request(QJsonObject message, int timeoutMs, bool& answered);

    QIODevice* _device = nullptr;
    QByteArray _buffer;
    QJsonObject _welcome;
    std::map<qint64, QJsonObject> _answers;
    std::deque<QJsonObject> _events;
    qint64 _nextRid = 1;
    bool _connected = false;
};

/// One host's answer to `resolve`
struct HostChoice
{
    discovery::Entry entry;
    Result resolved;  ///< body: host, target, focus_at
};

/// Which host a call without an explicit host goes to (design §4.5): the only one that resolves the target, else the
/// one whose node took focus last; `ambiguous` when several resolve and none has focus, `not_found` when none does
Result ChooseHost(const std::vector<HostChoice>& choices, size_t& chosen);

}  // namespace guicontrol
