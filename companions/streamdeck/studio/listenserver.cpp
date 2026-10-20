#include "listenserver.h"

#include <QtEndian>

#include "sdb/emuwire.h"
#include "virtualdeck.h"

using namespace sdb;

namespace
{
constexpr int kHeader = 3;  // u8 type, u16 length
}

ListenServer::ListenServer(VirtualDeck* deck, QObject* parent) : DeckLink(parent), _deck(deck)
{
    connect(&_server, &QTcpServer::newConnection, this, &ListenServer::OnNewConnection);
    connect(_deck, &VirtualDeck::InputReport, this, [this](const QByteArray& r) { SendFrame(emuwire::Input, r); });
    connect(_deck, &VirtualDeck::ModelChanged, this, [this] {
        _parser.reset();
        if (Connected())
            SendHello();
    });
}

bool ListenServer::Listen(quint16 port, bool anyInterface, QString* error)
{
    _any = anyInterface;
    if (_server.listen(anyInterface ? QHostAddress::Any : QHostAddress::LocalHost, port))
        return true;
    if (error)
        *error = _server.errorString();
    return false;
}

QString ListenServer::Peer() const
{
    return Connected() ? QStringLiteral("%1:%2").arg(_client->peerAddress().toString()).arg(_client->peerPort())
                       : QString();
}

void ListenServer::OnNewConnection()
{
    while (QTcpSocket* s = _server.nextPendingConnection())
    {
        if (_client)
            _client->disconnectFromHost();  // the newest host wins
        _client = s;
        _rx.clear();
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(s, &QTcpSocket::readyRead, this, &ListenServer::OnReadyRead);
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            if (s == _client)
            {
                _deck->ClearRemote();
                _deck->SetHostDrawsPresses(false);
            }
            s->deleteLater();
            emit Info(QStringLiteral("host disconnected"));
            emit StateChanged();
        });
        _parser.reset();
        _deck->ClearRemote();               // presses of the previous host's real deck must not stay lit
        _deck->SetHostDrawsPresses(false);  // until it shows it is a scenario (its page)
        // the logo must not peek out between the keys a host draws; what a host drew stays for the next one
        if (_deck->ShowingLogo())
            _deck->ClearScreen();
        SendHello();
        emit Info(QStringLiteral("host %1 connected").arg(Peer()));
        emit StateChanged();
    }
}

void ListenServer::SendFrame(quint8 type, const QByteArray& payload)
{
    if (!Connected() || payload.size() > 0xFFFF)
        return;
    QByteArray f(kHeader, '\0');
    f[0] = static_cast<char>(type);
    qToLittleEndian<quint16>(static_cast<quint16>(payload.size()), f.data() + 1);
    _client->write(f + payload);
}

void ListenServer::SnapshotDone(bool ok)
{
    SendFrame(emuwire::SnapshotDone, QByteArray(1, ok ? '\1' : '\0'));
}

void ListenServer::RecordDone(bool ok, const QString& text)
{
    SendFrame(emuwire::RecordDone, QByteArray(1, ok ? '\1' : '\0') + text.toUtf8());
}

void ListenServer::SendHello()
{
    const QByteArray serial = _deck->Serial().toLatin1();
    QByteArray p(6, '\0');
    p[0] = static_cast<char>(emuwire::kVersion);
    qToLittleEndian<quint16>(kElgatoVid, p.data() + 1);
    qToLittleEndian<quint16>(_deck->GetModel().pid, p.data() + 3);
    p[5] = static_cast<char>(serial.size());
    SendFrame(emuwire::Hello, p + serial);
}

void ListenServer::OnReadyRead()
{
    if (!_client || sender() != _client)
        return;
    _rx.append(_client->readAll());
    while (_rx.size() >= kHeader)
    {
        const int len = qFromLittleEndian<quint16>(_rx.constData() + 1);
        if (_rx.size() < kHeader + len)
            break;
        const auto type = static_cast<quint8>(_rx[0]);
        const QByteArray payload = _rx.mid(kHeader, len);
        _rx.remove(0, kHeader + len);
        switch (type)
        {
            case emuwire::Output:
                _deck->Output(payload);
                break;
            case emuwire::SetFeature:
                _deck->SetFeature(payload);
                break;
            case emuwire::GetFeature:
                SendFrame(emuwire::FeatureReply, _deck->GetFeature(payload));
                break;
            case emuwire::HostInput: {
                if (!_parser)
                    _parser.emplace(_deck->GetModel());
                const auto* p = reinterpret_cast<const std::uint8_t*>(payload.constData());
                for (const auto& e : _parser->Feed(std::span(p, static_cast<std::size_t>(payload.size()))))
                    _deck->ShowRemote(e);
                emit HostInput(payload);
                break;
            }
            case emuwire::HostNote:
                emit HostNote(QString::fromUtf8(payload));
                break;
            case emuwire::HostPage:
                _deck->SetHostDrawsPresses(true);
                emit HostPage(payload);
                break;
            case emuwire::HostMirror:
                break;  // the studio draws nothing on a real deck
            case emuwire::Snapshot:
                emit SnapshotRequested(QString::fromUtf8(payload));
                break;
            case emuwire::Record:
                if (!payload.isEmpty())
                    emit RecordRequested(QString::fromUtf8(payload.mid(1)), static_cast<quint8>(payload[0]));
                break;
            case emuwire::SetModel:
                if (payload.size() >= 2)
                    emit ModelRequested(qFromLittleEndian<quint16>(payload.constData()));
                break;
            default:
                break;
        }
    }
}

QString ListenServer::StatusText() const
{
    return QStringLiteral("listening on %1:%2 (v1, --listen): %3")
        .arg(_any ? QStringLiteral("*") : QStringLiteral("127.0.0.1"))
        .arg(Port())
        .arg(Connected() ? QStringLiteral("host %1").arg(Peer()) : QStringLiteral("no host"));
}

QString ListenServer::Banner() const
{
    return {};  // a host draws when it comes; the picture stays between hosts
}

nlohmann::json ListenServer::Describe() const
{
    return {{"mode", "listen"},
            {"port", Port()},
            {"any_interface", _any},
            {"connected", Connected()},
            {"host", Peer().toStdString()},
            {"serial", _deck->Serial().toStdString()},
            {"model", std::string(_deck->GetModel().name)}};
}
