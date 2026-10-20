// The studio as a viewer of the daemon (device wire v2, docs/inprogress/2026-10-19-streamdeck-integration/
// device-wire.md): connects to unreal-streamdeck, joins as a mirror of a real deck or as a deck of its own, applies
// what the daemon sends to the software deck and sends the deck's input back. Reconnects with a backoff while the
// daemon is not there. Qt sockets on the GUI thread, no threads of its own.
#pragma once
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <optional>

#include "decklink.h"
#include "sdb/devicewire.h"
#include "sdb/input.h"

class VirtualDeck;

class DeckClient : public DeckLink
{
    Q_OBJECT
public:
    enum class State
    {
        Stopped,     // not started, or Stop()
        Connecting,  // no daemon yet: retrying with a backoff
        Joining,     // connected, Join sent, no Welcome yet
        Waiting,     // mirror without a deck: the daemon answers again when one is attached
        Joined,      // Welcome: the deck is live
        Failed       // the daemon refused the Join (version, role, model): no retry until Start()
    };

    struct Role
    {
        bool own = false;
        QString deck = QStringLiteral("first");  // mirror: a serial or "first"
        QString model;                           // own: a model name (plusxl, neo, ...) or PID
        bool operator==(const Role&) const = default;
    };

    explicit DeckClient(VirtualDeck* deck, QObject* parent = nullptr);
    ~DeckClient() override;

    void SetAddress(const QString& host, quint16 port);
    void SetRetryDelays(int firstMs, int maxMs);  // the backoff while there is no daemon
    void Start(const Role& role);                 // connects (or rejoins with this role)
    void Stop();                                  // Leave, close, no reconnect

    [[nodiscard]] State GetState() const
    {
        return _state;
    }
    [[nodiscard]] const Role& GetRole() const
    {
        return _role;
    }
    [[nodiscard]] QString Error() const
    {
        return _error;
    }
    [[nodiscard]] QString DeckSerial() const
    {
        return _deckSerial;
    }
    [[nodiscard]] bool Real() const
    {
        return _real;
    }
    [[nodiscard]] bool DeckConnected() const
    {
        return _deckConnected;
    }
    [[nodiscard]] QString Target() const
    {
        return _target;
    }
    [[nodiscard]] bool InReplay() const
    {
        return _inReplay;
    }
    [[nodiscard]] QString Address() const
    {
        return QStringLiteral("%1:%2").arg(_host).arg(_port);
    }

    // The Join payload for a role (JSON).
    [[nodiscard]] static QByteArray JoinPayload(const Role& role, const QString& serial);
    // A model as the daemon's Join takes it: its short name when it has one, else its PID in hex.
    [[nodiscard]] static QString ModelToken(const sdb::Model& m);
    [[nodiscard]] static const char* StateName(State s);

    QString StatusText() const override;
    QString Banner() const override;
    QString RoleText() const override;
    bool Live() const override
    {
        return _state == State::Joined;
    }
    nlohmann::json Describe() const override;

private:
    void Connect();
    void ScheduleRetry();
    void OnConnected();
    void OnReadyRead();
    void OnDisconnected();
    void HandleFrame(const sdb::wire::Frame& f);
    void HandleWelcome(const std::string& payload);
    void HandleDeckState(const std::string& payload);
    void HandleControl(const std::string& payload);
    void Send(std::uint8_t type, const QByteArray& payload);
    void SetState(State s);
    void CloseSocket(bool leave);

    VirtualDeck* _deck;
    QPointer<QTcpSocket> _socket;
    QTimer _retry;
    sdb::wire::FrameReader _reader;
    std::optional<sdb::InputParser> _parser;  // HostInput reports of the deck's model
    QString _host = QStringLiteral("127.0.0.1");
    quint16 _port = sdb::wire::kDefaultPort;
    int _retryFirstMs = 250, _retryMaxMs = 4000, _retryMs = 250;
    Role _role;
    State _state = State::Stopped;
    QString _error, _deckSerial, _target, _busyBy, _daemon;
    bool _real = false, _deckConnected = false, _inReplay = false;
    int _replayFrames = 0;
};
