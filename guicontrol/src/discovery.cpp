#include "guicontrol/discovery.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <libproc.h>
#endif

namespace guicontrol::discovery
{

QString Directory()
{
    const QString overridden = qEnvironmentVariable("UNREAL_GUI_CONTROL_DIR");
    if (!overridden.isEmpty())
        return overridden;
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    return base + QStringLiteral("/unreal-ng/gui");
}

QJsonObject Entry::ToJson() const
{
    QJsonObject json{{"id", id},           {"pid", pid},         {"program", program},
                     {"version", version}, {"protocol", protocol}, {"socket", socket},
                     {"started", started}, {"endpoints", endpoints}};
    if (tcpPort > 0)
    {
        json["tcp_port"] = tcpPort;
        json["tcp_bind"] = tcpBind;
    }
    return json;
}

Entry Entry::FromJson(const QJsonObject& json)
{
    Entry entry;
    entry.id = json.value("id").toString();
    entry.pid = json.value("pid").toInteger();
    entry.program = json.value("program").toString();
    entry.version = json.value("version").toString();
    entry.protocol = json.value("protocol").toInt();
    entry.socket = json.value("socket").toString();
    entry.tcpPort = json.value("tcp_port").toInt();
    entry.tcpBind = json.value("tcp_bind").toString();
    entry.started = json.value("started").toInteger();
    entry.endpoints = json.value("endpoints").toArray();
    return entry;
}

std::vector<Entry> List(const QString& directory)
{
    const QDir dir(directory.isEmpty() ? Directory() : directory);
    std::vector<Entry> entries;
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            continue;
        Entry entry = Entry::FromJson(document.object());
        if (entry.id.isEmpty() || entry.socket.isEmpty() || !HostAlive(entry))
            continue;
        entry.file = info.absoluteFilePath();
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.started < b.started; });
    return entries;
}

int RemoveStale(const QString& directory)
{
    const QDir dir(directory.isEmpty() ? Directory() : directory);
    int removed = 0;
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        // Only a readable entry of a dead process: a file being written or someone else's stays
        if (!document.isObject())
            continue;
        const Entry entry = Entry::FromJson(document.object());
        if (entry.pid > 0 && !HostAlive(entry) && QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

bool Write(const QString& file, const Entry& entry, QString* error)
{
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = out.errorString();
        return false;
    }
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(QJsonDocument(entry.ToJson()).toJson(QJsonDocument::Indented));
    if (!out.commit())
    {
        if (error != nullptr)
            *error = out.errorString();
        return false;
    }
    return true;
}

bool ProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

qint64 ProcessStarted(qint64 pid)
{
    if (pid <= 0)
        return 0;
#if defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
        return 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user);
    CloseHandle(process);
    if (!ok)
        return 0;
    // 100 ns ticks since 1601
    const qint64 ticks = (static_cast<qint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return ticks / 10000 - 11644473600000LL;
#elif defined(Q_OS_MACOS)
    proc_bsdinfo info{};
    if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        return 0;
    return static_cast<qint64>(info.pbi_start_tvsec) * 1000 + static_cast<qint64>(info.pbi_start_tvusec) / 1000;
#elif defined(Q_OS_LINUX)
    // The 22nd field of /proc/<pid>/stat: clock ticks after the boot; /proc/stat's btime: the boot, in seconds
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    QFile system(QStringLiteral("/proc/stat"));
    if (!stat.open(QIODevice::ReadOnly) || !system.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = stat.readAll();
    const qsizetype close = line.lastIndexOf(')');  // the command name may hold spaces and parentheses
    if (close < 0)
        return 0;
    const QList<QByteArray> fields = line.mid(close + 2).split(' ');  // from the 3rd field, the state
    if (fields.size() < 20)
        return 0;
    const qint64 ticks = fields[19].toLongLong();
    qint64 boot = 0;
    for (const QByteArray& row : system.readAll().split('\n'))
        if (row.startsWith("btime "))
            boot = row.mid(6).trimmed().toLongLong();
    const long hz = sysconf(_SC_CLK_TCK);
    if (boot <= 0 || hz <= 0)
        return 0;
    return boot * 1000 + ticks * 1000 / hz;
#else
    return 0;
#endif
}

bool HostAlive(const Entry& entry)
{
    if (!ProcessAlive(entry.pid))
        return false;
    // The host writes `started` after its process began: a process that began later took a dead host's pid. The
    // margin covers the clocks' grain (the boot time is in whole seconds on Linux); 0 on either side: the pid alone
    constexpr qint64 kMarginMs = 5000;
    const qint64 began = ProcessStarted(entry.pid);
    return entry.started <= 0 || began <= 0 || began <= entry.started + kMarginMs;
}

}  // namespace guicontrol::discovery
