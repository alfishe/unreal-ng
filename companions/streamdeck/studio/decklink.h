// What feeds the software deck: a session with the daemon over the device wire v2 (DeckClient, the default) or, for
// protocol tests only, a v1 server for one host (ListenServer, --listen). The window and the automation see this.
#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <nlohmann/json.hpp>

class DeckLink : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    [[nodiscard]] virtual QString StatusText() const = 0;       // the status bar
    [[nodiscard]] virtual QString Banner() const = 0;           // a line over the deck; empty = the deck is live
    [[nodiscard]] virtual QString RoleText() const = 0;         // the window title: mirror, own, listening
    [[nodiscard]] virtual bool Live() const = 0;                // something drives the deck now
    [[nodiscard]] virtual nlohmann::json Describe() const = 0;  // GET /api/v1/connection

signals:
    void StateChanged();
    void HostInput(const QByteArray& report);  // a press on the real deck (lights it here, never sent back)
    void HostNote(const QString& line);        // what the runtime did with the input
    void HostPage(const QByteArray& json);     // what the keys mean (scenario runtime)
    void Info(const QString& line);            // the connection itself (log source Info)
    void SnapshotRequested(const QString& path);
};
