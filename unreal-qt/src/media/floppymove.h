#pragma once

#include <QFileInfo>
#include <QString>

#include <string>

/// A floppy image dropped into one drive while another drive holds it. The media manager keeps a writable image in
/// one slot only ("already in slot 'fdd.d'"), so a drop of the disk that went to D by mistake into A was refused in
/// red. The drop moves it instead: the other drive gives it up (unless it has unsaved writes), then the target loads it
namespace FloppyMove
{
/// The two paths name one file (links and spellings resolved); an empty path names none
inline bool SameFile(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return false;
    if (a == b)
        return true;
    const QString left = QFileInfo(QString::fromStdString(a)).canonicalFilePath();
    return !left.isEmpty() && left == QFileInfo(QString::fromStdString(b)).canonicalFilePath();
}

/// The drive (0-3) other than `target` whose image is `file`, -1 when none. `paths`: the drives' image paths
template <typename Paths>
int DriveHolding(const Paths& paths, int target, const std::string& file)
{
    int index = 0;
    for (const auto& path : paths)
    {
        if (index != target && SameFile(path, file))
            return index;
        index++;
    }
    return -1;
}
}  // namespace FloppyMove
