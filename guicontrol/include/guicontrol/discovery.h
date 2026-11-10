#pragma once

/// @file discovery.h
/// @brief How clients find the running hosts (design §4.1): one owner-only JSON file per host in the per-user
/// runtime directory, `unreal-ng/gui/<host id>.json`, written when the host starts and removed when it stops.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace guicontrol::discovery
{

/// `UNREAL_GUI_CONTROL_DIR` when set (tests, a second set of programs), else `<runtime dir>/unreal-ng/gui`
QString Directory();

struct Entry
{
    QString id;       ///< the host's id: the pid, or "<pid>-<n>" for a second host in one process
    qint64 pid = 0;
    QString program;  ///< "unreal-qt"
    QString version;
    int protocol = 0;
    QString socket;   ///< the local socket's full name (QLocalSocket::connectToServer)
    int tcpPort = 0;  ///< 0: no TCP listener
    QString tcpBind;
    qint64 started = 0;     ///< ms since the epoch: with the pid, tells a live host from a reused pid
    QJsonArray endpoints;   ///< the emulator endpoints the program is bound to
    QString file;           ///< the discovery file itself

    QJsonObject ToJson() const;
    static Entry FromJson(const QJsonObject& json);
};

/// The hosts of `directory` (default: Directory()) whose process is alive, by id; unreadable files are skipped
std::vector<Entry> List(const QString& directory = {});

/// Writes the entry owner-only, atomically
/// Removes the entries of processes that are gone (a host killed before it could remove its own); the count removed
int RemoveStale(const QString& directory = {});
bool Write(const QString& file, const Entry& entry, QString* error = nullptr);

bool ProcessAlive(qint64 pid);

/// When the process began, ms since the epoch; 0 when the system does not say
qint64 ProcessStarted(qint64 pid);

/// The entry's process is alive and is the host that wrote it: not a later process given a dead host's pid
bool HostAlive(const Entry& entry);

}  // namespace guicontrol::discovery
