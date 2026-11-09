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
        if (entry.id.isEmpty() || entry.socket.isEmpty() || !ProcessAlive(entry.pid))
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
        if (entry.pid > 0 && !ProcessAlive(entry.pid) && QFile::remove(info.absoluteFilePath()))
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

}  // namespace guicontrol::discovery
