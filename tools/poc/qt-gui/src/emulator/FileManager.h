#pragma once

#include <QString>
#include <map>
#include <string>

enum SupportedFileCategoriesEnum
{
    FileUnknown = 0,
    FileROM,
    FileSnapshot,
    FileTape,
    FileDisk,
    FileArchive,
    FileSymbol
};

typedef std::map<std::string, SupportedFileCategoriesEnum> PatternCategoryMap;

class FileManager
{
public:
    static PatternCategoryMap _extensions;
    static SupportedFileCategoriesEnum determineFileCategoryByExtension(const QString& filepath);
};
