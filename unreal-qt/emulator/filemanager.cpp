#include "filemanager.h"

#include <QFileInfo>

PatternCategoryMap FileManager::_extensions =
{
    { "rom", SupportedFileCategoriesEnum::ROM },
    { "bin", SupportedFileCategoriesEnum::ROM },
    { "bin", SupportedFileCategoriesEnum::ROM },

    { "sna", SupportedFileCategoriesEnum::Snapshot },
    { "z80", SupportedFileCategoriesEnum::Snapshot },

    { "tap", SupportedFileCategoriesEnum::Tape },
    { "tzx", SupportedFileCategoriesEnum::Tape },

    { "trd", SupportedFileCategoriesEnum::Disk },
    { "scl", SupportedFileCategoriesEnum::Disk },

    { "gz", SupportedFileCategoriesEnum::Archive },
    { "tar", SupportedFileCategoriesEnum::Archive },
    { "zip", SupportedFileCategoriesEnum::Archive },
    { "rar", SupportedFileCategoriesEnum::Archive },
    { "7z", SupportedFileCategoriesEnum::Archive },
};

SupportedFileCategoriesEnum FileManager::determineFileCategoryByExtension(QString filepath)
{
    SupportedFileCategoriesEnum result = SupportedFileCategoriesEnum::Unknown;

    QFileInfo fileInfo(filepath);
    if (!fileInfo.suffix().isEmpty())
    {
        QString extension = fileInfo.suffix().toLower();

        auto match = _extensions.find(extension.toStdString());
        if (match != _extensions.end())
        {
            result = match->second;
        }
    }

    return result;
}
