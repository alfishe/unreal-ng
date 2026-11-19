#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <functional>

class QMimeData;

/// The local files a drag carries, with the reason when it carries none that can be opened.
///
/// Finder drags a file as a file reference URL (`file:///.file/id=<volume>.<inode>`). Qt turns it into a path through
/// the system; when the system cannot resolve it Qt hands over an empty URL, and the drop used to show nothing at all
/// (no drop zones, no refusal) and then "file not found: ''". That happens when Finder's id of the volume is stale: a
/// Finder that has run for weeks named the volume 7594310 while every other process saw 7216937, the inode the same.
/// So the drag pasteboard's own URLs are read where Qt's are empty, a reference is resolved by the system first and
/// then by its inode on the local volumes (only one volume may have it), and what is still not a file says why.
namespace DropPaths
{
inline constexpr int kMaxFiles = 32;  ///< a drop takes the first 32 files

struct Result
{
    QStringList paths;  ///< local files, in drag order
    QString refusal;    ///< why nothing can be opened (empty when `paths` has any)
};

/// `urls`: Qt's; `rawUrls`: the platform's file URLs of the same items, read only when one of Qt's is no local
/// file (it is called then, at most once); `resolve`: a raw URL to its path, "" when it cannot
Result Collect(const QList<QUrl>& urls, const std::function<QStringList()>& rawUrls,
               const std::function<QString(const QString&)>& resolve);

/// A drag's data: Qt's URLs, the drag pasteboard's (macOS) for those Qt left empty
Result FromMime(const QMimeData* mime);

/// A file reference URL's volume and inode; false when `url` is none
bool ParseFileReference(const QString& url, uint64_t* volume, uint64_t* inode);

/// One path among the candidates for an inode, found on several volumes or none: "" (never a guess)
QString UniqueMatch(const QStringList& candidates);

/// Platform (macOS; elsewhere empty): the file URLs on the drag pasteboard, one per item
QStringList NativeDragFileUrls();

/// Platform (macOS; elsewhere ""): a file URL or file reference URL to its path, "" when it names no file
QString Resolve(const QString& url);
}  // namespace DropPaths
