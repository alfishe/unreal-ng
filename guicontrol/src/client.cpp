#include "guicontrol/client.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTimer>

namespace guicontrol
{

Client::Client(QObject* parent) : QObject(parent)
{
}

Client::~Client()
{
    Close();
}

bool Client::Connect(const QString& socketName, int timeoutMs, QString* error)
{
    Close();
    auto* socket = new QLocalSocket(this);
    connect(socket, &QLocalSocket::disconnected, this, [this]() {
        _connected = false;
        emit disconnected();
    });
    socket->connectToServer(socketName);
    return Attach(socket, timeoutMs, error);
}

bool Client::ConnectTcp(const QString& address, quint16 port, int timeoutMs, QString* error)
{
    Close();
    auto* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::disconnected, this, [this]() {
        _connected = false;
        emit disconnected();
    });
    socket->connectToHost(address, port);
    return Attach(socket, timeoutMs, error);
}

bool Client::Attach(QIODevice* device, int timeoutMs, QString* error)
{
    _device = device;
    _connected = true;
    connect(device, &QIODevice::readyRead, this, &Client::Read);
    if (!WaitFor([this]() { return !_welcome.isEmpty() || !_connected; }, timeoutMs) || _welcome.isEmpty())
    {
        if (error != nullptr)
            *error = _device->errorString().isEmpty() ? QStringLiteral("no welcome from the host")
                                                      : _device->errorString();
        Close();
        return false;
    }
    return true;
}

bool Client::IsConnected() const
{
    return _device != nullptr && _connected;
}

void Client::Close()
{
    if (_device == nullptr)
        return;
    _device->disconnect(this);
    if (auto* local = qobject_cast<QLocalSocket*>(_device))
        local->abort();
    else if (auto* tcp = qobject_cast<QTcpSocket*>(_device))
        tcp->abort();
    _device->deleteLater();
    _device = nullptr;
    _connected = false;
    _buffer.clear();
    _welcome = {};
    _answers.clear();
    _events.clear();
}

void Client::Read()
{
    _buffer.append(_device->readAll());
    qsizetype end;
    while ((end = _buffer.indexOf('\n')) >= 0)
    {
        const QJsonDocument document = QJsonDocument::fromJson(_buffer.left(end));
        _buffer.remove(0, end + 1);
        if (!document.isObject())
            continue;
        const QJsonObject message = document.object();
        const QString op = message.value("op").toString();
        if (op == QLatin1String("welcome"))
            _welcome = message;
        else if (op == QLatin1String("event"))
        {
            _events.push_back(message);
            emit eventReceived(message);
        }
        else if (message.contains("rid"))
            _answers[message.value("rid").toInteger()] = message;
    }
}

bool Client::WaitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        const qint64 left = timeoutMs - clock.elapsed();
        if (left <= 0)
            return false;
        QEventLoop loop;
        QTimer::singleShot(static_cast<int>(left), &loop, &QEventLoop::quit);
        const QMetaObject::Connection read = connect(_device, &QIODevice::readyRead, &loop, &QEventLoop::quit,
                                                     Qt::QueuedConnection);
        const QMetaObject::Connection gone = connect(this, &Client::disconnected, &loop, &QEventLoop::quit,
                                                     Qt::QueuedConnection);
        loop.exec();
        disconnect(read);
        disconnect(gone);
    }
    return true;
}

QJsonObject Client::Request(QJsonObject message, int timeoutMs, bool& answered)
{
    answered = false;
    if (!IsConnected())
        return {};
    const qint64 rid = _nextRid++;
    message["rid"] = rid;
    _device->write(Encode(message));
    answered = WaitFor([this, rid]() { return _answers.count(rid) != 0 || !_connected; }, timeoutMs) &&
               _answers.count(rid) != 0;
    if (!answered)
        return {};
    QJsonObject answer = _answers[rid];
    _answers.erase(rid);
    return answer;
}

Result Client::Call(const QString& method, const QString& target, const QJsonObject& params, int timeoutMs)
{
    QJsonObject message{{"op", "call"}, {"method", method}};
    if (!target.isEmpty())
        message["target"] = target;
    if (!params.isEmpty())
        message["params"] = params;
    bool answered = false;
    const QJsonObject answer = Request(message, timeoutMs, answered);
    if (!answered)
        return Result::Failure(IsConnected() ? errors::kTimeout : errors::kFailed,
                               IsConnected() ? QStringLiteral("no answer in %1 ms").arg(timeoutMs)
                                             : QStringLiteral("not connected to the host"));
    if (answer.value("op").toString() == QLatin1String("error"))
        return Result::Failure(answer.value("error").toString(), answer.value("message").toString());
    return Result::FromJson(answer);
}

bool Client::Subscribe(const QStringList& topics, int maxHz, int timeoutMs)
{
    bool answered = false;
    const QJsonObject answer =
        Request(QJsonObject{{"op", "subscribe"}, {"topics", QJsonArray::fromStringList(topics)}, {"max_hz", maxHz}},
                timeoutMs, answered);
    return answered && answer.value("op").toString() == QLatin1String("subscribed");
}

std::optional<QJsonObject> Client::NextEvent(int timeoutMs)
{
    if (_events.empty() && _device != nullptr)
        WaitFor([this]() { return !_events.empty() || !_connected; }, timeoutMs);
    if (_events.empty())
        return std::nullopt;
    QJsonObject event = _events.front();
    _events.pop_front();
    return event;
}

Result ChooseHost(const std::vector<HostChoice>& choices, size_t& chosen)
{
    std::vector<size_t> resolved;
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i].resolved.ok)
            resolved.push_back(i);
    if (resolved.empty())
    {
        // Every host's own reason, the first one that is not not_found first
        for (const HostChoice& choice : choices)
            if (choice.resolved.error != QLatin1String(errors::kNotFound))
                return choice.resolved;
        return choices.empty() ? Result::Failure(errors::kNotFound, QStringLiteral("no GUI program is running"))
                               : choices.front().resolved;
    }
    if (resolved.size() == 1)
    {
        chosen = resolved.front();
        return Result::Success();
    }
    qint64 latest = 0;
    for (const size_t i : resolved)
    {
        const qint64 at = choices[i].resolved.body.value("focus_at").toInteger();
        if (at > latest)
        {
            latest = at;
            chosen = i;
        }
    }
    if (latest > 0)
        return Result::Success();
    QJsonArray candidates;
    for (const size_t i : resolved)
        candidates.append(choices[i].entry.id + '/' + choices[i].resolved.body.value("target").toString());
    return Result::Failure(errors::kAmbiguous,
                           QStringLiteral("%1 hosts match and none has focus: name one with --host").arg(resolved.size()),
                           QJsonObject{{"candidates", candidates}});
}

}  // namespace guicontrol
