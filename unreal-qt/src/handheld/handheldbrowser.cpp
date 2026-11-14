#include "handheldbrowser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <string>

#include "common/filehelper.h"
#include "emulator/emulator.h"

namespace
{
const QString kRecentKey = QStringLiteral("Handheld/recentFiles");
/// Where the last file was opened from: the browser opens there next time
const QString kLastFolderKey = QStringLiteral("Handheld/lastFolder");
const QString kLastRootKey = QStringLiteral("Handheld/lastRoot");
const QString kLastFileKey = QStringLiteral("Handheld/lastFile");
constexpr int kRecentMax = 20;

QString Tr(const char* text)
{
    return QCoreApplication::translate("HandheldBrowser", text);
}

bool Contains(const std::vector<std::string>& list, const std::string& item)
{
    return std::find(list.begin(), list.end(), item) != list.end();
}

QString Detail(const QFileInfo& info)
{
    const QString size = QLocale().formattedDataSize(info.size(), 0);
    return QStringLiteral("%1 · %2").arg(size, info.lastModified().toString(QStringLiteral("dd MMM yyyy")));
}
}  // namespace

HandheldBrowser::Kind HandheldBrowser::KindOf(const QString& path)
{
    const std::string ext = QFileInfo(path).suffix().toLower().toStdString();
    if (ext.empty())
        return Kind::Root;
    if (Contains(Emulator::SupportedSnapshotExtensions(), ext))
        return Kind::Snapshot;
    if (Emulator::IsTapeExtension(ext))
        return Kind::Tape;
    if (Contains(Emulator::SupportedDiskExtensions(), ext))
        return Kind::Disk;
    return Kind::Root;
}

QString HandheldBrowser::Library()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString library = QDir(documents.isEmpty() ? QDir::homePath() : documents).filePath(QStringLiteral("UnrealNG"));
    QDir().mkpath(QDir(library).filePath(QStringLiteral("Games")));
    QDir().mkpath(QDir(library).filePath(QStringLiteral("Saves")));
    return library;
}

QString HandheldBrowser::Saves()
{
    return QDir(Library()).filePath(QStringLiteral("Saves"));
}

QStringList HandheldBrowser::Recent()
{
    QStringList recent = QSettings().value(kRecentKey).toStringList();
    recent.erase(std::remove_if(recent.begin(), recent.end(), [](const QString& path) { return !QFileInfo::exists(path); }),
                 recent.end());
    return recent;
}

void HandheldBrowser::AddRecent(const QString& path)
{
    QStringList recent = QSettings().value(kRecentKey).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kRecentMax)
        recent.removeLast();
    QSettings().setValue(kRecentKey, recent);
}

void HandheldBrowser::Open(Filter filter)
{
    _filter = filter;
    _root.clear();
    _focusAfterMove = 0;
    const QSettings settings;
    const QString folder = settings.value(kLastFolderKey).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
    {
        listRoots();
        return;
    }
    // The root it came down from, while the folder is still under it (else the folder is its own root)
    const QString root = settings.value(kLastRootKey).toString();
    const QString under = QDir(root).absolutePath() + '/';
    _root = !root.isEmpty() && (QDir(folder) == QDir(root) || QDir(folder).absolutePath().startsWith(under)) ? root : folder;
    listFolder(folder);
    const QString file = settings.value(kLastFileKey).toString();
    for (size_t n = 0; n < _entries.size(); ++n)
        if (!file.isEmpty() && _entries[n].path == file)
            _focusAfterMove = static_cast<int>(n);
}

QString HandheldBrowser::title() const
{
    if (_atRoots)
        return {};
    if (_atRecent)
        return Tr("Recent");
    return _folder;
}

bool HandheldBrowser::accepts(Kind kind) const
{
    switch (_filter)
    {
        case Filter::Any: return kind == Kind::Snapshot || kind == Kind::Tape || kind == Kind::Disk;
        case Filter::Disk: return kind == Kind::Disk;
        case Filter::Tape: return kind == Kind::Tape;
        case Filter::All: return kind != Kind::Root && kind != Kind::Folder && kind != Kind::Up;
    }
    return false;
}

void HandheldBrowser::listRoots()
{
    _atRoots = true;
    _atRecent = false;
    _folder.clear();
    _entries.clear();
    int recent = 0;
    for (const QString& path : Recent())
        if (accepts(KindOf(path) == Kind::Root ? Kind::File : KindOf(path)))
            ++recent;
    if (recent > 0)
        _entries.push_back({Kind::Root, Tr("Recent"), {}, Tr("%1 files").arg(recent)});
    _entries.push_back({Kind::Root, Tr("Library"), Library(), Library()});
    // The software the bundle ships (testsoft, testtapes, testsnapshots beside the executable): demo material
    const QString resources = QString::fromStdString(FileHelper::GetResourcesPath());
    for (const auto& [folder, name] : {std::pair{"testsoft", "Bundled software"}, std::pair{"testtapes", "Bundled tapes"},
                                       std::pair{"testsnapshots", "Bundled snapshots"}})
    {
        const QString path = QDir(resources).filePath(QString::fromLatin1(folder));
        if (QFileInfo(path).isDir())
            _entries.push_back({Kind::Root, Tr(name), path, path});
    }
    // Removable cards (SteamOS mounts the microSD under /run/media/<user>/<label>)
    const QString media = QStringLiteral("/run/media/%1").arg(qEnvironmentVariable("USER"));
    for (const QFileInfo& card : QDir(media).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        _entries.push_back({Kind::Root, Tr("Card %1").arg(card.fileName()), card.absoluteFilePath(), card.absoluteFilePath()});
    _entries.push_back({Kind::Root, Tr("Home"), QDir::homePath(), QDir::homePath()});
}

void HandheldBrowser::listRecent()
{
    _atRoots = false;
    _atRecent = true;
    _entries.clear();
    for (const QString& path : Recent())
    {
        Kind kind = KindOf(path);
        if (kind == Kind::Root)
            kind = Kind::File;
        if (!accepts(kind))
            continue;
        const QFileInfo info(path);
        _entries.push_back({kind, info.fileName(), info.absoluteFilePath(), info.absolutePath()});
    }
}

void HandheldBrowser::listFolder(const QString& folder)
{
    _atRoots = false;
    _atRecent = false;
    _folder = QDir(folder).absolutePath();
    _entries.clear();
    _entries.push_back({Kind::Up, Tr(".. (up)"), {}, {}});
    QDir dir(_folder);
    for (const QFileInfo& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
        _entries.push_back({Kind::Folder, info.fileName(), info.absoluteFilePath(), {}});
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase))
    {
        Kind kind = KindOf(info.fileName());
        if (kind == Kind::Root)
            kind = Kind::File;
        if (accepts(kind))
            _entries.push_back({kind, info.fileName(), info.absoluteFilePath(), Detail(info)});
    }
}

QString HandheldBrowser::Activate(int index)
{
    if (index < 0 || index >= static_cast<int>(_entries.size()))
        return {};
    const Entry entry = _entries[static_cast<size_t>(index)];
    _focusAfterMove = 0;
    switch (entry.kind)
    {
        case Kind::Root:
            if (entry.path.isEmpty())
            {
                listRecent();
                return {};
            }
            _root = entry.path;
            listFolder(entry.path);
            return {};
        case Kind::Folder:
            listFolder(entry.path);
            return {};
        case Kind::Up:
            Up();
            return {};
        case Kind::Snapshot:
        case Kind::Tape:
        case Kind::Disk:
        case Kind::File:
        {
            // Next time the browser opens here (a file from the recent list: in its own folder)
            QSettings settings;
            const QString folder = QFileInfo(entry.path).absolutePath();
            settings.setValue(kLastFolderKey, folder);
            settings.setValue(kLastRootKey, _atRecent ? folder : _root);
            settings.setValue(kLastFileKey, entry.path);
            return entry.path;
        }
    }
    return {};
}

bool HandheldBrowser::Up()
{
    if (_atRoots)
        return false;
    const QString from = _folder;
    if (_atRecent || from.isEmpty() || QDir(from) == QDir(_root))
    {
        // Back to the roots, the focus on the root we came from
        const QString root = _atRecent ? QString() : _root;
        listRoots();
        for (size_t n = 0; n < _entries.size(); ++n)
            if (_entries[n].path == root)
                _focusAfterMove = static_cast<int>(n);
        return true;
    }
    QDir parent(from);
    if (!parent.cdUp())
    {
        listRoots();
        return true;
    }
    listFolder(parent.absolutePath());
    for (size_t n = 0; n < _entries.size(); ++n)
        if (_entries[n].path == QDir(from).absolutePath())
            _focusAfterMove = static_cast<int>(n);
    return true;
}
