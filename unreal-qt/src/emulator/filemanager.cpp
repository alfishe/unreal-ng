#include "filemanager.h"

#include <QFileInfo>

#include "emulator/emulator.h"

PatternCategoryMap FileManager::_extensions =
{
    { "rom", SupportedFileCategoriesEnum::FileROM },
    { "bin", SupportedFileCategoriesEnum::FileROM },
    { "bin", SupportedFileCategoriesEnum::FileROM },

    { "sna", SupportedFileCategoriesEnum::FileSnapshot },
    { "z80", SupportedFileCategoriesEnum::FileSnapshot },

    { "trd", SupportedFileCategoriesEnum::FileDisk },
    { "scl", SupportedFileCategoriesEnum::FileDisk },
    { "udi", SupportedFileCategoriesEnum::FileDisk },
    { "fdi", SupportedFileCategoriesEnum::FileDisk },
    { "dsk", SupportedFileCategoriesEnum::FileDisk },
    { "td0", SupportedFileCategoriesEnum::FileDisk },
    { "mgt", SupportedFileCategoriesEnum::FileDisk },
    { "img", SupportedFileCategoriesEnum::FileDisk },

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

        // Tapes: every format the tape loaders read (the registry is the list)
        auto match = _extensions.find(extension.toStdString());
        if (Emulator::IsTapeExtension(extension.toStdString()))
        {
            result = SupportedFileCategoriesEnum::FileTape;
        }
        else if (match != _extensions.end())
        {
            result = match->second;
        }
    }

    return result;
}
