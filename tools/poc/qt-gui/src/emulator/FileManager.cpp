#include "FileManager.h"
#include <QFileInfo>

PatternCategoryMap FileManager::_extensions =
{
    { "rom", FileROM },
    { "bin", FileROM },

    { "sna", FileSnapshot },
    { "z80", FileSnapshot },

    { "tap", FileTape },
    { "tzx", FileTape },

    { "trd", FileDisk },
    { "scl", FileDisk },
    { "udi", FileDisk },
    { "fdi", FileDisk },

    { "gz", FileArchive },
    { "tar", FileArchive },
    { "zip", FileArchive },
    { "rar", FileArchive },
    { "7z", FileArchive },

    { "map", FileSymbol },
    { "sym", FileSymbol }
};

SupportedFileCategoriesEnum FileManager::determineFileCategoryByExtension(const QString& filepath)
{
    QFileInfo fileInfo(filepath);
    if (!fileInfo.suffix().isEmpty())
    {
        QString extension = fileInfo.suffix().toLower();
        auto match = _extensions.find(extension.toStdString());
        if (match != _extensions.end())
        {
            return match->second;
        }
    }
    return FileUnknown;
}
