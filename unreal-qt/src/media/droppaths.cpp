#include "media/droppaths.h"

#include <QCoreApplication>
#include <QMimeData>
#include <QtGlobal>

namespace DropPaths
{
Result Collect(const QList<QUrl>& urls, const std::function<QStringList()>& rawUrls,
               const std::function<QString(const QString&)>& resolve)
{
    Result result;
    QStringList raw;
    bool rawRead = false;
    int unresolved = 0;
    QString foreign;  // the first URL that is no file at all (http:, a remote IDE's own scheme)
    const int count = static_cast<int>(urls.size());
    for (int i = 0; i < count && result.paths.size() < kMaxFiles; i++)
    {
        const QUrl& url = urls.at(i);
        if (url.isLocalFile() && !url.toLocalFile().isEmpty())
        {
            result.paths << url.toLocalFile();
            continue;
        }
        if (!url.isEmpty() && !url.isLocalFile())
        {
            if (foreign.isEmpty())
                foreign = url.toString();
            continue;
        }
        // Qt could not make a path of it (a file reference the system did not resolve): the item's own URL
        if (!rawRead)
        {
            raw = rawUrls ? rawUrls() : QStringList();
            rawRead = true;
        }
        const QString path = i < raw.size() && resolve ? resolve(raw.at(i)) : QString();
        if (path.isEmpty())
            unresolved++;
        else
            result.paths << path;
    }
    if (!result.paths.isEmpty())
        return result;
    if (unresolved > 0)
        result.refusal = QCoreApplication::translate(
            "DropPaths", "the system cannot find the dragged file (Finder's view of its disk may be out of date: "
                         "relaunch Finder, or open the file with File > Open)");
    else if (!foreign.isEmpty())
        result.refusal = QCoreApplication::translate("DropPaths", "not a file on this computer: %1").arg(foreign);
    return result;
}

Result FromMime(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls())
        return {};
    return Collect(mime->urls(), &NativeDragFileUrls, &Resolve);
}

bool ParseFileReference(const QString& url, uint64_t* volume, uint64_t* inode)
{
    static const QString kPrefix = QStringLiteral("file:///.file/id=");
    if (!url.startsWith(kPrefix))
        return false;
    QString ids = url.mid(kPrefix.size());
    if (ids.endsWith(QLatin1Char('/')))
        ids.chop(1);
    const qsizetype dot = ids.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == ids.size() - 1)
        return false;
    bool okVolume = false;
    bool okInode = false;
    const uint64_t v = ids.left(dot).toULongLong(&okVolume);
    const uint64_t n = ids.mid(dot + 1).toULongLong(&okInode);
    if (!okVolume || !okInode)
        return false;
    if (volume)
        *volume = v;
    if (inode)
        *inode = n;
    return true;
}

QString UniqueMatch(const QStringList& candidates)
{
    return candidates.size() == 1 ? candidates.front() : QString();
}

#ifndef Q_OS_MACOS
// macOS has its own (platform/macos/droppaths_macos.mm): no other system drags file references
QStringList NativeDragFileUrls()
{
    return {};
}

QString Resolve(const QString& url)
{
    const QUrl parsed(url);
    return parsed.isLocalFile() ? parsed.toLocalFile() : QString();
}
#endif
}  // namespace DropPaths
