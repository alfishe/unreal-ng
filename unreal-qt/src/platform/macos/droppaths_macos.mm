#include "media/droppaths.h"

#import <AppKit/AppKit.h>

#include <sys/fsgetpath.h>
#include <sys/mount.h>
#include <sys/stat.h>

// The drag pasteboard's file URLs and their paths (media/droppaths.h)

namespace
{
QString FromNs(NSString* string)
{
    return string ? QString::fromNSString(string) : QString();
}

/// The inode on every local volume: the path when exactly one has it as a regular file (a few ms for ~30 mounts)
QString FindInode(uint64_t inode)
{
    struct statfs* mounts = nullptr;
    const int count = getmntinfo(&mounts, MNT_NOWAIT);
    QStringList found;
    for (int i = 0; i < count; i++)
    {
        if (!(mounts[i].f_flags & MNT_LOCAL))
            continue;
        char path[PATH_MAX];
        fsid_t fsid = mounts[i].f_fsid;
        if (fsgetpath(path, sizeof(path), &fsid, inode) <= 0)
            continue;
        struct stat st{};
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && static_cast<uint64_t>(st.st_ino) == inode &&
            !found.contains(QString::fromUtf8(path)))
            found << QString::fromUtf8(path);
    }
    return DropPaths::UniqueMatch(found);
}
}  // namespace

QStringList DropPaths::NativeDragFileUrls()
{
    QStringList urls;
    @autoreleasepool
    {
        NSPasteboard* pasteboard = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
        for (NSPasteboardItem* item in pasteboard.pasteboardItems)
            urls << FromNs([item stringForType:NSPasteboardTypeFileURL]);
    }
    return urls;
}

QString DropPaths::Resolve(const QString& url)
{
    if (url.isEmpty())
        return {};
    @autoreleasepool
    {
        NSURL* nsUrl = [NSURL URLWithString:url.toNSString()];
        if (nsUrl.isFileURL)
        {
            NSURL* pathUrl = nsUrl.filePathURL;  // a file reference to its path; a path URL stays itself
            if (pathUrl && pathUrl.path.length > 0 && [[NSFileManager defaultManager] fileExistsAtPath:pathUrl.path])
                return FromNs(pathUrl.path);
        }
    }
    // A reference the system cannot resolve (its volume id is stale): the inode on the local volumes
    uint64_t volume = 0;
    uint64_t inode = 0;
    if (!ParseFileReference(url, &volume, &inode))
        return {};
    return FindInode(inode);
}
