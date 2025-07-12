#include "filemanager.h"

#include <QFileInfo>

PatternCategoryMap FileManager::_extensions =
{
    { "rom", SupportedFileCategoriesEnum::FileROM },
    { "bin", SupportedFileCategoriesEnum::FileROM },
    { "bin", SupportedFileCategoriesEnum::FileROM },

    { "sna", SupportedFileCategoriesEnum::FileSnapshot },
    { "z80", SupportedFileCategoriesEnum::FileSnapshot },
    { "uns", SupportedFileCategoriesEnum::FileSnapshot }, // Unreal NG Snapshot

    { "tap", SupportedFileCategoriesEnum::FileTape },
    { "tzx", SupportedFileCategoriesEnum::FileTape },

    { "trd", SupportedFileCategoriesEnum::FileDisk },
    { "scl", SupportedFileCategoriesEnum::FileDisk },
    { "udi", SupportedFileCategoriesEnum::FileDisk },
    { "fdi", SupportedFileCategoriesEnum::FileDisk },

    { "gz", SupportedFileCategoriesEnum::FileArchive },
    { "tar", SupportedFileCategoriesEnum::FileArchive },
    { "zip", SupportedFileCategoriesEnum::FileArchive },
    { "rar", SupportedFileCategoriesEnum::FileArchive },
    { "7z", SupportedFileCategoriesEnum::FileArchive },

    { "map", SupportedFileCategoriesEnum::FileSymbol },
    { "sym", SupportedFileCategoriesEnum::FileSymbol }
};

/// Detect file type/category based on its extension
/// List of supported types is in FileManager::_extensions map
SupportedFileCategoriesEnum FileManager::determineFileCategoryByExtension(QString& filepath)
{
    SupportedFileCategoriesEnum result = SupportedFileCategoriesEnum::FileUnknown;

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
