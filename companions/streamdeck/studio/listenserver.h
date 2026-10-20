// --listen: the studio plays the device for ONE host over the v1 emulator wire (sdb/emuwire.h), the way a developer
// drives it with `sdctl --device emu:host:port` for protocol tests. Not the default: normally the studio is a viewer
// of the daemon (DeckClient). The newest host wins; what a host drew stays for the next one, as on a real deck.
#pragma once
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <optional>

#include "decklink.h"
#include "sdb/input.h"

class VirtualDeck;

class ListenServer : public DeckLink
{
    Q_OBJECT
public:
    explicit ListenServer(VirtualDeck* deck, QObject* parent = nullptr);

    bool Listen(quint16 port, bool anyInterface, QString* error);
    [[nodiscard]] bool Connected() const
    {
        return _client && _client->state() == QAbstractSocket::ConnectedState;
    }
    [[nodiscard]] QString Peer() const;
    [[nodiscard]] quint16 Port() const
    {
        return _server.serverPort();
    }

    // Answers to the host's Snapshot / Record requests.
    void SnapshotDone(bool ok);
    void RecordDone(bool ok, const QString& text);

    QString StatusText() const override;
    QString Banner() const override;
    QString RoleText() const override
    {
        return QStringLiteral("listening");
    }
    bool Live() const override
    {
        return Connected();
    }
    nlohmann::json Describe() const override;

signals:
    void ModelRequested(quint16 pid);                    // SetModel: the host mirrors a deck of this model
    void RecordRequested(const QString& path, int fps);  // an empty path = stop

private:
    void OnNewConnection();
    void OnReadyRead();
    void SendFrame(quint8 type, const QByteArray& payload);
    void SendHello();

    VirtualDeck* _deck;
    QTcpServer _server;
    QPointer<QTcpSocket> _client;
    QByteArray _rx;
    std::optional<sdb::InputParser> _parser;  // HostInput: each report carries the whole state, the parser the last
    bool _any = false;
};
