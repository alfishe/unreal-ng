#include "deckclient.h"

#include <QCoreApplication>
#include <algorithm>
#include <span>

#include "virtualdeck.h"

using namespace sdb;
using nlohmann::json;

namespace
{
std::string Str(const json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
QString QStr(const json& j, const char* key)
{
    return QString::fromStdString(Str(j, key));
}
QString NameOf(const Model& m)
{
    return QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size()));
}
// Why the daemon refused a Join, for people.
QString Explain(const std::string& error)
{
    if (error == "version")
        return QStringLiteral("unreal-streamdeck speaks another device wire version: update both together");
    if (error == "bad_role")
        return QStringLiteral("unreal-streamdeck does not know this role");
    if (error == "bad_model")
        return QStringLiteral("unreal-streamdeck does not know this model");
    if (error == "bad_request")
        return QStringLiteral("unreal-streamdeck could not read the Join");
    return QStringLiteral("unreal-streamdeck refused the Join: %1").arg(QString::fromStdString(error));
}
}  // namespace

DeckClient::DeckClient(VirtualDeck* deck, QObject* parent) : DeckLink(parent), _deck(deck)
{
    _retry.setSingleShot(true);
    connect(&_retry, &QTimer::timeout, this, &DeckClient::Connect);
    connect(_deck, &VirtualDeck::InputReport, this, [this](const QByteArray& r) {
        if (_state == State::Joined)  // a click without a deck to go to is dropped
            Send(wire::Input, r);
    });
}

DeckClient::~DeckClient()
{
    CloseSocket(true);
}

void DeckClient::SetAddress(const QString& host, quint16 port)
{
    _host = host;
    _port = port;
}

void DeckClient::SetRetryDelays(int firstMs, int maxMs)
{
    _retryFirstMs = std::max(1, firstMs);
    _retryMaxMs = std::max(_retryFirstMs, maxMs);
    _retryMs = _retryFirstMs;
}

const char* DeckClient::StateName(State s)
{
    switch (s)
    {
        case State::Stopped:
            return "stopped";
        case State::Connecting:
            return "connecting";
        case State::Joining:
            return "joining";
        case State::Waiting:
            return "waiting";
        case State::Joined:
            return "joined";
        case State::Failed:
            return "failed";
    }
    return "?";
}

QString DeckClient::ModelToken(const Model& m)
{
    for (const char* alias : {"plusxl", "plus", "xl", "xl2", "classic", "neo", "mini", "mini2", "original", "original2",
                              "scissor", "pedal", "module6", "module15", "module32", "discord"})
        if (FindModelByName(alias) == &m)
            return QString::fromLatin1(alias);
    return QStringLiteral("%1").arg(m.pid, 4, 16, QLatin1Char('0'));
}

QByteArray DeckClient::JoinPayload(const Role& role, const QString& serial)
{
    json j{{"v", wire::kVersion}};
    if (role.own)
    {
        j["role"] = "own";
        j["model"] = role.model.toStdString();
        if (!serial.isEmpty())
            j["serial"] = serial.toStdString();
    }
    else
    {
        j["role"] = "mirror";
        j["deck"] = role.deck.isEmpty() ? std::string("first") : role.deck.toStdString();
    }
    j["name"] = "Stream Deck Studio";
    j["pid"] = QCoreApplication::applicationPid();
    return QByteArray::fromStdString(j.dump());
}

void DeckClient::Start(const Role& role)
{
    _role = role;
    _error.clear();
    _deckSerial.clear();  // what the last Welcome said no longer holds
    _target.clear();
    _busyBy.clear();
    _real = _deckConnected = false;
    if (_role.own)
    {  // the window shows the chosen model while it joins
        const Model* m = FindModelByName(_role.model.toStdString());
        if (m && m != &_deck->GetModel())
            _deck->SetModel(*m);
    }
    CloseSocket(true);
    _retryMs = _retryFirstMs;
    Connect();
}

void DeckClient::Stop()
{
    _retry.stop();
    CloseSocket(true);
    SetState(State::Stopped);
}

void DeckClient::SetState(State s)
{
    if (s == _state)
        return;
    _state = s;
    emit StateChanged();
}

void DeckClient::Connect()
{
    _retry.stop();
    CloseSocket(false);
    _reader = {};
    auto* s = new QTcpSocket(this);
    _socket = s;
    connect(s, &QTcpSocket::connected, this, &DeckClient::OnConnected);
    connect(s, &QTcpSocket::readyRead, this, &DeckClient::OnReadyRead);
    connect(s, &QTcpSocket::disconnected, this, &DeckClient::OnDisconnected);
    connect(s, &QTcpSocket::errorOccurred, this, [this, s](QAbstractSocket::SocketError) {
        if (s == _socket && s->state() == QAbstractSocket::UnconnectedState)
            OnDisconnected();  // refused or lost before `disconnected` could come
    });
    SetState(State::Connecting);
    s->connectToHost(_host, _port);
}

void DeckClient::ScheduleRetry()
{
    _retry.start(_retryMs);
    _retryMs = std::min(_retryMs * 2, _retryMaxMs);
}

void DeckClient::CloseSocket(bool leave)
{
    QTcpSocket* s = _socket;
    if (!s)
        return;
    _socket = nullptr;  // its signals from here on are ignored
    s->disconnect(this);
    if (leave && s->state() == QAbstractSocket::ConnectedState)
    {
        s->write(QByteArray::fromStdString(wire::EncodeFrame(wire::Leave, {})));
        s->flush();
        if (s->bytesToWrite() > 0)
            s->waitForBytesWritten(200);  // only when closing: the daemon then sees a clean disconnect
        // Read what the daemon sent meanwhile (a mirror gets images all the time): closing with unread data makes
        // the stack send a reset instead of a FIN, and on Windows a reset discards what the daemon has received but
        // not read yet, the Leave with it.
        s->waitForReadyRead(0);
        s->readAll();
    }
    s->abort();
    s->deleteLater();
    if (_inReplay)
        _deck->SetQuiet(false);
    _inReplay = false;
}

void DeckClient::OnConnected()
{
    if (sender() != _socket)
        return;
    _socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    SetState(State::Joining);
    Send(wire::Join, JoinPayload(_role, _deck->Serial()));
}

void DeckClient::OnDisconnected()
{
    if (!_socket || sender() != _socket)
        return;
    const bool wasLive = _state == State::Joined || _state == State::Waiting;
    CloseSocket(false);
    _deck->ClearRemote();
    _deck->SetHostDrawsPresses(false);
    _deckConnected = false;
    if (_state == State::Stopped || _state == State::Failed)
    {
        emit StateChanged();
        return;
    }
    if (wasLive)
        emit Info(QStringLiteral("unreal-streamdeck went away: retrying"));
    SetState(State::Connecting);
    ScheduleRetry();
}

void DeckClient::Send(std::uint8_t type, const QByteArray& payload)
{
    if (!_socket || _socket->state() != QAbstractSocket::ConnectedState)
        return;
    _socket->write(QByteArray::fromStdString(
        wire::EncodeFrame(type, std::string_view(payload.constData(), static_cast<std::size_t>(payload.size())))));
}

void DeckClient::OnReadyRead()
{
    if (sender() != _socket)
        return;
    const QByteArray bytes = _socket->readAll();
    _reader.Feed(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    wire::Frame f;
    while (_socket && _reader.Next(f))
        HandleFrame(f);
    if (_socket && _reader.Broken())
    {
        _error = QStringLiteral("broken frame from unreal-streamdeck");
        emit Info(_error);
        _socket->abort();  // -> OnDisconnected -> retry
    }
}

void DeckClient::HandleFrame(const wire::Frame& f)
{
    const QByteArray payload = QByteArray::fromStdString(f.payload);
    if (_inReplay)
        ++_replayFrames;
    switch (f.type)
    {
        case wire::Welcome:
            HandleWelcome(f.payload);
            break;
        case wire::Output:
            _deck->Output(payload);
            break;
        case wire::SetFeature:
            _deck->SetFeature(payload);
            break;
        case wire::GetFeature:
            if (_role.own)  // a mirror's deck answers itself, on USB
                Send(wire::FeatureReply, _deck->GetFeature(payload));
            break;
        case wire::HostInput: {
            if (!_parser)
                _parser.emplace(_deck->GetModel());
            const auto* p = reinterpret_cast<const std::uint8_t*>(f.payload.data());
            for (const auto& e : _parser->Feed(std::span(p, f.payload.size())))
                _deck->ShowRemote(e);
            emit HostInput(payload);
            break;
        }
        case wire::HostNote:
            emit HostNote(QString::fromStdString(f.payload));
            break;
        case wire::HostPage:
            _deck->SetHostDrawsPresses(true);  // a scenario runtime draws its pressed keys itself
            emit HostPage(payload);
            break;
        case wire::DeckState:
            HandleDeckState(f.payload);
            break;
        case wire::ReplayBegin:
            _inReplay = true;
            _replayFrames = 0;
            _deck->SetQuiet(true);
            _deck->ClearScreen();  // the replay is the whole picture
            break;
        case wire::ReplayEnd:
            if (_inReplay)
            {
                _inReplay = false;
                _deck->SetQuiet(false);
                emit Info(QStringLiteral("picture replayed: %1 reports").arg(_replayFrames - 1));
            }
            break;
        case wire::Control:
            HandleControl(f.payload);
            break;
        default:
            break;  // a newer daemon's frame
    }
}

void DeckClient::HandleWelcome(const std::string& payload)
{
    const json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        _error = QStringLiteral("unreadable Welcome from unreal-streamdeck");
        emit Info(_error);
        return;
    }
    const std::string error = Str(j, "error");
    _daemon = QStr(j, "daemon");
    if (error == "no_deck")
    {
        _error.clear();
        _deckSerial.clear();
        _deckConnected = false;
        _retryMs = _retryFirstMs;
        emit Info(QStringLiteral("unreal-streamdeck has no Stream Deck: waiting for one"));
        if (!j.value("waiting", false))
        {                                // a daemon that does not keep a waiting mirror: ask again later
            _state = State::Connecting;  // OnDisconnected retries
            if (_socket)
                _socket->disconnectFromHost();
            return;
        }
        SetState(State::Waiting);
        return;
    }
    if (!error.empty())
    {
        _error = Explain(error);
        emit Info(_error);
        SetState(State::Failed);  // the daemon closes the connection; no retry until Start()
        return;
    }
    const Model* m = nullptr;
    if (j.contains("pid") && j["pid"].is_number_integer())
        m = FindModel(kElgatoVid, static_cast<std::uint16_t>(j["pid"].get<int>()));
    if (!m)
        m = FindModelByName(Str(j, "model"));
    if (!m)
    {
        _error = QStringLiteral("unreal-streamdeck's deck is a model this studio does not know (%1)")
                     .arg(QString::fromStdString(j.dump()));
        emit Info(_error);
        SetState(State::Failed);
        CloseSocket(true);
        return;
    }
    if (m != &_deck->GetModel())
        _deck->SetModel(*m);  // a mirror follows the deck
    _parser.emplace(*m);
    _deckSerial = QStr(j, "deck");
    if (!_deckSerial.isEmpty())
        _deck->SetSerial(_deckSerial);
    _real = j.value("real", false);
    _deckConnected = true;
    _error.clear();
    _retryMs = _retryFirstMs;
    _deck->ClearRemote();
    _deck->SetHostDrawsPresses(false);  // until a page shows a scenario runtime
    if (_deck->ShowingLogo())
        _deck->ClearScreen();
    emit Info(QStringLiteral("joined %1 %2 (%3%4)")
                  .arg(NameOf(*m), _deckSerial, _role.own ? QStringLiteral("own") : QStringLiteral("mirror"),
                       _real ? QStringLiteral(", real deck") : QString()));
    _state = State::Joined;
    emit StateChanged();
}

void DeckClient::HandleDeckState(const std::string& payload)
{
    const json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return;
    if (j.contains("connected") && j["connected"].is_boolean())
    {
        const bool c = j["connected"].get<bool>();
        if (c != _deckConnected)
            emit Info(c ? QStringLiteral("deck %1 is back").arg(_deckSerial)
                        : QStringLiteral("deck %1 unplugged").arg(_deckSerial));
        _deckConnected = c;
        if (!c)
            _deck->ClearRemote();
    }
    if (j.contains("target"))
        _target = QStr(j, "target");
    if (j.contains("busy_by"))
        _busyBy = QStr(j, "busy_by");
    emit StateChanged();
}

void DeckClient::HandleControl(const std::string& payload)
{
    const json j = json::parse(payload, nullptr, false);
    const std::string op = j.is_object() ? Str(j, "op") : std::string();
    if (op == "snapshot" && !Str(j, "path").empty())
        emit SnapshotRequested(QStr(j, "path"));
    else
        emit Info(QStringLiteral("control request not supported: %1").arg(QString::fromStdString(payload)));
}

QString DeckClient::RoleText() const
{
    return _role.own ? QStringLiteral("own") : QStringLiteral("mirror");
}

QString DeckClient::Banner() const
{
    switch (_state)
    {
        case State::Stopped:
            return QStringLiteral("not connected to unreal-streamdeck");
        case State::Connecting:
        case State::Joining:
            return QStringLiteral("waiting for unreal-streamdeck on %1").arg(Address());
        case State::Waiting:
            return QStringLiteral("waiting for a Stream Deck: attach one, or pick an own deck (Deck menu)");
        case State::Failed:
            return _error;
        case State::Joined:
            return _deckConnected ? QString() : QStringLiteral("deck %1 unplugged").arg(_deckSerial);
    }
    return {};
}

QString DeckClient::StatusText() const
{
    QString s = QStringLiteral("unreal-streamdeck %1: %2").arg(Address(), QString::fromLatin1(StateName(_state)));
    if (_state == State::Joined)
    {
        s += QStringLiteral(", %1 %2").arg(_role.own ? QStringLiteral("own deck") : QStringLiteral("mirror of"),
                                           _deckSerial);
        if (!_deckConnected)
            s += QStringLiteral(" (unplugged)");
        if (!_target.isEmpty())
            s += QStringLiteral(", target %1").arg(_target);
    }
    return s;
}

json DeckClient::Describe() const
{
    json j{{"mode", "client"},
           {"daemon", Address().toStdString()},
           {"state", StateName(_state)},
           {"role", _role.own ? "own" : "mirror"},
           {"deck", _deckSerial.toStdString()},
           {"model", ModelToken(_deck->GetModel()).toStdString()},
           {"model_name", std::string(_deck->GetModel().name)},
           {"real", _real},
           {"deck_connected", _deckConnected},
           {"target", _target.toStdString()},
           {"busy_by", _busyBy.toStdString()},
           {"daemon_version", _daemon.toStdString()},
           {"in_replay", _inReplay}};
    if (_role.own)
        j["requested_model"] = _role.model.toStdString();
    else
        j["requested_deck"] = _role.deck.toStdString();
    if (!_error.isEmpty())
        j["error"] = _error.toStdString();
    return j;
}
